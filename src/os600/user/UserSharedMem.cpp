/*
	File:		user/UserSharedMem.cpp

	Contains:	TUSharedMem and TUSharedMemMsg (UserSharedMem.h): handles on
				kernel shared memory (a buffer exposed to other tasks) and shared
				memory messages (the same plus the message machinery ports use).
				Thin wrappers over the SMem*SWI system calls.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "UserSharedMem.h"
#include "UserPorts.h"
#include "UserGlobals.h"
#include "os600/ObjectMessage.h"


/* -------------------------------------------------------------------------------
	TUSharedMem
------------------------------------------------------------------------------- */

// ROM 0x0025a5ac Init__11TUSharedMemFv
long
TUSharedMem::Init()
{
	ObjectMessage msg;
	return MakeObject(kObjectSharedMem, &msg, kObjectMessage_HeaderSize);
}


// ROM 0x0025a5d0 SetBuffer__11TUSharedMemFPvUlT2
long
TUSharedMem::SetBuffer(void* buffer, ULong size, ULong permissions)
{
	return SMemSetBufferSWI(fId, buffer, size, permissions);
}


// ROM 0x0025a5e8 GetSize__11TUSharedMemFPUlPPv
long
TUSharedMem::GetSize(ULong* returnSize, void** returnBuffer)
{
	return SMemGetSizeSWI(fId, returnSize, returnBuffer, nil);
}


// ROM 0x0025a5f4 CopyToShared__11TUSharedMemFPvUlT2P10TUMsgToken
// A token names the message the memory came with, which lets the kernel
// check the copy is legitimate.
long
TUSharedMem::CopyToShared(void* buffer, ULong size, ULong offset, TUMsgToken* token)
{
	TObjectId sendersMsgId = 0;
	ULong signature = 0;
	if (token != nil)
	{
		sendersMsgId = token->GetMsgId();
		signature = token->GetSignature();
	}
	return SMemCopyToSharedSWI(fId, buffer, size, offset, sendersMsgId, signature);
}


// ROM 0x0025a648 CopyFromShared__11TUSharedMemFPUlPvUlT3P10TUMsgToken
long
TUSharedMem::CopyFromShared(ULong* returnSize, void* buffer, ULong size, ULong offset, TUMsgToken* token)
{
	TObjectId sendersMsgId = 0;
	ULong signature = 0;
	if (token != nil)
	{
		sendersMsgId = token->GetMsgId();
		signature = token->GetSignature();
	}
	return SMemCopyFromSharedSWI(fId, buffer, size, offset, sendersMsgId, signature, returnSize);
}


/* -------------------------------------------------------------------------------
	TUSharedMemMsg
------------------------------------------------------------------------------- */

// ROM 0x0025a6ac Init__14TUSharedMemMsgFv
long
TUSharedMemMsg::Init()
{
	ObjectMessage msg;
	return MakeObject(kObjectSharedMemMsg, &msg, kObjectMessage_HeaderSize);
}


// ROM 0x0025a6d0 SetTimerParms__14TUSharedMemMsgFUlP5TTime
// The timeout for the next call the message is used in, and optionally the
// time at which a delayed send goes off.
long
TUSharedMemMsg::SetTimerParms(TTimeout timeout, TTime* delay)
{
	ULong timeLow = 0, timeHigh = 0;
	if (delay != nil)
	{
		timeLow = delay->time.lo;
		timeHigh = delay->time.hi;
	}
	return SMemMsgSetTimerParmsSWI(fId, timeout, timeLow, timeHigh);
}


// ROM 0x0025a6e4 SetMsgAvailPort__14TUSharedMemMsgFUl
long
TUSharedMemMsg::SetMsgAvailPort(TObjectId availPortId)
{
	return SMemMsgSetMsgAvailPortSWI(fId, availPortId);
}


// ROM 0x0025a6ec GetSenderTaskId__14TUSharedMemMsgFPUl
long
TUSharedMemMsg::GetSenderTaskId(TObjectId* theSenderTaskId)
{
	return SMemMsgGetSenderTaskIdSWI(fId, theSenderTaskId);
}


// ROM 0x0025a6f4 GetSize__14TUSharedMemMsgFPUlPPvT1
long
TUSharedMemMsg::GetSize(ULong* returnSize, void** returnBuffer, ULong* refConPtr)
{
	return SMemGetSizeSWI(fId, returnSize, returnBuffer, refConPtr);
}


// ROM 0x0025a5d8 SetUserRefCon__14TUSharedMemMsgFUl
long
TUSharedMemMsg::SetUserRefCon(ULong refCon)
{
	return SMemMsgSetUserRefConSWI(fId, refCon);
}


// ROM 0x0025a5e0 GetUserRefCon__14TUSharedMemMsgFPUl
long
TUSharedMemMsg::GetUserRefCon(ULong* refConPtr)
{
	return SMemMsgGetUserRefConSWI(fId, refConPtr);
}
