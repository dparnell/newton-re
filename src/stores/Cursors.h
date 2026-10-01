/*
	File:		stores/Cursors.h

	Contains:	TUnionSoupIndex, the index over the same-path indexes of a
				union soup's soups (one soup is the common case), and
				TCursor, a query's position in a soup: the cursor frame
				(cursorPrototype with the TCursor in a C-object binary in its
				TCursor slot) that NewtonScript walks with Next/Prev/Entry.
				TCollectCursor collects the matching entries up front.

	A query spec: indexPath (default _uniqueID), beginKey/beginExclKey,
	endKey/endExclKey, startKey, secOrder, indexValidTest (a function of
	the key), validTest and endTest (functions of the entry), tagSpec,
	words/entireWords and text (sped up by the entries' word hints,
	GetWordsHints, and read through the text cache's decompressor).

	The ROM's layouts: TUnionSoupIndex 0x14, UnionIndexData 0x84 per soup,
	TCursor 0xc0, TCollectCursor 0xc8, CursorState 0x60.
*/

#ifndef __CURSORS_H
#define __CURSORS_H

#ifndef __SOUPS_H
#include "Soups.h"
#endif


/*------------------------------------------------------------------------------
	T U n i o n S o u p I n d e x
------------------------------------------------------------------------------*/

enum
{
	kUnionStateInvalid = 0,		// the soup's position must be found afresh
	kUnionStateValid,			// positioned on fKeyField
	kUnionStateExhausted		// past the end in the current direction
};

struct UnionIndexData
{
				UnionIndexData();

	TSoupIndex*	fIndex;						// +0x00
	long		fState;						// +0x04  kUnionState...
	IndexState	fIndexState;				// +0x08
	KeyField	fKeyBuffer[kKeyFieldBufferSize];	// +0x18
	KeyField*	fKeyField;					// +0x7c  &fKeyBuffer
	long		fModCount;					// +0x80  the node cache's when the state was taken
};

class TUnionSoupIndex : public TAbstractSoupIndex
{
public:
				TUnionSoupIndex(long numSoups, UnionIndexData* data);
				~TUnionSoupIndex();

	int			Find(SKey* key, SKey* outKey, SKey* outData, Boolean exact);
	int			First(SKey* outKey, SKey* outData);
	int			Last(SKey* outKey, SKey* outData);
	int			Next(SKey* key, SKey* data, int mode, SKey* outKey, SKey* outData);
	int			Prior(SKey* key, SKey* data, Boolean skipDups, SKey* outKey, SKey* outData);
	int			Search(Boolean forward, SKey* key, SKey* data, IndexStopProcPtr stop, void* refCon, SKey* outKey, SKey* outData, int mode);
	int			MoveToNextSoup(Boolean forward, int mode, SKey* key, Boolean currentGone);
	int			CurrentSoupGone(SKey* key, SKey* outKey, SKey* outData);
	void		InvalidateState(void);
	Boolean		IsValidState(SKey* key, SKey* data);
	void		Commit(void);
	void		SetCurrentSoup(long index);

	long			fNumSoups;				// +0x04
	UnionIndexData*	fData;					// +0x08
	long			fCurrentSoup;			// +0x0c
	Boolean			fDirection;				// +0x10  the direction the states were taken in
};


/*------------------------------------------------------------------------------
	T C u r s o r
------------------------------------------------------------------------------*/

// the query's parts (fFlags)
enum
{
	kQueryTags = 0x001,
	kQueryWords = 0x002,
	kQueryText = 0x004,
	kQueryBeginKey = 0x008,
	kQueryBeginExclKey = 0x010,
	kQueryEndKey = 0x020,
	kQueryEndExclKey = 0x040,
	kQueryIndexValidTest = 0x080,
	kQueryValidTest = 0x100,
	kQueryEndTest = 0x200,
	kQueryEntireWords = 0x400,
	kQueryKeyBounds = kQueryBeginKey | kQueryBeginExclKey | kQueryEndKey | kQueryEndExclKey,
	kQueryEntryTests = kQueryIndexValidTest | kQueryValidTest | kQueryEndTest
};

struct CursorSoupInfo
{
				CursorSoupInfo();

	Ref			fSoup;			// +0x00
	Ref			fTagsBits;		// +0x04  the query's tagSpec encoded against this soup's tags (EncodeQueryTags)
};

struct CursorState				// 0x60
{
	long		fSoupIndex;		// +0x00
	Ref			fEntry;			// +0x04
	SKey		fKey;			// +0x08
	SKey		fEntryData;		// +0x58  the entry's store object id as the index datum
	Boolean		fParkedAtEnd;	// +0x5c
	Boolean		fEntryRemoved;	// +0x5d
};

class TCursor
{
public:
				TCursor();
	virtual		~TCursor();

	// the virtuals, in the ROM's order
	virtual Ref	Move(long count);
	virtual Ref	GotoEntry(RefArg entry);
	virtual Ref	GotoKey(RefArg key);
	virtual Ref	Clone(void);
	virtual long	CountEntries(void);
	virtual void	EntryRemoved(RefArg entry);
	virtual void	EntrySoupChanged(RefArg entry, RefArg newEntry);
	virtual void	GCMark(void);
	virtual void	GCUpdate(void);
	virtual void	RebuildInfo(Boolean keepSoupInfo, long removedSoup);
	virtual void	Invalidate(void);

	static Ref	CreateNewCursor(void);
	void		Init(RefArg cursor, RefArg soup, RefArg querySpec);
	void		Init(RefArg cursor, const TCursor* other);
	Ref			CloneFrameSlot(RefArg frame, RefArg tag) const;
	void		BuildSoupsInfo(void);
	void		CreateIndexes(void);
	int			ExitParking(Boolean forward);
	Boolean		KeyBoundsValidTest(const SKey& key, Boolean atEnd);
	Boolean		WordsValidTest(PSSId id);
	Boolean		TextValidTest(PSSId id);
	Boolean		ValidTest(const SKey& key, PSSId id, Boolean atEnd, Boolean* entryMade, Boolean* outOfBounds);
	Ref			Entry(void);
	Ref			EntryKey(void);
	void		GetState(CursorState* state);
	void		SetState(CursorState& state);
	void		MakeEntryFaultBlock(PSSId id);
	void		Park(Boolean atEnd);
	long		GetSoupInfoIndex(RefArg soup);
	void		SoupRemoved(RefArg soup);
	void		SoupAdded(RefArg soup);
	Ref			Status(void);
	void		SetSoup(RefArg soup);
	void		IndexRemoved(RefArg soup, RefArg indexDesc);
	void		IndexObjectsChanged(void);
	void		SoupTagsChanged(RefArg soup);
	Boolean		PinCurrentKey(void);
	Ref			Reset(void);
	Ref			ResetToEnd(void);
	Ref			IsParked(void);
	void		EntryChanged(RefArg entry, Boolean keysChanged, Boolean tagsChanged);
	void		EntryReadded(RefArg entry, RefArg faultBlock);
	void		RegisterInSoup(RefArg soup) const;
	void		UnregisterFromSoup(RefArg soup) const;

	Ref			fSoup;				// +0x04  the soup queried (plain or union)
	Ref			fCursor;			// +0x08  the cursor frame
	ULong		fFlags;				// +0x0c  kQuery...
	long		fNumSoups;			// +0x10
	CursorSoupInfo*	fSoupInfo;		// +0x14  [fNumSoups]
	TUnionSoupIndex*	fIndex;		// +0x18
	Ref			fTagSpec;			// +0x1c
	TSoupIndex**	fTagsIndexes;	// +0x20  [fNumSoups] the soups' tags indexes, for a tagSpec query
	Ref			fIndexPath;			// +0x24
	Ref			fIndexType;			// +0x28
	Boolean		fSecOrder;			// +0x2c
	Ref			fWords;				// +0x30
	void*		fWordsHints;		// +0x34  the words' hint chunks (GetWordsHints)
	Ref			fText;				// +0x38
	Ref			fIndexValidTest;	// +0x3c
	Ref			fValidTest;			// +0x40
	Ref			fEndTest;			// +0x44
	Ref			fTestArgs;			// +0x48  the one-element argument array for the tests
	Ref			fStartKey;			// +0x4c
	Ref			fBeginKey;			// +0x50  (nil once fBeginKeyData is made)
	Ref			fEndKey;			// +0x54
	SKey*		fBeginKeyData;		// +0x58
	SKey*		fEndKeyData;		// +0x5c
	long		fMissingIndex;		// +0x60  a soup added to the union lacks the index
	SKey		fEntryData;			// +0x64  the current entry's store object id, as the index datum (host: an SKey, the ROM's 4-byte ULong)
	PSSId		EntryId(void) const	{ return (PSSId) (long) fEntryData; }
	Ref			fEntry;				// +0x68  the current entry (nil: parked)
	SKey		fKey;				// +0x6c  the current key
	Boolean		fParkedAtEnd;		// +0xbc
	Boolean		fEntryRemoved;		// +0xbd  the current entry was removed (Entry answers 'deleted)
};

class TCollectCursor : public TCursor
{
public:
				TCollectCursor();
	virtual		~TCollectCursor();

	virtual Ref	Move(long count);
	virtual Ref	GotoEntry(RefArg entry);
	virtual Ref	GotoKey(RefArg key);
	virtual Ref	Clone(void);
	virtual long	CountEntries(void);
	virtual void	EntryRemoved(RefArg entry);
	virtual void	EntrySoupChanged(RefArg entry, RefArg newEntry);
	virtual void	GCMark(void);
	virtual void	GCUpdate(void);
	virtual void	RebuildInfo(Boolean keepSoupInfo, long removedSoup);
	virtual void	Invalidate(void);

	static Ref	CreateNewCollectCursor(void);
	void		Collect(void);
	void		DefineCurrentEntry(void);
	long		FindEntry(RefArg entry);

	Ref			fEntries;			// +0xc0  [id, soup index] pairs
	long		fCurrent;			// +0xc4
};

TCursor*	CursorObj(RefArg cursor);
Ref			CursorMove(RefArg cursor, long count);
Ref			CursorNext(RefArg cursor);
Ref			CursorPrev(RefArg cursor);
Ref			CursorReset(RefArg cursor);
Ref			CursorEntry(RefArg cursor);
Ref			CursorClone(RefArg cursor);
Ref			CursorGoto(RefArg cursor, RefArg entry);
Ref			CursorGotoKey(RefArg cursor, RefArg key);
Ref			CursorResetToEnd(RefArg cursor);
Ref			CursorCountEntries(RefArg cursor);
Ref			CursorWhichEnd(RefArg cursor);
Ref			CommonSoupQuery(RefArg rcvr, RefArg querySpec);
Ref			SoupCollect(RefArg rcvr, RefArg querySpec);
void		DefineCursor(RefArg soup, RefArg querySpec, RefArg cursor);
void		InitCursorPrototype(void);			// host: the cursor prototype frame when no ROM objects are imported
void		RegisterCursorNatives(void);

#endif	/* __CURSORS_H */
