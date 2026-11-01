/*
	File:		hal/host/HostMesh.cpp

	Contains:	The host's LoRa mesh node (HostMesh.h): MeshCore's
				BaseChatMesh on the radio the host gives, run on one thread
				of the host's own.

	Written by:	The reconstruction, from lilyphone1's
				src/apps/mesh_net.cpp (the owner's own code)
*/

#include "HostMesh.h"

#include <MeshCore.h>
#include <Mesh.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/AdvertDataHelpers.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#if defined(ESP_PLATFORM)
extern "C" void esp_fill_random(void* buf, size_t len);
#endif

#ifndef HOST_LARGE_STATIC
#define HOST_LARGE_STATIC
#endif


// ----------------------------------------------------------------------
// The presets: MeshCore's community-suggested radio settings, the list its
// own apps offer (HostMeshPresets.inc, made by tools/mesh/meshpresets.py) -
// the frequency, bandwidth and spreading factor have to match a mesh's to
// hear it (the coding rate travels in each packet's header).  A preset only
// fills the settings in: they are kept as values, and may be set to any the
// radio takes (HostMeshSetRadio).

struct Preset
{
	const char*		name;
	float			frequency, bandwidth;
	int				spreadingFactor, codingRate;
	int				pathHashSize;
};

static const Preset kPresets[] =
{
#include "HostMeshPresets.inc"
};
const long		kPresetCount = (long) (sizeof(kPresets) / sizeof(kPresets[0]));
static const char	kDefaultPreset[] = "Australia (Narrow)";	// (the owner's mesh; else the first)

// version 1 of the kept settings named one of these presets (lilyphone1's)
// by its index; its settings are taken over as they were, except that its
// narrow Australian one becomes the community's (CR 4/7 rather than 4/8)
static const Preset kVersion1Presets[] =
{
	{ "Australia (Narrow)",	916.575f,	62.5f,	7,	8,	1 },
	{ "Aus / NZ",			915.800f,	250.0f,	10,	5,	1 },
	{ "EU / UK",			869.525f,	250.0f,	10,	5,	1 },
	{ "US / Canada",		910.525f,	250.0f,	10,	5,	1 },
};
const int		kTxPowerMax = 22;
const int		kTxPowerMin = -9;			// (the SX1262's range, dBm)

// the bandwidths a LoRa radio takes (the SX126x's), kHz
static const float	kBandwidths[] = { 7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f };
const long			kBandwidthCount = (long) (sizeof(kBandwidths) / sizeof(kBandwidths[0]));

// Every MeshCore node has the same key for the public channel: it is the
// open conversation on the frequency, not a secret
#define PUBLIC_CHANNEL_KEY	"8B3387E9C5CDEA6AC9E5EDBAA115CD72"	// (izOH6cXN6mrJ5e26oRXNcg== in base64)
#define PUBLIC_CHANNEL_NAME	"Public"

const unsigned long	kAdvertPeriodMs = 10 * 60 * 1000;	// this node announced unprompted this often
const unsigned long	kFirstAdvertMs = 3000;				// ... and this long after starting
const unsigned long	kContactsSaveMs = 10 * 1000;		// contacts kept this long after they last changed
const long			kIdleWaitMs = 50;					// the engine's longest wait, nothing to send, on a host that wakes it (HostMeshPlatform's wait)
const int			kSendAttempts = 3;
const int			kMessagesKept = 32;					// received and not yet taken
const int			kSendsKept = 32;


// ----------------------------------------------------------------------
// The clocks.  The mesh clock is the host's once the host knows the time
// (a board learns it from the network after it starts): until then it
// counts on from the last time it was told (lilyphone1's
// VolatileRTCClock, which nothing ever ticked, so its time stood still)

static unsigned long
NowMs(void)
{
	using namespace std::chrono;
	return (unsigned long) duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

class HostMillis : public mesh::MillisecondClock
{
public:
	unsigned long	getMillis() override { return NowMs(); }
};

const uint32_t	kTimeKnown = 1700000000;		// (Nov 2023) a host clock before this does not know the time

class HostRTC : public mesh::RTCClock
{
	uint32_t		fBase = 1715770351;			// MeshCore's own start (May 2024)
	unsigned long	fBaseMs = NowMs();
public:
	uint32_t getCurrentTime() override
	{
		time_t host = time(NULL);
		if ((uint32_t) host >= kTimeKnown)
			return (uint32_t) host;
		return fBase + (uint32_t) ((NowMs() - fBaseMs) / 1000);
	}
	void setCurrentTime(uint32_t t) override
	{
		fBase = t;
		fBaseMs = NowMs();
	}
};

class HostRNG : public mesh::RNG
{
public:
	void random(uint8_t* dest, size_t size) override { HostMeshRandom(dest, size); }
};

static void (*gRandomSource)(void* bytes, size_t size) = NULL;

void
HostMeshSetRandomSource(void (*source)(void* bytes, size_t size))
{
	gRandomSource = source;
}


extern "C" void
HostMeshRandom(void* bytes, size_t size)
{
	if (gRandomSource != NULL)
	{
		gRandomSource(bytes, size);
		return;
	}
#if defined(ESP_PLATFORM)
	// (ESP-IDF's C library has no getentropy for std::random_device)
	esp_fill_random(bytes, size);
#else
	static std::random_device device;
	unsigned char* p = (unsigned char*) bytes;
	for (size_t k = 0; k < size; k++)
		p[k] = (unsigned char) (device() & 0xFF);
#endif
}


// ----------------------------------------------------------------------
// The engine's state, shared under gLock: the snapshot the natives read,
// the queues, the requests

static std::mutex				gLock;
static std::condition_variable	gWake;
static std::atomic<bool>		gWillRun(false);
static std::atomic<bool>		gStarted(false);

static HostMeshStatusInfo		gStatus;
static HOST_LARGE_STATIC HostMeshContact	gContacts[kHostMeshContactsMax];
static long						gContactCount = 0;

static HOST_LARGE_STATIC HostMeshMessage	gMessages[kMessagesKept];
static int						gMessageHead = 0, gMessageCount = 0;

struct Send
{
	long		id;
	int			state;			// HostMeshSendState
	bool		channel;
	char		channelName[kHostMeshNameSize];	// (a channel's: "Public" or one joined)
	uint8_t		key[kHostMeshKeySize];
	char		text[kHostMeshTextSize];
	int			attempt;		// 0: not yet sent
	uint32_t	timestamp;		// the same for every attempt (the receiver drops repeats by it)
	uint32_t	ack;
	unsigned long deadline;
};
static HOST_LARGE_STATIC Send	gSends[kSendsKept];
static long						gNextSendId = 1;

// requests the engine's thread takes up
static bool					gAdvertAsked = false;
static bool					gNameAsked = false;
static char					gNameWanted[kHostMeshNameSize];
static bool					gRadioAsked = false;
static HostMeshRadioSettings	gRadioWanted;
// the channels: Public (MeshCore's slot 0) and those joined, kept in the
// blob "channels"; changed by any thread, given to MeshCore by the engine's
struct KeptChannel
{
	char		name[32];
	uint8_t		secret[32];
	uint8_t		size;			// 16 or 32
	uint8_t		unused[3];
};
const long						kChannelsMagic = 0x4D434831;	// "MCH1"
static HOST_LARGE_STATIC KeptChannel	gChannels[kHostMeshChannelsMax];
static long						gChannelCount = 0;		// (Public included)
static bool						gChannelsChanged = false;
static std::atomic<bool>		gTraceRx(false);
// the position put in this node's adverts (HostMeshSetLocation)
static bool						gHasLocation = false;
static double					gLatitude = 0, gLongitude = 0;
// the last packet read (logRxRaw), and the last channel packet no channel
// could decrypt (logRx)
static uint8_t					gLastRaw[MAX_TRANS_UNIT + 1];
static int						gLastRawLength = 0;
static uint8_t					gUnreadable[MAX_TRANS_UNIT + 1];
static int						gUnreadableLength = 0;
static uint8_t					gMismatched[MAX_TRANS_UNIT + 1];
static int						gMismatchedLength = 0;

// contacts saved (MeshCore's favourites) or forgotten, by key
struct ContactAsk
{
	uint8_t		key[kHostMeshKeySize];
	char		name[kHostMeshNameSize];	// "" to keep the name it has
	bool		saved;
};
const int						kContactAsksMax = 64;
static HOST_LARGE_STATIC ContactAsk	gContactAsks[kContactAsksMax];
static int						gContactAskCount = 0;

// the power
static bool					(*gPowerSwitch)(bool on) = NULL;
static std::atomic<bool>	gPowered(true);
static std::atomic<bool>	gSleepAsked(false);
static std::atomic<bool>	gWakeAsked(false);


static void
HexOf(const uint8_t* bytes, long size, char* hex)
{
	static const char kDigits[] = "0123456789ABCDEF";
	for (long k = 0; k < size; k++)
	{
		hex[2 * k] = kDigits[bytes[k] >> 4];
		hex[2 * k + 1] = kDigits[bytes[k] & 15];
	}
	hex[2 * size] = 0;
}


// ==> whether hex was a whole key
static bool
KeyOf(const char* hex, uint8_t* key)
{
	for (long k = 0; k < kHostMeshKeySize; k++)
	{
		int v = 0;
		for (int n = 0; n < 2; n++)
		{
			char c = hex[2 * k + n];
			int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
			if (d < 0)
				return false;
			v = v * 16 + d;
		}
		key[k] = (uint8_t) v;
	}
	return hex[2 * kHostMeshKeySize] == 0;
}


// text cut to at most size - 1 bytes, at a character's start (UTF-8)
static void
CopyUTF8(char* to, const char* from, size_t size)
{
	size_t n = strlen(from);
	if (n >= size)
	{
		n = size - 1;
		while (n > 0 && (((unsigned char) from[n]) & 0xC0) == 0x80)
			n--;
	}
	memcpy(to, from, n);
	to[n] = 0;
}


// ----------------------------------------------------------------------
// What is kept across restarts

struct KeptSettings
{
	uint32_t	version;
	char		name[kHostMeshNameSize];
	int32_t		region;				// (version 1's preset: read, then kept as -1)
	int32_t		txPower;
	// version 2: the radio's settings themselves (a preset only fills them in)
	float		frequency;			// MHz
	float		bandwidth;			// kHz
	int32_t		spreadingFactor;
	int32_t		codingRate;
	int32_t		pathHashSize;		// the mesh's: bytes of each hop's hash in a flood's path
};
const uint32_t	kSettingsVersion = 2;
const long		kSettingsVersion1Size = 4 + kHostMeshNameSize + 4 + 4;

// a contact as kept: MeshCore's ContactInfo without its cached secret or
// its route (learnt again: the first message to it floods), the newest
// kKeptContacts of them, the saved ones (favourites) always first (a
// board's NVS is small: 64 are 5.6 KB), written
// only when the list itself changes - who is in it, by what name - not each
// time one of them announces itself again
struct KeptContact
{
	uint8_t		key[PUB_KEY_SIZE];
	char		name[32];
	uint8_t		type, flags, unused[2];
	uint32_t	lastAdvert, lastmod;
	int32_t		latitude, longitude;
	uint32_t	syncSince;
};
const long		kContactsMagic = 0x4D43432;		// "MCC" 2
const long		kKeptContacts = 64;
// (file statics, not a function's: a function's static in an inline member
// function and a file's in the PSRAM section conflict on the board)
static HOST_LARGE_STATIC KeptContact	gKept[kHostMeshContactsMax];
static HOST_LARGE_STATIC uint8_t		gContactsBlob[8 + kKeptContacts * sizeof(KeptContact)];


// ----------------------------------------------------------------------
// The node

class HostMeshNode;
static HostMeshNode*			gNode = NULL;
static const HostMeshPlatform*	gPlatform = NULL;


// the engine woken from its wait between looks (a request)
static void
WakeEngine(void)
{
	gWake.notify_one();
	const HostMeshPlatform* platform = gPlatform;
	if (platform != NULL && platform->wake != NULL)
		platform->wake(platform->ref);
}
static KeptSettings				gSettings;
static bool						gContactsChanged = false;
static unsigned long			gContactsChangedAt = 0;
static uint32_t					gContactsSignature = 0;		// the list as last kept

static void		RecordMessage(const char* key, const char* name, const char* channel, const char* text, uint32_t sent);
static int		SecretOf(const char* text, uint8_t* secret);
static void		LoadChannels(void);
static void		SaveChannels(void);
static long		SendText(const char* key, const char* channel, const char* text);

class HostMeshNode : public BaseChatMesh
{
public:
	HostMeshNode(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::PacketManager& mgr, mesh::MeshTables& tables)
		: BaseChatMesh(radio, ms, rng, rtc, mgr, tables) { }

	// a node in a pocket is a poor repeater and forwarding costs battery
	bool	allowPacketForward(const mesh::Packet*) override { return false; }

	// a full table makes room for a node newly heard: the oldest not saved
	// goes (MeshCore's default is to add no more - a node heard once the
	// table was full could never be written to, nor its messages read)
	bool	shouldOverwriteWhenFull() const override { return true; }

	// a contact saved (a favourite: never the one to make room) or not,
	// added when it is not known - by its key alone, a node never heard
	void SetSaved(const uint8_t* key, const char* name, bool saved)
	{
		ContactInfo* c = lookupContactByPubKey(key, PUB_KEY_SIZE);
		if (c == NULL)
		{
			if (!saved)
				return;
			ContactInfo n;
			memset(&n, 0, sizeof(n));
			n.id = mesh::Identity(key);
			n.type = ADV_TYPE_CHAT;
			n.out_path_len = OUT_PATH_UNKNOWN;
			n.lastmod = getRTCClock()->getCurrentTime();
			if (!addContact(n))
				return;
			c = lookupContactByPubKey(key, PUB_KEY_SIZE);
			if (c == NULL)
				return;
		}
		if (name != NULL && name[0] != 0 && (c->name[0] == 0 || c->last_advert_timestamp == 0))
			StrHelper::strncpy(c->name, name, sizeof(c->name));	// (a node heard names itself)
		if (c->name[0] == 0)
		{
			char hex[kHostMeshKeyHexSize];
			HexOf(key, PUB_KEY_SIZE, hex);
			hex[8] = 0;
			StrHelper::strncpy(c->name, hex, sizeof(c->name));
		}
		c->flags = saved ? (c->flags | 1) : (c->flags & ~1);
		ContactsChanged();
	}

	void ContactsChanged()
	{
		Mirror();
		uint32_t signature = Signature();
		if (signature != gContactsSignature)
		{
			gContactsSignature = signature;
			gContactsChanged = true;
			gContactsChangedAt = NowMs();
		}
	}

	void onDiscoveredContact(ContactInfo&, bool, uint8_t, const uint8_t*) override
	{
		ContactsChanged();
	}

	void onContactPathUpdated(const ContactInfo&) override
	{
		Mirror();
	}

	// who is in the list, by what name and kind (a hash: FNV-1a)
	uint32_t Signature()
	{
		uint32_t h = 2166136261u;
		ContactsIterator walk = startContactsIterator();
		ContactInfo c;
		while (walk.hasNext(this, c))
		{
			if (c.type == ADV_TYPE_NONE)
				continue;
			const uint8_t* parts[2] = { c.id.pub_key, (const uint8_t*) c.name };
			const size_t sizes[2] = { PUB_KEY_SIZE, strnlen(c.name, sizeof(c.name)) };
			for (int p = 0; p < 2; p++)
				for (size_t k = 0; k < sizes[p]; k++)
					h = (h ^ parts[p][k]) * 16777619u;
			h = (h ^ c.type) * 16777619u;
			h = (h ^ (c.flags & 1)) * 16777619u;
		}
		return h;
	}

	ContactInfo* processAck(const uint8_t* data) override
	{
		uint32_t ack;
		memcpy(&ack, data, 4);
		std::lock_guard<std::mutex> hold(gLock);
		for (int k = 0; k < kSendsKept; k++)
		{
			Send& s = gSends[k];
			if (s.id != 0 && s.state == kHostMeshSendBusy && !s.channel && s.attempt > 0 && s.ack == ack)
			{
				s.state = kHostMeshSendDelivered;
				return lookupContactByPubKey(s.key, PUB_KEY_SIZE);
			}
		}
		return NULL;
	}

	void onMessageRecv(const ContactInfo& contact, mesh::Packet*, uint32_t timestamp, const char* text) override
	{
		char key[kHostMeshKeyHexSize];
		HexOf(contact.id.pub_key, PUB_KEY_SIZE, key);
		RecordMessage(key, contact.name, "", text, timestamp);
	}

	void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t timestamp, const uint8_t*, const char* text) override
	{
		onMessageRecv(contact, pkt, timestamp, text);
	}

	void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet*, uint32_t timestamp, const char* text) override
	{
		// MeshCore's channel text is "<sender>: <text>"
		char name[kHostMeshNameSize] = "";
		const char* colon = strstr(text, ": ");
		if (colon != NULL && colon - text < kHostMeshNameSize)
		{
			memcpy(name, text, colon - text);
			name[colon - text] = 0;
			text = colon + 2;
		}
		ChannelDetails details;
		int index = findChannelIdx(channel);
		const char* channelName = index >= 0 && getChannel(index, details) ? details.name : "?";
		RecordMessage("", name, channelName, text, timestamp);
	}

	int ErrFlags() const { return _err_flags; }

	// a channel's slot in MeshCore's table, by name, or -1
	int ChannelIndex(const char* name)
	{
		for (int k = 0; k < MAX_GROUP_CHANNELS; k++)
		{
			ChannelDetails d;
			if (getChannel(k, d) && d.name[0] != 0 && strcmp(d.name, name) == 0)
				return k;
		}
		return -1;
	}

	// the channels given to MeshCore's table, in order; the slots after
	// them emptied
	void ApplyChannels()
	{
		KeptChannel list[kHostMeshChannelsMax];
		long count;
		{
			std::lock_guard<std::mutex> hold(gLock);
			count = gChannelCount;
			memcpy(list, gChannels, sizeof(list));
		}
		for (int k = 0; k < MAX_GROUP_CHANNELS; k++)
		{
			ChannelDetails d;
			memset(&d, 0, sizeof(d));
			if (k < count)
			{
				memcpy(d.name, list[k].name, sizeof(d.name));
				d.name[sizeof(d.name) - 1] = 0;
				memcpy(d.channel.secret, list[k].secret, list[k].size);
			}
			setChannel(k, d);
			if (k >= count)
			{
				// (an empty slot's hash made one no packet has: its secret is
				// nothing, and it is never searched in vain for long)
				memset(&d, 0, sizeof(d));
			}
		}
	}

	// what was heard, counted (HostMeshStatus): every packet the dispatcher
	// parsed, before it is known to be a repeat
	void logRxRaw(float snr, float rssi, const uint8_t* raw, int len) override
	{
		// (as Dispatcher::tryParsePacket will judge it)
		bool dropped = len < 2;
		int path = 0;
		if (!dropped)
		{
			uint8_t header = raw[0];
			int i = 1 + ((header & PH_ROUTE_MASK) == ROUTE_TYPE_TRANSPORT_FLOOD || (header & PH_ROUTE_MASK) == ROUTE_TYPE_TRANSPORT_DIRECT ? 4 : 0);
			if (((header >> PH_VER_SHIFT) & PH_VER_MASK) > PAYLOAD_VER_1 || i >= len)
				dropped = true;
			else
			{
				path = raw[i++];
				int pathBytes = (path & 63) * ((path >> 6) + 1);
				dropped = (path >> 6) == 3 || pathBytes > MAX_PATH_SIZE || i + pathBytes > len
					   || len - i - pathBytes > MAX_PACKET_PAYLOAD;
			}
		}
		long frequencyError = gPlatform->radioFrequencyError != NULL ? gPlatform->radioFrequencyError(gPlatform->ref) : 0;
		if (len > 0 && len <= (int) sizeof(gLastRaw))
		{
			memcpy(gLastRaw, raw, len);
			gLastRawLength = len;
		}
		int first = -1;
		int differs = gPlatform->radioReread != NULL && len > 0 ? gPlatform->radioReread(gPlatform->ref, raw, len, &first) : -1;
		if (differs >= 0)
		{
			std::lock_guard<std::mutex> hold(gLock);
			gStatus.rereads++;
			if (differs > 0)
			{
				gStatus.rereadDiffers++;
				gStatus.lastRereadOffset = first;
			}
		}
		std::lock_guard<std::mutex> hold(gLock);
		gStatus.lastRssi = rssi;
		gStatus.lastSnr = snr;
		gStatus.lastFreqError = frequencyError;
		gStatus.rawPackets++;
		if (dropped)
		{
			gStatus.rawDropped++;
			gStatus.lastDroppedHeader = len > 0 ? (raw[0] << 8 | (path & 255)) : 0;
		}
	}

	void logRx(mesh::Packet* pkt, int, float) override
	{
		uint8_t type = pkt->getPayloadType();
		int known = -1, n = 0;
		if ((type == PAYLOAD_TYPE_GRP_TXT || type == PAYLOAD_TYPE_GRP_DATA) && pkt->payload_len > 3)
		{
			known = 0;
			mesh::GroupChannel channels[4];
			n = searchChannelsByHash(&pkt->payload[0], channels, 4);
			uint8_t data[MAX_PACKET_PAYLOAD];
			for (int j = 0; j < n && !known; j++)
				known = mesh::Utils::MACThenDecrypt(channels[j].secret, data, &pkt->payload[1], pkt->payload_len - 1) > 0;
		}
		if (gTraceRx)
		{
			static const char* const kTypes[16] = { "req", "response", "text", "ack", "advert", "channelText", "channelData",
				"anonReq", "path", "trace", "multipart", "control", "type12", "type13", "type14", "raw" };
			char path[3 * 64 + 1] = "";
			int hashes = pkt->getPathHashCount(), size = pkt->getPathHashSize();
			for (int k = 0; k < hashes && (k + 1) * size * 2 + k < (int) sizeof(path) - 1; k++)
				for (int b = 0; b < size; b++)
					snprintf(path + strlen(path), sizeof(path) - strlen(path), "%s%02X", b == 0 && k > 0 ? "," : "", pkt->path[k * size + b]);
			HostMeshStatusInfo s;
			{
				std::lock_guard<std::mutex> hold(gLock);
				s = gStatus;
			}
			printf("[mesh] rx %s %s path [%s] rssi %.0f snr %.1f ferr %ld\n", kTypes[type & 15],
					pkt->isRouteFlood() ? "flood" : "direct", path, s.lastRssi, s.lastSnr, s.lastFreqError);
		}
		std::lock_guard<std::mutex> hold(gLock);
		gStatus.heardByType[type & 15]++;
		if (known > 0)
			gStatus.channelKnown++;
		else if (known == 0)
		{
			gStatus.channelUnknown++;
			gStatus.lastUnknownHash = pkt->payload[0];
			memcpy(gUnreadable, gLastRaw, gLastRawLength);
			gUnreadableLength = gLastRawLength;
			if (n > 0)
			{
				gStatus.channelMismatched++;
				memcpy(gMismatched, gLastRaw, gLastRawLength);
				gMismatchedLength = gLastRawLength;
			}
		}
	}

	// commands are for repeaters and room servers: nothing here answers one
	void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override { }
	uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override { return 0; }
	void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override { }

	// how long to wait for an acknowledgement (lilyphone1's): a flood has
	// to cross the mesh and be answered back across it
	uint32_t calcFloodTimeoutMillisFor(uint32_t airtime) const override { return 12000 + airtime * 12; }
	uint32_t calcDirectTimeoutMillisFor(uint32_t airtime, uint8_t pathLength) const override { return 6000 + airtime * 2 * (pathLength + 2); }

	// BaseChatMesh keeps one timeout, for the last message sent: each send
	// keeps its own here (CheckSends)
	void	onSendTimeout() override { }

	// the contacts as the natives see them, newest first
	void Mirror()
	{
		std::lock_guard<std::mutex> hold(gLock);
		long count = 0;
		ContactsIterator walk = startContactsIterator();
		ContactInfo c;
		while (count < kHostMeshContactsMax && walk.hasNext(this, c))
		{
			if (c.type == ADV_TYPE_NONE)
				continue;
			HostMeshContact& m = gContacts[count++];
			HexOf(c.id.pub_key, PUB_KEY_SIZE, m.key);
			CopyUTF8(m.name, c.name, sizeof(m.name));
			m.kind = c.type;
			m.hops = c.out_path_len == OUT_PATH_UNKNOWN ? -1 : c.out_path_len;
			m.heard = c.lastmod;
			m.saved = (c.flags & 1) != 0;
		}
		// newest first (insertion: there are few)
		for (long i = 1; i < count; i++)
			for (long j = i; j > 0 && gContacts[j].heard > gContacts[j - 1].heard; j--)
			{
				HostMeshContact t = gContacts[j];
				gContacts[j] = gContacts[j - 1];
				gContacts[j - 1] = t;
			}
		gContactCount = count;
		gStatus.contacts = count;
	}

	void SaveContacts()
	{
		KeptContact* kept = gKept;
		long count = 0;
		ContactsIterator walk = startContactsIterator();
		ContactInfo c;
		while (count < kHostMeshContactsMax && walk.hasNext(this, c))
		{
			if (c.type == ADV_TYPE_NONE)
				continue;
			KeptContact& k = kept[count++];
			memset(&k, 0, sizeof(k));
			memcpy(k.key, c.id.pub_key, PUB_KEY_SIZE);
			memcpy(k.name, c.name, sizeof(k.name));
			k.type = c.type;
			k.flags = c.flags;
			k.lastAdvert = c.last_advert_timestamp;
			k.lastmod = c.lastmod;
			k.latitude = c.gps_lat;
			k.longitude = c.gps_lon;
			k.syncSince = c.sync_since;
		}
		// the saved first, then the newest
		for (long i = 1; i < count; i++)
			for (long j = i; j > 0 && ((kept[j].flags & 1) > (kept[j - 1].flags & 1)
										|| ((kept[j].flags & 1) == (kept[j - 1].flags & 1) && kept[j].lastmod > kept[j - 1].lastmod)); j--)
			{
				KeptContact t = kept[j];
				kept[j] = kept[j - 1];
				kept[j - 1] = t;
			}
		if (count > kKeptContacts)
			count = kKeptContacts;
		uint8_t* blob = gContactsBlob;
		uint32_t magic = kContactsMagic, n = (uint32_t) count;
		memcpy(blob, &magic, 4);
		memcpy(blob + 4, &n, 4);
		memcpy(blob + 8, kept, count * sizeof(KeptContact));
		gPlatform->save(gPlatform->ref, "contacts", blob, 8 + count * (long) sizeof(KeptContact));
	}

	void LoadContacts()
	{
		uint8_t* blob = gContactsBlob;
		long size = sizeof(gContactsBlob);
		if (!gPlatform->load(gPlatform->ref, "contacts", blob, &size) || size < 8)
			return;
		uint32_t magic, n;
		memcpy(&magic, blob, 4);
		memcpy(&n, blob + 4, 4);
		if (magic != kContactsMagic || n > kKeptContacts || 8 + n * (long) sizeof(KeptContact) > size)
			return;
		for (uint32_t i = 0; i < n; i++)
		{
			KeptContact k;
			memcpy(&k, blob + 8 + i * sizeof(KeptContact), sizeof(k));
			ContactInfo c;
			memset(&c, 0, sizeof(c));
			c.id = mesh::Identity(k.key);
			memcpy(c.name, k.name, sizeof(c.name));
			c.name[sizeof(c.name) - 1] = 0;
			c.type = k.type;
			c.flags = k.flags;
			c.out_path_len = OUT_PATH_UNKNOWN;
			c.last_advert_timestamp = k.lastAdvert;
			c.lastmod = k.lastmod;
			c.gps_lat = k.latitude;
			c.gps_lon = k.longitude;
			c.sync_since = k.syncSince;
			addContact(c);
		}
	}

	void Advertise()
	{
		bool has;
		double latitude, longitude;
		{
			std::lock_guard<std::mutex> hold(gLock);
			has = gHasLocation;
			latitude = gLatitude;
			longitude = gLongitude;
		}
		mesh::Packet* advert = has ? createSelfAdvert(gSettings.name, latitude, longitude) : createSelfAdvert(gSettings.name);
		if (advert != NULL)
			sendFlood(advert, 0, (uint8_t) gSettings.pathHashSize);
	}

	// floods with the mesh's path hash size (a preset's network setting)
	void sendFloodScoped(const ContactInfo&, mesh::Packet* pkt, uint32_t delay_millis) override
	{
		sendFlood(pkt, delay_millis, (uint8_t) gSettings.pathHashSize);
	}

	void sendFloodScoped(const mesh::GroupChannel&, mesh::Packet* pkt, uint32_t delay_millis) override
	{
		sendFlood(pkt, delay_millis, (uint8_t) gSettings.pathHashSize);
	}

	// each send waiting for its turn, its acknowledgement or its timeout
	void CheckSends()
	{
		unsigned long now = NowMs();
		for (int k = 0; k < kSendsKept; k++)
		{
			Send s;
			{
				std::lock_guard<std::mutex> hold(gLock);
				if (gSends[k].id == 0 || gSends[k].state != kHostMeshSendBusy)
					continue;
				if (gSends[k].attempt > 0 && (long) (now - gSends[k].deadline) < 0)
					continue;
				s = gSends[k];
			}
			int state = kHostMeshSendBusy;
			uint32_t ack = 0, timeout = 0;
			if (s.channel)
			{
				ChannelDetails channel;
				int index = ChannelIndex(s.channelName);
				state = index >= 0 && getChannel(index, channel)
					 && sendGroupMessage(s.timestamp, channel.channel, gSettings.name, s.text, (int) strlen(s.text))
						? kHostMeshSendSent : kHostMeshSendFailed;
				if (state == kHostMeshSendSent)
				{
					std::lock_guard<std::mutex> hold(gLock);
					gStatus.sent++;
				}
			}
			else if (s.attempt >= kSendAttempts)
				state = kHostMeshSendFailed;			// the last attempt's timeout
			else
			{
				ContactInfo* contact = lookupContactByPubKey(s.key, PUB_KEY_SIZE);
				if (contact == NULL)
				{
					// (a key never heard, or made room for since: added - a
					// message needs only the key; the first floods)
					ContactInfo n;
					memset(&n, 0, sizeof(n));
					n.id = mesh::Identity(s.key);
					n.type = ADV_TYPE_CHAT;
					n.out_path_len = OUT_PATH_UNKNOWN;
					n.lastmod = getRTCClock()->getCurrentTime();
					char hex[kHostMeshKeyHexSize];
					HexOf(s.key, PUB_KEY_SIZE, hex);
					hex[8] = 0;
					StrHelper::strncpy(n.name, hex, sizeof(n.name));
					if (addContact(n))
					{
						ContactsChanged();
						contact = lookupContactByPubKey(s.key, PUB_KEY_SIZE);
					}
				}
				if (contact == NULL)
					state = kHostMeshSendFailed;
				else
				{
					// the last attempt by flood: the route may have gone
					if (s.attempt == kSendAttempts - 1 && contact->out_path_len != OUT_PATH_UNKNOWN)
						resetPathTo(*contact);
					if (sendMessage(*contact, s.timestamp, (uint8_t) s.attempt, s.text, ack, timeout) == MSG_SEND_FAILED)
						state = kHostMeshSendFailed;
					else if (s.attempt == 0)
					{
						std::lock_guard<std::mutex> hold(gLock);
						gStatus.sent++;
					}
				}
			}
			std::lock_guard<std::mutex> hold(gLock);
			Send& t = gSends[k];
			if (t.id != s.id || t.state != kHostMeshSendBusy)
				continue;						// (settled meanwhile)
			t.state = state;
			if (state == kHostMeshSendBusy)
			{
				t.attempt++;
				t.ack = ack;
				t.deadline = NowMs() + timeout;
			}
		}
	}
};


void	(*gHostMeshNews)(void) = NULL;


static void
RecordMessage(const char* key, const char* name, const char* channel, const char* text, uint32_t sent)
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gMessageCount == kMessagesKept)
	{
		gMessageHead = (gMessageHead + 1) % kMessagesKept;		// the oldest dropped
		gMessageCount--;
	}
	HostMeshMessage& m = gMessages[(gMessageHead + gMessageCount++) % kMessagesKept];
	memset(&m, 0, sizeof(m));
	CopyUTF8(m.key, key, sizeof(m.key));
	CopyUTF8(m.name, name, sizeof(m.name));
	CopyUTF8(m.channel, channel, sizeof(m.channel));
	CopyUTF8(m.text, text, sizeof(m.text));
	m.sent = sent;
	m.received = gNode != NULL ? gNode->getRTCClock()->getCurrentTime() : 0;
	if (gHostMeshNews != NULL)
		gHostMeshNews();
	gStatus.received++;
}


// ----------------------------------------------------------------------
// The engine's thread

static HostMillis		gMillis;
static HostRTC			gRTC;
static HostRNG			gRNG;
static SimpleMeshTables	gTables;

static HostMeshRadioSettings
RadioSettings(void)
{
	HostMeshRadioSettings r = { gSettings.frequency, gSettings.bandwidth, (int) gSettings.spreadingFactor,
								(int) gSettings.codingRate, (int) gSettings.txPower, (int) gSettings.pathHashSize };
	return r;
}


// the preset these settings are (the frequency to the kHz), or -1
static long
PresetOf(const HostMeshRadioSettings& r)
{
	for (long k = 0; k < kPresetCount; k++)
		if (fabsf(kPresets[k].frequency - r.frequency) < 0.0005f && fabsf(kPresets[k].bandwidth - r.bandwidth) < 0.01f
		 && kPresets[k].spreadingFactor == r.spreadingFactor && kPresets[k].codingRate == r.codingRate
		 && kPresets[k].pathHashSize == r.pathHashSize)
			return k;
	return -1;
}


// whether the radio takes these: 150-960 MHz (the SX1262's), one of a LoRa
// radio's bandwidths, SF 5-12, CR 4/5-4/8, -9-22 dBm (and a path hash of 1-3
// bytes, MeshCore's)
static bool
RadioValid(const HostMeshRadioSettings& r)
{
	bool bandwidth = false;
	for (long k = 0; k < kBandwidthCount; k++)
		if (fabsf(kBandwidths[k] - r.bandwidth) < 0.01f)
			bandwidth = true;
	return bandwidth && r.frequency >= 150.0f && r.frequency <= 960.0f
		&& r.spreadingFactor >= 5 && r.spreadingFactor <= 12 && r.codingRate >= 5 && r.codingRate <= 8
		&& r.txPower >= kTxPowerMin && r.txPower <= kTxPowerMax && r.pathHashSize >= 1 && r.pathHashSize <= 3;
}


static void
UsePreset(const Preset& p)
{
	gSettings.frequency = p.frequency;
	gSettings.bandwidth = p.bandwidth;
	gSettings.spreadingFactor = p.spreadingFactor;
	gSettings.codingRate = p.codingRate;
	gSettings.pathHashSize = p.pathHashSize;
}


static const Preset&
DefaultPreset(void)
{
	for (long k = 0; k < kPresetCount; k++)
		if (strcmp(kPresets[k].name, kDefaultPreset) == 0)
			return kPresets[k];
	return kPresets[0];
}


// what the status says the radio is on: its preset's name, or "Custom"
static const char*
RadioName(const HostMeshRadioSettings& r)
{
	long k = PresetOf(r);
	return k >= 0 ? kPresets[k].name : "Custom";
}


static void
SaveSettings(void)
{
	gPlatform->save(gPlatform->ref, "settings", &gSettings, sizeof(gSettings));
}


static void
StatusRadio(void)
{
	std::lock_guard<std::mutex> hold(gLock);
	gStatus.radio = RadioSettings();
	CopyUTF8(gStatus.region, RadioName(gStatus.radio), sizeof(gStatus.region));
	CopyUTF8(gStatus.name, gSettings.name, sizeof(gStatus.name));
}


void
HostMeshRun(const HostMeshPlatform* platform)
{
	gPlatform = platform;
	{
		std::lock_guard<std::mutex> hold(gLock);
		gStatus.lastUnknownHash = -1;
		gStatus.lastDroppedHeader = -1;
		gStatus.lastRereadOffset = -1;
	}
	gStarted = true;
	{
		std::lock_guard<std::mutex> hold(gLock);
		gStatus.state = kHostMeshStarting;
	}

	// the settings
	memset(&gSettings, 0, sizeof(gSettings));
	long size = sizeof(gSettings);
	bool loaded = platform->load(platform->ref, "settings", &gSettings, &size);
	if (loaded && size == kSettingsVersion1Size && gSettings.version == 1)
	{
		// version 1 kept a preset: its settings filled in
		long k = gSettings.region;
		if (k == 0 || k < 0 || k >= (long) (sizeof(kVersion1Presets) / sizeof(kVersion1Presets[0])))
			UsePreset(DefaultPreset());
		else
			UsePreset(kVersion1Presets[k]);
		gSettings.version = kSettingsVersion;
		gSettings.region = -1;
		SaveSettings();
	}
	else if (!loaded || size != (long) sizeof(gSettings) || gSettings.version != kSettingsVersion)
	{
		memset(&gSettings, 0, sizeof(gSettings));
		gSettings.version = kSettingsVersion;
		CopyUTF8(gSettings.name, platform->defaultName != NULL ? platform->defaultName : "Newton", sizeof(gSettings.name));
		gSettings.region = -1;
		gSettings.txPower = kTxPowerMax;
		UsePreset(DefaultPreset());
	}
	if (!RadioValid(RadioSettings()))
	{
		UsePreset(DefaultPreset());
		if (gSettings.txPower < kTxPowerMin || gSettings.txPower > kTxPowerMax)
			gSettings.txPower = kTxPowerMax;
	}

	static StaticPoolPacketManager* manager = new StaticPoolPacketManager(16);
	HostMeshNode* node = new HostMeshNode(*platform->radio, gMillis, gRNG, gRTC, *manager, gTables);

	// the identity: this node's name on the mesh for good
	uint8_t identity[PUB_KEY_SIZE + PRV_KEY_SIZE];
	long identitySize = sizeof(identity);
	if (platform->load(platform->ref, "identity", identity, &identitySize) && identitySize == (long) sizeof(identity))
		node->self_id.readFrom(identity, sizeof(identity));
	else
	{
		node->self_id = mesh::LocalIdentity(&gRNG);
		node->self_id.writeTo(identity, sizeof(identity));
		platform->save(platform->ref, "identity", identity, sizeof(identity));
		SaveSettings();
	}
	{
		std::lock_guard<std::mutex> hold(gLock);
		HexOf(node->self_id.pub_key, PUB_KEY_SIZE, gStatus.key);
	}
	StatusRadio();

	HostMeshRadioSettings radio = RadioSettings();
	bool radioUp = gPowered && platform->radioStart(platform->ref, &radio);
	node->begin();
	LoadChannels();
	node->ApplyChannels();
	node->LoadContacts();
	node->Mirror();
	gContactsSignature = node->Signature();
	gNode = node;
	{
		std::lock_guard<std::mutex> hold(gLock);
		gStatus.state = gPowered ? kHostMeshOn : kHostMeshOff;
	}
	fprintf(stderr, "[mesh] up as \"%s\" (%.8s), %s: %.3f MHz bw %.2f sf %d cr %d %d dBm hash %d%s\n", gSettings.name, gStatus.key,
			RadioName(radio), radio.frequency, radio.bandwidth, radio.spreadingFactor, radio.codingRate, radio.txPower, radio.pathHashSize,
			gPowered ? (radioUp ? "" : " - the radio would not start") : " - the radio is off");

	unsigned long nextAdvert = NowMs() + kFirstAdvertMs;
	for (;;)
	{
		// the power
		if (gSleepAsked.exchange(false))
		{
			platform->radioSleep(platform->ref);
			if (gPowerSwitch != NULL)
				gPowerSwitch(false);
			std::lock_guard<std::mutex> hold(gLock);
			gStatus.state = kHostMeshOff;
		}
		if (gWakeAsked.exchange(false))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));	// the module given a moment
			radio = RadioSettings();
			radioUp = platform->radioStart(platform->ref, &radio);
			fprintf(stderr, "[mesh] the radio on again%s\n", radioUp ? "" : " - it would not start");
			{
				std::lock_guard<std::mutex> hold(gLock);
				gStatus.state = kHostMeshOn;
			}
			nextAdvert = NowMs() + kFirstAdvertMs;
		}
		if (!gPowered)
		{
			std::unique_lock<std::mutex> hold(gLock);
			gWake.wait_for(hold, std::chrono::milliseconds(200));
			continue;
		}

		// the requests
		bool advert = false, rename = false, retune = false;
		HostMeshRadioSettings wanted;
		char name[kHostMeshNameSize];
		{
			std::lock_guard<std::mutex> hold(gLock);
			advert = gAdvertAsked;
			gAdvertAsked = false;
			rename = gNameAsked;
			gNameAsked = false;
			memcpy(name, gNameWanted, sizeof(name));
			retune = gRadioAsked;
			gRadioAsked = false;
			wanted = gRadioWanted;
		}
		if (rename)
		{
			memcpy(gSettings.name, name, sizeof(gSettings.name));
			SaveSettings();
			StatusRadio();
			advert = true;
		}
		if (retune && RadioValid(wanted))
		{
			HostMeshRadioSettings was = RadioSettings();
			gSettings.frequency = wanted.frequency;
			gSettings.bandwidth = wanted.bandwidth;
			gSettings.spreadingFactor = wanted.spreadingFactor;
			gSettings.codingRate = wanted.codingRate;
			gSettings.txPower = wanted.txPower;
			gSettings.pathHashSize = wanted.pathHashSize;
			SaveSettings();
			StatusRadio();
			radio = RadioSettings();
			platform->radioStart(platform->ref, &radio);
			fprintf(stderr, "[mesh] %s: %.3f MHz bw %.2f sf %d cr %d %d dBm hash %d\n", RadioName(radio), radio.frequency, radio.bandwidth,
					radio.spreadingFactor, radio.codingRate, radio.txPower, radio.pathHashSize);
			if (was.frequency != radio.frequency || was.bandwidth != radio.bandwidth
			 || was.spreadingFactor != radio.spreadingFactor || was.codingRate != radio.codingRate)
				advert = true;									// nobody there has heard this node
		}

		long recoveries = platform->radioCheck != NULL ? platform->radioCheck(platform->ref) : -1;
		bool channelsChanged;
		{
			std::lock_guard<std::mutex> hold(gLock);
			channelsChanged = gChannelsChanged;
			gChannelsChanged = false;
		}
		if (channelsChanged)
		{
			node->ApplyChannels();
			SaveChannels();
		}
		{
			ContactAsk asks[kContactAsksMax];
			int n;
			{
				std::lock_guard<std::mutex> hold(gLock);
				n = gContactAskCount;
				memcpy(asks, gContactAsks, n * sizeof(ContactAsk));
				gContactAskCount = 0;
			}
			for (int k = 0; k < n; k++)
				node->SetSaved(asks[k].key, asks[k].name, asks[k].saved);
		}
		node->CheckSends();
		node->loop();
		{
			std::lock_guard<std::mutex> hold(gLock);
			gStatus.packetsHeard = (long) (node->getNumRecvFlood() + node->getNumRecvDirect());
			gStatus.packetsSent = (long) (node->getNumSentFlood() + node->getNumSentDirect());
			gStatus.radioErrors = platform->radioErrors != NULL ? platform->radioErrors(platform->ref) : -1;
			gStatus.radioRecoveries = recoveries;
			long idleMs = 0;
			gStatus.radioIdleRestarts = platform->radioIdle != NULL ? platform->radioIdle(platform->ref, &idleMs) : -1;
			gStatus.radioIdleMs = idleMs;
			gStatus.errFlags = node->ErrFlags();
			gStatus.poolFree = manager->getFreeCount();
			gStatus.noiseFloor = platform->radio->getNoiseFloor();
		}

		unsigned long now = NowMs();
		if (advert || (long) (now - nextAdvert) >= 0)
		{
			node->Advertise();
			nextAdvert = now + kAdvertPeriodMs;
		}
		if (gContactsChanged && now - gContactsChangedAt >= kContactsSaveMs)
		{
			gContactsChanged = false;
			node->SaveContacts();
		}

		// short enough not to miss a receive window, long enough to yield;
		// a request wakes it at once.  A host that wakes it on the radio's
		// interrupt as well waits longer while there is nothing to send, so
		// that its processor can sleep (a packet is read on its interrupt,
		// and none is short enough to come whole in kIdleWaitMs)
		if (platform->wait != NULL)
			platform->wait(platform->ref, manager->getOutboundTotal() > 0 ? 5 : kIdleWaitMs);
		else
		{
			std::unique_lock<std::mutex> hold(gLock);
			gWake.wait_for(hold, std::chrono::milliseconds(5));
		}
	}
}


void
HostMeshWillRun(void)
{
	gWillRun = true;
	std::lock_guard<std::mutex> hold(gLock);
	if (gStatus.state == kHostMeshNone)
		gStatus.state = kHostMeshStarting;
}


bool
HostMeshStarted(void)
{
	return gWillRun || gStarted;
}


// ----------------------------------------------------------------------
// The interface (any thread)

void
HostMeshGetStatus(HostMeshStatusInfo* status)
{
	std::lock_guard<std::mutex> hold(gLock);
	*status = gStatus;
}


long
HostMeshGetContacts(HostMeshContact* contacts, long max)
{
	std::lock_guard<std::mutex> hold(gLock);
	for (long k = 0; k < gContactCount && k < max; k++)
		contacts[k] = gContacts[k];
	return gContactCount;
}


bool
HostMeshNextMessage(HostMeshMessage* message)
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gMessageCount == 0)
		return false;
	*message = gMessages[gMessageHead];
	gMessageHead = (gMessageHead + 1) % kMessagesKept;
	gMessageCount--;
	message->now = gRTC.getCurrentTime();
	return true;
}


long
HostMeshSendText(const char* key, const char* text)
{
	return SendText(key, PUBLIC_CHANNEL_NAME, text);
}


// a message to a node (key) or else a channel (by name)
static long
SendText(const char* key, const char* channel, const char* text)
{
	if (!HostMeshStarted() || text == NULL || text[0] == 0)
		return 0;
	Send s;
	memset(&s, 0, sizeof(s));
	s.channel = key == NULL || key[0] == 0;
	if (s.channel)
		CopyUTF8(s.channelName, channel, sizeof(s.channelName));
	else if (!KeyOf(key, s.key))
		return 0;
	// (MeshCore takes MAX_TEXT_LEN, 160 bytes; a channel's has the
	// sender's name in front, which it cuts to fit)
	CopyUTF8(s.text, text, MAX_TEXT_LEN + 1);
	s.state = kHostMeshSendBusy;
	s.timestamp = gRTC.getCurrentTimeUnique();
	std::lock_guard<std::mutex> hold(gLock);
	int slot = -1;
	for (int k = 0; k < kSendsKept && slot < 0; k++)
		if (gSends[k].id == 0)
			slot = k;
	for (int k = 0; k < kSendsKept && slot < 0; k++)
		if (gSends[k].state != kHostMeshSendBusy)
			slot = k;							// the oldest settled one forgotten
	if (slot < 0)
		return 0;
	s.id = gNextSendId++;
	gSends[slot] = s;
	WakeEngine();
	return s.id;
}


int
HostMeshGetSendState(long id)
{
	std::lock_guard<std::mutex> hold(gLock);
	for (int k = 0; k < kSendsKept; k++)
		if (gSends[k].id == id)
			return gSends[k].state;
	return kHostMeshSendNone;
}


// ----------------------------------------------------------------------
// The channels

// the channels kept (the blob "channels": magic, count, KeptChannel[]), after
// Public, which is always first and not kept
static void
LoadChannels(void)
{
	KeptChannel publicChannel;
	memset(&publicChannel, 0, sizeof(publicChannel));
	CopyUTF8(publicChannel.name, PUBLIC_CHANNEL_NAME, sizeof(publicChannel.name));
	publicChannel.size = (uint8_t) SecretOf(PUBLIC_CHANNEL_KEY, publicChannel.secret);
	uint8_t blob[8 + kHostMeshChannelsMax * sizeof(KeptChannel)];
	long size = sizeof(blob);
	std::lock_guard<std::mutex> hold(gLock);
	gChannels[0] = publicChannel;
	gChannelCount = 1;
	if (!gPlatform->load(gPlatform->ref, "channels", blob, &size) || size < 8)
		return;
	uint32_t magic, n;
	memcpy(&magic, blob, 4);
	memcpy(&n, blob + 4, 4);
	if (magic != (uint32_t) kChannelsMagic || n > (uint32_t) kHostMeshChannelsMax - 1 || 8 + n * (long) sizeof(KeptChannel) > size)
		return;
	for (uint32_t k = 0; k < n; k++)
	{
		KeptChannel c;
		memcpy(&c, blob + 8 + k * sizeof(KeptChannel), sizeof(c));
		c.name[sizeof(c.name) - 1] = 0;
		if (c.name[0] != 0 && (c.size == 16 || c.size == 32))
			gChannels[gChannelCount++] = c;
	}
}


static void
SaveChannels(void)
{
	uint8_t blob[8 + kHostMeshChannelsMax * sizeof(KeptChannel)];
	uint32_t magic = (uint32_t) kChannelsMagic, n;
	{
		std::lock_guard<std::mutex> hold(gLock);
		n = (uint32_t) (gChannelCount - 1);
		memcpy(blob + 8, &gChannels[1], n * sizeof(KeptChannel));
	}
	memcpy(blob, &magic, 4);
	memcpy(blob + 4, &n, 4);
	gPlatform->save(gPlatform->ref, "channels", blob, 8 + n * (long) sizeof(KeptChannel));
}


// a secret as hex (32 or 64 digits) or base64 (16 or 32 bytes) ==> its size, 0
static int
SecretOf(const char* text, uint8_t* secret)
{
	size_t length = strlen(text);
	if (length == 32 || length == 64)
	{
		bool hex = true;
		for (size_t k = 0; k < length && hex; k++)
			hex = isxdigit((unsigned char) text[k]) != 0;
		if (hex)
		{
			for (size_t k = 0; k < length / 2; k++)
			{
				char pair[3] = { text[2 * k], text[2 * k + 1], 0 };
				secret[k] = (uint8_t) strtoul(pair, NULL, 16);
			}
			return (int) length / 2;
		}
	}
	if (length == 24 || length == 44)
	{
		// base64 (MeshCore's base64.hpp defines its functions in the header:
		// a second file including it meets the first's at link time)
		uint8_t bytes[33];
		int size = 0;
		uint32_t bits = 0;
		int held = 0;
		for (size_t k = 0; k < length && text[k] != '='; k++)
		{
			char c = text[k];
			int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
				  : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
			if (v < 0)
				return 0;
			bits = (bits << 6) | (uint32_t) v;
			held += 6;
			if (held >= 8)
			{
				held -= 8;
				if (size == (int) sizeof(bytes))
					return 0;
				bytes[size++] = (uint8_t) (bits >> held);
			}
		}
		if (size == 16 || size == 32)
		{
			memcpy(secret, bytes, size);
			return size;
		}
	}
	return 0;
}


long
HostMeshLastMismatched(char* hex, long size)
{
	std::lock_guard<std::mutex> hold(gLock);
	long n = gMismatchedLength;
	if (2 * n + 1 > size)
		n = (size - 1) / 2;
	HexOf(gMismatched, n, hex);
	return n;
}


long
HostMeshLastUnreadable(char* hex, long size)
{
	std::lock_guard<std::mutex> hold(gLock);
	long n = gUnreadableLength;
	if (2 * n + 1 > size)
		n = (size - 1) / 2;
	HexOf(gUnreadable, n, hex);
	return n;
}


void
HostMeshSetLocation(bool has, double latitude, double longitude)
{
	std::lock_guard<std::mutex> hold(gLock);
	gHasLocation = has && latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
	gLatitude = latitude;
	gLongitude = longitude;
	gStatus.hasLocation = gHasLocation;
	gStatus.latitude = latitude;
	gStatus.longitude = longitude;
}


void
HostMeshTraceRx(bool on)
{
	gTraceRx = on;
}


long
HostMeshGetChannels(HostMeshChannel* channels, long max)
{
	std::lock_guard<std::mutex> hold(gLock);
	long count = gChannelCount < max ? gChannelCount : max;
	for (long k = 0; k < count; k++)
	{
		CopyUTF8(channels[k].name, gChannels[k].name, sizeof(channels[k].name));
		HexOf(gChannels[k].secret, gChannels[k].size, channels[k].secret);
	}
	return count;
}


int
HostMeshJoinChannel(const char* name, const char* secret)
{
	if (!HostMeshStarted())
		return kHostMeshJoinNoNode;
	if (name == NULL || name[0] == 0 || strlen(name) >= sizeof(((KeptChannel*) 0)->name) || (name[0] == '#' && name[1] == 0))
		return kHostMeshJoinBadName;
	KeptChannel c;
	memset(&c, 0, sizeof(c));
	CopyUTF8(c.name, name, sizeof(c.name));
	if (secret == NULL || secret[0] == 0)
	{
		if (name[0] != '#')
			return kHostMeshJoinBadSecret;		// (a private channel needs its secret)
		uint8_t digest[32];
		mesh::Utils::sha256(digest, sizeof(digest), (const uint8_t*) name, (int) strlen(name));
		memcpy(c.secret, digest, 16);
		c.size = 16;
	}
	else if ((c.size = (uint8_t) SecretOf(secret, c.secret)) == 0)
		return kHostMeshJoinBadSecret;
	std::lock_guard<std::mutex> hold(gLock);
	for (long k = 0; k < gChannelCount; k++)
		if (strcmp(gChannels[k].name, c.name) == 0
		 || (gChannels[k].size == c.size && memcmp(gChannels[k].secret, c.secret, c.size) == 0))
			return kHostMeshJoinAlready;
	if (gChannelCount >= kHostMeshChannelsMax)
		return kHostMeshJoinFull;
	gChannels[gChannelCount++] = c;
	gChannelsChanged = true;
	WakeEngine();
	return kHostMeshJoined;
}


bool
HostMeshLeaveChannel(const char* name)
{
	if (!HostMeshStarted() || name == NULL)
		return false;
	std::lock_guard<std::mutex> hold(gLock);
	for (long k = 1; k < gChannelCount; k++)		// (Public, the first, stays)
		if (strcmp(gChannels[k].name, name) == 0)
		{
			memmove(&gChannels[k], &gChannels[k + 1], (gChannelCount - k - 1) * sizeof(KeptChannel));
			gChannelCount--;
			gChannelsChanged = true;
			WakeEngine();
			return true;
		}
	return false;
}


long
HostMeshSendChannelText(const char* channel, const char* text)
{
	if (channel == NULL || channel[0] == 0)
		return 0;
	return SendText(NULL, channel, text);
}


bool
HostMeshSetSaved(const char* key, const char* name, bool saved)
{
	ContactAsk a;
	memset(&a, 0, sizeof(a));
	if (!HostMeshStarted() || key == NULL || strlen(key) != 2 * kHostMeshKeySize || !KeyOf(key, a.key))
		return false;
	CopyUTF8(a.name, name != NULL ? name : "", sizeof(a.name));
	a.saved = saved;
	std::lock_guard<std::mutex> hold(gLock);
	for (int k = 0; k < gContactAskCount; k++)
		if (memcmp(gContactAsks[k].key, a.key, sizeof(a.key)) == 0)
		{
			gContactAsks[k] = a;					// (the later wins)
			WakeEngine();
			return true;
		}
	if (gContactAskCount == kContactAsksMax)
		return false;
	gContactAsks[gContactAskCount++] = a;
	WakeEngine();
	return true;
}


bool
HostMeshAdvertise(void)
{
	if (!HostMeshStarted())
		return false;
	std::lock_guard<std::mutex> hold(gLock);
	gAdvertAsked = true;
	WakeEngine();
	return true;
}


bool
HostMeshSetName(const char* name)
{
	if (!HostMeshStarted() || name == NULL || name[0] == 0)
		return false;
	std::lock_guard<std::mutex> hold(gLock);
	CopyUTF8(gNameWanted, name, sizeof(gNameWanted));
	gNameAsked = true;
	WakeEngine();
	return true;
}


long
HostMeshRegionCount(void)
{
	return kPresetCount;
}


const char*
HostMeshRegionName(long index)
{
	return index >= 0 && index < kPresetCount ? kPresets[index].name : NULL;
}


bool
HostMeshRegionSettings(long index, HostMeshRadioSettings* settings)
{
	if (index < 0 || index >= kPresetCount)
		return false;
	settings->frequency = kPresets[index].frequency;
	settings->bandwidth = kPresets[index].bandwidth;
	settings->spreadingFactor = kPresets[index].spreadingFactor;
	settings->codingRate = kPresets[index].codingRate;
	settings->pathHashSize = kPresets[index].pathHashSize;
	return true;
}


long
HostMeshRegion(void)
{
	std::lock_guard<std::mutex> hold(gLock);
	return PresetOf(gStatus.radio);
}


bool
HostMeshSetRegion(long index)
{
	HostMeshRadioSettings r;
	HostMeshGetRadio(&r);
	return HostMeshRegionSettings(index, &r) && HostMeshSetRadio(&r);
}


void
HostMeshGetRadio(HostMeshRadioSettings* settings)
{
	std::lock_guard<std::mutex> hold(gLock);
	*settings = gStatus.radio;
}


long
HostMeshBandwidthCount(void)
{
	return kBandwidthCount;
}


float
HostMeshBandwidth(long index)
{
	return index >= 0 && index < kBandwidthCount ? kBandwidths[index] : 0;
}


bool
HostMeshSetRadio(const HostMeshRadioSettings* settings)
{
	if (!HostMeshStarted() || settings == NULL || !RadioValid(*settings))
		return false;
	std::lock_guard<std::mutex> hold(gLock);
	gRadioWanted = *settings;
	gRadioAsked = true;
	gStatus.radio = *settings;									// (as the natives will read it)
	CopyUTF8(gStatus.region, RadioName(*settings), sizeof(gStatus.region));
	WakeEngine();
	return true;
}


// ----------------------------------------------------------------------
// The power

void
HostMeshSetPowerSwitch(bool (*powerSwitch)(bool on), bool on)
{
	gPowerSwitch = powerSwitch;
	gPowered = on;
	std::lock_guard<std::mutex> hold(gLock);
	if (!on)
		gStatus.state = kHostMeshOff;
}


bool
HostMeshPowerSwitchable(void)
{
	return gPowerSwitch != NULL;
}


bool
HostMeshPowered(void)
{
	return gPowered;
}


bool
HostMeshSetPower(bool on)
{
	if (gPowerSwitch == NULL)
		return false;
	if (on == gPowered)
		return true;
	if (on)
	{
		// the supply on now; the engine starts the radio again once it has
		// had a moment
		if (!gPowerSwitch(true))
			return false;
		gPowered = true;
		gWakeAsked = true;
	}
	else
	{
		// the engine puts the radio to sleep, then switches it off
		gPowered = false;
		gSleepAsked = true;
		if (!gStarted)
		{
			gSleepAsked = false;
			gPowerSwitch(false);
		}
	}
	std::lock_guard<std::mutex> hold(gLock);
	gStatus.state = on ? kHostMeshStarting : kHostMeshOff;
	WakeEngine();
	return true;
}
