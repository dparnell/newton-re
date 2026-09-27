/*
	File:		testing/TestAgent.cpp

	Contains:	The test agent, its reporters and its message queue.  See
				TestAgent.h.
*/

#include "TestAgent.h"
#include "Journal.h"
#include "NewtWorld.h"
#include "NameServer.h"
#include "UserTasks.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "Dates.h"
#include "Unicode.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "Interpreter.h"
#include "PackageManager.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

TAgentReporter*		gTestReporterForNewt = nil;
TMessageQueue*		gTestAgentMessageQueue = nil;
RefStruct*			gTestMgrAppContext = nil;
TtspsPart*			gtspsPartHandler = nil;
void*				gTestCasePartAddress = nil;
long				gStdioOffCount = 1;		// (_sys_initio sets it to one: stdio starts off)

static const UniChar kLocalTestManager[] = { '*', 0 };	// ROM 0x0037a704


/*------------------------------------------------------------------------------
	S t a n d a r d   i / o
------------------------------------------------------------------------------*/

// ROM 0x003193ac StdioOn__Fv
// The C library's i/o goes to the serial debugger's link once the count
// is below one.  DEVIATION: the host's stdio is its console whatever the
// count says; only the count is kept.
void
StdioOn(void)
{
	gStdioOffCount--;
}


// ROM 0x00319018 StdioOff__Fv
void
StdioOff(void)
{
	gStdioOffCount++;
}


/*------------------------------------------------------------------------------
	T M e s s a g e Q u e u e
------------------------------------------------------------------------------*/

// ROM 0x00228b30 __ct__13TMessageQueueFv
TMessageQueue::TMessageQueue()
	: CList()
{ }


// ROM 0x00228b64 EnqueueMessage__13TMessageQueueFUlP10FourULongsPCc
// A message put at the front: a handle of its size (the text's length
// and its terminator rounded up to a word, and the 0x18 bytes before it),
// its kind, the four words (noughts when there are none) and the text.
void
TMessageQueue::EnqueueMessage(ULong kind, FourULongs* data, const char* text)
{
	long size = (long) strlen(text) + 1;
	if (size % 4 != 0)
		size = (size >> 2) * 4 + 4;
	size += 0x18;
	Handle item = NewHandle(size);
	if (item == nil)
		return;
	char* p = *item;
	int32_t word = (int32_t) size;
	BlockMove(&word, p, 4);
	uint32_t kindWord = (uint32_t) kind;
	BlockMove(&kindWord, p + 4, 4);
	uint32_t words[4] = { 0, 0, 0, 0 };
	if (data != nil)
		for (int i = 0; i < 4; i++)
			words[i] = (uint32_t) data->fWord[i];
	BlockMove(words, p + 8, 0x10);
	BlockMove(text, p + 0x18, (long) strlen(text) + 1);
	InsertAt(0, item);
}


// ROM 0x00228c5c DequeueMessage__13TMessageQueueFPlPUlP10FourULongsPc
// The oldest message (the last in the list) taken out and its handle
// given back.  ==> the list's error from shrinking; nothing to take is 0.
NewtonErr
TMessageQueue::DequeueMessage(long* size, ULong* kind, FourULongs* data, char* text)
{
	Handle item = (Handle) At(Count() - 1);
	if (item == nil)
		return noErr;
	char* p = *item;
	int32_t word;
	BlockMove(p, &word, 4);
	*size = word;
	uint32_t kindWord;
	BlockMove(p + 4, &kindWord, 4);
	*kind = kindWord;
	uint32_t words[4];
	BlockMove(p + 8, words, 0x10);
	for (int i = 0; i < 4; i++)
		data->fWord[i] = words[i];
	BlockMove(p + 0x18, text, (long) strlen(p + 0x18) + 1);
	DisposHandle(item);
	if (Count() == 0)
		return noErr;
	return RemoveElementsAt(Count() - 1, 1);
}


// ROM 0x00228d04 IsQueueEmpty__13TMessageQueueFv
Boolean
TMessageQueue::IsQueueEmpty(void)
{
	return Count() == 0;
}


/*------------------------------------------------------------------------------
	T T e s t R e p o r t e r
------------------------------------------------------------------------------*/

// ROM 0x0022b414 __ct__13TTestReporterFUlN21
TTestReporter::TTestReporter(ULong id, TObjectId agentPort, ULong maxErrors)
{
	f190 = 1;
	fAgentPort = agentPort;
	fId = id;
	fParameters[0] = 0;
	fErrors = 0;
	fMaxErrors = maxErrors;
}


// ROM 0x0022b46c __dt__13TTestReporterFv
TTestReporter::~TTestReporter()
{ }


// ROM 0x0022b478 SendToTestAgent__13TTestReporterFUlPcl
// A 'newt/'tste event to the agent: the kind, this reporter's number and
// up to 0xe0 bytes of text (or of whatever block the text is).
void
TTestReporter::SendToTestAgent(ULong kind, char* text, long sub)
{
	TTestAgentEvent event;
	TUPort port(fAgentPort);
	event.fAEventClass = kNewtEventClass;
	event.fAEventID = kTestAgentEventId;
	event.fReporter = fId;
	event.fKind = kind;
	if (text == nil)
		event.fText[0] = 0;
	else
		BlockMove(text, event.fText, sizeof(event.fText));
	event.fSub = sub;
	port.Send(&event, sizeof(event));
}


// ROM 0x0022b544 TestReportError__13TTestReporterFPcT1l
// (A message over 200 characters is cut short where it lies.)
void
TTestReporter::TestReportError(char* message, char* where, long error)
{
	char line[256];
	if (strlen(message) > 200)
		message[200] = 0;
	fErrors++;
	if (where == nil)
	{
		if (error == 0)
			sprintf(line, "Test Case ERR: \t%s\r", message);
		else
			sprintf(line, "Test Case ERR: %d\t%s\r", (int) error, message);
	}
	else
		sprintf(line, "Test Case ERR: %d\t%s\t%s\r", (int) error, where, message);
	SendToTestAgent(kTestAgentTestError, line, 0);
}


// ROM 0x0022b640 TestReportErrorValues__13TTestReporterFPcT1lT3
// ROM bug kept: with no `where` the format asks for two strings and is
// given one, so the values line is whatever the next argument word held
// - here, as there, it is simply left out.
void
TTestReporter::TestReportErrorValues(char* message, char* where, long got, long expected)
{
	char values[128];
	char line[256];
	if (strlen(message) > 200)
		message[200] = 0;
	fErrors++;
	sprintf(values, "               got: %d (0x%x), expect %d (0x%x)", (int) got, (unsigned) got, (int) expected, (unsigned) expected);
	if (where == nil)
		sprintf(line, "Test Case ERR: \t%s\r%s\r", message, "");
	else
		sprintf(line, "Test Case ERR: %s\t%s\r%s\r", where, message, values);
	SendToTestAgent(kTestAgentTestError, line, 0);
}


// ROM 0x0022b760 TestReportMessage__13TTestReporterFPcT1
void
TTestReporter::TestReportMessage(char* message, char* where)
{
	char line[256];
	if (strlen(message) > 200)
		message[200] = 0;
	if (where == nil)
		sprintf(line, "Test Case MSG: %s\r", message);
	else
		sprintf(line, "Test Case MSG: %s\t%s\r", where, message);
	SendToTestAgent(kTestAgentTestMessage, line, 0);
}


// ROM 0x0022b804 TestFPrintf__13TTestReporterFiPCce
// A message (kind 2) or an error (anything else, and counted).
void
TTestReporter::TestFPrintf(int kind, const char* format, ...)
{
	char line[256];
	va_list args;
	va_start(args, format);
	vsprintf(line, format, args);
	va_end(args);
	ULong what;
	if (kind == 2)
		what = kTestAgentTestMessage;
	else
	{
		fErrors++;
		what = kTestAgentTestError;
	}
	SendToTestAgent(what, line, 0);
}


// ROM 0x0022b880 TestReadDataFile__13TTestReporterFPclPlPPc
// A data file read into a new block: an offset of -1 reads it off the
// host with the C library (stdio on for it); otherwise the newt world's
// own reporter with a test manager asks the manager (testMgrReadDataFile,
// [name, offset, size] -> a binary), and any other reporter asks the
// agent by RPC.  ==> 0, -2 for a file that is not there, or the error.
long
TTestReporter::TestReadDataFile(char* name, long offset, long* size, char** data)
{
	long err = noErr;
	if (offset == -1)
	{
		StdioOn();
		FILE* f = fopen(name, "r");
		if (f == nil)
			err = -2;			// (and stdio left on: kept)
		else
		{
			fseek(f, 0, SEEK_END);
			*size = ftell(f);
			fseek(f, 0, SEEK_SET);
			*data = NewPtr(*size + 4);
			fread(*data, 1, *size, f);
			fclose(f);
			StdioOff();
		}
	}
	else if (gTestMgrAppContext == nil || this != gTestReporterForNewt)
	{
		TTestAgentEvent event;
		event.fAEventClass = kNewtEventClass;
		event.fAEventID = kTestAgentEventId;
		event.fKind = kTestAgentDataFile;
		event.fReporter = fId;
		event.fSub = 0;
		event.fData.fWord[1] = *size;
		event.fData.fWord[0] = offset;
		if (name == nil)
			event.fText[0] = 0;
		else
			strcpy(event.fText, name);
		TUPort port(fAgentPort);
		struct { long fErr; long fSize; char* fData; } reply;
		ULong replySize;
		err = port.SendRPC(&replySize, &event, sizeof(event), &reply, sizeof(reply));
		if (err == noErr)
		{
			*size = reply.fSize;
			*data = reply.fData;
			err = reply.fErr;
		}
	}
	else
	{
		RefVar args(AllocateArray(RefVar(RSSYMarray), 3));
		UniChar uname[256];
		ConvertToUnicode(name, uname, kMacRomanEncoding, 0x7fffffff);
		SetArraySlot(args, 0, RefVar(MakeString(uname)));
		SetArraySlot(args, 1, RefVar(MAKEINT(offset)));
		SetArraySlot(args, 2, RefVar(MAKEINT(*size)));
		RefVar result(DoMessage(*gTestMgrAppContext, RefVar(Intern((char*) "testMgrReadDataFile")), args));
		*size = Length(result);
		*data = NewPtr(*size + 4);
		if (*data != nil)
			BlockMove(BinaryData(result), *data, *size);
	}
	return err;
}


// ROM 0x0022bba8 TestFlushReportQueue__13TTestReporterFv
void
TTestReporter::TestFlushReportQueue(void)
{
	TTestAgentEvent event;
	TUPort port(fAgentPort);
	event.fAEventClass = kNewtEventClass;
	event.fAEventID = kTestAgentEventId;
	event.fKind = kTestAgentFlush;
	event.fReporter = fId;
	event.fText[0] = 0;
	event.fSub = 0;
	port.Send(&event, sizeof(event));
}


/*------------------------------------------------------------------------------
	T A g e n t R e p o r t e r
------------------------------------------------------------------------------*/

// ROM 0x0020684c __ct__14TAgentReporterFUlN21
TAgentReporter::TAgentReporter(ULong id, TObjectId agentPort, ULong maxErrors)
	: TTestReporter(id, agentPort, maxErrors)
{ }


// ROM 0x00206898 __dt__14TAgentReporterFv
TAgentReporter::~TAgentReporter()
{ }


// ROM 0x002068d4 AgentReportError__14TAgentReporterFPcT1l
void
TAgentReporter::AgentReportError(char* message, char* where, long error)
{
	char line[256];
	fErrors++;
	sprintf(line, "TestAgent ERR\t%d\t%s\t%s\r", (int) error, where, message);
	SendToTestAgent(kTestAgentAgentError, line, 0);
}


// ROM 0x00206948 AgentReportStatus__14TAgentReporterFlPc
// A status sent as kind 5 with its own kind as the sub-kind: 2 (and 5, 8)
// a test starting - "in newt", or "in ttsk" for a test case task - which
// sets the error count going again; 4, 6 and 9 a test finished, with the
// errors reported and logged; anything else (1: the agent activated,
// with its block; 10: parameters) the block itself, 0xe0 bytes of it.
// A start and a test case's start report the free memory too.
void
TAgentReporter::AgentReportStatus(long kind, char* text)
{
	TDate now;
	UniChar date[32], time[32];
	char dateText[32], timeText[32];
	char line[256];
	char counts[128];
	now.SetCurrentTime();
	now.ShortDateString(0, date, 0x40);
	now.TimeString(0, time, 0x40);
	ConvertFromUnicode(date, dateText, kMacRomanEncoding, 0x20);
	ConvertFromUnicode(time, timeText, kMacRomanEncoding, 0x20);
	if (text == nil)
		text = (char*) "";
	switch (kind)
	{
	case 2: case 5: case 8:
		fErrors = 0;
		sprintf(line, "....................\rTestAgent MSG: test %s started in %s at %s, %s\r",
				text, kind == 5 ? "ttsk" : "newt", timeText, dateText);
		break;
	case 4: case 6: case 9:
		{
			ReportMemoryInfo();
			sprintf(line, "TestAgent MSG: test %s finished at %s, %s\r", text, timeText, dateText);
			ULong logged = fErrors <= fMaxErrors ? fErrors : fMaxErrors;
			sprintf(counts, ".....%d errors reported, %d errors logged\r", (int) fErrors, (int) logged);
			strcat(line, counts);
		}
		break;
	default:
		BlockMove(text, line, 0xe0 + 16);		// (the block's size: TTestAgentEvent's fText)
		break;
	}
	SendToTestAgent(kTestAgentStatus, line, kind);
	if (kind == 2 || kind == 5)
		ReportMemoryInfo();
}


// ROM 0x00206b8c ReportMemoryInfo__14TAgentReporterFv
void
TAgentReporter::ReportMemoryInfo(void)
{
	char line[256];
	sprintf(line, "TestAgent MSG\tTotalSystemFree=%d\r", (int) TotalSystemFree());
	SendToTestAgent(kTestAgentAgentMessage, line, 0);
}


/*------------------------------------------------------------------------------
	T T e s t A g e n t
------------------------------------------------------------------------------*/

// ROM 0x00229b68 __ct__10TTestAgentFv
TTestAgent::TTestAgent()
	: TAppWorld()
{
	fTestTask = 0;
	fServer = nil;
	fPipe = nil;
	fMessages = nil;
	fBusy = false;
	f88 = 0;
	f8c = 0;
	fTestPartHandler = nil;
	fEventHandler = nil;
	fReplyPending = false;
	fFork = nil;
	fTestMgr = nil;
	fNoReporter = false;
}


// ROM 0x002278bc GetSizeOf__10TTestAgentFv
ULong
TTestAgent::GetSizeOf()
{
	return sizeof(TTestAgent);
}


// ROM 0x0022a218 MainConstructor__10TTestAgentFv
// The agent's state; its 'tstp part handler and its 'tste event handler
// (idling every three seconds); the reporter the newt world reports
// through; the message queue.  ==> -1..-5 for each that could not be
// made.
long
TTestAgent::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	fActivated = false;
	fAgentPort = *((TAppWorld*) GetGlobals())->GetMyPort();
	fNewtPort.CopyObject(0);
	fState = 1;
	fServer = nil;
	fLastState = 1;
	fBusy = false;
	fStoreCommands = nil;
	fTestFiles = nil;
	fDataFiles = nil;
	fTestMgr = nil;
	fd8 = 0;
	fFork = nil;
	fRunFromStore = false;
	fTestInfo = (ULong*) NewPtr(0x100);
	fTestInfo[0] = 0;
	fCaseInfo = (ULong*) NewPtr(0x100);
	fCaseInfo[0] = 0;
	fTestPartHandler = new TtstpPart(this);
	if (fTestPartHandler == nil)
		return -1;
	err = fTestPartHandler->Init('tstp');
	if (err != noErr)
		return err;
	fEventHandler = new TTestAgentEventHandler(this);
	if (fEventHandler == nil)
		return -3;
	fEventHandler->Init(kTestAgentEventId, kNewtEventClass);
	fEventHandler->InitIdler(3, kSeconds, 0, true);
	fNoReporter = false;
	AllocateATestReporter(&gTestReporterForNewt);
	if (gTestReporterForNewt == nil)
		return -4;
	gTestAgentMessageQueue = new TMessageQueue;
	fMessages = gTestAgentMessageQueue;
	if (fMessages == nil)
		return -5;
	return noErr;
}


// ROM 0x0022a930 MainDestructor__10TTestAgentFv
// ROM bug kept: the queue is freed but gTestAgentMessageQueue still
// points at it, so a TestMGetReportMsg after the agent has gone reads
// freed memory.  (The host clears it, which the ROM does not: DEVIATION,
// a stale pointer cannot be reproduced safely.)
void
TTestAgent::MainDestructor()
{
	if (fServer != nil)
		DoDropConnection();
	if (fTestPartHandler != nil)
		delete fTestPartHandler;
	if (fEventHandler != nil)
		delete fEventHandler;
	if (fMessages != nil)
	{
		delete fMessages;
		if (gTestAgentMessageQueue == fMessages)
			gTestAgentMessageQueue = nil;
	}
	DisposPtr((Ptr) fTestInfo);
	DisposPtr((Ptr) fCaseInfo);
	TAppWorld::MainDestructor();
}


// ROM 0x0022a9bc MakeFork__10TTestAgentFv
TForkWorld*
TTestAgent::MakeFork()
{
	return new TTestAgent;
}


// ROM 0x0022a9c4 ForkInit__10TTestAgentFP10TForkWorld
// A fork starts with its parent's state (0x108 bytes of it, from the
// agent's port to the server's zone) and not busy.
long
TTestAgent::ForkInit(TForkWorld* parent)
{
	long err = TAppWorld::ForkInit(parent);
	if (err == noErr)
	{
		TTestAgent* agent = (TTestAgent*) parent;
		fAgentPort = agent->fAgentPort;
		fNewtPort = agent->fNewtPort;
		fActivated = agent->fActivated;
		fState = agent->fState;
		fLastState = agent->fLastState;
		f88 = agent->f88;
		f8c = agent->f8c;
		fTestPartHandler = agent->fTestPartHandler;
		fEventHandler = agent->fEventHandler;
		fTestInfo = agent->fTestInfo;
		fCaseInfo = agent->fCaseInfo;
		fTestTask = agent->fTestTask;
		fServer = agent->fServer;
		fPipe = agent->fPipe;
		fMessages = agent->fMessages;
		fDataFileToken = agent->fDataFileToken;
		fReplyPending = agent->fReplyPending;
		fMessageData = agent->fMessageData;
		fd8 = agent->fd8;
		fStoreCommands = agent->fStoreCommands;
		fTestFiles = agent->fTestFiles;
		fDataFiles = agent->fDataFiles;
		memcpy(fServerName, agent->fServerName, sizeof(fServerName));
		memcpy(fServerZone, agent->fServerZone, sizeof(fServerZone));
		fBusy = false;
	}
	return err;
}


// ROM 0x00226a40 ForkConstructor__10TTestAgentFP10TForkWorld
// The fork takes the events over: the parent's handler and the parent
// itself are pointed at it.
long
TTestAgent::ForkConstructor(TForkWorld* parent)
{
	long err = TAppWorld::ForkConstructor(parent);
	if (err != noErr)
		return err;
	TTestAgent* agent = (TTestAgent*) parent;
	agent->fEventHandler->fAgent = this;
	fFork = nil;
	agent->fFork = this;
	return err;
}


// ROM 0x00227868 AllocateATestReporter__10TTestAgentFPP14TAgentReporter
// The newt world's reporter (number 9, reporting to this agent, logging
// at most twenty errors) - unless the agent was told there is to be none,
// when it answers the one there is.  ==> -1 for no memory.
long
TTestAgent::AllocateATestReporter(TAgentReporter** reporter)
{
	long err = noErr;
	TAgentReporter* made = gTestReporterForNewt;
	if (!fNoReporter)
	{
		made = new TAgentReporter(9, fAgentPort, 0x14);
		if (made == nil)
			err = -1;
	}
	*reporter = made;
	return err;
}


// ROM 0x002277ac DoDropConnection__10TTestAgentFv
// NOT YET RECONSTRUCTED: the test server (TCommServer) - its 'drop command
// sent, the connection closed and the server object freed.  With no
// server ever connected on the host there is nothing to drop.
long
TTestAgent::DoDropConnection(void)
{
	fBusy = true;
	if (fServer != nil)
	{
		fServer = nil;
		if (fFork != nil)
			fFork->fServer = nil;
	}
	fBusy = false;
	return noErr;
}


// ROM 0x002286e4 AgentReportDirect__10TTestAgentFUlPc
// ROM bug kept: the format asks for the text and is given none, so the
// line carries whatever the argument word held - here, nothing.
void
TTestAgent::AgentReportDirect(ULong kind, char* text)
{
	char line[256];
	sprintf(line, "TestAgent ERR\t%s\r", "");
	fMessages->EnqueueMessage(kind, nil, line);
}


// ROM 0x00228374 IdleProc__10TTestAgentFv
// The journal played (and put back to idle once the replay is over);
// the queue sent to the server when there is one (NOT YET); a finished
// test's script part and package let go, and the server told the state
// (NOT YET: no server); then the next idle - three seconds after a
// replay ends, fifty milliseconds while a test manager is listening,
// and otherwise the handler's own three seconds.
void
TTestAgent::IdleProc(void)
{
	Boolean replayDone = false;
	if (fBusy)
		return;
	fBusy = true;
	if (gJournallingState == 2)
	{
		JournalInsertTabletSamople();
		replayDone = !IsJournalReplayBusy();
		if (replayDone)
			gJournallingState = 0;
	}
	// NOT YET RECONSTRUCTED: with a test server (fServer), every queued
	// message sent to it (SendCommandHeader, SendChunk; a 'tfil answered
	// with ProcessTestServerCommand)
	if (fState == 2)
	{
		if (gtspsPartHandler != nil && gtspsPartHandler->fTest != nil)
		{
			delete gtspsPartHandler->fTest;
			gtspsPartHandler->fTest = nil;
		}
		if (fTestInfo[0] != 0)
		{
			RemovePackage(fTestInfo[0]);
			fTestInfo[0] = 0;
		}
		// NOT YET RECONSTRUCTED: the state sent to the server ('sdon,
		// 'cdon, 'idle), and running the tests kept on a store
		// (StartACardTestCase, the file lists deleted)
	}
	if (replayDone)
		fEventHandler->ResetIdle(3, kSeconds);
	else if (gTestMgrAppContext == nil)
		fEventHandler->ResetIdle();
	else
		fEventHandler->ResetIdle(50, kMilliseconds);
	fLastState = fState;
	fBusy = false;
}


// ROM 0x0022795c AEHandlerProc__10TTestAgentFP10TUMsgTokenPUlP15TTestAgentEvent
// What a reporter sends.  Messages and errors (kinds 1-4) are queued as
// 'amsg, 'aerr, 'tmsg and 'terr.  A status (kind 5) is by its sub-kind:
// 1 the agent activated (by the task and test manager in the block; a
// name of "*" is a test manager on the machine and drops any server,
// anything else is a server to connect to - NOT YET), 2/4 a test started
// and finished (timed), 5/6/7/8/9 a test case task started and finished
// (timed; NOT YET: the C test cases themselves), 10 the test's name and
// parameters, 11 the agent told to quit, 12 the server dropped, 13 the
// idle made quick.  A finished test is queued, the state noted, and the
// test manager sent testMgrFrameDoneScript (4) or testMgrCaseDoneScript
// (6, 9) through the newt world.  A data file (9) is answered from the
// store's file list, asked of the test manager, or queued as a 'tfil for
// the server; 10 sends the queue on.
void
TTestAgent::AEHandlerProc(TUMsgToken* token, ULong* size, TTestAgentEvent* event)
{
	long newState = -1;
	char timing[128];
	char line[256];
	timing[0] = 0;
	char* text = event->fText;
	ULong kind = 0;
	switch (event->fKind)
	{
	case kTestAgentAgentMessage:	kind = 'amsg'; goto queue;
	case kTestAgentAgentError:		kind = 'aerr'; goto queue;
	case kTestAgentTestMessage:		kind = 'tmsg'; goto queue;
	case kTestAgentTestError:		kind = 'terr'; goto queue;

	case kTestAgentStatus:
		switch (event->fSub)
		{
		case 1:
			{
				// the block AgentReportStatus(1) carries (FActivateTestAgent)
				struct ActivateInfo { TObjectId fTask; RefStruct* fTestMgr; UniChar fName[0x21]; UniChar fServer[0x21]; };
				ActivateInfo* info = (ActivateInfo*) text;
				TUNameServer nameServer;
				fActivated = true;
				TObjectId newtPort = 0;
				ULong spec;
				nameServer.Lookup((char*) "newt", (char*) "TUPort", &newtPort, &spec);
				fNewtPort.CopyObject(newtPort);
				fTestTask = info->fTask;
				fTestMgr = info->fTestMgr;
				Ustrcpy(fServerName, info->fName);
				Ustrcpy(fServerZone, info->fServer);
				if (Ustrcmp(fServerName, kLocalTestManager) == 0)
				{
					if (fServer != nil)
						DoDropConnection();
				}
				else
				{
					// NOT YET RECONSTRUCTED: Setup - the test server named
					// connected to (the test manager sent
					// testMgrConnectToServer before and
					// testMgrConnectedToServer after)
				}
			}
			break;
		case 2:
			// the start times: the global time at +0x10, the task's at +0x18
			{
				TTime now = GetGlobalTime();
				TTime task = GetTaskTime(fTestTask);
				memcpy(&fTestInfo[0x10 / sizeof(ULong)], (void*) &now, sizeof(TTime));
				memcpy(&fTestInfo[0x18 / sizeof(ULong)], (void*) &task, sizeof(TTime));
			}
			goto finished;
		case 3:
			// NOT YET RECONSTRUCTED: Setup when there is no server
			break;
		case 4:
			{
				newState = fState == 5 ? 4 : 2;
				TTime start, taskStart;
				memcpy((void*) &start, &fTestInfo[0x10 / sizeof(ULong)], sizeof(TTime));
				memcpy((void*) &taskStart, &fTestInfo[0x18 / sizeof(ULong)], sizeof(TTime));
				TTime task = GetTaskTime(fTestTask) - taskStart;
				TTime all = GetGlobalTime() - start;
				sprintf(timing, "     duration= %d ms, task duration= %d ms\r",
						(int) all.ConvertTo(kMilliseconds), (int) task.ConvertTo(kMilliseconds));
			}
			goto finished;
		case 5:
		case 8:
			// NOT YET RECONSTRUCTED: a test case task's port looked up
			// ('ttsk) and its start timed (5); the test case run in the
			// activating task (8)
			goto finished;
		case 6:
		case 9:
			newState = fState == 5 ? 3 : 2;
			// NOT YET RECONSTRUCTED: the test case task timed and told to
			// quit (6), its package removed
			goto finished;
		case 7:
			fCaseInfo[0] = 0;
			fState = fState == 2 ? 4 : 5;
			if (fFork != nil)
				fFork->fState = fState;
			{
				int noReporter = 0;
				sscanf(text, "%d", &noReporter);
				fNoReporter = noReporter != 0;
			}
			// NOT YET RECONSTRUCTED: StartCTestCase
			break;
		case 10:
			{
				int which = 0;
				char name[256], params[256];
				name[0] = params[0] = 0;
				sscanf(text, "%d %s %s", &which, name, params);
				RecoverSpacesInString(name, 1);
				RecoverSpacesInString(params, 1);
				if (which == 1 || which == 4)
				{
					strcpy((char*) &fCaseInfo[0x20 / sizeof(ULong)], name);
					strcpy((char*) &fCaseInfo[0x80 / sizeof(ULong)], params);
					if (which != 4)
						return;
				}
				else if (which == 2)
				{
					strcpy((char*) &fTestInfo[0x20 / sizeof(ULong)], name);
					strcpy((char*) &fTestInfo[0x80 / sizeof(ULong)], params);
				}
				else
					return;
				strcpy(gTestReporterForNewt->fName, name);
				strcpy(gTestReporterForNewt->fParameters, params);
			}
			break;
		case 11:
			DoDropConnection();
			AETerminateLoop();
			break;
		case 12:
			DoDropConnection();
			break;
		case 13:
			fEventHandler->ResetIdle(50, kMilliseconds);
			break;
		}
		break;

	finished:
		strcpy(line, text);
		strcat(line, timing);
		fMessages->EnqueueMessage('amsg', nil, line);
		if (newState != -1)
		{
			fState = newState;
			if (fFork != nil)
				fFork->fState = newState;
		}
		if (fTestMgr != nil)
		{
			// the test manager told through the newt world
			TNewtTestScriptEvent done;
			done.fAEventClass = kNewtEventClass;
			done.fAEventID = kTestScriptEventId;
			ULong sub = event->fSub;
			if (sub == 4)
				done.fKind = 6;
			else if (sub == 6 || sub == 9)
				done.fKind = 5;
			else
				break;
			fNewtPort.Send(&done, sizeof(done));
		}
		break;

	case kTestAgentDataFile:
		fDataFileToken = *token;
		fReplyPending = true;
		// NOT YET RECONSTRUCTED: a data file out of the store's file list
		// (fRunFromStore)
		if (gTestMgrAppContext == nil)
			kind = 'tfil';
		else
		{
			// asked of the test manager in the newt world, and its answer
			// passed back as this RPC's.  ROM bug kept: the request is
			// then queued as well, with a kind nothing set (whatever the
			// stack held; nought here).
			TNewtTestScriptEvent ask;
			ask.fAEventClass = kNewtEventClass;
			ask.fAEventID = kTestScriptEventId;
			ask.fKind = 7;
			strcpy(ask.fName, text);
			sprintf(ask.fParameters, "%d %d %d %d", (int) event->fData.fWord[0], (int) event->fData.fWord[1], 0, 0);
			struct { long fErr; long fSize; char* fData; } reply;
			ULong replySize;
			fNewtPort.SendRPC(&replySize, &ask, sizeof(ask), &reply, sizeof(reply));
			fDataFileToken.ReplyRPC(&reply, sizeof(reply));
			fReplyPending = false;
		}
		goto queue;

	case kTestAgentFlush:
		// NOT YET RECONSTRUCTED: with a server, the idle proc run now
		if (fServer != nil)
			IdleProc();
		break;
	}
	return;

queue:
	fMessages->EnqueueMessage(kind, &event->fData, text);
}


/*------------------------------------------------------------------------------
	T T e s t A g e n t E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x00228d8c AEHandlerProc__22TTestAgentEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The agent handles it, and nothing is replied (a data file's RPC is
// answered by the agent itself).
void
TTestAgentEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	fAgent->AEHandlerProc(token, size, (TTestAgentEvent*) event);
	DeferReply();
}


// ROM 0x00228db8 AECompletionProc__22TTestAgentEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TTestAgentEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{ }


// ROM 0x00228db0 IdleProc__22TTestAgentEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TTestAgentEventHandler::IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	fAgent->IdleProc();
}


/*------------------------------------------------------------------------------
	T h e   p a r t s
------------------------------------------------------------------------------*/

// ROM 0x00228d1c Install__9TtstpPartFRC6PartId10SourceTypeP8PartInfo
// A C test case's code copied into a block of its own.
NewtonErr
TtstpPart::Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo)
{
	void* data = NewPtr(partInfo->sizeInMemory);
	if (data == nil)
		return MemError();
	NewtonErr err = Copy(data);
	if (err == noErr)
	{
		fData = data;
		gTestCasePartAddress = data;
	}
	return err;
}


// ROM 0x00228d6c Remove__9TtstpPartFRC6PartIdUll
NewtonErr
TtstpPart::Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr)
{
	DisposPtr((Ptr) fData);
	return noErr;
}


// ROM 0x00229a88 __ct__9TtspsPartFv
TtspsPart::TtspsPart()
	: TFramePartHandler()
{
	fTest = nil;
}


// ROM 0x00229a48 __dt__9TtspsPartFv
TtspsPart::~TtspsPart()
{ }


// ROM 0x00229ad0 InstallFrame__9TtspsPartFRC6RefVarRC6PartId10SourceTypeP8PartInfo
// The test's frame kept (the last one installed: an earlier one's holder
// is simply dropped).
NewtonErr
TtspsPart::InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo)
{
	fTest = new RefStruct;
	if (fTest == nil)
		return MemError();
	*fTest = frame;
	return noErr;
}


// ROM 0x00229b40 RemoveFrame__9TtspsPartFRC6RefVarRC6PartIdUl
NewtonErr
TtspsPart::RemoveFrame(RefArg removeObject, const PartId& partId, PartType partType)
{
	return noErr;
}


/*------------------------------------------------------------------------------
	S t a r t i n g   t h e   a g e n t
------------------------------------------------------------------------------*/

// ROM 0x000ea470 InitTestAgent__Fv
// The agent's world started ('tagt, stack 6000) unless the name server
// has one.  ==> the world's error.
long
InitTestAgent(void)
{
	TUNameServer nameServer;
	TObjectId id;
	ULong spec;
	if (nameServer.Lookup((char*) "tagt", (char*) "TUPort", &id, &spec) == noErr)
		return noErr;
	TTestAgent agent;
	long err = agent.Init(kTestAgentName, true, 6000);
	if (err != noErr)
		printf("Could not Init TTestAgent");
	return err;
}


// ROM 0x00228dbc HandleTestAgentEvent__Fv
void
HandleTestAgentEvent(void)
{ }


// ROM 0x0022a8a4 ReplaceSpacesInString__FPcc
void
ReplaceSpacesInString(char* s, char with)
{
	long n = (long) strlen(s);
	for (long i = 0; i < n; i++)
		if (s[i] == ' ')
			s[i] = with;
}


// ROM 0x0022a8e8 RecoverSpacesInString__FPcc
void
RecoverSpacesInString(char* s, char with)
{
	long n = (long) strlen(s);
	for (long i = 0; i < n; i++)
		if (s[i] == with)
			s[i] = ' ';
}
