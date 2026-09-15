/*
	File:		user/UserPorts.cpp

	Contains:	TUPort, TUMsgToken and TUAsyncMessage (UserPorts.h): the user's
				view of the kernel's ports and messages.

				A synchronous Send/SendRPC uses the task's built-in message and
				shared memory (kBuiltInSMemMsgId / kBuiltInSMemId); an async one
				uses a TUAsyncMessage's.  Receive fetches the sender's content
				into the caller's buffer and either finishes the message at once
				or, when the caller asked for a token, leaves it for
				TUMsgToken::ReplyRPC / CashMessageToken.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "UserPorts.h"
#include "UserGlobals.h"
#include "os600/ObjectMessage.h"
#include "os600/GenericSWISelectors.h"
#include "OSErrors.h"

// a content pointer of all ones (kPortSend_BufferAlreadySet) means "leave the
// message's buffer as it is"
static void* const kNoContent = (void*) (uintptr_t) (ULong) kPortSend_BufferAlreadySet;

static inline Boolean
CopyOK(long err)
{
	return err == noErr || err == kError_Size_To_Large_Copy_Truncated;
}


/* -------------------------------------------------------------------------------
	TUPort
------------------------------------------------------------------------------- */

// ROM 0x00257b78 __ct__6TUPortFUl
TUPort::TUPort(TObjectId id)
	: TUObject(id)
{
}


// ROM 0x00257bb0 Init__6TUPortFv
long
TUPort::Init()
{
	ObjectMessage msg;
	return MakeObject(kObjectPort, &msg, kObjectMessage_HeaderSize);
}


// ROM 0x00257bd4 SendGoo__6TUPortFUlT1PvN31UcT1P5TTime
// The common send: point the message at the content, arm its timeout and/or
// delayed-send time, and hand it to the kernel.
long
TUPort::SendGoo(TObjectId msgId, TObjectId replyId, void* content, ULong size, ULong msgType, ULong flags, Boolean urgent, TTimeout timeout, TTime* futureTimeToSend)
{
	long err;
	if (urgent)
		flags |= kPortFlags_Urgent;
	if (content != kNoContent && (err = SMemSetBufferSWI(msgId, content, size, kSMemReadOnly)) != noErr)
		return err;
	if (timeout != kNoTimeout)
		flags |= kPortFlags_WantTimeout;
	if (futureTimeToSend != nil)
	{
		flags |= kPortFlags_WantDelay;
		if ((err = SMemMsgSetTimerParmsSWI(msgId, timeout, futureTimeToSend->time.lo, futureTimeToSend->time.hi)) != noErr)
			return err;
	}
	else if ((flags & kPortFlags_WantTimeout) && (err = SMemMsgSetTimerParmsSWI(msgId, timeout, 0, 0)) != noErr)
		return err;
	return PortSendSWI(fId, msgId, replyId, msgType, flags);
}


// ROM 0x00257ca0 SendRPCGoo__6TUPortFUlT1PUlPvN31UcT4N21P5TTime
// A send with a reply buffer; after a synchronous call the reply's size is
// read back.
long
TUPort::SendRPCGoo(TObjectId msgId, TObjectId replyId, ULong* returnSize, void* content, ULong size, ULong msgType, ULong flags, Boolean urgent, void* replyBuf, ULong replySize, TTimeout timeout, TTime* futureTimeToSend)
{
	long err;
	if (replyBuf != kNoContent && (err = SMemSetBufferSWI(replyId, replyBuf, replySize, kSMemReadWrite)) != noErr)
		return err;
	err = SendGoo(msgId, replyId, content, size, msgType, flags, urgent, timeout, futureTimeToSend);
	if (err == noErr && (flags & kPortFlags_Async) == 0)
		err = SMemGetSizeSWI(replyId, returnSize, nil, nil);
	return err;
}


// ROM 0x00257d50 Receive__6TUPortFPUlPvUlP10TUMsgTokenT1N23UcT8
// Waits for a message (with the task's built-in message), then collects it:
// a collected-receiver completion is unwrapped with CheckForDone, the content
// is copied out (or only measured, tokenOnly), and the message is finished -
// unless the caller took a token and there is a reply to make (or asked for
// the token alone), when finishing is the caller's job.  A message that
// expected a reply but gets none is finished with
// kError_Receiver_Did_Not_Do_RPC.
long
TUPort::Receive(ULong* returnSize, void* content, ULong size, TUMsgToken* token, ULong* returnMsgType, TTimeout timeout, ULong msgFilter, Boolean onMsgAvail, Boolean tokenOnly)
{
	ULong senderMsgId = 0, replyMemId = 0, msgType = 0, signature = 0, gotSize = 0;
	TObjectId rcvrMsgId = 0;
	if (token != nil)
	{
		token->fMsgId = 0;
		token->fReplyId = 0;
		token->fSignature = 0;
		token->fRcvrMsgId = 0;
	}
	ULong flags = onMsgAvail ? kPortFlags_ReceiveOnMsgAvail : 0;
	long err;
	if (timeout != kNoTimeout)
	{
		flags |= kPortFlags_WantTimeout;
		if ((err = SMemMsgSetTimerParmsSWI(kBuiltInSMemMsgId, timeout, 0, 0)) != noErr)
			return err;
	}
	err = PortReceiveSWI(fId, kBuiltInSMemMsgId, msgFilter, flags, &senderMsgId, &replyMemId, &msgType, &signature);
	if (err != noErr)
		return err;

	long copyErr = noErr;
	if (msgType & kMsgType_CollectedReceiver)
	{
		TObjectId collector = senderMsgId;
		copyErr = SMemMsgCheckForDoneSWI(collector, 0, &senderMsgId, &replyMemId, &msgType, &signature);
		if (copyErr == noErr)
		{
			msgType |= kMsgType_CollectedReceiver;
			rcvrMsgId = collector;
		}
	}
	if (copyErr == noErr && (msgType & kMsgType_CollectedSender) == 0)
	{
		if (!tokenOnly)
			copyErr = SMemCopyFromSharedSWI(senderMsgId, content, size, 0, senderMsgId, signature, &gotSize);
		else
			copyErr = SMemGetSizeSWI(senderMsgId, &gotSize, nil, nil);
	}

	if (returnMsgType != nil)
		*returnMsgType = msgType;
	if (returnSize != nil)
		*returnSize = gotSize;
	if (token != nil)
	{
		token->fMsgId = senderMsgId;
		token->fReplyId = replyMemId;
		token->fSignature = signature;
		token->fRcvrMsgId = rcvrMsgId;
		if (CopyOK(copyErr) && (tokenOnly || replyMemId != 0))
			return copyErr;
	}
	long doneResult = noErr;
	if (replyMemId != 0)
		doneResult = kError_Receiver_Did_Not_Do_RPC;
	if (copyErr != noErr)
		doneResult = copyErr;
	SMemMsgMsgDoneSWI(senderMsgId, doneResult, signature);
	return copyErr;
}


// ROM 0x00257f54 Receive__6TUPortFP14TUAsyncMessageUlT2Uc
// An asynchronous receive on the async message's behalf; with no message it
// is a peek (kPortFlags_IsMsgAvail) with the built-in message.
long
TUPort::Receive(TUAsyncMessage* async, TTimeout timeout, ULong msgFilter, Boolean onMsgAvail)
{
	ULong flags = (onMsgAvail ? kPortFlags_ReceiveOnMsgAvail : 0) | kPortFlags_Async;
	TObjectId msgId;
	if (async == nil)
	{
		flags |= kPortFlags_IsMsgAvail;
		msgId = kBuiltInSMemMsgId;
	}
	else
	{
		msgId = async->GetMsgId();
		if (timeout != kNoTimeout)
		{
			flags |= kPortFlags_WantTimeout;
			long err = SMemMsgSetTimerParmsSWI(msgId, timeout, 0, 0);
			if (err != noErr)
				return err;
		}
	}
	return PortReceiveSWI(fId, msgId, msgFilter, flags, nil, nil, nil, nil);
}


// ROM 0x0025808c ResetMsgFilter__6TUPortFP14TUAsyncMessageUl
long
TUPort::ResetMsgFilter(TUAsyncMessage* async, ULong msgFilter)
{
	return PortResetFilterSWI(fId, async->GetMsgId(), msgFilter);
}


// ROM 0x00258098 Reset__6TUPortFUlT1
long
TUPort::Reset(ULong sendersResetFlags, ULong receiversResetFlags)
{
	return GenericSWI(kGeneric_PortReset, fId, sendersResetFlags, receiversResetFlags);
}


/* -------------------------------------------------------------------------------
	TUMsgToken
------------------------------------------------------------------------------- */

// ROM 0x002579f8 CashMessageToken__10TUMsgTokenFPUlPvUlT3Uc
// Copies the sender's content out of a message received tokenOnly, and
// finishes the message if it wants no reply (or the copy failed).
long
TUMsgToken::CashMessageToken(ULong* returnSize, void* content, ULong size, ULong offset, Boolean copyDone)
{
	long err = SMemCopyFromSharedSWI(fMsgId, content, size, offset, fMsgId, fSignature, returnSize);
	long doneResult = CopyOK(err) ? noErr : err;
	if ((fReplyId == 0 && copyDone) || doneResult != noErr)
		SMemMsgMsgDoneSWI(fMsgId, doneResult, fSignature);
	return err;
}


// ROM 0x0025800c ReplyRPC__10TUMsgTokenFPvUll
// Copies the reply into the sender's reply memory and finishes the message.
long
TUMsgToken::ReplyRPC(void* content, ULong size, long replyResult)
{
	if (content != kNoContent && size != 0)
	{
		long err = SMemCopyToSharedSWI(fReplyId, content, size, 0, fMsgId, fSignature);
		if (err != noErr)
		{
			SMemMsgMsgDoneSWI(fMsgId, replyResult, fSignature);
			return err;
		}
	}
	return SMemMsgMsgDoneSWI(fMsgId, replyResult, fSignature);
}


// ROM 0x00257a90 GetUserRefCon__10TUMsgTokenFPUl
// The receiver's own message's ref con if this was an async receive, else
// the sender's.
long
TUMsgToken::GetUserRefCon(ULong* refConPtr)
{
	return SMemMsgGetUserRefConSWI(fRcvrMsgId != 0 ? fRcvrMsgId : fMsgId, refConPtr);
}


/* -------------------------------------------------------------------------------
	TUAsyncMessage
------------------------------------------------------------------------------- */

// ROM 0x002580ac __ct__14TUAsyncMessageFv
TUAsyncMessage::TUAsyncMessage()
{
}


// ROM 0x002580e8 __ct__14TUAsyncMessageFRC14TUAsyncMessage
TUAsyncMessage::TUAsyncMessage(const TUAsyncMessage& copy)
{
	fMsg.CopyObject(copy.fMsg.fId);
	fReplyMem.CopyObject(copy.fReplyMem.fId);
}


// ROM 0x00258144 __ct__14TUAsyncMessageFUlT1
TUAsyncMessage::TUAsyncMessage(TObjectId sMemMsg, TObjectId replyMem)
{
	fMsg.CopyObject(sMemMsg);
	fReplyMem.CopyObject(replyMem);
}


// ROM 0x002581e8 __dt__14TUAsyncMessageFv
TUAsyncMessage::~TUAsyncMessage()
{
}


// ROM 0x002581a4 __as__14TUAsyncMessageFRC10TUMsgToken
// Take over the message a token refers to: the receiver's message of an
// async receive, or the sender's message and its reply memory.
void
TUAsyncMessage::operator=(const TUMsgToken& copy)
{
	if (copy.fRcvrMsgId != 0)
		fMsg.CopyObject(copy.fRcvrMsgId);
	else
	{
		fMsg.CopyObject(copy.fMsgId);
		fReplyMem.CopyObject(copy.fReplyId);
	}
}


// ROM 0x0025828c SetCollectorPort__14TUAsyncMessageFUl
// The port that receives the message (as a collected sender) when the
// asynchronous call completes: the same SWI as SetMsgAvailPort.
long
TUAsyncMessage::SetCollectorPort(TObjectId portId)
{
	return SMemMsgSetMsgAvailPortSWI(fMsg, portId);
}


// ROM 0x00258244 Init__14TUAsyncMessageFUc
long
TUAsyncMessage::Init(Boolean forSendRPC)
{
	long err = fMsg.Init();
	if (err == noErr && forSendRPC && (err = fReplyMem.Init()) != noErr)
		fMsg.CopyObject(0);
	return err;
}


// ROM 0x00257aa4 GetResult__14TUAsyncMessageFPUlN31
long
TUAsyncMessage::GetResult(TObjectId* sentbyId, TObjectId* replymemId, ULong* msgType, ULong* signature)
{
	return SMemMsgCheckForDoneSWI(fMsg, 0, sentbyId, replymemId, msgType, signature);
}


// ROM 0x00257adc BlockTillDone__14TUAsyncMessageFPUlN31
long
TUAsyncMessage::BlockTillDone(TObjectId* sentbyId, TObjectId* replymemId, ULong* msgType, ULong* signature)
{
	return SMemMsgCheckForDoneSWI(fMsg, kSMemMsgFlags_BlockTillDone, sentbyId, replymemId, msgType, signature);
}


// ROM 0x00257b14 Abort__14TUAsyncMessageFv
long
TUAsyncMessage::Abort()
{
	return SMemMsgCheckForDoneSWI(fMsg, kSMemMsgFlags_Abort, nil, nil, nil, nil);
}


// ROM 0x00257b40 Abort__14TUAsyncMessageFP10TUMsgTokenPUl
long
TUAsyncMessage::Abort(TUMsgToken* token, ULong* msgType)
{
	token->fRcvrMsgId = fMsg;
	return SMemMsgCheckForDoneSWI(fMsg, kSMemMsgFlags_Abort, &token->fMsgId, &token->fReplyId, msgType, &token->fSignature);
}
