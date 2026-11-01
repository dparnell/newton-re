/*
	File:		host/HostCellularNatives.cpp

	Contains:	The NewtonScript natives over the cellular modem engine
				(HostCellular.h, which lists them).  The engine speaks
				UTF-8; a NewtonScript string is UTF-16, turned here.
				A desktop has a module when newton is given one with
				--cell-port (tools/modem/fakecell.py at the other end).
*/

#include "HostCellular.h"
#include "hal/Power.h"
#include "hal/host/HostPower.h"
#include "HostUTF8.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Unicode.h"
#include <string.h>
#include <time.h>

const long	kSecondsFrom1904To1970 = 2082844800L;


static void
SetSlot(RefArg frame, const char* name, RefArg value)
{
	SetFrameSlot(frame, RefVar(Intern((char*) name)), value);
}


static Ref
FHostCellAvailable(RefArg /*rcvr*/)
{
	return HostCellularStarted() ? TRUEREF : NILREF;
}


static Ref
FHostCellStatus(RefArg /*rcvr*/)
{
	if (!HostCellularStarted())
		return NILREF;
	HostCellStatusInfo info;
	HostCellularStatus(&info);
	static const char* const kStates[] = { "none", "starting", "noSIM", "locked", "searching", "home", "roaming" };
	static const char* const kCalls[] = { "idle", "incoming", "dialing", "active" };
	RefVar status(AllocateFrame());
	SetSlot(status, "state", RefVar(Intern((char*) kStates[info.state])));
	SetSlot(status, "operator", info.operatorName[0] != 0 ? RefVar(HostStringOfUTF8(info.operatorName)) : RefVar(NILREF));
	SetSlot(status, "rssi", info.rssi != 0 ? RefVar(MAKEINT(info.rssi)) : RefVar(NILREF));
	SetSlot(status, "callState", RefVar(Intern((char*) kCalls[info.call])));
	SetSlot(status, "number", info.number[0] != 0 ? RefVar(HostStringOfUTF8(info.number)) : RefVar(NILREF));
	SetSlot(status, "seconds", RefVar(MAKEINT(info.callSeconds)));
	return status;
}


static Ref
FHostCellDial(RefArg /*rcvr*/, RefArg number)
{
	char digits[kHostCellNumberSize];
	HostUTF8Of(number, digits, sizeof(digits));
	return HostCellularDial(digits) ? TRUEREF : NILREF;
}


static Ref
FHostCellAnswer(RefArg /*rcvr*/)
{
	return HostCellularAnswer() ? TRUEREF : NILREF;
}


static Ref
FHostCellHangUp(RefArg /*rcvr*/)
{
	return HostCellularHangUp() ? TRUEREF : NILREF;
}


static Ref
FHostCellSendText(RefArg /*rcvr*/, RefArg number, RefArg text)
{
	char digits[kHostCellNumberSize];
	static char body[kHostCellTextSize];
	HostUTF8Of(number, digits, sizeof(digits));
	HostUTF8Of(text, body, sizeof(body));
	long id = HostCellularSendText(digits, body);
	return id != 0 ? MAKEINT(id) : NILREF;
}


static Ref
FHostCellSendState(RefArg /*rcvr*/, RefArg id)
{
	if (!ISINT(id))
		return NILREF;
	static const char* const kStates[] = { nil, "busy", "sent", "failed" };
	HostCellSendState state = HostCellularSendState(RINT(id));
	return state == kHostCellSendNone ? NILREF : Intern((char*) kStates[state]);
}


static Ref
NextText(HostCellReader reader)
{
	static HostCellText text;
	if (!HostCellularNextText(&text, reader))
		return NILREF;
	RefVar message(AllocateFrame());
	SetSlot(message, "number", RefVar(HostStringOfUTF8(text.number)));
	SetSlot(message, "text", RefVar(HostStringOfUTF8(text.text)));
	// (minutes since 1904, as Time() gives them)
	SetSlot(message, "time", text.time != 0 ? RefVar(MAKEINT((long) ((text.time + kSecondsFrom1904To1970) / 60))) : RefVar(NILREF));
	// (how long ago it was sent, in minutes, by the host's clock - which is
	// GMT: Time() less this is when, in the Newton's own zone; time is GMT)
	long long now = (long long) ::time(nil);
	SetSlot(message, "age", text.time != 0 && now >= text.time ? RefVar(MAKEINT((long) ((now - text.time) / 60))) : RefVar(NILREF));
	return message;
}


static Ref
FHostCellNextText(RefArg /*rcvr*/)
{
	return NextText(kHostCellReaderScript);
}


// the text transport's own (comms/host/HostText.ns): every message, as the
// script's natives get every one too
static Ref
FHostCellTransportText(RefArg /*rcvr*/)
{
	return NextText(kHostCellReaderTool);
}


static Ref
FHostCellNextEvent(RefArg /*rcvr*/)
{
	HostCellEvent event;
	if (!HostCellularNextEvent(&event, kHostCellReaderScript))
		return NILREF;
	static const char* const kEvents[] = { "ring", "connected", "ended", "busy", "noAnswer" };
	RefVar frame(AllocateFrame());
	SetSlot(frame, "event", RefVar(Intern((char*) kEvents[event.kind])));
	SetSlot(frame, "number", event.number[0] != 0 ? RefVar(HostStringOfUTF8(event.number)) : RefVar(NILREF));
	return frame;
}


static Ref
FHostCellCommand(RefArg /*rcvr*/, RefArg command)
{
	char line[256];
	static char reply[kHostCellReplySize];
	HostUTF8Of(command, line, sizeof(line));
	if (line[0] == 0 || !HostCellularStarted())
		return NILREF;
	bool ok = HostCellularCommand(line, reply, sizeof(reply), 10000);
	RefVar answer(AllocateFrame());
	SetSlot(answer, "ok", ok ? RefVar(TRUEREF) : RefVar(NILREF));
	SetSlot(answer, "reply", RefVar(HostStringOfUTF8(reply)));
	return answer;
}


// a message or a call wakes a sleeping machine, as a MessagePad's serial port's
// ring line did ('serialgpi: the root puts it back to sleep after 45 s
// unless the user does something meanwhile)
static void
WakeForNews(void)
{
	HostPowerWake(kPowerEventSerialGPI);
}


void
HostRegisterCellularFunctions(void)
{
	gHostCellularNews = WakeForNews;
	RefVar functions(gFunctionFrame);
	SetSlot(functions, "HostCellAvailable", RefVar(MakeCFunction((void*) FHostCellAvailable, 0, nil)));
	SetSlot(functions, "HostCellStatus", RefVar(MakeCFunction((void*) FHostCellStatus, 0, nil)));
	SetSlot(functions, "HostCellDial", RefVar(MakeCFunction((void*) FHostCellDial, 1, nil)));
	SetSlot(functions, "HostCellAnswer", RefVar(MakeCFunction((void*) FHostCellAnswer, 0, nil)));
	SetSlot(functions, "HostCellHangUp", RefVar(MakeCFunction((void*) FHostCellHangUp, 0, nil)));
	SetSlot(functions, "HostCellSendText", RefVar(MakeCFunction((void*) FHostCellSendText, 2, nil)));
	SetSlot(functions, "HostCellSendState", RefVar(MakeCFunction((void*) FHostCellSendState, 1, nil)));
	SetSlot(functions, "HostCellNextText", RefVar(MakeCFunction((void*) FHostCellNextText, 0, nil)));
	SetSlot(functions, "HostCellTransportText", RefVar(MakeCFunction((void*) FHostCellTransportText, 0, nil)));
	SetSlot(functions, "HostCellNextEvent", RefVar(MakeCFunction((void*) FHostCellNextEvent, 0, nil)));
	SetSlot(functions, "HostCellCommand", RefVar(MakeCFunction((void*) FHostCellCommand, 1, nil)));
}
