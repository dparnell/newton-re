/*
	File:		armcpu/ARMStore.cpp

	Contains:	A package's TStore implementation on the ARM interpreter: the
				TStore proxy (ARMProtocols.h), its forty-two methods in the
				interface's order (slots 4-45) - and ToObject(TStore*), a
				store's NewtonScript frame, for ARM code.

				The arguments are marshalled as the ROM's calling convention
				has them: numbers as they are; a buffer lent to the ARM code
				for the call (ARMWorld.h's ARMLent, copied back after); an
				out-parameter a scratch word or byte of the ARM heap, read
				back; a TStore the host hands over (SetBuddy, SetStore) the
				ARM instance its proxy stands for; the PSS info Init is given
				a copy in the ROM's layout (0x50 bytes).  What comes back: a
				NewtonErr as a signed word, a Boolean as its low byte, an
				address (Address) as the host memory it is in, a name
				(StoreKind) copied into the proxy.

	docs/armcpu/README.md, "Protocol parts".
*/

#include "ARMStore.h"
#include "ARMPSSManager.h"
#include "ARMProtocols.h"
#include "ARMWorld.h"
#include "Store.h"
#include "PSSInfo.h"
#include "CardHandler.h"
#include "PSSManager.h"
#include "Soups.h"
#include "Objects.h"
#include "OSErrors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool	gTrace = false;

static void
PutBE32(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v;
}

// a host store as the ARM code knows it: the ARM instance its proxy stands
// for, else a handle standing for the store of the host's own (the glue for
// the TStore calls maps it back; ARMPSSManager.cpp hands out the same)
static uint32_t
StoreToARM(TStore* store)
{
	if (store == nil)
		return 0;
	uint32_t instance = ARMInstanceOf(store);
	return instance != 0 ? instance : ARMMirrorFor(store, 16, 'stor');
}

// a PSS info in the ROM's layout (0x50 bytes, armcpu/ARMPSSManager.cpp's)
// in the ARM heap - the caller's to free
static uint32_t
PSSInfoToARM(const SPSSStoreInfo* info)
{
	if (info == nil)
		return 0;
	uint8_t b[0x50];
	memset(b, 0, sizeof(b));
	PutBE32(b + 0x00, (uint32_t) info->fPhysId);
	PutBE32(b + 0x04, (uint32_t) info->fField04);
	PutBE32(b + 0x08, (uint32_t) info->fPhys[0]);
	PutBE32(b + 0x0c, (uint32_t) info->fPhys[1]);
	PutBE32(b + 0x10, info->fStore != nil ? ARMInstanceOf(info->fStore) : 0);
	PutBE32(b + 0x14, (uint32_t) info->fSize);
	PutBE32(b + 0x18, (uint32_t) info->fSocket);
	PutBE32(b + 0x1c, (uint32_t) (uintptr_t) info->fBase);			// (an ARM address: ARMPSSManager.cpp)
	PutBE32(b + 0x20, (uint32_t) info->fOffset);
	PutBE32(b + 0x24, (uint32_t) info->fField24);
	PutBE32(b + 0x28, info->fCardHandler != nil ? ARMInstanceOf(info->fCardHandler) : 0);
	PutBE32(b + 0x30, (uint32_t) info->fType);
	PutBE32(b + 0x34, (uint32_t) info->fField34);
	b[0x38] = info->fBadCIS;
	b[0x39] = info->fInitialized;
	b[0x3a] = info->fField3A[0];
	b[0x3b] = info->fField3A[1];
	PutBE32(b + 0x3c, (uint32_t) info->fEnvironment);
	PutBE32(b + 0x40, info->fRawStore != nil ? ARMInstanceOf(info->fRawStore) : 0);
	PutBE32(b + 0x44, (uint32_t) info->fField44[0]);
	PutBE32(b + 0x48, (uint32_t) info->fField44[1]);
	PutBE32(b + 0x4c, (uint32_t) info->fField44[2]);
	if (info->fFlash != nil)
		fprintf(stderr, "[armstore] a PSS info's flash handed to ARM code (NOT YET)\n");
	uint32_t a = ARMAlloc(sizeof(b), false);
	memcpy(ARMHostAddress(a, sizeof(b)), b, sizeof(b));
	return a;
}

// a scratch word in the ARM heap, read back when it goes
class TOutWord
{
public:
				TOutWord() : fARM(ARMAlloc(4, true)) { }
				~TOutWord()	{ ARMFree(fARM); }
	uint32_t	Value(void)	{ uint32_t v = 0; ARMRead32(fARM, &v); return v; }
	uint8_t		Byte(void)	{ uint8_t v = 0; ARMRead8(fARM, &v); return v; }
	uint32_t	fARM;
};


class TStoreARM : public TStore
{
public:
	uint32_t	Call(int slot, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0, uint32_t f = 0)
				{
					uint32_t args[6] = { a, b, c, d, e, f };
					return ARMCallSlot(this, slot, args, 6);
				}
	NewtonErr	Err(uint32_t r)		{ return (NewtonErr) (int32_t) r; }

	NewtonErr	Init(void* storeAddress, ULong storeSize, ULong arg3, int socketNumber, ULong flags, void* pssInfo)
				{
					// (an ARM store's address is an ARM address: its PSS info's
					// fBase, ARMPSSManager.cpp)
					// (the info: the ARM code's own when it made it - a driver's
					// may be longer than the ROM's - else a copy in the ROM's
					// layout)
					uint32_t info = ARMStoreInfoAddress((const SPSSStoreInfo*) pssInfo);
					bool copied = info == 0;
					if (copied)
						info = PSSInfoToARM((const SPSSStoreInfo*) pssInfo);
					NewtonErr err = Err(Call(4, (uint32_t) (uintptr_t) storeAddress, (uint32_t) storeSize, (uint32_t) arg3, (uint32_t) socketNumber, (uint32_t) flags, info));
					if (copied)
						ARMFree(info);
					return err;
				}
	NewtonErr	NeedsFormat(Boolean* needsFormat)	{ TOutWord o; NewtonErr err = Err(Call(5, needsFormat != nil ? o.fARM : 0)); if (needsFormat != nil) *needsFormat = o.Byte(); return err; }
	NewtonErr	Format()							{ return Err(Call(6)); }
	NewtonErr	GetRootId(PSSId* rootId)			{ TOutWord o; NewtonErr err = Err(Call(7, rootId != nil ? o.fARM : 0)); if (rootId != nil) *rootId = o.Value(); return err; }
	NewtonErr	NewObject(long size, PSSId* id)		{ TOutWord o; NewtonErr err = Err(Call(8, (uint32_t) size, id != nil ? o.fARM : 0)); if (id != nil) *id = o.Value(); return err; }
	NewtonErr	EraseObject(PSSId id)				{ return Err(Call(9, (uint32_t) id)); }
	NewtonErr	DeleteObject(PSSId id)				{ return Err(Call(10, (uint32_t) id)); }
	NewtonErr	SetObjectSize(PSSId id, long size)	{ return Err(Call(11, (uint32_t) id, (uint32_t) size)); }
	NewtonErr	GetObjectSize(PSSId id, long* size)	{ TOutWord o; NewtonErr err = Err(Call(12, (uint32_t) id, o.fARM)); if (size != nil) *size = (long) (int32_t) o.Value(); return err; }
	NewtonErr	Write(PSSId id, long offset, char* buffer, long count)
				{
					ARMLent b(buffer, (uint32_t) count, true);
					return Err(Call(13, (uint32_t) id, (uint32_t) offset, b.fARM, (uint32_t) count));
				}
	NewtonErr	Read(PSSId id, long offset, char* buffer, long count)
				{
					ARMLent b(buffer, (uint32_t) count);
					return Err(Call(14, (uint32_t) id, (uint32_t) offset, b.fARM, (uint32_t) count));
				}
	NewtonErr	GetStoreSizes(long* totalSize, long* usedSize)
				{
					TOutWord t, u;
					NewtonErr err = Err(Call(15, t.fARM, u.fARM));
					if (totalSize != nil) *totalSize = (long) (int32_t) t.Value();
					if (usedSize != nil) *usedSize = (long) (int32_t) u.Value();
					return err;
				}
	NewtonErr	IsReadOnly(Boolean* isReadOnly)		{ TOutWord o; NewtonErr err = Err(Call(16, isReadOnly != nil ? o.fARM : 0)); if (isReadOnly != nil) *isReadOnly = o.Byte(); return err; }
	NewtonErr	LockStore()							{ return Err(Call(17)); }
	NewtonErr	UnlockStore()						{ return Err(Call(18)); }
	NewtonErr	Abort()								{ return Err(Call(19)); }
	NewtonErr	Idle(Boolean* arg1, Boolean* arg2)
				{
					TOutWord a, b;
					NewtonErr err = Err(Call(20, a.fARM, b.fARM));
					if (arg1 != nil) *arg1 = a.Byte();
					if (arg2 != nil) *arg2 = b.Byte();
					return err;
				}
	NewtonErr	NextObject(PSSId id, PSSId* nextId)	{ TOutWord o; NewtonErr err = Err(Call(21, (uint32_t) id, nextId != nil ? o.fARM : 0)); if (nextId != nil) *nextId = o.Value(); return err; }
	NewtonErr	CheckIntegrity(ULong* arg)			{ TOutWord o; NewtonErr err = Err(Call(22, arg != nil ? o.fARM : 0)); if (arg != nil) *arg = o.Value(); return err; }
	NewtonErr	SetBuddy(TStore* buddy)				{ return Err(Call(23, StoreToARM(buddy))); }
	Boolean		OwnsObject(PSSId id)				{ return (Boolean) (Call(24, (uint32_t) id) & 0xff); }
	void*		Address(PSSId id)
				{
					uint32_t a = Call(25, (uint32_t) id);
					return a != 0 ? (void*) ARMHostAddress(a, 1) : nil;
				}
	const char*	StoreKind()
				{
					uint32_t a = Call(26);
					fKind[0] = 0;
					if (a != 0 && !ARMReadCString(a, fKind, sizeof(fKind)))
						fKind[0] = 0;
					return fKind;
				}
	NewtonErr	SetStore(TStore* store, ULong arg)	{ return Err(Call(27, StoreToARM(store), (uint32_t) arg)); }
	Boolean		IsSameStore(void* data, ULong size)
				{
					ARMLent b(data, (uint32_t) size, true);
					return (Boolean) (Call(28, b.fARM, (uint32_t) size) & 0xff);
				}
	Boolean		IsLocked()							{ return (Boolean) (Call(29) & 0xff); }
	NewtonErr	VppOff()							{ return Err(Call(30)); }
	NewtonErr	Sleep()								{ return Err(Call(31)); }
	Boolean		IsROM()								{ return (Boolean) (Call(32) & 0xff); }
	NewtonErr	NewWithinTransaction(long size, PSSId* id)	{ TOutWord o; NewtonErr err = Err(Call(33, (uint32_t) size, id != nil ? o.fARM : 0)); if (id != nil) *id = o.Value(); return err; }
	NewtonErr	StartTransactionAgainst(PSSId id)	{ return Err(Call(34, (uint32_t) id)); }
	NewtonErr	SeparatelyAbort(PSSId id)			{ return Err(Call(35, (uint32_t) id)); }
	NewtonErr	AddToCurrentTransaction(PSSId id)	{ return Err(Call(36, (uint32_t) id)); }
	Boolean		InSeparateTransaction(PSSId id)		{ return (Boolean) (Call(37, (uint32_t) id) & 0xff); }
	NewtonErr	LockReadOnly()						{ return Err(Call(38)); }
	NewtonErr	UnlockReadOnly(Boolean reset)		{ return Err(Call(39, reset)); }
	Boolean		InTransaction()						{ return (Boolean) (Call(40) & 0xff); }
	NewtonErr	NewObject(char* data, long size, PSSId* id)
				{
					ARMLent b(data, (uint32_t) size, true);
					TOutWord o;
					NewtonErr err = Err(Call(41, b.fARM, (uint32_t) size, o.fARM));
					if (id != nil) *id = o.Value();
					return err;
				}
	NewtonErr	ReplaceObject(PSSId id, char* data, long size)
				{
					ARMLent b(data, (uint32_t) size, true);
					return Err(Call(42, (uint32_t) id, b.fARM, (uint32_t) size));
				}
	NewtonErr	CalcXIPObjectSize(long arg1, long arg2, long* size)
				{
					TOutWord o;
					NewtonErr err = Err(Call(43, (uint32_t) arg1, (uint32_t) arg2, o.fARM));
					if (size != nil) *size = (long) (int32_t) o.Value();
					return err;
				}
	NewtonErr	NewXIPObject(long size, PSSId* id)	{ TOutWord o; NewtonErr err = Err(Call(44, (uint32_t) size, id != nil ? o.fARM : 0)); if (id != nil) *id = o.Value(); return err; }
	NewtonErr	GetXIPObjectInfo(PSSId id, ULong* arg1, ULong* arg2, ULong* arg3)
				{
					TOutWord a, b, c;
					NewtonErr err = Err(Call(45, (uint32_t) id, a.fARM, b.fARM, c.fARM));
					if (arg1 != nil) *arg1 = a.Value();
					if (arg2 != nil) *arg2 = b.Value();
					if (arg3 != nil) *arg3 = c.Value();
					return err;
				}

	char		fKind[64];
};

static TProtocol*	MakeStoreARM(void* at)	{ return new (at) TStoreARM; }
static size_t		SizeStoreARM(void)		{ return sizeof(TStoreARM); }


/*------------------------------------------------------------------------------
	T o O b j e c t
------------------------------------------------------------------------------*/

static TStore*	StoreOfARM(uint32_t a);

// ROM 0x00350b74 ToObject__FP6TStore
// The store frame of a store the ARM code holds: an ARM one's proxy, or the
// host's own a handle stands for.
static bool
Glue_ToObject(void*, ARMTrapContext& c)
{
	uint32_t instance = c.Arg(0);
	TStore* store = StoreOfARM(instance);
	if (store == nil)
	{
		fprintf(stderr, "[armstore] ToObject(%08x): not a store the host can stand in for (NOT YET)\n", instance);
		return false;
	}
	RefVar frame(ToObject(store));
	c.Return(c.RefToARM((intptr_t) (Ref) frame));
	if (gTrace)
		fprintf(stderr, "[armstore] ToObject(%08x) -> a store frame\n", instance);
	return true;
}


/*------------------------------------------------------------------------------
	T h e   T S t o r e   c a l l s   A R M   c o d e   m a k e s
	(through the private jump table: ATA Support calls these by slot)
------------------------------------------------------------------------------*/

// a store the ARM code names: its proxy when it is an ARM instance, the host
// store a handle stands for (StoreToARM, armcpu/ARMPSSManager.cpp), nil
static TStore*
StoreOfARM(uint32_t a)
{
	if (a == 0)
		return nil;
	if (TStore* host = (TStore*) ARMHostOf(a, 'stor'))
		return host;
	return (TStore*) ARMProxyFor(a);
}

// ROM 0x00386a08 New__6TStoreSFPc
// A store made by implementation name (the registry's, so a part on the
// ARM interpreter as well as the host's own - ATA Support makes a
// TMuxStore round its TATAStore); the ARM code is handed its ARM instance,
// or a handle standing for a host store, which the TStore calls answer.
static bool
Glue_TStore_New(void*, ARMTrapContext& c)
{
	char name[64];
	if (!c.ReadCString(c.Arg(0), name, sizeof(name)))
		name[0] = 0;
	TStore* store = TStore::New(name);
	uint32_t instance = StoreToARM(store);
	if (gTrace)
		fprintf(stderr, "[armstore] TStore::New(%s) -> %08x\n", name, instance);
	c.Return(instance);
	return true;
}

// ROM 0x00386b64 SetStore__6TStoreFP6TStoreUl
// (the interface's glue: the store asked, whichever it is)
static bool
Glue_TStore_SetStore(void*, ARMTrapContext& c)
{
	TStore* store = StoreOfARM(c.Arg(0));
	if (store == nil)
	{
		fprintf(stderr, "[armstore] SetStore on %08x: no store (NOT YET)\n", c.Arg(0));
		return false;
	}
	c.Return((uint32_t) store->SetStore(StoreOfARM(c.Arg(1)), c.Arg(2)));
	return true;
}


void
InstallARMStores(void)
{
	gTrace = getenv("NEWTON_TRACE_ARMPROTOCOLS") != nil;
	RegisterARMProxyKind("TStore", MakeStoreARM, SizeStoreARM);
	ARMRegisterGlue("ToObject__FP6TStore", Glue_ToObject);
	ARMRegisterGlue("New__6TStoreSFPc", Glue_TStore_New);
	ARMRegisterGlue("SetStore__6TStoreFP6TStoreUl", Glue_TStore_SetStore);
}
