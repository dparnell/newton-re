/*
	File:		DoubleQ.cpp

	Contains:	Doubly linked queues used throughout the kernel.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The original constructors self-allocate (`if (this == 0) this = new ...`),
	which was the compiler's idiom, not application logic, and is not
	reproduced.
*/

#include "DoubleQ.h"

#include <stdint.h>


/* -------------------------------------------------------------------------------
	TDoubleQItem
------------------------------------------------------------------------------- */

// ROM 0x0009d8dc __ct__12TDoubleQItemFv
TDoubleQItem::TDoubleQItem()
{
	fNext = nil;
	fPrev = nil;
	fContainer = nil;
}


/* -------------------------------------------------------------------------------
	TDoubleQContainer
------------------------------------------------------------------------------- */

// ROM 0x0009dad0 __ct__17TDoubleQContainerFv
TDoubleQContainer::TDoubleQContainer()
{
	Init(0);
}


// ROM 0x0009db08 __ct__17TDoubleQContainerFPc
TDoubleQContainer::TDoubleQContainer(ULong offsetToDoubleQItem)
{
	Init(offsetToDoubleQItem);
}


// ROM 0x0009db44 __ct__17TDoubleQContainerFPcPFPvT1_vPv
TDoubleQContainer::TDoubleQContainer(ULong offsetToDoubleQItem, DestructorProcPtr destructor, void* destructorInstance)
{
	Init(offsetToDoubleQItem);
	fDestructorInstance = destructorInstance;
	fDestructor = destructor;
}


// ROM 0x0009db90 Init__17TDoubleQContainerFPc
void
TDoubleQContainer::Init(ULong offsetToDoubleQItem)
{
	fHead = nil;
	fOffsetToDoubleQItem = offsetToDoubleQItem;
	fTail = nil;
	fDestructor = nil;
	fDestructorInstance = nil;
}


// ROM 0x0009dbac CheckBeforeAdd__17TDoubleQContainerFPv
// A no-op in the release ROM (returns this); presumably a debug hook.
void
TDoubleQContainer::CheckBeforeAdd(void* /*item*/)
{
}


// The TDoubleQItem embedded in a queued object, and back again.
static inline TDoubleQItem* ItemOf(void* object, ULong offset)
{
	return (TDoubleQItem*) ((char*) object + offset);
}

static inline void* ObjectOf(TDoubleQItem* item, ULong offset)
{
	return (char*) item - offset;
}


// ROM 0x0009dbb0 Add__17TDoubleQContainerFPv
void
TDoubleQContainer::Add(void* item)
{
	TDoubleQItem* q = ItemOf(item, fOffsetToDoubleQItem);
	CheckBeforeAdd(item);
	q->fNext = nil;
	if (fHead == nil)
	{
		fHead = q;
		// The ROM stores the container, not nil, as the first item's fPrev.
		// Nothing follows fPrev from the head, so it is harmless; kept as-is.
		q->fPrev = (TDoubleQItem*) this;
	}
	else
	{
		fTail->fNext = q;
		q->fPrev = fTail;
	}
	fTail = q;
	q->fContainer = this;
}


// ROM 0x0009dc08 AddToFront__17TDoubleQContainerFPv
void
TDoubleQContainer::AddToFront(void* item)
{
	TDoubleQItem* q = ItemOf(item, fOffsetToDoubleQItem);
	CheckBeforeAdd(item);
	q->fNext = nil;
	q->fPrev = nil;
	if (fHead == nil)
		fTail = q;
	else
	{
		fHead->fPrev = q;
		q->fNext = fHead;
	}
	fHead = q;
	q->fContainer = this;
}


// ROM 0x0009d914 AddBefore__17TDoubleQContainerFPvT1
void
TDoubleQContainer::AddBefore(void* existingItem, void* item)
{
	TDoubleQItem* q = ItemOf(item, fOffsetToDoubleQItem);
	TDoubleQItem* before = ItemOf(existingItem, fOffsetToDoubleQItem);
	CheckBeforeAdd(item);
	if (fHead != nil && fHead != before)
	{
		q->fPrev = before->fPrev;
		q->fNext = before;
		before->fPrev = q;
		q->fPrev->fNext = q;
		q->fContainer = this;
		return;
	}
	// existingItem is the head (or the queue is empty): same as AddToFront
	AddToFront(item);
}


// ROM 0x0009d97c Remove__17TDoubleQContainerFv
void*
TDoubleQContainer::Remove()
{
	TDoubleQItem* q = fHead;
	if (q == nil)
		return nil;
	fHead = q->fNext;
	if (fHead == nil)
		fTail = nil;
	else
		fHead->fPrev = nil;
	q->fPrev = nil;
	q->fNext = nil;
	q->fContainer = nil;
	return ObjectOf(q, fOffsetToDoubleQItem);
}


// ROM 0x0009d9c4 RemoveFromQueue__17TDoubleQContainerFPv
Boolean
TDoubleQContainer::RemoveFromQueue(void* item)
{
	if (item == nil)
		return false;
	TDoubleQItem* q = ItemOf(item, fOffsetToDoubleQItem);
	if (q->fContainer != this)
		return false;

	TDoubleQItem* next = q->fNext;
	TDoubleQItem* prev = q->fPrev;
	if (fHead == q)
	{
		if (fTail == fHead)
		{
			fHead = nil;
			fTail = nil;
		}
		else
		{
			fHead = next;
			next->fPrev = nil;
		}
	}
	else
	{
		prev->fNext = next;
		if (fTail == q)
			fTail = prev;
		else
			next->fPrev = prev;
	}
	q->fPrev = nil;
	q->fNext = nil;
	q->fContainer = nil;
	return true;
}


// ROM 0x0009da44 DeleteFromQueue__17TDoubleQContainerFPv
Boolean
TDoubleQContainer::DeleteFromQueue(void* item)
{
	if (!RemoveFromQueue(item))
		return false;
	if (fDestructor != nil)
	{
		// The ROM clears the two low bits of the pointer before calling it
		// (`bic #3`); they carry no meaning we have found, so this is a no-op
		// for any properly aligned function.
		DestructorProcPtr proc = (DestructorProcPtr) ((uintptr_t) fDestructor & ~(uintptr_t) 3);
		proc(fDestructorInstance, (char*) item);
	}
	return true;
}


// ROM 0x0009da84 Peek__17TDoubleQContainerFv
void*
TDoubleQContainer::Peek()
{
	if (fHead == nil)
		return nil;
	return ObjectOf(fHead, fOffsetToDoubleQItem);
}


// ROM 0x0009da9c GetNext__17TDoubleQContainerFPv
void*
TDoubleQContainer::GetNext(void* item)
{
	if (item == nil)
		return nil;
	TDoubleQItem* q = ItemOf(item, fOffsetToDoubleQItem);
	if (q->fContainer != this || q->fNext == nil)
		return nil;
	return ObjectOf(q->fNext, fOffsetToDoubleQItem);
}
