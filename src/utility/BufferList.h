/*
	File:		utility/BufferList.h

	Contains:	CBufferList, a list of buffers read and written as one: a
				CList of CBuffer*s with a current segment, the byte calls going
				to it and moving on to the next when it is used up.  Hide and
				Seek walk the list; the segments before the first and after
				the last Hide has left are out of the list's view (fFirst,
				fLast), which is how a comm tool takes a header off the front
				of the data it is given.

				The DDK's BufferList.h is the external interface only (the
				constructor private "to prevent external code from knowing the
				size of the object"); this header replaces it
				(sync_ddk_headers.py, REPLACED) with the ROM's class, 0x20
				bytes, its virtuals CBuffer's.

	Reconstructed from the MP2x00 US ROM (0x00045bbc-0x00046abc); each
	function cites its origin.
*/

#ifndef __BUFFERLIST_H
#define __BUFFERLIST_H

#ifndef __BUFFERSEGMENT_H
#include "BufferSegment.h"
#endif

class CList;
class CListIterator;


class CBufferList : public CBuffer
{
public:
					CBufferList();
	virtual			~CBufferList();

	static CBufferList*	New(void)		{ return new CBufferList; }
	void			Delete(void)			{ delete this; }

	NewtonErr		Init(Boolean deleteSegments = true);					// a list of its own
	NewtonErr		Init(CList* bufList, Boolean deleteSegments = true);	// the caller's list

	// get primitives
	virtual int		Peek(void);
	virtual int		Next(void);
	virtual int		Skip(void);
	virtual int		Get(void);
	virtual Size	Getn(UByte* p, Size n);
	virtual int		CopyOut(UByte* p, Size& n);

	// put primitives
	virtual int		Put(int dataByte);
	virtual Size	Putn(const UByte* p, Size n);
	virtual int		CopyIn(const UByte* p, Size& n);

	// misc
	virtual void	Reset(void);
	virtual Size	GetSize(void) const;

	// position and size
	virtual Boolean	AtEOF(void) const;
	virtual Long	Hide(Long count, int dir);
	virtual Size	Seek(Long off, int dir);
	virtual Size	Position(void) const;

	void			ResetMark(void);

	// list methods
	CBuffer*		At(ArrayIndex index);
	CBuffer*		First(void);
	CBuffer*		Last(void);
	NewtonErr		Insert(CBuffer* item);
	NewtonErr		InsertBefore(ArrayIndex index, CBuffer* item);
	NewtonErr		InsertAt(ArrayIndex index, CBuffer* item);
	NewtonErr		InsertFirst(CBuffer* item);
	NewtonErr		InsertLast(CBuffer* item);
	NewtonErr		Remove(CBuffer* item);
	NewtonErr		RemoveAt(ArrayIndex index);
	NewtonErr		RemoveFirst(void);
	NewtonErr		RemoveLast(void);
	NewtonErr		RemoveAll(void);
	NewtonErr		Delete(CBuffer* item);			// removed and deleted
	NewtonErr		DeleteAt(ArrayIndex index);
	NewtonErr		DeleteFirst(void);
	NewtonErr		DeleteLast(void);
	NewtonErr		DeleteAll(void);
	ArrayIndex		GetIndex(CBuffer* item);

private:
	Boolean			NextSegment(void);
	void			SelectSegment(ArrayIndex index);

	CBuffer*		fSegment;			// +0x04  the current segment
	CList*			fList;				// +0x08
	CListIterator*	fIter;				// +0x0c
	ArrayIndex		fFirst;				// +0x10  the first segment in view
	ArrayIndex		fCurrent;			// +0x14  the current one's index
	ArrayIndex		fLast;				// +0x18  the last in view
	Boolean			fDeleteSegments;	// +0x1c  the segments go with the list
	Boolean			fOwnsList;			// +0x1d  the list is our own (Init())
};

#endif	/* __BUFFERLIST_H */
