/*
	File:		recognition/RecObject.h

	Contains:	The recognition system's object base classes: TRecObject
				(a flag word, and the Dispose/Dump/SizeInBytes/CopyInto
				virtuals), TArray (a growable array of fixed-size entries in
				a handle: an element size, a count, spare slots, a chunk to
				grow by, a use count for Clone/Release), TDArray (TArray with
				Delete/Insert in the middle) and TArrayIterator (a cursor
				over an array's entries).  The strokes, units, unit lists
				and areas of the recogniser are built on them.  The ROM's
				objects: TRecObject 8 bytes, TArray/TDArray 0x20.

				The recogniser keeps its storage in handles through its own
				MakeHandle/ResizeHandle/DeleteHandle (over the memory
				manager's), named with a tag; here they are the same thing
				over NewtonMemory.h's handles.

	Reconstructed from the MP2x00 US ROM (0x0021c984-0x0021ca70,
	0x00208e98-0x00209654, 0x0020c764-0x0020ca60, 0x0011b864-0x0011b958);
	each function cites its origin.
*/

#ifndef __RECOBJECT_H
#define __RECOBJECT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "NewtonMemory.h"
#include "objects.h"

class TMsg;

// the recogniser's handle functions (0x0011d2d8-)
Handle	MakeHandle(long size);							// ROM 0x0011b870 MakeHandle__Fl
long	ResizeHandle(Handle h, long size);				// ROM 0x0011b910 ResizeHandle__FPPcl - ==> 0, or an error
void	DeleteHandle(Handle h);							// ROM 0x0011b8f0 DeleteHandle__FPPc
long	SizeOfHandle(Handle h);							// ROM 0x0011b900 SizeOfHandle__FPPc
long	CopyHandle(Handle* h);							// ROM 0x0011b8cc CopyHandle__FPPPc - *h replaced by a copy; ==> 0, or an error
void	NameHandle(Handle h, ULong name);				// ROM 0x0011b930 NameHandle__FPPcUl (a tag: nothing on the host)
void	MoveBlock(const void* src, void* dst, long size);	// ROM 0x0011b868 MoveBlock__FPcT1l
long	MemoryError(void);								// ROM 0x0011b864 MemoryError__Fv
void	NamePtr(char* ptr, ULong name);					// ROM 0x0011b858 NamePtr__FPcUl (a tag: nothing on the host)
ULong	GetTicks(void);									// ROM 0x0011b8dc GetTicks__Fv - Ticks(), sixtieths of a second

class TRecObject
{
public:
					TRecObject();							// ROM 0x0021c984 __ct__10TRecObjectFv
	virtual			~TRecObject();							// ROM 0x0021c9b8 __dt__10TRecObjectFv
	virtual void	Dispose(void);							// ROM 0x0021ca50 Dispose__10TRecObjectFv (vtable +0x00: the object deleted)
	virtual void	Dump(TMsg* msg);						// ROM 0x0021ca48 Dump__10TRecObjectFP4TMsg
	virtual long	SizeInBytes(void);						// ROM 0x0021ca4c SizeInBytes__10TRecObjectFv
	virtual long	CopyInto(TRecObject* other);			// ROM 0x0021ca60 CopyInto__10TRecObjectFP10TRecObject

	void			SetFlags(ULong flags);					// ROM 0x0021c9d0 SetFlags__10TRecObjectFUl
	void			UnsetFlags(ULong flags);				// ROM 0x0021c9e0 UnsetFlags__10TRecObjectFUl
	Boolean			TestFlags(ULong flags);					// ROM 0x0021c9f0 TestFlags__10TRecObjectFUl
	void			DumpObject(char* title);				// ROM 0x0021ca04 DumpObject__10TRecObjectFPc

	ULong			fFlags;			// +0x04
};

// a cursor over an array's entries (0x20 bytes; the ROM keeps the entry
// address up to date across the handle's moves)
class TArray;
struct TArrayIterator
{
	Handle		fHandle;		// +0x00  the array's data
	char*		fBase;			// +0x04  where the data was when last looked at
	char*		fEntry;			// +0x08  the current entry
	long		fElementSize;	// +0x0c
	long		fIndex;			// +0x10
	long		fCount;			// +0x14
	char*		(*fGetNext)(TArrayIterator*);	// +0x18
	char*		(*fGetCur)(TArrayIterator*);	// +0x1c

	char*		GetNext(void)		{ return fGetNext(this); }
	char*		GetCur(void)		{ return fGetCur(this); }
	void		RemoveCurrent(void);					// ROM 0x00209304 RemoveCurrent__FP14TArrayIterator
};

class TArray : public TRecObject
{
public:
					TArray();								// ROM 0x00208e98 __ct__6TArrayFv
	virtual			~TArray();								// ROM 0x00208ed8 __dt__6TArrayFv
	static TArray*	Make(ULong elementSize, ULong count);	// ROM 0x00209260 Make__6TArraySFUlT1
	long			IArray(ULong elementSize, ULong count);	// ROM 0x00209484 IArray__6TArrayFUlT1 - ==> 0, or an error

	virtual void	Dispose(void);							// ROM 0x002095ac Dispose__6TArrayFv (released; gone when no user is left)
	virtual void	Dump(TMsg* msg);						// ROM 0x00209540 Dump__6TArrayFP4TMsg
	virtual long	SizeInBytes(void);						// ROM 0x00208f18 SizeInBytes__6TArrayFv
	virtual long	CopyInto(TRecObject* other);			// ROM 0x00208f54 CopyInto__6TArrayFP10TRecObject
	virtual void	IDispose(void);							// ROM 0x002095d8 IDispose__6TArrayFv (vtable +0x10: the storage freed, the object deleted)
	virtual long	Add(void);								// ROM 0x00209388 Add__6TArrayFv - a slot added at the end; ==> its index, -1 for no memory
	virtual char*	AddEntry(void);							// ROM 0x0020941c AddEntry__6TArrayFv - ==> the new entry
	virtual char*	GetEntry(ULong index);					// ROM 0x002091bc GetEntry__6TArrayFUl - nil past the count
	virtual char*	SetEntry(ULong index, const char* data);	// ROM 0x0020944c SetEntry__6TArrayFUlPc - the entry's bytes copied in; ==> the entry
	virtual void	Compact(void);							// ROM 0x00209038 Compact__6TArrayFv - the spare slots given back
	virtual void	CutToIndex(ULong index);				// ROM 0x00209368 CutToIndex__6TArrayFUl - the entries from the index dropped (kept as spare)
	virtual void	Clear(void);							// ROM 0x0020935c Clear__6TArrayFv
	virtual void	Reuse(ULong count);						// ROM 0x00208fe4 Reuse__6TArrayFUl - emptied, sized for count entries
	virtual long	Load(ULong, ULong, ULong, ULong);		// ROM 0x00209068 Load__6TArrayFUlN31 (nothing)
	virtual long	LoadFromSoup(RefArg headers, RefArg datas, ULong index);	// ROM 0x00209070 LoadFromSoup__6TArrayFRC6RefVarT1Ul
	virtual long	Save(ULong, ULong, ULong, ULong);		// ROM 0x00209114 Save__6TArrayFUlN31 (nothing to save to)

	char*			GetIterator(TArrayIterator* iter);		// ROM 0x002091f4 GetIterator__6TArrayFP14TArrayIterator - ==> the first entry
	void			Clone(void);							// ROM 0x00209608 Clone__6TArrayFv - one more user
	Boolean			Release(void);							// ROM 0x00209618 Release__6TArrayFv - one user fewer; ==> whether none is left
	char*			Lock(void);								// ROM 0x00209634 Lock__6TArrayFv - the data pinned; ==> its address
	void			Unlock(void);							// ROM 0x00209644 Unlock__6TArrayFv

	long			Count(void) const		{ return fCount; }
	long			ElementSize(void) const	{ return fElementSize; }

	long			fElementSize;	// +0x08
	long			fCount;			// +0x0c
	long			fFree;			// +0x10  spare slots after the count
	long			fChunk;			// +0x14  slots added at a time (6)
	long			fUsers;			// +0x18  Clone/Release count
	Handle			fData;			// +0x1c
};

class TDArray : public TArray
{
public:
					TDArray();								// ROM 0x0020c764 __ct__7TDArrayFv
	static TDArray*	Make(ULong elementSize, ULong count);	// ROM 0x0020c7a4 Make__7TDArraySFUlT1
	long			IDArray(ULong elementSize, ULong count);	// ROM 0x0020c800 IDArray__7TDArrayFUlT1

	virtual void	Delete(ULong index);					// ROM 0x0020c830 Delete__7TDArrayFUl
	virtual ULong	DeleteEntries(ULong index, ULong count);	// ROM 0x0020c83c DeleteEntries__7TDArrayFUlT1 - ==> the index, -1 past the end
	virtual ULong	Insert(ULong index);					// ROM 0x0020c8f8 Insert__7TDArrayFUl - a slot opened at the index; ==> its index, -1 for no memory
	virtual ULong	InsertEntry(ULong index, const char* data);	// ROM 0x0020c9a0 InsertEntry__7TDArrayFUlPc - ==> the index, -1 for no memory
	virtual ULong	InsertEntries(ULong index, const char* data, ULong count);	// ROM 0x0020c9f8 InsertEntries__7TDArrayFUlPcT1
};

#endif	/* __RECOBJECT_H */
