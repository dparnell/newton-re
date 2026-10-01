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
				  over the ARM instances (ARMProxyFor), +0x1c (where the
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
	return instance != 0 ? instance : ARMMirrorFor(store, 4, 'stor');
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

// an ARM instance as a host proxy (nil for 0; traced when it has none)
static TProtocol*
ProxyOf(uint32_t instance, const char* what)
{
	if (instance == 0)
		return nil;
	TProtocol* p = ARMProxyFor(instance);
	if (p == nil)
		fprintf(stderr, "[armpss] the %s %08x has no host proxy (NOT YET)\n", what, instance);
	return p;
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
	info.Clear();
	info.fPhysId = (TObjectId) BE32(b + 0x00);
	info.fField04 = BE32(b + 0x04);
	info.fPhys[0] = BE32(b + 0x08);
	info.fPhys[1] = BE32(b + 0x0c);
	info.fStore = (TStore*) ProxyOf(BE32(b + 0x10), "store");
	info.fSize = BE32(b + 0x14);
	info.fSocket = BE32(b + 0x18);
	uint32_t base = BE32(b + 0x1c);
	info.fBase = base != 0 ? (char*) ARMHostAddress(base, 1) : nil;
	if (base != 0 && info.fBase == nil)
		fprintf(stderr, "[armpss] slot %u store %u: its base %08x is no host memory (NOT YET)\n", slot, index, base);
	info.fOffset = BE32(b + 0x20);
	info.fField24 = BE32(b + 0x24);
	info.fCardHandler = (TCardHandler*) ProxyOf(BE32(b + 0x28), "card handler");
	if (BE32(b + 0x2c) != 0)
		fprintf(stderr, "[armpss] slot %u store %u: a TFlash %08x (NOT YET)\n", slot, index, BE32(b + 0x2c));
	info.fFlash = nil;
	info.fType = BE32(b + 0x30);
	info.fField34 = BE32(b + 0x34);
	info.fBadCIS = b[0x38];
	info.fInitialized = b[0x39];
	info.fField3A[0] = b[0x3a];
	info.fField3A[1] = b[0x3b];
	info.fEnvironment = BE32(b + 0x3c);
	info.fRawStore = (TStore*) ProxyOf(BE32(b + 0x40), "raw store");
	info.fField44[0] = BE32(b + 0x44);
	info.fField44[1] = BE32(b + 0x48);
	info.fField44[2] = BE32(b + 0x4c);
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


void
InstallARMPSSManager(void)
{
	gTrace = getenv("NEWTON_TRACE_ARMPROTOCOLS") != nil;
	if (gView != 0)
		return;
	gView = ARMMapDevice(0, kViewSize, ViewRead, ViewWrite, nil);
	ARMMapDevice(kROMgPSSManagerAt, 4, GlobalRead, GlobalWrite, nil);
}


uint32_t
ARMPSSManagerView(void)
{
	return gView;
}
