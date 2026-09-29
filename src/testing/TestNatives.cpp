/*
	File:		testing/TestNatives.cpp

	Contains:	The test agent's NewtonScript side: ActivateTestAgent and
				DeactivateTestAgent, the newt world's handler for what the
				agent tells the test manager, the Test* natives a test
				script reports and reads through, and the debugging hooks
				(StdioOn/StdioOff, debug, DebugRunUntilIdle,
				DebugMemoryStats, HobbleTablet).  See TestAgent.h.
*/

#include "TestAgent.h"
#include "Journal.h"
#include "NewtWorld.h"
#include "NameServer.h"
#include "NewtonMemory.h"
#include "Unicode.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "Compiler.h"
#include "RSSymbols.h"
#include "RichString.h"
#include "REPTranslators.h"
#include "RootView.h"
#include "View.h"
#include "Application.h"
#include "Ports.h"
#include <stdio.h>
#include <string.h>

static TNewtTestScriptEventHandler*	gNewtTestScriptHandler = nil;	// (unnamed in the ROM, at 0x0c104d4c)
static Boolean						gTestWillExit = false;			// (unnamed in the ROM, at 0x0c104d5c) a test script's test finished (TestWillCallExit clears it)

static const UniChar kLocalTestManager[] = { '*', 0 };

// a string's characters
static inline UniChar*
UStringOf(RefArg str)
{
	return (UniChar*) BinaryData(str);
}


// the text of a string argument in the 8-bit encoding (at most 0xff bytes)
static void
CStringOf(RefArg str, char* buffer)
{
	ConvertFromUnicode(UStringOf(str), buffer, kMacRomanEncoding, 0xff);
}


// DEVIATION: most of the natives below report through
// gTestReporterForNewt (and look in gtspsPartHandler) without asking
// whether the agent is running, which on the machine reads and sends
// through low memory; the host answers nil instead of calling through a
// nil pointer.
#define NEEDS_AGENT		if (gTestReporterForNewt == nil) return NILREF
#define TEST_PART		(gtspsPartHandler != nil ? gtspsPartHandler->fTest : nil)


/*------------------------------------------------------------------------------
	T N e w t T e s t S c r i p t E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// The 'tsps part's test run: its testScript method sent; a NewtonScript
// error is reported as "Test script failed!!" with the error code and
// symbol of a frame exception, and the part let go.  ==> whether it ran.
static void
RunTestScriptPart(void)
{
	RefVar test(*gtspsPartHandler->fTest);
	RefVar script(GetFrameSlot(test, RefVar(Intern((char*) "testscript"))));
	gTestWillExit = true;
	if (ISNIL(script))
		gTestReporterForNewt->AgentReportError((char*) "Cannot find testscript slot", (char*) "", 0);
	else
	{
		newton_try
		{
			DoMessage(test, RefVar(Intern((char*) "testScript")), RefVar(NILREF));
		}
		newton_catch("evt.ex")
		{
			gTestWillExit = true;
			char symbol[256];
			symbol[0] = 0;
			long error = 0;
			Exception* x = CurrentException();
			if (Subexception(x->name, (ExceptionName) "type.ref"))
			{
				RefVar data(*(RefStruct*) x->data);
				if (IsFrame(data))
				{
					RefVar code(GetFrameSlot(data, RefVar(Intern((char*) "errorcode"))));
					if (ISINT(code))
						error = RINT(code);
					RefVar sym(GetFrameSlot(data, RefVar(Intern((char*) "symbol"))));
					if (IsSymbol(sym))
						strcpy(symbol, SymbolName(sym));
					else
						memcpy(symbol, "(unknown)", 10);
				}
			}
			gTestReporterForNewt->AgentReportError((char*) "Test script failed!! ", symbol, error);
		}
		end_try;
	}
	if (gTestWillExit)
	{
		if (gtspsPartHandler->fTest != nil)
			delete gtspsPartHandler->fTest;
		gtspsPartHandler->fTest = nil;
	}
}


// ROM 0x00229288 AEHandlerProc__27TNewtTestScriptEventHandlerFP10TUMsgTokenPUlP7TAEvent
// What the agent tells the newt world, drawn in the main port: 1 a
// script's source compiled and run (the source's block freed), 2 the
// 'tsps part's test run, 3 a C test case (NOT YET: DoNewtCTestCase),
// 5/6 the test manager sent testMgrCaseDoneScript/testMgrFrameDoneScript,
// 7 the test manager asked for a data file (testMgrReadDataFile with
// [name, offset, size]) and its binary copied into a block the reply
// carries (-1 for none).  The reply is deferred: the RPC of 7 is answered
// here.
void
TNewtTestScriptEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* inEvent)
{
	TNewtTestScriptEvent* event = (TNewtTestScriptEvent*) inEvent;
	GrafPtr savedPort;
	GetPort(&savedPort);
	SetPort(&gGrafPort);
	switch (event->fKind)
	{
	case 1:
		if (event->fSource != nil)
		{
			strcpy(gTestReporterForNewt->fName, event->fName);
			strcpy(gTestReporterForNewt->fParameters, event->fParameters);
			gTestReporterForNewt->AgentReportStatus(2, event->fName);
			RefVar source(MakeString((const char*) event->fSource));
			RefVar code(ParseString(source));
			if (NOTNIL(code))
				InterpretBlock(code, RefVar(gVarFrame));
			DisposPtr((Ptr) event->fSource);
			gTestReporterForNewt->AgentReportStatus(4, event->fName);
		}
		break;
	case 2:
		strcpy(gTestReporterForNewt->fName, event->fName);
		strcpy(gTestReporterForNewt->fParameters, event->fParameters);
		gTestReporterForNewt->AgentReportStatus(2, event->fName);
		if (gtspsPartHandler->fTest != nil)
			RunTestScriptPart();
		if (gTestWillExit)
			gTestReporterForNewt->AgentReportStatus(4, event->fName);
		break;
	case 3:
		strcpy(gTestReporterForNewt->fName, event->fName);
		strcpy(gTestReporterForNewt->fParameters, event->fParameters);
		// NOT YET RECONSTRUCTED: DoNewtCTestCase (0x0022909c), the C test
		// case whose class info the event carries run in this task
		gTestReporterForNewt->AgentReportStatus(9, event->fName);
		break;
	case 5:
		DoMessage(*gTestMgrAppContext, RefVar(Intern((char*) "testMgrCaseDoneScript")), RefVar(NILREF));
		break;
	case 6:
		DoMessage(*gTestMgrAppContext, RefVar(Intern((char*) "testMgrFrameDoneScript")), RefVar(NILREF));
		break;
	case 7:
		{
			RefVar args(AllocateArray(RefVar(RSSYMarray), 3));
			fToken = *token;
			UniChar name[0x60];
			ConvertToUnicode(event->fName, name, kMacRomanEncoding, 0x7fffffff);
			int offset = 0, length = 0, unused = 0;
			sscanf(event->fParameters, "%d %d %d", &offset, &length, &unused);
			SetArraySlot(args, 0, RefVar(MakeString(name)));
			SetArraySlot(args, 1, RefVar(MAKEINT(offset)));
			SetArraySlot(args, 2, RefVar(MAKEINT(length)));
			RefVar data(DoMessage(*gTestMgrAppContext, RefVar(Intern((char*) "testMgrReadDataFile")), args));
			struct { long fErr; long fSize; char* fData; } reply;
			if (ISNIL(data))
				reply.fErr = -1;
			else
			{
				reply.fErr = 0;
				reply.fSize = Length(data);
				char* block = NewPtr(reply.fSize + 0x14);
				if (block != nil)
				{
					BlockMove(BinaryData(data), block, reply.fSize);
					reply.fData = block;
				}
			}
			fToken.ReplyRPC(&reply, sizeof(reply));
		}
		break;
	}
	DeferReply();
	SetPort(savedPort);
}


// ROM 0x00229a44 AECompletionProc__27TNewtTestScriptEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TNewtTestScriptEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{ }


/*------------------------------------------------------------------------------
	A c t i v a t i n g   t h e   a g e n t
------------------------------------------------------------------------------*/

// ROM 0x00228dc0 FActivateTestAgent
// ActivateTestAgent(name, server): the agent started if it is not
// running.  The first time (it was not registered), the newt world's
// 'tsse handler and the 'tsps part handler are made.  A name of "*" makes
// the caller the test manager (gTestMgrAppContext); anything else is a
// test server to connect to (NOT YET).  The agent is told the task, the
// manager, the name and the server ("*" when there is none).
static Ref
FActivateTestAgent(RefArg rcvr, RefArg name, RefArg server)
{
	TUNameServer nameServer;
	if (!IsString(name))
		return NILREF;
	TObjectId id;
	ULong spec;
	NewtonErr notRunning = nameServer.Lookup((char*) "tagt", (char*) "TUPort", &id, &spec);
	InitTestAgent();
	if (gTestReporterForNewt == nil)
		return NILREF;
	if (notRunning != noErr)
	{
		gNewtTestScriptHandler = new TNewtTestScriptEventHandler;
		gNewtTestScriptHandler->Init(kTestScriptEventId, kNewtEventClass);
		gtspsPartHandler = new TtspsPart;
		if (gtspsPartHandler == nil || gtspsPartHandler->Init('tsps') != noErr)
			return NILREF;
	}
	if (Ustrcmp(UStringOf(name), kLocalTestManager) == 0)
	{
		gTestMgrAppContext = new RefStruct;
		if (gTestMgrAppContext == nil)
			MemError();
		else
			*gTestMgrAppContext = rcvr;
	}
	struct ActivateInfo { TObjectId fTask; RefStruct* fTestMgr; UniChar fName[0x21]; UniChar fServer[0x21]; };
	char block[0xe0 + 16];
	ActivateInfo* info = (ActivateInfo*) block;
	info->fTask = gCurrentTaskId;
	info->fTestMgr = gTestMgrAppContext;
	Ustrcpy(info->fName, UStringOf(name));
	if (!IsString(server))
	{
		info->fServer[0] = '*';
		info->fServer[1] = 0;
	}
	else
		Ustrcpy(info->fServer, UStringOf(server));
	gTestReporterForNewt->AgentReportStatus(1, block);
	return NILREF;
}


// ROM 0x00228ffc FDeactivateTestAgent
// The test manager, the 'tsps part handler and the 'tsse handler let go,
// the agent told (11: it quits), the reporter freed and the journal's
// replay stopped.
static Ref
FDeactivateTestAgent(RefArg rcvr)
{
	if (gTestMgrAppContext != nil)
	{
		delete gTestMgrAppContext;
		gTestMgrAppContext = nil;
	}
	if (gtspsPartHandler != nil)
	{
		delete gtspsPartHandler;
		gtspsPartHandler = nil;
	}
	if (gNewtTestScriptHandler != nil)
	{
		delete gNewtTestScriptHandler;
		gNewtTestScriptHandler = nil;
	}
	// (the ROM sends the status without asking whether there is a
	// reporter: NEEDS_AGENT's DEVIATION)
	if (gTestReporterForNewt != nil)
		gTestReporterForNewt->AgentReportStatus(11, nil);
	if (gTestReporterForNewt != nil)
		delete gTestReporterForNewt;
	gTestReporterForNewt = nil;
	JournalStopReplay();
	return NILREF;
}


/*------------------------------------------------------------------------------
	R e p o r t i n g
------------------------------------------------------------------------------*/

// ROM 0x00229b48 FTestGetParameterString
static Ref
FTestGetParameterString(RefArg rcvr)
{
	const char* params = gTestReporterForNewt != nil ? gTestReporterForNewt->fParameters : "";
	long n = (long) strlen(params);
	RefVar str(AllocateBinary(RefVar(RSSYMstring), n * sizeof(UniChar) + sizeof(UniChar)));
	ConvertToUnicode(params, (UniChar*) BinaryData(str), kMacRomanEncoding, 0x7fffffff);
	return str;
}


// ROM 0x00229bd8 FTestGetParameterArray
// The test's name and then its parameters, split at spaces (a quoted one
// running to the matching quote), ten at most.  The positions are
// halfwords, as the ROM's are.
static Ref
FTestGetParameterArray(RefArg rcvr)
{
	RefVar result(MakeArray(0));
	if (gTestReporterForNewt == nil)
		return result;
	AddArraySlot(result, RefVar(MakeString(gTestReporterForNewt->fName)));
	char params[256];
	strcpy(params, gTestReporterForNewt->fParameters);
	short i = 0;
	short n = (short) strlen(params);
	for (short count = 0; i <= n && count < 10; count++)
	{
		while (params[i] == ' ')
			i++;
		if (i >= n)
			break;
		char end = params[i];
		if (end == '\'' || end == '"')
			i++;
		else
			end = ' ';
		short j = i;
		while (params[j] != end && j <= n)
			j++;
		params[j] = 0;
		AddArraySlot(result, RefVar(MakeString(params + i)));
		i = j + 1;
	}
	return result;
}


// ROM 0x00229d5c FTestReportError
// With no agent the message is simply printed.
static Ref
FTestReportError(RefArg rcvr, RefArg message)
{
	if (gTestReporterForNewt == nil)
	{
		FPrint(rcvr, message);
		return NILREF;
	}
	char text[256];
	CStringOf(message, text);
	gTestReporterForNewt->TestReportError(text, nil, 0);
	return NILREF;
}


// ROM 0x00229de8 FTestReportMessage
static Ref
FTestReportMessage(RefArg rcvr, RefArg message)
{
	if (gTestReporterForNewt == nil)
	{
		FPrint(rcvr, message);
		return NILREF;
	}
	char text[256];
	CStringOf(message, text);
	gTestReporterForNewt->TestReportMessage(text, nil);
	return NILREF;
}


// ROM 0x00229e70 FStdioOn
static Ref
FStdioOn(RefArg rcvr)
{
	StdioOn();
	return NILREF;
}


// ROM 0x00229e88 FStdioOff
static Ref
FStdioOff(RefArg rcvr)
{
	StdioOff();
	return NILREF;
}


// ROM 0x00229ea0 FTestReadTextFile
// TestReadTextFile(name, offset, size): the file's text as a string, nil
// when it could not be read.
static Ref
FTestReadTextFile(RefArg rcvr, RefArg name, RefArg offset, RefArg size)
{
	NEEDS_AGENT;
	char file[256];
	CStringOf(name, file);
	long start = RINT(offset);
	long length = RINT(size);
	char* data;
	if (gTestReporterForNewt->TestReadDataFile(file, start, &length, &data) != noErr)
		return NILREF;
	data[length] = 0;
	RefVar str(AllocateBinary(RefVar(RSSYMstring), (length + 1) * sizeof(UniChar)));
	if (NOTNIL(str))
		ConvertToUnicode(data, (UniChar*) BinaryData(str), kMacRomanEncoding, length + 1);
	DisposPtr(data);
	return str;
}


// ROM 0x00229fb0 FTestReadDataFile
// TestReadDataFile(name, offset, size): the file's bytes as a 'foo binary.
static Ref
FTestReadDataFile(RefArg rcvr, RefArg name, RefArg offset, RefArg size)
{
	NEEDS_AGENT;
	char file[256];
	CStringOf(name, file);
	long start = RINT(offset);
	long length = RINT(size);
	char* data;
	if (gTestReporterForNewt->TestReadDataFile(file, start, &length, &data) != noErr)
		return NILREF;
	RefVar bin(AllocateBinary(RefVar(RSSYMfoo), length));
	if (NOTNIL(bin))
		BlockMove(data, BinaryData(bin), length);
	DisposPtr(data);
	return bin;
}


// ROM 0x0022a0e4 FTestFlushReportQueue
static Ref
FTestFlushReportQueue(RefArg rcvr)
{
	if (gTestReporterForNewt != nil)
		gTestReporterForNewt->TestFlushReportQueue();
	return NILREF;
}


// ROM 0x0022a10c FTestMSetParameterString
// TestMSetParameterString(which, name, parameters): the agent told the
// name and parameters of the test (2), the test case (1), or both (4) -
// spaces made \x01 on the way, which the agent turns back.
static Ref
FTestMSetParameterString(RefArg rcvr, RefArg which, RefArg name, RefArg params)
{
	NEEDS_AGENT;
	if (!ISINT(which) || !IsString(name))
		return NILREF;
	char nameText[256], paramText[256], line[256];
	CStringOf(name, nameText);
	if (!IsString(params))
		return NILREF;
	CStringOf(params, paramText);
	ReplaceSpacesInString(nameText, 1);
	ReplaceSpacesInString(paramText, 1);
	sprintf(line, "%d %s %s", (int) RINT(which), nameText, paramText);
	gTestReporterForNewt->AgentReportStatus(10, line);
	return NILREF;
}


// ROM 0x0022a200 FTestWillCallExit
static Ref
FTestWillCallExit(RefArg rcvr)
{
	gTestWillExit = false;
	return NILREF;
}


// ROM 0x0022a3b0 FTestExit
// The test finished (with a test script part in hand) and the journal's
// replay stopped.
static Ref
FTestExit(RefArg rcvr)
{
	NEEDS_AGENT;
	if (TEST_PART != nil)
		gTestReporterForNewt->AgentReportStatus(4, nil);
	JournalStopReplay();
	return NILREF;
}


// ROM 0x0022a3f0 FTestMStartTestFrame
// TestMStartTestFrame(name, noReporter): with an integer the agent is
// told to run a C test case (7, with the flag); otherwise the test is
// started and the 'tsps part's testScript run, as the 'tsse handler's 2.
static Ref
FTestMStartTestFrame(RefArg rcvr, RefArg name, RefArg flag)
{
	NEEDS_AGENT;
	if (!IsString(name))
		return NILREF;
	char text[256];
	CStringOf(name, text);
	if (ISINT(flag))
	{
		char number[8];
		sprintf(number, "%d", (int) (RINT(flag) & 0xff));
		gTestReporterForNewt->AgentReportStatus(7, number);
		return NILREF;
	}
	gTestReporterForNewt->AgentReportStatus(2, text);
	if (TEST_PART != nil)
		RunTestScriptPart();
	if (gTestWillExit)
		gTestReporterForNewt->AgentReportStatus(4, text);
	return NILREF;
}


// ROM 0x0022a7cc FTestMStartTestCase
static Ref
FTestMStartTestCase(RefArg rcvr, RefArg noReporter)
{
	NEEDS_AGENT;
	char number[20];
	sprintf(number, "%d", NOTNIL(noReporter) ? 1 : 0);
	gTestReporterForNewt->AgentReportStatus(7, number);
	return NILREF;
}


// ROM 0x0022a820 FTestMGetReportMsg
// The oldest message the agent has queued, or nil.
static Ref
FTestMGetReportMsg(RefArg rcvr)
{
	if (gTestAgentMessageQueue == nil || gTestAgentMessageQueue->IsQueueEmpty())
		return NILREF;
	long size;
	ULong kind;
	FourULongs data;
	char text[256];
	gTestAgentMessageQueue->DequeueMessage(&size, &kind, &data, text);
	return MakeString(text);
}


// ROM 0x0022a878 FTestMDropConnection
static Ref
FTestMDropConnection(RefArg rcvr)
{
	NEEDS_AGENT;
	gTestReporterForNewt->AgentReportStatus(12, nil);
	return NILREF;
}


/*------------------------------------------------------------------------------
	D e b u g g i n g
------------------------------------------------------------------------------*/

// ROM 0x001e9b40 DebugHashValue__FRC6RefVar
// A string's hash, as a view's `debug` slot may hold it instead of its
// name: each character, folded to lower case if it is a capital letter,
// XORed into the hash shifted left one.  (The ROM reads the characters
// with word loads at halfword addresses, which on the ARM comes to the
// same character.)  ==> the hash as an integer Ref, 32-bit as the ROM's.
static Ref
DebugHashValue(RefArg str)
{
	const UniChar* s = (const UniChar*) BinaryData(str);
	long n = Ustrlen(s);
	uint32_t hash = 0;
	for (long i = 0; i < n; i++)
	{
		uint32_t c = s[i];
		if (c > 0x40 && c < 0x5b)
			c += 0x20;
		hash = c ^ (hash << 1);
	}
	return MAKEINT((int32_t) (hash << 2) >> 2);
}


static Ref FindForm(RefArg context, RefArg form);

// ROM 0x001e9bd8 FindForm__FRC6RefVarT1
// The view a form names, below `context`: an array is a path, each
// element looked for below the last; an integer is that child of the
// viewChildren (the view if there is one, else the template); a string is
// a view whose `debug` slot is that string (or its DebugHashValue), or
// whose text is - a text starting with 0xfc01 compared from its second
// character.  Failing that, every child is searched in turn - the open
// view's children, or the template's viewChildren.  ==> the context, nil.
static Ref
FindForm(RefArg context, RefArg form)
{
	RefVar found;
	if (IsArray(form))
	{
		found = context;
		long n = Length(form);
		for (long i = 0; i < n; i++)
		{
			found = FindForm(found, RefVar(GetArraySlot(form, i)));
			if (ISNIL(found))
				break;
		}
	}
	else if (IsString(form))
	{
		RefVar debug(GetProtoVariable(context, RSSYMdebug, nil));
		if (NOTNIL(debug))
		{
			if (!IsString(debug))
			{
				if (ISINT(debug) && DebugHashValue(form) == (Ref) debug)
					found = context;
			}
			else
			{
				TRichString a(form);
				TRichString b(debug);
				if (a.CompareSubStringCommon(b, 0, -1, false) == 0)
					found = context;
			}
		}
		if (ISNIL(found))
		{
			RefVar text(GetProtoVariable(context, RSSYMtext, nil));
			if (NOTNIL(text))
			{
				if (!IsString(text))
				{
					gREPout->Print("### non-string found in 'text slot!\r");
					PrintObject(context, 0);
				}
				else
				{
					TRichString a(form);
					TRichString b(text);
					ULong start = 0;
					if (b.Length() != 0 && b.GetChar(0) == 0xfc01)
						start = 1;
					if (b.CompareSubStringCommon(a, start, -1, false) == 0)
						found = context;
				}
			}
		}
	}
	else if (ISINT(form))
	{
		RefVar children(GetProtoVariable(context, RSSYMviewchildren, nil));
		RefVar child(GetArraySlot(children, RINT(form)));
		TView* view = GetView(context, child);
		found = view != nil ? (Ref) view->fContext : NILREF;
		if (ISNIL(found))
			found = GetArraySlot(children, RINT(form));
	}
	if (ISNIL(found))
	{
		TView* view = GetView(context);
		if (view == nil)
		{
			RefVar children(GetProtoVariable(context, RSSYMviewchildren, nil));
			if (NOTNIL(children))
			{
				TObjectIterator iter(children, false);
				while (!iter.Done())
				{
					found = FindForm(RefVar(iter.Value()), form);
					if (NOTNIL(found))
						break;
					iter.Next();
				}
			}
		}
		else
		{
			TListLoop loop(view->fChildren);
			TView* child;
			while ((child = (TView*) loop.Next()) != nil)
			{
				found = FindForm(child->fContext, form);
				if (NOTNIL(found))
					break;
			}
		}
	}
	return found;
}


// ROM 0x001ea0a8 FDebug
// debug(form): the view a name, an index or a path means, from the root.
static Ref
FDebug(RefArg rcvr, RefArg form)
{
	return FindForm(gRootView->fContext, form);
}


// ROM 0x001ea0bc FDebugMemoryStats
static Ref
FDebugMemoryStats(RefArg rcvr)
{
	return NILREF;
}


// ROM 0x001ea0c4 FDebugRunUntilIdle
// The screen brought up to date and the application run, then every
// delayed action that is due, the screen brought up to date after each.
static Ref
FDebugRunUntilIdle(RefArg rcvr)
{
	gRootView->Update(nil);
	gApplication->Run();
	while (gApplication->RunNextDelayedAction())
		gRootView->Update(nil);
	return NILREF;
}


// ROM 0x0013fb98 HobbleTablet__Fv
// The inker sent its 0x1d command ('newt/'inkr), which slows the tablet
// down.  NOT YET RECONSTRUCTED: the inker's command handler; the host
// has no inker port (gTheInkerPort is nil), so nothing is sent.
static void
HobbleTablet(void)
{
	if (gTheInkerPort == nil)
		return;
	struct { TAEvent fEvent; ULong fCommand; } command;
	command.fEvent.fAEventClass = kNewtEventClass;
	command.fEvent.fAEventID = 'inkr';
	command.fCommand = 0x1d;
	TAEvent reply[3];
	ULong replySize;
	gTheInkerPort->SendRPC(&replySize, &command, sizeof(command), reply, sizeof(reply));
}


// ROM 0x00140094 FHobbleTablet
static Ref
FHobbleTablet(RefArg rcvr)
{
	HobbleTablet();
	return NILREF;
}


void
RegisterTestAgentNatives(void)
{
	RegisterSerialDebuggerNatives();
	RegisterNativeFunction("FActivateTestAgent", (void*) FActivateTestAgent, 2);
	RegisterNativeFunction("FDeactivateTestAgent", (void*) FDeactivateTestAgent, 0);
	RegisterNativeFunction("FTestGetParameterString", (void*) FTestGetParameterString, 0);
	RegisterNativeFunction("FTestGetParameterArray", (void*) FTestGetParameterArray, 0);
	RegisterNativeFunction("FTestReportError", (void*) FTestReportError, 1);
	RegisterNativeFunction("FTestReportMessage", (void*) FTestReportMessage, 1);
	RegisterNativeFunction("FStdioOn", (void*) FStdioOn, 0);
	RegisterNativeFunction("FStdioOff", (void*) FStdioOff, 0);
	RegisterNativeFunction("FTestReadTextFile", (void*) FTestReadTextFile, 3);
	RegisterNativeFunction("FTestReadDataFile", (void*) FTestReadDataFile, 3);
	RegisterNativeFunction("FTestFlushReportQueue", (void*) FTestFlushReportQueue, 0);
	RegisterNativeFunction("FTestMSetParameterString", (void*) FTestMSetParameterString, 3);
	RegisterNativeFunction("FTestWillCallExit", (void*) FTestWillCallExit, 0);
	RegisterNativeFunction("FTestExit", (void*) FTestExit, 0);
	RegisterNativeFunction("FTestMStartTestFrame", (void*) FTestMStartTestFrame, 2);
	RegisterNativeFunction("FTestMStartTestCase", (void*) FTestMStartTestCase, 1);
	RegisterNativeFunction("FTestMGetReportMsg", (void*) FTestMGetReportMsg, 0);
	RegisterNativeFunction("FTestMDropConnection", (void*) FTestMDropConnection, 0);
	RegisterNativeFunction("FDebug", (void*) FDebug, 1);
	RegisterNativeFunction("FDebugMemoryStats", (void*) FDebugMemoryStats, 0);
	RegisterNativeFunction("FDebugRunUntilIdle", (void*) FDebugRunUntilIdle, 0);
	RegisterNativeFunction("FHobbleTablet", (void*) FHobbleTablet, 0);
}
