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

extern const ExceptionName exPipeException;
extern const ExceptionName exLongErrorException;
extern const ExceptionName exErrorException;
extern const ExceptionName exFrames;
extern const ExceptionName exOutOfMemory;



static const ExceptionName exRefExceptionName = (ExceptionName) "type.ref";		// ROM 0x00380880 exRefException


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
	fField18 = NILREF;
	fCallback = NILREF;
	fExtensions = NILREF;
	fDesktopApps = NILREF;
	fCursors = nil;
	fSyncChanges = NILREF;
	fField8c = 0;
	fField90 = 0;
	fField94 = 0;
	fField98 = 0;
	fSessionAgreed = false;
	fPlatform = 2;
	fStopping = false;
	fProtocolVersion = 9;
	fLength = 0;
	fError = noErr;
	fPipe = nil;
	fPipeOpen = false;
	fSelectiveSyncOK = false;
	fField2c = false;
	fField2d = false;
	fField2e = false;
	fField28 = NILREF;
	fSessionStarted = false;
	fField30 = false;
	fField38 = 0;
	fField31 = false;
	fField3c = 0;
	fLocked = false;
	fField34 = NILREF;
	fHasArg1 = false;
	fLoadPackageOnly = false;
	fDynArray7c = nil;
	fExtensionCommands = nil;
	fField58 = 0;
	fField60 = NILREF;
	fField5c = 0;
	fField64 = 0;
	fField68 = 0;
	fField6c = NILREF;
	fField70 = NILREF;
	fCleanedUp = false;
	fFieldb4 = false;
	fState = kDockStateNone;
	// (not set by the ROM's constructor either)
	fStopDone = false;
	fCommand = 0;
	fField74 = 0;
	fField78 = 0;
	fField9c = 0;
	fFielda0 = 0;
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
// After an operation: an error (other than kDockErrAlreadyDocking and
// -16005, a connection the desktop closed), or force, ends the session -
// the error is told the desktop, if the pipe was ever open and the error is
// not -16009, and the pipe and the rest are given back.
void
TDocker::CleanUpIfError(Boolean force)
{
	long error = fError;
	if ((error == noErr || error == kDockErrAlreadyDocking || error == -16005) && !force)
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
	// NOT YET: the cursor array (fCursors) is made by a docking session's
	// soup commands, which are not reconstructed, so it is always nil here
	if (fDynArray7c != nil)
	{
		newton_try
		{
			delete fDynArray7c;
		}
		newton_catch_all
		{ }
		end_try;
		fDynArray7c = nil;
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
// 'dock' a docking session (NOT YET: kCommErrMethodNotImplemented).
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
			// NOT YET: ReadInitiateDocking, WriteNewtonName, the desktop
			// info ('dinf'), the icons ('wicn'), the timeout ('stim') or
			// a result ('dres'), and from protocol 10 the password
			// exchange
			Throw(exLongErrorException, (void*) (intptr_t) kCommErrMethodNotImplemented, nil);
		}
		else
			Throw(exLongErrorException, (void*) (intptr_t) (fCommand == kDRequestToDock ? kDockErrRequestToDock : kDockErrBadHeader), nil);
	}
	newton_catch_all
	{
		ProcessException(CurrentException());
	}
	end_try;
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
// (CompatabilityHacks), or a docking session's commands (NOT YET).  done
// says whether the session is over.  ==> fError.
long
TDocker::DoConnection(RefArg arg1, RefArg arg2, RefArg callback, Boolean* done)
{
	if (fCleanedUp)
		return noErr;
	fStopping = false;
	Boolean keyboard = false;
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
				// NOT YET: the docking session - a sync or restore
				// started ('ssyn', 'rrst') or a result, then the
				// commands read and carried out (ProcessCommand,
				// KeyboardProcessCommand) until done
				Throw(exLongErrorException, (void*) (intptr_t) kCommErrMethodNotImplemented, nil);
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
		if (fError == noErr && fState == kDockStateSync && keyboard)
			fSelectiveSyncOK = true;
	}
	else
	{
		if (!keyboard)
			*done = CleanUpIfStopping(*done);
		if (fError == -16005)
			fError = noErr;
	}
	fField6c = NILREF;
	fField70 = NILREF;
	fField60 = NILREF;
	FreeCurrentStore();
	fCurrentSoup = NILREF;
	fField28 = NILREF;
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
// After a native: no error (or kDockErrAlreadyDocking, or -16005) notes the
// time the desktop was last heard from (if touch); any other error deletes
// the docker and leaves the frame not connected.  An error is then thrown
// if throwIt says so.
void
CleanUpDockerIfError(RefArg connection, long error, Boolean touch, Boolean throwIt)
{
	if (error == noErr || error == kDockErrAlreadyDocking || error == -16005)
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
	if (error == noErr || error == kDockErrAlreadyDocking)
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
	RegisterNativeFunction("FConnBuildStoreFrame", (void*) FConnBuildStoreFrame, 2);
	RegisterNativeFunction("FConnGetSyncChanges", (void*) FConnGetSyncChanges, 0);
	RegisterNativeFunction("FConnectionState", (void*) FConnectionState, 0);
	RegisterNativeFunction("FConnSetState", (void*) FConnSetState, 1);
	RegisterNativeFunction("FConnDesktopType", (void*) FConnDesktopType, 0);
	RegisterNativeFunction("FConnBytesAvailable", (void*) FConnBytesAvailable, 0);
	RegisterNativeFunction("FConnGetCurrentStore", (void*) FConnGetCurrentStore, 0);
	RegisterNativeFunction("FConnAbort", (void*) FConnAbort, 1);
}
