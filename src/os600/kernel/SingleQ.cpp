/*
	File:		SingleQ.cpp

	Contains:	TSingleQContainer (SingleQ.h): a singly linked list threaded
				through a TSingleQItem inside each item, last in first out.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "SingleQ.h"


// ROM 0x001e4fd4 __ct__17TSingleQContainerFv
TSingleQContainer::TSingleQContainer()
{
	fHead = nil;
}


// ROM 0x001e4fe4 Init__17TSingleQContainerFUl
void
TSingleQContainer::Init(ULong offsetToSingleQItem)
{
	fOffsetToSingleQItem = offsetToSingleQItem;
	fHead = nil;
}


// ROM 0x001e4ff4 Add__17TSingleQContainerFPv
void
TSingleQContainer::Add(void* item)
{
	TSingleQItem* link = (TSingleQItem*) ((char*) item + fOffsetToSingleQItem);
	link->fLink = fHead;
	fHead = link;
}


// ROM 0x001e500c Remove__17TSingleQContainerFv
void*
TSingleQContainer::Remove()
{
	TSingleQItem* link = fHead;
	if (link == nil)
		return nil;
	fHead = link->fLink;
	return (char*) link - fOffsetToSingleQItem;
}


// ROM 0x001e5030 Peek__17TSingleQContainerFv
void*
TSingleQContainer::Peek()
{
	if (fHead == nil)
		return nil;
	return (char*) fHead - fOffsetToSingleQItem;
}


// ROM 0x001e5048 GetNext__17TSingleQContainerFPv
void*
TSingleQContainer::GetNext(void* item)
{
	if (item == nil)
		return nil;
	TSingleQItem* link = ((TSingleQItem*) ((char*) item + fOffsetToSingleQItem))->fLink;
	if (link == nil)
		return nil;
	return (char*) link - fOffsetToSingleQItem;
}
