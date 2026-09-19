// Newt world test: the OS booted (OsBoot), the loader's 'main' task runs
// NewtUserMain - a TNewtWorld over the host display (the ROM image read
// in for the fonts) - and the kernel services task, standing in for the
// keyboard tool, the tablet and the screen driver's events, drives it
// through the 'newt port: a script event defines a view with click and
// gesture scripts, a tap on the tablet is idled through (the world's idle
// timer runs the application), a keyboard event types into a paragraph,
// a redraw event, and script events read the results back.  The world
// is then stopped by a 'host/'quit event handled by a handler the test
// installs.
#include "NewtWorld.h"
#include "SystemNatives.h"
#include "Notebook.h"
#include "HostViews.h"
#include "HostScreen.h"
#include "HostTablet.h"
#include "RootView.h"
#include "Keyboard.h"
#include "Commands.h"
#include "Recognizer.h"
#include "StrokeCentral.h"
#include "Frames.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "UserBoot.h"
#include "UserTasks.h"
#include "UserPorts.h"
#include "NameServer.h"
#include "KernelGlobals.h"
#include "os600/kernel/host/TaskRuntime.h"
#include "os600/kernel/Boot.h"
#include "OSErrors.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// the test's own event: 'host/'quit ends the world's loop
class TQuitHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
	{
		SetReply(*size, event);
		if (token != nil && token->GetReplyId() != 0)
			ReplyImmed();
		((TAppWorld*) GetGlobals())->AETerminateLoop();
	}
};
static TQuitHandler* gQuitHandler = nil;

// 'host/'eval: a NewtonScript source (the event carries its address) run
// in the world, the result an integer or 0
struct TEvalEvent : public TAEvent
{
	const char*	fSource;
	long		fResult;
};
class TEvalHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
	{
		TEvalEvent* eval = (TEvalEvent*) event;
		RefVar fn(ParseString(RefVar(MakeString(eval->fSource))));
		RefVar result(InterpretBlock(fn, RefVar(gVarFrame)));
		eval->fResult = ISINT(result) ? RINT(result) : 0;
		SetReply(*size, event);
		if (token != nil && token->GetReplyId() != 0)
			ReplyImmed();
	}
};
static TEvalHandler* gEvalHandler = nil;
static const char* kSetupSource =
	"begin "
	"GetRoot().testApp := {"
	"  clicks: 0, taps: 0, "
	"  setup: func(data) begin "
	"    self.view := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200 + 0x800, viewBounds: {left: 60, top: 40, right: 120, bottom: 80}, viewFormat: 1, "
	"      viewClickScript: func(unit) begin testApp.clicks := testApp.clicks + 1; nil end, "
	"      viewGestureScript: func(unit, kind) begin testApp.taps := testApp.taps + 1; true end}); "
	"    self.para := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 100, right: 300, bottom: 130}, viewFormat: 0x111, viewFont: 0x3000, text: \"Type here\"}); "
	"    view:Dirty(); para:Dirty(); KeyboardConnect(true); SetKeyView(para, 4); 1 end, "
	"  pause: func(data) begin "
	"    local tickled := GetRoot():EventPause(true); "			// the tickle: nothing has happened since
	"    local since := GetRoot():EventPause(nil); "
	"    if tickled = 0 and IsInteger(since) and since >= 0 then 1 else 0 end, "
	"  worldData: func(data) "
	"    if GetPackageStore(\"WorldData\") then 1 else 0, "		// the store part of the WorldData package, mounted
	"  battery: func(data) begin "
	"    local b := BatteryStatus(0); "
	"    if b.batteryType = 'alkaline and b.batteryCapacity = 100 and b.acPower = 'no "
	"       and b.chargeState = 'discharging and b.batteryVoltage > 5.0 then 1 else 0 end, "
	"  countClicks: func(data) clicks, "
	"  countTaps: func(data) taps, "
	"  textLength: func(data) StrLen(para.text) "
	"}; 7 "
	"end";

// what the world sees
static long gWorldTaskId = 0;
static long gClicksSeen = 0;
static long gTapsSeen = 0;
static long gRedraws = 0;
static long gScriptErr = -1;
static long gTextLength = 0;
static Boolean gPauseOk = false;
static Boolean gBatteryOk = false;
static Boolean gMinimumBatteryOk = false;
static Boolean gWorldDataOk = false;
static long gMainDone = 0;
static Boolean gAliveAfterBoot = false;


// the host boot: the display and the toolbox, the test's quit handler; the
// test's frame goes into the root view (a root variable, as the
// applications are) once the notebook has made it
static void
TestBoot(void)
{
	HostBootNewtWorld();
	gWorldTaskId = gCurrentTaskId;
	gAliveAfterBoot = gNewtIsAliveAndWell;
	// the machine has enough power to go on (the ROM's own scripts reach
	// FMinimumBatteryCheck through a function object with no name)
	gMinimumBatteryOk = ISNIL(FMinimumBatteryCheck(RefVar(NILREF)));

	gQuitHandler = new TQuitHandler;
	gQuitHandler->Init('quit', 'host');
	gEvalHandler = new TEvalHandler;
	gEvalHandler->Init('eval', 'host');
}


// the kernel services task: the world found by name, then driven
static void
Scenario(void)
{
	TUNameServer nameServer;
	TObjectId portId = 0;
	ULong spec = 0;
	for (long tries = 0; tries < 200 && nameServer.Lookup("newt", "TUPort", &portId, &spec) != noErr; tries++)
		Sleep(10 * kMilliseconds);
	EXPECT(portId != 0);
	if (portId == 0)
	{
		HostStopTasks();
		return;
	}
	TUPort newtPort(portId);
	Sleep(50 * kMilliseconds);					// the boot's first Run

	// the test's frame put in the root (a 'host/'eval event of the test's own),
	// then its views set up by a script event
	{
		TEvalEvent eval;
		eval.fAEventClass = 'host';
		eval.fAEventID = 'eval';
		eval.fSource = kSetupSource;
		eval.fResult = 0;
		ULong replySize = 0;
		long err = newtPort.SendRPC(&replySize, &eval, sizeof(eval), &eval, sizeof(eval));
		EXPECT(err == noErr && eval.fResult == 7);
		TRunScriptEvent setup("testApp", "setup");
		err = newtPort.SendRPC(&replySize, &setup, sizeof(setup), &setup, sizeof(setup));
		EXPECT(err == noErr && setup.fError == 0 && setup.fResult == 1);
		gScriptErr = setup.fError;
	}
	// a tap on the view: the click at the pen-down, the tap at the pen-up
	// (the world's idle timer runs IdleStrokes in the ticks between)
	HostTabletPenDown(90, 60, 0);
	Sleep(20 * kMilliseconds);
	HostTabletPenUp(0);
	Sleep(200 * kMilliseconds);
	{
		TRunScriptEvent count("testApp", "countClicks");
		ULong replySize = 0;
		newtPort.SendRPC(&replySize, &count, sizeof(count), &count, sizeof(count));
		gClicksSeen = count.fResult;
		TRunScriptEvent taps("testApp", "countTaps");
		newtPort.SendRPC(&replySize, &taps, sizeof(taps), &taps, sizeof(taps));
		gTapsSeen = taps.fResult;
		// EventPause: how long the machine has been left alone, and the
		// tickle that says it has just been used
		TRunScriptEvent pause("testApp", "pause");
		newtPort.SendRPC(&replySize, &pause, sizeof(pause), &pause, sizeof(pause));
		gPauseOk = pause.fError == 0 && pause.fResult == 1;
		// BatteryStatus: the host's power plant, as a script sees it
		TRunScriptEvent battery("testApp", "battery");
		newtPort.SendRPC(&replySize, &battery, sizeof(battery), &battery, sizeof(battery));
		gBatteryOk = battery.fError == 0 && battery.fResult == 1;
		TRunScriptEvent world("testApp", "worldData");
		newtPort.SendRPC(&replySize, &world, sizeof(world), &world, sizeof(world));
		gWorldDataOk = world.fError == 0 && world.fResult == 1;
	}
	// a key typed into the paragraph: the keyboard tool's 'keyb event, the
	// repeat rates replied
	{
		KeyboardEvent down(aeKeyDown, 2);		// the d key of the German layout
		KeyboardEvent reply(aeKeyDown, 0);
		ULong replySize = 0;
		long err = newtPort.SendRPC(&replySize, &down, sizeof(down), &reply, sizeof(reply));
		EXPECT(err == noErr && replySize == sizeof(reply) && reply.fEventId == 0x24 && reply.fFromKeyboard == 200 && reply.fKeyCode == 600 && reply.fLength == 16);
		KeyboardEvent up(aeKeyUp, 2);
		newtPort.SendRPC(&replySize, &up, sizeof(up), &reply, sizeof(reply));
		Sleep(50 * kMilliseconds);
		TRunScriptEvent length("testApp", "textLength");
		newtPort.SendRPC(&replySize, &length, sizeof(length), &length, sizeof(length));
		gTextLength = length.fResult;
	}
	// a redraw event
	{
		Rect r = { 0, 0, 100, 100 };
		TRedrawScreenEvent redraw(r);
		ULong replySize = 0;
		long err = newtPort.SendRPC(&replySize, &redraw, sizeof(redraw), &redraw, sizeof(redraw));
		EXPECT(err == noErr);
		gRedraws = 1;
	}
	// the world stopped
	{
		TAEvent quit;
		quit.fAEventClass = 'host';
		quit.fAEventID = 'quit';
		ULong replySize = 0;
		newtPort.SendRPC(&replySize, &quit, sizeof(quit), &quit, sizeof(quit));
		Sleep(50 * kMilliseconds);
	}
	HostStopTasks();
}


int main()
{
	HostConfigureNewtWorld(NEWTON_ROM_BIN, 0x400000, 320, 480, 4);
	gNewtHostBoot = TestBoot;
	NewtInstallUserMain();
	gHostKernelServicesTask = Scenario;
	OsBoot();
	EXPECT(gWorldTaskId != 0 && !gAliveAfterBoot);		// the world booted in the 'main' task, before PreMain
	EXPECT(gNewtIsAliveAndWell && gApplication != nil && gApplication->ClassID() == clARMNotebook && gRootView != nil);
	EXPECT(gNewtPort != nil && gRecognition.fLevel == 1 && gRecognition.fRecognizers != nil);
	EXPECT(gScriptErr == 0);
	EXPECT(gClicksSeen == 1 && gTapsSeen == 1);
	EXPECT(gPauseOk);
	EXPECT(gBatteryOk);
	EXPECT(gMinimumBatteryOk);
	EXPECT(gWorldDataOk);
	EXPECT(gTextLength == 10);					// "Typed here"
	EXPECT(gRedraws == 1);
	if (failures == 0)
		printf("test_Newt: all passed\n");
	else
		printf("test_Newt: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
