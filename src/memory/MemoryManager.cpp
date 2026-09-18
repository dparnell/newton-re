/*
	File:		memory/MemoryManager.cpp

	Contains:	The memory manager's API (NewtonMemory.h): pointers (NewPtr,
				DisposPtr, ReallocPtr, ...), handles (NewHandle, HLock, ...),
				the VM heaps (NewVMHeap, NewSegregatedVMHeap, the persistent
				heaps), the task's current heap (GetHeap/SetHeap), MemError,
				heap walking (HeapSeed/NextHeapBlock/CountHeapBlocks) and the
				memory manager's debugging breaks.  All over the Skia heap
				(SkiaHeap.h) - or the safe heap (SafeHeap.h), the kernel's own
				before the VM heaps exist.

	A call finds the block's heap (PtrToHeap/HandleToHeap), makes it the
	current heap for the duration, takes the heap's semaphore if it has one,
	and leaves its result in the task globals for MemError.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SkiaHeap.h"
#include "SafeHeap.h"
#include "KernelGlobals.h"
#include "Boot.h"
#include "os600/TaskGlobals.h"
#include "MemObjManager.h"
#include "UserSemaphore.h"
#include "VirtualMemory.h"
#include "NewtonExceptions.h"
#include "NewtMemory.h"
#include "OSErrors.h"

extern "C" void ClearMemory(void* p, ULong size);

#include <stdio.h>

// (gPtrsUsed and gHandlesUsed, the running totals the scheduler charges to
// tasks, are the kernel's: KernelGlobals.h)

// the debugging breaks (SetMemMgrBreak)
Boolean	sMMBreak_Enabled = false;	// 0x0c10174c
ULong	sMMBreak_LT = 0;			// 0x0c101750  break on a request smaller than this
ULong	sMMBreak_GE = 0;			// 0x0c101754  ... or at least this
ULong	sMMBreak_OnCCHash = 0;		// 0x0c101758  ... or from a call chain with this hash
ULong	sMMBreak_OnCount = 0;		// 0x0c10175c  ... or the nth call
void*	sMMBreak_AddressIn = nil;	// 0x0c101760  ... or on this address coming in
void*	sMMBreak_AddressOut = nil;	// 0x0c101764  ... or going out
ULong	gMemMgrCallCounter = 0;		// 0x0c101768
Boolean	sMMTagBlocksWithCCHash = false;	// 0x0c10176c  name each new block with its call chain's hash

const NewtonErr kMemErr_NoMemory = -108;		// what NewPtr & co. leave in MemError


// The task globals hold the current heap and the memory manager's result.
// (Before the first task runs there are no globals: the result is dropped,
// which the ROM never has to consider - its kernel heap is a safe heap.)
static inline TaskGlobals*
Globals()
{
	return gCurrentGlobals != nil ? (TaskGlobals*) ((char*) gCurrentGlobals - kTaskGlobalsSize) : nil;
}

static inline void
SetMemError(NewtonErr err)
{
	TaskGlobals* globals = Globals();
	if (globals != nil)
		globals->fMemError = err;
}


// ROM 0x00142758 MemError
extern "C" NewtonErr
MemError(void)
{
	TaskGlobals* globals = Globals();
	return globals != nil ? globals->fMemError : noErr;
}


// ROM 0x00142b20 GetHeap
extern "C" Heap
GetHeap(void)
{
	return GetCurrentHeap();
}


// ROM 0x00142b24 SetHeap
extern "C" void
SetHeap(Heap heap)
{
	SetCurrentHeap(heap);
}


/* -------------------------------------------------------------------------------
	Debugging breaks
------------------------------------------------------------------------------- */

// ROM 0x00142104 ReportMemMgrTrap__Fl
// (the ROM formats the message for the debugger; there is none here)
static void
ReportMemMgrTrap(long what)
{
	char message[200];
	sprintf(message, "MemMgr trigger '%c%c%c%c'", (int) (what >> 24) & 0xff, (int) (what >> 16) & 0xff, (int) (what >> 8) & 0xff, (int) what & 0xff);
}


// ROM 0x001414d8 ReportSmashedHeap__FPclPv
void
ReportSmashedHeap(char* where, long err, void* address)
{
	char message[200];
	sprintf(message, "Smashed heap (err %ld) at $%08lx %s memmgr", err, (unsigned long) (uintptr_t) address, where);
}


// ROM 0x003893ec HashCallChain
// NOT YET RECONSTRUCTED: a hash of the return addresses up the ARM stack
// frames (the debugger's block tagging); nothing on the host.
static ULong
HashCallChain(void)
{
	return 0;
}


// ROM 0x00141534 SetMemMgrBreak
// (empty in this ROM: the breaks are set from the debugger)
extern "C" void
SetMemMgrBreak(long /*what*/, ...)
{
}


// The checks every call makes when the breaks are on (inlined in the ROM).
static inline void
BreakOnCall(void)
{
	if (sMMBreak_Enabled)
	{
		gMemMgrCallCounter++;
		if (gMemMgrCallCounter == sMMBreak_OnCount)
			ReportMemMgrTrap('#');
	}
}

static inline void
BreakOnSize(Size size)
{
	if (sMMBreak_Enabled)
	{
		if (sMMBreak_LT != 0 && (ULong) size < sMMBreak_LT)
			ReportMemMgrTrap('<');
		else if ((ULong) size >= sMMBreak_GE)
			ReportMemMgrTrap(0x3e3d);		// '>='
	}
}

static inline void
BreakOnAddressIn(const void* address)
{
	if (sMMBreak_Enabled && sMMBreak_AddressIn != nil && address == sMMBreak_AddressIn)
		ReportMemMgrTrap('&');
}

static inline void
BreakOnAddressOut(const void* address)
{
	if (sMMBreak_AddressOut != nil && address == sMMBreak_AddressOut)
		ReportMemMgrTrap('&');
}


/* -------------------------------------------------------------------------------
	The heap semaphore
------------------------------------------------------------------------------- */

// ROM 0x001429bc GetHeapSemaphore__FPv
TULockingSemaphore*
GetHeapSemaphore(void* heap)
{
	if (heap == nil)
		heap = GetHeap();
	if (IsSafeHeap(heap))
		return ((SSafeHeapPage*) heap)->fSemaphore;
	return GetSkiaHeapSemaphore(heap);
}


// (inlined in every call: the current heap's semaphore is taken if it has one)
static inline TULockingSemaphore*
AcquireHeapSemaphore(void)
{
	TULockingSemaphore* semaphore = GetHeapSemaphore(nil);
	if (semaphore != nil)
		semaphore->Acquire(kWaitOnBlock);
	return semaphore;
}

static inline void
ReleaseHeapSemaphore(TULockingSemaphore* semaphore)
{
	if (semaphore != nil)
		semaphore->Release();
}


// ROM 0x001429f8 AddSemaphoreToHeap
extern "C" NewtonErr
AddSemaphoreToHeap(Heap heap)
{
	TULockingSemaphore* semaphore = new TULockingSemaphore;
	if (semaphore == nil)
		return MemError();
	NewtonErr err = semaphore->Init();
	if (err == noErr)
	{
		if (IsSafeHeap(heap))
			((SSafeHeapPage*) heap)->fSemaphore = semaphore;
		else
			SetSkiaHeapSemaphore(heap, semaphore);
	}
	else
		delete semaphore;
	return err;
}


// ROM 0x00142a8c ClobberHeapSemaphore
extern "C" NewtonErr
ClobberHeapSemaphore(Heap heap)
{
	if (heap == nil)
		heap = GetHeap();
	TULockingSemaphore* semaphore = GetSkiaHeapSemaphore(heap);
	if (semaphore != nil)
	{
		SetSkiaHeapSemaphore(heap, nil);
		delete semaphore;
	}
	return noErr;
}


/* -------------------------------------------------------------------------------
	Switching to a block's heap
------------------------------------------------------------------------------- */

// A call on a block switches to the block's heap; this remembers the
// caller's if it differs (nil: no switch), and false says the block has no
// heap at all.
static inline Boolean
SwitchToHeap(Heap heap, Heap* outSaved)
{
	Heap current = GetHeap();
	if (current == heap)
		*outSaved = nil;
	else
	{
		if (heap == nil)
			return false;
		SetHeap(heap);
		*outSaved = current;
	}
	return true;
}

static inline void
SwitchBack(Heap saved)
{
	if (saved != nil)
		SetHeap(saved);
}


// ROM 0x00310fc0 PtrToHeap
extern "C" Heap
PtrToHeap(Ptr p)
{
	return p != nil ? SkiaBlock::Of(p)->fParent : nil;
}


// ROM 0x00310fd0 HandleToHeap
extern "C" Heap
HandleToHeap(Handle h)
{
	return h != nil ? ((SkiaMasterPointer*) h)->fHeap : nil;
}


/* -------------------------------------------------------------------------------
	Pointers
------------------------------------------------------------------------------- */

// ROM 0x00142b28 NewPtr
// A block from the task's heap: a direct block, busy from birth (it never
// moves).  In a safe heap - the kernel heap, and so everything before the OS
// runs - the safe heap allocates.  (The ROM tests gOSIsRunning as well as
// the heap's kind, which comes to the same there; on the host the kernel
// heap is not a safe heap, so only the kind is tested.)
extern "C" Ptr
NewPtr(Size size)
{
	BreakOnCall();
	if (sMMBreak_Enabled)
	{
		if (sMMBreak_OnCCHash != 0 && (HashCallChain() & 0x7fffffff) == (sMMBreak_OnCCHash & 0x7fffffff))
			ReportMemMgrTrap('hash');
		BreakOnSize(size);
	}
	Ptr p;
	if (!IsSafeHeap(GetHeap()))
	{
		TULockingSemaphore* semaphore = AcquireHeapSemaphore();
		p = (Ptr) NewDirectBlock(size);
		if (p == nil)
			SetMemError(kMemErr_NoMemory);
		else
		{
			gPtrsUsed += size;
			IncrementBlockBusy(p);
			SetMemError(noErr);
		}
		ReleaseHeapSemaphore(semaphore);
		if (sMMTagBlocksWithCCHash)
			SetPtrName(p, HashCallChain() & 0x7fffffff);
	}
	else
	{
		TULockingSemaphore* semaphore = AcquireHeapSemaphore();
		p = (Ptr) SafeHeapAlloc(size, (SSafeHeapPage*) gKernelHeap);
		if (p != nil)
			gPtrsUsed += size;
		ReleaseHeapSemaphore(semaphore);
	}
	BreakOnAddressOut(p);
	return p;
}


// ROM 0x00143090 NewNamedPtr
extern "C" Ptr
NewNamedPtr(Size size, ULong name)
{
	Ptr p = NewPtr(size);
	if (p != nil)
		SetPtrName(p, name);
	return p;
}


// ROM 0x001431e0 NewPtrClear
extern "C" Ptr
NewPtrClear(Size byteCount)
{
	Ptr p = NewPtr(byteCount);
	if (p != nil)
		ClearMemory(p, byteCount);
	return p;
}


// ROM 0x0014320c DisposPtr  (0x002f3c24 operator delete is the same code)
// Back to its heap; a heap with much free space gives pages back.
extern "C" void
DisposPtr(Ptr p)
{
	BreakOnCall();
	BreakOnAddressIn(p);
	if (p == nil)
		return;
	if (IsSafeHeap(GetHeap()))
	{
		TULockingSemaphore* semaphore = AcquireHeapSemaphore();
		Size size = SafeHeapBlockSize(p);
		SafeHeapFree(p);
		gPtrsUsed -= size;
		ReleaseHeapSemaphore(semaphore);
		return;
	}
	Heap saved;
	if (!SwitchToHeap(PtrToHeap(p), &saved))
		return;
	TULockingSemaphore* semaphore = AcquireHeapSemaphore();
	Size size = GetDirectBlockSize(p);
	DecrementBlockBusy(p);
	DisposeDirectBlock(p);
	gPtrsUsed -= size;
	SetMemError(noErr);
	ShrinkHeapLeaving(GetHeap(), 0);
	ReleaseHeapSemaphore(semaphore);
	SwitchBack(saved);
}


// ROM 0x0014342c GetPtrSize
extern "C" Size
GetPtrSize(Ptr p)
{
	BreakOnCall();
	BreakOnAddressIn(p);
	if (p == nil)
		return 0;
	return GetDirectBlockSize(p);
}


// ROM 0x00143498 ReallocPtr
// Resizes in place if it can, else moves the block; nil (and the old block
// intact) if there is no room.
extern "C" Ptr
ReallocPtr(Ptr p, Size size)
{
	BreakOnCall();
	BreakOnAddressIn(p);
	BreakOnSize(size);
	Ptr result;
	if (p == nil)
		result = NewPtr(size);
	else if (IsSafeHeap(GetHeap()))
	{
		TULockingSemaphore* semaphore = AcquireHeapSemaphore();
		result = (Ptr) SafeHeapRealloc(p, size);
		ReleaseHeapSemaphore(semaphore);
	}
	else
	{
		Heap saved;
		if (!SwitchToHeap(PtrToHeap(p), &saved))
			result = nil;
		else
		{
			TULockingSemaphore* semaphore = AcquireHeapSemaphore();
			Size oldSize = GetDirectBlockSize(p);
			DecrementBlockBusy(p);
			result = (Ptr) SetDirectBlockSize(p, size);
			SetMemError(result == nil ? kMemErr_NoMemory : noErr);
			if (result == nil)
				IncrementBlockBusy(p);
			else
			{
				IncrementBlockBusy(result);
				gPtrsUsed += size - oldSize;
			}
			ReleaseHeapSemaphore(semaphore);
			SwitchBack(saved);
		}
	}
	BreakOnAddressOut(result);
	return result;
}


// ROM 0x00143044 LockPtr
// The stack manager keeps the block's pages in.
extern "C" NewtonErr
LockPtr(Ptr p)
{
	Size size = GetPtrSize(p);
	return LockHeapRange((VAddr) p, (VAddr) (p + size - 1), false);
}


// ROM 0x0014306c UnlockPtr
extern "C" NewtonErr
UnlockPtr(Ptr p)
{
	GetPtrSize(p);
	return UnlockHeapRange((VAddr) p, (VAddr) p);
}


// ROM 0x00142e2c NewWiredPtr
extern "C" Ptr
NewWiredPtr(Size /*size*/)
{
	// NOT YET RECONSTRUCTED: a block in the heap's wired sub-heap - an
	// SWiredHeapDescr over a 0x8000-byte area from the stack manager, made on
	// first use, whose pages are wired; a safe heap over it (SWiredHeapPage,
	// 0x001c8000-)
	return nil;
}


// ROM 0x00142f80 DisposeWiredPtr
extern "C" void
DisposeWiredPtr(Ptr /*p*/)
{
	// NOT YET RECONSTRUCTED: see NewWiredPtr
}


// ROM 0x001430bc TotalSystemFree
extern "C" Size
TotalSystemFree(void)
{
	// NOT YET RECONSTRUCTED: SystemFreePageCount() << 12 (the page manager)
	return 0;
}


// ROM 0x001430d4 SystemRAMSize
extern "C" Size
SystemRAMSize(void)
{
	// NOT YET RECONSTRUCTED: TRAMTable::GetRAMSize() - InternalStoreInfo(1)
	return 0;
}


// ROM 0x00311380 GetPtrOwner
extern "C" TObjectId
GetPtrOwner(Ptr p)
{
	if (p != nil && (SkiaBlock::Of(p)->fOwner & 0x80000000) == 0)
		return (TObjectId) SkiaBlock::Of(p)->fOwner;
	return 0;
}


// ROM 0x003113a4 SetPtrOwner
extern "C" void
SetPtrOwner(Ptr p, TObjectId owner)
{
	if (p != nil)
		SkiaBlock::Of(p)->fOwner = owner;
}


// ROM 0x003113dc GetPtrType
extern "C" HeapBlockType
GetPtrType(Ptr p)
{
	return p != nil ? GetBlockType(p) : 0;
}


// ROM 0x003113ec SetPtrType
extern "C" void
SetPtrType(Ptr p, HeapBlockType type)
{
	if (p != nil)
		SetBlockType(p, type);
}


// ROM 0x00311440 GetPtrName
// A name shares the owner word (its top bit set says so).
extern "C" ULong
GetPtrName(Ptr p)
{
	if (p != nil && (SkiaBlock::Of(p)->fOwner & 0x80000000) != 0)
		return SkiaBlock::Of(p)->fOwner & 0x7fffffff;
	return 0;
}


// ROM 0x0031145c SetPtrName
extern "C" void
SetPtrName(Ptr p, ULong name)
{
	if (p != nil)
		SkiaBlock::Of(p)->fOwner = name | 0x80000000;
}


/* -------------------------------------------------------------------------------
	Handles
------------------------------------------------------------------------------- */

// ROM 0x00141538 NewHandle
// An indirect block in the task's heap's relocatable heap; the handle is
// its master pointer.
extern "C" Handle
NewHandle(Size size)
{
	BreakOnCall();
	BreakOnSize(size);
	Heap saved = nil;
	if (GetRelocHeap(GetHeap()) != GetHeap())
	{
		saved = GetHeap();
		SetHeap(GetRelocHeap(GetHeap()));
	}
	TULockingSemaphore* semaphore = AcquireHeapSemaphore();
	Handle h = (Handle) NewIndirectBlock(size);
	if (h != nil)
		gHandlesUsed += size;
	ReleaseHeapSemaphore(semaphore);
	SetMemError(h == nil ? kMemErr_NoMemory : noErr);
	if (sMMTagBlocksWithCCHash)
		SetHandleName(h, HashCallChain() & 0x7fffffff);
	BreakOnAddressOut(h);
	if (saved != nil)
		SetHeap(saved);
	return h;
}


// ROM 0x0014171c NewNamedHandle
extern "C" Handle
NewNamedHandle(Size size, ULong name)
{
	Handle h = NewHandle(size);
	if (h != nil)
		SetHandleName(h, name);
	return h;
}


// ROM 0x00141748 NewHandleClear
extern "C" Handle
NewHandleClear(Size byteCount)
{
	Handle h = NewHandle(byteCount);
	if (h != nil)
	{
		void* p = HLock(h);
		ClearMemory(p, byteCount);
		HUnlock(h);
	}
	return h;
}


// ROM 0x00141784 DisposHandle
extern "C" void
DisposHandle(Handle h)
{
	BreakOnCall();
	BreakOnAddressIn(h);
	if (h == nil)
		return;
	if (IsFakeIndirectBlock((SkiaMasterPointer*) h))
	{
		TULockingSemaphore* semaphore = AcquireHeapSemaphore();
		SetMemError(noErr);
		DisposeIndirectBlock((SkiaMasterPointer*) h);
		ReleaseHeapSemaphore(semaphore);
		return;
	}
	Heap saved;
	if (!SwitchToHeap(HandleToHeap(h), &saved))
		return;
	TULockingSemaphore* semaphore = AcquireHeapSemaphore();
	xSetBlockBusy(*h, 0);
	SetMemError(noErr);
	Size size = GetIndirectBlockSize((SkiaMasterPointer*) h);
	DisposeIndirectBlock((SkiaMasterPointer*) h);
	gHandlesUsed -= size;
	if (TotalFreeInHeap(nil) >= 0x200)
		ShrinkHeapLeaving(GetHeap(), 0);
	ReleaseHeapSemaphore(semaphore);
	SwitchBack(saved);
}


// ROM 0x00141a0c GetHandleSize
extern "C" Size
GetHandleSize(Handle h)
{
	BreakOnCall();
	BreakOnAddressIn(h);
	if (IsFakeIndirectBlock((SkiaMasterPointer*) h))
		return GetFakeIndirectBlockSize((SkiaMasterPointer*) h);
	if (h == nil)
		return 0;
	TULockingSemaphore* semaphore = AcquireHeapSemaphore();
	SetMemError(noErr);
	Size size = GetIndirectBlockSize((SkiaMasterPointer*) h);
	ReleaseHeapSemaphore(semaphore);
	return size;
}


// ROM 0x00141b78 SetHandleSize
extern "C" NewtonErr
SetHandleSize(Handle h, Size size)
{
	BreakOnCall();
	BreakOnAddressIn(h);
	BreakOnSize(size);
	NewtonErr err = -1;
	if (!IsFakeIndirectBlock((SkiaMasterPointer*) h) && h != nil)
	{
		Heap saved;
		if (SwitchToHeap(HandleToHeap(h), &saved))
		{
			TULockingSemaphore* semaphore = AcquireHeapSemaphore();
			Size oldSize = GetIndirectBlockSize((SkiaMasterPointer*) h);
			err = SetIndirectBlockSize((SkiaMasterPointer*) h, size) != nil ? noErr : kMemErr_NoMemory;
			if (err == noErr)
				gHandlesUsed += size - oldSize;
			ReleaseHeapSemaphore(semaphore);
			SwitchBack(saved);
		}
	}
	SetMemError(err);
	return err;
}


// ROM 0x00141d88 HLock
// The block stays put while locked; its address is the result.
extern "C" void*
HLock(Handle h)
{
	if (!IsFakeIndirectBlock((SkiaMasterPointer*) h))
	{
		if (h == nil)
		{
			SetMemError(-1);
			return nil;
		}
		TULockingSemaphore* semaphore = AcquireHeapSemaphore();
		IncrementBlockBusy(*h);
		SetMemError(noErr);
		ReleaseHeapSemaphore(semaphore);
	}
	return *h;
}


// ROM 0x00141ea4 HUnlock
extern "C" void
HUnlock(Handle h)
{
	if (h == nil || IsFakeIndirectBlock((SkiaMasterPointer*) h))
		return;
	Heap saved;
	if (!SwitchToHeap(HandleToHeap(h), &saved))
		return;
	TULockingSemaphore* semaphore = AcquireHeapSemaphore();
	DecrementBlockBusy(*h);
	SetMemError(noErr);
	ReleaseHeapSemaphore(semaphore);
	SwitchBack(saved);
}


// ROM 0x00141fe4 HSetState
extern "C" void
HSetState(Handle h, char savedCount)
{
	if (IsFakeIndirectBlock((SkiaMasterPointer*) h) || h == nil)
		return;
	TULockingSemaphore* semaphore = AcquireHeapSemaphore();
	xSetBlockBusy(*h, (UByte) savedCount);
	ReleaseHeapSemaphore(semaphore);
	SetMemError(noErr);
}


// ROM 0x00142070 HGetState
extern "C" char
HGetState(Handle h)
{
	if (IsFakeIndirectBlock((SkiaMasterPointer*) h) || h == nil)
		return 0;
	TULockingSemaphore* semaphore = AcquireHeapSemaphore();
	SetMemError(noErr);
	char busy = (char) GetBlockBusy(*h);
	ReleaseHeapSemaphore(semaphore);
	return busy;
}


// ROM 0x00142174 NewFakeHandle
// A handle on memory that is not a heap block.
extern "C" Handle
NewFakeHandle(void* address, Size size)
{
	Heap saved = nil;
	if (GetRelocHeap(GetHeap()) != GetHeap())
	{
		saved = GetHeap();
		SetHeap(GetRelocHeap(GetHeap()));
	}
	TULockingSemaphore* semaphore = AcquireHeapSemaphore();
	Handle h = (Handle) NewFakeIndirectBlock(address, size);
	ReleaseHeapSemaphore(semaphore);
	if (saved != nil)
		SetHeap(saved);
	return h;
}


// ROM 0x00142218 IsFakeHandle
extern "C" Boolean
IsFakeHandle(Handle h)
{
	return IsFakeIndirectBlock((SkiaMasterPointer*) h);
}


// ROM 0x00142230 HandToHand
// Replaces the handle with a copy.
extern "C" NewtonErr
HandToHand(Handle* hPtr)
{
	if (hPtr == nil || *hPtr == nil)
		return -1;
	Size size = GetHandleSize(*hPtr);
	Handle h = NewHandle(size);
	if (h == nil)
		return 1;
	HLock(*hPtr);
	HLock(h);
	BlockMove(**hPtr, *h, size);
	HUnlock(*hPtr);
	HUnlock(h);
	*hPtr = h;
	return noErr;
}


// ROM 0x001422a8 CopyHandle
extern "C" Handle
CopyHandle(Handle h)
{
	if (h == nil || *h == nil)
		return nil;
	Size size = GetHandleSize(h);
	Handle copy = NewHandle(size);
	if (copy != nil)
	{
		void* src = HLock(h);
		void* dst = HLock(copy);
		BlockMove(src, dst, size);
		HUnlock(h);
		HUnlock(copy);
	}
	return copy;
}


// ROM 0x0014231c MoveHHi
extern "C" void
MoveHHi(Handle /*h*/)
{
	SetMemError(noErr);
}


// ROM 0x003113b8 GetHandleOwner
extern "C" TObjectId
GetHandleOwner(Handle h)
{
	return h != nil ? GetPtrOwner((Ptr) *h) : 0;
}


// ROM 0x003113cc SetHandleOwner
extern "C" void
SetHandleOwner(Handle h, TObjectId owner)
{
	if (h != nil)
		SetPtrOwner((Ptr) *h, owner);
}


// ROM 0x00311400 GetHandleType
extern "C" HeapBlockType
GetHandleType(Handle h)
{
	return h != nil ? GetPtrType((Ptr) *h) : 0;
}


// ROM 0x0031142c SetHandleType
extern "C" void
SetHandleType(Handle h, HeapBlockType type)
{
	if (h != nil)
		SetPtrType((Ptr) *h, type);
}


// ROM 0x00311474 GetHandleName
extern "C" ULong
GetHandleName(Handle h)
{
	return h != nil ? GetPtrName((Ptr) *h) : 0;
}


// ROM 0x003114a0 SetHandleName
extern "C" void
SetHandleName(Handle h, ULong name)
{
	if (h != nil)
		SetPtrName((Ptr) *h, name);
}


/* -------------------------------------------------------------------------------
	Heaps
------------------------------------------------------------------------------- */

// ROM 0x00142818 NewHeapAt
// A heap laid out in memory the caller has (1 KB to start with).
extern "C" NewtonErr
NewHeapAt(VAddr address, Size size, Heap* pResult)
{
	Heap heap = NewHeap((void*) address, size, 0x400);
	if (heap == nil)
		return kMemErr_NoMemory;
	*pResult = heap;
	return noErr;
}


// ROM 0x00142338 NewVMHeap
// A heap in a new area from the stack manager: maxSize of address space,
// of which a page is made real and laid out; the heap grows into the rest
// as it needs, and has a semaphore.  A persistent heap starts on a page
// boundary, a page at a time.
extern "C" NewtonErr
NewVMHeap(TObjectId defaultDomain, Size maxSize, Heap* pResult, ULong options)
{
	if (defaultDomain == 0)
		defaultDomain = Globals()->fHeapDomainId;
	if (options & kPersistentHeap)
		maxSize += 0x1000;
	VAddr start, end;
	NewtonErr err = NewHeapArea(defaultDomain, 0, maxSize, (options & ~kPersistentHeap) | 4, &start, &end);
	if (err != noErr)
		return err;
	Size initial = 0x400;
	if (options & kPersistentHeap)
	{
		start = (start + 0xfff) & ~(VAddr) 0xfff;
		initial = 0x1000;
	}
	Heap saved = GetHeap();
	LockHeapRange(start, start + initial, false);
	UnlockHeapRange(start, start + initial);
	Heap heap = NewHeap((void*) start, end - start, initial);
	SetHeap(saved);
	if (heap == nil)
		return kMemErr_NoMemory;
	AddSemaphoreToHeap(heap);
	SetHeapIsVMBacked(heap);
	SetRemoveRoutine((VAddr) heap, (ReleaseProcPtr) HeapReleaseRequestHandler, heap);
	*pResult = heap;
	return noErr;
}


// ROM 0x00142448 NewPersistentVMHeap
// A VM heap recorded in the memory object database, to be found again
// after a warm reboot.
extern "C" NewtonErr
NewPersistentVMHeap(TObjectId domainId, Size maxSize, Heap* pResult, ULong options, ULong name)
{
	if (domainId == 0)
		domainId = Globals()->fHeapDomainId;
	// the domain's index in the domain table
	MemObjEntry entry;
	long error;
	ULong index = 0;
	if (!MemObjManager::FindEntryByIndex(kMemObjDomain, 0, &entry, &error))
		return kStackError_BadDomain;
	ULong domainIndex;
	for (;;)
	{
		if ((TObjectId) entry.fValue == domainId)
		{
			domainIndex = index;
			break;
		}
		index++;
		if (!MemObjManager::FindEntryByIndex(kMemObjDomain, index, &entry, &error))
			return kStackError_BadDomain;
	}
	NewtonErr err = NewVMHeap(domainId, maxSize, pResult, options | kPersistentHeap);
	if (err != noErr)
		return err;
	VAddr start, end;
	err = GetHeapAreaInfo((VAddr) *pResult, &start, &end);
	if (err != noErr)
		return err;
	PersistentDBEntry dbEntry;
	dbEntry.Init(name, true, domainIndex);
	dbEntry.fHeap = *pResult;
	dbEntry.fStart = start;
	dbEntry.fSize = end - start;
	dbEntry.fFlags &= ~kPersistent_Unknown80;
	err = MemObjManager::RegisterPersistentNewEntry(name, &dbEntry);
	if (err != noErr)
		DestroyVMHeap(*pResult);
	return err;
}


// ROM 0x00142598 DeletePersistentVMHeap
extern "C" NewtonErr
DeletePersistentVMHeap(ULong name)
{
	PersistentDBEntry entry;
	NewtonErr err = MemObjManager::FindEntryByName(kMemObjPersistent, name, &entry);
	if (err == noErr && (err = MemObjManager::DeregisterPersistentEntry(name)) == noErr)
		DestroyVMHeap(entry.fHeap);
	return err;
}


// ROM 0x001425e8 NewSegregatedVMHeap
// Three VM heaps in one: pointers in the first (the fixed heap), master
// pointers in the second (a quarter of the size), handles in the third;
// all three share the first's semaphore.
extern "C" NewtonErr
NewSegregatedVMHeap(TObjectId defaultDomain, Size ptrSize, Size handleSize, Heap* pResult, ULong options)
{
	Heap fixed = nil, masters = nil, reloc = nil;
	NewtonErr err = NewVMHeap(defaultDomain, ptrSize, &fixed, options);
	if (err == noErr)
	{
		if (handleSize < 1)
		{
			*pResult = fixed;
			return noErr;
		}
		err = NewVMHeap(defaultDomain, ptrSize / 4, &masters, options);
		if (err == noErr)
		{
			ClobberHeapSemaphore(masters);
			SetSkiaHeapSemaphore(masters, GetSkiaHeapSemaphore(fixed));
			SetFixedHeap(masters, fixed);
			SetMPHeap(fixed, masters);
			SetMPHeap(masters, masters);
			err = NewVMHeap(defaultDomain, handleSize, &reloc, options);
			if (err == noErr)
			{
				SetFixedHeap(reloc, fixed);
				SetMPHeap(reloc, masters);
				SetRelocHeap(fixed, reloc);
				SetRelocHeap(masters, reloc);
				SetRelocHeap(reloc, reloc);
				ClobberHeapSemaphore(reloc);
				SetSkiaHeapSemaphore(reloc, GetSkiaHeapSemaphore(fixed));
				*pResult = fixed;
				return noErr;
			}
		}
	}
	if (err != noErr && fixed != nil)
		DestroyVMHeap(fixed);
	return err;
}


// ROM 0x00142734 DestroyVMHeapHelper__FPv
static void
DestroyVMHeapHelper(void* heap)
{
	if (heap != nil)
		FreePagedMem((VAddr) GetHeapEnd(heap));
}


// ROM 0x0014276c DestroyVMHeap
extern "C" NewtonErr
DestroyVMHeap(Heap heap)
{
	if (heap != nil)
	{
		ClobberHeapSemaphore(heap);
		if (GetWiredHeap(heap) != nil)
		{
			// NOT YET RECONSTRUCTED: SWiredHeapPage::Destroy(GetWiredHeap(heap)->fPage)
			SetWiredHeap(heap, nil);
		}
		if (GetRelocHeap(heap) != heap)
			DestroyVMHeapHelper(GetRelocHeap(heap));
		if (GetMPHeap(heap) != heap)
			DestroyVMHeapHelper(GetMPHeap(heap));
		if (GetSPHeap(heap) != heap)
			DestroyVMHeapHelper(GetSPHeap(heap));
		DestroyVMHeapHelper(heap);
	}
	return noErr;
}


// ROM 0x00142844 ZapHeap
// Lays a fresh heap over an existing one's area (the verification word is
// '->-<'... '-><-').
extern "C" NewtonErr
ZapHeap(Heap heap, ULong verification, Boolean isPersistent)
{
	if (verification != 0x2d3e3c2d || heap == nil)
		return kError_Bad_Parameters;
	VAddr start, end;
	NewtonErr err = GetHeapAreaInfo((VAddr) heap, &start, &end);
	if (isPersistent)
		start = (start + 0xfff) & ~(VAddr) 0xfff;
	if (err != noErr)
		return err;
	Size initial = isPersistent ? 0x1000 : 0x400;
	err = SetHeapLimits(start, start + initial);
	LockHeapRange(start, start + initial, false);
	UnlockHeapRange(start, start + initial);
	Heap saved = GetHeap();
	Heap fresh = NewHeap((void*) start, end - start, initial);
	SetHeap(saved);
	if (fresh == nil)
		return kMemErr_NoMemory;
	SetHeapIsVMBacked(fresh);
	SetRemoveRoutine((VAddr) fresh, (ReleaseProcPtr) HeapReleaseRequestHandler, fresh);
	return err;
}


// ROM 0x00142948 ResurrectVMHeap
// A persistent heap found after a warm reboot is put back in service.
extern "C" NewtonErr
ResurrectVMHeap(Heap oldHeap)
{
	NewtonErr err = noErr;
	if (IsSkiaHeap(oldHeap))
	{
		ResurrectSkiaHeap((SkiaHeap*) oldHeap);
		err = AddSemaphoreToHeap(oldHeap);
		if (err == noErr)
			err = SetHeapLimits((VAddr) GetHeapStart(oldHeap), (VAddr) ((char*) oldHeap + GetHeapExtent(oldHeap)));
		SetRemoveRoutine((VAddr) oldHeap, (ReleaseProcPtr) HeapReleaseRequestHandler, oldHeap);
	}
	return err;
}


// ROM 0x00142ad8 ShrinkHeapLeaving
extern "C" NewtonErr
ShrinkHeapLeaving(Heap heap, Size amountLeftFree)
{
	NewtonErr err = ShrinkSkiaHeapLeaving((SkiaHeap*) heap, amountLeftFree);
	if (GetRelocHeap(heap) != heap)
		err = ShrinkSkiaHeapLeaving((SkiaHeap*) GetRelocHeap(heap), amountLeftFree);
	return err;
}


// ROM 0x00142d90 GetHeapRefcon
extern "C" void*
GetHeapRefcon(Heap heap)
{
	return GetSkiaHeapRefcon(heap);
}


// ROM 0x00142d94 SetHeapRefcon
extern "C" void
SetHeapRefcon(void* refCon, Heap heap)
{
	SetSkiaHeapRefcon(refCon, heap);
}


// ROM 0x00142d98 VoidStarToHeap
extern "C" Heap
VoidStarToHeap(void* base)
{
	return (Heap) ((char*) base + kBlockHeaderSize);
}


/* -------------------------------------------------------------------------------
	Walking a heap
------------------------------------------------------------------------------- */

// ROM 0x00271f00 HeapSeed
extern "C" long
HeapSeed(Heap heap)
{
	if (heap == nil)
		heap = GetCurrentHeap();
	return ((SkiaHeap*) heap)->fSeed;
}


// ROM 0x00271f1c NextHeapBlock
// The block after fromBlock (nil: the first, the header) and what it is.
extern "C" int
NextHeapBlock(Heap opaque_heap, long seed, void* fromBlock, void** pFoundBlock, void*** pFoundBlockHandle, int* pFoundBlockType, char* pFoundBlockTag, Size* pFoundBlockSize, TObjectId* pFoundBlockOwner)
{
	if (opaque_heap == nil)
		opaque_heap = GetCurrentHeap();
	SkiaHeap* heap = (SkiaHeap*) opaque_heap;
	if (heap->fSeed != seed)
		return kMM_HeapSeedFailure;
	SkiaBlock* from = SkiaBlock::Of(fromBlock != nil ? fromBlock : (void*) heap);
	SkiaBlock* b = from->Following();
	if ((char*) b >= heap->fEnd)
		b = (SkiaBlock*) fromBlock;			// (sic: past the sentinel the ROM reads the caller's data as a header)
	int type;
	void** handle = nil;
	char tag = 0;
	TObjectId owner = 0;
	if (b->IsFree())
		type = kMM_HeapFreeBlock;
	else if ((b->fFlags & kBlockFlag_KindMask) != kBlockFlag_Direct)
	{
		type = kMM_HeapHandleBlock;
		handle = (void**) b->fParent;
		tag = (char) b->fType;
		owner = (TObjectId) b->fOwner;
	}
	else if ((b->fFlags & kBlockFlag_Private) == 0 || b->fBusy != 0xff)
	{
		type = kMM_HeapPtrBlock;
		tag = (char) b->fType;
		owner = (TObjectId) b->fOwner;
	}
	else
	{
		switch (b->fType)
		{
		case 1:
		case 2:								type = kMM_InternalBlock; break;
		case kBlockType_HeapHeader:			type = kMM_HeapHeaderBlock; break;
		case kBlockType_EndSentinel:		type = kMM_HeapEndBlock; break;
		case kBlockType_MasterPointers:		type = kMM_HeapMPBlock; break;
		default:							return -1;
		}
	}
	if (pFoundBlock != nil)
		*pFoundBlock = b->Data();
	if (pFoundBlockHandle != nil)
		*pFoundBlockHandle = handle;
	if (pFoundBlockType != nil)
		*pFoundBlockType = type;
	if (pFoundBlockTag != nil)
		*pFoundBlockTag = tag;
	if (pFoundBlockSize != nil)
		*pFoundBlockSize = b->fSize;
	if (pFoundBlockOwner != nil)
		*pFoundBlockOwner = owner;
	return type;
}


// ROM 0x001430e0 CountHeapBlocks
// Counts the pointer and/or handle blocks (blockType, or 0 for both) whose
// name matches under the mask.
extern "C" void
CountHeapBlocks(Size* pTotalSize, ULong* pFoundCount, Heap heap, int blockType, ULong name, ULong nameMask)
{
	for (;;)
	{
		Heap walked = heap != nil ? heap : GetHeap();
		Size total = 0;
		ULong count = 0;
		long seed = HeapSeed(walked);
		void* block = nil;
		int type;
		Size size;
		TObjectId owner;
		while ((type = NextHeapBlock(walked, seed, block, &block, nil, nil, nil, &size, &owner)) != kMM_HeapSeedFailure)
		{
			if (type == kMM_HeapEndBlock)
			{
				if (pTotalSize != nil)
					*pTotalSize = total;
				if (pFoundCount != nil)
					*pFoundCount = count;
				return;
			}
			if ((type == kMM_HeapPtrBlock || type == kMM_HeapHandleBlock) && (blockType == 0 || type == blockType)
				&& (owner & nameMask & 0x7fffffff) == name)
			{
				total += size;
				count++;
			}
		}
		// the seed changed: start over
	}
}


// ROM 0x00271da0 CheckHeap
extern "C" NewtonErr
CheckHeap(Heap opaque_heap, void** whereSmashed)
{
	NewtonErr err = noErr;
	newton_try
	{
		if (opaque_heap == nil)
			opaque_heap = GetCurrentHeap();
		// NOT YET RECONSTRUCTED: VetHeap(opaque_heap, whereSmashed)
		(void) whereSmashed;
	}
	newton_catch(exAbort)
	{
		err = kMM_ExceptionGrokkingHeap;
	}
	end_try;
	return err;
}


// ROM 0x0011f328 ClearMemory
extern "C" void
ClearMemory(void* p, ULong size)
{
	ZeroBytes(p, size);
}
