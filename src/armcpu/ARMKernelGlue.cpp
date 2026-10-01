/*
	File:		armcpu/ARMKernelGlue.cpp

	Contains:	The user-side OS objects ARM code makes (ARMProtocols.h):
				event handlers, events, async messages, ports, locking
				semaphores, times and timers, and a few odds and ends.

				An ARM object of one of these classes is its bytes in the
				ARM world, in the ROM's layout; the glue keeps a host object
				of the class beside it (bound to the ARM address) and does
				each call on that.  An event handler is the one that is
				called back: its host object is a TAEventHandler of the
				host's whose AETestEvent, AEHandlerProc, AECompletionProc
				and IdleProc call the ARM object's own virtual functions -
				its vtable (an array of branches, the destructor first) as
				the ARM code built it.

				Events cross between the two worlds as messages, and a
				TAEvent's header is two ULongs - 32 bits each on the ARM, as
				wide as a pointer on the host.  DEVIATION: every message the
				ARM code sends is taken to begin with a TAEvent (an ARM
				driver sends events: to the newt world's port, to its own
				handlers) and is widened on the way out, the rest of its
				bytes kept as they are; a message handed to an ARM handler,
				and a reply handed back, is narrowed again.

	Written by:	the reconstruction (DEVIATION: the ROM runs these where they
				lie).  Each glue function cites the ROM function it answers
				for.
*/

#include "ARMWorld.h"
#include "ARMProtocols.h"
#include "AEventHandler.h"
#include "AEvents.h"
#include "UserPorts.h"
#include "UserSemaphore.h"
#include "UserSharedMem.h"
#include "NewtonTime.h"
#include "DelayTimer.h"
#include "FIQTimer.h"
#include "MemObjManager.h"
#include "NewtonMemory.h"
#include "Objects.h"
#include "UserGlobals.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"
#include "Host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void	Reboot(NewtonErr error, ULong rebootType, Boolean safe);

// A growable array (the standard containers reach <locale.h>)
template <class T>
class KVec
{
public:
				KVec() : fItems(nil), fCount(0), fCapacity(0) { }
	size_t		size() const					{ return fCount; }
	T&			operator[](size_t i)			{ return fItems[i]; }
	void		push_back(const T& v)
	{
		if (fCount == fCapacity)
		{
			fCapacity = fCapacity == 0 ? 16 : fCapacity * 2;
			fItems = (T*) realloc((void*) fItems, fCapacity * sizeof(T));
		}
		fItems[fCount++] = v;
	}
	void		erase(size_t i)
	{
		for (size_t j = i + 1; j < fCount; j++)
			fItems[j - 1] = fItems[j];
		fCount--;
	}
private:
	T*			fItems;
	size_t		fCount;
	size_t		fCapacity;
};

static bool	gTrace = false;

// the host objects bound to ARM ones
enum { kBoundHandler = 'aeh ', kBoundAsync = 'amsg', kBoundSemaphore = 'lsem', kBoundToken = 'mtok' };
struct Bound { uint32_t fARM; uint32_t fKind; void* fHost; };
static KVec<Bound>	gBound;

static void
Bind(uint32_t arm, uint32_t kind, void* host)
{
	Bound b = { arm, kind, host };
	gBound.push_back(b);
}
static void*
BoundTo(uint32_t arm, uint32_t kind)
{
	for (size_t i = 0; i < gBound.size(); i++)
		if (gBound[i].fARM == arm && gBound[i].fKind == kind)
			return gBound[i].fHost;
	return nil;
}
static void
Unbind(uint32_t arm, uint32_t kind)
{
	for (size_t i = 0; i < gBound.size(); i++)
		if (gBound[i].fARM == arm && gBound[i].fKind == kind)
		{
			gBound.erase(i);
			return;
		}
}

static inline uint32_t	BE32(const uint8_t* p)	{ return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
static inline void		PutBE32(uint8_t* p, uint32_t v)	{ p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }


/*------------------------------------------------------------------------------
	E v e n t s   b e t w e e n   t h e   w o r l d s
------------------------------------------------------------------------------*/

static const ULong	kHostEventHeader = sizeof(TAEvent);	// the host's header
static const ULong	kARMEventHeader = 8;				// the ARM's
static const ULong	kWiden = kHostEventHeader - kARMEventHeader;

// an ARM message's size on the host and back
static ULong		HostSize(ULong armSize)		{ return armSize >= kARMEventHeader ? armSize + kWiden : armSize; }
static ULong		ARMSize(ULong hostSize)		{ return hostSize >= kHostEventHeader ? hostSize - kWiden : hostSize; }

// an ARM message (n bytes at arm, read through the calling world) into a
// host buffer of HostSize(n) bytes
static void
Widen(ARMTrapContext* c, uint32_t arm, ULong n, uint8_t* host)
{
	uint8_t* bytes = (uint8_t*) malloc(n + 1);
	for (ULong i = 0; i < n; i++)
	{
		uint8_t b = 0;
		if (c != nil)
			c->Read8(arm + (uint32_t) i, &b);
		else
			ARMRead8(arm + (uint32_t) i, &b);
		bytes[i] = b;
	}
	if (n < kARMEventHeader)
		memcpy(host, bytes, n);
	else
	{
		TAEvent* e = (TAEvent*) host;
		e->fAEventClass = BE32(bytes);
		e->fAEventID = BE32(bytes + 4);
		memcpy(host + kHostEventHeader, bytes + kARMEventHeader, n - kARMEventHeader);
	}
	free(bytes);
}

// a host message of n bytes written to the ARM side, narrowed
static void
Narrow(ARMTrapContext* c, const uint8_t* host, ULong n, uint32_t arm)
{
	ULong armSize = ARMSize(n);
	uint8_t* bytes = (uint8_t*) malloc(armSize + 1);
	if (n < kHostEventHeader)
		memcpy(bytes, host, n);
	else
	{
		const TAEvent* e = (const TAEvent*) host;
		PutBE32(bytes, (uint32_t) e->fAEventClass);
		PutBE32(bytes + 4, (uint32_t) e->fAEventID);
		memcpy(bytes + kARMEventHeader, host + kHostEventHeader, n - kHostEventHeader);
	}
	for (ULong i = 0; i < armSize; i++)
		if (c != nil)
			c->Write8(arm + (uint32_t) i, bytes[i]);
		else
			ARMWrite8(arm + (uint32_t) i, bytes[i]);
	free(bytes);
}

// what an async message is carrying: the host copies of the content and of
// the reply buffer (which must last until the message is done), and where
// the reply goes on the ARM side
struct AsyncRecord
{
	TUAsyncMessage*	fMessage;
	uint8_t*		fContent;
	uint8_t*		fReply;
	ULong			fReplySize;			// the host buffer's
	uint32_t		fARMReply;
};


/*------------------------------------------------------------------------------
	E v e n t   h a n d l e r s
------------------------------------------------------------------------------*/

// a message token as the ARM code sees one (TUMsgToken: msg id, reply id,
// signature, receiver's msg id), the host's own kept beside it
static uint32_t
TokenMirror(TUMsgToken* token)
{
	if (token == nil)
		return 0;
	uint32_t m = ARMMirrorFor(token, 16, kBoundToken);
	ARMWrite32(m + 0, (uint32_t) token->GetMsgId());
	ARMWrite32(m + 4, (uint32_t) token->GetReplyId());
	ARMWrite32(m + 8, (uint32_t) token->GetSignature());
	ARMWrite32(m + 12, (uint32_t) token->GetReceiverMsgId());
	return m;
}

class TARMEventHandler : public TAEventHandler
{
public:
				TARMEventHandler(uint32_t arm) : fARM(arm) { }

	// the ARM object's virtual function in slot n of its vtable
	uint32_t	Virtual(int slot, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0)
				{
					uint32_t vtable = 0;
					ARMRead32(fARM, &vtable);
					uint32_t args[4] = { fARM, a, b, c };
					return ARMCall(vtable + (uint32_t) slot * 4, args, 4);
				}

	Boolean		AETestEvent(TAEvent* event)
				{
					// (a test of the header alone: the event's size is not to hand)
					uint32_t scratch = ARMAlloc(kARMEventHeader, false);
					ARMWrite32(scratch, (uint32_t) event->fAEventClass);
					ARMWrite32(scratch + 4, (uint32_t) event->fAEventID);
					Boolean result = (Boolean) (Virtual(1, scratch) & 0xff);
					ARMFree(scratch);
					return result;
				}

	void		Deliver(int slot, TUMsgToken* token, ULong* size, TAEvent* event)
				{
					ULong hostSize = size != nil ? *size : 0;
					ULong armSize = ARMSize(hostSize);
					uint32_t buffer = 0;
					bool scratch = true;
					AsyncRecord* record = slot == 3 ? RecordOfReply((uint8_t*) event) : nil;
					if (record != nil)
					{
						buffer = record->fARMReply;
						scratch = false;
					}
					else
						buffer = ARMAlloc(armSize + 4, false);
					if (event != nil)
						Narrow(nil, (const uint8_t*) event, hostSize, buffer);
					uint32_t sizeAt = ARMAlloc(4, false);
					ARMWrite32(sizeAt, (uint32_t) armSize);
					fCurrentARM = buffer;
					fCurrentHost = event;
					Virtual(slot, TokenMirror(token), sizeAt, event != nil ? buffer : 0);
					uint32_t newSize = 0;
					ARMRead32(sizeAt, &newSize);
					if (event != nil && scratch)
					{
						// (the event as the ARM code left it, back where it came from)
						Widen(nil, buffer, newSize, (uint8_t*) event);
						if (size != nil)
							*size = HostSize(newSize);
					}
					fCurrentARM = 0;
					fCurrentHost = nil;
					ARMFree(sizeAt);
					if (scratch)
						ARMFree(buffer);
					ARMForgetMirror(token);
				}

	void		AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)		{ Deliver(2, token, size, event); }
	void		AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)	{ Deliver(3, token, size, event); }
	void		IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)			{ Deliver(4, token, size, event); }

	static AsyncRecord*	RecordOfReply(uint8_t* hostBuffer);

	uint32_t	fARM;
	uint32_t	fCurrentARM;			// the event being handled, on the ARM side
	TAEvent*	fCurrentHost;			// and on the host
};

static TARMEventHandler*
HandlerOf(ARMTrapContext& c)
{
	TARMEventHandler* h = (TARMEventHandler*) BoundTo(c.Arg(0), kBoundHandler);
	if (h == nil)
		ThrowMsg("armcpu: not an event handler the ARM code made");
	return h;
}

TAEventHandler*
ARMEventHandlerOf(uint32_t arm)
{
	return (TARMEventHandler*) BoundTo(arm, kBoundHandler);
}

// ROM 0x00025574 __ct__14TAEventHandlerFv
// The ROM's fields (vtable, fNext, fEventClass, fEventID, fIdler: 0x14
// bytes) and a host handler bound to it.  The vtable is the ROM's
// TAEventHandler's until the ARM subclass's constructor puts its own.
static bool
Glue_TAEventHandler_ct(void*, ARMTrapContext& c)
{
	uint32_t self = c.Arg(0);
	if (self == 0)
		self = ARMAlloc(0x14, true);
	c.Write32(self + 0, 0x0001D42C);		// (__vt__14TAEventHandler)
	c.Write32(self + 4, 0);
	c.Write32(self + 8, 0);
	c.Write32(self + 12, 0);
	c.Write32(self + 16, 0);
	Bind(self, kBoundHandler, new TARMEventHandler(self));
	c.Return(self);
	return true;
}

// ROM 0x000255bc __dt__14TAEventHandlerFv
static bool
Glue_TAEventHandler_dt(void*, ARMTrapContext& c)
{
	uint32_t self = c.Arg(0);
	TARMEventHandler* h = (TARMEventHandler*) BoundTo(self, kBoundHandler);
	if (h != nil)
	{
		Unbind(self, kBoundHandler);
		delete h;
	}
	if ((c.Arg(1) & 1) != 0)
		ARMFree(self);
	c.Return(0);
	return true;
}

// ROM 0x00025628 Init__14TAEventHandlerFUlT1
static bool
Glue_TAEventHandler_Init(void*, ARMTrapContext& c)
{
	TARMEventHandler* h = HandlerOf(c);
	NewtonErr err = h->Init((AEEventID) c.Arg(1), (AEEventClass) c.Arg(2));
	c.Write32(c.Arg(0) + 8, c.Arg(2));
	c.Write32(c.Arg(0) + 12, c.Arg(1));
	if (gTrace)
		fprintf(stderr, "[armkernel] event handler %08x for %08x/%08x -> %ld\n", c.Arg(0), c.Arg(2), c.Arg(1), (long) err);
	c.Return((uint32_t) err);
	return true;
}

// ROM 0x0002588c InitIdler__14TAEventHandlerFUl9TimeUnitsT1Uc
static bool
Glue_TAEventHandler_InitIdler(void*, ARMTrapContext& c)
{
	c.Return((uint32_t) HandlerOf(c)->InitIdler(c.Arg(1), (TimeUnits) c.Arg(2), c.Arg(3), (Boolean) (c.Arg(4) & 0xff)));
	return true;
}
// ROM 0x00025950 StopIdle__14TAEventHandlerFv
static bool	Glue_TAEventHandler_StopIdle(void*, ARMTrapContext& c)		{ c.Return((uint32_t) HandlerOf(c)->StopIdle()); return true; }
// ROM 0x0002591c StartIdle__14TAEventHandlerFv
static bool	Glue_TAEventHandler_StartIdle(void*, ARMTrapContext& c)		{ c.Return((uint32_t) HandlerOf(c)->StartIdle()); return true; }
// ROM 0x000259e4 ResetIdle__14TAEventHandlerFUl9TimeUnits
static bool	Glue_TAEventHandler_ResetIdle(void*, ARMTrapContext& c)		{ c.Return((uint32_t) HandlerOf(c)->ResetIdle(c.Arg(1), (TimeUnits) c.Arg(2))); return true; }
// ROM 0x00025980 ResetIdle__14TAEventHandlerFv
static bool	Glue_TAEventHandler_ResetIdle0(void*, ARMTrapContext& c)		{ c.Return((uint32_t) HandlerOf(c)->ResetIdle()); return true; }
// ROM 0x00025880 AETestEvent__14TAEventHandlerFP7TAEvent
static bool	Glue_TAEventHandler_AETestEvent(void*, ARMTrapContext& c)	{ c.Return(1); return true; }
// ROM 0x00025874 AEHandlerProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent
// ROM 0x00025878 AECompletionProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent
// ROM 0x0002587c IdleProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent
static bool	Glue_TAEventHandler_Nothing(void*, ARMTrapContext& c)		{ c.Return(0); return true; }

// ROM 0x0002566c SetReply__14TAEventHandlerFUlP7TAEvent
// (the reply is the event being handled, or one of the ARM code's own,
// widened into a host block that lasts as long as the handler)
static bool
Glue_TAEventHandler_SetReply(void*, ARMTrapContext& c)
{
	TARMEventHandler* h = HandlerOf(c);
	ULong size = c.Arg(1);
	uint32_t event = c.Arg(2);
	if (event == h->fCurrentARM && h->fCurrentHost != nil)
	{
		Widen(&c, event, size, (uint8_t*) h->fCurrentHost);
		h->SetReply(HostSize(size), h->fCurrentHost);
	}
	else
	{
		uint8_t* host = (uint8_t*) malloc(HostSize(size) + 8);
		Widen(&c, event, size, host);
		h->SetReply(HostSize(size), (TAEvent*) host);		// (kept: the reply may go after this returns)
	}
	c.Return(0);
	return true;
}


/*------------------------------------------------------------------------------
	E v e n t s ,   a s y n c   m e s s a g e s ,   p o r t s
------------------------------------------------------------------------------*/

// ROM 0x00025d1c __ct__7TAEventFv
static bool
Glue_TAEvent_ct(void*, ARMTrapContext& c)
{
	uint32_t self = c.Arg(0);
	if (self == 0)
		self = ARMAlloc(8, true);
	c.Write32(self, kNewtEventClass);
	c.Return(self);
	return true;
}

static KVec<AsyncRecord*>	gAsync;

AsyncRecord*
TARMEventHandler::RecordOfReply(uint8_t* hostBuffer)
{
	for (size_t i = 0; i < gAsync.size(); i++)
		if (gAsync[i]->fReply == hostBuffer && hostBuffer != nil)
			return gAsync[i];
	return nil;
}

static AsyncRecord*
AsyncOf(uint32_t arm)
{
	return (AsyncRecord*) BoundTo(arm, kBoundAsync);
}

static AsyncRecord*
AsyncWithMsgId(ULong msgId)
{
	for (size_t i = 0; i < gAsync.size(); i++)
		if (gAsync[i]->fMessage != nil && gAsync[i]->fMessage->GetMsgId() == (TObjectId) msgId)
			return gAsync[i];
	return nil;
}

// ROM 0x00259fe4 __ct__14TUAsyncMessageFv
// (a TUSharedMemMsg and a TUSharedMem: 0x10 bytes, their ids at 0 and 8)
static bool
Glue_TUAsyncMessage_ct(void*, ARMTrapContext& c)
{
	uint32_t self = c.Arg(0);
	if (self == 0)
		self = ARMAlloc(0x10, true);
	for (uint32_t i = 0; i < 0x10; i += 4)
		c.Write32(self + i, 0);
	AsyncRecord* r = new AsyncRecord;
	memset(r, 0, sizeof(AsyncRecord));
	r->fMessage = new TUAsyncMessage;
	Bind(self, kBoundAsync, r);
	gAsync.push_back(r);
	c.Return(self);
	return true;
}

// ROM 0x0025a120 __dt__14TUAsyncMessageFv
static bool
Glue_TUAsyncMessage_dt(void*, ARMTrapContext& c)
{
	uint32_t self = c.Arg(0);
	AsyncRecord* r = AsyncOf(self);
	if (r != nil)
	{
		Unbind(self, kBoundAsync);
		for (size_t i = 0; i < gAsync.size(); i++)
			if (gAsync[i] == r)
			{
				gAsync.erase(i);
				break;
			}
		delete r->fMessage;
		free(r->fContent);
		free(r->fReply);
		delete r;
	}
	if ((c.Arg(1) & 1) != 0)
		ARMFree(self);
	c.Return(0);
	return true;
}

// ROM 0x0025a17c Init__14TUAsyncMessageFUc
static bool
Glue_TUAsyncMessage_Init(void*, ARMTrapContext& c)
{
	AsyncRecord* r = AsyncOf(c.Arg(0));
	if (r == nil)
		ThrowMsg("armcpu: not an async message the ARM code made");
	NewtonErr err = r->fMessage->Init((Boolean) (c.Arg(1) & 0xff));
	c.Write32(c.Arg(0) + 0, (uint32_t) r->fMessage->GetMsgId());
	c.Write32(c.Arg(0) + 8, (uint32_t) r->fMessage->GetReplyMemId());
	c.Return((uint32_t) err);
	return true;
}

// ROM 0x0025a1c4 SetCollectorPort__14TUAsyncMessageFUl
static bool
Glue_TUAsyncMessage_SetCollectorPort(void*, ARMTrapContext& c)
{
	AsyncRecord* r = AsyncOf(c.Arg(0));
	c.Return(r != nil ? (uint32_t) r->fMessage->SetCollectorPort((TObjectId) c.Arg(1)) : (uint32_t) kError_Bad_Parameters);
	return true;
}

// ROM 0x0025a5d8 SetUserRefCon__14TUSharedMemMsgFUl
// (an async message's own: its first member)
static bool
Glue_TUSharedMemMsg_SetUserRefCon(void*, ARMTrapContext& c)
{
	AsyncRecord* r = AsyncOf(c.Arg(0));
	if (r != nil)
		c.Return((uint32_t) r->fMessage->SetUserRefCon(c.Arg(1)));
	else
	{
		uint32_t id = 0;
		c.Read32(c.Arg(0), &id);
		TUSharedMemMsg msg((TObjectId) id);
		c.Return((uint32_t) msg.SetUserRefCon(c.Arg(1)));
	}
	return true;
}

// ROM 0x00259ab0 __ct__6TUPortFUl
static bool
Glue_TUPort_ct(void*, ARMTrapContext& c)
{
	uint32_t self = c.Arg(0);
	if (self == 0)
		self = ARMAlloc(8, true);
	c.Write32(self, c.Arg(1));
	c.Write32(self + 4, 0);
	c.Return(self);
	return true;
}

static TObjectId
PortOf(ARMTrapContext& c)
{
	uint32_t id = 0;
	c.Read32(c.Arg(0), &id);
	return (TObjectId) id;
}

static TTime*
TimeArg(ARMTrapContext& c, uint32_t at, TTime* t)
{
	if (at == 0)
		return nil;
	uint32_t hi = 0, lo = 0;
	c.Read32(at, &hi);
	c.Read32(at + 4, &lo);
	t->time.hi = (SLong) (int32_t) hi;
	t->time.lo = lo;
	return t;
}

// a message whose buffer is set already (os600/user/UserPorts.cpp's)
static void* const kNoContent = (void*) (uintptr_t) (ULong) kPortSend_BufferAlreadySet;
static const uint32_t kARMNoContent = (uint32_t) kPortSend_BufferAlreadySet;

// TUPort's SendGoo and SendRPCGoo (private in the DDK's class), as
// os600/user/UserPorts.cpp does them: the content made the message's
// buffer, the timers set, the port sent to
static long
PortSendGoo(TObjectId port, TObjectId msgId, TObjectId replyId, void* content, ULong size, ULong msgType, ULong flags,
			Boolean urgent, TTimeout timeout, TTime* futureTimeToSend)
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
	return PortSendSWI(port, msgId, replyId, msgType, flags);
}

static long
PortSendRPCGoo(TObjectId port, TObjectId msgId, TObjectId replyId, ULong* returnSize, void* content, ULong size, ULong msgType, ULong flags,
			   Boolean urgent, void* replyBuf, ULong replySize, TTimeout timeout, TTime* futureTimeToSend)
{
	long err;
	if (replyBuf != kNoContent && (err = SMemSetBufferSWI(replyId, replyBuf, replySize, kSMemReadWrite)) != noErr)
		return err;
	err = PortSendGoo(port, msgId, replyId, content, size, msgType, flags, urgent, timeout, futureTimeToSend);
	if (err == noErr && (flags & kPortFlags_Async) == 0)
		err = SMemGetSizeSWI(replyId, returnSize, nil, nil);
	return err;
}

// ROM 0x00259b0c SendGoo__6TUPortFUlT1PvN31UcT1P5TTime
// (msgId, replyId, content, size, msgType, flags, urgent, timeout, futureTime)
static bool
Glue_TUPort_SendGoo(void*, ARMTrapContext& c)
{
	ULong msgId = c.Arg(1), replyId = c.Arg(2), size = c.Arg(4);
	uint32_t content = c.Arg(3);
	ULong hostSize = HostSize(size);
	uint8_t* host = nil;
	if (content != kARMNoContent && content != 0)
	{
		host = (uint8_t*) malloc(hostSize + 8);
		Widen(&c, content, size, host);
	}
	TTime future;
	TTime* futureAt = TimeArg(c, c.Arg(9), &future);
	long err = PortSendGoo(PortOf(c), (TObjectId) msgId, (TObjectId) replyId, content == kARMNoContent ? kNoContent : host, hostSize, c.Arg(5), c.Arg(6), (Boolean) (c.Arg(7) & 0xff),
							(TTimeout) c.Arg(8), futureAt);
	AsyncRecord* r = AsyncWithMsgId(msgId);
	if (r != nil && (c.Arg(6) & kPortFlags_Async) != 0)
	{
		free(r->fContent);
		r->fContent = host;			// (kept until the message is done with)
	}
	else
		free(host);
	if (gTrace)
		fprintf(stderr, "[armkernel] SendGoo to port %lu, %lu bytes -> %ld\n", (unsigned long) PortOf(c), (unsigned long) size, err);
	c.Return((uint32_t) err);
	return true;
}

// ROM 0x00259bd8 SendRPCGoo__6TUPortFUlT1PUlPvN31UcT4N21P5TTime
// (msgId, replyId, returnSize*, content, size, msgType, flags, urgent,
// replyBuf, replySize, timeout, futureTime)
static bool
Glue_TUPort_SendRPCGoo(void*, ARMTrapContext& c)
{
	ULong msgId = c.Arg(1), replyId = c.Arg(2), size = c.Arg(5), flags = c.Arg(7);
	uint32_t returnSizeAt = c.Arg(3), content = c.Arg(4), replyBuf = c.Arg(9), replySize = c.Arg(10);
	ULong hostSize = HostSize(size), hostReplySize = HostSize(replySize);
	uint8_t* host = nil;
	if (content != kARMNoContent && content != 0)
	{
		host = (uint8_t*) malloc(hostSize + 8);
		Widen(&c, content, size, host);
	}
	uint8_t* reply = replyBuf != kARMNoContent && replyBuf != 0 ? (uint8_t*) calloc(hostReplySize + 8, 1) : nil;
	TTime future;
	TTime* futureAt = TimeArg(c, c.Arg(12), &future);
	ULong returned = 0;
	long err = PortSendRPCGoo(PortOf(c), (TObjectId) msgId, (TObjectId) replyId, &returned,
							   content == kARMNoContent ? kNoContent : host, hostSize, c.Arg(6), flags,
							   (Boolean) (c.Arg(8) & 0xff), replyBuf == kARMNoContent ? kNoContent : reply, hostReplySize, (TTimeout) c.Arg(11), futureAt);
	AsyncRecord* r = AsyncWithMsgId(msgId);
	if (r != nil && (flags & kPortFlags_Async) != 0)
	{
		// the reply comes later, to the handler's AECompletionProc
		free(r->fContent);
		free(r->fReply);
		r->fContent = host;
		r->fReply = reply;
		r->fReplySize = hostReplySize;
		r->fARMReply = replyBuf;
	}
	else
	{
		if (err == noErr && reply != nil)
			Narrow(&c, reply, returned, replyBuf);
		if (returnSizeAt != 0)
			c.Write32(returnSizeAt, (uint32_t) ARMSize(returned));
		free(host);
		free(reply);
	}
	if (gTrace)
		fprintf(stderr, "[armkernel] SendRPCGoo to port %lu, %lu bytes%s -> %ld\n", (unsigned long) PortOf(c), (unsigned long) size,
				(flags & kPortFlags_Async) != 0 ? " (async)" : "", err);
	c.Return((uint32_t) err);
	return true;
}

// ROM 0x00259f44 ReplyRPC__10TUMsgTokenFPvUll
static bool
Glue_TUMsgToken_ReplyRPC(void*, ARMTrapContext& c)
{
	TUMsgToken* token = (TUMsgToken*) ARMHostOf(c.Arg(0), kBoundToken);
	if (token == nil)
		ThrowMsg("armcpu: not a message token the host handed over");
	ULong size = c.Arg(2);
	uint8_t* host = (uint8_t*) malloc(HostSize(size) + 8);
	Widen(&c, c.Arg(1), size, host);
	long err = token->ReplyRPC(host, HostSize(size), (long) (int32_t) c.Arg(3));
	free(host);
	c.Return((uint32_t) err);
	return true;
}


/*------------------------------------------------------------------------------
	S e m a p h o r e s
------------------------------------------------------------------------------*/

// ROM 0x0025a478 GetRefCon__16TUSemaphoreGroupFPPv
// (the inline TULockingSemaphore constructor asks it before Init: nought)
static bool
Glue_TUSemaphoreGroup_GetRefCon(void*, ARMTrapContext& c)
{
	if (c.Arg(1) != 0)
		c.Write32(c.Arg(1), 0);
	c.Return(noErr);
	return true;
}

static TULockingSemaphore*
SemaphoreOf(ARMTrapContext& c)
{
	TULockingSemaphore* s = (TULockingSemaphore*) BoundTo(c.Arg(0), kBoundSemaphore);
	if (s == nil)
		ThrowMsg("armcpu: not a semaphore the ARM code made");
	return s;
}

// ROM 0x0025a4c8 Init__18TULockingSemaphoreFv
static bool
Glue_TULockingSemaphore_Init(void*, ARMTrapContext& c)
{
	TULockingSemaphore* s = new TULockingSemaphore;
	NewtonErr err = s->Init();
	Bind(c.Arg(0), kBoundSemaphore, s);
	c.Write32(c.Arg(0), (uint32_t) (TObjectId) *s);
	c.Return((uint32_t) err);
	return true;
}
// ROM 0x0025a298 Acquire__18TULockingSemaphoreF8SemFlags
static bool	Glue_TULockingSemaphore_Acquire(void*, ARMTrapContext& c)	{ c.Return((uint32_t) SemaphoreOf(c)->Acquire((SemFlags) c.Arg(1))); return true; }
// ROM 0x0025a31c Release__18TULockingSemaphoreFv
static bool	Glue_TULockingSemaphore_Release(void*, ARMTrapContext& c)	{ c.Return((uint32_t) SemaphoreOf(c)->Release()); return true; }
// ROM 0x0025a564 __dt__18TULockingSemaphoreFv
static bool
Glue_TULockingSemaphore_dt(void*, ARMTrapContext& c)
{
	TULockingSemaphore* s = (TULockingSemaphore*) BoundTo(c.Arg(0), kBoundSemaphore);
	if (s != nil)
	{
		Unbind(c.Arg(0), kBoundSemaphore);
		delete s;
	}
	if ((c.Arg(1) & 1) != 0)
		ARMFree(c.Arg(0));
	c.Return(0);
	return true;
}


/*------------------------------------------------------------------------------
	T i m e s
------------------------------------------------------------------------------*/

static void
PutTime(ARMTrapContext& c, uint32_t at, const TTime& t)
{
	c.Write32(at, (uint32_t) t.time.hi);
	c.Write32(at + 4, (uint32_t) t.time.lo);
}

// ROM 0x0013d0e0 GetGlobalTime (a TTime returned through r0)
static bool	Glue_GetGlobalTime(void*, ARMTrapContext& c)	{ PutTime(c, c.Arg(0), GetGlobalTime()); c.Return(c.Arg(0)); return true; }
// ROM 0x0013d188 GetTaskTime
static bool	Glue_GetTaskTime(void*, ARMTrapContext& c)		{ PutTime(c, c.Arg(0), GetTaskTime((TObjectId) c.Arg(1))); c.Return(c.Arg(0)); return true; }
// ROM 0x0035e4c0 ConvertTo__5TTimeF9TimeUnits
static bool
Glue_TTime_ConvertTo(void*, ARMTrapContext& c)
{
	TTime t;
	TimeArg(c, c.Arg(0), &t);
	c.Return((uint32_t) t.ConvertTo((TimeUnits) c.Arg(1)));
	return true;
}
// ROM 0x0008e978 __ct__11TDelayTimerFv
static bool
Glue_TDelayTimer_ct(void*, ARMTrapContext& c)
{
	uint32_t self = c.Arg(0);
	if (self == 0)
		self = ARMAlloc(12, true);
	for (uint32_t i = 0; i < 12; i += 4)
		c.Write32(self + i, 0);
	c.Return(self);
	return true;
}
// ROM 0x0008e9bc GetHardwareTime__11TDelayTimerFv
static bool	Glue_TDelayTimer_GetHardwareTime(void*, ARMTrapContext& c)	{ TDelayTimer t; c.Return((uint32_t) t.GetHardwareTime()); return true; }
// ROM 0x0008e9b4 ConvertFromHardwareTime__11TDelayTimerFUl
static bool	Glue_TDelayTimer_ConvertFrom(void*, ARMTrapContext& c)		{ TDelayTimer t; c.Return((uint32_t) t.ConvertFromHardwareTime(c.Arg(1))); return true; }
// ROM 0x0008e9ac ConvertToHardwareTime__11TDelayTimerFUl
static bool	Glue_TDelayTimer_ConvertTo(void*, ARMTrapContext& c)		{ TDelayTimer t; c.Return((uint32_t) t.ConvertToHardwareTime((TTimeout) c.Arg(1))); return true; }
// DEVIATION: a driver waits for its hardware in a loop round TimedOut or a
// short delay, and on the machine an interrupt - the card's, which is often
// what it waits for - comes in meanwhile.  The host delivers interrupts
// only at its safe points (a system call's exit, docs/host-runtime.md), so
// these calls are made safe points too: the interrupts that are due are
// delivered before each answers, unless one is being delivered already.
static void
ARMSafePoint(void)
{
	if (gHostInterruptLevel == 0)
		HostDeliverInterrupts();
}

// (an ARM TDelayTimer: fTimeOutStart +0, fTimeOutDelay +4, the counter +8)
// ROM 0x0008e9f4 ResetTimeOut__11TDelayTimerFUl
static bool
Glue_TDelayTimer_ResetTimeOut(void*, ARMTrapContext& c)
{
	uint32_t now = (uint32_t) FIQTimerCounter();
	c.Write32(c.Arg(0) + 4, c.Arg(1));
	c.Write32(c.Arg(0), now);
	c.Return(0);
	return true;
}
static uint32_t
TimeOutStart(ARMTrapContext& c)
{
	uint32_t start = 0;
	c.Read32(c.Arg(0), &start);
	return start;
}
// ROM 0x0008ea34 TimedOut__11TDelayTimerFv
static bool
Glue_TDelayTimer_TimedOut(void*, ARMTrapContext& c)
{
	ARMSafePoint();
	uint32_t delay = 0;
	c.Read32(c.Arg(0) + 4, &delay);
	c.Return(delay <= (uint32_t) FIQTimerCounter() - TimeOutStart(c) ? 1 : 0);
	return true;
}
// ROM 0x0008ea58 TimedOut__11TDelayTimerFUl
static bool	Glue_TDelayTimer_TimedOutDelay(void*, ARMTrapContext& c)	{ ARMSafePoint(); c.Return(c.Arg(1) <= (uint32_t) FIQTimerCounter() - TimeOutStart(c) ? 1 : 0); return true; }
// ROM 0x0008ea08 ShortTimerDelayUntil__11TDelayTimerFUl
static bool
Glue_TDelayTimer_ShortTimerDelayUntil(void*, ARMTrapContext& c)
{
	uint32_t start = TimeOutStart(c);
	while (c.Arg(1) > (uint32_t) FIQTimerCounter() - start)
		ARMSafePoint();
	c.Return(0);
	return true;
}
// ROM 0x0008e948 ShortTimerDelay__FUl
static bool	Glue_ShortTimerDelay(void*, ARMTrapContext& c)				{ ShortTimerDelay(c.Arg(0)); ARMSafePoint(); c.Return(0); return true; }


/*------------------------------------------------------------------------------
	O d d s   a n d   e n d s
------------------------------------------------------------------------------*/

// ROM 0x0038ce6c DebugStr
static bool
Glue_DebugStr(void*, ARMTrapContext& c)
{
	char s[256];
	if (!c.ReadCString(c.Arg(0), s, sizeof(s)))
		s[0] = 0;
	fprintf(stderr, "[armcpu] DebugStr: %s\n", s);
	c.Return(0);
	return true;
}

// ROM 0x0031139c ZeroBytes
static bool
Glue_ZeroBytes(void*, ARMTrapContext& c)
{
	for (uint32_t i = 0; i < c.Arg(1); i++)
		c.Write8(c.Arg(0) + i, 0);
	c.Return(0);
	return true;
}

// ROM 0x0031bfe4 GC__Fv
static bool	Glue_GC(void*, ARMTrapContext& c)	{ GC(); c.Return(0); return true; }

// ROM 0x000d9884 Reboot__FlUlUc
// DEVIATION: the host does not restart the machine for ARM code; it says so
static bool
Glue_Reboot(void*, ARMTrapContext& c)
{
	fprintf(stderr, "[armcpu] the ARM code asked for a restart (%ld): not done (NOT YET)\n", (long) (int32_t) c.Arg(0));
	c.Return(0);
	return true;
}

// ROM 0x0011d450 FindEnvironmentId__13MemObjManagerSFUlPUl
static bool
Glue_FindEnvironmentId(void*, ARMTrapContext& c)
{
	TObjectId id = 0;
	NewtonErr err = MemObjManager::FindEnvironmentId(c.Arg(0), &id);
	if (c.Arg(1) != 0)
		c.Write32(c.Arg(1), (uint32_t) id);
	c.Return((uint32_t) err);
	return true;
}

// ROM 0x003503d0 rand
// ROM 0x003504dc srand
// DEVIATION: the C library's generator as ANSI C gives it (seed * 1103515245
// + 12345, fifteen bits of it), not checked against the ROM's
static uint32_t	gRandSeed = 1;
static bool	Glue_rand(void*, ARMTrapContext& c)		{ gRandSeed = gRandSeed * 1103515245u + 12345u; c.Return((gRandSeed >> 16) & 0x7fff); return true; }
static bool	Glue_srand(void*, ARMTrapContext& c)	{ gRandSeed = c.Arg(0); c.Return(0); return true; }


void
InstallARMKernelGlue(void)
{
	gTrace = getenv("NEWTON_TRACE_ARMPROTOCOLS") != nil;
	ARMRegisterGlue("__ct__14TAEventHandlerFv", Glue_TAEventHandler_ct);
	ARMRegisterGlue("__dt__14TAEventHandlerFv", Glue_TAEventHandler_dt);
	ARMRegisterGlue("Init__14TAEventHandlerFUlT1", Glue_TAEventHandler_Init);
	ARMRegisterGlue("InitIdler__14TAEventHandlerFUl9TimeUnitsT1Uc", Glue_TAEventHandler_InitIdler);
	ARMRegisterGlue("StopIdle__14TAEventHandlerFv", Glue_TAEventHandler_StopIdle);
	ARMRegisterGlue("StartIdle__14TAEventHandlerFv", Glue_TAEventHandler_StartIdle);
	ARMRegisterGlue("ResetIdle__14TAEventHandlerFUl9TimeUnits", Glue_TAEventHandler_ResetIdle);
	ARMRegisterGlue("ResetIdle__14TAEventHandlerFv", Glue_TAEventHandler_ResetIdle0);
	ARMRegisterGlue("AETestEvent__14TAEventHandlerFP7TAEvent", Glue_TAEventHandler_AETestEvent);
	ARMRegisterGlue("AEHandlerProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent", Glue_TAEventHandler_Nothing);
	ARMRegisterGlue("AECompletionProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent", Glue_TAEventHandler_Nothing);
	ARMRegisterGlue("IdleProc__14TAEventHandlerFP10TUMsgTokenPUlP7TAEvent", Glue_TAEventHandler_Nothing);
	ARMRegisterGlue("SetReply__14TAEventHandlerFUlP7TAEvent", Glue_TAEventHandler_SetReply);
	ARMRegisterGlue("__ct__7TAEventFv", Glue_TAEvent_ct);
	ARMRegisterGlue("__ct__14TUAsyncMessageFv", Glue_TUAsyncMessage_ct);
	ARMRegisterGlue("__dt__14TUAsyncMessageFv", Glue_TUAsyncMessage_dt);
	ARMRegisterGlue("Init__14TUAsyncMessageFUc", Glue_TUAsyncMessage_Init);
	ARMRegisterGlue("SetCollectorPort__14TUAsyncMessageFUl", Glue_TUAsyncMessage_SetCollectorPort);
	ARMRegisterGlue("SetUserRefCon__14TUSharedMemMsgFUl", Glue_TUSharedMemMsg_SetUserRefCon);
	ARMRegisterGlue("__ct__6TUPortFUl", Glue_TUPort_ct);
	ARMRegisterGlue("SendGoo__6TUPortFUlT1PvN31UcT1P5TTime", Glue_TUPort_SendGoo);
	ARMRegisterGlue("SendRPCGoo__6TUPortFUlT1PUlPvN31UcT4N21P5TTime", Glue_TUPort_SendRPCGoo);
	ARMRegisterGlue("ReplyRPC__10TUMsgTokenFPvUll", Glue_TUMsgToken_ReplyRPC);
	ARMRegisterGlue("GetRefCon__16TUSemaphoreGroupFPPv", Glue_TUSemaphoreGroup_GetRefCon);
	ARMRegisterGlue("Init__18TULockingSemaphoreFv", Glue_TULockingSemaphore_Init);
	ARMRegisterGlue("Acquire__18TULockingSemaphoreF8SemFlags", Glue_TULockingSemaphore_Acquire);
	ARMRegisterGlue("Release__18TULockingSemaphoreFv", Glue_TULockingSemaphore_Release);
	ARMRegisterGlue("__dt__18TULockingSemaphoreFv", Glue_TULockingSemaphore_dt);
	ARMRegisterGlue("GetGlobalTime", Glue_GetGlobalTime);
	ARMRegisterGlue("GetTaskTime", Glue_GetTaskTime);
	ARMRegisterGlue("ConvertTo__5TTimeF9TimeUnits", Glue_TTime_ConvertTo);
	ARMRegisterGlue("__ct__11TDelayTimerFv", Glue_TDelayTimer_ct);
	ARMRegisterGlue("GetHardwareTime__11TDelayTimerFv", Glue_TDelayTimer_GetHardwareTime);
	ARMRegisterGlue("ConvertFromHardwareTime__11TDelayTimerFUl", Glue_TDelayTimer_ConvertFrom);
	ARMRegisterGlue("ShortTimerDelay__FUl", Glue_ShortTimerDelay);
	ARMRegisterGlue("ConvertToHardwareTime__11TDelayTimerFUl", Glue_TDelayTimer_ConvertTo);
	ARMRegisterGlue("ResetTimeOut__11TDelayTimerFUl", Glue_TDelayTimer_ResetTimeOut);
	ARMRegisterGlue("TimedOut__11TDelayTimerFv", Glue_TDelayTimer_TimedOut);
	ARMRegisterGlue("TimedOut__11TDelayTimerFUl", Glue_TDelayTimer_TimedOutDelay);
	ARMRegisterGlue("ShortTimerDelayUntil__11TDelayTimerFUl", Glue_TDelayTimer_ShortTimerDelayUntil);
	ARMRegisterGlue("DebugStr", Glue_DebugStr);
	ARMRegisterGlue("ZeroBytes", Glue_ZeroBytes);
	ARMRegisterGlue("GC__Fv", Glue_GC);
	ARMRegisterGlue("Reboot__FlUlUc", Glue_Reboot);
	ARMRegisterGlue("FindEnvironmentId__13MemObjManagerSFUlPUl", Glue_FindEnvironmentId);
	ARMRegisterGlue("rand", Glue_rand);
	ARMRegisterGlue("srand", Glue_srand);
}
