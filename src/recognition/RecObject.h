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

	Reconstructed from the MP2100 D ROM (0x0021a254-0x0021a340,
	0x00206768-0x00206f24, 0x0020a034-0x0020a330, 0x0011d2cc-0x0011d3c0);
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
Handle	MakeHandle(long size);							// ROM 0x0011d2d8 MakeHandle__Fl
long	ResizeHandle(Handle h, long size);				// ROM 0x0011d378 ResizeHandle__FPPcl - ==> 0, or an error
void	DeleteHandle(Handle h);							// ROM 0x0011d358 DeleteHandle__FPPc
long	SizeOfHandle(Handle h);							// ROM 0x0011d368 SizeOfHandle__FPPc
long	CopyHandle(Handle* h);							// ROM 0x0011d334 CopyHandle__FPPPc - *h replaced by a copy; ==> 0, or an error
void	NameHandle(Handle h, ULong name);				// ROM 0x0011d398 NameHandle__FPPcUl (a tag: nothing on the host)
void	MoveBlock(const void* src, void* dst, long size);	// ROM 0x0011d2d0 MoveBlock__FPcT1l
long	MemoryError(void);								// ROM 0x0011d2cc MemoryError__Fv

class TRecObject
{
public:
					TRecObject();							// ROM 0x0021a254 __ct__10TRecObjectFv
	virtual			~TRecObject();							// ROM 0x0021a288 __dt__10TRecObjectFv
	virtual void	Dispose(void);							// ROM 0x0021a320 Dispose__10TRecObjectFv (vtable +0x00: the object deleted)
	virtual void	Dump(TMsg* msg);						// ROM 0x0021a318 Dump__10TRecObjectFP4TMsg
	virtual long	SizeInBytes(void);						// ROM 0x0021a31c SizeInBytes__10TRecObjectFv
	virtual long	CopyInto(TRecObject* other);			// ROM 0x0021a330 CopyInto__10TRecObjectFP10TRecObject

	void			SetFlags(ULong flags);					// ROM 0x0021a2a0 SetFlags__10TRecObjectFUl
	void			UnsetFlags(ULong flags);				// ROM 0x0021a2b0 UnsetFlags__10TRecObjectFUl
	Boolean			TestFlags(ULong flags);					// ROM 0x0021a2c0 TestFlags__10TRecObjectFUl
	void			DumpObject(char* title);				// ROM 0x0021a2d4 DumpObject__10TRecObjectFPc

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
	void		RemoveCurrent(void);					// ROM 0x00206bd4 RemoveCurrent__FP14TArrayIterator
};

class TArray : public TRecObject
{
public:
					TArray();								// ROM 0x00206768 __ct__6TArrayFv
	virtual			~TArray();								// ROM 0x002067a8 __dt__6TArrayFv
	static TArray*	Make(ULong elementSize, ULong count);	// ROM 0x00206b30 Make__6TArraySFUlT1
	long			IArray(ULong elementSize, ULong count);	// ROM 0x00206d54 IArray__6TArrayFUlT1 - ==> 0, or an error

	virtual void	Dispose(void);							// ROM 0x00206e7c Dispose__6TArrayFv
	virtual void	Dump(TMsg* msg);						// ROM 0x00206e10 Dump__6TArrayFP4TMsg
	virtual long	SizeInBytes(void);						// ROM 0x002067e8 SizeInBytes__6TArrayFv
	virtual long	CopyInto(TRecObject* other);			// ROM 0x00206824 CopyInto__6TArrayFP10TRecObject
	virtual void	IDispose(void);							// ROM 0x00206ea8 IDispose__6TArrayFv (vtable +0x10: the storage freed)
	virtual long	Add(void);								// ROM 0x00206c58 Add__6TArrayFv - a slot added at the end; ==> its index, -1 for no memory
	virtual char*	AddEntry(void);							// ROM 0x00206cec AddEntry__6TArrayFv - ==> the new entry
	virtual char*	GetEntry(ULong index);					// ROM 0x00206a8c GetEntry__6TArrayFUl - nil past the count
	virtual char*	SetEntry(ULong index, const char* data);	// ROM 0x00206d1c SetEntry__6TArrayFUlPc - the entry's bytes copied in; ==> the entry
	virtual void	Compact(void);							// ROM 0x00206908 Compact__6TArrayFv - the spare slots given back
	virtual void	CutToIndex(ULong index);				// ROM 0x00206c38 CutToIndex__6TArrayFUl - the entries from the index dropped (kept as spare)
	virtual void	Clear(void);							// ROM 0x00206c2c Clear__6TArrayFv
	virtual void	Reuse(ULong count);						// ROM 0x002068b4 Reuse__6TArrayFUl - emptied, sized for count entries
	virtual long	Load(ULong, ULong, ULong, ULong);		// ROM 0x00206938 Load__6TArrayFUlN31 (nothing)
	virtual long	LoadFromSoup(RefArg headers, RefArg datas, ULong index);	// ROM 0x00206940 LoadFromSoup__6TArrayFRC6RefVarT1Ul
	virtual long	Save(ULong, ULong, ULong, ULong);		// ROM 0x002069e4 Save__6TArrayFUlN31 (nothing to save to)

	char*			GetIterator(TArrayIterator* iter);		// ROM 0x00206ac4 GetIterator__6TArrayFP14TArrayIterator - ==> the first entry
	void			Clone(void);							// ROM 0x00206ed8 Clone__6TArrayFv - one more user
	Boolean			Release(void);							// ROM 0x00206ee8 Release__6TArrayFv - one user fewer; ==> whether none is left
	char*			Lock(void);								// ROM 0x00206f04 Lock__6TArrayFv - the data pinned; ==> its address
	void			Unlock(void);							// ROM 0x00206f14 Unlock__6TArrayFv

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
					TDArray();								// ROM 0x0020a034 __ct__7TDArrayFv
	static TDArray*	Make(ULong elementSize, ULong count);	// ROM 0x0020a074 Make__7TDArraySFUlT1
	long			IDArray(ULong elementSize, ULong count);	// ROM 0x0020a0d0 IDArray__7TDArrayFUlT1

	virtual void	Delete(ULong index);					// ROM 0x0020a100 Delete__7TDArrayFUl
	virtual ULong	DeleteEntries(ULong index, ULong count);	// ROM 0x0020a10c DeleteEntries__7TDArrayFUlT1 - ==> the index, -1 past the end
	virtual ULong	Insert(ULong index);					// ROM 0x0020a1c8 Insert__7TDArrayFUl - a slot opened at the index; ==> its index, -1 for no memory
	virtual ULong	InsertEntry(ULong index, const char* data);	// ROM 0x0020a270 InsertEntry__7TDArrayFUlPc - ==> the index, -1 for no memory
	virtual ULong	InsertEntries(ULong index, const char* data, ULong count);	// ROM 0x0020a2c8 InsertEntries__7TDArrayFUlPcT1
};

#endif	/* __RECOBJECT_H */
