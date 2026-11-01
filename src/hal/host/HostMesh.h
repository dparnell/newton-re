/*
	File:		hal/host/HostMesh.h

	Contains:	The host's LoRa mesh: a MeshCore chat node
				(src/thirdparty/meshcore) - this node announced to the mesh
				(an advert), the nodes heard from kept as contacts, text
				sent to one of them (encrypted to it, acknowledged) or to
				the Public channel every MeshCore node shares, and text
				received from either.

				The engine (HostMesh.cpp) is portable and knows nothing of
				the Newton: it owns the MeshCore node and the radio it is
				given in a HostMeshPlatform - the T-Deck Pro's SX1262
				(src/host/esp32/main/tdeck_lora.cpp), or on a desktop a
				pretend radio whose air is a UDP multicast group
				(HostMeshAir.cpp, newton --mesh-air), so that desktop
				newtons and tools/mesh's peer can mesh with each other.  It
				is run by the host on a thread of the host's own
				(HostMeshRun never returns), the only thread that touches
				MeshCore, which is not thread-safe; everything else reaches
				it through a request queue and comes back through a
				snapshot of its state, its contacts and a queue of the
				messages received, each under the engine's lock.

				Ported from lilyphone1's src/apps/mesh_net.cpp (the owner's
				own code): its node (no forwarding; MeshCore's auto-adding
				of contacts; its acknowledgement and timeout rules), its
				radio presets and its public channel are kept; its UI
				tables, GPS position sharing and BLE companion are not.
				Changed from it: the mesh clock follows the host's (it was
				never ticked), contacts are kept across a restart, a node
				may have several sends in flight (each its own
				acknowledgement and timeout, tried again up to three
				times), and the radio is put to sleep before its power is
				cut (docs/host-tdeck-pro-mesh.md).

				Text crosses the interface as UTF-8; a node is named by its
				public key in hex (64 characters).

				The NewtonScript natives (HostMeshNatives.cpp, registered
				with the view functions, host/HostViews.cpp):

					HostMeshAvailable()		true when there is a mesh node
					HostMeshStatus()		{state, name, id, region,
											contacts, received, sent,
											packetsHeard, packetsSent}: state
											'starting, 'on or 'off (the radio
											powered down), id the first 4
											bytes of this node's key in hex;
											received and sent are messages,
											the packets everything on the air
											(adverts, acknowledgements, others'
											traffic: the radio is working)
					HostMeshContacts()		the nodes heard from, newest first -
											[{key, name, kind, hops, heard}]:
											kind 'chat, 'repeater, 'room,
											'sensor; hops nil when no route is
											known; heard in minutes since 1904;
											saved (a favourite) true or nil
					HostMeshSaveContact(key, name, saved)
											saved non-nil: the node kept for
											good (MeshCore's favourite: kept
											first across restarts, never the
											one to make room), added by its
											key alone when it was not known
											(name nil: as heard, or its id);
											nil: no longer saved
					HostMeshSendText(key, text)
											key nil: the Public channel
											==> an id for HostMeshSendState,
											or nil
					HostMeshSendState(id)	'busy, 'sent (on the air - all a
											channel message gets), 'delivered
											(acknowledged), 'failed (nil: no
											such sending)
					HostMeshNextMessage()	the oldest message received and not
											yet taken - {key, name, channel,
											text, time, age}: key the sender's
											(nil on a channel), channel the
											channel's name (nil: to this node),
											name the sender's as known (a
											channel message's own "name: "),
											time when it was sent by the
											sender's clock (minutes since 1904,
											GMT), age how many minutes ago by
											ours - or nil
					HostMeshAdvertise()		this node announced now
					HostMeshChannels()		the channels this node is in, Public
											first - [{name, secret}], secret in
											hex
					HostMeshJoinChannel(name, secret)
											secret nil for a hashtag channel
											("#name"), else 32 or 64 hex digits
											or base64 ==> true, or why not:
											'badName, 'badSecret, 'full,
											'already
					HostMeshLeaveChannel(name)
					HostMeshSendChannelText(channel, text)
											==> an id for HostMeshSendState
					HostMeshSetName(name)	this node's name, announced
					HostMeshSetLocation(latitude, longitude)
											this node's position in its adverts
											from the next on (degrees, north
											and east positive); nil: none
					HostMeshRegions()		the radio presets' names
					HostMeshRegion()		the one in use (an index), or nil
											(the radio set otherwise)
					HostMeshSetRegion(index)
											the radio retuned to that preset
					HostMeshRegionSettings(index)
											a preset's {frequency, bandwidth,
											spreadingFactor, codingRate,
											pathHashSize}
					HostMeshRadio()			the radio's {frequency (MHz, a
											real), bandwidth (kHz),
											spreadingFactor, codingRate (5-8),
											txPower (dBm), pathHashSize (1-3:
											the mesh's, a flood path's bytes
											per hop)}
					HostMeshSetRadio(frame)	the radio set so (==> whether the
											radio takes it)
					HostMeshBandwidths()	the bandwidths it takes (kHz)
					HostMeshPowerSwitchable(), HostMeshPowered(),
					HostMeshSetPower(on)	the radio's power, where the board
											can switch it
*/

#ifndef __HOSTMESH_H
#define __HOSTMESH_H

#include <stddef.h>
#include <stdint.h>

namespace mesh { class Radio; }

const long	kHostMeshKeySize = 32;			// a public key's bytes
const long	kHostMeshKeyHexSize = 65;		// ... in hex, its terminator included
const long	kHostMeshNameSize = 32;			// a node's or channel's name, ditto
const long	kHostMeshTextSize = 192;		// a message's text in UTF-8 (MeshCore's
											// MAX_TEXT_LEN, 160, its terminator, room)
const long	kHostMeshContactsMax = 64;		// MAX_CONTACTS (thirdparty/meshcore/CMakeLists.txt)

// The radio's settings: all four of frequency, bandwidth, spreading factor
// and coding rate must match a mesh's to hear it
struct HostMeshRadioSettings
{
	float	frequency;		// MHz
	float	bandwidth;		// kHz
	int		spreadingFactor;
	int		codingRate;		// 5..8 (4/5 .. 4/8)
	int		txPower;		// dBm
	int		pathHashSize;	// (the mesh's, not the radio's: bytes of each hop's hash in a flood's path, 1-3)
};

// What the host gives the engine (each call from the engine's thread only)
struct HostMeshPlatform
{
	void*			ref;
	mesh::Radio*	radio;
	// ==> the radio started (again, after its power came back) or retuned
	// with these settings
	bool			(*radioStart)(void* ref, const HostMeshRadioSettings* settings);
	// the radio to sleep: its power is about to be cut
	void			(*radioSleep)(void* ref);
	// a small blob kept across restarts (the identity, the settings, the
	// contacts): ==> whether there was one (*size its size, at most the
	// size given)
	bool			(*load)(void* ref, const char* name, void* bytes, long* size);
	void			(*save)(void* ref, const char* name, const void* bytes, long size);
	const char*		defaultName;	// this node's name until it is given one
	// the radio's receive errors so far (NULL: not counted)
	long			(*radioErrors)(void* ref);
	// called each time round the engine's loop: a received packet's
	// interrupt missed is found (its line still raised) and handed on;
	// ==> how many times so far (NULL: not watched)
	long			(*radioCheck)(void* ref);
	// how often the radio was found out of receive (neither receiving nor
	// sending) and put back, and for how long all told (NULL: not watched)
	long			(*radioIdle)(void* ref, long* idleMs);
	// the radio's estimate of the last packet's frequency error, in Hz
	// (NULL: not told) - at a narrow bandwidth an offset costs sensitivity
	long			(*radioFrequencyError)(void* ref);
	// the packet just read (raw, len) read out of the radio again ==> how
	// many bytes differ, and where the first does; -1: it could not be read
	// again (NULL: not done)
	int				(*radioReread)(void* ref, const uint8_t* raw, int len, int* first);
	// the engine's wait between its looks at the radio, up to ms, cut short
	// by wake (from any thread; a device's radio interrupt wakes it too) - a
	// device lets its processor sleep meanwhile; the engine waits 5 ms while
	// it has packets to send and longer otherwise (NULL, both: its own wait,
	// 5 ms each time)
	void			(*wait)(void* ref, long ms);
	void			(*wake)(void* ref);
};

// The engine's thread
void		HostMeshRun(const HostMeshPlatform* platform);	// never returns
void		HostMeshWillRun(void);	// the host will run it (said early, before the world boots)
bool		HostMeshStarted(void);

// Random bytes, from the host (keys, packet nonces): the source a board
// gives (its hardware's), or std::random_device
extern "C" void	HostMeshRandom(void* bytes, size_t size);
void		HostMeshSetRandomSource(void (*source)(void* bytes, size_t size));

// The node's state
enum HostMeshState { kHostMeshNone, kHostMeshStarting, kHostMeshOn, kHostMeshOff };

struct HostMeshStatusInfo
{
	HostMeshState			state;
	char					name[kHostMeshNameSize];
	char					key[kHostMeshKeyHexSize];
	char					region[kHostMeshNameSize];
	HostMeshRadioSettings	radio;
	long					contacts;
	long					received;	// messages
	long					sent;
	long					packetsHeard;	// on the air, anyone's (MeshCore's dispatcher counts)
	long					packetsSent;
	// what the radio hears, to tell a deaf radio from packets dropped later:
	// packets by MeshCore payload type (PAYLOAD_TYPE_*, repeats included),
	// channel messages by whether a channel this node has took them, the
	// radio's receive errors (CRC; -1: the platform does not count them),
	// and the last packet's signal
	long					heardByType[16];
	long					channelKnown;		// GRP_TXT/GRP_DATA a channel of ours decrypted
	long					channelUnknown;		// ... no channel of ours could
	long					channelMismatched;	// ... of those, ones whose hash is a channel of ours (its MAC wrong)
	int						lastUnknownHash;	// the last of those's channel hash (-1: none)
	long					radioErrors;
	long					radioRecoveries;	// the radio's interrupt found missed (-1: not watched)
	long					radioIdleRestarts;	// the radio found out of receive, put back (-1: not watched)
	long					radioIdleMs;		// ... how long it had been out, all told
	int						errFlags;			// MeshCore's dispatcher's (ERR_EVENT_*: 1 pool full, 2 channel busy too long, 4 not receiving for 8 s)
	int						noiseFloor;			// dBm, MeshCore's sampling (0: not yet)
	// every packet the radio read intact (its CRC good), before MeshCore
	// parses it - and those its parser drops unseen: a payload version past
	// 1, a path mode of 3, a path or payload longer than it takes; and the
	// packets free in its pool (none: a packet read is dropped unseen)
	long					rawPackets;
	long					rawDropped;
	int						lastDroppedHeader;	// (its header and path-length bytes: header << 8 | path_len; -1 none)
	int						poolFree;
	long					lastFreqError;		// Hz, the radio's estimate for the last packet (0: not told)
	// each packet read out of the radio a second time and compared
	// (radioReread): how many were, and how many came back different -
	// the read itself unreliable (a packet damaged after its CRC was checked)
	long					rereads;
	long					rereadDiffers;
	int						lastRereadOffset;	// (the first byte that differed; -1 none)
	bool					hasLocation;		// the position in this node's adverts (HostMeshSetLocation)
	double					latitude, longitude;
	float					lastRssi, lastSnr;
};
void		HostMeshGetStatus(HostMeshStatusInfo* status);

enum HostMeshKind { kHostMeshKindNone, kHostMeshKindChat, kHostMeshKindRepeater, kHostMeshKindRoom, kHostMeshKindSensor };

struct HostMeshContact
{
	char		key[kHostMeshKeyHexSize];
	char		name[kHostMeshNameSize];
	int			kind;		// HostMeshKind
	int			hops;		// -1: no route known (a message goes by flood)
	uint32_t	heard;		// when, by the mesh clock (Unix seconds)
	bool		saved;		// a favourite: kept first, never the one to make room
};
// ==> how many contacts there are; as many as fit given, newest first
long		HostMeshGetContacts(HostMeshContact* contacts, long max);

// The channels this node is in: Public always first, then those joined
// (kept across restarts).  A hashtag channel ("#name") has the first 16
// bytes of SHA-256 of its name, "#" included, as its secret - as every
// MeshCore application makes it (meshcore_py's set_channel), so that anyone
// who knows the name is in it; any other is private, its secret (16 or 32
// bytes) shared by those in it.
const long	kHostMeshChannelsMax = 8;		// MAX_GROUP_CHANNELS (thirdparty/meshcore/CMakeLists.txt)
const long	kHostMeshSecretHexSize = 65;	// a secret of 32 bytes in hex, its terminator included

struct HostMeshChannel
{
	char		name[kHostMeshNameSize];
	char		secret[kHostMeshSecretHexSize];	// in hex (32 or 64 digits)
};
long		HostMeshGetChannels(HostMeshChannel* channels, long max);

// each packet heard printed (stderr): its type, route, path (the hash of
// each repeater it came through), RSSI, SNR and frequency error - to see
// which nodes are heard, and how far off-tune each is
void		HostMeshTraceRx(bool on);

// the last channel packet heard that no channel of this node's could
// decrypt, as the radio gave it (its bytes in hex, "" none) - to see why
long		HostMeshLastUnreadable(char* hex, long size);
// ... and the last whose channel hash is one of this node's channels', yet
// none of them could decrypt it (its MAC wrong: a damaged packet, or a
// channel of the same hash) - the puzzling kind
long		HostMeshLastMismatched(char* hex, long size);

enum HostMeshJoin { kHostMeshJoined, kHostMeshJoinBadName, kHostMeshJoinBadSecret, kHostMeshJoinFull, kHostMeshJoinAlready, kHostMeshJoinNoNode };
// a channel joined: secret NULL or "" for a hashtag channel, else 32 or 64
// hex digits, or the same in base64 (24 or 44 characters, as MeshCore's
// applications show it)
int			HostMeshJoinChannel(const char* name, const char* secret);
bool		HostMeshLeaveChannel(const char* name);		// (not Public)
// ==> an id for HostMeshGetSendState, or 0
long		HostMeshSendChannelText(const char* channel, const char* text);

struct HostMeshMessage
{
	char		key[kHostMeshKeyHexSize];	// the sender's ("" on a channel)
	char		name[kHostMeshNameSize];	// the sender's, as known
	char		channel[kHostMeshNameSize];	// "" to this node
	char		text[kHostMeshTextSize];
	uint32_t	sent;		// by the sender's clock (Unix seconds)
	uint32_t	received;	// by the mesh clock (Unix seconds)
	uint32_t	now;		// the mesh clock as it was taken (for its age)
};
bool		HostMeshNextMessage(HostMeshMessage* message);

// Sending: key NULL or "" the Public channel ==> an id, 0 none
enum HostMeshSendState { kHostMeshSendNone, kHostMeshSendBusy, kHostMeshSendSent, kHostMeshSendDelivered, kHostMeshSendFailed };
long		HostMeshSendText(const char* key, const char* text);
int			HostMeshGetSendState(long id);

bool		HostMeshAdvertise(void);
bool		HostMeshSetName(const char* name);
// this node's position, put in its adverts from the next on (has false:
// none - adverts without one, as before)
void		HostMeshSetLocation(bool has, double latitude, double longitude);
// a contact saved (added by its key alone when not known; name "" or NULL:
// the one it has, or its id) or no longer saved
bool		HostMeshSetSaved(const char* key, const char* name, bool saved);

long		HostMeshRegionCount(void);
const char*	HostMeshRegionName(long index);
long		HostMeshRegion(void);					// the preset the radio is on, or -1 (its own settings)
bool		HostMeshSetRegion(long index);			// the preset's settings (the power kept)
bool		HostMeshRegionSettings(long index, HostMeshRadioSettings* settings);	// a preset's (the power left)

// The radio's settings themselves: any the radio takes - 150-960 MHz, one of
// a LoRa radio's bandwidths (HostMeshBandwidth), SF 5-12, CR 5-8 (4/5-4/8),
// -9-22 dBm; kept, the radio retuned, this node announced on a new channel
void		HostMeshGetRadio(HostMeshRadioSettings* settings);
bool		HostMeshSetRadio(const HostMeshRadioSettings* settings);	// ==> whether they were taken
long		HostMeshBandwidthCount(void);
float		HostMeshBandwidth(long index);			// kHz

// The radio's power, which a board can switch (the T-Deck Pro's GPIO 46):
// the board gives the switch and whether it is on (kept off from before);
// HostMeshSetPower puts the radio to sleep first, or starts it again after
// ==> whether it was taken
void		HostMeshSetPowerSwitch(bool (*powerSwitch)(bool on), bool on);
bool		HostMeshPowerSwitchable(void);
bool		HostMeshPowered(void);
bool		HostMeshSetPower(bool on);

// A desktop's pretend radio (HostMeshAir.cpp): spec "lan" or "lan:port" (the
// UDP multicast group 239.255.78.121 on that port, 3685 by default);
// statePrefix, when there is one, the path the node's blobs are kept beside
// ("<statePrefix>.mesh-identity", ...).  ==> whether the engine was started
// (on a thread of its own)
bool		HostMeshAirStart(const char* spec, const char* statePrefix, const char* name);

// The NewtonScript natives (HostMeshNatives.cpp)
void		HostRegisterMeshFunctions(void);

#endif
