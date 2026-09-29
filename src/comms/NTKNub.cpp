/*
	File:		comms/NTKNub.cpp

	Contains:	The NTK's debugger nub (TNTKNub), its task and endpoint
				client (TNTKTask, TNTKEndpointClient), the REP's idler, and
				the natives ntkListener, ntkDownload, NTKSend, NTKAlive,
				ntpTetheredListener and ntpDownloadPackage - NTK.h.

	Reconstructed from the MP2x00 US ROM (0x00129eb4, 0x0012a890,
	0x0012aa84-0x0012d028, 0x002d3510); each function cites its origin.
*/

#include "NTK.h"
#include "CommManager.h"
#include "EzEndpointPipe.h"
#include "Options.h"
#include "PackageManager.h"
#include "Application.h"
#include "View.h"
#include "RootView.h"
#include "CommErrors.h"
#include "Ports.h"
#include "NewtWorld.h"
#include "DebugAPI.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "NewtErrors.h"
#include "UserTasks.h"

#include <string.h>

extern const ExceptionName exMsgException;
// (the host has no exRefException object to link to)
static const ExceptionName exRefExceptionName = (ExceptionName) "type.ref";		// ROM 0x00380880 exRefException

#define kNTKErrBadCommand		(-28016)		// not a command the nub knows (0xffff9290)
#define kNTKErrQuiet			(-36006)		// ends the connection without a notification
#define kNTKErrDisconnected		(-16013)		// the endpoint disconnected

#define kNTKBufferSize			0x200			// each ring buffer, and the task's send and receive blocks


TNTKNub*			gNTKNub = nil;				// 0x0c10155c
TREPEventHandler*	gREPEventHandler = nil;		// 0x0c101560


// the protocol's words are big-endian
static inline ULong
GetBEWord(const UByte* bytes)
{
	return ((ULong) bytes[0] << 24) | ((ULong) bytes[1] << 16) | ((ULong) bytes[2] << 8) | bytes[3];
}

static inline void
PutBEWord(UByte* bytes, ULong word)
{
	bytes[0] = word >> 24; bytes[1] = word >> 16; bytes[2] = word >> 8; bytes[3] = word;
}

// a word of data (not a header's)
static void
SendDataWord(PNTKOutTranslator* out, ULong word)
{
	UByte bytes[4];
	PutBEWord(bytes, word);
	out->SendData(bytes, 4);
}

// what an exception carries, as the nub's functions answer it
#define CaughtError()	((NewtonErr) (Long) CurrentException()->data)


/*------------------------------------------------------------------------------
	The REP's idler
------------------------------------------------------------------------------*/

// ROM 0x00129eb4 IdleProc__16TREPEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The REP run in the global port, the deferred actions done, and the
// idler set again for when the translators next want it.
void
TREPEventHandler::IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	IncrementCurrentStackPos();
	GrafPort* oldPort;
	GetPort(&oldPort);
	SetPort(&gGrafPort);
	REPIdle();
	CheckForDeferredActions();
	ResetREPIdler();
	SetPort(oldPort);
	DecrementCurrentStackPos();
	ClearRefHandles();
}


// ROM 0x0012a890 ResetREPIdler__Fv
// The idler set for when the translators want idling (stopped if never).
void
ResetREPIdler(void)
{
	long time = REPTime();
	if (time != 0)
		gREPEventHandler->ResetIdle((TTimeout) time);
	else
		gREPEventHandler->StopIdle();
}


// ROM 0x0012b198 NTKInit__Fv
// The translators' class infos in the registry and the REP's idler made.
void
NTKInit(void)
{
	// NOT YET RECONSTRUCTED: PHammerInTranslator, PHammerOutTranslator
	// (the ROM registers them first)
	gProtocolRegistry->Register(PNullInTranslator::ClassInfo(), 0);
	gProtocolRegistry->Register(PNullOutTranslator::ClassInfo(), 0);
	// ROM BUG: PStdioInTranslator is registered twice and
	// PStdioOutTranslator never
	gProtocolRegistry->Register(PStdioInTranslator::ClassInfo(), 0);
	gProtocolRegistry->Register(PStdioInTranslator::ClassInfo(), 0);
	gProtocolRegistry->Register(PSerialInTranslator::ClassInfo(), 0);
	gProtocolRegistry->Register(PSerialOutTranslator::ClassInfo(), 0);
	gProtocolRegistry->Register(PNTKInTranslator::ClassInfo(), 0);
	gProtocolRegistry->Register(PNTKOutTranslator::ClassInfo(), 0);
	gREPEventHandler = new TREPEventHandler;
	gREPEventHandler->Init('rep ', 'newt');
	gREPEventHandler->InitIdler((TTimeout) 0, 0, false);
}


/*------------------------------------------------------------------------------
	TNTKNub
------------------------------------------------------------------------------*/

// ROM 0x0012aa84 __ct__7TNTKNubFv
TNTKNub::TNTKNub()
{
	fSavedREPin = nil;
	fSavedREPout = nil;
	fInTranslator = nil;
	fOutTranslator = nil;
	fNTKIn = nil;
	fNTKOut = nil;
	fNTKProtocol = false;
	fConnected = false;
	fInBuffer = nil;
	fOutBuffer = nil;
}


// ROM 0x0012aae0 __dt__7TNTKNubFv
// The REP's translators put back, the task told to finish, and the
// translators and the buffers disposed of.
TNTKNub::~TNTKNub()
{
	if (fSavedREPin != nil)
		gREPin = fSavedREPin;
	if (fSavedREPout != nil)
		gREPout = fSavedREPout;
	ResetREPIdler();
	if (fTaskPort != 0)
	{
		TKillEvent kill;
		fTaskPort.Send(&kill, sizeof(kill));
		fTaskPort.CopyObject(0);
	}
	if (fInTranslator != nil)
		fInTranslator->Delete();
	if (fOutTranslator != nil)
		fOutTranslator->Delete();
	// ROM BUG: the task may still have events in hand that write to these
	// (it is only told to finish, not waited for)
	if (fInBuffer != nil)
		delete fInBuffer;
	if (fOutBuffer != nil)
		delete fOutBuffer;
}


// ROM 0x0012abf8 Init__7TNTKNubFP12TOptionArrayN21PcT4Uc
// The two buffers, the task that fills and empties them over the
// connection, and the translators: the NTK's (by name, the NTK's own
// unless given) or, for the plain-text listener, the serial ones.
NewtonErr
TNTKNub::Init(TOptionArray* openOptions, TOptionArray* bindOptions, TOptionArray* connectOptions,
			  char* inTranslator, char* outTranslator, Boolean serialListener)
{
	NewtonErr err;
	fInBuffer = new TTaskSafeRingBuffer;
	if (fInBuffer == nil)
		return MemError();
	if ((err = fInBuffer->Init(kNTKBufferSize, false)) != noErr)
		return err;
	fOutBuffer = new TTaskSafeRingBuffer;
	if (fOutBuffer == nil)
		return MemError();
	if ((err = fOutBuffer->Init(kNTKBufferSize, false)) != noErr)
		return err;
	{
		TNTKTask task;
		err = task.InitNTK(openOptions, bindOptions, connectOptions, fInBuffer, fOutBuffer, kNTKBufferSize, kNTKBufferSize);
		if (err != noErr)
			return err;
	}
	if ((err = GetOSPortFromName('ntk ', &fTaskPort)) != noErr)
		return err;
	if (serialListener)
	{
		fNTKProtocol = false;
		if ((err = CreateSerialInTranslator(&fInTranslator, fInBuffer)) != noErr)
			return err;
		return CreateSerialOutTranslator(&fOutTranslator, fOutBuffer);
	}
	if ((err = CreateNTKInTranslator(&fInTranslator, inTranslator != nil ? inTranslator : (char*) "PNTKInTranslator", fInBuffer)) != noErr)
		return err;
	if ((err = CreateNTKOutTranslator(&fOutTranslator, outTranslator != nil ? outTranslator : (char*) "PNTKOutTranslator", fOutBuffer)) != noErr)
		return err;
	fNTKProtocol = true;
	fNTKIn = (PNTKInTranslator*) fInTranslator;
	fNTKOut = (PNTKOutTranslator*) fOutTranslator;
	return noErr;
}


// ROM 0x0012adbc StartListener__7TNTKNubFv
// The nub's translators in the REP's place; the NTK's connection opened
// with 'cnnt', which the desktop answers 'okln'.
NewtonErr
TNTKNub::StartListener(void)
{
	NewtonErr err = noErr;
	fSavedREPin = gREPin;
	fSavedREPout = gREPout;
	gREPin = fInTranslator;
	gREPout = fOutTranslator;
	ResetREPIdler();
	if (fNTKProtocol)
	{
		newton_try
		{
			ULong command, length;
			fNTKOut->SendHeader('newt', 'ntp ');
			fNTKOut->SendCommand('cnnt', 0);
			fNTKOut->Flush();
			err = ReadCommand(&command, &length);
			if (err == noErr)
			{
				if (command == 'okln' && length == 0)
					fConnected = true;
				else
					err = kNTKErrBadCommand;
			}
		}
		newton_catch_all
		{
			err = CaughtError();
		}
		end_try;
	}
	return err;
}


// ROM 0x0012aed8 StopListener__7TNTKNubFv
// 'term' to the desktop, and the Toolkit application's state forgotten.
NewtonErr
TNTKNub::StopListener(void)
{
	NewtonErr err = noErr;
	if (fNTKProtocol && fConnected)
	{
		newton_try
		{
			fNTKOut->SendHeader('newt', 'ntp ');
			fNTKOut->SendCommand('term', 0);
			fNTKOut->Flush();
		}
		newton_catch_all
		{
			err = CaughtError();
		}
		end_try;
	}
	RefVar ntkApp(GetFrameSlotRef(gRootView->fContext, Intern((char*) "newtoolspro")));
	if ((Ref) ntkApp != NILREF)
		SetFrameSlot(ntkApp, RefVar(Intern((char*) "ntpstate")), RefVar(NILREF));
	return err;
}


// ROM 0x0012b028 DownloadPackage__7TNTKNubFv
// 'dpkg' asks for a package; the desktop may delete others and set the
// timeout first.  The result is sent back.
NewtonErr
TNTKNub::DownloadPackage(void)
{
	NewtonErr err = noErr;
	newton_try
	{
		Boolean done = false;
		ULong command, length;
		fNTKOut->SendHeader('newt', 'ntp ');
		fNTKOut->SendCommand('dpkg', 0);
		fNTKOut->Flush();
		do
		{
			if ((err = ReadCommand(&command, &length)) != noErr)
				break;
			if (command == 'pkg ')
			{
				err = fNTKIn->LoadPackage();
				done = true;
			}
			else if (command == 'pkgX')
				err = DeletePackage(length);
			else if (command == 'stou')
			{
				UByte bytes[4];
				fNTKIn->ReadData(bytes, 4);
				ULong seconds = GetBEWord(bytes);
				fNTKIn->SetTimeout(seconds * kSeconds);
				fNTKOut->SetTimeout(seconds * kSeconds);
			}
			else
			{
				err = kNTKErrBadCommand;
				break;
			}
		} while (err == noErr && !done);
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	SendResult(err);
	return err;
}


// ROM 0x0012b2d4 DoCommand__7TNTKNubFv
// One of the desktop's commands: 1 when it is a code block for the REP
// (the in translator then has a frame), -1 when the desktop has gone.
NewtonErr
TNTKNub::DoCommand(void)
{
	ULong command, length;
	NewtonErr err = ReadCommand(&command, &length);
	if (err != noErr)
		return err;
	switch (command)
	{
	case 'pkgX':
		err = DeletePackage(length);
		SendResult(err);
		break;
	case 'code':
		err = HandleCodeBlock(length);
		break;
	case 'lscb':
		err = 1;
		break;
	case 'pkg ':
		err = fNTKIn->LoadPackage();
		SendResult(err);
		break;
	case 'stou':
		{
			UByte bytes[4];
			fNTKIn->ReadData(bytes, 4);
			ULong seconds = GetBEWord(bytes);
			fNTKIn->SetTimeout(seconds * kSeconds);
			fNTKOut->SetTimeout(seconds * kSeconds);
			SendResult(noErr);
		}
		break;
	case 'term':
		err = -1;
		fConnected = false;
		break;
	default:
		err = kNTKErrBadCommand;
		SendResult(err);
		break;
	}
	return err;
}


// ROM 0x0012b428 HandleCodeBlock__7TNTKNubFUl
// A code block run in the REP's context and its result sent back as
// 'code'.
NewtonErr
TNTKNub::HandleCodeBlock(ULong length)
{
	NewtonErr err = noErr;
	RefVar result(NILREF);
	RefVar block(NILREF);
	newton_try
	{
		UByte size[4];
		fNTKIn->ReadData(size, 4);				// (the object's size: not needed)
		block = fNTKIn->ProduceFrame(0);
		result = InterpretBlock(block, RefVar(gREPContext));
		fNTKOut->SendHeader('newt', 'ntp ');
		// ROM QUIRK: the answer's length is the command's, not the result's
		fNTKOut->SendCommand('code', length);
		fNTKOut->ConsumeFrameReally(result);
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012b53c DeletePackage__7TNTKNubFUl
// The package of the name that follows removed.
NewtonErr
TNTKNub::DeletePackage(ULong length)
{
	// ROM BUG: the name's block is never freed
	UniChar* name = (UniChar*) NewPtr(length);
	fNTKIn->ReadData(name, length);
	// the name's UniChars are big-endian on the connection
	for (ULong i = 0; i < length / sizeof(UniChar); i++)
	{
		UByte* c = (UByte*) &name[i];
		name[i] = (UniChar) ((c[0] << 8) | c[1]);
	}
	TPMIterator iter;
	iter.Init();
	while (iter.More())
	{
		if (Ustrcmp(iter.PackageName(), name) == 0)
		{
			NSCallGlobalFn(RefVar(RSSYMremovepackage), RefVar(MAKEINT(iter.PackageId())));
			break;
		}
		iter.NextPackage();
	}
	iter.Done();
	return noErr;
}


// ROM 0x0012b618 ReadCommand__7TNTKNubFPUlT1
// A message's header ('newt' 'ntp '), its command and its length.
NewtonErr
TNTKNub::ReadCommand(ULong* command, ULong* length)
{
	NewtonErr err = noErr;
	*command = 0;
	*length = 0;
	newton_try
	{
		ULong word1, word2;
		fNTKIn->ReadHeader(&word1, &word2);
		if (word1 == 'newt' && word2 == 'ntp ')
			fNTKIn->ReadHeader(command, length);
		else
			err = kNTKErrBadCommand;
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012b6c8 SendTextHeader__7TNTKNubFUl
NewtonErr
TNTKNub::SendTextHeader(ULong length)
{
	NewtonErr err = noErr;
	newton_try
	{
		fNTKOut->SendHeader('newt', 'ntp ');
		fNTKOut->SendCommand('text', length);
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012b748 SendResult__7TNTKNubFl
NewtonErr
TNTKNub::SendResult(NewtonErr result)
{
	NewtonErr err = noErr;
	newton_try
	{
		fNTKOut->SendHeader('newt', 'ntp ');
		fNTKOut->SendCommand('rslt', 4);
		SendDataWord(fNTKOut, result);
		fNTKOut->Flush();
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012b7e4 SendEOM__7TNTKNubFv
NewtonErr
TNTKNub::SendEOM(void)
{
	NewtonErr err = noErr;
	newton_try
	{
		fNTKOut->SendHeader('newt', 'ntp ');
		fNTKOut->SendCommand('teom', 0);
		fNTKOut->Flush();
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012b864 SendRef__7TNTKNubFUlRC6RefVar
// An object: the command, then the object's size and its NSOF (the size
// standing where a length would).
NewtonErr
TNTKNub::SendRef(ULong command, RefArg obj)
{
	NewtonErr err = noErr;
	newton_try
	{
		fNTKOut->SendHeader('newt', 'ntp ');
		SendDataWord(fNTKOut, command);
		fNTKOut->ConsumeFrameReally(obj);
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012b8ec EnterBreakLoop__7TNTKNubFi
NewtonErr
TNTKNub::EnterBreakLoop(int level)
{
	NewtonErr err = noErr;
	newton_try
	{
		fNTKOut->SendHeader('newt', 'ntp ');
		fNTKOut->SendCommand('eext', 0);
		fNTKOut->Flush();
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012b96c ExitBreakLoop__7TNTKNubFv
NewtonErr
TNTKNub::ExitBreakLoop(void)
{
	NewtonErr err = noErr;
	newton_try
	{
		fNTKOut->SendHeader('newt', 'ntp ');
		fNTKOut->SendCommand('bext', 0);
		fNTKOut->Flush();
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012ba50 ExceptionNotify__7TNTKNubFP9Exception
// A message exception with its message, a ref exception with its object,
// anything else with its error.
NewtonErr
TNTKNub::ExceptionNotify(Exception* exception)
{
	char* name = (char*) exception->name;
	if (Subexception(exception->name, exMsgException))
		return SendExceptionData(name, (char*) exception->data);
	if (Subexception(exception->name, exRefExceptionName))
	{
		RefVar data(**(RefStruct**) &exception->data);
		return SendExceptionData(name, data);
	}
	return SendExceptionData(name, (long) (Long) exception->data);
}


// ROM 0x0012bac0 SendExceptionHeader__7TNTKNubFUl
// (no length: the data says how long it is)
NewtonErr
TNTKNub::SendExceptionHeader(ULong command)
{
	NewtonErr err = noErr;
	newton_try
	{
		fNTKOut->SendHeader('newt', 'ntp ');
		SendDataWord(fNTKOut, command);
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012bb38 SendExceptionData__7TNTKNubFPcRC6RefVar
// 'eref': the name and the object (PNTKOutTranslator::ConsumeExceptionFrame).
NewtonErr
TNTKNub::SendExceptionData(char* name, RefArg data)
{
	NewtonErr err = SendExceptionHeader('eref');
	if (err != noErr)
		return err;
	newton_try
	{
		fNTKOut->ConsumeExceptionFrame(data, name);
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012bbb0 SendExceptionData__7TNTKNubFPcT1
// 'estr': the whole length, the name's and the name, the message's and
// the message (each with its terminator).
NewtonErr
TNTKNub::SendExceptionData(char* name, char* message)
{
	NewtonErr err = SendExceptionHeader('estr');
	if (err != noErr)
		return err;
	newton_try
	{
		long nameLength = strlen(name) + 1;
		long messageLength = strlen(message) + 1;
		SendDataWord(fNTKOut, nameLength + messageLength);
		SendDataWord(fNTKOut, nameLength);
		fNTKOut->SendData(name, nameLength);
		SendDataWord(fNTKOut, messageLength);
		fNTKOut->SendData(message, messageLength);
		fNTKOut->Flush();
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


// ROM 0x0012bca4 SendExceptionData__7TNTKNubFPcl
// 'eerr': the whole length, the name's and the name, the error.
NewtonErr
TNTKNub::SendExceptionData(char* name, long error)
{
	NewtonErr err = SendExceptionHeader('eerr');
	if (err != noErr)
		return err;
	newton_try
	{
		long nameLength = strlen(name) + 1;
		SendDataWord(fNTKOut, nameLength + 4);
		SendDataWord(fNTKOut, nameLength);
		fNTKOut->SendData(name, nameLength);
		SendDataWord(fNTKOut, error);
		fNTKOut->Flush();
	}
	newton_catch_all
	{
		err = CaughtError();
	}
	end_try;
	return err;
}


/*------------------------------------------------------------------------------
	The connection's life
------------------------------------------------------------------------------*/

// ROM 0x0012b9ec NTKShutdown__Fl
// The connection ended (the desktop told), and an error reported unless it
// is one that means the connection simply went.
void
NTKShutdown(NewtonErr error)
{
	if (gNTKNub == nil)
		return;
	gNTKNub->StopListener();
	if (gNTKNub != nil)
		delete gNTKNub;
	gNTKNub = nil;
	if (error != noErr && error != -1 && error != kNTKErrQuiet)
		ErrorNotify(error, 3);
}


// ROM 0x0012bd80 StringRefToHandle__FRC6RefVarPPPc
// A string's characters copied into a handle (nil for nil).
NewtonErr
StringRefToHandle(RefArg string, char*** handle)
{
	NewtonErr err = noErr;
	if (handle == nil)
		return -1;
	if ((Ref) string == NILREF)
	{
		*handle = nil;
		return noErr;
	}
	LockRef(string);
	UniChar* chars = GetCString(string);
	*handle = (char**) NewHandle(2 + Ustrlen(chars) * 2);
	if (*handle != nil)
	{
		HLock((Handle) *handle);
		Ustrcpy((UniChar*) **handle, chars);
		HUnlock((Handle) *handle);
		SetHandleName((Handle) *handle, 'ustr');
	}
	else
		err = MemError();
	UnlockRef(string);
	return err;
}


// ROM 0x0012be34 NTKSendStackTrace__FRC6RefVar
NewtonErr
NTKSendStackTrace(RefArg trace)
{
	return gNTKNub->SendRef('fstk', trace);
}


// ROM 0x002d3510 NTKStackTrace__FPv
// The interpreter's stack frames, innermost first, as an array of
// NTKStackFrameInfo frames sent as 'fstk'.
NewtonErr
NTKStackTrace(void* interpreter)
{
	TNSDebugAPI api((TInterpreter*) interpreter);
	long count = api.NumStackFrames();
	RefVar trace(AllocateArray(RSSYMarray, count));
	for (long i = 0, slot = count - 1; i < count; i++, slot--)
		SetArraySlotRef(trace, slot, NTKStackFrameInfo(api, i));
	return NTKSendStackTrace(trace);
}


// ROM 0x0012bfa8 CreateNub__FRC6RefVarN31
// gNTKNub made over a connection: a connection type (0 serial, 3 MNP
// serial; 1 and 2 AppleTalk) or an options frame, an address (a string
// for AppleTalk; otherwise non-nil for the plain-text listener) and the
// translators' names.
NewtonErr
CreateNub(RefArg connection, RefArg address, RefArg inTranslator, RefArg outTranslator)
{
	NewtonErr err = noErr;
	char inName[64];
	char outName[64];
	char* inTranslatorName = nil;
	char* outTranslatorName = nil;
	TOptionArray* openOptions = nil;
	TOptionArray* bindOptions = nil;
	TOptionArray* connectOptions = nil;
	Boolean serialListener = false;

	gNTKNub = new TNTKNub;
	if (gNTKNub == nil)
		return MemError();

	if ((Ref) inTranslator != NILREF && (Ref) outTranslator != NILREF)
	{
		// ROM BUG: 0x3f (the names' room) is passed as the encoding and
		// 0x7fffffff as the room, so the conversion reads past the
		// encodings' table.  DEVIATION: the host converts as Mac Roman
		// into the 63 characters there are room for.
		ConvertFromUnicode(GetCString(inTranslator), inName, kMacRomanEncoding, sizeof(inName) - 1);
		ConvertFromUnicode(GetCString(outTranslator), outName, kMacRomanEncoding, sizeof(outName) - 1);
		inTranslatorName = inName;
		outTranslatorName = outName;
	}

	if (ISINT(connection))
	{
		openOptions = new TOptionArray;
		if (openOptions == nil)
			{ err = MemError(); goto failed; }
		if ((err = openOptions->Init()) != noErr)
			goto failed;
		switch (RINT(connection))
		{
		case 0:
			if ((err = EzSerialOptions(openOptions, nil, 0, 0)) != noErr)
				goto failed;
			break;
		case 1:
		case 2:
			// NOT YET RECONSTRUCTED: the AppleTalk (ADSP) connection,
			// NubADSPOptions (0x0012c0a8); type 2 is the plain-text listener
			// over it
			connectOptions = new TOptionArray;
			if (connectOptions == nil)
				{ err = MemError(); goto failed; }
			if ((err = connectOptions->Init()) != noErr)
				goto failed;
			err = kCommErrNotSupported;
			goto failed;
		case 3:
			if ((err = EzMNPSerialOptions(openOptions, nil)) != noErr)
				goto failed;
			connectOptions = new TOptionArray;
			if (connectOptions == nil)
				{ err = MemError(); goto failed; }
			if ((err = connectOptions->Init()) != noErr)
				goto failed;
			// ROM QUIRK: the connect options go into the open array, so the
			// connect array stays empty
			if ((err = EzMNPConnectOptions(openOptions, nil)) != noErr)
				goto failed;
			break;
		}
	}
	else
		EzConvertOptions(connection, &openOptions, &bindOptions, &connectOptions);

	if (IsString(address))
	{
		// NOT YET RECONSTRUCTED: the AppleTalk address looked up by name
		// (NubADSPLookup 0x0012be50) and put in the connect options
		err = kCommErrNotSupported;
		goto failed;
	}
	serialListener = ((Ref) address != NILREF);
	err = gNTKNub->Init(openOptions, bindOptions, connectOptions, inTranslatorName, outTranslatorName, serialListener);

failed:
	if (err != noErr)
	{
		if (openOptions != nil)
			delete openOptions;
		if (bindOptions != nil)
			delete bindOptions;
		if (connectOptions != nil)
			delete connectOptions;
	}
	return err;
}


/*------------------------------------------------------------------------------
	The natives
------------------------------------------------------------------------------*/

// ROM 0x0012c318 FNTKListener
// ntkListener(on, connection, address, inTranslator, outTranslator): the
// nub made and the connection opened, or (on nil) closed.
Ref
FNTKListener(RefArg rcvr, RefArg on, RefArg connection, RefArg address, RefArg inTranslator, RefArg outTranslator)
{
	NewtonErr err;
	if ((Ref) on == NILREF)
	{
		if (gNTKNub == nil)
			return MAKEINT(-1);
		err = gNTKNub->StopListener();
		if (err != noErr && err != kNTKErrQuiet)
			ErrorNotify(err, 3);
		if (gNTKNub != nil)
			delete gNTKNub;
		gNTKNub = nil;
	}
	else
	{
		if (gNTKNub != nil)
			return MAKEINT(-1);
		err = CreateNub(connection, address, inTranslator, outTranslator);
		if (err == noErr)
			err = gNTKNub->StartListener();
		if (err != noErr)
		{
			if (gNTKNub != nil)
				delete gNTKNub;
			gNTKNub = nil;
		}
	}
	return MAKEINT(err);
}


// ROM 0x0012c3dc FNTKDownload
// ntkDownload(connection, address, inTranslator, outTranslator): a nub
// made for as long as it takes to ask for a package and load it.
Ref
FNTKDownload(RefArg rcvr, RefArg connection, RefArg address, RefArg inTranslator, RefArg outTranslator)
{
	NewtonErr err;
	if (gNTKNub != nil)
		err = -1;
	else
	{
		err = CreateNub(connection, address, inTranslator, outTranslator);
		if (err == noErr)
			err = gNTKNub->DownloadPackage();
	}
	Sleep(100 * kMilliseconds);
	// ROM BUG: a listener's nub is deleted too when there already was one
	// (it is not stopped first)
	if (gNTKNub != nil)
		delete gNTKNub;
	gNTKNub = nil;
	return MAKEINT(err);
}


// ROM 0x0012c45c FNTKSend
// NTKSend(obj): the object to the desktop as 'fobj'.
Ref
FNTKSend(RefArg rcvr, RefArg obj)
{
	if (gNTKNub == nil)
		return MAKEINT(-1);
	return MAKEINT(gNTKNub->SendRef('fobj', obj));
}


// ROM 0x0012c498 FNTKAlive
Ref
FNTKAlive(RefArg rcvr)
{
	return (gNTKNub != nil) ? TRUEREF : NILREF;
}


// ROM 0x0012c4b4 FSetupTetheredListener
// ntpTetheredListener(on, connection, address): ntkListener with the
// NTK's translators.
Ref
FSetupTetheredListener(RefArg rcvr, RefArg on, RefArg connection, RefArg address)
{
	return FNTKListener(rcvr, on, connection, address, RefVar(NILREF), RefVar(NILREF));
}


// ROM 0x0012c530 FpkgDownload
// ntpDownloadPackage(connection, address)
Ref
FpkgDownload(RefArg rcvr, RefArg connection, RefArg address)
{
	return FNTKDownload(rcvr, connection, address, RefVar(NILREF), RefVar(NILREF));
}


void
RegisterNTKNatives(void)
{
	RegisterNativeFunction("FNTKListener", (void*) FNTKListener, 5);
	RegisterNativeFunction("FNTKDownload", (void*) FNTKDownload, 4);
	RegisterNativeFunction("FNTKSend", (void*) FNTKSend, 1);
	RegisterNativeFunction("FNTKAlive", (void*) FNTKAlive, 0);
	RegisterNativeFunction("FSetupTetheredListener", (void*) FSetupTetheredListener, 3);
	RegisterNativeFunction("FpkgDownload", (void*) FpkgDownload, 2);
}


/*------------------------------------------------------------------------------
	The kill event
------------------------------------------------------------------------------*/

// ROM 0x0012c604 __ct__10TKillEventFv
TKillEvent::TKillEvent()
{
	fAEventClass = 'ntk ';
	fAEventID = 'kill';
}


// ROM 0x0012c5a0 __ct__17TKillEventHandlerFP18TNTKEndpointClient
TKillEventHandler::TKillEventHandler(TNTKEndpointClient* client)
{
	fClient = client;
}


// ROM 0x0012c5e8 Init__17TKillEventHandlerFv
NewtonErr
TKillEventHandler::Init(void)
{
	return TAEventHandler::Init('kill', 'ntk ');
}


// ROM 0x0012c5fc AEHandlerProc__17TKillEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TKillEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	fClient->MakeYourPeace();
}


/*------------------------------------------------------------------------------
	TNTKTask
------------------------------------------------------------------------------*/

// ROM 0x0012c6dc __ct__8TNTKTaskFv
TNTKTask::TNTKTask()
{
	fOutBuffer = nil;
	fInBuffer = nil;
	fSendSize = 0;
	fReceiveSize = 0;
	fOpenOptions = nil;
	fBindOptions = nil;
	fConnectOptions = nil;
}


// ROM 0x0012c73c __dt__8TNTKTaskFv
TNTKTask::~TNTKTask()
{ }


// ROM 0x0012c77c InitNTK__8TNTKTaskFP12TOptionArrayN21P19TTaskSafeRingBufferT4lT6
// The task ('ntk ', its name registered) started.
NewtonErr
TNTKTask::InitNTK(TOptionArray* openOptions, TOptionArray* bindOptions, TOptionArray* connectOptions,
				  TTaskSafeRingBuffer* inBuffer, TTaskSafeRingBuffer* outBuffer, long sendSize, long receiveSize)
{
	fOpenOptions = openOptions;
	fBindOptions = bindOptions;
	fConnectOptions = connectOptions;
	fReceiveSize = receiveSize;
	fOutBuffer = outBuffer;
	fInBuffer = inBuffer;
	fSendSize = sendSize;
	return TAppWorld::Init('ntk ', true, 0x1770);
}


// ROM 0x0012c7c0 MainConstructor__8TNTKTaskFv
long
TNTKTask::MainConstructor()
{
	EnableForking(false);
	return TAppWorld::MainConstructor();
}


// ROM 0x0012c7e4 MainDestructor__8TNTKTaskFv
void
TNTKTask::MainDestructor()
{
	TAppWorld::MainDestructor();
}


// ROM 0x0012c7e8 GetSizeOf__8TNTKTaskFv
ULong
TNTKTask::GetSizeOf()
{
	return sizeof(TNTKTask);
}


// ROM 0x0012c7f0 PreMain__8TNTKTaskFv
// The endpoint client and the handler of the nub's kill event.
long
TNTKTask::PreMain()
{
	NewtonErr err;
	TKillEventHandler* killHandler = nil;
	TNTKEndpointClient* client = new TNTKEndpointClient;
	if (client == nil || (killHandler = new TKillEventHandler(client)) == nil)
		err = MemError();
	else
	{
		killHandler->Init();
		err = client->Init(fOpenOptions, fBindOptions, fConnectOptions, fInBuffer, fOutBuffer, fSendSize, fReceiveSize);
		if (err == noErr)
			return noErr;
	}
	if (err != noErr)
	{
		if (killHandler != nil)
			delete killHandler;
		if (client != nil)
			delete client;
	}
	return err;
}


// ROM 0x0012c8a0 PostMain__8TNTKTaskFv
void
TNTKTask::PostMain()
{ }


/*------------------------------------------------------------------------------
	TNTKEndpointClient
------------------------------------------------------------------------------*/

// ROM 0x0012c8a4 __ct__18TNTKEndpointClientFv
TNTKEndpointClient::TNTKEndpointClient()
{
	fOutBuffer = nil;
	fInBuffer = nil;
	fSendBuffer = nil;
	fReceiveBuffer = nil;
	fPause = 50 * kMilliseconds;
	fSending = false;
	fDying = false;
	fOpenOptions = nil;
	fBindOptions = nil;
	fConnectOptions = nil;
}


// ROM 0x0012c918 __dt__18TNTKEndpointClientFv
TNTKEndpointClient::~TNTKEndpointClient()
{
	// DEVIATION: the ROM gives the endpoint's block back with operator
	// delete, its destructor not run; the host's protocol instances are
	// NewPtr'd, so it goes back to the pointer heap the same way
	if (fEndpoint != nil)
		DisposPtr((Ptr) fEndpoint);
	free(fSendBuffer);
	free(fReceiveBuffer);
	if (fOpenOptions != nil)
		delete fOpenOptions;
	if (fBindOptions != nil)
		delete fBindOptions;
	if (fConnectOptions != nil)
		delete fConnectOptions;
}


// ROM 0x0012ca5c Init__18TNTKEndpointClientFP12TOptionArrayN21P19TTaskSafeRingBufferT4lT6
// The send and receive blocks, the endpoint (asynchronous) and its bind
// begun; a failure is signalled to both buffers.
NewtonErr
TNTKEndpointClient::Init(TOptionArray* openOptions, TOptionArray* bindOptions, TOptionArray* connectOptions,
						 TTaskSafeRingBuffer* inBuffer, TTaskSafeRingBuffer* outBuffer, long sendSize, long receiveSize)
{
	NewtonErr err;
	TEndpoint* endpoint = nil;
	fReceiveSize = receiveSize;
	fSendSize = sendSize;
	fOpenOptions = openOptions;
	fBindOptions = bindOptions;
	fConnectOptions = connectOptions;
	fInBuffer = inBuffer;
	fOutBuffer = outBuffer;
	if ((fSendBuffer = (UByte*) malloc(fSendSize)) == nil
	||  (fReceiveBuffer = (UByte*) malloc(fReceiveSize)) == nil)
		err = MemError();
	else if ((err = CMGetEndpoint(fOpenOptions, &endpoint, false)) == noErr
		  && (err = TEndpointClient::Init(endpoint, 'endp', 'newt')) == noErr
		  && (err = fEndpoint->Open((ULong) this)) == noErr)
	{
		fEndpoint->SetSync(false);
		err = fEndpoint->nBind(fBindOptions, 0, false);
	}
	if (err != noErr)
	{
		fOutBuffer->fPutSignal = err;
		fInBuffer->fGetSignal = err;
	}
	return err;
}


// ROM 0x0012cb64 CheckSend__18TNTKEndpointClientFv
// What the nub has written sent, a block at a time.
void
TNTKEndpointClient::CheckSend(void)
{
	newton_try
	{
		if (!fSending && fOutBuffer != nil && fOutBuffer->DataCount() > 0)
		{
			Size count = fSendSize;
			count = fOutBuffer->Getn(fSendBuffer, count);
			fSending = true;
			NewtonErr err = fEndpoint->nSnd(fSendBuffer, &count, 1, 0, false, nil);		// (flags 1: kPacket)
			if (err != noErr)
				fOutBuffer->fPutSignal = err;
		}
	}
	newton_catch_all
	{ }
	end_try;
}


// ROM 0x0012cc38 IdleProc__18TNTKEndpointClientFP10TUMsgTokenPUlP7TAEvent
void
TNTKEndpointClient::IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	if (fDying)
		return;
	CheckSend();
	ResetIdle(50, kMilliseconds);
}


// ROM 0x0012cc74 BindComplete__18TNTKEndpointClientFP14TEndpointEvent
void
TNTKEndpointClient::BindComplete(TEndpointEvent* event)
{
	if (event->fError != noErr)
	{
		fOutBuffer->fPutSignal = event->fError;
		fInBuffer->fGetSignal = event->fError;
		return;
	}
	NewtonErr err = fEndpoint->nConnect(fConnectOptions, nil, nil, 0, false);
	if (err != noErr)
	{
		fOutBuffer->fPutSignal = err;
		fInBuffer->fGetSignal = err;
	}
}


// ROM 0x0012ccec ConnectComplete__18TNTKEndpointClientFP14TEndpointEvent
// Connected: the idler that sends, and the first receive.
void
TNTKEndpointClient::ConnectComplete(TEndpointEvent* event)
{
	if (event->fError != noErr)
	{
		fOutBuffer->fPutSignal = event->fError;
		fInBuffer->fGetSignal = event->fError;
		return;
	}
	NewtonErr err = InitIdler(50, kMilliseconds, 0, true);
	if (err == noErr)
	{
		ULong flags = 0;
		Size count = fReceiveSize;
		err = fEndpoint->nRcv(fReceiveBuffer, &count, 1, &flags, 0, false, nil);
	}
	if (err != noErr)
		fInBuffer->fGetSignal = err;
}


// ROM 0x0012cda0 SndComplete__18TNTKEndpointClientFP14TEndpointEvent
void
TNTKEndpointClient::SndComplete(TEndpointEvent* event)
{
	if (event->fError != noErr)
		fOutBuffer->fPutSignal = event->fError;
	fSending = false;
	if (!fDying)
		CheckSend();
}


// ROM 0x0012cdc8 RcvComplete__18TNTKEndpointClientFP14TEndpointEvent
// What came in put in the nub's buffer, and the next receive.
void
TNTKEndpointClient::RcvComplete(TEndpointEvent* event)
{
	if (fDying)
		return;
	if (event->fError == noErr)
	{
		newton_try
		{
			fInBuffer->PutnCompletely(fReceiveBuffer, ((TRcvCompleteEvent*) event)->fCount, fPause, 0);
		}
		newton_catch_all
		{ }
		end_try;
	}
	else
		fInBuffer->fGetSignal = event->fError;
	ULong flags = 0;
	Size count = fReceiveSize;
	NewtonErr err = fEndpoint->nRcv(fReceiveBuffer, &count, 1, &flags, 0, false, nil);
	if (err != noErr)
		fInBuffer->fGetSignal = err;
}


// ROM 0x0012ceb0 MakeYourPeace__18TNTKEndpointClientFv
// (the nub's kill event) the endpoint aborted, which takes it down.
void
TNTKEndpointClient::MakeYourPeace(void)
{
	fDying = true;
	fEndpoint->nAbort(false);
}


// ROM 0x0012cec4 AbortComplete__18TNTKEndpointClientFP14TEndpointEvent
void
TNTKEndpointClient::AbortComplete(TEndpointEvent* event)
{
	if (fEndpoint->nDisconnect(nil, 0, 0, 0, false) != noErr)
		fEndpoint->nUnBind(0, false);
}


// ROM 0x0012cf18 DisconnectComplete__18TNTKEndpointClientFP14TEndpointEvent
void
TNTKEndpointClient::DisconnectComplete(TEndpointEvent* event)
{
	fEndpoint->nUnBind(0, false);
}


// ROM 0x0012cfe8 UnBindComplete__18TNTKEndpointClientFP14TEndpointEvent
// Unbound: closed, and the task's loop ended.
void
TNTKEndpointClient::UnBindComplete(TEndpointEvent* event)
{
	fEndpoint->Close();
	((TAppWorld*) GetGlobals())->AETerminateLoop();
}


// ROM 0x0012d008 Disconnect__18TNTKEndpointClientFP14TEndpointEvent
// The other end went: both buffers signalled.
void
TNTKEndpointClient::Disconnect(TEndpointEvent* event)
{
	fOutBuffer->fPutSignal = kNTKErrDisconnected;
	fInBuffer->fGetSignal = kNTKErrDisconnected;
	fDying = true;
}
