/*
	File:		armcpu/ARMPSSManager.cpp

	Contains:	The PSS manager's slots as a card driver package's ARM code
				reaches them: through the ROM global gPSSManager, at the
				offsets the ROM's TPSSManager has them.

				Kallisys's ATA Support keeps its own stores (TATAStore) and
				puts them in the PSS manager's slot table itself, so that
				the ROM's code that asks it about a card's stores
				(GetStorePSSInfo, GetCardSlotStores, the card-gone and
				unmount paths) finds them: it reads gPSSManager - picking
				the global's address for the ROM version Gestalt names, this
				ROM's being RW data at 0x0c1016bc - and writes the slot
				count (+0x304), a slot's state (slot +0) and the slot's four
				SPSSStoreInfos (slot +0xbc, 0x50 bytes each), a slot being
				0x1fc bytes from +0x308.

				DEVIATION: the host's TPSSManager does not have the ROM's
				layout (its pointers are the host's), and it stays the one
				truth: the global and the object are devices
				(ARMWorld.h's ARMMapDevice) presenting exactly the fields
				the driver reads -

				- gPSSManager (a word): the view's address (0 when there is
				  no PSS manager);
				- +0x304 the slot count, slot +0 the slot's state, and an
				  SPSSStoreInfo's +0x10, its store (the ARM instance a store
				  proxy stands for; a host store as a mirror handle) -

				and turning exactly the writes it makes into calls on the
				host's (TPSSManager::HostDriverSet...):

				- the slot count and a slot's state, as words;
				- an SPSSStoreInfo, staged a word or a byte at a time and
				  handed over when its last byte has been written (the
				  driver copies one whole, or ZeroBytes one) - its stores
				  and card handler (+0x10, +0x28, +0x40) made host proxies
				  over the ARM instances (ARMProxyFor), or the host's own
				  store a handle stands for (the TMuxStore the driver makes
				  round its TATAStore, armcpu/ARMStore.cpp), +0x1c (where the
				  device is mapped) the host address of an ARM one, the
				  rest copied as numbers.

				Any other access is refused, with a trace and NOT YET, so
				the ARM code stops there rather than reading or writing
				something nobody has thought about.

	docs/armcpu/README.md, "The PSS manager's slots".
*/

#include "ARMPSSManager.h"
#include "ARMProtocols.h"
#include "ARMWorld.h"
#include "PSSManager.h"
#include "PSSInfo.h"
#include "CardHandler.h"
#include "Store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// the ROM's layout (this ROM's: the package picks the global by version)
static const uint32_t	kROMgPSSManagerAt	= 0x0c1016bc;		// RW data: gPSSManager
static const uint32_t	kSlotCountAt		= 0x304;
static const uint32_t	kSlotsAt			= 0x308;
static const uint32_t	kSlotSize			= 0x1fc;
static const uint32_t	kSlotStoresAt		= 0xbc;
static const uint32_t	kStoreInfoSize		= 0x50;
static const uint32_t	kSlots				= 4;
static const uint32_t	kViewSize			= kSlotsAt + kSlots * kSlotSize;

static uint32_t	gView = 0;
static uint8_t	gStaged[kSlots][4][kStoreInfoSize];		// infos being written
static bool		gTrace = false;

static uint32_t	BE32(const uint8_t* p)	{ return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }

static bool
Refuse(const char* what, uint32_t offset, uint32_t size)
{
	fprintf(stderr, "[armpss] the ARM code %s %s +%#x (%u bytes) of the PSS manager: not presented (NOT YET)\n",
			what, offset == 0xffffffff ? "gPSSManager" : "", offset == 0xffffffff ? 0 : offset, size);
	return false;
}


/*------------------------------------------------------------------------------
	g P S S M a n a g e r
------------------------------------------------------------------------------*/

static bool
GlobalRead(void*, uint32_t offset, uint32_t size, uint32_t* value)
{
	if (offset != 0 || size != 4)
		return Refuse("read", 0xffffffff, size);
	*value = gPSSManager != nil ? gView : 0;
	return true;
}

static bool
GlobalWrite(void*, uint32_t, uint32_t size, uint32_t)
{
	return Refuse("wrote", 0xffffffff, size);
}


/*------------------------------------------------------------------------------
	T h e   s l o t s
------------------------------------------------------------------------------*/

// where an offset is: the slot count, a slot's state, or a store info of a
// slot (and the offset in it); false for anywhere else
enum EField { kNone, kCount, kState, kStoreInfo };
static EField
FieldAt(uint32_t offset, uint32_t* slot, uint32_t* index, uint32_t* within)
{
	if (offset == kSlotCountAt)
		return kCount;
	if (offset < kSlotsAt || offset >= kViewSize)
		return kNone;
	uint32_t rel = offset - kSlotsAt;
	*slot = rel / kSlotSize;
	uint32_t o = rel % kSlotSize;
	if (o == 0)
		return kState;
	if (o >= kSlotStoresAt && o < kSlotStoresAt + 4 * kStoreInfoSize)
	{
		*index = (o - kSlotStoresAt) / kStoreInfoSize;
		*within = (o - kSlotStoresAt) % kStoreInfoSize;
		return kStoreInfo;
	}
	return kNone;
}

// a host store as the ARM code may compare it: its ARM instance when it
// is a proxy, else a handle that stands for it
static uint32_t
StoreAsARM(TStore* store)
{
	if (store == nil)
		return 0;
	uint32_t instance = ARMInstanceOf(store);
	return instance != 0 ? instance : ARMMirrorFor(store, 16, 'stor');
}

static bool
ViewRead(void*, uint32_t offset, uint32_t size, uint32_t* value)
{
	TPSSManager* manager = gPSSManager;
	uint32_t slot = 0, index = 0, within = 0;
	EField field = FieldAt(offset, &slot, &index, &within);
	if (manager == nil || size != 4)
		return Refuse("read", offset, size);
	switch (field)
	{
	case kCount:
		*value = (uint32_t) manager->fSlotCount;
		return true;
	case kState:
		*value = (uint32_t) manager->fSlots[slot].fState;
		return true;
	case kStoreInfo:
		if (within == 0x10)
		{
			*value = StoreAsARM(manager->fSlots[slot].fStores[index].fStore);
			return true;
		}
		break;
	default:
		break;
	}
	return Refuse("read", offset, size);
}

// an ARM instance as a host proxy, or the host store a handle stands for
// (StoreAsARM) - nil for 0; traced when it is neither
static TProtocol*
ProxyOf(uint32_t instance, const char* what)
{
	if (instance == 0)
		return nil;
	if (TStore* host = (TStore*) ARMHostOf(instance, 'stor'))
		return host;
	TProtocol* p = ARMProxyFor(instance);
	if (p == nil)
		fprintf(stderr, "[armpss] the %s %08x has no host proxy (NOT YET)\n", what, instance);
	return p;
}

// an SPSSStoreInfo in the ROM's layout (0x50 bytes, big-endian) as the
// host's, and back
static void
InfoFromROM(const uint8_t* b, SPSSStoreInfo* info)
{
	info->Clear();
	info->fPhysId = (TObjectId) BE32(b + 0x00);
	info->fField04 = BE32(b + 0x04);
	info->fPhys[0] = BE32(b + 0x08);
	info->fPhys[1] = BE32(b + 0x0c);
	info->fStore = (TStore*) ProxyOf(BE32(b + 0x10), "store");
	info->fSize = BE32(b + 0x14);
	info->fSocket = BE32(b + 0x18);
	// DEVIATION: where the device is mapped is the ARM store's own business
	// - ATA Support's is an ARM address, which its Init is handed back -
	// so it is kept as that address rather than made a host pointer
	// (nothing on the host reads through it but a flash store, and an ARM
	// store is none)
	info->fBase = (char*) (uintptr_t) BE32(b + 0x1c);
	info->fOffset = BE32(b + 0x20);
	info->fField24 = BE32(b + 0x24);
	info->fCardHandler = (TCardHandler*) ProxyOf(BE32(b + 0x28), "card handler");
	if (BE32(b + 0x2c) != 0)
		fprintf(stderr, "[armpss] a store info's TFlash %08x (NOT YET)\n", BE32(b + 0x2c));
	info->fFlash = nil;
	info->fType = BE32(b + 0x30);
	info->fField34 = BE32(b + 0x34);
	info->fBadCIS = b[0x38];
	info->fInitialized = b[0x39];
	info->fField3A[0] = b[0x3a];
	info->fField3A[1] = b[0x3b];
	info->fEnvironment = BE32(b + 0x3c);
	info->fRawStore = (TStore*) ProxyOf(BE32(b + 0x40), "raw store");
	info->fField44[0] = BE32(b + 0x44);
	info->fField44[1] = BE32(b + 0x48);
	info->fField44[2] = BE32(b + 0x4c);
}

static void
PutBE32(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v;
}

static uint32_t		StoreAsARM(TStore* store);

static void
InfoToROM(const SPSSStoreInfo* info, uint8_t* b)
{
	memset(b, 0, kStoreInfoSize);
	PutBE32(b + 0x00, (uint32_t) info->fPhysId);
	PutBE32(b + 0x04, (uint32_t) info->fField04);
	PutBE32(b + 0x08, (uint32_t) info->fPhys[0]);
	PutBE32(b + 0x0c, (uint32_t) info->fPhys[1]);
	PutBE32(b + 0x10, StoreAsARM(info->fStore));
	PutBE32(b + 0x14, (uint32_t) info->fSize);
	PutBE32(b + 0x18, (uint32_t) info->fSocket);
	PutBE32(b + 0x1c, (uint32_t) (uintptr_t) info->fBase);
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
	PutBE32(b + 0x40, StoreAsARM(info->fRawStore));
	PutBE32(b + 0x44, (uint32_t) info->fField44[0]);
	PutBE32(b + 0x48, (uint32_t) info->fField44[1]);
	PutBE32(b + 0x4c, (uint32_t) info->fField44[2]);
}

// a staged store info, as written in the ROM's layout, handed over
static void
CommitStoreInfo(uint32_t slot, uint32_t index)
{
	const uint8_t* b = gStaged[slot][index];
	bool empty = true;
	for (uint32_t i = 0; i < kStoreInfoSize; i++)
		if (b[i] != 0)
			empty = false;
	if (empty)
	{
		gPSSManager->HostDriverSetStoreInfo((int) slot, (int) index, nil);
		if (gTrace)
			fprintf(stderr, "[armpss] slot %u store %u cleared\n", slot, index);
		return;
	}
	SPSSStoreInfo info;
	InfoFromROM(b, &info);
	gPSSManager->HostDriverSetStoreInfo((int) slot, (int) index, &info);
	if (gTrace)
		fprintf(stderr, "[armpss] slot %u store %u: %08x (%p), type %08x, socket %u\n", slot, index,
				BE32(b + 0x10), (void*) info.fStore, (unsigned) info.fType, (unsigned) info.fSocket);
}

static bool
ViewWrite(void*, uint32_t offset, uint32_t size, uint32_t value)
{
	TPSSManager* manager = gPSSManager;
	uint32_t slot = 0, index = 0, within = 0;
	EField field = FieldAt(offset, &slot, &index, &within);
	if (manager == nil)
		return Refuse("wrote", offset, size);
	switch (field)
	{
	case kCount:
		if (size != 4 || value > kSlots)
			break;
		manager->HostDriverSetSlotCount((int) value);
		if (gTrace)
			fprintf(stderr, "[armpss] slot count %u\n", value);
		return true;
	case kState:
		if (size != 4 || value > SPSSSlotInfo::kUnmounted)
			break;
		manager->HostDriverSetSlotState((int) slot, value);
		if (gTrace)
			fprintf(stderr, "[armpss] slot %u state %u\n", slot, value);
		return true;
	case kStoreInfo:
	{
		if (within + size > kStoreInfoSize)
			break;
		uint8_t* b = gStaged[slot][index] + within;
		if (size == 4)
		{
			b[0] = (uint8_t) (value >> 24); b[1] = (uint8_t) (value >> 16); b[2] = (uint8_t) (value >> 8); b[3] = (uint8_t) value;
		}
		else
			b[0] = (uint8_t) value;
		if (within + size == kStoreInfoSize)
			CommitStoreInfo(slot, index);
		return true;
	}
	default:
		break;
	}
	return Refuse("wrote", offset, size);
}


/*------------------------------------------------------------------------------
	T h e   s t o r e   e v e n t s
	('newt 'idle {'stor | 'rstr}: a slot's stores to be mounted or
	unmounted, TNewStoreEvent) - sent to the newt world by a driver that
	keeps its own stores as the PSS manager sends them, and answered with
	itself.  The ARM's is 0xb4 bytes (4 + 4 + 0x20 for each store); its
	pointers are made the host's: a store as above, an info the host's
	slot table's place the view's address stands for, a card handler its
	proxy.  Bytes after the event (ATA Support sends 0x10 more) go as they
	are.
------------------------------------------------------------------------------*/

static const unsigned long	kARMStoreEventSize = 0xb4;

static uint32_t	BE(const uint8_t* p)	{ return BE32(p); }
static void		PutBE(uint8_t* p, uint32_t v)	{ p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }

// An info's address, as the host's: one in the view is the PSS manager's
// own place; one in the ARM code's memory (ATA Support sends its own copy)
// a host shadow of it, read afresh as each event comes and written back as
// the reply goes.
struct ShadowInfo { uint32_t fARM; SPSSStoreInfo fInfo; };
static ShadowInfo	gShadows[16];
static int			gShadowCount = 0;

static SPSSStoreInfo*
InfoOfARM(uint32_t a)
{
	if (a == 0)
		return nil;
	uint32_t slot = 0, index = 0, within = 0;
	if (gView != 0 && a >= gView && FieldAt(a - gView, &slot, &index, &within) == kStoreInfo && within == 0 && gPSSManager != nil)
		return &gPSSManager->fSlots[slot].fStores[index];
	uint8_t b[kStoreInfoSize];
	for (uint32_t i = 0; i < kStoreInfoSize; i++)
		if (!ARMRead8(a + i, &b[i]))
		{
			fprintf(stderr, "[armpss] a store event's info %08x is nowhere (NOT YET)\n", a);
			return nil;
		}
	ShadowInfo* shadow = nil;
	for (int i = 0; i < gShadowCount; i++)
		if (gShadows[i].fARM == a)
			shadow = &gShadows[i];
	if (shadow == nil)
	{
		if (gShadowCount == 16)
		{
			fprintf(stderr, "[armpss] too many store infos of the ARM code's own (NOT YET)\n");
			return nil;
		}
		shadow = &gShadows[gShadowCount++];
		shadow->fARM = a;
	}
	InfoFromROM(b, &shadow->fInfo);
	return &shadow->fInfo;
}
static uint32_t
InfoToARM(const SPSSStoreInfo* info)
{
	if (info == nil)
		return 0;
	for (int i = 0; i < gShadowCount; i++)
		if (info == &gShadows[i].fInfo)
		{
			uint8_t b[kStoreInfoSize];
			InfoToROM(info, b);
			for (uint32_t k = 0; k < kStoreInfoSize; k++)
				ARMWrite8(gShadows[i].fARM + k, b[k]);
			return gShadows[i].fARM;
		}
	if (gPSSManager == nil)
		return 0;
	for (uint32_t slot = 0; slot < kSlots; slot++)
		for (uint32_t i = 0; i < 4; i++)
			if (info == &gPSSManager->fSlots[slot].fStores[i])
				return gView + kSlotsAt + slot * kSlotSize + kSlotStoresAt + i * kStoreInfoSize;
	fprintf(stderr, "[armpss] a store event's info %p is not one of the PSS manager's (NOT YET)\n", (const void*) info);
	return 0;
}

static bool
StoreEventMatches(uint32_t eventClass, uint32_t, uint32_t type)
{
	return eventClass == 'newt' && (type == 'stor' || type == 'rstr');
}
static unsigned long	StoreEventHostSize(unsigned long arm)	{ return sizeof(TNewStoreEvent) + (arm > kARMStoreEventSize ? arm - kARMStoreEventSize : 0); }
static unsigned long	StoreEventARMSize(unsigned long host)	{ return kARMStoreEventSize + (host > sizeof(TNewStoreEvent) ? host - sizeof(TNewStoreEvent) : 0); }

static void
StoreEventWiden(const uint8_t* b, unsigned long armSize, uint8_t* host)
{
	TNewStoreEvent* e = (TNewStoreEvent*) host;
	memset(host, 0, sizeof(TNewStoreEvent));
	e->fAEventClass = BE(b);
	e->fAEventID = BE(b + 4);
	e->fType = BE(b + 8);
	e->fStore = (TStore*) ProxyOf(BE(b + 0x0c), "store");
	e->fInfo = InfoOfARM(BE(b + 0x10));
	for (int i = 0; i < 4; i++)
	{
		const uint8_t* s = b + 0x14 + i * 0x28;
		e->fStores[i].fStore = (TStore*) ProxyOf(BE(s), "store");
		e->fStores[i].fInfo = InfoOfARM(BE(s + 4));
		SCardMessageDevice& d = e->fStores[i].fDevice;
		d.fType = BE(s + 0x08);
		d.fPhys = (TObjectId) BE(s + 0x0c);
		d.fHandler = (TCardHandler*) ProxyOf(BE(s + 0x10), "card handler");
		d.fDriver = nil;
		if (BE(s + 0x14) != 0)
			fprintf(stderr, "[armpss] a store event's device driver %08x (NOT YET)\n", BE(s + 0x14));
		d.fOffset = BE(s + 0x18);
		d.fSize = BE(s + 0x1c);
		d.fField18 = BE(s + 0x20);
		d.fField1C = BE(s + 0x24);
	}
	if (armSize > kARMStoreEventSize)
		memcpy(host + sizeof(TNewStoreEvent), b + kARMStoreEventSize, armSize - kARMStoreEventSize);
	if (gTrace)
		fprintf(stderr, "[armpss] a store event '%c%c%c%c' for %p\n", (char) (e->fType >> 24), (char) (e->fType >> 16),
				(char) (e->fType >> 8), (char) e->fType, (void*) e->fStore);
}

static void
StoreEventNarrow(const uint8_t* host, unsigned long hostSize, uint8_t* b)
{
	const TNewStoreEvent* e = (const TNewStoreEvent*) host;
	memset(b, 0, kARMStoreEventSize);
	PutBE(b, (uint32_t) e->fAEventClass);
	PutBE(b + 4, (uint32_t) e->fAEventID);
	PutBE(b + 8, (uint32_t) e->fType);
	PutBE(b + 0x0c, StoreAsARM(e->fStore));
	PutBE(b + 0x10, InfoToARM(e->fInfo));
	for (int i = 0; i < 4; i++)
	{
		uint8_t* s = b + 0x14 + i * 0x28;
		PutBE(s, StoreAsARM(e->fStores[i].fStore));
		PutBE(s + 4, InfoToARM(e->fStores[i].fInfo));
		const SCardMessageDevice& d = e->fStores[i].fDevice;
		PutBE(s + 0x08, (uint32_t) d.fType);
		PutBE(s + 0x0c, (uint32_t) d.fPhys);
		PutBE(s + 0x10, d.fHandler != nil ? ARMInstanceOf((TCardHandler*) d.fHandler) : 0);
		PutBE(s + 0x14, 0);
		PutBE(s + 0x18, (uint32_t) d.fOffset);
		PutBE(s + 0x1c, (uint32_t) d.fSize);
		PutBE(s + 0x20, (uint32_t) d.fField18);
		PutBE(s + 0x24, (uint32_t) d.fField1C);
	}
	if (hostSize > sizeof(TNewStoreEvent))
		memcpy(b + kARMStoreEventSize, host + sizeof(TNewStoreEvent), hostSize - sizeof(TNewStoreEvent));
}

static const ARMEventTranslator	gStoreEvents =
{
	StoreEventMatches, StoreEventHostSize, StoreEventARMSize, StoreEventWiden, StoreEventNarrow
};


void
InstallARMPSSManager(void)
{
	gTrace = getenv("NEWTON_TRACE_ARMPROTOCOLS") != nil;
	if (gView != 0)
		return;
	gView = ARMMapDevice(0, kViewSize, ViewRead, ViewWrite, nil);
	ARMRegisterEventTranslator(&gStoreEvents);
	ARMMapDevice(kROMgPSSManagerAt, 4, GlobalRead, GlobalWrite, nil);
}


// an info the ARM code made itself, as its ARM address (its shadow written
// back first), 0 for any other
uint32_t
ARMStoreInfoAddress(const SPSSStoreInfo* info)
{
	for (int i = 0; i < gShadowCount; i++)
		if (info == &gShadows[i].fInfo)
			return InfoToARM(info);
	return 0;
}


uint32_t
ARMPSSManagerView(void)
{
	return gView;
}
