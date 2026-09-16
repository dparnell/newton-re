/*
	File:		utility/NArray.h

	Contains:	NArray - a second family of growable fixed-element arrays
				(beside CDynamicArray), with NSortedArray keeping its elements
				ordered by an NComparator, and NIterator, the cursor an array
				tells about insertions and removals.  Not in the DDK; these
				declarations follow the ROM (0x00127298-0x00127708,
				0x0012a00c-0x0012a5ec, 0x0012b704-0x0012b910).  The protocol
				registry keeps its entries in an NSortedArray.

	Layouts (ROM): NArray 0x20 - vptr +0, fCount +4, fPhysicalCount +8,
	fElementSize +0xc, fChunkSize +0x10, fArray +0x14, fIterators +0x18,
	fShrink +0x1c; NSortedArray 0x24 adds fComparator +0x20; NComparator
	0x4 (vptr), NBlockComparator 0x8 adds fSize +4.  NIterator: fArray +0,
	fCurrent +4, fLow +8, fHigh +0xc, fNext +0x14, fReverse +0x18 (the ROM
	names only its three array-notification methods; the field names here
	are ours).
*/

#ifndef __NARRAY_H
#define __NARRAY_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif


// Orders keys.  KeyOf turns an element into a key (identity here);
// CompareKeys answers <0, 0, >0.
class NComparator
{
public:
					NComparator();
	virtual			~NComparator();
	virtual const void*	KeyOf(const void* element) const;
	virtual int		CompareKeys(const void* key1, const void* key2) const;
};


// Keys are blocks of fSize bytes, compared with memcmp.
class NBlockComparator : public NComparator
{
public:
					NBlockComparator(long size);
	virtual			~NBlockComparator();
	virtual int		CompareKeys(const void* key1, const void* key2) const;

	long			fSize;
};


class NIterator;

class NArray
{
public:
					NArray();
	virtual			~NArray();
	virtual void*	At(long index) const;					// nil when out of range
	virtual long	Contains(const void* element) const;	// its index, or -1
	virtual long	Where(const void* element) const;		// where it would go: the end

	NewtonErr		Init(long elementSize, long chunkSize, long physicalCount, Boolean shrink);
	NewtonErr		InsertElements(long index, long count, const void* elements);
	NewtonErr		RemoveElements(long index, long count);
	NewtonErr		SetCount(long count);
	NewtonErr		SetPhysicalCount(long count);

	long			fCount;				// +0x04
	long			fPhysicalCount;		// +0x08  elements the block holds
	long			fElementSize;		// +0x0c
	long			fChunkSize;			// +0x10  the block grows in these (elements)
	void*			fArray;				// +0x14
	NIterator*		fIterators;			// +0x18  ring of iterators to keep in step
	Boolean			fShrink;			// +0x1c  give memory back when the count drops
};


class NSortedArray : public NArray
{
public:
					NSortedArray();
	virtual			~NSortedArray();
	virtual long	Contains(const void* element) const;	// index of the last element equal to it, or -1
	virtual long	Where(const void* element) const;		// after the last element not greater than it

	NewtonErr		Init(NComparator* comparator, long elementSize, long chunkSize, long physicalCount, Boolean shrink);

	NComparator*	fComparator;		// +0x20
};


// An iterator's position is kept valid across changes to its array.
class NIterator
{
public:
	void			InsertElements(long index, long count);
	void			RemoveElements(long index, long count);
	void			DeleteArray();

	NArray*			fArray;				// +0x00
	long			fCurrent;			// +0x04
	long			fLow;				// +0x08
	long			fHigh;				// +0x0c
	long			fUnknown10;			// +0x10
	NIterator*		fNext;				// +0x14  the ring: back to fArray->fIterators
	Boolean			fReverse;			// +0x18
};

#endif	/* __NARRAY_H */
