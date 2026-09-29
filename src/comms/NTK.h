/*
	File:		comms/NTK.h

	Contains:	The Newton Toolkit's inspector connection: the debugger nub
				the desktop's NTK (or any tool speaking its protocol) talks
				to over a serial (MNP) or AppleTalk connection, and the
				plain-text tethered listener.

				ntkListener (a script, usually the Toolkit application's)
				makes the nub: two task-safe ring buffers
				(utility/TaskSafeRingBuffer.h), a task of its own
				(TNTKTask, the 'ntk ' world) whose endpoint client moves
				bytes between the connection and those buffers, and the REP
				translators (PNTKInTranslator, PNTKOutTranslator) that
				replace the REP's, reading commands and writing answers
				through the buffers.  The REP's own idler
				(TREPEventHandler, made by NTKInit at boot) runs them.

				The protocol: every message is 'newt' 'ntp ', a command and
				a length (big-endian words), then its data.  The Newton
				sends 'cnnt' on connecting (answered 'okln'), 'rslt' (an
				error code) after each command, 'text' (what the REP
				prints), 'fobj' (NTKSend's object), 'fstk' (a stack trace),
				'eext'/'bext' (the break loop entered and left),
				'eerr'/'estr'/'eref' (an exception with its error, message
				or object), 'dpkg' (asking for a package, ntkDownload),
				'term' on leaving.  The desktop sends 'code' (an NSOF code
				block, run and its result sent back), 'lscb' (a code block
				for the REP), 'pkg ' (a package, installed on the default
				store), 'pkgX' (a package deleted by name), 'stou' (the
				timeout in seconds), 'term'.  Objects go as a length and
				NSOF (stores/ObjectStreamer.h).

				The plain-text listener (the connection's address given but
				not a string) uses PSerialInTranslator/PSerialOutTranslator
				instead: lines typed, the REP's text back.

				NOT YET: the AppleTalk (ADSP) connection and its NBP lookup
				(NubADSPLookup, NubADSPOptions), the Hammer debugger's
				translators.

				The field names are ours, their order the ROM's (offsets
				noted).

	Reconstructed from the MP2x00 US ROM (0x00129eb4-0x0012d174,
	0x001dd574-0x001dd720, 0x001de770-0x001de9f4, 0x002d3510); each
	function cites its origin.
*/

#ifndef __COMMS_NTK_H
#define __COMMS_NTK_H

#include "REPTranslators.h"
#include "TaskSafeRingBuffer.h"
#include "Endpoint.h"
#include "AppWorld.h"
#include "UserPorts.h"

class TOptionArray;
class TNTKEndpointClient;


/*------------------------------------------------------------------------------
	The translators
------------------------------------------------------------------------------*/

// what CreateNTKInTranslator/CreateNTKOutTranslator give Init
struct NTKTranslatorContext
{
	TTaskSafeRingBuffer*	fBuffer;			// +0x00
	ULong				fPause;					// +0x04
	ULong				fTimeout;				// +0x08
	ULong				fTextSize;				// +0x0c  (out) the text buffer
};

PROTOCOL PNTKInTranslator : public PInTranslator
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PNTKInTranslator);

	PNTKInTranslator*	New();
	void			Delete();

	long			Init(void* context);
	long			Idle();
	Boolean			FrameAvailable();
	Ref				ProduceFrame(int level);

	void			SetTimeout(ULong timeout);
	void			ReadHeader(ULong* word1, ULong* word2);
	void			ReadData(void* data, long size);
	NewtonErr		LoadPackage(void);

	ULong			fPause;					// +0x10
	ULong			fTimeout;				// +0x14
	TTaskSafeRingBuffer*	fBuffer;		// +0x18
	TTaskSafeRingPipe*	fPipe;				// +0x1c
	Boolean			fFrameAvailable;		// +0x20
};

PROTOCOL PNTKOutTranslator : public POutTranslator
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PNTKOutTranslator);

	PNTKOutTranslator*	New();
	void			Delete();

	long			Init(void* context);
	long			Idle();
	void			ConsumeFrame(RefArg obj, int depth, long indent);
	void			Flush();
	void			Prompt(int level);
	long			Print(const char* format, ...);
	int				Putc(int c);
	void			EnterBreakLoop(int level);
	void			ExitBreakLoop();
	void			StackTrace(void* interpreter);
	void			ExceptionNotify(Exception* exception);

	void			SetTimeout(ULong timeout);
	void			SendHeader(ULong word1, ULong word2);
	void			SendCommand(ULong command, ULong length);
	void			SendData(const void* data, long size);
	void			ConsumeFrameReally(RefArg obj);
	void			ConsumeExceptionFrame(RefArg obj, char* name);
	void			FlushText(void);

	ULong			fPause;					// +0x10
	ULong			fTimeout;				// +0x14
	TTaskSafeRingBuffer*	fBuffer;		// +0x18
	char*			fText;					// +0x1c  what is printed, sent a line at a time
	long			fTextSize;				// +0x20
	TTaskSafeRingPipe*	fPipe;				// +0x24
	char*			fTextPtr;				// +0x28
	long			fTextLeft;				// +0x2c
};

// what CreateSerialInTranslator/CreateSerialOutTranslator give Init
struct SerialTranslatorContext
{
	TTaskSafeRingBuffer*	fBuffer;			// +0x00
	long				fSize;					// +0x04  of a line
};

PROTOCOL PSerialInTranslator : public PInTranslator
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PSerialInTranslator);

	PSerialInTranslator*	New();
	void			Delete();

	long			Init(void* context);
	long			Idle();
	Boolean			FrameAvailable();
	Ref				ProduceFrame(int level);

	TTaskSafeRingBuffer*	fBuffer;		// +0x10
	char*			fLine;					// +0x14
	long			fSize;					// +0x18
};

PROTOCOL PSerialOutTranslator : public POutTranslator
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PSerialOutTranslator);

	PSerialOutTranslator*	New();
	void			Delete();

	long			Init(void* context);
	long			Idle();
	void			ConsumeFrame(RefArg obj, int depth, long indent);
	void			Flush();
	void			Prompt(int level);
	long			Print(const char* format, ...);
	int				Putc(int c);
	void			EnterBreakLoop(int level);
	void			ExitBreakLoop();
	void			StackTrace(void* interpreter);
	void			ExceptionNotify(Exception* exception);

	TTaskSafeRingBuffer*	fBuffer;		// +0x10
	char*			fText;					// +0x14
	long			fSize;					// +0x18
};

NewtonErr	CreateNTKInTranslator(PInTranslator** translator, char* name, TTaskSafeRingBuffer* buffer);
NewtonErr	CreateNTKOutTranslator(POutTranslator** translator, char* name, TTaskSafeRingBuffer* buffer);
NewtonErr	CreateSerialInTranslator(PInTranslator** translator, TTaskSafeRingBuffer* buffer);
NewtonErr	CreateSerialOutTranslator(POutTranslator** translator, TTaskSafeRingBuffer* buffer);


/*------------------------------------------------------------------------------
	The nub
------------------------------------------------------------------------------*/

class TNTKNub
{
public:
						TNTKNub();
						~TNTKNub();

	NewtonErr			Init(TOptionArray* openOptions, TOptionArray* bindOptions, TOptionArray* connectOptions,
							 char* inTranslator, char* outTranslator, Boolean serialListener);
	NewtonErr			StartListener(void);
	NewtonErr			StopListener(void);
	NewtonErr			DownloadPackage(void);
	NewtonErr			DoCommand(void);
	NewtonErr			HandleCodeBlock(ULong length);
	NewtonErr			DeletePackage(ULong length);
	NewtonErr			ReadCommand(ULong* command, ULong* length);
	NewtonErr			SendTextHeader(ULong length);
	NewtonErr			SendResult(NewtonErr result);
	NewtonErr			SendEOM(void);
	NewtonErr			SendRef(ULong command, RefArg obj);
	NewtonErr			EnterBreakLoop(int level);
	NewtonErr			ExitBreakLoop(void);
	NewtonErr			ExceptionNotify(Exception* exception);
	NewtonErr			SendExceptionHeader(ULong command);
	NewtonErr			SendExceptionData(char* name, RefArg data);
	NewtonErr			SendExceptionData(char* name, char* message);
	NewtonErr			SendExceptionData(char* name, long error);

	PInTranslator*		fSavedREPin;			// +0x00
	POutTranslator*		fSavedREPout;			// +0x04
	PInTranslator*		fInTranslator;			// +0x08
	POutTranslator*		fOutTranslator;			// +0x0c
	PNTKInTranslator*	fNTKIn;					// +0x10
	PNTKOutTranslator*	fNTKOut;				// +0x14
	TTaskSafeRingBuffer*	fInBuffer;			// +0x18  from the connection
	TTaskSafeRingBuffer*	fOutBuffer;			// +0x1c  to it
	TUPort				fTaskPort;				// +0x20  the 'ntk ' task's
	Boolean				fNTKProtocol;			// +0x28  (not the plain-text listener)
	Boolean				fConnected;				// +0x29
};

extern TNTKNub*		gNTKNub;					// 0x0c10155c

void		NTKInit(void);
void		NTKShutdown(NewtonErr error);
void		ResetREPIdler(void);
NewtonErr	NTKSendStackTrace(RefArg trace);
NewtonErr	NTKStackTrace(void* interpreter);
NewtonErr	CreateNub(RefArg connection, RefArg address, RefArg inTranslator, RefArg outTranslator);
NewtonErr	StringRefToHandle(RefArg string, char*** handle);

// the natives
Ref		FNTKListener(RefArg rcvr, RefArg on, RefArg connection, RefArg address, RefArg inTranslator, RefArg outTranslator);
Ref		FNTKDownload(RefArg rcvr, RefArg connection, RefArg address, RefArg inTranslator, RefArg outTranslator);
Ref		FNTKSend(RefArg rcvr, RefArg obj);
Ref		FNTKAlive(RefArg rcvr);
Ref		FSetupTetheredListener(RefArg rcvr, RefArg on, RefArg connection, RefArg address);
Ref		FpkgDownload(RefArg rcvr, RefArg connection, RefArg address);
void	RegisterNTKNatives(void);


/*------------------------------------------------------------------------------
	The REP's idler
------------------------------------------------------------------------------*/

class TREPEventHandler : public TAEventHandler
{
public:
	virtual void		IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);
};

extern TREPEventHandler*	gREPEventHandler;	// 0x0c101560


/*------------------------------------------------------------------------------
	The nub's task
------------------------------------------------------------------------------*/

class TNTKTask : public TAppWorld
{
public:
						TNTKTask();
	virtual				~TNTKTask();

	NewtonErr			InitNTK(TOptionArray* openOptions, TOptionArray* bindOptions, TOptionArray* connectOptions,
								TTaskSafeRingBuffer* inBuffer, TTaskSafeRingBuffer* outBuffer, long sendSize, long receiveSize);

	virtual ULong		GetSizeOf();
	virtual long		MainConstructor();
	virtual void		MainDestructor();
	virtual long		PreMain();
	virtual void		PostMain();

	TTaskSafeRingBuffer*	fOutBuffer;			// +0x70
	TTaskSafeRingBuffer*	fInBuffer;			// +0x74
	long				fSendSize;				// +0x78
	long				fReceiveSize;			// +0x7c
	TOptionArray*		fOpenOptions;			// +0x80
	TOptionArray*		fBindOptions;			// +0x84
	TOptionArray*		fConnectOptions;		// +0x88
};


class TNTKEndpointClient : public TEndpointClient
{
public:
						TNTKEndpointClient();
	virtual				~TNTKEndpointClient();

	NewtonErr			Init(TOptionArray* openOptions, TOptionArray* bindOptions, TOptionArray* connectOptions,
							 TTaskSafeRingBuffer* inBuffer, TTaskSafeRingBuffer* outBuffer, long sendSize, long receiveSize);
	void				CheckSend(void);
	void				MakeYourPeace(void);

	virtual void		IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);
	virtual void		Disconnect(TEndpointEvent* event);
	virtual void		SndComplete(TEndpointEvent* event);
	virtual void		RcvComplete(TEndpointEvent* event);
	virtual void		ConnectComplete(TEndpointEvent* event);
	virtual void		DisconnectComplete(TEndpointEvent* event);
	virtual void		BindComplete(TEndpointEvent* event);
	virtual void		UnBindComplete(TEndpointEvent* event);
	virtual void		AbortComplete(TEndpointEvent* event);

	TTaskSafeRingBuffer*	fOutBuffer;			// +0x18  sent from
	TTaskSafeRingBuffer*	fInBuffer;			// +0x1c  received into
	UByte*				fSendBuffer;			// +0x20
	UByte*				fReceiveBuffer;			// +0x24
	long				fSendSize;				// +0x28
	long				fReceiveSize;			// +0x2c
	ULong				fPause;					// +0x30
	TOptionArray*		fOpenOptions;			// +0x34
	TOptionArray*		fBindOptions;			// +0x38
	TOptionArray*		fConnectOptions;		// +0x3c
	Boolean				fSending;				// +0x40
	Boolean				fDying;					// +0x41
};


// the kill event the nub sends the task, and its handler there
class TKillEvent : public TAEvent
{
public:
						TKillEvent();
};

class TKillEventHandler : public TAEventHandler
{
public:
						TKillEventHandler(TNTKEndpointClient* client);

	NewtonErr			Init(void);
	virtual void		AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);

	TNTKEndpointClient*	fClient;				// +0x14
};

#endif	/* __COMMS_NTK_H */
