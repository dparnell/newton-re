/*
	File:		SharedMem.cpp

	Contains:	TSharedMem and TSharedMemMsg construction.  Completion
				(CompleteMsg & co.) and the destructor live in Port.cpp with
				the port code they are entangled with.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SharedMem.h"
#include "NewtErrors.h"

#include <stddef.h>


// ROM 0x001dfa54 Init__10TSharedMemFP12TEnvironment
NewtonErr
TSharedMem::Init(TEnvironment* environment)
{
	fBuffer = nil;
	fSize = 0;
	fFlags = 0;
	fEnvironment = environment;
	return noErr;
}


// ROM 0x001e017c __ct__13TSharedMemMsgFv
// The queue items construct themselves; fSenders links messages through
// their fPortQItem (the ROM passes the literal offset 0x80).
TSharedMemMsg::TSharedMemMsg()
	: fSenders(offsetof(TSharedMemMsg, fPortQItem))
{
}


// ROM 0x001e03f4 Init__13TSharedMemMsgFP12TEnvironment
NewtonErr
TSharedMemMsg::Init(TEnvironment* environment)
{
	TSharedMem::Init(environment);
	fTimeout = 0;
	fExpiryTime.hi = 0;
	fExpiryTime.lo = 0;
	fTimerFlags = 0;
	fPortId = 0;
	fStatus = 0;
	fUserRefCon = nil;
	fReplyMemId = 0;
	fMsgFlags = 0;
	fFilter = 0;
	fSenderMsgId = 0;
	fSenderReplyMemId = 0;
	fSenderMsgFlags = 0;
	fSequenceNo = 0;
	fMsgAvailPortId = 0;
	fNotifyId = 0;
	fSenderTaskId = 0;
	fCurrentSequence = 0;
	fSequenceCounter = 1;
	fCopyingTaskId = 0;
	return noErr;
}
