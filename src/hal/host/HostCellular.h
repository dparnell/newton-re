/*
	File:		hal/host/HostCellular.h

	Contains:	The host's cellular modem: a SIMCom A76xx-class module
				(the LilyGO T-Deck Pro's 4G variant carries an A7682E;
				lilyphone1, the owner's earlier firmware for the same board,
				calls it a SIM7672 to TinyGSM) driven in AT commands - the
				network it is registered on, voice calls made and answered,
				text messages sent and received, and the network's time.

				The engine (HostCellular.cpp) is portable and knows nothing
				of the Newton: it owns the module's byte link, given to it
				as a HostCellularLink - the board's UART
				(src/host/esp32/main/tdeck_modem.cpp), or on a desktop a TCP
				connection to tools/modem/fakecell.py, a pretend module
				(newton --cell-port) - and is run by the host on a thread
				of the host's own (HostCellularRun never returns).  Only
				that thread reads or writes the link: an unsolicited result
				code (RING, +CLIP, +CMTI, ...) that arrives in the middle of
				another command's answer is taken for what it is and handled
				there, never handed back as the answer.  Everything else
				reaches the engine through a request queue and comes back
				through a snapshot of its state and two queues (call events,
				received messages), each under the engine's lock.

				Text crosses the interface as UTF-8; telephone numbers as
				the module gives and takes them ("+61412345678").

				Ported from lilyphone1's src/peripherals/peri_modem.cpp
				(the owner's own code): its AT dialect, its URC handling,
				its UCS2 message bodies and its CLCC polling are kept;
				lilyphone1's UDP relay and notification tones are not
				(docs/host-tdeck-pro-phone.md).

				The NewtonScript natives (HostCellularNatives.cpp,
				registered with the view functions, host/HostViews.cpp):

					HostCellAvailable()		true when there is a module (or a
											link to one)
					HostCellStatus()		{state, operator, rssi, callState,
											number, seconds}: state 'none (no module
											answering), 'starting, 'noSIM,
											'locked (wants a PIN), 'searching,
											'home or 'roaming; rssi in dBm or
											nil; callState 'idle, 'incoming,
											'dialing or 'active, number the
											other party's (nil: withheld or no
											call), seconds the call's so far
					HostCellDial(number)	==> true when asked
					HostCellAnswer()		==> true when asked
					HostCellHangUp()		==> true when asked
					HostCellSendText(number, text)
											==> an id for HostCellSendState,
											or nil
					HostCellSendState(id)	'busy, 'sent, 'failed (nil: no
											such sending)
					HostCellNextText()		the oldest message received and not
											yet taken - {number, text, time}
											(time in minutes since 1904, GMT;
											age how many minutes ago, by the
											host's clock - Time() - age is when
											in the Newton's own zone; both nil
											when the network gave none) - or nil
					HostCellTransportText()
											the same, the text transport's own
											(comms/host/HostText.ns): each
											message is given to both
					HostCellNextEvent()		the oldest call event not yet
											taken - {event, number}: event
											'ring (number its caller's when the
											network gave it), 'connected,
											'ended, 'busy, 'noAnswer - or nil
					HostCellCommand(command)
											a command sent as it is (for looking
											at what a module can do: "AT+CLAC",
											"AT+FCLASS=?") ==> {ok, reply}: ok
											true at OK, reply the lines before
											the result code, one a line (10 s
											at most)
*/

#ifndef __HOSTCELLULAR_H
#define __HOSTCELLULAR_H

const long	kHostCellNumberSize = 32;		// a telephone number, its terminator included
const long	kHostCellOperatorSize = 24;
const long	kHostCellTextSize = 640;		// a message's text in UTF-8 (a UCS2 SMS is 70
											// characters, four bytes each at most; a GSM one 160)
const long	kHostCellReplySize = 1024;		// HostCellularCommand's reply

// The module's byte link (each call from the engine's thread only)
struct HostCellularLink
{
	void*	ref;
	// ==> how many bytes were read into bytes (at most size), waiting up to
	// timeoutMs for the first: 0 none came, -1 the link is gone for good
	long	(*read)(void* ref, unsigned char* bytes, long size, long timeoutMs);
	void	(*write)(void* ref, const void* bytes, long size);
	// a command sent after the engine's own configuration, each time the
	// module is configured (nil: none) - the T-Deck Pro's AT+CSCLK=1, the
	// module asleep while its DTR is high (src/host/esp32/main/tdeck_modem.cpp)
	const char*	setup;
};

enum HostCellState
{
	kHostCellNone,							// no module answering (yet)
	kHostCellStarting,						// answering, being configured
	kHostCellNoSIM,
	kHostCellLocked,						// the SIM wants a PIN (or a PUK)
	kHostCellSearching,						// not registered on a network
	kHostCellHome,
	kHostCellRoaming
};

enum HostCellCallState
{
	kHostCellCallIdle,
	kHostCellCallIncoming,					// ringing, not answered yet
	kHostCellCallDialing,					// dialled out, not connected yet
	kHostCellCallActive						// connected
};

struct HostCellStatusInfo
{
	HostCellState		state;
	char				operatorName[kHostCellOperatorSize];
	long				rssi;				// dBm; 0 unknown
	HostCellCallState	call;
	char				number[kHostCellNumberSize];	// the other party's; "" withheld or no call
	long				callSeconds;		// since it connected; 0 not connected
};

enum HostCellEventKind
{
	kHostCellEventRing,						// (each RING: the module rings once every few seconds)
	kHostCellEventConnected,
	kHostCellEventEnded,					// hung up, by either end (NO CARRIER, VOICE CALL: END)
	kHostCellEventBusy,
	kHostCellEventNoAnswer
};

struct HostCellEvent
{
	HostCellEventKind	kind;
	char				number[kHostCellNumberSize];	// kHostCellEventRing: the caller's, when known
};

struct HostCellText
{
	char		number[kHostCellNumberSize];
	char		text[kHostCellTextSize];	// UTF-8
	long long	time;						// seconds since 1970 (UTC); 0 the network gave none
};

enum HostCellSendState
{
	kHostCellSendNone,						// no such sending (or long forgotten)
	kHostCellSendBusy,
	kHostCellSendSent,						// accepted by the network
	kHostCellSendFailed
};

// The engine: run on a thread of the host's own, never returning.
// HostCellularStarted says whether one has been started (the natives' "is
// there a module").  The handler, when there is one, is given the network's
// time each time the module reads one (seconds since 1970, UTC; the zone's
// offset in minutes east of Greenwich, when the network gave it - otherwise
// hasOffset is false).
void	HostCellularRun(const HostCellularLink* link);
bool	HostCellularStarted(void);
// The host says the engine is coming before its thread gets to
// HostCellularRun (a board first powers the module and waits for it), so
// that the Newton's side, starting meanwhile, counts it as there
void	HostCellularWillRun(void);
void	HostCellularSetClockHandler(void (*handler)(long long utcSeconds, long offsetMinutes, bool hasOffset));
// A module whose supply the host can switch (a board's, to save its
// battery): the host gives its switch (==> whether it took) and whether the
// module is on now.  HostCellularSetPower (the Host preferences panel) off
// shuts the module down in order (AT+CPOF) and then switches it off - the
// engine sends it nothing and waits, its status "no module"; on switches
// it on and the engine configures it as after a power cycle
void	HostCellularSetPowerSwitch(bool (*powerSwitch)(bool on), bool on);
bool	HostCellularPowerSwitchable(void);
bool	HostCellularPowered(void);
bool	HostCellularSetPower(bool on);

// What the Newton's side asks of it (any thread; none of them waits on the
// module but HostCellularCommand)
void	HostCellularStatus(HostCellStatusInfo* info);
bool	HostCellularDial(const char* number);
bool	HostCellularAnswer(void);
bool	HostCellularHangUp(void);
bool	HostCellularSendDigits(const char* digits);	// keypad tones in a call (0-9, *, #, A-D)
bool	HostCellularSetSpeaker(bool on);			// the call's audio to the loudspeaker, or the handset
long	HostCellularSendText(const char* number, const char* text);	// ==> an id (0: not queued)
HostCellSendState	HostCellularSendState(long id);
// The call events and the received messages are given to each reader, every
// one: a script's (the natives HostCellNextEvent, HostCellNextText) and the
// system's - the telephone provider's comm tool (comms/host/HostCellTool.h),
// which listens all the time, and the text transport (comms/host/HostText.ns,
// through HostCellTransportText)
enum HostCellReader { kHostCellReaderScript, kHostCellReaderTool, kHostCellReaders };
bool	HostCellularNextText(HostCellText* text, HostCellReader reader);
bool	HostCellularNextEvent(HostCellEvent* event, HostCellReader reader);
// a command sent as it is, the answer waited for (the module's up to
// timeoutMs; the asker gives up 5 s after that) ==> whether it ended
// OK; reply the lines before the result code, each ended by '\n'
bool	HostCellularCommand(const char* command, char* reply, long size, long timeoutMs);

// (the engine's parsing, for its tests - hal/tests/test_HostCellular.cpp)
bool	HostCellularUCS2ToUTF8(const char* hex, char* out, long size);
bool	HostCellularUTF8ToUCS2(const char* utf8, char* out, long size);
bool	HostCellularParseStamp(const char* stamp, long long* utcSeconds, long* offsetMinutes, bool* hasOffset);
void	HostCellularParseCMGR(const char* line, char* number, long numberSize, char* stamp, long stampSize, long* dcs);

void	HostRegisterCellularFunctions(void);		// the natives (host/HostViews.cpp)

// A desktop's module (HostCellularSocket.cpp): a TCP port on 127.0.0.1
// listened on (0: a free one, *listening says which), the engine run over
// whatever connects - tools/modem/fakecell.py (newton --cell-port)
bool	HostCellularListen(unsigned short port, unsigned short* listening);

#endif	/* __HOSTCELLULAR_H */
