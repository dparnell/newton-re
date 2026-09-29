/*
	File:		comms/Docker.cpp

	Contains:	The docker (Docker.h): the 'dock' protocol's framing, the
				session's connection and the package loader's session, and
				the protocol frame's native methods.

	Reconstructed from the MP2x00 US ROM (0x00092664-0x0009b40c); each
	function cites its origin.
*/

#include "Docker.h"
#include "EzEndpointPipe.h"
#include "StorePackages.h"
#include "NewtWorld.h"
#include "Dates.h"
#include "Soups.h"
#include "ROMImport.h"
#include "PackageManager.h"
#include "LargeObjects.h"
#include "Cursors.h"
#include "Entries.h"
#include "RichString.h"
#include "ROMConstants.h"
#include "ObjectStreamer.h"
#include "Locale.h"
#include "Ports.h"
#include "NewtonGestalt.h"
#include "hal/System.h"
#include "AppWorld.h"
#include "Interpreter.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "toolbox/ByteOrder.h"
#include "Unicode.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern const ExceptionName exPipeException;
extern const ExceptionName exLongErrorException;
extern const ExceptionName exErrorException;
extern const ExceptionName exFrames;
extern const ExceptionName exOutOfMemory;
extern const ExceptionName exNoSoupDefException;



static const ExceptionName exRefExceptionName = (ExceptionName) "type.ref";		// ROM 0x00380880 exRefException


// NEWTON_TRACE_DOCK (host only): the commands each way, and the errors the
// docker records, on stderr
static int
TracingDock(void)
{
	static int tracing = -1;
	if (tracing < 0)
		tracing = getenv("NEWTON_TRACE_DOCK") != nil;
	return tracing;
}


static void
TraceCommand(const char* direction, ULong command, ULong length)
{
	if (TracingDock())
		fprintf(stderr, "[dock] %s %c%c%c%c (%lu)\n", direction, (char) (command >> 24), (char) (command >> 16),
			(char) (command >> 8), (char) command, (unsigned long) length);
}


TDocker*	gTheDocker = nil;		// ROM 0x0c100cb8 gTheDocker


// ROM 0x001fbbe4 FDefaultStore
// The store GetDefaultStore answers.
Ref
FDefaultStore(RefArg /*rcvr*/)
{
	RefVar fn(NSGetGlobalFn(RefVar(RSSYMgetdefaultstore)));
	return NSCall(fn);
}


// ------------------------------------------------------------------------
//	TEzPipeProtocol
// ------------------------------------------------------------------------

// ROM 0x00092664 ProtocolInit__15TEzPipeProtocolFUlT1
void
TEzPipeProtocol::ProtocolInit(ULong protocol, ULong subProtocol)
{
	fProtocol = protocol;
	fSubProtocol = subProtocol;
}


// ROM 0x00097288 WriteDockerHeader__15TEzPipeProtocolFUlUc
// The header; with flush, a length of nought after it and the pipe flushed
// (a command with no data), else the length is the caller's to write.
void
TEzPipeProtocol::WriteDockerHeader(ULong command, Boolean flush)
{
	UByte header[16];
	PutBigEndianWord(header, fProtocol);
	PutBigEndianWord(header + 4, fSubProtocol);
	PutBigEndianWord(header + 8, command);
	PutBigEndianWord(header + 12, 0);
	TraceCommand("->", command, 0);
	if (!flush)
		fPipe->WriteChunk(header, 12, false);
	else
	{
		fPipe->WriteChunk(header, 16, false);
		fPipe->FlushWrite();
	}
}


// ROM 0x000978b8 SendDockerHeader__15TEzPipeProtocolFUlUc
void
TEzPipeProtocol::SendDockerHeader(ULong command, Boolean flush)
{
	WriteDockerHeader(command, flush);
}


// ROM 0x00098de0 ReadDockerHeader__15TEzPipeProtocolFRUlT1
// A header: the two protocol words (kDockErrBadHeader if they are not
// ours), the command and its length.
void
TEzPipeProtocol::ReadDockerHeader(ULong* command, ULong* length)
{
	UByte words[8];
	long count = 8;
	Boolean eof;
	fPipe->ReadChunk(words, count, eof);
	if (GetBigEndianWord(words) != fProtocol || GetBigEndianWord(words + 4) != fSubProtocol)
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrBadHeader, nil);
	count = 8;
	fPipe->ReadChunk(words, count, eof);
	*command = GetBigEndianWord(words);
	*length = GetBigEndianWord(words + 4);
	TraceCommand("<-", *command, *length);
}


// ROM 0x00098144 FindDockerHeader__15TEzPipeProtocolFRUlT1
// A header, the bytes before it skipped: the protocol words are looked for
// a byte at a time.
void
TEzPipeProtocol::FindDockerHeader(ULong* command, ULong* length)
{
	UByte words[8];
	long count = 8;
	Boolean eof;
	fPipe->ReadChunk(words, count, eof);
	ULong first = GetBigEndianWord(words);
	ULong second = GetBigEndianWord(words + 4);
	while (!(first == fProtocol && second == fSubProtocol))
	{
		char c;
		*fPipe >> c;
		first = first << 8 | second >> 24;
		second = (UByte) c | second << 8;
	}
	count = 8;
	fPipe->ReadChunk(words, count, eof);
	*command = GetBigEndianWord(words);
	*length = GetBigEndianWord(words + 4);
}


// ------------------------------------------------------------------------
//	TDockerDynArray
// ------------------------------------------------------------------------

// ROM 0x00099f34 __ct__15TDockerDynArrayFv
TDockerDynArray::TDockerDynArray()
{
	fWords = nil;
	fCount = 0;
	fAllocated = 0;
}


// ROM 0x0009b0f4 __dt__15TDockerDynArrayFv
TDockerDynArray::~TDockerDynArray()
{
	if (fWords != nil)
		DisposHandle(fWords);
}


// ROM 0x0009ba80 Add__15TDockerDynArrayFUl
// The word added at the end, the handle grown thirty words at a time.
// ==> noErr, or why not.
NewtonErr
TDockerDynArray::Add(ULong value)
{
	NewtonErr err = noErr;
	if (fWords == nil)
	{
		fAllocated = 30;
		fWords = NewHandle(30 * sizeof(ULong));
		if (fWords == nil)
			return -10007;			// (kOSErrNoMemory)
	}
	if (fCount >= fAllocated)
	{
		// ROM BUG: the room is counted before the handle has grown, so
		// when SetHandleSize fails the next Add believes there is room and
		// writes past the end of the handle
		fAllocated += 30;
		SetHandleSize(fWords, fAllocated * sizeof(ULong));
		err = MemError();
		if (err != noErr)
			return err;
	}
	((ULong*) *fWords)[fCount] = value;
	fCount++;
	return err;
}


// ROM 0x0009266c AddAndReplaceZero__15TDockerDynArrayFUlRl
// The word put in the first free (nought) slot, or added at the end;
// index says where.
NewtonErr
TDockerDynArray::AddAndReplaceZero(ULong value, long* index)
{
	NewtonErr err = noErr;
	*index = Find(0);
	if (*index < 0)
	{
		err = Add(value);
		*index = fCount - 1;
	}
	else
		((ULong*) *fWords)[*index] = value;
	return err;
}


// ROM 0x000932e8 Find__15TDockerDynArrayFl
// ==> the word's index, or -1.
long
TDockerDynArray::Find(long value)
{
	if (fWords != nil && fCount != 0)
	{
		for (ULong i = 0; i < fCount; i++)
			if (((Long32*) *fWords)[i] == value)
				return i;
	}
	return -1;
}


// ROM 0x00092cdc Replace__15TDockerDynArrayFlUl
void
TDockerDynArray::Replace(long index, ULong value)
{
	if ((ULong) index >= fCount)
		return;
	((ULong*) *fWords)[index] = value;
}


// ------------------------------------------------------------------------
//	TDocker
// ------------------------------------------------------------------------

// ROM 0x00097374 __ct__7TDockerFv
TDocker::TDocker()
{
	fCurrentSoup = NILREF;
	fCurrentStore = NILREF;
	fDoConnectionArg = NILREF;
	fDuplicateOf = NILREF;
	fCallback = NILREF;
	fExtensions = NILREF;
	fDesktopApps = NILREF;
	fCursors = nil;
	fSyncChanges = NILREF;
	fDesktopChallenge[0] = 0;
	fDesktopChallenge[1] = 0;
	fNewtonChallenge[0] = 0;
	fNewtonChallenge[1] = 0;
	fSessionAgreed = false;
	fPlatform = 2;
	fStopping = false;
	fProtocolVersion = 9;
	fLength = 0;
	fError = noErr;
	fPipe = nil;
	fPipeOpen = false;
	fSelectiveSyncOK = false;
	fIsDirectorySoup = false;
	fIsSystemSoup = false;
	fIsPackageSoup = false;
	fQuery = NILREF;
	fSessionStarted = false;
	fDataPending = false;
	fBytesRead = 0;
	fWritingData = false;
	fBytesWritten = 0;
	fLocked = false;
	fVBOCompression = 2;
	fHasArg1 = false;
	fLoadPackageOnly = false;
	fChangedIDs = nil;
	fExtensionCommands = nil;
	fManufacturer = 0;
	fSourceVersion = 2;
	fMachineType = 0;
	fSourceManufacturer = 0;
	fSourceMachineType = 0;
	fConversionFrame = NILREF;
	fOwnerApp = NILREF;
	fCleanedUp = false;
	fInConnectionApp = false;
	fState = kDockStateNone;
	// (not set by the ROM's constructor either)
	fStopDone = false;
	fCommand = 0;
	fDesktopTime = 0;
	fTimeSet = 0;
	fKey[0] = 0;			// (the ROM's key is whatever the heap held)
	fKey[1] = 0;
	fProtocol = 0;
	fSubProtocol = 0;
}


// ROM 0x0009771c __dt__7TDockerFv
TDocker::~TDocker()
{
	TossDataStructures();
}


// ROM 0x000977b0 GetTDockerLock__7TDockerFv
Boolean
TDocker::GetTDockerLock(void)
{
	return fLocked;
}


// ROM 0x000977b8 WaitAndLockTDocker__7TDockerFv
// The docker locked - waiting up to ten seconds for another operation on
// it to finish, the NewtonScript world's fork let run meanwhile.
// ==> whether it was locked.
Boolean
TDocker::WaitAndLockTDocker(void)
{
	ULong start = Ticks();
	ULong waited = 0;
	while (GetTDockerLock() && waited < 600)
	{
		FYieldToFork(RefVar(NILREF));
		waited = Ticks() - start;
	}
	Boolean wasFree = !GetTDockerLock();
	if (wasFree)
		fLocked = true;
	return wasFree;
}


// ROM 0x0009783c UnlockTDocker__7TDockerFv
void
TDocker::UnlockTDocker(void)
{
	fLocked = false;
}


// ROM 0x00097848 OutOfMemory__7TDockerFv
void
TDocker::OutOfMemory(void)
{
	fError = -10007;			// (kOSErrNoMemory)
	Throw(exOutOfMemory, (void*) (intptr_t) -10007, nil);
}


// ROM 0x00093ff8 GetSyncChanges__7TDockerFv
Ref
TDocker::GetSyncChanges(void)
{
	return fSyncChanges;
}


// ROM 0x00094004 SetState__7TDockerF13eDockingState
void
TDocker::SetState(long state)
{
	fState = state;
}


// ROM 0x0009400c GetPlatform__7TDockerFv
long
TDocker::GetPlatform(void)
{
	return fPlatform;
}


// ROM 0x000940f8 GetCurrentStore__7TDockerFv
Ref
TDocker::GetCurrentStore(void)
{
	return fCurrentStore;
}


// ROM 0x00093efc GetState__7TDockerFv
// The state frame: {protocolVersion, store, soup, state, selectiveSyncOK,
// desktopApps}.
Ref
TDocker::GetState(void)
{
	RefVar state(AllocateFrame());
	SetFrameSlot(state, RSSYMprotocolversion, RefVar(MAKEINT(fProtocolVersion)));
	SetFrameSlot(state, RSSYMstore, fCurrentStore);
	SetFrameSlot(state, RSSYMsoup, fCurrentSoup);
	SetFrameSlot(state, RSSYMstate, RefVar(MAKEINT(fState)));
	SetFrameSlot(state, RSSYMselectivesyncok, RefVar(fSelectiveSyncOK ? TRUEREF : NILREF));
	SetFrameSlot(state, RSSYMdesktopapps, fDesktopApps);
	return state;
}


// ROM 0x00094014 BytesAvailable__7TDockerFUc
// How many bytes the desktop has sent that are not yet read (nought once
// the docker is cleaned up, or when it cannot be locked); locked says the
// caller holds the lock already.
ULong
TDocker::BytesAvailable(Boolean locked)
{
	if (fCleanedUp)
		return 0;
	ULong available = 0;
	if (locked || WaitAndLockTDocker())
	{
		newton_try
		{
			available = fPipe->BytesAvailable();
		}
		newton_catch_all
		{ }
		end_try;
		if (!locked)
			UnlockTDocker();
	}
	return available;
}


// ROM 0x000925c0 DockerFramesException__FP9Exception
// The error a NewtonScript exception stands for: a type.ref one's frame's
// errorCode, else its data; -1 when there is none.
static long
DockerFramesException(Exception* exception)
{
	long error = -1;
	if (!Subexception(exception->name, exRefExceptionName))
		error = (long) (Long) exception->data;
	else
	{
		RefVar data(*(RefStruct*) exception->data);
		if (IsFrame(data) && FrameHasSlot(data, RSSYMerrorcode))
			error = RINT(GetFrameSlot(data, RSSYMerrorcode));
	}
	return error;
}


// ROM 0x000941b8 ProcessException__7TDockerFP9Exception
// The error an exception caught during an operation stands for, in
// fError: a pipe, long-error or out-of-memory exception's data, a
// NewtonScript one's error code, anything else -1 (unless an error was
// already recorded).  The two endpoint errors of a connection that went
// (-36006, and -10039 or -36003) become the comm tool's.
void
TDocker::ProcessException(Exception* exception)
{
	if (TracingDock())
		fprintf(stderr, "[dock] exception %s (%ld)\n", exception->name, (long) (intptr_t) exception->data);
	if (Subexception(exception->name, exPipeException)
	 || Subexception(exception->name, exLongErrorException)
	 || Subexception(exception->name, exOutOfMemory))
		fError = (long) (Long) exception->data;
	else if (Subexception(exception->name, exFrames)
		  || Subexception(exception->name, exRefExceptionName)
		  || Subexception(exception->name, exErrorException))
		fError = DockerFramesException(exception);
	else if (fError == noErr)
		fError = -1;
	if (fError == -36006)
		fError = -16009;
	else if (fError == -10039 || fError == -36003)
		fError = -16005;
}


// ROM 0x000942d4 Delay__7TDockerFUl
// A busy wait of so many ticks.
void
TDocker::Delay(ULong ticks)
{
	ULong start = Ticks();
	while (Ticks() - start < ticks)
		;
}


// ROM 0x00094310 WaitForDisconnect__7TDockerFv
// Three seconds for the desktop to read the result and go.
void
TDocker::WaitForDisconnect(void)
{
	Delay(180);
}


// ROM 0x00094318 CleanUpIfError__7TDockerFUc
// After an operation: an error (other than kDockErrRetryPassword and
// -16005, a connection the desktop closed), or force, ends the session -
// the error is told the desktop, if the pipe was ever open and the error is
// not -16009, and the pipe and the rest are given back.
void
TDocker::CleanUpIfError(Boolean force)
{
	long error = fError;
	if ((error == noErr || error == kDockErrRetryPassword || error == -16005) && !force)
		return;
	fCleanedUp = true;
	if (error != noErr && fPipe != nil && fPipeOpen && error != -16009)
	{
		newton_try
		{
			WriteResult(fError);
			WaitForDisconnect();
		}
		newton_catch_all
		{ }
		end_try;
	}
	TossDataStructures();
}


// ROM 0x000943c4 CleanUpIfStopping__7TDockerFUc
// A session stopped (Stop) while it was not done: once the stop has
// taken, a desktop of protocol 10 or later is told the operation was
// cancelled ('opca') and its commands are read and thrown away until it
// acknowledges ('ocaa') or says 'disc'.  ==> whether the session is done.
Boolean
TDocker::CleanUpIfStopping(Boolean done)
{
	if (fStopping && !done)
	{
		WaitForStopToComplete();
		long error = fError;
		if (error == noErr || error == -16005)
		{
			if (fProtocolVersion < 10)
				done = true;
			else
			{
				long saved = -16005;
				if (error != noErr)
					saved = error;
				fError = noErr;
				newton_try
				{
					WriteDockerHeader(kDOperationCanceled, true);
					Boolean first = true;
					while (fError == noErr && fCommand != kDOpCanceledAck && !done)
					{
						if (first)
						{
							FindDockerHeader(&fCommand, &fLength);
							first = false;
						}
						else
							ReadDockerHeader(&fCommand, &fLength);
						if (fCommand == kDDisconnect)
							done = true;
						FlushCommand();
					}
					fSessionStarted = true;
				}
				newton_catch_all
				{
					ProcessException(CurrentException());
				}
				end_try;
				if (fError == noErr)
					fError = saved;
			}
		}
	}
	return done;
}


// ROM 0x0009450c AbortConnection__7TDockerFl
// The session ended with the error: the pipe aborted and the docker
// cleaned up.
Boolean
TDocker::AbortConnection(long error)
{
	fError = error;
	if (fPipe != nil)
	{
		newton_try
		{
			fPipe->Abort();
		}
		newton_catch_all
		{ }
		end_try;
	}
	CleanUpIfError(true);
	return true;
}


// ROM 0x00097568 WaitForStopToComplete__7TDockerFv
// Up to three seconds for a Stop in the fork to finish aborting the pipe.
void
TDocker::WaitForStopToComplete(void)
{
	if (!fStopping)
		return;
	ULong start = Ticks();
	while (!fStopDone && Ticks() < start + 180)
		FYieldToFork(RefVar(NILREF));
	fStopping = false;
	fStopDone = false;
}


// ROM 0x000975dc TossDataStructures__7TDockerFv
// The pipe (and its endpoint) and the session's arrays given back.
void
TDocker::TossDataStructures(void)
{
	WaitForStopToComplete();
	if (fPipe != nil)
	{
		newton_try
		{
			delete fPipe;
		}
		newton_catch_all
		{ }
		end_try;
		fPipe = nil;
	}
	if (fCursors != nil)
	{
		newton_try
		{
			delete fCursors;
		}
		newton_catch_all
		{ }
		end_try;
		fCursors = nil;
	}
	if (fChangedIDs != nil)
	{
		newton_try
		{
			delete fChangedIDs;
		}
		newton_catch_all
		{ }
		end_try;
		fChangedIDs = nil;
	}
	if (fExtensionCommands != nil)
	{
		newton_try
		{
			delete fExtensionCommands;
		}
		newton_catch_all
		{ }
		end_try;
		fExtensionCommands = nil;
	}
}


// ROM 0x00095010 Stop__7TDockerFv
// Stopped from the fork while an operation holds the docker: the pipe is
// aborted (ROM: through its vtable, which for a TEzEndpointPipe is the
// hidden, empty TEzEndpointPipe::Abort) and the operation notices
// fStopping.
void
TDocker::Stop(void)
{
	if (GetTDockerLock())
	{
		fStopping = true;
		fStopDone = false;
		if (fPipe != nil)
			fPipe->Abort();
		fStopDone = true;
	}
}


// ROM 0x00094104 BroadcastChanges__7TDockerFv
// The soups a session changed told to the applications (ConnSendChanges,
// with whether it was a restore).  ==> fError.
long
TDocker::BroadcastChanges(void)
{
	if (NOTNIL(fSyncChanges))
	{
		newton_try
		{
			RefVar restore(fState == kDockStateRestore ? TRUEREF : NILREF);
			NSCallGlobalFn(RefVar(RSSYMconnsendchanges), restore, fSyncChanges);
		}
		newton_catch_all
		{
			ProcessException(CurrentException());
		}
		end_try;
		CleanUpIfError(false);
	}
	return fError;
}


// A protocol extension's command: its first four characters as a word
// (ConvertFromUnicode's bytes read big-endian, as the ROM reads them).
// DEVIATION: the ROM's buffer is uninitialised, so a command of fewer than
// four characters has stack rubbish after its terminator; here, noughts.
static ULong
ExtensionCommandWord(RefArg command)
{
	RefVar str(command);
	if (!IsString(str) || Ustrlen(GetCString(str)) > 4)
		str = Substring(str, 0, 4);
	UByte bytes[20];
	memset(bytes, 0, sizeof(bytes));
	ConvertFromUnicode(GetCString(str), bytes, kMacRomanEncoding, 0x7fffffff);
	return GetBigEndianWord(bytes);
}


// ROM 0x0009c200 InstallProtocolExtension__7TDockerFRC6RefVarT1Ul
// A function to run when the desktop sends the command (its word, or
// else its first four characters): recorded in the first free slot, the
// same slot of fExtensions holding the function.  A command already
// extended is kDockErrBadExtension.  ==> fError.
long
TDocker::InstallProtocolExtension(RefArg command, RefArg function, ULong commandWord)
{
	if (fCleanedUp)
		return noErr;
	fError = noErr;
	newton_try
	{
		if (commandWord == 0)
			commandWord = ExtensionCommandWord(command);
		if (commandWord == 0)
			Throw(exLongErrorException, (void*) (intptr_t) kDockErrBadExtension, nil);
		if (fExtensionCommands == nil)
		{
			fExtensionCommands = new TDockerDynArray;
			if (fExtensionCommands == nil)
				OutOfMemory();
			fExtensions = AllocateArray(RSSYMarray, 0);
		}
		if (fExtensionCommands->Find(commandWord) < 0)
		{
			long index;
			fError = fExtensionCommands->AddAndReplaceZero(commandWord, &index);
			if (fError == noErr)
			{
				if (index < Length(fExtensions))
					SetArraySlot(fExtensions, index, function);
				else
					AddArraySlot(fExtensions, function);
			}
		}
		else
			fError = kDockErrBadExtension;
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	CleanUpIfStopping(false);
	if (fError != kDockErrBadExtension)
		CleanUpIfError(false);
	return fError;
}


// ROM 0x0009c3f8 RemoveProtocolExtension__7TDockerFRC6RefVarUl
// The command's extension, if there is one, forgotten (its slot freed).
// ==> fError.
long
TDocker::RemoveProtocolExtension(RefArg command, ULong commandWord)
{
	if (fCleanedUp)
		return noErr;
	fError = noErr;
	newton_try
	{
		if (commandWord == 0)
			commandWord = ExtensionCommandWord(command);
		long index;
		if (fExtensionCommands != nil && (index = fExtensionCommands->Find(commandWord)) >= 0)
		{
			fExtensionCommands->Replace(index, 0);
			SetArraySlot(fExtensions, index, RefVar(NILREF));
		}
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	CleanUpIfStopping(false);
	CleanUpIfError(false);
	return fError;
}


// ROM 0x00094b74 Connect__7TDockerFRC6RefVarN21
// The session begun: the pipe made from the options frame (its
// connectTimeout in seconds, 30 if it has none; then its idleTimeout),
// 'rtdk' said and the desktop's answer read.  'lpkg' is a package to load;
// 'dock' a docking session: its kind, the Newton's name, the desktop's
// info, the icons and the timeout, then from protocol 10 the passwords -
// each side sends the other's challenge encrypted under its key.
// ==> fError.
long
TDocker::Connect(RefArg connection, RefArg options, RefArg password)
{
	WaitAndLockTDocker();
	fError = noErr;
	fCleanedUp = false;
	newton_try
	{
		fConnection = connection;
		fPipe = new TEzEndpointPipe;
		if (fPipe == nil)
			OutOfMemory();
		ULong timeout = 30 * kSeconds;
		// (the ROM's RefHandle for the timeouts is given back only when
		// Connect succeeds: one is lost with every exception - not
		// reproduced, a RefVar goes with the stack)
		RefVar slot(GetFrameSlot(options, RSSYMconnecttimeout));
		if (NOTNIL(slot))
			timeout = RINT(slot) * kSeconds;
		fPipe->Init(options, timeout);
		fPipeOpen = true;
		if (fStopping)
			Throw(exLongErrorException, (void*) (intptr_t) kDockErrDisconnected, nil);
		slot = GetFrameSlot(options, RSSYMidletimeout);
		if (NOTNIL(slot))
			fPipe->SetTimeout(RINT(slot) * kSeconds);
		ProtocolInit(kDNewtonDock, kDDock);
		WriteLong(kDRequestToDock, 9);
		ReadDockerHeader(&fCommand, &fLength);
		if (fCommand == kDLoadPackage)
		{
			fLoadPackageOnly = true;
			fSessionAgreed = true;
			fState = kDockStateLoadPackage;
		}
		else if (fCommand == kDDock)
		{
			if (fStopping)
				Throw(exLongErrorException, (void*) (intptr_t) kDockErrDisconnected, nil);
			ReadInitiateDocking();
			WriteNewtonName();
			ReadDockerHeader(&fCommand, &fLength);
			if (fCommand == kDDesktopInfo)
			{
				ReadDesktopInfo();
				ReadDockerHeader(&fCommand, &fLength);
			}
			if (fProtocolVersion < 10)
			{
				// an old desktop may only load packages
				if (fState != kDockStateLoadPackage)
					Throw(exLongErrorException, (void*) (intptr_t) kDockErrProtocolVersion, nil);
				fSessionAgreed = true;
			}
			if (fCommand == kDWhichIcons)
			{
				SetWhichIcons();
				ReadDockerHeader(&fCommand, &fLength);
			}
			if (fCommand == kDSetTimeout)
			{
				long seconds;
				*fPipe >> seconds;
				fPipe->SetTimeout(seconds * kSeconds);
			}
			else
			{
				long error = kDockErrDesktopError;
				if (fCommand == kDResult)
					error = fError = ReadResult();
				if (error != noErr)
					Throw(exLongErrorException, (void*) (intptr_t) error, nil);
			}
			if (fStopping)
				Throw(exLongErrorException, (void*) (intptr_t) kDockErrDisconnected, nil);
			if (fProtocolVersion > 9)
			{
				WritePassword(password);
				ReadPassword();
			}
			if (fStopping)
				Throw(exLongErrorException, (void*) (intptr_t) kDockErrDisconnected, nil);
			fSessionStarted = true;
		}
		else
			Throw(exLongErrorException, (void*) (intptr_t) (fCommand == kDRequestToDock ? kDockErrRequestToDock : kDockErrBadHeader), nil);
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	if (TracingDock())
		fprintf(stderr, "[dock] connected: error %ld, state %ld, protocol %lu\n", fError, fState, (unsigned long) fProtocolVersion);
	CleanUpIfError(fStopping);
	UnlockTDocker();
	return fError;
}


// ROM 0x00095058 CompatabilityHacks__7TDockerFv
// The package loader's session: each package read and installed, its
// result said ('dres'), and half a second given the desktop to send
// another 'lpkg' (or 'disc', or anything else: kDockErrDesktopError).
void
TDocker::CompatabilityHacks(void)
{
	Boolean another;
	do
	{
		another = false;
		ReadPackage();
		newton_try
		{
			WriteResult(fError);
			Delay(30);
			if (BytesAvailable(true))
			{
				ReadDockerHeader(&fCommand, &fLength);
				another = fCommand == kDLoadPackage;
				if (!another && fCommand != kDDisconnect)
					fError = kDockErrDesktopError;
			}
			if (!another)
				Delay(60);
		}
		newton_catch_all
		{ }
		end_try;
	} while (another);
}


// ROM 0x00095130 DoConnection__7TDockerFRC6RefVarN21RUc
// The session carried out, the NewtonScript world forked first so that it
// goes on running meanwhile: a package loader's session
// (CompatabilityHacks), or a docking session - what the Newton wants
// said, then the desktop's commands carried out until one ends it.  done
// says whether the session is over.  ==> fError.
long
TDocker::DoConnection(RefArg arg1, RefArg arg2, RefArg callback, Boolean* done)
{
	if (fCleanedUp)
		return noErr;
	fStopping = false;
	Boolean operationDone = false;
	*done = true;
	fError = noErr;
	if (!fSessionAgreed)
		fError = kDockErrNotConnected;
	else
	{
		WaitAndLockTDocker();
		fDoConnectionArg = arg2;
		fCallback = callback;
		fHasArg1 = NOTNIL(arg1);
		fSyncChanges = NILREF;
		FreeCurrentStore();
		fCurrentSoup = NILREF;
		newton_try
		{
			fError = ((TForkWorld*) GetGlobals())->Fork(nil);
			if (fError != noErr)
				Throw(exLongErrorException, (void*) (intptr_t) fError, nil);
			if (!fLoadPackageOnly)
			{
				// the session under way and the desktop silent: what the
				// Newton wants of it said (a sync, a restore, or a result)
				if (fSessionStarted && BytesAvailable(true) == 0)
				{
					if (fState == kDockStateSync)
						WriteDockerHeader(kDSync, true);
					else if (fState == kDockStateRestore)
						WriteDockerHeader(kDRestore, true);
					else
						WriteResult(noErr);
				}
				*done = false;
				while (fError == noErr && !*done && !operationDone && !fStopping)
				{
					ReadDockerHeader(&fCommand, &fLength);
					ProcessCommand(done, &operationDone);
					if (fState == kDockStateKeyboard)
					{
						// NOT YET: the keyboard passthrough
						// (KeyboardProcessCommand)
						operationDone = true;
						fSessionStarted = true;
					}
				}
			}
			else
				CompatabilityHacks();
		}
		newton_catch_all
		{
			ProcessException(CurrentException());
		}
		end_try;
	}
	if (!fStopping)
	{
		if (fError == noErr && fState == kDockStateSync && operationDone)
			fSelectiveSyncOK = true;
	}
	else
	{
		if (!operationDone)
			*done = CleanUpIfStopping(*done);
		if (fError == -16005)
			fError = noErr;
	}
	fConversionFrame = NILREF;
	fOwnerApp = NILREF;
	fSourceVersion = 2;
	FreeCurrentStore();
	fCurrentSoup = NILREF;
	fQuery = NILREF;
	CleanUpIfError(*done);
	UnlockTDocker();
	return fError;
}


// ROM 0x0009b30c ReadPackage__7TDockerFv
// A package the desktop sent ('lpkg', its length fLength) read onto the
// current store (the default store if there is none) and, unless this is
// a restore, activated; its padding read after it.  An error other than
// the three a package may simply be refused with is thrown.
void
TDocker::ReadPackage(void)
{
	RefVar store(fCurrentStore);
	if (ISNIL(store))
		store = FDefaultStore(RefVar(NILREF));
	RefVar result(SuckPackageThruPipe(fPipe, store, fCallback, 2000, fState != kDockStateRestore));
	if (ISINT(result))
		fError = RVALUE(result);
	long error = fError;
	if (error != noErr && error != -10402 && error != -10409 && error != -10410)
		Throw(exLongErrorException, (void*) (intptr_t) error, nil);
	FlushPadding(fLength);
}


// ROM 0x00098008 FreeCurrentStore__7TDockerFv
// The current store, if there is one, marked not busy for the application.
void
TDocker::FreeCurrentStore(void)
{
	if (ISNIL(fCurrentStore))
		return;
	RefVar appSymbol(GetFrameSlot(fConnection, RSSYMappsymbol));
	NSSend(fCurrentStore, RefVar(RSSYMmarknotbusy), appSymbol);
	fCurrentStore = NILREF;
}


// ROM 0x00099c30 WriteLong__7TDockerFUlT1
// A command whose data is one word.
void
TDocker::WriteLong(ULong command, ULong value)
{
	WriteDockerHeader(command, false);
	*fPipe << (long) 4;
	*fPipe << (unsigned long) value;
	fPipe->FlushWrite();
}


// ROM 0x00099c74 WriteResult__7TDockerFl
// (A 'dres' of the result.)
void
TDocker::WriteResult(long result)
{
	WriteLong(kDResult, result);
}


// ROM 0x00099c84 ReadResult__7TDockerFv
long
TDocker::ReadResult(void)
{
	long result;
	*fPipe >> result;
	return result;
}


// ROM 0x00097868 ReadChunk__7TDockerFPvlUc
// ==> whether the pipe reached its end.
Boolean
TDocker::ReadChunk(void* buffer, long length, Boolean flushPadding)
{
	long count = length;
	Boolean eof;
	fPipe->ReadChunk(buffer, count, eof);
	if (flushPadding)
		FlushPadding(length);
	return eof;
}


// ROM 0x000978c0 Pad__7TDockerFUl
// The noughts that bring data of that length to a whole word.
void
TDocker::Pad(ULong length)
{
	if ((length & 3) == 0)
		return;
	ULong zero = 0;
	fPipe->WriteChunk(&zero, 4 - (length & 3), false);
}


// ROM 0x00097904 FlushPadding__7TDockerFUl
// The padding after data of that length read and thrown away.
void
TDocker::FlushPadding(ULong length)
{
	if ((length & 3) != 0)
	{
		UByte padding[4];
		long count = 4 - (length & 3);
		Boolean eof;
		fPipe->ReadChunk(padding, count, eof);
	}
}


// ROM 0x0009bf6c FlushCommand__7TDockerFv
// The current command's data (fLength bytes and their padding) read and
// thrown away.
void
TDocker::FlushCommand(void)
{
	if (fLength == 0)
		return;
	Ptr data = NewPtr(fLength);
	if (data == nil)
		OutOfMemory();
	newton_try
	{
		ReadChunk(data, fLength, true);
	}
	newton_catch_all
	{
		DisposPtr(data);
		rethrow;
	}
	end_try;
	DisposPtr(data);
}


// ROM 0x00099e7c ReadRef__7TDockerFRC6RefVar
// An object the desktop sent (NSOF), its large binaries onto the store
// given; the command's padding read after it.
Ref
TDocker::ReadRef(RefArg store)
{
	RefVar obj;
	{
		TObjectReader reader(*fPipe, store);
		obj = reader.Read();
	}
	if (fLength != 0xffffffff)
		FlushPadding(fLength);
	return obj;
}


// ROM 0x00099d10 WriteRef__7TDockerFUlRC6RefVar
// An object sent as the command's data (NSOF, its length first, padded),
// large binaries compressed if the desktop asked ('cvbo'); a command of 0
// is the object alone, no header.
void
TDocker::WriteRef(ULong command, RefArg obj)
{
	TObjectWriter writer(obj, *fPipe, false);
	if (command != 0)
	{
		WriteDockerHeader(command, false);
		if (fVBOCompression == 2 || (fIsPackageSoup && fVBOCompression == 1))
			writer.SetCompressLargeBinaries();
		long size = writer.Size();
		*fPipe << size;
		writer.Write();
		Pad(size);
		fPipe->FlushWrite();
	}
	else
		writer.Write();
}


// ROM 0x00094580 ReadInitiateDocking__7TDockerFv
// 'dock': the kind of session the desktop wants.
void
TDocker::ReadInitiateDocking(void)
{
	if (fLength != 4)
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrBadLength, nil);
	unsigned long state;
	*fPipe >> state;
	fState = (long) state;
}


// ROM 0x0009bc08 WriteNewtonName__7TDockerFv
// 'name': the Newton's unique id (made the first time it is asked for),
// what Gestalt says of the machine, the internal store's signature, the
// serial number, the protocol version, and the owner's name.
void
TDocker::WriteNewtonName(void)
{
	RefVar pref(GetPreference(RefVar(RSSYMnewtonuniqueid)));
	ULong uniqueID;
	if (ISNIL(pref))
	{
		long seed = GetRandSeed();
		SetRandSeed(RealClock());
		do
			uniqueID = (ULong) (Long32) (Random() + Random() * 0x10000);
		while (uniqueID == 0 || (Long32) uniqueID == -1);
		SetPreference(RefVar(RSSYMnewtonuniqueid), RefVar(MAKEINT(uniqueID)));
		SetRandSeed(seed);
	}
	else
		uniqueID = RINT(pref);
	RefVar name(GetPreference(RefVar(RSSYMname)));
	static const UniChar kNoName[1] = { 0 };
	const UniChar* nameChars = ISNIL(name) ? kNoName : GetCString(name);
	ULong version = RINT(GetProtoVariable(fConnection, RefVar(RSSYMprotocolversion), nil));
	ULong serialNumber[2] = { 0, 0 };
	GetSystemSerialNumber(serialNumber);

	WriteDockerHeader(kDNewtonName, false);
	long nameLength = Ustrlen(nameChars) * sizeof(UniChar) + sizeof(UniChar);
	*fPipe << (long) (nameLength + 0x4c);
	TUGestalt gestalt;
	// DEVIATION (pointer size): the ROM asks for 0x3c bytes, the class and
	// the date of manufacture after it
	struct { TGestaltSystemInfo info; ULong manufactureDate; } system;
	memset(&system, 0, sizeof(system));
	fError = gestalt.Gestalt(kGestalt_SystemInfo, &system, sizeof(system));
	if (fError != noErr)
		Throw(exLongErrorException, (void*) (intptr_t) fError, nil);
	ULong nsVersion = 0;
	fError = gestalt.Gestalt(kGestalt_NewtonScriptVersion, &nsVersion, sizeof(nsVersion));
	if (fError != noErr)
		Throw(exLongErrorException, (void*) (intptr_t) fError, nil);
	*fPipe << (long) 0x48;
	*fPipe << (unsigned long) uniqueID;
	fManufacturer = system.info.fManufacturer;
	*fPipe << (unsigned long) system.info.fManufacturer;
	fMachineType = system.info.fMachineType;
	*fPipe << (unsigned long) system.info.fMachineType;
	*fPipe << (unsigned long) system.info.fROMVersion;
	*fPipe << (unsigned long) system.info.fROMStage;
	*fPipe << (unsigned long) system.info.fRAMSize;
	// (the height before the width)
	*fPipe << (unsigned long) system.info.fScreenHeight;
	*fPipe << (unsigned long) system.info.fScreenWidth;
	*fPipe << (unsigned long) system.info.fPatchVersion;
	*fPipe << (unsigned long) nsVersion;
	RefVar internal(GetArraySlot(RefVar(GetStores()), 0));
	*fPipe << (long) RINT(StoreGetSignature(internal));
	*fPipe << (long) system.info.fScreenResolution.v;
	*fPipe << (long) system.info.fScreenResolution.h;
	*fPipe << (unsigned long) system.info.fScreenDepth;
	*fPipe << (long) 3;			// (the system update, the ROM says only "3")
	*fPipe << (unsigned long) serialNumber[0];
	*fPipe << (unsigned long) serialNumber[1];
	*fPipe << (unsigned long) version;
	// DEVIATION: the name's UniChars are the device's, big-endian
	UniChar* bigEndian = (UniChar*) NewPtr(nameLength);
	if (bigEndian == nil)
		OutOfMemory();
	for (long i = 0; i < nameLength / (long) sizeof(UniChar); i++)
	{
		UByte* b = (UByte*) &bigEndian[i];
		b[0] = (UByte) (nameChars[i] >> 8);
		b[1] = (UByte) nameChars[i];
	}
	fPipe->WriteChunk(bigEndian, nameLength, false);
	DisposPtr((Ptr) bigEndian);
	Pad(nameLength);
	fPipe->FlushWrite();
}


// ROM 0x000945d8 ReadDesktopInfo__7TDockerFv
// 'dinf': the desktop's protocol version (10 at least), its platform, its
// challenge, the session it wants, whether it can sync selectively and
// (if there is more) its applications; answered by 'ninf' - the version
// both speak and the Newton's own challenge, two random words.
void
TDocker::ReadDesktopInfo(void)
{
	unsigned long word;
	*fPipe >> word;
	fProtocolVersion = word;
	if (fProtocolVersion < 10)
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrProtocolVersion, nil);
	*fPipe >> word;
	fPlatform = (long) word;
	*fPipe >> word;
	fDesktopChallenge[0] = (DESWord) word;
	*fPipe >> word;
	fDesktopChallenge[1] = (DESWord) word;
	*fPipe >> word;
	fState = (long) word;
	*fPipe >> word;
	fSelectiveSyncOK = (Boolean) word;
	if (fLength - 0x18 < 4)
		fDesktopApps = NILREF;
	else
		fDesktopApps = ReadRef(RefVar(NILREF));

	WriteDockerHeader(kDNewtonInfo, false);
	*fPipe << (long) 0xc;
	ULong version = RINT(GetProtoVariable(fConnection, RefVar(RSSYMprotocolversion), nil));
	if (version < fProtocolVersion)
		fProtocolVersion = version;
	*fPipe << (unsigned long) fProtocolVersion;
	SetFrameSlot(fConnection, RSSYMprotocolversion, RefVar(MAKEINT(fProtocolVersion)));
	long seed = GetRandSeed();
	SetRandSeed(RealClock());
	fNewtonChallenge[0] = (DESWord) (Random() + Random() * 0x100);
	fNewtonChallenge[1] = (DESWord) (Random() + Random() * 0x100);
	SetRandSeed(seed);
	*fPipe << (unsigned long) fNewtonChallenge[0];
	*fPipe << (unsigned long) fNewtonChallenge[1];
	fPipe->FlushWrite();
}


// ROM 0x00092c80 SetWhichIcons__7TDockerFv
// 'wicn': which of the Connection application's buttons to show.
void
TDocker::SetWhichIcons(void)
{
	unsigned long icons;
	*fPipe >> icons;
	SetFrameSlot(fConnection, RSSYMwhichicons, RefVar(MAKEINT(icons)));
	WriteResult(noErr);
}


// ROM 0x0009b970 SetTimeout__7TDockerFv
// 'stim' during a session: the pipe's timeout, in seconds.
void
TDocker::SetTimeout(void)
{
	unsigned long seconds;
	*fPipe >> seconds;
	fPipe->SetTimeout(seconds * kSeconds);
	WriteResult(noErr);
}


// ROM 0x000947e8 WritePassword__7TDockerFRC6RefVar
// 'pass': the desktop's challenge encrypted under the password's key (a
// string, or the key itself as an 8-byte binary).  A desktop that sent no
// challenge is told kDockErrNotConnected instead.
void
TDocker::WritePassword(RefArg password)
{
	if (fDesktopChallenge[0] == 0 && fDesktopChallenge[1] == 0)
	{
		WriteDockerHeader(kDResult, false);
		*fPipe << (long) 4;
		*fPipe << (long) kDockErrNotConnected;
		fPipe->FlushWrite();
		return;
	}
	if (IsInstance(password, RSSYMstring))
	{
		LockRef(password);
		DESCharToKey(GetCString(password), fKey);
		UnlockRef(password);
	}
	else if (IsBinary(password))
	{
		LockRef(password);
		const UByte* key = (const UByte*) BinaryData(password);
		fKey[0] = GetBigEndianWord(key);
		fKey[1] = GetBigEndianWord(key + 4);
		UnlockRef(password);
	}
	else
		// ROM BUG: neither a string nor a key: the desktop is told the
		// password is wrong, and then sent the challenge encrypted under
		// whatever key the docker had anyway (and the ROM unlocks nothing
		// it locked, which is harmless)
		WriteResult(kDockErrNotConnected);
	DESWord block[2] = { fDesktopChallenge[0], fDesktopChallenge[1] };
	DESEncodeNonce(fKey, block);
	WriteDockerHeader(kDPassword, false);
	*fPipe << (long) 8;
	*fPipe << (unsigned long) block[0];
	*fPipe << (unsigned long) block[1];
	fPipe->FlushWrite();
}


// ROM 0x00094920 VerifyPassword__7TDockerFv
// The desktop's 'pass': our challenge, encrypted under our key - or, the
// ROM allows, under the empty password's.  Anything else is a wrong
// password (said, and thrown).
void
TDocker::VerifyPassword(void)
{
	if (fLength != 8)
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrBadLength, nil);
	unsigned long word;
	*fPipe >> word;
	DESWord reply[2];
	reply[0] = (DESWord) word;
	*fPipe >> word;
	reply[1] = (DESWord) word;
	DESWord block[2] = { reply[0], reply[1] };
	DESDecodeNonce(fKey, block);
	if (!(fNewtonChallenge[0] == block[0] && fNewtonChallenge[1] == block[1]))
	{
		static const UniChar kNoPassword[2] = { 0, 0 };
		DESWord emptyKey[2];
		DESCharToKey(kNoPassword, emptyKey);
		DESDecodeNonce(emptyKey, reply);
		if (!(fNewtonChallenge[0] == reply[0] && fNewtonChallenge[1] == reply[1]))
		{
			WriteResult(kDockErrNotConnected);
			Throw(exLongErrorException, (void*) (intptr_t) kDockErrNotConnected, nil);
		}
	}
	fSessionAgreed = true;
}


// ROM 0x00094a20 ReadPassword__7TDockerFv
// The desktop's answer to our 'pass': its own ('pass', verified - the ROM
// has VerifyPassword in line), 'pwbd' (ask the user again:
// kDockErrRetryPassword), or a result, which ends the session.
void
TDocker::ReadPassword(void)
{
	ReadDockerHeader(&fCommand, &fLength);
	if (fCommand == kDPassword)
	{
		VerifyPassword();
		return;
	}
	if (fCommand == kDPWWrong)
	{
		fError = kDockErrRetryPassword;
		return;
	}
	long error = kDockErrDesktopError;
	if (fCommand == kDResult)
	{
		error = fError = ReadResult();
		if (error == noErr)
			return;
	}
	Throw(exLongErrorException, (void*) (intptr_t) error, nil);
}


// ROM 0x00094ac4 RetryPassword__7TDockerFRC6RefVar
// The password again, after the desktop said 'pwbd'.  ==> fError.
long
TDocker::RetryPassword(RefArg password)
{
	if (fCleanedUp)
		return noErr;
	WaitAndLockTDocker();
	fError = noErr;
	newton_try
	{
		WritePassword(password);
		ReadPassword();
		fSessionStarted = true;
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	CleanUpIfStopping(false);
	CleanUpIfError(false);
	UnlockTDocker();
	return fError;
}


// ROM 0x0009c150 CheckProtocolExtension__7TDockerFUlRUc
// A command a protocol extension handles: its function called (on the
// protocol frame) with the docker unlocked meanwhile; result says whether
// it answered something other than nil.  ==> whether it was one.
Boolean
TDocker::CheckProtocolExtension(ULong command, Boolean* result)
{
	long index;
	if (fExtensionCommands == nil || (index = fExtensionCommands->Find(command)) < 0)
		return false;
	fDataPending = true;
	Boolean wasLocked = GetTDockerLock();
	UnlockTDocker();
	RefVar fn(GetArraySlot(fExtensions, index));
	*result = NOTNIL(NSCall(fn, fConnection));
	fDataPending = false;
	if (wasLocked)
		WaitAndLockTDocker();
	return true;
}


// ROM 0x0009c1f8 CheckProtocolPatch__7TDockerFUlRUc
// (A command a protocol patch handles: none, in this ROM.)
Boolean
TDocker::CheckProtocolPatch(ULong /*command*/, Boolean* /*result*/)
{
	return false;
}


// ------------------------------------------------------------------------
//	The stores
// ------------------------------------------------------------------------

Ref		FConnBuildStoreFrame(RefArg rcvr, RefArg store, RefArg withInfo);


// ROM 0x00097bbc MakeStoreFrame__7TDockerFRC6RefVar
// What the desktop is told of a store (ConnBuildStoreFrame, with its info).
Ref
TDocker::MakeStoreFrame(RefArg store)
{
	return FConnBuildStoreFrame(RefVar(NILREF), store, RefVar(TRUEREF));
}


// ROM 0x00097c28 WriteStoreNames__7TDockerFv
// 'gsto' -> 'stor': every store's frame.
void
TDocker::WriteStoreNames(void)
{
	RefVar stores(GetStores());
	RefVar frames(AllocateArray(RSSYMarray, Length(stores)));
	RefVar frame;
	long slot = 0;
	RefVar store;
	for (long i = 0; i < Length(stores); i++)
	{
		store = GetArraySlot(stores, i);
		frame = MakeStoreFrame(store);
		SetArraySlot(frames, slot, frame);
		slot++;
	}
	WriteRef('stor', frames);
}


// ROM 0x00098080 ReserveCurrentStore__7TDockerFRC6RefVar
// The store made the current one, marked busy for the application.
void
TDocker::ReserveCurrentStore(RefArg store)
{
	if (NOTNIL(fCurrentStore))
		FreeCurrentStore();
	fCurrentStore = store;
	RefVar appName(GetFrameSlot(fConnection, RSSYMappname));
	RefVar appSymbol(GetFrameSlot(fConnection, RSSYMappsymbol));
	NSSend(fCurrentStore, RefVar(RSSYMmarkbusy), appSymbol, appName);
}


// ROM 0x0009821c SetCurrentStore__7TDockerFUc
// 'ssto' / 'ssgn': the store the desktop names (a frame of its name, kind,
// signature and perhaps info) made the current one - the first of that
// name and kind, if its signature is the one asked for (or none was).
// ==> 'dres' (kDockErrBadStoreSignature for the wrong signature, -28014
// for no such store) or, for 'ssgn', the store's soups ('soup'); and the
// store given the info, if the frame had any.
void
TDocker::SetCurrentStore(Boolean andSoups)
{
	RefVar frame(ReadRef(RefVar(NILREF)));
	RefVar name(GetFrameSlot(frame, RSSYMname));
	RefVar kind(GetFrameSlot(frame, RSSYMkind));
	RefVar signatureRef(GetFrameSlot(frame, RSSYMsignature));
	Boolean hasInfo = FrameHasSlot(frame, RSSYMinfo);
	RefVar info(GetFrameSlot(frame, RSSYMinfo));
	RefVar stores(GetStores());
	RefVar store, storeName, storeKind;
	long signature = RINT(signatureRef);
	Boolean wrongSignature = false;
	TRichString wantedName(name);
	TRichString wantedKind(kind);
	FreeCurrentStore();
	for (short i = 0; i < Length(stores); i++)
	{
		store = GetArraySlot(stores, i);
		storeName = StoreGetName(store);
		storeKind = StoreGetKind(store);
		long storeSignature = RINT(StoreGetSignature(store));
		TRichString thisName(storeName);
		TRichString thisKind(storeKind);
		if (thisName.CompareSubStringCommon(wantedName, 0, -1, false) == 0
		 && thisKind.CompareSubStringCommon(wantedKind, 0, -1, false) == 0)
		{
			wrongSignature = signature != 0 && storeSignature != signature;
			if (!wrongSignature)
				ReserveCurrentStore(store);
			break;
		}
	}
	if (ISNIL(fCurrentStore))
	{
		fError = wrongSignature ? kDockErrBadStoreSignature : kDockErrNoStore;
		WriteResult(fError);
	}
	else if (fError == noErr && andSoups)
		WriteSoupNames();
	else
		WriteResult(fError);
	if (NOTNIL(fCurrentStore))
	{
		StoreFlush(fCurrentStore);
		if (hasInfo)
		{
			if (ISNIL(info))
				info = AllocateFrame();
			StoreSetAllInfo(fCurrentStore, info);
		}
	}
	if (wrongSignature)
		fError = noErr;
}


// ROM 0x000985c0 SetStoreToDefault__7TDockerFv
// 'sdef': the default store made the current one.
void
TDocker::SetStoreToDefault(void)
{
	RefVar store(FDefaultStore(RefVar(NILREF)));
	ReserveCurrentStore(store);
}


// ROM 0x00098610 WriteDefaultStore__7TDockerFv
// 'gdfs' -> 'dfst': the default store's frame.
void
TDocker::WriteDefaultStore(void)
{
	RefVar store(FDefaultStore(RefVar(NILREF)));
	RefVar frame(MakeStoreFrame(store));
	WriteRef('dfst', frame);
}


// ROM 0x0009b9c8 SetStoreSignature__7TDockerFv
// 'ssig': the current store's signature.
void
TDocker::SetStoreSignature(void)
{
	unsigned long signature;
	*fPipe >> signature;
	if (NOTNIL(fCurrentStore))
		StoreSetSignature(fCurrentStore, RefVar(MAKEINT(signature)));
	WriteResult(noErr);
}


// ------------------------------------------------------------------------
//	The soups
// ------------------------------------------------------------------------

// ROM 0x00097d24 WriteSoupNames__7TDockerFv
// 'soup': the current store's soups' names and signatures, two arrays one
// after the other - but not a soup whose soupDef says it belongs to
// 'SystemScratch.
void
TDocker::WriteSoupNames(void)
{
	WriteDockerHeader('soup', false);
	RefVar names(StoreGetSoupNames(fCurrentStore));
	RefVar soup;
	long length = Length(names);
	long count = 0;
	RefVar kept(AllocateArray(RSSYMarray, length));
	RefVar signatures(AllocateArray(RSSYMarray, length));
	RefVar info;
	RefVar name;
	for (long i = 0; i < length; i++)
	{
		name = GetArraySlot(names, i);
		soup = StoreGetSoup(fCurrentStore, name);
		info = SoupGetInfo(soup, RSSYMsoupdef);
		Boolean keep = ISNIL(info);
		if (!keep)
		{
			info = GetFrameSlot(info, RSSYMownerapp);
			keep = ISNIL(info) || !EQRef(info, RSSYMsystemscratch);
		}
		if (keep)
		{
			SetArraySlot(kept, count, name);
			SetArraySlot(signatures, count, RefVar(SoupGetSignature(soup)));
			count++;
		}
	}
	if (count != length)
	{
		SetLength(kept, count);
		SetLength(signatures, count);
	}
	TObjectWriter nameWriter(kept, *fPipe, false);
	TObjectWriter signatureWriter(signatures, *fPipe, false);
	long nameSize = nameWriter.Size();
	long signatureSize = signatureWriter.Size();
	*fPipe << (nameSize + signatureSize);
	nameWriter.Write();
	signatureWriter.Write();
	Pad(nameSize + signatureSize);
	fPipe->FlushWrite();
}


// ROM 0x0009bb1c ReadString__7TDockerFUl
// The command's data (and padding) in a new Ptr (nil for none).
Ptr
TDocker::ReadString(ULong length)
{
	Ptr data = nil;
	if (length != 0)
	{
		data = NewPtr(length);
		if (data == nil)
			OutOfMemory();
		newton_try
		{
			ReadChunk(data, length, true);
		}
		newton_catch_all
		{
			DisposPtr(data);
			rethrow;
		}
		end_try;
	}
	return data;
}


// ROM 0x00098b78 ReadCurrentSoup__7TDockerFv
// The soup the desktop names (a string, the command's data) made the
// current one, in the current store (kDockErrNoCurrentStore without one);
// a soup of 'SystemScratch is refused (kDockErrNoCurrentSoup), none of the
// name is -28015.
void
TDocker::ReadCurrentSoup(void)
{
	Ptr data = ReadString(fLength);
	newton_try
	{
		if (ISNIL(fCurrentStore))
			Throw(exLongErrorException, (void*) (intptr_t) kDockErrNoCurrentStore, nil);
		// DEVIATION: the name's UniChars are the desktop's, big-endian
		long count = fLength / sizeof(UniChar);
		UniChar* name = (UniChar*) data;
		for (long i = 0; i < count; i++)
			name[i] = (UniChar) (((UByte*) data)[i * 2] << 8 | ((UByte*) data)[i * 2 + 1]);
		RefVar soupName(MakeString(name));
		fCurrentSoup = StoreGetSoup(fCurrentStore, soupName);
	}
	newton_catch_all
	{
		DisposPtr(data);
		rethrow;
	}
	end_try;
	DisposPtr(data);
	if (NOTNIL(fCurrentSoup))
	{
		RefVar info(SoupGetInfo(fCurrentSoup, RSSYMsoupdef));
		if (NOTNIL(info))
		{
			info = GetFrameSlot(info, RSSYMownerapp);
			if (NOTNIL(info) && EQRef(info, RSSYMsystemscratch))
			{
				// ROM BUG: the soup is forgotten by storing 0 - the integer
				// nought's Ref - rather than nil, so it is not forgotten:
				// SetupSoup below asks it its name, which throws
				fCurrentSoup = (Ref) 0;
				WriteResult(kDockErrNoCurrentSoup);
			}
		}
		if (NOTNIL(fCurrentSoup))
			SetupSoup();
		return;
	}
	WriteResult(kDockErrNoSuchSoup);
}


// ROM 0x00098730 SetupSoup__7TDockerFv
// The session's notes about the current soup started afresh: which of the
// three special soups it is.
void
TDocker::SetupSoup(void)
{
	fConversionFrame = NILREF;
	fOwnerApp = NILREF;
	fIsDirectorySoup = false;
	fIsSystemSoup = false;
	fIsPackageSoup = false;
	fQuery = NILREF;
	if (ISNIL(fCurrentSoup))
	{
		if (fError == noErr)
			fError = -1;
		return;
	}
	RefVar name(SoupGetName(fCurrentSoup));
	TRichString soupName(name);
	RefVar metaName(Rmetasoupname), systemName(Rsystemsoupname), extrasName(Rextrassoupname);
	TRichString directory(metaName);
	fIsDirectorySoup = directory.CompareSubStringCommon(soupName, 0, -1, false) == 0;
	TRichString system(systemName);
	fIsSystemSoup = system.CompareSubStringCommon(soupName, 0, -1, false) == 0;
	TRichString extras(extrasName);
	fIsPackageSoup = extras.CompareSubStringCommon(soupName, 0, -1, false) == 0;
}


// ROM 0x00097954 VerifySoup__7TDockerFv
// kDockErrNoCurrentSoup unless there is a current soup.
void
TDocker::VerifySoup(void)
{
	if (ISNIL(fCurrentSoup))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrNoCurrentSoup, nil);
}


// ROM 0x00098d0c SetCurrentSoup__7TDockerFUc
// 'ssou' / 'ssgi': the soup made the current one; 'ssgi' answers its info
// ('sinf') where 'ssou' answers a result.
void
TDocker::SetCurrentSoup(Boolean withInfo)
{
	ReadCurrentSoup();
	if (ISNIL(fCurrentSoup))
		return;
	if (!withInfo)
		WriteResult(fError);
	else
		WriteSoupInfo(true);
}


// ROM 0x000996d4 WriteSoupInfo__7TDockerFUc
// 'sinf': the current soup's info frame - with ifChanged only if it
// changed since the time the desktop gave ('stme'), else 'dres' 0.
void
TDocker::WriteSoupInfo(Boolean ifChanged)
{
	VerifySoup();
	if (ifChanged)
	{
		RefVar modTime(SoupGetInfoModTime(fCurrentSoup));
		Boolean changed = ISNIL(modTime) || fDesktopTime <= (ULong) RINT(modTime);
		if (!changed)
		{
			WriteResult(noErr);
			return;
		}
	}
	RefVar info(SoupGetAllInfo(fCurrentSoup));
	WriteRef('sinf', info);
}


// ROM 0x00099790 WriteIndexDescription__7TDockerFUc
// 'indx': the current soup's indexes - with ifChanged only if they changed
// since the desktop's time, else 'dres' 0.
void
TDocker::WriteIndexDescription(Boolean ifChanged)
{
	VerifySoup();
	if (ifChanged)
	{
		RefVar modTime(SoupGetIndexesModTime(fCurrentSoup));
		Boolean changed = ISNIL(modTime) || fDesktopTime <= (ULong) RINT(modTime);
		if (!changed)
		{
			WriteResult(noErr);
			return;
		}
	}
	RefVar indexes(SoupGetIndexes(fCurrentSoup));
	WriteRef('indx', indexes);
}


// ROM 0x00099634 SetSoupInfoFrame__7TDockerFv
// 'sinf' from the desktop: the current soup's info replaced - unless this
// is a restore that keeps the soups' own (fSourceVersion 1: from a 1.x Newton) and the soup has a
// soupDef.
void
TDocker::SetSoupInfoFrame(void)
{
	RefVar info(ReadRef(fCurrentStore));
	VerifySoup();
	Boolean set = true;
	if (fState == kDockStateRestore && fSourceVersion == 1)
		set = ISNIL(SoupGetInfo(fCurrentSoup, RSSYMsoupdef));
	if (ISNIL(info))
		info = AllocateFrame();
	if (set)
		SoupSetAllInfo(fCurrentSoup, info);
}


// ROM 0x0009ba38 SetSoupSignature__7TDockerFv
// 'ssos': the current soup's signature.
void
TDocker::SetSoupSignature(void)
{
	unsigned long signature;
	*fPipe >> signature;
	if (NOTNIL(fCurrentSoup))
		SoupSetSignature(fCurrentSoup, (long) signature);
	WriteResult(noErr);
}


// ------------------------------------------------------------------------
//	TCursorArray
// ------------------------------------------------------------------------

// ROM 0x000940a4 __ct__12TCursorArrayFv
TCursorArray::TCursorArray()
{
	fCursors = AllocateArray(RSSYMarray, 0);
}


// ROM 0x000947b8 __dt__12TCursorArrayFv
TCursorArray::~TCursorArray()
{ }


// ROM 0x00095824 Add__12TCursorArrayFRC6RefVar
// The cursor in the first free slot, or at the end.  ==> its number.
ULong
TCursorArray::Add(RefArg cursor)
{
	ULong length = Length(fCursors);
	for (ULong slot = 0; slot < length; slot++)
		if (ISNIL(GetArraySlot(fCursors, slot)))
		{
			SetArraySlot(fCursors, slot, cursor);
			return slot;
		}
	AddArraySlot(fCursors, cursor);
	return length;
}


// ROM 0x00096cb0 Get__12TCursorArrayFUl
// ==> the cursor of the number, nil if there is none.
Ref
TCursorArray::Get(ULong index)
{
	if (index >= (ULong) Length(fCursors))
		return NILREF;
	return GetArraySlot(fCursors, index);
}


// ROM 0x000966bc Remove__12TCursorArrayFUl
// The number freed: the slot made nil, or the array cut short there when no
// cursor follows it.
void
TCursorArray::Remove(ULong index)
{
	ULong length = Length(fCursors);
	if (index >= length)
		return;
	for (ULong slot = index + 1; slot < length; slot++)
		if (NOTNIL(GetArraySlot(fCursors, slot)))
		{
			SetArraySlot(fCursors, index, RefVar(NILREF));
			return;
		}
	SetLength(fCursors, index);
}


// ------------------------------------------------------------------------
//	The cursors
// ------------------------------------------------------------------------

// ROM 0x00098f3c ValidateQuery__7TDockerFv
// A cursor over the whole current soup, made the first time it is wanted.
void
TDocker::ValidateQuery(void)
{
	if (ISNIL(fQuery))
		fQuery = SoupQuery(fCurrentSoup, RefVar(NILREF));
}


// ROM 0x00092e44 RemoteQuery__7TDockerFv
// 'qury': a cursor the desktop keeps - a frame of the query spec and,
// if it is not the current soup, the soup's name.  ==> 'ldta' its number.
void
TDocker::RemoteQuery(void)
{
	RefVar frame(ReadRef(fCurrentStore));
	RefVar querySpec(GetFrameSlot(frame, RSSYMqueryspec));
	RefVar soupName(GetFrameSlot(frame, RSSYMsoupname));
	if (NOTNIL(soupName) && IsString(soupName) && Ustrlen(GetCString(soupName)) != 0)
	{
		fCurrentSoup = StoreGetSoup(fCurrentStore, soupName);
		if (ISNIL(fCurrentSoup))
		{
			WriteResult(kDockErrNoSuchSoup);
			return;
		}
		SetupSoup();
	}
	VerifySoup();
	RefVar cursor(SoupQuery(fCurrentSoup, querySpec));
	if (fCursors == nil)
	{
		fCursors = new TCursorArray;
		if (fCursors == nil)
			OutOfMemory();
	}
	WriteLong('ldta', fCursors->Add(cursor));
}


// ROM 0x00092fb0 RemoteGetCursor__7TDockerFv
// The cursor the command names (its first word); kDockErrBadCursor if
// there is none of the number.
Ref
TDocker::RemoteGetCursor(void)
{
	unsigned long number;
	*fPipe >> number;
	if (fCursors == nil)
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrBadCursor, nil);
	RefVar cursor(fCursors->Get(number));
	if (ISNIL(cursor))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrBadCursor, nil);
	return cursor;
}


// ROM 0x00093040 RemoteCursorGotoKey__7TDockerFv
// 'goto': the cursor moved to the key.  ==> 'entr' the entry there.
void
TDocker::RemoteCursorGotoKey(void)
{
	RefVar cursor(RemoteGetCursor());
	RefVar key(ReadRef(fCurrentStore));
	RefVar entry(CursorGotoKey(cursor, key));
	WriteEntry('entr', entry);
}


// ROM 0x000930b8 RemoteCursorMap__7TDockerFv
// 'cmap': a function mapped over the cursor's entries (MapCursor).
// ==> 'ref ' what it answers.
void
TDocker::RemoteCursorMap(void)
{
	RefVar cursor(RemoteGetCursor());
	RefVar fn(ReadRef(fCurrentStore));
	RefVar result(NSCallGlobalFn(RefVar(RSSYMmapcursor), cursor, fn));
	WriteRef('ref ', result);
}


// ROM 0x00093138 RemoteCursorEntry__7TDockerFv
// 'crsr': ==> 'entr' the cursor's entry (as it is: no directory
// expansion, unlike 'goto').
void
TDocker::RemoteCursorEntry(void)
{
	RefVar cursor(RemoteGetCursor());
	RefVar entry(CursorEntry(cursor));
	WriteRef('entr', entry);
}


// ROM 0x00093190 RemoteCursorMove__7TDockerFv
// 'move': the cursor moved so many entries.  ==> 'entr'.
void
TDocker::RemoteCursorMove(void)
{
	RefVar cursor(RemoteGetCursor());
	unsigned long count;
	*fPipe >> count;
	RefVar entry(CursorMove(cursor, (long) (Long32) count));
	WriteRef('entr', entry);
}


// ROM 0x000931f8 RemoteCursorNext__7TDockerFv
// 'next': ==> 'entr' the next entry.
void
TDocker::RemoteCursorNext(void)
{
	RefVar cursor(RemoteGetCursor());
	RefVar entry(CursorNext(cursor));
	WriteRef('entr', entry);
}


// ROM 0x00093250 RemoteCursorPrev__7TDockerFv
// 'prev': ==> 'entr' the previous entry.
void
TDocker::RemoteCursorPrev(void)
{
	RefVar cursor(RemoteGetCursor());
	RefVar entry(CursorPrev(cursor));
	WriteRef('entr', entry);
}


// ROM 0x000932a8 RemoteCursorReset__7TDockerFv
// 'rset'
void
TDocker::RemoteCursorReset(void)
{
	RefVar cursor(RemoteGetCursor());
	CursorReset(cursor);
	WriteResult(noErr);
}


// ROM 0x00093330 RemoteCursorResetToEnd__7TDockerFv
// 'rend'
void
TDocker::RemoteCursorResetToEnd(void)
{
	RefVar cursor(RemoteGetCursor());
	CursorResetToEnd(cursor);
	WriteResult(noErr);
}


// ROM 0x00093370 RemoteCursorCountEntries__7TDockerFv
// 'cnt ': ==> 'ldta' how many entries the cursor has.
void
TDocker::RemoteCursorCountEntries(void)
{
	RefVar cursor(RemoteGetCursor());
	WriteLong('ldta', RINT(CursorCountEntries(cursor)));
}


// ROM 0x000933c8 RemoteCursorWhichEnd__7TDockerFv
// 'whch': ==> 'ldta' which end the cursor is off - 0 neither, 1 the
// beginning, 2 the end.
void
TDocker::RemoteCursorWhichEnd(void)
{
	RefVar cursor(RemoteGetCursor());
	RefVar end(CursorWhichEnd(cursor));
	ULong which = 0;
	if (NOTNIL(end))
	{
		which = EQRef(end, RSSYMbegin) ? 1 : 0;
		if (which == 0 && EQRef(end, RSSYMend))
			which = 2;
	}
	WriteLong('ldta', which);
}


// ROM 0x00093470 RemoteCursorFree__7TDockerFv
// 'cfre': the cursor's number freed.
void
TDocker::RemoteCursorFree(void)
{
	unsigned long number;
	*fPipe >> number;
	if (fCursors != nil)
		fCursors->Remove(number);
	WriteResult(noErr);
}


// ------------------------------------------------------------------------
//	The entries
// ------------------------------------------------------------------------

// ROM 0x00099fd8 GetEntryFromID__7TDockerFUl
// The current soup's entry of the unique id, or nil.
Ref
TDocker::GetEntryFromID(ULong id)
{
	VerifySoup();
	ValidateQuery();
	return CursorGotoKey(fQuery, RefVar(MAKEINT(id)));
}


// ROM 0x00099f6c WriteEntry__7TDockerFUlRC6RefVar
// An entry sent - a directory (metasoup) entry expanded first.
void
TDocker::WriteEntry(ULong command, RefArg entry)
{
	RefVar obj(entry);
	if (fIsDirectorySoup)
		obj = NSCallGlobalFn(RefVar(RSSYMexpanddirectoryentry), entry);
	WriteRef(command, obj);
}


// ROM 0x0009b1b4 ReturnEntry__7TDockerFUl
// 'rete' ('entr') / 'rcen' ('cent'): the entry of the id sent back
// (kDockErrNoSuchEntry if there is none).
void
TDocker::ReturnEntry(ULong command)
{
	unsigned long id;
	*fPipe >> id;
	RefVar entry(GetEntryFromID(id));
	if (ISNIL(entry))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrNoSuchEntry, nil);
	else
		WriteEntry(command, entry);
}


// ROM 0x0009a600 IsDuplicateEntry__7TDockerFRC6RefVar
// For a selective restore: whether an entry like this one (its id and
// time taken out) is already in the current soup - looked for by the first
// of the soup's indexes that is not a tags or multi-slot index nor one of
// the slots the soup's entries may differ in (the ROM's table 0x00472c1d,
// by soup name: Notes' timestamp), else by unique id, each candidate
// compared slot by slot (ConnectionDupValidTest -> ConnEntriesEqual).
Boolean
TDocker::IsDuplicateEntry(RefArg entry)
{
	RemoveSlot(entry, RSSYM_uniqueid);
	RemoveSlot(entry, RSSYM_modtime);
	RefVar querySpec(AllocateFrame());
	RefVar soupName(SoupGetName(fCurrentSoup));
	char name[256];
	ConvertFromUnicode(GetCString(soupName), name, kMacRomanEncoding, 0x7fffffff);
	// ROM 0x00472c1d (object) the slots each soup's duplicates may differ in
	RefVar ignored(TranslateROMRef(0x00472c1d));
	fIgnoredSlots = GetFrameSlot(ignored, RefVar(Intern(name)));
	RefVar indexes(SoupGetIndexes(fCurrentSoup));
	RefVar path;
	if (NOTNIL(indexes) && Length(indexes) > 0)
	{
		RefVar index, type, structure;
		for (long i = 0; i < Length(indexes); i++)
		{
			index = GetArraySlot(indexes, i);
			type = GetFrameSlot(index, RSSYMtype);
			structure = GetFrameSlot(index, RSSYMstructure);
			if (!EQRef(type, RSSYMtags) && !EQRef(structure, RSSYMmultislot))
			{
				path = GetFrameSlot(GetArraySlot(indexes, i), RSSYMpath);
				if (NOTNIL(fIgnoredSlots))
					for (long j = 0; j < Length(fIgnoredSlots); j++)
						if (EQRef(path, GetArraySlot(fIgnoredSlots, j)))
						{
							path = NILREF;
							break;
						}
			}
			if (NOTNIL(path))
				break;
		}
	}
	if (ISNIL(path))
		SetFrameSlot(querySpec, RSSYMindexpath, RSSYM_uniqueid);
	else
	{
		SetFrameSlot(querySpec, RSSYMindexpath, path);
		RefVar key(GetFramePath(entry, path));
		SetFrameSlot(querySpec, RSSYMbeginkey, key);
		SetFrameSlot(querySpec, RSSYMendkey, key);
	}
	gTheDocker = this;
	SetFrameSlot(querySpec, RSSYMvalidtest, RefVar(Rconnectiondupvalidtest));
	fDuplicateOf = entry;
	RefVar cursor(SoupQuery(fCurrentSoup, querySpec));
	Boolean duplicate = false;
	if (NOTNIL(cursor))
		duplicate = NOTNIL(CursorEntry(cursor));
	gTheDocker = nil;
	fIgnoredSlots = NILREF;
	return duplicate;
}


// ROM 0x0009a5b0 EntriesEqual__7TDockerFRC6RefVar
// A soup entry the same as the one being restored: the same slots but its
// own _uniqueID and _modTime (FramesEqual's ROM code, branched into).
Boolean
TDocker::EntriesEqual(RefArg candidate)
{
	long difference = FrameHasSlot(candidate, RSSYM_modtime) ? -2 : -1;
	return FramesEqual(candidate, fDuplicateOf, difference);
}


// ROM 0x0009a128 RefsEqual__7TDockerFRC6RefVarT1
// Two values the same: frames slot by slot (FramesEqual), arrays element by
// element, binaries byte by byte (a word at a time when their length is a
// multiple of four), anything else by identity.
Boolean
TDocker::RefsEqual(RefArg a, RefArg b)
{
	if (IsFrame(a))
	{
		if (IsFrame(b))
			return FramesEqual(a, b, 0);
		return false;
	}
	if (IsArray(a))
	{
		if (IsArray(b) && Length(a) == Length(b))
		{
			RefVar x, y;
			for (long i = 0; i < Length(a); i++)
			{
				y = GetArraySlot(b, i);
				x = GetArraySlot(a, i);
				if (!RefsEqual(x, y))
					return false;
			}
			return true;
		}
		return false;
	}
	if (IsBinary(a))
	{
		if (IsBinary(b))
		{
			ULong length = Length(a);
			if ((ULong) Length(b) == length)
			{
				LockRef(a);
				LockRef(b);
				Boolean equal = memcmp(BinaryData(a), BinaryData(b), length) == 0;
				UnlockRef(a);
				UnlockRef(b);
				return equal;
			}
		}
		return false;
	}
	return (Ref) a == (Ref) b;
}


// ROM 0x0009a39c FramesEqual__7TDockerFRC6RefVarT1Ul
// Two frames the same: b's slots (its _proto aside, and the slots the
// soup's entries may differ in) all in a, each equal (RefsEqual), and a
// having that many slots more or less slotDifference.
Boolean
TDocker::FramesEqual(RefArg a, RefArg b, long slotDifference)
{
	if (FrameHasSlot(a, RSSYM_proto))
		slotDifference--;
	if (FrameHasSlot(b, RSSYM_proto))
		slotDifference++;
	if (Length(a) + slotDifference != Length(b))
		return false;
	TObjectIterator iter(b, false);
	RefVar tag, x;
	for ( ; !iter.Done(); iter.Next())
	{
		tag = iter.Tag();
		if (EQRef(tag, RSSYM_proto))
			continue;
		Boolean ignored = false;
		if (NOTNIL(fIgnoredSlots))
			for (long i = 0; i < Length(fIgnoredSlots); i++)
				if (EQRef(tag, GetArraySlot(fIgnoredSlots, i)))
				{
					ignored = true;
					break;
				}
		if (ignored)
			continue;
		if (!FrameHasSlot(a, tag))
			return false;
		x = GetFrameSlot(a, tag);
		RefVar value(iter.Value());
		if (!RefsEqual(x, value))
			return false;
	}
	return true;
}


// ROM 0x0009aa08 ConvertEntry__7TDockerFRC6RefVar
// An entry from a 1.x Newton (or of an unknown version) converted by the
// conversion frame of the application that owns the soup (the System and
// directory soups' own, else GetOwnerApp's) through ConvertFrame; an entry
// no application converts is taken as it is.  A conversion that throws is
// noted ('conversionError) and the entry dropped.
Ref
TDocker::ConvertEntry(RefArg entry)
{
	RefVar fromVersion(fSourceVersion == 1 ? RSSYMoneo : RSSYMunknown);
	RefVar result;
	newton_try
	{
		if (ISNIL(fOwnerApp) && ISNIL(fConversionFrame))
		{
			if (fIsSystemSoup)
				fOwnerApp = RSSYMsystem;
			else if (fIsDirectorySoup)
				fOwnerApp = RSSYMsystemdirectory;
			if (ISNIL(fOwnerApp))
			{
				RefVar name(SoupGetName(fCurrentSoup));
				fOwnerApp = NSCallGlobalFn(RefVar(RSSYMgetownerapp), name);
			}
			if (ISNIL(fOwnerApp))
				// (the integer nought, not nil: the question is not asked
				// again, and nought is not a symbol, so nothing converts)
				fOwnerApp = (Ref) 0;
			else
				fConversionFrame = NSCallGlobalFn(RefVar(RSSYMgetconversionframe), fOwnerApp);
		}
		if (ISNIL(fConversionFrame) || !IsSymbol(fOwnerApp))
			result = entry;
		else
		{
			RefVar fn(NSGetGlobalFn(RefVar(RSSYMconvertframe)));
			result = NSCall(fn, fOwnerApp, fConversionFrame, fromVersion, RefVar(RSSYMtwoo), entry, RefVar(TRUEREF));
		}
	}
	newton_catch_all
	{
		result = NILREF;
		AddChangedSoup(RefVar(RSSYMconversionerror), 1);
	}
	end_try;
	return result;
}


// ROM 0x0009ac3c AddEntry__7TDockerFUc
// 'adde' / 'auni': an entry the desktop sends added to the current soup
// (with its own unique id, for 'auni', if it has one).  An entry from a
// 1.x Newton is converted first; a System soup entry from another kind of
// Newton goes through Restore2.0SystemEntry, and the user configuration
// entry replaces the machine's own (its password dropped) and the globals
// are loaded again.  ==> 'adid' the new id ('adde') or 'dres' ('auni').
void
TDocker::AddEntry(Boolean withUniqueID)
{
	VerifySoup();
	RefVar entry(ReadRef(fCurrentStore));
	if (ISNIL(entry))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrNoEntry, nil);
	Boolean reloadGlobals = false;
	Boolean converted = false;
	long id = -1;
	if (fSourceVersion != 2)
	{
		converted = true;
		entry = ConvertEntry(entry);
	}
	else if (fIsSystemSoup)
	{
		if (!(fMachineType == fSourceMachineType && fManufacturer == fSourceManufacturer))
			entry = NSCallGlobalFn(RefVar(RSSYMrestore2_2E0systementry), entry);
		if (NOTNIL(entry))
		{
			RefVar tag(GetFrameSlot(entry, RSSYMtag));
			if (NOTNIL(tag))
			{
				RefVar userConfiguration(MakeString("Userconfiguration"));
				TRichString wanted(userConfiguration);
				TRichString given(tag);
				if (given.CompareSubStringCommon(wanted, 0, -1, false) == 0)
				{
					RefVar current(GetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration));
					if (IsSoupEntry(current))
						EntryRemoveFromSoup(current);
					RemoveSlot(entry, RSSYMusepassword);
					RemoveSlot(entry, RSSYMpasswordkey);
					reloadGlobals = true;
				}
			}
		}
	}
	Boolean add = NOTNIL(entry);
	if (add && fHasArg1)
		add = !IsDuplicateEntry(entry);
	if (add)
	{
		newton_try
		{
			if (withUniqueID && FrameHasSlot(entry, RSSYM_uniqueid))
				SoupAddFlushedWithUniqueId(fCurrentSoup, entry);
			else
			{
				SoupAddFlushed(fCurrentSoup, entry);
				id = EntryUniqueID(entry);
			}
		}
		newton_catch_all
		{
			if (converted)
				AddChangedSoup(RefVar(RSSYMconversionerror), 1);
			else
				rethrow;
		}
		end_try;
	}
	if (reloadGlobals)
		NSCall(RefVar(Rloadglobals));
	if (withUniqueID)
		WriteResult(fError);
	else
		WriteLong('adid', id);
	if (add)
		AddChangedSoup(RefVar(RSSYMadded), 1);
}


// ROM 0x0009afd4 ReplaceEntryContents__7TDockerFRC6RefVar
// The current soup's entry of the new contents' _uniqueID replaced by them,
// keeping the modification time they carry (kDockErrEntryNotFound if
// there is no such entry).
void
TDocker::ReplaceEntryContents(RefArg entry)
{
	RefVar id(GetFrameSlot(entry, RSSYM_uniqueid));
	if (ISNIL(id))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrEntryNotFound, nil);
	RefVar current(GetEntryFromID(RINT(id)));
	if (ISNIL(current))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrEntryNotFound, nil);
	SetFrameSlot(entry, RSSYM_modtime, RefVar(MAKEINT(RINT(GetFrameSlot(entry, RSSYM_modtime)))));
	EntryReplaceWithModTime(current, entry);
}


// ROM 0x0009b128 ChangeEntry__7TDockerFv
// 'cent': an entry the desktop changed put back.
void
TDocker::ChangeEntry(void)
{
	VerifySoup();
	RefVar entry(ReadRef(fCurrentStore));
	if (ISNIL(entry))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrNoEntry, nil);
	ReplaceEntryContents(entry);
	WriteResult(noErr);
	AddChangedSoup(RefVar(RSSYMchanged), 1);
}


// ROM 0x0009a058 DeleteEntries__7TDockerFv
// 'dele': a count and that many unique ids, each such entry of the current
// soup removed.
void
TDocker::DeleteEntries(void)
{
	VerifySoup();
	UByte* ids = (UByte*) NewPtr(fLength);
	if (ids == nil)
		OutOfMemory();
	long deleted = 0;
	ReadChunk(ids, fLength, true);
	RefVar entry;
	// DEVIATION: the words are the desktop's, big-endian
	ULong count = GetBigEndianWord(ids);
	for (ULong i = 0; i < count; i++)
	{
		entry = GetEntryFromID(GetBigEndianWord(ids + 4 + i * 4));
		if (NOTNIL(entry))
		{
			deleted++;
			EntryRemoveFromSoup(entry);
		}
	}
	DisposPtr((Ptr) ids);
	WriteResult(noErr);
	if (deleted != 0)
		AddChangedSoup(RefVar(RSSYMdeleted), deleted);
}


// ROM 0x0009b238 EmptyOrDelete__7TDockerFUl
// 'esou' / 'dsou': the current soup emptied, or removed from its store -
// the directory, for a 1.x restore, emptied of all but the built-in soups
// instead; the packages soup's packages zapped for a full restore instead;
// the globals loaded again after the System soup.
void
TDocker::EmptyOrDelete(ULong command)
{
	VerifySoup();
	Boolean plain = true;
	if (fSourceVersion == 1 && fIsDirectorySoup)
	{
		NSCallGlobalFn(RefVar(RSSYMremallbutbuiltinfromdir));
		plain = false;
	}
	if (!fIsPackageSoup)
	{
		if (plain)
		{
			if (command == 'dsou')
				SoupRemoveFromStore(fCurrentSoup);
			else
				SoupRemoveAllEntries(fCurrentSoup);
			if (fIsSystemSoup)
				NSCall(RefVar(Rloadglobals));
		}
	}
	else
	{
		NSCallGlobalFn(RefVar(RSSYMzappackagesforfullrestore), fCurrentStore);
		plain = false;
	}
	WriteResult(noErr);
	if (plain || fIsDirectorySoup)
		AddChangedSoup(RefVar(RSSYMemptied), 1);
}


// ROM 0x00098684 AddChangedSoup__7TDockerFRC6RefVarUl
// What happened to the current soup noted for BroadcastChanges
// (ConnAddChangedSoup(changes, name, kind, count)).
void
TDocker::AddChangedSoup(RefArg change, ULong count)
{
	if (ISNIL(fCurrentSoup))
		return;
	RefVar name(SoupGetName(fCurrentSoup));
	if (ISNIL(fSyncChanges))
		fSyncChanges = AllocateArray(RSSYMarray, 0);
	// (the ROM's four-argument NSCallGlobalFn, which the host has not got)
	RefVar fn(NSGetGlobalFn(RefVar(RSSYMconnaddchangedsoup)));
	NSCall(fn, fSyncChanges, name, change, RefVar(MAKEINT(count)));
}


// ROM 0x0009bba8 ReadSourceVersion__7TDockerFv
// 'sver': the version of the Newton the data comes from, and (if the
// command is long enough) its manufacturer and machine.
void
TDocker::ReadSourceVersion(void)
{
	unsigned long word;
	*fPipe >> word;
	fSourceVersion = (long) word;
	if (fLength < 5)
	{
		fSourceManufacturer = 0;
		fSourceMachineType = 0;
	}
	else
	{
		*fPipe >> word;
		fSourceManufacturer = word;
		*fPipe >> word;
		fSourceMachineType = word;
	}
	WriteResult(noErr);
}


// ROM 0x00098d54 ShouldBackupEntry__7TDockerFRC6RefVar
// A System soup entry whose backupInfo says 'dontBackup is left out.
Boolean
TDocker::ShouldBackupEntry(RefArg entry)
{
	if (!fIsSystemSoup)
		return true;
	RefVar info(GetFrameSlot(entry, RSSYMbackupinfo));
	return ISNIL(info) || !EQRef(info, RSSYMdontbackup);
}


// ROM 0x00098e98 GetSoupIDCount__7TDockerFRC6RefVar
// How many of the cursor's entries are backed up.
long
TDocker::GetSoupIDCount(RefArg cursor)
{
	if (fIsSystemSoup)
	{
		long count = 0;
		RefVar entry(CursorEntry(cursor));
		while (NOTNIL(entry))
		{
			if (ShouldBackupEntry(entry))
				count++;
			entry = CursorNext(cursor);
		}
		CursorReset(cursor);
		return count;
	}
	return RINT(CursorCountEntries(cursor));
}


// ROM 0x0009984c WriteSoupIDs__7TDockerFv
// 'gids' -> 'sids': the unique ids of the entries backed up (for the
// packages soup, those of the packages a backup takes), thirty at a time;
// and, if the desktop gave its time, the ones changed since noted for
// 'gcid'.
void
TDocker::WriteSoupIDs(void)
{
	VerifySoup();
	RefVar cursor;
	if (!fIsPackageSoup)
	{
		ValidateQuery();
		CursorReset(fQuery);
		cursor = fQuery;
	}
	else
		cursor = NSCallGlobalFn(RefVar(RSSYMgetbackupallpackagescursor), fCurrentStore);
	WriteDockerHeader('sids', false);
	ULong count = ISNIL(cursor) ? 0 : GetSoupIDCount(cursor);
	*fPipe << (unsigned long) (count * 4 + 4);
	*fPipe << (unsigned long) count;
	if (fChangedIDs != nil)
	{
		delete fChangedIDs;
		fChangedIDs = nil;
	}
	if (ISNIL(cursor))
	{
		fPipe->FlushWrite();
		return;
	}
	RefVar entry(CursorEntry(cursor));
	UByte ids[30 * 4];
	long n = 0;
	while (NOTNIL(entry))
	{
		if (ShouldBackupEntry(entry))
		{
			// DEVIATION: the words written big-endian, as the ROM's are
			PutBigEndianWord(ids + n * 4, (unsigned int) EntryUniqueID(entry));
			if (++n == 30)
			{
				n = 0;
				fPipe->WriteChunk(ids, sizeof(ids), false);
			}
		}
		entry = CursorNext(cursor);
	}
	if (n != 0)
		fPipe->WriteChunk(ids, n * 4, false);
	fPipe->FlushWrite();
	if (fDesktopTime != 0)
	{
		CursorReset(cursor);
		for (entry = CursorEntry(cursor); NOTNIL(entry); entry = CursorNext(cursor))
		{
			if (fDesktopTime <= (ULong) EntryModTime(entry) && ShouldBackupEntry(entry))
			{
				if (fChangedIDs == nil)
				{
					fChangedIDs = new TDockerDynArray;
					if (fChangedIDs == nil)
						OutOfMemory();
				}
				if (fChangedIDs->Add(EntryUniqueID(entry)) != noErr)
					// ROM BUG: the id is added a second time to find the
					// error to throw
					Throw(exLongErrorException, (void*) (intptr_t) fChangedIDs->Add(EntryUniqueID(entry)), nil);
			}
		}
	}
}


// ROM 0x00099b24 WriteChangedIDs__7TDockerFv
// 'gcid' -> 'cids': the ids 'gids' found changed since the desktop's time
// (then forgotten), or none.
void
TDocker::WriteChangedIDs(void)
{
	WriteDockerHeader('cids', false);
	if (fChangedIDs != nil && fChangedIDs->fCount != 0 && fChangedIDs->fWords != nil)
	{
		HLock(fChangedIDs->fWords);
		ULong count = fChangedIDs->fCount;
		*fPipe << (unsigned long) (count * 4 + 4);
		*fPipe << (unsigned long) count;
		// DEVIATION: the words written big-endian, as the ROM's are
		for (ULong i = 0; i < count; i++)
			*fPipe << (unsigned long) ((ULong*) *fChangedIDs->fWords)[i];
		delete fChangedIDs;
		fChangedIDs = nil;
	}
	else
	{
		*fPipe << (long) 4;
		*fPipe << (long) 0;
	}
	fPipe->FlushWrite();
}


// ------------------------------------------------------------------------
//	Making, sending and backing up soups
// ------------------------------------------------------------------------

// DEVIATION: a soup name the desktop sends is big-endian UniChars; the host
// turns it round in place
static UniChar*
DeviceString(Ptr data, ULong length)
{
	UniChar* chars = (UniChar*) data;
	for (ULong i = 0; i < length / sizeof(UniChar); i++)
		chars[i] = (UniChar) (((UByte*) data)[i * 2] << 8 | ((UByte*) data)[i * 2 + 1]);
	return chars;
}


// ROM 0x000988a4 CreateSoup__7TDockerFv
// 'csop': a soup of the name (its length first) with the indexes that
// follow made on the current store, unless there is one, and made the
// current soup.  ==> 'dres' (with none, only fError: kDockErrNoCurrentSoup).
void
TDocker::CreateSoup(void)
{
	unsigned long length;
	*fPipe >> length;
	Ptr data = ReadString(length);
	newton_try
	{
		RefVar name(MakeString(DeviceString(data, length)));
		RefVar indexes(ReadRef(fCurrentStore));
		fCurrentSoup = StoreGetSoup(fCurrentStore, name);
		if (ISNIL(fCurrentSoup))
		{
			fCurrentSoup = StoreCreateSoup(fCurrentStore, name, indexes);
			if (NOTNIL(fCurrentSoup))
				AddChangedSoup(RefVar(RSSYMchanged), 1);
		}
	}
	newton_catch_all
	{
		DisposPtr(data);
		rethrow;
	}
	end_try;
	DisposPtr(data);
	if (ISNIL(fCurrentSoup))
		fError = kDockErrNoCurrentSoup;
	else
	{
		SetupSoup();
		WriteResult(fError);
	}
}


// ROM 0x00098a00 CreateSoupFromSoupDef__7TDockerFv
// 'cdsp': the soup of the name made from its registered soupDef
// (CreateUSoupMember), unless there is one; a soup no soupDef describes is
// not an error here.  ==> 'dres' (-28015 for no soup).
void
TDocker::CreateSoupFromSoupDef(void)
{
	Ptr data = ReadString(fLength);
	newton_try
	{
		newton_try
		{
			RefVar name(MakeString(DeviceString(data, fLength)));
			fCurrentSoup = StoreGetSoup(fCurrentStore, name);
			if (ISNIL(fCurrentSoup))
			{
				fCurrentSoup = NSCallGlobalFn(RefVar(RSSYMcreateusoupmember), name, fCurrentStore);
				if (NOTNIL(fCurrentSoup))
					AddChangedSoup(RefVar(RSSYMchanged), 1);
			}
		}
		newton_catch_all
		{
			DisposPtr(data);
			data = nil;
			rethrow;
		}
		end_try;
		DisposPtr(data);
		data = nil;
	}
	newton_catch(exNoSoupDefException)
	{ }
	end_try;
	long result;
	if (ISNIL(fCurrentSoup))
		result = kDockErrNoSuchSoup;
	else
	{
		SetupSoup();
		result = fError;
	}
	WriteResult(result);
}


// ROM 0x00099040 GetBackupCursor__7TDockerFv
// A cursor over what a backup of the current soup takes: its entries, from
// the start, or for the packages soup the packages GetBackupAllPackagesCursor
// answers.
Ref
TDocker::GetBackupCursor(void)
{
	RefVar cursor;
	if (ISNIL(fCurrentSoup))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrNoCurrentSoup, nil);
	else if (!fIsPackageSoup)
	{
		ValidateQuery();
		CursorReset(fQuery);
		cursor = fQuery;
	}
	else
		cursor = NSCallGlobalFn(RefVar(RSSYMgetbackupallpackagescursor), fCurrentStore);
	return cursor;
}


// ROM 0x00098f94 CheckCancel__7TDockerFRUl
// During a long send: what the desktop sent read - 'opca' cancels it
// (acknowledged, and kDockErrDisconnected thrown), anything else is thrown
// away.
// ROM BUG: it means to look at most every 90 ticks, but returns when 90
// ticks HAVE passed since the last look - and the last look is only
// recorded when it looks - so after the machine's first second and a half
// it never looks at all, and a desktop cannot cancel a backup.
void
TDocker::CheckCancel(ULong* lastLook)
{
	ULong now = Ticks();
	if (*lastLook + 90 <= now)
		return;
	*lastLook = now;
	if (BytesAvailable(true) == 0)
		return;
	ReadDockerHeader(&fCommand, &fLength);
	if (fCommand != kDOperationCanceled)
	{
		FlushCommand();
		return;
	}
	Boolean done = false, operationDone = false;
	ProcessCommand(&done, &operationDone);
	Throw(exLongErrorException, (void*) (intptr_t) kDockErrDisconnected, nil);
}


// ROM 0x000990e0 SendSoup__7TDockerFv
// 'snds': every entry a backup takes sent ('entr' each), then 'bsdn'.
void
TDocker::SendSoup(void)
{
	RefVar cursor(GetBackupCursor());
	if (NOTNIL(cursor))
	{
		ULong lastLook = 0;
		for (RefVar entry(CursorEntry(cursor)); NOTNIL(entry); entry = CursorNext(cursor))
		{
			CheckCancel(&lastLook);
			if (ShouldBackupEntry(entry))
				WriteEntry('entr', entry);
		}
	}
	WriteDockerHeader('bsdn', true);
	ClearSoupDirty();
}


// ROM 0x000991bc FinishSequence__7TDockerFRss
// A run of ids in a 'bids' closed: the count of consecutive ids after its
// first, negated, if there were any, and then the value (the next id, or
// 0x8000 to end the list).
void
TDocker::FinishSequence(short* count, short value)
{
	if (*count > 0)
	{
		*fPipe << (short) -*count;
		*count = 0;
	}
	*fPipe << value;
}


// ROM 0x00099218 SoupChangedSinceLastBackup__7TDockerFv
// Whether the current soup must be backed up: not if it was backed up at
// the desktop's time (or a minute later) and has not been dirtied since,
// in which case the desktop is told 'ndir'.
Boolean
TDocker::SoupChangedSinceLastBackup(void)
{
	Boolean changed = true;
	if (ISNIL(fCurrentSoup))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrNoCurrentSoup, nil);
	RefVar last(SoupGetInfo(fCurrentSoup, RSSYMncklastbackuptime));
	if (NOTNIL(last))
	{
		ULong lastBackup = RINT(last);
		if (fDesktopTime == lastBackup || fDesktopTime == lastBackup + 1)
		{
			RefVar flags(SoupGetFlags(fCurrentSoup));
			if (NOTNIL(flags) && (RINT(flags) & 2) == 0)
			{
				changed = false;
				WriteDockerHeader('ndir', true);
			}
		}
	}
	return changed;
}


// ROM 0x00099310 ClearSoupDirty__7TDockerFv
// The current soup noted as backed up at the time the desktop set, and its
// dirty flag cleared (not on a read-only store).
void
TDocker::ClearSoupDirty(void)
{
	if (ISNIL(StoreIsReadOnly(fCurrentStore)))
	{
		SoupSetInfo(fCurrentSoup, RSSYMncklastbackuptime, RefVar(MAKEINT(fTimeSet)));
		RefVar flags(SoupGetFlags(fCurrentSoup));
		if (NOTNIL(flags))
			SoupSetFlags(fCurrentSoup, RefVar(MAKEINT(RINT(flags) & ~2)));
	}
}


// ROM 0x000993c8 BackupSoup__7TDockerFv
// 'bksp': the current soup backed up, if it changed since the last backup
// - an entry changed since the desktop's time (or with an id above the one
// the desktop gave, if it gave one) sent whole ('entr'), and the ids of
// the others sent as runs ('bids': a first id, then for a run of the ids
// after it their count negated, the next id, ..., and 0x8000 to end;
// 'base' first when an id does not fit in a short); then 'bsdn'.
// ROM BUG: the base 'base' announces is never kept - the ids after it are
// still written less nought - so an id above 0x7fff is sent as its low
// sixteen bits, and every later id makes another 'base'.
void
TDocker::BackupSoup(void)
{
	unsigned long maxID = 0x7fffffff;
	if (fLength == 4)
		*fPipe >> maxID;
	if (SoupChangedSinceLastBackup())
	{
		RefVar cursor(GetBackupCursor());
		if (NOTNIL(cursor))
		{
			ULong lastLook = 0;
			short count = 0;
			Boolean inSequence = false;
			ULong previous = 0;
			const ULong base = 0;
			ULong id = 0;
			for (RefVar entry(CursorEntry(cursor)); NOTNIL(entry); entry = CursorNext(cursor))
			{
				CheckCancel(&lastLook);
				if (!ShouldBackupEntry(entry))
					continue;
				if ((ULong) EntryModTime(entry) < fDesktopTime && (ULong) EntryUniqueID(entry) <= maxID)
				{
					id = EntryUniqueID(entry);
					Boolean start = !inSequence;
					if (inSequence)
					{
						if (previous + 1 == id && count < 0x7fff)
							count++;
						else if (id - base > 0x7fff)
						{
							FinishSequence(&count, (short) -0x8000);
							WriteDockerHeader('base', false);
							*fPipe << (unsigned long) id;
							start = true;
						}
						else
							FinishSequence(&count, (short) (id - base));
					}
					if (start)
					{
						WriteDockerHeader('bids', false);
						*fPipe << (unsigned long) 0xffffffff;
						*fPipe << (short) (id - base);
						inSequence = true;
						count = 0;
					}
					previous = id;
				}
				else
				{
					if (inSequence)
					{
						inSequence = false;
						FinishSequence(&count, (short) -0x8000);
					}
					WriteEntry('entr', entry);
				}
			}
			if (inSequence)
				FinishSequence(&count, (short) -0x8000);
		}
		WriteDockerHeader('bsdn', true);
	}
	ClearSoupDirty();
}


// ROM 0x0009b40c GetPackageInfo__7TDockerFv
// 'gpin' -> 'pinf': a frame for each package on the current store (or
// only the one of the name the desktop gives) - its name, size, id,
// version, source (format, device kind, number and id), modification
// time, whether it is copy-protected, the length of its name in bytes, and
// whether it may be removed now.
void
TDocker::GetPackageInfo(void)
{
	RefVar wanted(ReadRef(RefVar(NILREF)));
	RefVar info(AllocateArray(RSSYMarray, 0));
	TRichString wantedName;
	if (NOTNIL(wanted))
		wantedName.SetStringData(wanted);
	TRichString name;
	TPMIterator iter;
	iter.Init();
	fError = noErr;			// (the ROM's Init answers an error, thrown here; the host's cannot fail)
	RefVar frame;
	while (iter.More())
	{
		TStore* store;
		PSSId rootId;
		if (IdToStore(iter.PackageId(), &store, &rootId) == noErr && fCurrentStore == ToObject(store))
		{
			Boolean take = true;
			if (NOTNIL(wanted))
			{
				name.SetCPlainStringData(iter.PackageName());
				take = name.CompareSubStringCommon(wantedName, 0, -1, false) == 0;
			}
			if (take)
			{
				frame = AllocateFrame();
				SourceType source = iter.fSourceType;
				SetFrameSlot(frame, RSSYMname, RefVar(MakeString(iter.PackageName())));
				SetFrameSlot(frame, RSSYMpackagesize, RefVar(MAKEINT(iter.PackageSize())));
				SetFrameSlot(frame, RSSYMpackageid, RefVar(MAKEINT(iter.PackageId())));
				SetFrameSlot(frame, RSSYMpackageversion, RefVar(MAKEINT(iter.fVersion)));
				SetFrameSlot(frame, RSSYMformat, RefVar(MAKEINT(source.format)));
				SetFrameSlot(frame, RSSYMdevicekind, RefVar(MAKEINT(source.deviceKind)));
				SetFrameSlot(frame, RSSYMdevicenumber, RefVar(MAKEINT(source.deviceNumber)));
				SetFrameSlot(frame, RSSYMdeviceid, RefVar(MAKEINT(source.deviceId)));
				SetFrameSlot(frame, RSSYMmodtime, RefVar(MAKEINT(iter.fModifyDate)));
				SetFrameSlot(frame, RSSYMiscopyprotected, RefVar(MAKEINT(iter.IsCopyProtected())));
				SetFrameSlot(frame, RSSYMlength, RefVar(MAKEINT(Ustrlen(iter.PackageName()) * 2 + 2)));
				UChar safe = false;
				if (SafeToDeactivatePackage(iter.PackageId(), &safe) != noErr)
					// ROM BUG: asked a second time for the error to throw
					Throw(exLongErrorException, (void*) (intptr_t) SafeToDeactivatePackage(iter.PackageId(), &safe), nil);
				SetFrameSlot(frame, RSSYMsafetoremove, RefVar(safe ? TRUEREF : NILREF));
				AddArraySlot(info, frame);
			}
		}
		iter.NextPackage();
	}
	iter.Done();
	WriteRef('pinf', info);
}


// ROM 0x00092b18 DoRestorePackage__7TDockerFv
// 'rpkg': a package restored from the pieces a backup kept (its entry in
// the current store's packages soup; RestoreAPackageFromPieces).
void
TDocker::DoRestorePackage(void)
{
	if (ISNIL(fCurrentStore))
		fCurrentSoup = NILREF;
	else
		fCurrentSoup = StoreGetSoup(fCurrentStore, RefVar(Rextrassoupname));
	RefVar pieces(ReadRef(fCurrentStore));
	NSCallGlobalFn(RefVar(RSSYMrestoreapackagefrompieces), pieces, fCurrentStore);
	WriteResult(noErr);
	AddChangedSoup(RefVar(RSSYMchanged), 1);
}


// ROM 0x00092bf0 DoRemovePackage__7TDockerFv
// 'rmvp': the package of the name removed from the current store, if it is
// there.
void
TDocker::DoRemovePackage(void)
{
	RefVar name(ReadRef(fCurrentStore));
	RefVar entry(NSCallGlobalFn(RefVar(RSSYMgetpackageentry), name, fCurrentStore));
	if (NOTNIL(entry))
		NSCallGlobalFn(RefVar(RSSYMremovepackage), entry);
	WriteResult(noErr);
	AddChangedSoup(RefVar(RSSYMchanged), 1);
}


// ROM 0x000979ec WriteInheritanceFrame__7TDockerFv
// 'ginh' -> 'dinh': the class inheritance (each class and its superclass
// as C strings, their count first).
void
TDocker::WriteInheritanceFrame(void)
{
	ULong count = 0;
	ULong length = 0;
	{
		RefVar inheritance(gInheritanceFrame);
		TObjectIterator iter(inheritance, false);
		for ( ; !iter.Done(); iter.Next())
		{
			count++;
			length += strlen(SymbolName(iter.Tag())) + strlen(SymbolName(iter.Value())) + 2;
		}
	}
	WriteDockerHeader('dinh', false);
	*fPipe << (unsigned long) (length + 4);
	*fPipe << (unsigned long) count;
	{
		RefVar inheritance(gInheritanceFrame);
		TObjectIterator iter(inheritance, false);
		for ( ; !iter.Done(); iter.Next())
		{
			const char* name = SymbolName(iter.Tag());
			fPipe->WriteChunk(name, strlen(name) + 1, false);
			name = SymbolName(iter.Value());
			fPipe->WriteChunk(name, strlen(name) + 1, false);
		}
	}
	Pad(length + 4);
	fPipe->FlushWrite();
}


// ROM 0x0009b9b8 WriteSyncOptions__7TDockerFv
// 'gsyn' -> 'sopt': the sync options the Connection application gave
// DoConnection.
void
TDocker::WriteSyncOptions(void)
{
	WriteDockerHeader('sopt', false);
	TObjectWriter writer(fDoConnectionArg, *fPipe, false);
	if (fVBOCompression == 2 || (fIsPackageSoup && fVBOCompression == 1))
		writer.SetCompressLargeBinaries();
	long size = writer.Size();
	*fPipe << size;
	writer.Write();
	Pad(size);
	fPipe->FlushWrite();
}


// ROM 0x0009bff8 TestMessage__7TDockerFv
// 'test': the data sent back as it came (no data: an empty 'test').
void
TDocker::TestMessage(void)
{
	if (fLength == 0)
	{
		WriteDockerHeader('test', true);
		return;
	}
	Ptr data = NewPtr(fLength);
	if (data == nil)
		OutOfMemory();
	ReadChunk(data, fLength, true);
	WriteDockerHeader('test', false);
	*fPipe << (unsigned long) fLength;
	fPipe->WriteChunk(data, fLength, false);
	Pad(fLength);
	fPipe->FlushWrite();
	DisposPtr(data);
}


// ROM 0x0009c0c0 TestRefMessage__7TDockerFv
// 'rtst': the object sent back (no data: an empty 'rtst').
// ROM BUG: the header is written twice - once here, without its length,
// and again by WriteRef - so what the desktop gets is a stray 12-byte
// header in front of the answer.
void
TDocker::TestRefMessage(void)
{
	if (fLength == 0)
	{
		WriteDockerHeader('rtst', true);
		return;
	}
	RefVar obj(ReadRef(RefVar(NILREF)));
	WriteDockerHeader('rtst', false);
	WriteRef('rtst', obj);
}


// ROM 0x00092cf8 CallFunction__7TDockerFUc
// 'cgfn' (global) / 'crmf' (the root's method): a function called - a frame
// {function, args}, or its name followed by the arguments.  ==> 'cres'
// what it answered.
void
TDocker::CallFunction(Boolean global)
{
	RefVar call(ReadRef(fCurrentStore));
	RefVar name, args, result;
	if (!IsSymbol(call))
	{
		name = GetFrameSlot(call, RSSYMfunction);
		args = GetFrameSlot(call, RSSYMargs);
	}
	else
	{
		name = call;
		args = ReadRef(fCurrentStore);
	}
	if (!global)
	{
		RefVar root(NSCallGlobalFn(RefVar(RSSYMgetroot)));
		result = DoMessage(root, name, args);
	}
	else
		result = NSCallGlobalFnWithArgArray(name, args);
	WriteRef('cres', result);
}


// ROM 0x000927ac CallConnectionApp__7TDockerFRC6RefVarT1
// A method of the Connection application called with the protocol frame
// and the argument (kDockErrDesktopError if there is no application or no
// such method); an error the docker recorded meanwhile is thrown.
Ref
TDocker::CallConnectionApp(RefArg method, RefArg arg)
{
	RefVar root(NSCallGlobalFn(RefVar(RSSYMgetroot)));
	if (ISNIL(root))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrDesktopError, nil);
	RefVar app(GetVariable(root, RefVar(RSSYMconnection), nil, 0));
	if (ISNIL(app))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrDesktopError, nil);
	RefVar fn(GetProtoVariable(app, method, nil));
	if (ISNIL(fn))
		Throw(exLongErrorException, (void*) (intptr_t) kDockErrDesktopError, nil);
	RefVar args(AllocateArray(RSSYMarray, 2));
	SetArraySlot(args, 0, fConnection);
	SetArraySlot(args, 1, arg);
	fInConnectionApp = true;
	RefVar result(DoBlock(fn, args));
	fInConnectionApp = false;
	if (fError != noErr)
		Throw(exLongErrorException, (void*) (intptr_t) fError, nil);
	return result;
}


// ROM 0x00092938 DoDisplaySlip__7TDockerFv
// 'dslp': a slip the desktop describes shown by the Connection application
// (DisplaySlip).  ==> 'slrs' what it answered (a number).
void
TDocker::DoDisplaySlip(void)
{
	RefVar slip(ReadRef(RefVar(NILREF)));
	RefVar result(CallConnectionApp(RefVar(RSSYMdisplayslip), slip));
	WriteDockerHeader('slrs', false);
	*fPipe << (long) 4;
	*fPipe << (long) RINT(result);
	fPipe->FlushWrite();
}


// ROM 0x000929f8 DoImportParametersSlip__7TDockerFv
// 'islp': the import parameters slip shown (DisplayImportSlip), the docker
// unlocked meanwhile.  ==> 'islr' what the user chose.
void
TDocker::DoImportParametersSlip(void)
{
	RefVar parameters(ReadRef(RefVar(NILREF)));
	Boolean wasLocked = GetTDockerLock();
	UnlockTDocker();
	RefVar result(CallConnectionApp(RefVar(RSSYMdisplayimportslip), parameters));
	if (wasLocked)
		WaitAndLockTDocker();
	WriteRef('islr', result);
}


// ROM 0x00092aa0 DoGetPassword__7TDockerFv
// 'gpwd': the user asked for the password (GetPassword), which is sent
// ('pass', the desktop's challenge under its key).
void
TDocker::DoGetPassword(void)
{
	RefVar prompt(ReadRef(RefVar(NILREF)));
	RefVar password(CallConnectionApp(RefVar(RSSYMgetpassword), prompt));
	WritePassword(password);
}


// ROM 0x000926cc ReadProtocolExtension__7TDockerFv
// 'pext': a protocol extension the desktop installs - the command, then
// the function.
void
TDocker::ReadProtocolExtension(void)
{
	unsigned long command;
	*fPipe >> command;
	RefVar fn(ReadRef(RefVar(NILREF)));
	fError = InstallProtocolExtension(RefVar(NILREF), fn, command);
}


// ROM 0x00092760 ReadRemoveProtocolExtension__7TDockerFv
// 'rpex': a protocol extension removed.
void
TDocker::ReadRemoveProtocolExtension(void)
{
	unsigned long command;
	*fPipe >> command;
	fError = RemoveProtocolExtension(RefVar(NILREF), command);
}


// ROM 0x00099ca8 ReadResultString__7TDockerFv
// 'ress': a result the desktop gives as a string, kept in the protocol
// frame's desktopResult.  ==> kDockErrResultString.
long
TDocker::ReadResultString(void)
{
	RefVar result(ReadRef(RefVar(NILREF)));
	SetFrameSlot(fConnection, RSSYMdesktopresult, result);
	return kDockErrResultString;
}


// ROM 0x000959d4 WriteCommand__7TDockerFRC6RefVarT1lUcRUl
// A command the Connection application writes (its name a string of four
// characters, answered in commandWord): with data (an integer as one long,
// anything else as NSOF), or a header with the length of the data
// WriteBytes will follow it with, or with no data.  A 'helo' - a keep-alive
// the application's idle sends - is dropped while an operation holds the
// docker, unless the operation is waiting on the application itself.
// ==> fError (kDockErrBusy when the docker could not be had).
long
TDocker::WriteCommand(RefArg command, RefArg data, long length, Boolean withData, ULong* commandWord)
{
	if (fCleanedUp)
		return noErr;
	fError = noErr;
	fWritingData = false;
	Boolean busy = GetTDockerLock() && !fStopping;
	Boolean locked = false;
	newton_try
	{
		if (!IsString(command))
			Throw(exLongErrorException, (void*) (intptr_t) kDockErrDesktopError, nil);
		UByte word[20];
		memset(word, 0, sizeof(word));
		ConvertFromUnicode(GetCString(command), word, kMacRomanEncoding, 0x7fffffff);
		*commandWord = GetBigEndianWord(word);
		if (!(busy && *commandWord == kDHello && !fInConnectionApp)
		 && (locked = WaitAndLockTDocker()) != false)
		{
			if (!fSessionAgreed && *commandWord != kDHello)
				fError = kDockErrNotConnected;
			else
			{
				if (!withData)
				{
					Boolean flush;
					if (length > 0)
					{
						fLength = length;
						fWritingData = true;
						flush = false;
					}
					else
						flush = !fWritingData;
					WriteDockerHeader(*commandWord, flush);
					if (fWritingData)
						*fPipe << length;
				}
				else if (ISINT(data))
				{
					WriteDockerHeader(*commandWord, false);
					*fPipe << (long) 4;
					*fPipe << (long) RINT(data);
					fPipe->FlushWrite();
				}
				else
					WriteRef(*commandWord, data);
			}
			if (*commandWord != kDHello)
				fSessionStarted = false;
		}
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	if (busy && !locked)
		return *commandWord == kDHello ? noErr : kDockErrBusy;
	CleanUpIfStopping(false);
	CleanUpIfError(false);
	if (!busy && locked)
		UnlockTDocker();
	return fError;
}


// ROM 0x00095c6c WriteBytes__7TDockerFRC6RefVar
// The next of the data a WriteCommand header promised (a binary), as much
// as is still owed; the pipe flushed when it is all written.
long
TDocker::WriteBytes(RefArg data)
{
	if (fCleanedUp)
		return noErr;
	fError = noErr;
	WaitAndLockTDocker();
	newton_try
	{
		if (!fWritingData)
			fError = kDockErrDesktopError;
		else
		{
			ULong count = Length(data);
			ULong owed = fLength - fBytesWritten;
			if (owed < count)
				count = owed;
			fBytesWritten += count;
			fPipe->WriteChunk(BinaryData(data), count, false);
			if (fBytesWritten == fLength)
			{
				fWritingData = false;
				fPipe->FlushWrite();
			}
		}
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	CleanUpIfStopping(false);
	CleanUpIfError(false);
	UnlockTDocker();
	return fError;
}


// ROM 0x00095fa8 ReadCommand__7TDockerFR6RefVarUcT2
// The desktop's next command (skipping keep-alives if asked) as a frame
// {command, length, data} - the data read too unless only the header is
// wanted (ReadCommandData or ReadBytes then read it).  ==> fError
// (-112088 before a session was agreed).
long
TDocker::ReadCommand(RefVar& command, Boolean headerOnly, Boolean skipHellos)
{
	if (fCleanedUp)
		return noErr;
	fError = noErr;
	if (!fSessionAgreed)
		return -112088;
	WaitAndLockTDocker();
	fBytesRead = 0;
	fDataPending = headerOnly;
	command = NILREF;
	newton_try
	{
		command = AllocateFrame();
		do
			ReadDockerHeader(&fCommand, &fLength);
		while (skipHellos && fCommand == kDHello);
		UByte word[4];
		PutBigEndianWord(word, fCommand);
		UniChar name[5];
		ConvertToUnicode(word, name, kMacRomanEncoding, 4);
		name[4] = 0;
		SetFrameSlot(command, RSSYMcommand, RefVar(MakeString(name)));
		SetFrameSlot(command, RSSYMlength, RefVar(MAKEINT(fLength)));
		RefVar data;
		if (!headerOnly)
		{
			fDataPending = true;
			ReadData(data);
		}
		SetFrameSlot(command, RSSYMdata, data);
		fSessionStarted = true;
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	CleanUpIfStopping(false);
	CleanUpIfError(false);
	UnlockTDocker();
	return fError;
}


// ROM 0x00095ecc ReadData__7TDockerFR6RefVar
// The pending command's data: a single long as an integer, anything else
// as NSOF, none as nil.
void
TDocker::ReadData(RefVar& data)
{
	if (fCleanedUp)
		return;
	fError = noErr;
	data = NILREF;
	if (!fDataPending)
		fError = kDockErrDesktopError;
	else
	{
		newton_try
		{
			if (fLength != 0)
			{
				if (fLength == 4)
				{
					unsigned long value;
					*fPipe >> value;
					data = MAKEINT(value);
				}
				else
					data = ReadRef(fCurrentStore);
			}
		}
		newton_catch_all
		{
			ProcessException(CurrentException());
		}
		end_try;
	}
	fDataPending = false;
}


// ROM 0x00095dc8 ReadCommandData__7TDockerFR6RefVar
long
TDocker::ReadCommandData(RefVar& data)
{
	if (fCleanedUp)
		return noErr;
	fError = noErr;
	WaitAndLockTDocker();
	ReadData(data);
	CleanUpIfStopping(false);
	CleanUpIfError(false);
	UnlockTDocker();
	return fError;
}


// ROM 0x00095e2c FlushCommandData__7TDockerFv
// The pending command's data thrown away.
long
TDocker::FlushCommandData(void)
{
	if (!fDataPending)
		fError = kDockErrDesktopError;
	else
	{
		WaitAndLockTDocker();
		newton_try
		{
			FlushCommand();
		}
		newton_catch_all
		{
			ProcessException(CurrentException());
		}
		end_try;
	}
	fDataPending = false;
	CleanUpIfStopping(false);
	CleanUpIfError(false);
	UnlockTDocker();
	return fError;
}


// ROM 0x0009616c ReadBytes__7TDockerFRl6RefVar
// The next of the pending command's data into a binary, as much as count
// asks for and is left (count says how much it was); the padding read with
// the last of it.
long
TDocker::ReadBytes(long* count, RefArg buffer)
{
	RefVar binary(buffer);
	if (fCleanedUp)
		return noErr;
	fError = noErr;
	WaitAndLockTDocker();
	newton_try
	{
		if (!fDataPending)
			fError = kDockErrDesktopError;
		else if (!IsBinary(binary))
			fError = -48408;				// (kNSErrNotABinaryObject)
		else
		{
			ULong left = fLength - fBytesRead;
			if (left < (ULong) *count)
				*count = left;
			if (*count <= Length(binary))
			{
				fBytesRead += *count;
				ReadChunk(BinaryData(binary), *count, fBytesRead == fLength);
				if (fBytesRead == fLength)
				{
					fBytesRead = 0;
					fDataPending = false;
				}
			}
			else
				fError = -48205;			// (kNSErrOutOfRange)
		}
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	CleanUpIfStopping(false);
	CleanUpIfError(false);
	UnlockTDocker();
	return fError;
}


// ROM 0x00094f4c ProcessBuiltinCommand__7TDockerFRUc
// The command the application read (ReadCommand's header) carried out as
// the docker would.  ==> fError.
long
TDocker::ProcessBuiltinCommand(Boolean* done)
{
	if (fCleanedUp)
		return noErr;
	fError = noErr;
	if (!fSessionAgreed)
		return kDockErrNotConnected;
	WaitAndLockTDocker();
	*done = false;
	Boolean operationDone = false;
	newton_try
	{
		ProcessCommand(done, &operationDone);
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
	*done = CleanUpIfStopping(*done);
	CleanUpIfError(*done);
	UnlockTDocker();
	return fError;
}


// ROM 0x000934b0 ProcessCommand__7TDockerFRUcT1
// One of the desktop's commands carried out.  done: the session is over
// ('disc'); operationDone: the operation the session was for is over, and
// the command loop ends without disconnecting (an extension answered,
// 'opca', 'opdn', a package loaded on protocol 10).
// The stores', soups', cursors' and entries' commands are here too, and
// making, sending and backing up soups, the package list, restoring and
// removing packages, calling functions and the Connection application's
// slips.  NOT YET: the system patches ('gpat', 'rpat') - each is answered 'unkn' as a command the Newton does not know
// is, which a desktop takes as a Newton too old to do it.
void
TDocker::ProcessCommand(Boolean* done, Boolean* operationDone)
{
	*done = false;
	*operationDone = false;
	fSessionStarted = false;
	if (CheckProtocolExtension(fCommand, operationDone) || CheckProtocolPatch(fCommand, operationDone))
		;
	else
	{
		switch (fCommand)
		{
		case kDLoadPackage:
			if (fState == kDockStateRestore)
				fCurrentSoup = ISNIL(fCurrentStore) ? NILREF : StoreGetSoup(fCurrentStore, RefVar(Rextrassoupname));
			ReadPackage();
			WriteResult(fError);
			if (fState != kDockStateRestore && fProtocolVersion == 10)
				*operationDone = true;
			break;
		case kDDisconnect:
			*done = true;
			break;
		case kDResult:
			fError = ReadResult();
			break;
		case kDHello:
			FlushCommand();
			break;
		case kDUnknownCommand:
		{
			unsigned long command;
			*fPipe >> command;
			fCommand = command;
			Throw(exLongErrorException, (void*) (intptr_t) kDockErrNoDocker, nil);
		}
		case kDWhichIcons:
			SetWhichIcons();
			break;
		case kDSetTimeout:
			SetTimeout();
			break;
		case kDOperationCanceled:
			*operationDone = true;
			WriteDockerHeader(kDOpCanceledAck, true);
			break;
		case kDOpDone:
			*operationDone = true;
			break;
		case kDSetVBOCompression:
		{
			unsigned long compression;
			*fPipe >> compression;
			fVBOCompression = (long) compression;
			break;
		}
		case kDSync:
			fState = kDockStateSync;
			WriteResult(noErr);
			break;
		case kDRestore:
			fState = kDockStateRestore;
			WriteResult(noErr);
			break;
		case kDRestoreAll:
			fState = kDockStateLoadPackage;
			WriteResult(noErr);
			break;
		case kDDesktopInControl:
			fState = kDockStateNone;
			fSessionStarted = false;
			break;
		case 'gsto':
			WriteStoreNames();
			break;
		case 'ssto':
			SetCurrentStore(false);
			break;
		case 'ssgn':
			SetCurrentStore(true);
			break;
		case 'sdef':
			SetStoreToDefault();
			WriteResult(noErr);
			break;
		case 'gdfs':
			WriteDefaultStore();
			break;
		case 'ssig':
			SetStoreSignature();
			break;
		case 'ssna':
		{
			RefVar name(ReadRef(fCurrentStore));
			StoreSetName(fCurrentStore, name);
			WriteResult(noErr);
			break;
		}
		case 'gets':
			WriteSoupNames();
			break;
		case 'ssou':
			SetCurrentSoup(false);
			break;
		case 'ssgi':
			SetCurrentSoup(true);
			break;
		case 'gsin':
			WriteSoupInfo(false);
			break;
		case 'cinf':
			WriteSoupInfo(true);
			break;
		case 'gind':
			WriteIndexDescription(false);
			break;
		case 'cidx':
			WriteIndexDescription(true);
			break;
		case kDSoupInfo:
			SetSoupInfoFrame();
			WriteResult(fError);
			break;
		case 'ssos':
			SetSoupSignature();
			break;
		case 'qury':
			RemoteQuery();
			break;
		case 'cmap':
			RemoteCursorMap();
			break;
		case 'goto':
			RemoteCursorGotoKey();
			break;
		case 'crsr':
			RemoteCursorEntry();
			break;
		case 'move':
			RemoteCursorMove();
			break;
		case 'next':
			RemoteCursorNext();
			break;
		case 'prev':
			RemoteCursorPrev();
			break;
		case 'rset':
			RemoteCursorReset();
			break;
		case 'rend':
			RemoteCursorResetToEnd();
			break;
		case 'cnt ':
			RemoteCursorCountEntries();
			break;
		case 'whch':
			RemoteCursorWhichEnd();
			break;
		case 'cfre':
			RemoteCursorFree();
			break;
		case 'rete':
			ReturnEntry('entr');
			break;
		case 'rcen':
			ReturnEntry('cent');
			break;
		case 'adde':
			AddEntry(false);
			break;
		case 'auni':
			AddEntry(true);
			break;
		case 'cent':
			ChangeEntry();
			break;
		case 'dele':
			DeleteEntries();
			break;
		case 'esou':
		case 'dsou':
			EmptyOrDelete(fCommand);
			break;
		case 'sver':
			ReadSourceVersion();
			break;
		case 'gids':
			WriteSoupIDs();
			break;
		case 'gcid':
			WriteChangedIDs();
			break;
		case 'csop':
			CreateSoup();
			break;
		case 'cdsp':
			CreateSoupFromSoupDef();
			break;
		case 'snds':
			SendSoup();
			break;
		case 'bksp':
			BackupSoup();
			break;
		case 'gpin':
			GetPackageInfo();
			break;
		case 'rpkg':
			DoRestorePackage();
			break;
		case 'rmvp':
			DoRemovePackage();
			break;
		case 'ginh':
			WriteInheritanceFrame();
			break;
		case 'gsyn':
			WriteSyncOptions();
			break;
		case 'test':
			TestMessage();
			break;
		case 'rtst':
			TestRefMessage();
			break;
		case 'cgfn':
			CallFunction(true);
			break;
		case 'crmf':
			CallFunction(false);
			break;
		case 'dslp':
			DoDisplaySlip();
			break;
		case 'islp':
			DoImportParametersSlip();
			break;
		case 'gpwd':
			DoGetPassword();
			break;
		case 'pext':
			ReadProtocolExtension();
			WriteResult(fError);
			break;
		case 'rpex':
			ReadRemoveProtocolExtension();
			WriteResult(fError);
			break;
		case 'ress':
			fError = ReadResultString();
			break;
		case kDSetTime:
		{
			unsigned long minutes;
			*fPipe >> minutes;
			fDesktopTime = minutes & 0x1fffffff;
			fTimeSet = RealClock();
			WriteLong(kDTime, fTimeSet);
			break;
		}
		default:
			// NOT YET (see above), and a command no Newton knows
			FlushCommand();
			WriteLong(kDUnknownCommand, fCommand);
			break;
		}
	}
	if (*operationDone)
		fSessionStarted = true;
}


// ------------------------------------------------------------------------
//	The protocol frame's natives
// ------------------------------------------------------------------------

// ROM 0x000971f0 GetTheDocker__FRC6RefVarUc
TDocker*
GetTheDocker(RefArg connection, Boolean mustExist)
{
	RefVar slot(GetFrameSlot(connection, RSSYMconncobject));
	if (NOTNIL(slot))
	{
		TDocker* docker = (TDocker*) RefToAddress(slot);
		if (docker != nil)
			return docker;
	}
	if (mustExist)
		Throw(exErrorException, (void*) (intptr_t) kDockErrNoDocker, nil);
	return nil;
}


// ROM 0x00097318 SaveTheDocker__FRC6RefVarP7TDocker
void
SaveTheDocker(RefArg connection, TDocker* docker)
{
	RefVar ref(NILREF);
	if (docker != nil)
		ref = AddressToRef(docker);
	SetFrameSlot(connection, RSSYMconncobject, ref);
}


// ROM 0x0009631c CleanUpDockerIfError__FRC6RefVarlUcT3
// After a native: no error (or kDockErrRetryPassword, or -16005) notes the
// time the desktop was last heard from (if touch); any other error deletes
// the docker and leaves the frame not connected.  An error is then thrown
// if throwIt says so.
void
CleanUpDockerIfError(RefArg connection, long error, Boolean touch, Boolean throwIt)
{
	if (error == noErr || error == kDockErrRetryPassword || error == -16005)
	{
		if (touch)
			SetFrameSlot(connection, RSSYMlastcommunicationwithdesktop, RefVar(FTimeInSeconds(RefVar(NILREF))));
	}
	else
	{
		// (the ROM passes an exception on - NextHandler - once the frame
		// is tidied; the host keeps what it was and throws it again)
		Boolean failed = false;
		ExceptionName name = nil;
		void* data = nil;
		ExceptionDestructor destructor = nil;
		newton_try
		{
			TDocker* docker = GetTheDocker(connection, true);
			if (docker != nil)
				delete docker;
		}
		newton_catch_all
		{
			failed = true;
			name = CurrentException()->name;
			data = CurrentException()->data;
			destructor = CurrentException()->destructor;
		}
		end_try;
		SaveTheDocker(connection, nil);
		SetFrameSlot(connection, RSSYMconnected, RefVar(NILREF));
		if (failed)
			Throw(name, data, destructor);
	}
	if (throwIt && error != noErr)
		Throw(exErrorException, (void*) (intptr_t) error, nil);
}


// ROM 0x00096834 FConnInstantiate
// ConnInstantiate(): a docker for the frame.
Ref
FConnInstantiate(RefArg rcvr)
{
	TDocker* docker = new TDocker;
	if (docker == nil)
		Throw(exErrorException, (void*) (intptr_t) -10007, nil);
	SaveTheDocker(rcvr, docker);
	return NILREF;
}


// ROM 0x00096770 FConnConnect
// Connect(options, password): the session begun; connected (and the
// desktop's platform noted) unless it failed.
Ref
FConnConnect(RefArg rcvr, RefArg options, RefArg password)
{
	long error = GetTheDocker(rcvr, true)->Connect(rcvr, options, password);
	if (error == noErr || error == kDockErrRetryPassword)
	{
		SetFrameSlot(rcvr, RSSYMthedesktoptype, RefVar(MAKEINT(GetTheDocker(rcvr, true)->GetPlatform())));
		SetFrameSlot(rcvr, RSSYMconnected, RefVar(TRUEREF));
	}
	CleanUpDockerIfError(rcvr, error, true, true);
	return NILREF;
}


// ROM 0x00096948 FConnDoConnection
// DoConnection(arg1, arg2): the session carried out, the frame's
// packageCallback told of a package's progress.  ==> whether it is over.
Ref
FConnDoConnection(RefArg rcvr, RefArg arg1, RefArg arg2)
{
	TDocker* docker = GetTheDocker(rcvr, true);
	SetFrameSlot(rcvr, RSSYMpercentdone, RefVar(MAKEINT(0)));
	SetFrameSlot(rcvr, RSSYMpackagename, RefVar(NILREF));
	Boolean done;
	RefVar callback(GetVariable(rcvr, RefVar(RSSYMpackagecallback), nil, 0));
	long error = docker->DoConnection(arg1, arg2, callback, &done);
	long broadcast = docker->BroadcastChanges();
	if (error == noErr)
		error = broadcast;
	CleanUpDockerIfError(rcvr, error, true, true);
	return done ? TRUEREF : NILREF;
}


// ROM 0x00096464 FConnBuildStoreFrame
// ConnBuildStoreFrame(store, withInfo): what the desktop is told of a
// store - its name, signature, sizes, kind, (its info, if asked for),
// whether it is read-only, its password key, whether it is the default
// store, and its version.
Ref
FConnBuildStoreFrame(RefArg rcvr, RefArg store, RefArg withInfo)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMname, RefVar(StoreGetName(store)));
	SetFrameSlot(frame, RSSYMsignature, RefVar(StoreGetSignature(store)));
	SetFrameSlot(frame, RSSYMtotalsize, RefVar(StoreTotalSize(store)));
	SetFrameSlot(frame, RSSYMusedsize, RefVar(StoreUsedSize(store)));
	SetFrameSlot(frame, RSSYMkind, RefVar(StoreGetKind(store)));
	if (NOTNIL(withInfo))
		SetFrameSlot(frame, RSSYMinfo, RefVar(StoreGetAllInfo(store)));
	SetFrameSlot(frame, RSSYMreadonly, RefVar(StoreIsReadOnly(store)));
	SetFrameSlot(frame, RSSYMstorepassword, RefVar(StoreGetPasswordKey(store)));
	if (FDefaultStore(RefVar(NILREF)) == (Ref) store)
		SetFrameSlot(frame, RSSYMdefaultstore, RefVar(TRUEREF));
	long version = 0;
	GetStoreVersion(StoreFromWrapper(store), &version);
	SetFrameSlot(frame, RSSYMstoreversion, RefVar(MAKEINT(version)));
	return frame;
}


// ROM 0x00096884 FConnRetryPassword
// RetryPassword(password)
Ref
FConnRetryPassword(RefArg rcvr, RefArg password)
{
	long error = GetTheDocker(rcvr, true)->RetryPassword(password);
	CleanUpDockerIfError(rcvr, error, true, true);
	return NILREF;
}


// ROM 0x00097124 FDESCreatePasswordKey
// DESCreatePasswordKey(password): the password's key, an 8-byte 'deskey
// binary (which a password slot may hold in its place).
Ref
FDESCreatePasswordKey(RefArg /*rcvr*/, RefArg password)
{
	if (!IsInstance(password, RSSYMstring))
		Throw(exErrorException, (void*) (intptr_t) kDockErrNotConnected, nil);
	LockRef(password);
	DESWord key[2];
	DESCharToKey(GetCString(password), key);
	UnlockRef(password);
	RefVar binary(AllocateBinary(RSSYMdeskey, 8));
	LockRef(binary);
	PutBigEndianWord(BinaryData(binary), key[0]);
	PutBigEndianWord(BinaryData(binary) + 4, key[1]);
	UnlockRef(binary);
	return binary;
}


// ROM 0x000970f8 FConnEntriesEqual
// ConnEntriesEqual(entry): the validTest of IsDuplicateEntry's query.
Ref
FConnEntriesEqual(RefArg /*rcvr*/, RefArg entry)
{
	return gTheDocker->EntriesEqual(entry) ? TRUEREF : NILREF;
}


// ROM 0x00096a7c ConnWriteCommand__FRC6RefVarN41RUl
// What WriteCommand and WriteCommandHeader share: the docker's
// WriteCommand, and an error other than kDockErrRetryPassword and -16005
// ending the docker (as CleanUpDockerIfError does, in line) and thrown.
static void
ConnWriteCommand(RefArg connection, RefArg command, RefArg data, RefArg length, RefArg withData, ULong* commandWord)
{
	long len = RINT(length);
	long error = GetTheDocker(connection, true)->WriteCommand(command, data, len, NOTNIL(withData), commandWord);
	if (error != noErr && error != kDockErrRetryPassword && error != -16005)
	{
		Boolean failed = false;
		ExceptionName name = nil;
		void* exceptionData = nil;
		ExceptionDestructor destructor = nil;
		newton_try
		{
			TDocker* docker = GetTheDocker(connection, true);
			if (docker != nil)
				delete docker;
		}
		newton_catch_all
		{
			failed = true;
			name = CurrentException()->name;
			exceptionData = CurrentException()->data;
			destructor = CurrentException()->destructor;
		}
		end_try;
		SaveTheDocker(connection, nil);
		SetFrameSlot(connection, RSSYMconnected, RefVar(NILREF));
		if (failed)
			Throw(name, exceptionData, destructor);
	}
	if (error != noErr)
		Throw(exErrorException, (void*) (intptr_t) error, nil);
}


// ROM 0x00096b04 FConnWriteCommand
// WriteCommand(command, data, withData): the desktop told something, and
// (unless it was a keep-alive) the time noted.
Ref
FConnWriteCommand(RefArg rcvr, RefArg command, RefArg data, RefArg withData)
{
	ULong commandWord;
	ConnWriteCommand(rcvr, command, data, RefVar(MAKEINT(-1)), withData, &commandWord);
	if (commandWord != kDHello)
		SetFrameSlot(rcvr, RSSYMlastcommunicationwithdesktop, RefVar(FTimeInSeconds(RefVar(NILREF))));
	return NILREF;
}


// ROM 0x00096bb8 FConnWriteCommandHeader
// WriteCommandHeader(command, length): meant to write a header for
// WriteBytes to follow.
// ROM BUG: the "no data" flag it passes is the integer nought, which is not
// nil, so the command goes out with a long of nought as its data and the
// length is ignored - and WriteBytes then finds nothing owed.
Ref
FConnWriteCommandHeader(RefArg rcvr, RefArg command, RefArg length)
{
	ULong commandWord;
	ConnWriteCommand(rcvr, command, RefVar(MAKEINT(0)), length, RefVar(MAKEINT(0)), &commandWord);
	SetFrameSlot(rcvr, RSSYMlastcommunicationwithdesktop, RefVar(FTimeInSeconds(RefVar(NILREF))));
	return NILREF;
}


// ROM 0x00096c70 FConnWriteBytes
Ref
FConnWriteBytes(RefArg rcvr, RefArg data)
{
	long error = GetTheDocker(rcvr, true)->WriteBytes(data);
	CleanUpDockerIfError(rcvr, error, true, true);
	return NILREF;
}


// ROM 0x00096cf0 ConnReadCommand__FRC6RefVarUcT2
static Ref
ConnReadCommand(RefArg rcvr, Boolean headerOnly, Boolean skipHellos)
{
	RefVar command;
	long error = GetTheDocker(rcvr, true)->ReadCommand(command, headerOnly, skipHellos);
	CleanUpDockerIfError(rcvr, error, true, true);
	return command;
}


// ROM 0x00096d5c FConnReadCommand
// ReadCommand(skipHellos): {command, length, data}.
Ref
FConnReadCommand(RefArg rcvr, RefArg skipHellos)
{
	return ConnReadCommand(rcvr, false, NOTNIL(skipHellos));
}


// ROM 0x00096d74 FConnReadCommandHeader
// ReadCommandHeader(skipHellos): {command, length}, the data left to read.
Ref
FConnReadCommandHeader(RefArg rcvr, RefArg skipHellos)
{
	return ConnReadCommand(rcvr, true, NOTNIL(skipHellos));
}


// ROM 0x00096d8c FConnReadCommandData
Ref
FConnReadCommandData(RefArg rcvr)
{
	RefVar data;
	long error = GetTheDocker(rcvr, true)->ReadCommandData(data);
	CleanUpDockerIfError(rcvr, error, true, true);
	return data;
}


// ROM 0x00096de8 FConnFlushCommandData
Ref
FConnFlushCommandData(RefArg rcvr)
{
	long error = GetTheDocker(rcvr, true)->FlushCommandData();
	CleanUpDockerIfError(rcvr, error, true, true);
	return NILREF;
}


// ROM 0x00096e20 FConnReadBytes
// ReadBytes(count, binary): ==> how many bytes were read into it.
Ref
FConnReadBytes(RefArg rcvr, RefArg count, RefArg binary)
{
	long n = RINT(count);
	long error = GetTheDocker(rcvr, true)->ReadBytes(&n, binary);
	CleanUpDockerIfError(rcvr, error, true, true);
	return MAKEINT(n);
}


// ROM 0x000968fc FProcessBuiltinCommand
// ProcessBuiltinCommand(): ==> whether the session is over.
Ref
FProcessBuiltinCommand(RefArg rcvr)
{
	Boolean done;
	long error = GetTheDocker(rcvr, true)->ProcessBuiltinCommand(&done);
	CleanUpDockerIfError(rcvr, error, true, true);
	return done ? TRUEREF : NILREF;
}


// ROM 0x00096e8c FConnInstallProtocolExtension
// InstallAnyProtocolExtension(command, function): an extension of the
// protocol (one already there is not an error).
Ref
FConnInstallProtocolExtension(RefArg rcvr, RefArg command, RefArg function)
{
	long error = GetTheDocker(rcvr, true)->InstallProtocolExtension(command, function, 0);
	if (error != kDockErrBadExtension)
		CleanUpDockerIfError(rcvr, error, false, true);
	return NILREF;
}


// ROM 0x00096ee4 FConnRemoveProtocolExtension
// RemoveProtocolExtension(command)
Ref
FConnRemoveProtocolExtension(RefArg rcvr, RefArg command)
{
	long error = GetTheDocker(rcvr, true)->RemoveProtocolExtension(command, 0);
	CleanUpDockerIfError(rcvr, error, false, true);
	return NILREF;
}


// ROM 0x00096f28 FConnStop
Ref
FConnStop(RefArg rcvr)
{
	GetTheDocker(rcvr, true)->Stop();
	return NILREF;
}


// ROM 0x00096f48 FConnGetSyncChanges
Ref
FConnGetSyncChanges(RefArg rcvr)
{
	return GetTheDocker(rcvr, true)->GetSyncChanges();
}


// ROM 0x00096f64 FConnectionState
// GetState(): the session's state frame.
Ref
FConnectionState(RefArg rcvr)
{
	return GetTheDocker(rcvr, true)->GetState();
}


// ROM 0x00096f80 FConnSetState
Ref
FConnSetState(RefArg rcvr, RefArg state)
{
	if (ISINT(state))
		GetTheDocker(rcvr, true)->SetState(RVALUE(state));
	return NILREF;
}


// ROM 0x00096fd0 FConnDesktopType
// DesktopType(): the frame's thedesktoptype unless that is nil or 2, else
// the docker's platform (2 with no docker).
Ref
FConnDesktopType(RefArg rcvr)
{
	RefVar type(GetFrameSlot(rcvr, RSSYMthedesktoptype));
	if (NOTNIL(type) && RINT(type) != 2)
		return type;
	TDocker* docker = GetTheDocker(rcvr, false);
	return MAKEINT(docker == nil ? 2 : docker->GetPlatform());
}


// ROM 0x0009705c FConnBytesAvailable
Ref
FConnBytesAvailable(RefArg rcvr)
{
	return MAKEINT(GetTheDocker(rcvr, true)->BytesAvailable(false));
}


// ROM 0x00097080 FConnGetCurrentStore
Ref
FConnGetCurrentStore(RefArg rcvr)
{
	return GetTheDocker(rcvr, true)->GetCurrentStore();
}


// ROM 0x0009709c FConnAbort
// Abort(error): the session ended with the error, and the docker gone.
Ref
FConnAbort(RefArg rcvr, RefArg error)
{
	long err = RINT(error);
	GetTheDocker(rcvr, true)->AbortConnection(err);
	CleanUpDockerIfError(rcvr, -1, false, false);
	return NILREF;
}


void
RegisterDockerNatives(void)
{
	RegisterNativeFunction("FConnInstantiate", (void*) FConnInstantiate, 0);
	RegisterNativeFunction("FConnConnect", (void*) FConnConnect, 2);
	RegisterNativeFunction("FConnDoConnection", (void*) FConnDoConnection, 2);
	RegisterNativeFunction("FConnInstallProtocolExtension", (void*) FConnInstallProtocolExtension, 2);
	RegisterNativeFunction("FConnRemoveProtocolExtension", (void*) FConnRemoveProtocolExtension, 1);
	RegisterNativeFunction("FConnStop", (void*) FConnStop, 0);
	RegisterNativeFunction("FConnWriteCommand", (void*) FConnWriteCommand, 3);
	RegisterNativeFunction("FConnWriteCommandHeader", (void*) FConnWriteCommandHeader, 2);
	RegisterNativeFunction("FConnWriteBytes", (void*) FConnWriteBytes, 1);
	RegisterNativeFunction("FConnReadCommand", (void*) FConnReadCommand, 1);
	RegisterNativeFunction("FConnReadCommandHeader", (void*) FConnReadCommandHeader, 1);
	RegisterNativeFunction("FConnReadCommandData", (void*) FConnReadCommandData, 0);
	RegisterNativeFunction("FConnFlushCommandData", (void*) FConnFlushCommandData, 0);
	RegisterNativeFunction("FConnReadBytes", (void*) FConnReadBytes, 2);
	RegisterNativeFunction("FProcessBuiltinCommand", (void*) FProcessBuiltinCommand, 0);
	RegisterNativeFunction("FConnEntriesEqual", (void*) FConnEntriesEqual, 1);
	RegisterNativeFunction("FConnRetryPassword", (void*) FConnRetryPassword, 1);
	RegisterNativeFunction("FDESCreatePasswordKey", (void*) FDESCreatePasswordKey, 1);
	RegisterNativeFunction("FConnBuildStoreFrame", (void*) FConnBuildStoreFrame, 2);
	RegisterNativeFunction("FConnGetSyncChanges", (void*) FConnGetSyncChanges, 0);
	RegisterNativeFunction("FConnectionState", (void*) FConnectionState, 0);
	RegisterNativeFunction("FConnSetState", (void*) FConnSetState, 1);
	RegisterNativeFunction("FConnDesktopType", (void*) FConnDesktopType, 0);
	RegisterNativeFunction("FConnBytesAvailable", (void*) FConnBytesAvailable, 0);
	RegisterNativeFunction("FConnGetCurrentStore", (void*) FConnGetCurrentStore, 0);
	RegisterNativeFunction("FConnAbort", (void*) FConnAbort, 1);
}
