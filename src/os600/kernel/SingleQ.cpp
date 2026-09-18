/*
	File:		SingleQ.cpp

	Contains:	TSingleQContainer (SingleQ.h): a singly linked list threaded
				through a TSingleQItem inside each item, last in first out.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SingleQ.h"


// ROM 0x001e2bbc __ct__17TSingleQContainerFv
TSingleQContainer::TSingleQContainer()
{
	fHead = nil;
}


// ROM 0x001e2bcc Init__17TSingleQContainerFUl
void
TSingleQContainer::Init(ULong offsetToSingleQItem)
{
	fOffsetToSingleQItem = offsetToSingleQItem;
	fHead = nil;
}


// ROM 0x001e2bdc Add__17TSingleQContainerFPv
void
TSingleQContainer::Add(void* item)
{
	TSingleQItem* link = (TSingleQItem*) ((char*) item + fOffsetToSingleQItem);
	link->fLink = fHead;
	fHead = link;
}


// ROM 0x001e2bf4 Remove__17TSingleQContainerFv
void*
TSingleQContainer::Remove()
{
	TSingleQItem* link = fHead;
	if (link == nil)
		return nil;
	fHead = link->fLink;
	return (char*) link - fOffsetToSingleQItem;
}


// ROM 0x001e2c18 Peek__17TSingleQContainerFv
void*
TSingleQContainer::Peek()
{
	if (fHead == nil)
		return nil;
	return (char*) fHead - fOffsetToSingleQItem;
}


// ROM 0x001e2c30 GetNext__17TSingleQContainerFPv
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
