/*
	File:		DoubleQ.h

	Contains:	Doubly linked queues used throughout the kernel (task queues,
				timer queues, port senders/receivers, ...).

				The DDK ships SingleQ.h for the singly linked variant; this is
				the kernel-private double-ended counterpart reconstructed from
				the MP2100 D ROM.  As with TSingleQContainer, the container is
				told the offset of the TDoubleQItem inside the objects it links,
				so callers deal in their own object pointers, not queue items.

	Reconstructed from:	TDoubleQItem 0x0009d8dc, TDoubleQContainer 0x0009d914-0x0009dc48
*/

#ifndef __DOUBLEQ_H
#define __DOUBLEQ_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TDoubleQContainer;

// Called by TDoubleQContainer::DeleteFromQueue to dispose of a removed item:
// (destructorInstance, item).
typedef void (*DestructorProcPtr)(void* instance, char* item);


// ROM size 0x0c
class TDoubleQItem
{
	public:
						TDoubleQItem();

		TDoubleQItem*		fNext;			// +0x00
		TDoubleQItem*		fPrev;			// +0x04  (see note in Add)
		TDoubleQContainer*	fContainer;		// +0x08  container this item is queued in, or nil
};


// ROM size 0x14
class TDoubleQContainer
{
	public:
						TDoubleQContainer();
						TDoubleQContainer(ULong offsetToDoubleQItem);
						TDoubleQContainer(ULong offsetToDoubleQItem, DestructorProcPtr destructor, void* destructorInstance);

		void			Init(ULong offsetToDoubleQItem);

		void			CheckBeforeAdd(void* item);
		void			Add(void* item);						// append
		void			AddToFront(void* item);
		void			AddBefore(void* existingItem, void* item);	// insert item before existingItem
		void*			Remove();								// pop the head; nil if empty
		Boolean			RemoveFromQueue(void* item);			// true if item was in this queue
		Boolean			DeleteFromQueue(void* item);			// RemoveFromQueue, then the destructor proc
		void*			Peek();									// head, nil if empty
		void*			GetNext(void* item);					// item's successor, nil at the end

	private:
		TDoubleQItem*	fHead;					// +0x00
		TDoubleQItem*	fTail;					// +0x04
		ULong			fOffsetToDoubleQItem;	// +0x08  byte offset of the TDoubleQItem in queued objects
		DestructorProcPtr fDestructor;			// +0x0c  optional, used by DeleteFromQueue
		void*			fDestructorInstance;	// +0x10  first argument to fDestructor
};

#endif	/* __DOUBLEQ_H */
