/*
	File:		testing/TestAgent.h

	Contains:	The test agent - the ROM's own test harness (TTestAgent, 'tagt).

				The agent is an application world of its own, started when a
				script activates it (ActivateTestAgent) or, on a machine
				built for testing, at boot (TLoader::TheMain with
				gNewtTests & 0x800).  It is where everything a test says
				goes: reporters (TTestReporter) send it 'newt/'tste events
				- messages, errors, a test starting and finishing, a data
				file wanted - and it keeps them in a message queue
				(TMessageQueue) for whoever is listening.  That is either a
				test server on a desktop, over a connection the agent makes
				(TCommServer - NOT YET, the comms side), or a test manager
				on the machine itself: an application activating the agent
				under the name "*" becomes its context (gTestMgrAppContext),
				reads the queue with TestMGetReportMsg and is sent
				testMgrCaseDoneScript/testMgrFrameDoneScript as tests
				finish.

				The agent's idle proc is also what plays the journal
				(testing/Journal.h): a replay only runs while the agent
				does, every 50 milliseconds with a test manager and every
				three seconds without.

				NOT YET RECONSTRUCTED: the test server (TCommServer, Setup,
				ProcessTestServerCommand, DoDropConnection's sending), the
				C test cases (TTestCaseTask, StartCTestCase, the 'tstp
				part's use), the card test cases (StartACardTestCase) and
				the tests kept on a store (MakeTestStore, TTestCommandQueue,
				TTestStoreFileList, DoRunTestsFromStore).
*/

#ifndef __TESTAGENT_H
#define __TESTAGENT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "AppWorld.h"
#include "AEventHandler.h"
#include "UserPorts.h"
#include "PartHandler.h"
#include "FramePartHandler.h"
#include "List.h"
#include "objects.h"

const ULong kTestAgentName			= 'tagt';
const ULong kTestAgentEventId		= 'tste';	// reporter -> agent
const ULong kTestScriptEventId		= 'tsse';	// agent -> the newt world's test manager

// The kinds of event a reporter sends the agent (TTestAgentEvent's fKind).
enum
{
	kTestAgentAgentMessage	= 1,	// queued as 'amsg
	kTestAgentAgentError	= 2,	// 'aerr
	kTestAgentTestMessage	= 3,	// 'tmsg
	kTestAgentTestError		= 4,	// 'terr
	kTestAgentStatus		= 5,	// fSub says what happened (AgentReportStatus's kind)
	kTestAgentDataFile		= 9,	// a data file wanted (an RPC)
	kTestAgentFlush			= 10	// the queue sent on to the server
};

// A four-word block each queued message carries (a data file's offset
// and size).
struct FourULongs
{
	ULong		fWord[4];
};

// The 'newt/'tste event (0x10c bytes in the ROM).
class TTestAgentEvent : public TAEvent
{
public:
	ULong		fKind;			// +0x08
	ULong		fSub;			// +0x0c  a status's kind
	ULong		fReporter;		// +0x10  the reporter's number
	ULong		f14;			// +0x14
	FourULongs	fData;			// +0x18  a data file's offset and size
	ULong		f28;			// +0x28
	// DEVIATION: the ROM's is 0xe0 bytes; the activation block a status
	// of kind 1 carries holds a task id and a pointer, which are wider on
	// the host, so the text is made room for it.
	char		fText[0xe0 + 16];	// +0x2c
};


// The 'newt/'tsse event the agent sends the newt world (0x10c bytes as
// an RPC, 0xf4 as a plain send): 1 a script's source to run (fSource),
// 2 the 'tsps part's test to run, 3 a C test case (NOT YET), 5 and 6 a
// test case and a test frame done, 7 a data file wanted.
class TNewtTestScriptEvent : public TAEvent
{
public:
	ULong		fKind;			// +0x08
	ULong		f0c;			// +0x0c
	void*		fSource;		// +0x10  kind 1: the source (a pointer the handler frees); 3: the test case's class info
	char		fName[0x60];	// +0x14  the test's name, or the data file's
	char		fParameters[0x98];	// +0x74  its parameters, or "offset size 0 0"
};


/*------------------------------------------------------------------------------
	T M e s s a g e Q u e u e
	A CList of handles, newest first: each [size, kind, FourULongs, text].
------------------------------------------------------------------------------*/

class TMessageQueue : public CList			// 0x18 bytes
{
public:
				TMessageQueue();
	void		EnqueueMessage(ULong kind, FourULongs* data, const char* text);
	NewtonErr	DequeueMessage(long* size, ULong* kind, FourULongs* data, char* text);
	Boolean		IsQueueEmpty(void);
};


/*------------------------------------------------------------------------------
	T T e s t R e p o r t e r
	What a test reports through: each report an event to the agent's port.
------------------------------------------------------------------------------*/

class TTestReporter							// 0x1a0 bytes
{
public:
				TTestReporter(ULong id, TObjectId agentPort, ULong maxErrors);
				~TTestReporter();

	void		SendToTestAgent(ULong kind, char* text, long sub);
	void		TestReportError(char* message, char* where, long error);
	void		TestReportErrorValues(char* message, char* where, long got, long expected);
	void		TestReportMessage(char* message, char* where);
	void		TestFPrintf(int kind, const char* format, ...);
	long		TestReadDataFile(char* name, long offset, long* size, char** data);
	void		TestFlushReportQueue(void);

	char		fName[0x60];		// +0x000  the test's name
	char		fParameters[0x128];	// +0x060  its parameter string
	TObjectId	fAgentPort;			// +0x188
	ULong		fId;				// +0x18c
	ULong		f190;				// +0x190  (1)
	ULong		f194;
	ULong		fMaxErrors;			// +0x198  the most errors worth logging
	ULong		fErrors;			// +0x19c  errors reported since the test started
};


// The agent's own reporter: the status of the tests it runs.
class TAgentReporter : public TTestReporter
{
public:
				TAgentReporter(ULong id, TObjectId agentPort, ULong maxErrors);
				~TAgentReporter();

	void		AgentReportError(char* message, char* where, long error);
	void		AgentReportStatus(long kind, char* text);
	void		ReportMemoryInfo(void);
};


/*------------------------------------------------------------------------------
	T T e s t A g e n t
------------------------------------------------------------------------------*/

class TCommServer;
class TTestAgentEventHandler;
class TTestStoreFileList;

class TTestAgent : public TAppWorld			// 0x178 bytes
{
public:
						TTestAgent();

	virtual ULong		GetSizeOf();
	virtual long		MainConstructor();
	virtual void		MainDestructor();
	virtual long		ForkInit(TForkWorld* parent);
	virtual long		ForkConstructor(TForkWorld* parent);
	virtual TForkWorld*	MakeFork();

	void		AEHandlerProc(TUMsgToken* token, ULong* size, TTestAgentEvent* event);
	void		IdleProc(void);
	void		AgentReportDirect(ULong kind, char* text);
	long		AllocateATestReporter(TAgentReporter** reporter);
	long		DoDropConnection(void);

	TObjectId			fAgentPort;			// +0x070  its own port
	TUPort				fNewtPort;			// +0x074  the newt world's
	Boolean				fActivated;			// +0x07c
	ULong				fState;				// +0x080  1 idle, 2 a test running, 3/4 done, 5 a test case task running
	ULong				fLastState;			// +0x084
	ULong				f88;				// +0x088
	ULong				f8c;				// +0x08c
	TPartHandler*		fTestPartHandler;	// +0x090  'tstp
	TTestAgentEventHandler*	fEventHandler;	// +0x094  'tste
	ULong*				fTestInfo;			// +0x098  0x100 bytes: [0] the test's package, +0x10/+0x18 its start times, +0x20 name, +0x80 parameters
	ULong*				fCaseInfo;			// +0x09c  likewise for a test case task (+4 its task, +8 its port)
	TObjectId			fTestTask;			// +0x0a0  the task that activated the agent
	TCommServer*		fServer;			// +0x0a4  the test server's connection (NOT YET: always nil)
	void*				fPipe;				// +0x0a8
	TMessageQueue*		fMessages;			// +0x0ac
	Boolean				fBusy;				// +0x0b0  in the idle proc or the server
	TUMsgToken			fDataFileToken;		// +0x0b4  a data file RPC being answered
	Boolean				fReplyPending;		// +0x0c4
	FourULongs			fMessageData;		// +0x0c8
	ULong				fd8;				// +0x0d8
	ULong*				fStoreCommands;		// +0x0dc  (NOT YET: tests on a store)
	TTestStoreFileList*	fTestFiles;			// +0x0e0
	TTestStoreFileList*	fDataFiles;			// +0x0e4
	UniChar				fServerName[0x21];	// +0x0e8
	UniChar				fServerZone[0x21];	// +0x12a
	Boolean				fRunFromStore;		// +0x16d
	Boolean				fDeleteWhenDone;	// +0x16e
	Boolean				fNoReporter;		// +0x16f
	TTestAgent*			fFork;				// +0x170
	RefStruct*			fTestMgr;			// +0x174  the test manager's context (gTestMgrAppContext)
};


class TTestAgentEventHandler : public TAEventHandler		// 0x18 bytes
{
public:
				TTestAgentEventHandler(TTestAgent* agent) : fAgent(agent) {}
	virtual	void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual	void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual	void	IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);

	TTestAgent*	fAgent;					// +0x14
};


// The 'tstp part: a C test case's code, copied into memory (the part's
// address is gTestCasePartAddress).
class TtstpPart : public TPartHandler		// 0x40 bytes
{
public:
				TtstpPart(TTestAgent* agent) : fData(nil), fAgent(agent) {}
	virtual	NewtonErr	Install(const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	Remove(const PartId& partId, PartType partType, RemoveObjPtr removePtr);

	void*		fData;					// +0x38
	TTestAgent*	fAgent;					// +0x3c
};


// The 'tsps part: a NewtonScript test - a frame whose testScript method
// is the test.
class TtspsPart : public TFramePartHandler	// 0x40 bytes
{
public:
				TtspsPart();
				~TtspsPart();
	virtual	NewtonErr	InstallFrame(RefArg frame, const PartId& partId, SourceType sourceType, PartInfo* partInfo);
	virtual	NewtonErr	RemoveFrame(RefArg removeObject, const PartId& partId, PartType partType);

	RefStruct*	fTest;					// +0x3c  the part's frame
};


// The newt world's side: what the agent tells the test manager.
class TNewtTestScriptEventHandler : public TAEventHandler	// 0x28 bytes
{
public:
				TNewtTestScriptEventHandler() {}
	virtual	void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual	void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);

	TUMsgToken	fToken;					// +0x18  a data file RPC being answered
};


extern TAgentReporter*		gTestReporterForNewt;		// ROM 0x0c104d48 gTestReporterForNewt - nil while the agent is not running
extern TMessageQueue*		gTestAgentMessageQueue;		// ROM 0x0c104d54
extern RefStruct*			gTestMgrAppContext;			// ROM 0x0c104d50 gTestMgrAppContext - the on-machine test manager
extern TtspsPart*			gtspsPartHandler;			// ROM 0x0c104d58
extern void*				gTestCasePartAddress;

long	InitTestAgent(void);							// ROM 0x000ea470 InitTestAgent__Fv - the agent started unless it is running
void	HandleTestAgentEvent(void);						// ROM 0x00228dbc HandleTestAgentEvent__Fv - (empty)
void	ReplaceSpacesInString(char* s, char with);		// ROM 0x0022a8a4 ReplaceSpacesInString__FPcc
void	RecoverSpacesInString(char* s, char with);		// ROM 0x0022a8e8 RecoverSpacesInString__FPcc

// the C library's standard i/o gate: the serial debugger's link carries
// it only while the count is below one (StdioOff counts up, StdioOn down)
extern long	gStdioOffCount;								// ROM 0x0c10553c
void	StdioOn(void);									// ROM 0x003193ac StdioOn__Fv
void	StdioOff(void);									// ROM 0x00319018 StdioOff__Fv

void	RegisterTestAgentNatives(void);					// ActivateTestAgent, the Test* natives, StdioOn/Off, debug, ...

#endif	/* __TESTAGENT_H */
