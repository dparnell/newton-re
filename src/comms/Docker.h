/*
	File:		comms/Docker.h

	Contains:	The docker: the Newton's side of the desktop connection's
				'dock' protocol, over a TEzEndpointPipe (EzEndpointPipe.h).
				A command on the wire is a header - the words 'newt',
				'dock', the command and the length of its data - then the
				data, padded to a multiple of four; every word big-endian.

				TEzPipeProtocol is the header's framing (the two protocol
				words and the pipe); TDocker is the session.  Connect makes
				the pipe from the Connection application's options frame,
				says 'rtdk' (ready to dock, protocol version 9) and reads
				what the desktop answers: 'lpkg' is a package to load (the
				old package downloader's session, which is all there is to
				it: CompatabilityHacks), 'dock' a docking session.
				DoConnection forks the NewtonScript world (so the machine
				stays responsive meanwhile) and then carries the session
				out.

				The protocol frame's native methods (the ROM's docker frame
				0x006482d1, which the Connection application's dtEndpoint
				is) are FConn*; the docker object hangs off the frame's
				conncobject slot as an address Ref.

				A docking session: 'dock' (the kind of session), the
				Newton's 'name', the desktop's 'dinf' answered by 'ninf'
				(the protocol both speak, and each side's challenge), the
				icons and the timeout, then the passwords - each side
				sends the other's challenge encrypted under its own key
				(utility/DES.h; the empty password's key is accepted too).
				Then ProcessCommand carries out the desktop's commands:
				packages ('lpkg'), the session's kind ('ssyn', 'rrst',
				'rins', 'dsnc'), the time, the timeout, the icons,
				cancelling, and the protocol extensions (a function the
				Connection application installed for a command).

				NOT YET: the soup, entry, cursor, store, package-list,
				patch, slip and function commands - each is answered
				'unkn', as a Newton that does not know it would; the
				keyboard passthrough.

				The ROM's class; its declaration is not in the DDK, so the
				names of the fields are ours, their order the ROM's (0xb8
				bytes on the ARM).

	Reconstructed from the MP2x00 US ROM (0x00092664-0x0009b40c); each
	function cites its origin.  docs/comms/README.md, "The desktop
	connection (Dock)".
*/

#ifndef __COMMS_DOCKER_H
#define __COMMS_DOCKER_H

#ifndef __COMMS_EZENDPOINTPIPE_H
#include "EzEndpointPipe.h"
#endif
#include "objects.h"
#include "NewtonExceptions.h"
#include "DES.h"

// the commands (what the words spell)
enum
{
	kDNewtonDock		= 'newt',
	kDDock				= 'dock',
	kDRequestToDock		= 'rtdk',
	kDLoadPackage		= 'lpkg',
	kDDisconnect		= 'disc',
	kDResult			= 'dres',
	kDDesktopInfo		= 'dinf',
	kDWhichIcons		= 'wicn',
	kDSetTimeout		= 'stim',
	kDSync				= 'ssyn',
	kDRestore			= 'rrst',
	kDOperationCanceled	= 'opca',
	kDOpCanceledAck		= 'ocaa',
	kDNewtonName		= 'name',
	kDNewtonInfo		= 'ninf',
	kDPassword			= 'pass',
	kDPWWrong			= 'pwbd',
	kDHello				= 'helo',
	kDUnknownCommand	= 'unkn',
	kDOpDone			= 'opdn',
	kDSetStoreGetNames	= 'ssgn',
	kDSetTime			= 'stme',
	kDTime				= 'time',
	kDSetVBOCompression	= 'cvbo',
	kDRestoreAll		= 'rins',
	kDDesktopInControl	= 'dsnc',
	kDSoupInfo			= 'sinf'
};

// the docker's errors
enum
{
	kDockErrBadStoreSignature	= -28001,
	kDockErrDesktopError		= -28012,		// (-28012 = 0xffff9294: an unexpected command)
	kDockErrBadHeader			= -28016,		// not 'newt' 'dock', or not what was expected
	kDockErrNotConnected		= -28022,		// DoConnection before a session was agreed; a bad password
	kDockErrRetryPassword		= -28023,		// the desktop asks for the password again ('pwbd'): not an error
	kDockErrNoDocker			= -28009,		// the frame has no docker
	kDockErrDisconnected		= -28013,		// stopped while connecting
	kDockErrRequestToDock		= -28029,		// the desktop said 'rtdk' back
	kDockErrProtocolVersion		= -28011,		// (-28011 = 0xffff9295)
	kDockErrBadExtension		= -28020,		// no command, or one already extended
	kDockErrBadLength			= -28007,		// a command's data not the length it must be
	kDockErrNoCurrentSoup		= -28006,		// a soup command with no current soup
	kDockErrNoStore				= -28014,		// no store of the name and kind
	kDockErrNoSuchSoup			= -28015,		// no soup of the name
	kDockErrNoCurrentStore		= -28015,		// (the ROM uses the same number) no current store
	kDockErrNoEntry				= -28005,		// an entry command with no entry
	kDockErrNoSuchEntry			= -28008,		// no entry of the id
	kDockErrEntryNotFound		= -28002,		// an entry to change that is not there
	kDockErrBadCursor			= -28026		// no cursor of the number
};

// the session's states (eDockingState)
enum
{
	kDockStateNone		= 1,
	kDockStateSync		= 2,
	kDockStateRestore	= 3,
	kDockStateLoadPackage = 4,
	kDockStateKeyboard	= 9
};


class TEzPipeProtocol
{
public:
	void			ProtocolInit(ULong protocol, ULong subProtocol);
	void			WriteDockerHeader(ULong command, Boolean flush);
	void			SendDockerHeader(ULong command, Boolean flush);
	void			ReadDockerHeader(ULong* command, ULong* length);
	void			FindDockerHeader(ULong* command, ULong* length);

	TEzEndpointPipe*	fPipe;				// +0x00
	ULong			fProtocol;				// +0x04  'newt'
	ULong			fSubProtocol;			// +0x08  'dock'
};


// A growable array of words in a handle, thirty at a time: the command
// words of the protocol extensions installed.
class TDockerDynArray
{
public:
					TDockerDynArray();
					~TDockerDynArray();

	NewtonErr		Add(ULong value);
	NewtonErr		AddAndReplaceZero(ULong value, long* index);
	long			Find(long value);
	void			Replace(long index, ULong value);

	Handle			fWords;					// +0x00
	ULong			fCount;					// +0x04
	ULong			fAllocated;				// +0x08
};


// The cursors a desktop opened ('qury'), by the number it was given: an
// array whose free slots are nil.
class TCursorArray
{
public:
					TCursorArray();
					~TCursorArray();

	ULong			Add(RefArg cursor);
	Ref				Get(ULong index);
	void			Remove(ULong index);

	RefStruct		fCursors;				// +0x00  (the ROM's own RefHandle)
};

class TDocker : public TEzPipeProtocol
{
public:
					TDocker();
					~TDocker();

	long			Connect(RefArg connection, RefArg options, RefArg password);
	long			DoConnection(RefArg arg1, RefArg arg2, RefArg callback, Boolean* done);
	void			Stop(void);
	Boolean			AbortConnection(long error);
	Ref				GetState(void);
	void			SetState(long state);
	long			GetPlatform(void);
	Ref				GetSyncChanges(void);
	Ref				GetCurrentStore(void);
	ULong			BytesAvailable(Boolean locked);
	long			BroadcastChanges(void);
	long			InstallProtocolExtension(RefArg command, RefArg function, ULong commandWord);
	long			RemoveProtocolExtension(RefArg command, ULong commandWord);

	Boolean			GetTDockerLock(void);
	Boolean			WaitAndLockTDocker(void);
	long			RetryPassword(RefArg password);
	void			UnlockTDocker(void);

	void			ProcessException(Exception* exception);
	void			CleanUpIfError(Boolean force);
	Boolean			CleanUpIfStopping(Boolean done);
	void			TossDataStructures(void);
	void			WaitForStopToComplete(void);
	void			WaitForDisconnect(void);
	void			Delay(ULong ticks);
	void			OutOfMemory(void);

	void			CompatabilityHacks(void);
	void			ReadPackage(void);
	void			FreeCurrentStore(void);

	void			WriteLong(ULong command, ULong value);
	void			WriteResult(long result);
	long			ReadResult(void);
	Boolean			ReadChunk(void* buffer, long length, Boolean flushPadding);
	void			Pad(ULong length);
	void			FlushPadding(ULong length);
	void			FlushCommand(void);
	Ref				ReadRef(RefArg store);
	void			WriteRef(ULong command, RefArg obj);

	// the docking session's handshake
	void			ReadInitiateDocking(void);
	void			WriteNewtonName(void);
	void			ReadDesktopInfo(void);
	void			SetWhichIcons(void);
	void			SetTimeout(void);
	void			WritePassword(RefArg password);
	void			ReadPassword(void);
	void			VerifyPassword(void);

	// its commands: stores
	Ref				MakeStoreFrame(RefArg store);
	void			WriteStoreNames(void);
	void			SetCurrentStore(Boolean andSoups);
	void			ReserveCurrentStore(RefArg store);
	void			SetStoreToDefault(void);
	void			WriteDefaultStore(void);
	void			SetStoreSignature(void);
	// soups
	void			WriteSoupNames(void);
	Ptr				ReadString(ULong length);
	void			ReadCurrentSoup(void);
	void			SetupSoup(void);
	void			VerifySoup(void);
	void			SetCurrentSoup(Boolean withInfo);
	void			WriteSoupInfo(Boolean ifChanged);
	void			WriteIndexDescription(Boolean ifChanged);
	void			SetSoupInfoFrame(void);
	void			SetSoupSignature(void);

	// cursors
	void			ValidateQuery(void);
	void			RemoteQuery(void);
	Ref				RemoteGetCursor(void);
	void			RemoteCursorGotoKey(void);
	void			RemoteCursorMap(void);
	void			RemoteCursorEntry(void);
	void			RemoteCursorMove(void);
	void			RemoteCursorNext(void);
	void			RemoteCursorPrev(void);
	void			RemoteCursorReset(void);
	void			RemoteCursorResetToEnd(void);
	void			RemoteCursorCountEntries(void);
	void			RemoteCursorWhichEnd(void);
	void			RemoteCursorFree(void);
	// entries
	Ref				GetEntryFromID(ULong id);
	void			WriteEntry(ULong command, RefArg entry);
	void			ReturnEntry(ULong command);
	void			AddEntry(Boolean withUniqueID);
	Ref				ConvertEntry(RefArg entry);
	Boolean			IsDuplicateEntry(RefArg entry);
	void			ChangeEntry(void);
	void			ReplaceEntryContents(RefArg entry);
	void			DeleteEntries(void);
	void			EmptyOrDelete(ULong command);
	void			AddChangedSoup(RefArg change, ULong count);
	void			ReadSourceVersion(void);
	Boolean			ShouldBackupEntry(RefArg entry);
	long			GetSoupIDCount(RefArg cursor);
	void			WriteSoupIDs(void);
	void			WriteChangedIDs(void);

	// making, sending and backing up soups
	void			CreateSoup(void);
	void			CreateSoupFromSoupDef(void);
	Ref				GetBackupCursor(void);
	void			CheckCancel(ULong* lastLook);
	void			SendSoup(void);
	void			FinishSequence(short* count, short value);
	Boolean			SoupChangedSinceLastBackup(void);
	void			ClearSoupDirty(void);
	void			BackupSoup(void);

	void			ProcessCommand(Boolean* done, Boolean* operationDone);
	Boolean			CheckProtocolExtension(ULong command, Boolean* result);
	Boolean			CheckProtocolPatch(ULong command, Boolean* result);

	// DEVIATION: the ROM keeps its Refs in RefHandles it allocates itself
	// (AllocateRefHandle, stackPos 0); RefStructs are the same thing, made
	// and given back in the same order
	RefStruct		fDoConnectionArg;		// +0x0c  DoConnection's second argument
	RefStruct		fCurrentStore;			// +0x10
	RefStruct		fCurrentSoup;			// +0x14
	RefStruct		fField18;				// +0x18
	RefStruct		fCallback;				// +0x1c  the package callback
	RefStruct		fConnection;			// +0x20  the protocol frame
	RefStruct		fField24;				// +0x24
	RefStruct		fQuery;					// +0x28  a cursor over the whole current soup (ValidateQuery)
	Boolean			fIsDirectorySoup;		// +0x2c  the current soup is the store's directory (the metasoup)
	Boolean			fIsSystemSoup;			// +0x2d  the System soup
	Boolean			fIsPackageSoup;			// +0x2e  the Extras (packages) soup
	Boolean			fSessionStarted;		// +0x2f  the session is under way (the desktop has spoken)
	Boolean			fInExtension;			// +0x30  a protocol extension is running
	Boolean			fField31;				// +0x31
	Boolean			fLocked;				// +0x32
	long			fVBOCompression;		// +0x34  'cvbo': large binaries written compressed (2 always, 1 for the packages soup)
	long			fField38;				// +0x38
	long			fField3c;				// +0x3c
	RefStruct		fSyncChanges;			// +0x40
	ULong			fCommand;				// +0x44  the last header read
	ULong			fLength;				// +0x48
	ULong			fProtocolVersion;		// +0x4c
	long			fError;					// +0x50
	TCursorArray*	fCursors;				// +0x54
	ULong			fManufacturer;			// +0x58  the Newton's, as its name says them
	ULong			fMachineType;			// +0x5c
	long			fSourceVersion;			// +0x60  'sver': the version of the Newton the data came from (2 unless told)
	ULong			fSourceManufacturer;	// +0x64  and its manufacturer and machine
	ULong			fSourceMachineType;		// +0x68
	RefStruct		fConversionFrame;		// +0x6c  ConvertEntry's (NOT YET)
	RefStruct		fOwnerApp;				// +0x70
	ULong			fDesktopTime;			// +0x74  'stme': the desktop's clock (minutes)
	ULong			fTimeSet;				// +0x78  and ours when it said so
	TDockerDynArray*	fChangedIDs;		// +0x7c  the entries changed since the desktop's time ('gids', 'gcid')
	TDockerDynArray*	fExtensionCommands;	// +0x80  the protocol extensions' commands
	RefStruct		fExtensions;			// +0x84  and their functions, in the same order
	RefStruct		fDesktopApps;			// +0x88
	DESWord			fDesktopChallenge[2];	// +0x8c  what the desktop wants encrypted with the password
	DESWord			fNewtonChallenge[2];	// +0x94  what the Newton wants encrypted
	DESWord			fKey[2];				// +0x9c  the password's key
	long			fState;					// +0xa4
	long			fPlatform;				// +0xa8
	Boolean			fSessionAgreed;			// +0xac  DoConnection may go ahead
	Boolean			fHasArg1;				// +0xad  DoConnection's first argument was not nil
	Boolean			fLoadPackageOnly;		// +0xae  the desktop said 'lpkg'
	Boolean			fStopping;				// +0xaf
	Boolean			fStopDone;				// +0xb0
	Boolean			fPipeOpen;				// +0xb1
	Boolean			fSelectiveSyncOK;		// +0xb2
	Boolean			fCleanedUp;				// +0xb3
	Boolean			fFieldb4;				// +0xb4
};


// the docker a protocol frame holds (throwing kDockErrNoDocker when asked
// to and it has none), and a docker kept in it
TDocker*	GetTheDocker(RefArg connection, Boolean mustExist);
void		SaveTheDocker(RefArg connection, TDocker* docker);
void		CleanUpDockerIfError(RefArg connection, long error, Boolean touch, Boolean throwIt);
Ref			FDefaultStore(RefArg rcvr);

void		RegisterDockerNatives(void);

#endif	/* __COMMS_DOCKER_H */
