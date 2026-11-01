/*
	File:		hal/host/HostCellular.cpp

	Contains:	The cellular modem engine (HostCellular.h): one thread
				owning the module's byte link, in AT commands.

				Ported from lilyphone1's src/peripherals/peri_modem.cpp, the
				owner's firmware for the same board, where every piece of
				the dialect below was found on the module itself:

				- the module stores a message and says where (+CMTI, with
				  AT+CNMI=2,1): reading it is a command of its own, so a
				  +CMTI only notes the slot, and the slot is read between
				  commands, then deleted (the SIM holds few);
				- text mode with the GSM character set: a body the sender
				  could not fit in GSM's 7-bit alphabet (curly quotes, any
				  emoji) arrives as UCS2 hex, told apart by the data coding
				  scheme AT+CSDH=1 adds to +CMGR's header;
				- a message is sent as GSM-7 when it is plain ASCII, and as
				  UCS2 otherwise (both AT+CSCS="UCS2" and AT+CSMP's dcs 8,
				  the number in hex too, the defaults put back after), and
				  if the module will not take UCS2, folded to ASCII and
				  sent as GSM-7 after all; AT+CMGS ends with CR alone (an
				  LF would land in the body);
				- the URCs alone do not say reliably when a call ends, so
				  while one is up AT+CLCC is asked every 2 s;
				- the network's time (NITZ, AT+CTZU=1) is read with
				  AT+CCLK? once registered and whenever the network says
				  it moved.

				Portable C++ (std::mutex and std::chrono; ESP-IDF has them
				over FreeRTOS), with nothing of the Newton's: the natives
				are HostCellularNatives.cpp.
*/

#include "HostCellular.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

const long		kLineSize = 256;
const long		kHexBodySize = 4 * 160 + 64;		// a UCS2 body: four hex digits a character
const long		kStatusPeriodMs = 15000;		// signal and registration asked again after this
const long		kCallPollMs = 2000;				// a call's state confirmed (AT+CLCC) this often
const long		kClockRetryMs = 30000;			// the network's time asked for again after this
const long		kRingWaitMs = 500;				// a RING's +CLIP waited for this long
const long		kPendingSlots = 8;
const long		kRequestQueue = 6;
const long		kTextQueue = 8;
const long		kEventQueue = 16;
const long		kSendStates = 8;

static long long
Millis(void)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void
SleepMs(long ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static void
Copy(char* to, const char* from, long size)
{
	strncpy(to, from, size - 1);
	to[size - 1] = 0;
}


/*------------------------------------------------------------------------------
	The engine's state: written by its thread, read by anyone, under gLock
------------------------------------------------------------------------------*/

enum RequestKind { kRequestDial, kRequestAnswer, kRequestHangUp, kRequestSendText, kRequestDigits, kRequestSpeaker };

struct Request
{
	RequestKind	kind;
	long		id;
	char		number[kHostCellNumberSize];
	char		text[kHostCellTextSize];
};

static std::mutex			gLock;
static const HostCellularLink*	gLink = nullptr;
static bool					gStarted = false;
static HostCellStatusInfo	gStatus;
static long long			gCallConnected = 0;			// Millis() at the connection; 0 none
static void					(*gClockHandler)(long long, long, bool) = nullptr;
static bool					(*gPowerSwitch)(bool) = nullptr;	// the host's switch of the module's supply; nil none
static std::atomic<bool>	gPoweredOff(false);			// switched off: nothing sent to it

static Request				gRequests[kRequestQueue] HOST_LARGE_STATIC;
static long					gRequestHead = 0, gRequestCount = 0;
static HostCellText			gTexts[kHostCellReaders][kTextQueue] HOST_LARGE_STATIC;	// (each reader its own copy)
static long					gTextHead[kHostCellReaders], gTextCount[kHostCellReaders];
static HostCellEvent		gEvents[kHostCellReaders][kEventQueue];		// (each reader its own copy)
static long					gEventHead[kHostCellReaders], gEventCount[kHostCellReaders];
static struct { long id; HostCellSendState state; }	gSends[kSendStates];
static long					gNextSendId = 1;

// the one command at a time HostCellularCommand hands over (its own lock
// keeps it one at a time; the reply is kept here, never in the asker's
// frame, so an asker that gave up waiting leaves nothing to write into)
static std::mutex			gCommandLock;
static std::condition_variable	gCommandDone;
static bool					gCommandWanted = false, gCommandFinished = false, gCommandOK = false;
static char					gCommand[kLineSize];
static long					gCommandTimeout;
static char					gCommandReply[kHostCellReplySize] HOST_LARGE_STATIC;

// (the engine's thread only)
static unsigned char		gIn[512];					// bytes read and not yet taken
static long					gInCount = 0;
static long					gPending[kPendingSlots];	// message slots a +CMTI named, not read yet
static long					gPendingCount = 0;
static bool					gClockWanted = true;		// the network's time to be read
static long long			gRingAt = 0;				// a RING waiting for its +CLIP; 0 none
static char					gRingNumber[kHostCellNumberSize];


static void
PushEvent(HostCellEventKind kind, const char* number)		// (gLock held)
{
	for (long reader = 0; reader < kHostCellReaders; reader++)
	{
		if (gEventCount[reader] == kEventQueue)
		{
			gEventHead[reader] = (gEventHead[reader] + 1) % kEventQueue;		// the oldest forgotten
			gEventCount[reader]--;
		}
		HostCellEvent* event = &gEvents[reader][(gEventHead[reader] + gEventCount[reader]++) % kEventQueue];
		event->kind = kind;
		Copy(event->number, number != nullptr ? number : "", kHostCellNumberSize);
	}
}


// The call's state as the module says it is now (number nil: unchanged);
// moving to active is the event connected, to idle the event ended (or
// busy, or no answer - the reason when the module gave one)
static void
SetCall(HostCellCallState state, const char* number, HostCellEventKind ending = kHostCellEventEnded)
{
	std::lock_guard<std::mutex> hold(gLock);
	HostCellCallState was = gStatus.call;
	gStatus.call = state;
	if (number != nullptr && number[0] != 0)
		Copy(gStatus.number, number, kHostCellNumberSize);
	if (state == kHostCellCallActive)
	{
		if (gCallConnected == 0)
			gCallConnected = Millis();
		if (was != kHostCellCallActive)
			PushEvent(kHostCellEventConnected, gStatus.number);
	}
	else
		gCallConnected = 0;
	if (state == kHostCellCallIdle)
	{
		if (was != kHostCellCallIdle)
			PushEvent(ending, gStatus.number);
		gStatus.number[0] = 0;
	}
}


static HostCellCallState
CallState(void)
{
	std::lock_guard<std::mutex> hold(gLock);
	return gStatus.call;
}


static void
SetState(HostCellState state)
{
	std::lock_guard<std::mutex> hold(gLock);
	gStatus.state = state;
}


static void
SetSendState(long id, HostCellSendState state)
{
	std::lock_guard<std::mutex> hold(gLock);
	long slot = id % kSendStates;
	gSends[slot].id = id;
	gSends[slot].state = state;
}


/*------------------------------------------------------------------------------
	Text: UCS2 hex and UTF-8
------------------------------------------------------------------------------*/

static long
HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}


static bool
HexUnit(const char* hex, unsigned long* unit)
{
	*unit = 0;
	for (int k = 0; k < 4; k++)
	{
		long d = HexDigit(hex[k]);
		if (d < 0)
			return false;
		*unit = (*unit << 4) | (unsigned long) d;
	}
	return true;
}


// A UCS2 body as the module gives it in text mode ("004C0069...") as
// UTF-8: false (out untouched) when it is not well-formed UCS2
bool
HostCellularUCS2ToUTF8(const char* hex, char* out, long size)
{
	size_t n = strlen(hex);
	if (n < 4 || (n % 4) != 0)
		return false;
	long o = 0;
	for (size_t i = 0; i + 3 < n; i += 4)
	{
		unsigned long cp;
		if (!HexUnit(hex + i, &cp))
			return false;
		if (cp >= 0xD800 && cp <= 0xDBFF)			// above the BMP - every emoji - a surrogate pair
		{
			unsigned long lo;
			if (i + 7 >= n || !HexUnit(hex + i + 4, &lo) || lo < 0xDC00 || lo > 0xDFFF)
				return false;
			cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
			i += 4;
		}
		else if (cp >= 0xDC00 && cp <= 0xDFFF)
			return false;							// a trailing surrogate alone
		if (cp < 0x80)
		{
			if (o + 1 >= size)
				break;
			out[o++] = (char) cp;
		}
		else if (cp < 0x800)
		{
			if (o + 2 >= size)
				break;
			out[o++] = (char) (0xC0 | (cp >> 6));
			out[o++] = (char) (0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			if (o + 3 >= size)
				break;
			out[o++] = (char) (0xE0 | (cp >> 12));
			out[o++] = (char) (0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (char) (0x80 | (cp & 0x3F));
		}
		else
		{
			if (o + 4 >= size)
				break;
			out[o++] = (char) (0xF0 | (cp >> 18));
			out[o++] = (char) (0x80 | ((cp >> 12) & 0x3F));
			out[o++] = (char) (0x80 | ((cp >> 6) & 0x3F));
			out[o++] = (char) (0x80 | (cp & 0x3F));
		}
	}
	out[o] = 0;
	return true;
}


// UTF-8 as the hex a UCS2 body (or, under AT+CSCS="UCS2", any string
// parameter) is written in: false when malformed or out of room
bool
HostCellularUTF8ToUCS2(const char* utf8, char* out, long size)
{
	const unsigned char* p = (const unsigned char*) utf8;
	long o = 0;
	while (*p)
	{
		unsigned long cp;
		int n;
		if (*p < 0x80)
			cp = *p, n = 1;
		else if ((*p & 0xE0) == 0xC0)
			cp = *p & 0x1F, n = 2;
		else if ((*p & 0xF0) == 0xE0)
			cp = *p & 0x0F, n = 3;
		else if ((*p & 0xF8) == 0xF0)
			cp = *p & 0x07, n = 4;
		else
			return false;
		for (int k = 1; k < n; k++)
		{
			if ((p[k] & 0xC0) != 0x80)
				return false;
			cp = (cp << 6) | (p[k] & 0x3F);
		}
		p += n;
		unsigned long units[2];
		int count = 1;
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			units[0] = 0xD800 + (cp >> 10);
			units[1] = 0xDC00 + (cp & 0x3FF);
			count = 2;
		}
		else
			units[0] = cp;
		for (int k = 0; k < count; k++)
		{
			if (o + 5 > size)
				return false;
			snprintf(out + o, size - o, "%04lX", units[k]);
			o += 4;
		}
	}
	out[o] = 0;
	return o > 0;
}


// For a module that reports no data coding scheme: hex of a UCS2 body's
// shape, with a hex letter in it (a message all digits - a one-time code -
// is not taken for UCS2)
static bool
LooksLikeUCS2(const char* s)
{
	size_t n = strlen(s);
	if (n < 4 || (n % 4) != 0)
		return false;
	bool letter = false;
	for (size_t i = 0; i < n; i++)
	{
		if (isdigit((unsigned char) s[i]))
			continue;
		if (HexDigit(s[i]) < 0)
			return false;
		letter = true;
	}
	return letter;
}


static bool
IsPlainASCII(const char* s)
{
	for (const unsigned char* p = (const unsigned char*) s; *p; p++)
		if (*p >= 0x80)
			return false;
	return true;
}


// The punctuation that would force UCS2 folded to ASCII, anything else
// that cannot be a '?' - the last resort when a module will not send UCS2
static void
FoldToASCII(const char* in, char* out, long size)
{
	static const struct { const char* utf8; const char* ascii; } kFolds[] = {
		{ "\xE2\x80\x9C", "\"" }, { "\xE2\x80\x9D", "\"" }, { "\xE2\x80\x98", "'" }, { "\xE2\x80\x99", "'" },
		{ "\xE2\x80\xA6", "..." }, { "\xE2\x80\x93", "-" }, { "\xE2\x80\x94", "-" } };
	const unsigned char* p = (const unsigned char*) in;
	long o = 0;
	while (*p && o + 1 < size)
	{
		if (*p < 0x80)
		{
			out[o++] = (char) *p++;
			continue;
		}
		bool folded = false;
		for (size_t i = 0; i < sizeof(kFolds) / sizeof(kFolds[0]) && !folded; i++)
		{
			size_t n = strlen(kFolds[i].utf8), a = strlen(kFolds[i].ascii);
			if (strncmp((const char*) p, kFolds[i].utf8, n) == 0 && o + (long) a + 1 < size)
			{
				memcpy(out + o, kFolds[i].ascii, a);
				o += a;
				p += n;
				folded = true;
			}
		}
		if (folded)
			continue;
		int n = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 1;
		while (n-- > 0 && *p)
			p++;
		out[o++] = '?';
	}
	out[o] = 0;
}


/*------------------------------------------------------------------------------
	Time stamps: "25/09/04,10:22:33+40" (+CMGR's and +CCLK?'s) - the local
	time, then the zone in quarter hours east of Greenwich (optional)
------------------------------------------------------------------------------*/

// days from 1970-01-01 to a date of the proleptic Gregorian calendar (the
// day may be one outside its month: it is counted on)
static long long
DaysFromCivil(long y, long m, long d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (long long) era * 146097 + doe - 719468;
}


bool
HostCellularParseStamp(const char* stamp, long long* utcSeconds, long* offsetMinutes, bool* hasOffset)
{
	int year, month, day, hour, minute, second, quarters = 0;
	int n = sscanf(stamp, "%d/%d/%d,%d:%d:%d%d", &year, &month, &day, &hour, &minute, &second, &quarters);
	if (n < 6 || month < 1 || month > 12)
		return false;
	*hasOffset = n >= 7;
	*offsetMinutes = *hasOffset ? quarters * 15 : 0;
	*utcSeconds = DaysFromCivil(year + 2000, month, day) * 86400LL + hour * 3600LL + minute * 60LL + second
				- *offsetMinutes * 60LL;
	return true;
}


/*------------------------------------------------------------------------------
	Fields of a line
------------------------------------------------------------------------------*/

// The which'th double-quoted field (from 0) ==> whether there is one
static bool
QuotedField(const char* line, int which, char* out, long size)
{
	const char* p = line;
	for (int i = 0; i <= which; i++)
	{
		if ((p = strchr(p, '"')) == nullptr)
			return false;
		p++;
		if (i < which)
		{
			if ((p = strchr(p, '"')) == nullptr)
				return false;
			p++;
		}
	}
	long n = 0;
	while (*p && *p != '"' && n < size - 1)
		out[n++] = *p++;
	out[n] = 0;
	return true;
}


// The last double-quoted field
static bool
LastQuotedField(const char* line, char* out, long size)
{
	const char* close = strrchr(line, '"');
	if (close == nullptr || close == line)
		return false;
	const char* open = close - 1;
	while (open > line && *open != '"')
		open--;
	if (*open != '"')
		return false;
	open++;
	long n = 0;
	while (open < close && n < size - 1)
		out[n++] = *open++;
	out[n] = 0;
	return n > 0;
}


// A +CMGR header:
//   +CMGR: "REC READ","+614...",,"25/09/04,10:22:33+40",145,4,0,8,"+614...",145,7
// the sender, the stamp (found by its shape: with AT+CSDH=1 the service
// centre's number is quoted after it) and the data coding scheme, the
// fourth number after the stamp (-1: none given)
void
HostCellularParseCMGR(const char* line, char* number, long numberSize, char* stamp, long stampSize, long* dcs)
{
	number[0] = stamp[0] = 0;
	*dcs = -1;
	QuotedField(line, 1, number, numberSize);
	for (int i = 0; i < 6; i++)
	{
		char field[40];
		int a, b, c, d, e, f;
		if (!QuotedField(line, i, field, sizeof(field)))
			break;
		if (sscanf(field, "%d/%d/%d,%d:%d:%d", &a, &b, &c, &d, &e, &f) != 6)
			continue;
		Copy(stamp, field, stampSize);
		const char* p = strstr(line, field);
		if (p == nullptr)
			break;
		p += strlen(field);
		if (*p == '"')
			p++;
		for (int commas = 0; *p && *p != '"'; p++)		// (a quote: the service centre - no dcs)
			if (*p == ',' && ++commas == 4)
			{
				if (isdigit((unsigned char) p[1]))
					*dcs = atol(p + 1);
				break;
			}
		break;
	}
}


/*------------------------------------------------------------------------------
	The link: lines in, commands out
------------------------------------------------------------------------------*/

// More bytes from the link, waiting up to timeoutMs ==> whether any came
static bool
Fill(long timeoutMs)
{
	if (gInCount == (long) sizeof(gIn))
		return true;
	long n = gLink->read(gLink->ref, gIn + gInCount, sizeof(gIn) - gInCount, timeoutMs);
	if (n < 0)
	{
		SleepMs(timeoutMs > 0 ? timeoutMs : 1);
		return false;
	}
	gInCount += n;
	return n > 0;
}


static void
Take(long count)
{
	memmove(gIn, gIn + count, gInCount - count);
	gInCount -= count;
}


// A whole line already read, if there is one ==> its length, -1 none
// (blank lines skipped)
static long
LineIn(char* line, long size)
{
	for (;;)
	{
		long skip = 0;
		while (skip < gInCount && (gIn[skip] == '\r' || gIn[skip] == '\n'))
			skip++;
		if (skip > 0)
			Take(skip);
		long end = 0;
		while (end < gInCount && gIn[end] != '\r' && gIn[end] != '\n')
			end++;
		if (end == gInCount)
		{
			if (gInCount == (long) sizeof(gIn))			// a line longer than the buffer: taken in pieces
				end = gInCount;
			else
				return -1;
		}
		long n = end < size - 1 ? end : size - 1;
		memcpy(line, gIn, n);
		line[n] = 0;
		Take(end);
		if (n > 0)
			return n;
	}
}


// The next line, waiting up to timeoutMs ==> its length, -1 none came
static long
ReadLine(char* line, long size, long timeoutMs)
{
	long long until = Millis() + timeoutMs;
	for (;;)
	{
		long n = LineIn(line, size);
		if (n >= 0)
			return n;
		long left = (long) (until - Millis());
		if (left <= 0)
			return -1;
		Fill(left < 50 ? left : 50);
	}
}


static void
Write(const char* text)
{
	gLink->write(gLink->ref, text, (long) strlen(text));
}


static void
WriteCommand(const char* command, const char* ending = "\r\n")
{
	Write(command);
	Write(ending);
}


static bool
IsResult(const char* line, bool* ok)
{
	if (strcmp(line, "OK") == 0)
		return *ok = true;
	*ok = false;
	return strcmp(line, "ERROR") == 0 || strncmp(line, "+CME ERROR", 10) == 0 || strncmp(line, "+CMS ERROR", 10) == 0;
}


static bool
IsURC(const char* line)
{
	static const char* const kURCs[] = { "RING", "+CLIP:", "+CMTI:", "NO CARRIER", "VOICE CALL:", "BUSY", "NO ANSWER",
		"*PSUTTZ:", "+CTZV:", "+CTZE:", "DST:", "MISSED_CALL:" };
	for (size_t i = 0; i < sizeof(kURCs) / sizeof(kURCs[0]); i++)
		if (strncmp(line, kURCs[i], strlen(kURCs[i])) == 0)
			return true;
	return false;
}


static void	HandleURC(const char* line);

// What the module has said by itself, handled (anything already read, and
// whatever comes within waitMs)
static void
HandleUnsolicited(long waitMs)
{
	char line[kLineSize];
	if (waitMs > 0)
		Fill(waitMs);
	while (Fill(0))
		;
	while (LineIn(line, sizeof(line)) >= 0)
		HandleURC(line);
}


// A command, its answer read to its result code ==> whether that was OK;
// the first line of information in first (when given), every line in all
// (when given, each ended by '\n').  A URC among the lines is handled, not
// taken for the answer.
static bool
Exec(const char* command, char* first, long firstSize, long timeoutMs, char* all = nullptr, long allSize = 0)
{
	char line[kLineSize];
	bool gotFirst = false, ok;
	if (first != nullptr && firstSize > 0)
		first[0] = 0;
	if (all != nullptr && allSize > 0)
		all[0] = 0;
	if (gPoweredOff)
		return false;			// (no module: the engine waits for it as for one not started)
	HandleUnsolicited(0);
	WriteCommand(command);
	long long until = Millis() + timeoutMs;
	while (Millis() < until)
	{
		if (ReadLine(line, sizeof(line), 200) < 0)
			continue;
		if (IsResult(line, &ok))
			return ok;
		if (strcmp(line, command) == 0)					// (the echo, before ATE0 has been taken)
			continue;
		if (IsURC(line))
		{
			HandleURC(line);
			continue;
		}
		if (!gotFirst && first != nullptr && firstSize > 0)
		{
			Copy(first, line, firstSize);
			gotFirst = true;
		}
		if (all != nullptr)
		{
			long used = (long) strlen(all);
			if (used + (long) strlen(line) + 2 <= allSize)
			{
				strcpy(all + used, line);
				strcat(all + used, "\n");
			}
		}
	}
	return false;
}


// The '>' of AT+CMGS, everything said before it kept in said ==> whether
// it came
static bool
WaitForPrompt(long timeoutMs, char* said, long saidSize)
{
	long n = 0;
	long long until = Millis() + timeoutMs;
	for (;;)
	{
		for (long i = 0; i < gInCount; i++)
			if (gIn[i] == '>')
			{
				Take(i + 1);
				said[n] = 0;
				return true;
			}
			else if (gIn[i] != '\r' && gIn[i] != '\n' && n < saidSize - 1)
				said[n++] = (char) gIn[i];
		gInCount = 0;
		said[n] = 0;
		long left = (long) (until - Millis());
		if (left <= 0)
			return false;
		Fill(left < 50 ? left : 50);
	}
}


/*------------------------------------------------------------------------------
	What the module says by itself
------------------------------------------------------------------------------*/

static void	News(void);


static void
RingHeard(const char* number)
{
	if (CallState() == kHostCellCallIdle || CallState() == kHostCellCallIncoming)
		SetCall(kHostCellCallIncoming, number);
	std::lock_guard<std::mutex> hold(gLock);
	PushEvent(kHostCellEventRing, number != nullptr && number[0] != 0 ? number : gStatus.number);
	News();
}


static void
HandleURC(const char* line)
{
	if (strncmp(line, "RING", 4) == 0)
	{
		// (its +CLIP, if the network gave the number, comes next: the ring
		// is told once that has come, or kRingWaitMs without it)
		if (gRingAt != 0)
			RingHeard(gRingNumber);
		gRingAt = Millis();
		gRingNumber[0] = 0;
		if (CallState() == kHostCellCallIdle)
			SetCall(kHostCellCallIncoming, nullptr);
	}
	else if (strncmp(line, "+CLIP:", 6) == 0)
	{
		char number[kHostCellNumberSize];
		if (!QuotedField(line, 0, number, sizeof(number)))
			number[0] = 0;
		gRingAt = 0;
		RingHeard(number);
	}
	else if (strncmp(line, "VOICE CALL: BEGIN", 17) == 0)
		SetCall(kHostCellCallActive, nullptr);
	else if (strncmp(line, "VOICE CALL: END", 15) == 0 || strncmp(line, "NO CARRIER", 10) == 0)
		SetCall(kHostCellCallIdle, nullptr);
	else if (strncmp(line, "BUSY", 4) == 0)
		SetCall(kHostCellCallIdle, nullptr, kHostCellEventBusy);
	else if (strncmp(line, "NO ANSWER", 9) == 0)
		SetCall(kHostCellCallIdle, nullptr, kHostCellEventNoAnswer);
	else if (strncmp(line, "*PSUTTZ:", 8) == 0 || strncmp(line, "+CTZV:", 6) == 0
		  || strncmp(line, "+CTZE:", 6) == 0 || strncmp(line, "DST:", 4) == 0)
		gClockWanted = true;				// (SIMCom's firmwares differ in which they send)
	else if (strncmp(line, "+CMTI:", 6) == 0)
	{
		// +CMTI: "SM",3 - a message in slot 3, read between commands
		const char* comma = strrchr(line, ',');
		if (comma == nullptr)
			return;
		long slot = atol(comma + 1);
		for (long i = 0; i < gPendingCount; i++)
			if (gPending[i] == slot)
				return;
		if (gPendingCount < kPendingSlots)
			gPending[gPendingCount++] = slot;
	}
}


/*------------------------------------------------------------------------------
	Messages
------------------------------------------------------------------------------*/

void	(*gHostCellularNews)(void) = nullptr;


// a message or a call the Newton is to hear of: a sleeping machine woken
// (gHostCellularNews; gLock held - it must not call back)
static void
News(void)
{
	if (gHostCellularNews != nullptr)
		gHostCellularNews();
}


static void
Deliver(const char* number, const char* text, long long time)
{
	std::lock_guard<std::mutex> hold(gLock);
	News();
	for (long reader = 0; reader < kHostCellReaders; reader++)
	{
		if (gTextCount[reader] == kTextQueue)
		{
			// (the oldest forgotten: a reader that is not reading - a script's,
			// when nobody asks - must not keep the others' from coming)
			if (reader == kHostCellReaderTool)
				fprintf(stderr, "[cell] the received messages' queue is full: the oldest dropped\n");
			gTextHead[reader] = (gTextHead[reader] + 1) % kTextQueue;
			gTextCount[reader]--;
		}
		HostCellText* to = &gTexts[reader][(gTextHead[reader] + gTextCount[reader]++) % kTextQueue];
		Copy(to->number, number, kHostCellNumberSize);
		Copy(to->text, text, kHostCellTextSize);
		to->time = time;
	}
}


// The message in a slot, handed on and deleted from the SIM
static void
FetchText(long slot)
{
	static char body[kHexBodySize] HOST_LARGE_STATIC;
	static char decoded[kHostCellTextSize] HOST_LARGE_STATIC;
	char command[32], line[kLineSize], number[kHostCellNumberSize], stamp[40];
	long dcs = -1;
	bool header = false, gotBody = false, ok;
	body[0] = number[0] = stamp[0] = 0;
	snprintf(command, sizeof(command), "AT+CMGR=%ld", slot);
	HandleUnsolicited(0);
	WriteCommand(command);
	long long until = Millis() + 5000;
	while (Millis() < until)
	{
		if (ReadLine(line, sizeof(line), 200) < 0)
			continue;
		if (strcmp(line, command) == 0)
			continue;
		if (strncmp(line, "+CMGR:", 6) == 0)
		{
			HostCellularParseCMGR(line, number, sizeof(number), stamp, sizeof(stamp), &dcs);
			header = true;
		}
		else if (header && !gotBody)
		{
			Copy(body, line, sizeof(body));			// (whatever it says: a message reading "OK" is a message)
			gotBody = true;
		}
		else if (IsResult(line, &ok))
		{
			if (!ok)
				return;
			break;
		}
		else if (header)
		{
			long used = (long) strlen(body);		// the sender's own line breaks kept
			if (used < (long) sizeof(body) - 2)
			{
				body[used] = '\n';
				Copy(body + used + 1, line, sizeof(body) - used - 1);
			}
		}
	}
	if (header && number[0] != 0)
	{
		// (bits 3..2 of the data coding scheme are the alphabet: 10 is UCS2)
		bool ucs2 = dcs >= 0 ? (dcs & 0x0C) == 0x08 : LooksLikeUCS2(body);
		long long time = 0;
		long offset;
		bool hasOffset;
		if (!HostCellularParseStamp(stamp, &time, &offset, &hasOffset))
			time = 0;
		if (ucs2 && HostCellularUCS2ToUTF8(body, decoded, sizeof(decoded)))
			Deliver(number, decoded, time);
		else
			Deliver(number, body, time);
	}
	snprintf(command, sizeof(command), "AT+CMGD=%ld", slot);
	Exec(command, nullptr, 0, 3000);
}


// The messages that came while nobody was listening
static void
FetchStoredTexts(void)
{
	char line[kLineSize];
	long slots[16], n = 0;
	bool ok;
	HandleUnsolicited(0);
	WriteCommand("AT+CMGL=\"ALL\"");
	long long until = Millis() + 10000;
	while (Millis() < until)
	{
		if (ReadLine(line, sizeof(line), 300) < 0)
			continue;
		if (IsResult(line, &ok))
		{
			if (!ok)
				return;
			break;
		}
		if (strncmp(line, "+CMGL:", 6) == 0 && n < 16)
			slots[n++] = atol(line + 6);
		else if (IsURC(line))
			HandleURC(line);
	}
	for (long i = 0; i < n; i++)
		FetchText(slots[i]);
}


// One try at AT+CMGS, as GSM-7 or as UCS2 ==> whether the network took it
static bool
SendTextAs(const char* number, const char* text, bool ucs2)
{
	static char hex[kHexBodySize] HOST_LARGE_STATIC;
	char numberHex[kHostCellNumberSize * 4 + 4], command[kHostCellNumberSize * 4 + 24], line[kLineSize], said[kLineSize];
	if (ucs2)
	{
		if (!HostCellularUTF8ToUCS2(text, hex, sizeof(hex)) || !HostCellularUTF8ToUCS2(number, numberHex, sizeof(numberHex)))
			return false;
		// (with the character set GSM the module refuses UCS2: +CMS ERROR
		// 305; under UCS2 every string is hex, the number too)
		if (!Exec("AT+CSCS=\"UCS2\"", nullptr, 0, 2000))
			return false;
		if (!Exec("AT+CSMP=17,167,0,8", nullptr, 0, 2000))
		{
			Exec("AT+CSCS=\"GSM\"", nullptr, 0, 2000);
			return false;
		}
	}
	// text mode every time: in PDU mode AT+CMGS takes a length, and the
	// prompt would never come
	Exec("AT+CMGF=1", nullptr, 0, 2000);
	HandleUnsolicited(0);
	snprintf(command, sizeof(command), "AT+CMGS=\"%s\"", ucs2 ? numberHex : number);
	WriteCommand(command, "\r");
	bool sent = false, ok;
	if (!WaitForPrompt(10000, said, sizeof(said)))
	{
		fprintf(stderr, "[cell] no prompt to send to; the module said: %s\n", said[0] != 0 ? said : "nothing");
		Write("\x1B");								// (the half-made command abandoned)
	}
	else
	{
		Write(ucs2 ? hex : text);
		Write("\x1A");								// (Ctrl-Z ends the body)
		long long until = Millis() + 60000;			// (the network may take its time)
		while (Millis() < until)
		{
			if (ReadLine(line, sizeof(line), 500) < 0)
				continue;
			if (strncmp(line, "+CMGS:", 6) == 0)
				continue;
			if (IsResult(line, &ok))
			{
				sent = ok;
				if (!ok)
					fprintf(stderr, "[cell] sending refused: %s\n", line);
				break;
			}
			HandleURC(line);
		}
	}
	if (ucs2)
	{
		Exec("AT+CSCS=\"GSM\"", nullptr, 0, 2000);	// (or every later string comes back hex)
		Exec("AT+CSMP=17,167,0,0", nullptr, 0, 2000);
	}
	return sent;
}


static bool
SendText(const char* number, const char* text)
{
	if (IsPlainASCII(text))
		return SendTextAs(number, text, false);
	if (SendTextAs(number, text, true))
		return true;
	static char folded[kHostCellTextSize] HOST_LARGE_STATIC;
	FoldToASCII(text, folded, sizeof(folded));
	fprintf(stderr, "[cell] UCS2 refused; sent as GSM-7 instead\n");
	return SendTextAs(number, folded, false);
}


/*------------------------------------------------------------------------------
	Calls, the network, the clock
------------------------------------------------------------------------------*/

// The call as the module lists it (AT+CLCC): no call listed is idle; a
// module that will not answer CLCC leaves the state as the URCs left it
static void
PollCall(void)
{
	char line[kLineSize], number[kHostCellNumberSize];
	long stat = -1;
	bool ok;
	number[0] = 0;
	HandleUnsolicited(0);
	WriteCommand("AT+CLCC");
	long long until = Millis() + 3000;
	while (Millis() < until)
	{
		if (ReadLine(line, sizeof(line), 200) < 0)
			continue;
		if (IsResult(line, &ok))
		{
			if (!ok)
				return;
			break;
		}
		int id, direction, s;
		if (strncmp(line, "+CLCC:", 6) == 0)
		{
			if (stat < 0 && sscanf(line, "+CLCC: %d,%d,%d", &id, &direction, &s) == 3)
			{
				stat = s;
				QuotedField(line, 0, number, sizeof(number));
			}
		}
		else if (IsURC(line))
			HandleURC(line);
	}
	switch (stat)
	{
	case -1: SetCall(kHostCellCallIdle, nullptr); break;
	case 0: SetCall(kHostCellCallActive, number); break;
	case 4:												// incoming
	case 5: SetCall(kHostCellCallIncoming, number); break;		// waiting
	default: SetCall(kHostCellCallDialing, number); break;		// held, dialing, alerting
	}
}


// The network's time (AT+CTZU=1 keeps the module's clock to NITZ) ==>
// whether a real one was read (an unset clock says a year before 2020)
static bool
ReadClock(void)
{
	char reply[kLineSize], stamp[40];
	long long seconds;
	long offset;
	bool hasOffset;
	if (!Exec("AT+CCLK?", reply, sizeof(reply), 3000) || !LastQuotedField(reply, stamp, sizeof(stamp))
	 || !HostCellularParseStamp(stamp, &seconds, &offset, &hasOffset) || seconds < 1577836800LL)
		return false;
	if (gClockHandler != nullptr)
		gClockHandler(seconds, offset, hasOffset);
	return true;
}


// The SIM, the signal, the registration and the operator ==> false when
// the module did not answer at all
static bool
RefreshStatus(void)
{
	char reply[kLineSize];
	HostCellState state = kHostCellSearching;
	if (Exec("AT+CPIN?", reply, sizeof(reply), 3000))
	{
		if (strstr(reply, "READY") == nullptr)
			state = kHostCellLocked;				// SIM PIN, SIM PUK, ...
	}
	else if (reply[0] == 0 && !Exec("AT", nullptr, 0, 1000))
		return false;
	else
		state = kHostCellNoSIM;						// (+CME ERROR: 10, SIM not inserted)
	long rssi = 0;
	int csq, ber;
	if (Exec("AT+CSQ", reply, sizeof(reply), 3000) && sscanf(reply, "+CSQ: %d,%d", &csq, &ber) >= 1 && csq >= 0 && csq <= 31)
		rssi = -113 + 2 * csq;
	int mode, stat;
	if (state == kHostCellSearching && Exec("AT+CREG?", reply, sizeof(reply), 3000)
	 && sscanf(reply, "+CREG: %d,%d", &mode, &stat) == 2)
		state = stat == 1 ? kHostCellHome : stat == 5 ? kHostCellRoaming : kHostCellSearching;
	char name[kHostCellOperatorSize];
	name[0] = 0;
	if ((state == kHostCellHome || state == kHostCellRoaming) && Exec("AT+COPS?", reply, sizeof(reply), 5000))
		QuotedField(reply, 0, name, sizeof(name));
	std::lock_guard<std::mutex> hold(gLock);
	gStatus.state = state;
	gStatus.rssi = rssi;
	Copy(gStatus.operatorName, name, kHostCellOperatorSize);
	return true;
}


// Text mode, caller identification, messages stored and their slots told,
// the network's time kept and its changes told
static void
Configure(void)
{
	Exec("ATE0", nullptr, 0, 2000);					// (no echo)
	Exec("AT+CMEE=1", nullptr, 0, 2000);			// (numeric errors)
	Exec("AT+CLIP=1", nullptr, 0, 2000);
	Exec("AT+CMGF=1", nullptr, 0, 2000);
	Exec("AT+CSCS=\"GSM\"", nullptr, 0, 2000);
	Exec("AT+CPMS=\"SM\",\"SM\",\"SM\"", nullptr, 0, 5000);
	Exec("AT+CNMI=2,1,0,0,0", nullptr, 0, 2000);
	Exec("AT+CSDH=1", nullptr, 0, 2000);			// (the data coding scheme with each message)
	Exec("AT+CSMP=17,167,0,0", nullptr, 0, 2000);
	Exec("AT+CTZU=1", nullptr, 0, 2000);
	Exec("AT+CTZR=1", nullptr, 0, 2000);
	Exec("AT+COPS=3,0", nullptr, 0, 2000);			// (the operator by name: the A7682E gives "50502" otherwise)
	if (gLink->setup != nullptr)
		Exec(gLink->setup, nullptr, 0, 2000);
	gClockWanted = true;
}


static void
Perform(const Request& request)
{
	char command[kHostCellNumberSize + 16];
	switch (request.kind)
	{
	case kRequestDial:
		snprintf(command, sizeof(command), "ATD%s;", request.number);
		// (what is dialled, as it goes: the Newton's dialling rules for its
		// home city shape the number - a US one puts 1 in front)
		fprintf(stderr, "[cell] dialling: %s\n", command);
		SetCall(kHostCellCallDialing, request.number);
		if (!Exec(command, nullptr, 0, 10000))
			SetCall(kHostCellCallIdle, nullptr);
		break;
	case kRequestAnswer:
		SetCall(Exec("ATA", nullptr, 0, 10000) ? kHostCellCallActive : kHostCellCallIdle, nullptr);
		break;
	case kRequestHangUp:
		Exec("AT+CHUP", nullptr, 0, 5000);
		SetCall(kHostCellCallIdle, nullptr);
		break;
	case kRequestSendText:
		SetSendState(request.id, SendText(request.number, request.text) ? kHostCellSendSent : kHostCellSendFailed);
		break;
	case kRequestDigits:
		// (one AT+VTS a digit: every A76xx takes that form)
		for (const char* p = request.number; *p != 0; p++)
			if (isdigit((unsigned char) *p) || *p == '*' || *p == '#' || (*p >= 'A' && *p <= 'D'))
			{
				snprintf(command, sizeof(command), "AT+VTS=%c", *p);
				Exec(command, nullptr, 0, 3000);
			}
		break;
	case kRequestSpeaker:
		// (AT+CSDVC: 1 the handset, 3 the loudspeaker)
		Exec(request.id != 0 ? "AT+CSDVC=3" : "AT+CSDVC=1", nullptr, 0, 2000);
		break;
	}
}


void
HostCellularRun(const HostCellularLink* link)
{
	{
		std::lock_guard<std::mutex> hold(gLock);
		gLink = link;
		gStarted = true;
		memset(&gStatus, 0, sizeof(gStatus));
		gStatus.state = kHostCellNone;
	}
	static Request request HOST_LARGE_STATIC;
	bool configured = false;
	long long lastStatus = 0, lastCallPoll = 0, lastClock = 0;
	for (;;)
	{
		if (!configured)
		{
			// (a module still starting - or not there - answers nothing:
			// asked again every 2 s)
			if (!Exec("AT", nullptr, 0, 1000))
			{
				HandleUnsolicited(0);
				SleepMs(1000);
				continue;
			}
			fprintf(stderr, "[cell] the module answers; configuring it\n");
			SetState(kHostCellStarting);
			Configure();
			RefreshStatus();
			FetchStoredTexts();
			lastStatus = Millis();
			lastClock = 0;
			configured = true;
		}

		// what the module said by itself, and messages it pointed at
		HandleUnsolicited(50);
		if (gRingAt != 0 && Millis() - gRingAt > kRingWaitMs)
		{
			gRingAt = 0;
			RingHeard(gRingNumber);
		}
		while (gPendingCount > 0)
		{
			long slot = gPending[0];
			memmove(gPending, gPending + 1, --gPendingCount * sizeof(gPending[0]));
			FetchText(slot);
		}

		// one request
		bool have = false;
		{
			std::lock_guard<std::mutex> hold(gLock);
			if (gRequestCount > 0)
			{
				request = gRequests[gRequestHead];
				gRequestHead = (gRequestHead + 1) % kRequestQueue;
				gRequestCount--;
				have = true;
			}
		}
		if (have)
			Perform(request);

		// a command sent as it is
		{
			std::unique_lock<std::mutex> hold(gCommandLock);
			if (gCommandWanted)
			{
				gCommandWanted = false;
				hold.unlock();
				static char reply[kHostCellReplySize] HOST_LARGE_STATIC;
				bool ok = Exec(gCommand, nullptr, 0, gCommandTimeout, reply, sizeof(reply));
				hold.lock();
				memcpy(gCommandReply, reply, sizeof(reply));
				gCommandOK = ok;
				gCommandFinished = true;
				gCommandDone.notify_all();
			}
		}

		// the network's time, once registered (there is none before)
		HostCellState state;
		{
			std::lock_guard<std::mutex> hold(gLock);
			state = gStatus.state;
		}
		if (gClockWanted && (state == kHostCellHome || state == kHostCellRoaming)
		 && (lastClock == 0 || Millis() - lastClock > kClockRetryMs))
		{
			lastClock = Millis();
			if (ReadClock())
				gClockWanted = false;
		}

		// a call's state confirmed
		if (CallState() != kHostCellCallIdle && Millis() - lastCallPoll > kCallPollMs)
		{
			lastCallPoll = Millis();
			PollCall();
		}

		// the network; a module no longer answering is waited for and
		// configured again (power cycled, it has forgotten everything)
		if (Millis() - lastStatus > kStatusPeriodMs)
		{
			lastStatus = Millis();
			if (!RefreshStatus())
			{
				fprintf(stderr, "[cell] the module stopped answering\n");
				SetCall(kHostCellCallIdle, nullptr);
				SetState(kHostCellNone);
				gPendingCount = 0;
				configured = false;
			}
		}
	}
}


/*------------------------------------------------------------------------------
	The Newton's side
------------------------------------------------------------------------------*/

bool
HostCellularStarted(void)
{
	std::lock_guard<std::mutex> hold(gLock);
	return gStarted;
}


void
HostCellularWillRun(void)
{
	std::lock_guard<std::mutex> hold(gLock);
	gStarted = true;
	gStatus.state = kHostCellNone;
}


void
HostCellularSetClockHandler(void (*handler)(long long, long, bool))
{
	gClockHandler = handler;
}


void
HostCellularSetPowerSwitch(bool (*powerSwitch)(bool on), bool on)
{
	gPowerSwitch = powerSwitch;
	gPoweredOff = !on;
}


bool
HostCellularPowerSwitchable(void)
{
	return gPowerSwitch != nullptr;
}


bool
HostCellularPowered(void)
{
	return !gPoweredOff;
}


bool
HostCellularSetPower(bool on)
{
	if (gPowerSwitch == nullptr)
		return false;
	if (on == !gPoweredOff)
		return true;
	if (!on)
	{
		// the module shut down in order (it leaves the network) before
		// its supply is taken away; the engine then finds it gone, forgets
		// its state and waits for it - as for a module power cycled
		// (only when it answers: the engine takes a command only then)
		HostCellState state;
		{
			std::lock_guard<std::mutex> hold(gLock);
			state = gStatus.state;
		}
		char reply[64];
		if (state != kHostCellNone && state != kHostCellStarting)
			HostCellularCommand("AT+CPOF", reply, sizeof(reply), 5000);
		gPoweredOff = true;
		return gPowerSwitch(false);
	}
	bool taken = gPowerSwitch(true);
	gPoweredOff = !taken;
	return taken;
}


void
HostCellularStatus(HostCellStatusInfo* info)
{
	std::lock_guard<std::mutex> hold(gLock);
	*info = gStatus;
	info->callSeconds = gCallConnected != 0 ? (long) ((Millis() - gCallConnected) / 1000) : 0;
}


static bool
Ask(RequestKind kind, const char* number, const char* text, long id = 0)
{
	std::lock_guard<std::mutex> hold(gLock);
	if (!gStarted || gRequestCount == kRequestQueue)
		return false;
	Request* request = &gRequests[(gRequestHead + gRequestCount++) % kRequestQueue];
	request->kind = kind;
	request->id = id;
	Copy(request->number, number != nullptr ? number : "", kHostCellNumberSize);
	Copy(request->text, text != nullptr ? text : "", kHostCellTextSize);
	return true;
}


bool
HostCellularDial(const char* number)
{
	return number != nullptr && number[0] != 0 && Ask(kRequestDial, number, nullptr);
}


bool
HostCellularAnswer(void)
{
	return Ask(kRequestAnswer, nullptr, nullptr);
}


bool
HostCellularHangUp(void)
{
	return Ask(kRequestHangUp, nullptr, nullptr);
}


bool
HostCellularSendDigits(const char* digits)
{
	return digits != nullptr && digits[0] != 0 && Ask(kRequestDigits, digits, nullptr);
}


bool
HostCellularSetSpeaker(bool on)
{
	return Ask(kRequestSpeaker, nullptr, nullptr, on ? 1 : 0);
}


long
HostCellularSendText(const char* number, const char* text)
{
	if (number == nullptr || number[0] == 0 || text == nullptr || text[0] == 0)
		return 0;
	long id;
	{
		std::lock_guard<std::mutex> hold(gLock);
		id = gNextSendId++;
	}
	SetSendState(id, kHostCellSendBusy);
	if (!Ask(kRequestSendText, number, text, id))
	{
		SetSendState(id, kHostCellSendNone);
		return 0;
	}
	return id;
}


HostCellSendState
HostCellularSendState(long id)
{
	std::lock_guard<std::mutex> hold(gLock);
	long slot = id % kSendStates;
	return id > 0 && gSends[slot].id == id ? gSends[slot].state : kHostCellSendNone;
}


bool
HostCellularNextText(HostCellText* text, HostCellReader reader)
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gTextCount[reader] == 0)
		return false;
	*text = gTexts[reader][gTextHead[reader]];
	gTextHead[reader] = (gTextHead[reader] + 1) % kTextQueue;
	gTextCount[reader]--;
	return true;
}


bool
HostCellularNextEvent(HostCellEvent* event, HostCellReader reader)
{
	std::lock_guard<std::mutex> hold(gLock);
	if (gEventCount[reader] == 0)
		return false;
	*event = gEvents[reader][gEventHead[reader]];
	gEventHead[reader] = (gEventHead[reader] + 1) % kEventQueue;
	gEventCount[reader]--;
	return true;
}


bool
HostCellularCommand(const char* command, char* reply, long size, long timeoutMs)
{
	static std::mutex sOneAtATime;
	std::lock_guard<std::mutex> one(sOneAtATime);
	if (size > 0)
		reply[0] = 0;
	if (!HostCellularStarted())
		return false;
	std::unique_lock<std::mutex> hold(gCommandLock);
	Copy(gCommand, command, sizeof(gCommand));
	gCommandTimeout = timeoutMs;
	gCommandFinished = false;
	gCommandWanted = true;
	// (the engine may be in the middle of something else: a little longer)
	if (!gCommandDone.wait_for(hold, std::chrono::milliseconds(timeoutMs + 5000), [] { return gCommandFinished; }))
	{
		gCommandWanted = false;
		return false;
	}
	if (size > 0)
		Copy(reply, gCommandReply, size);
	return gCommandOK;
}
