/*
	File:		stores/Tags.cpp

	Contains:	Soup tags: TagsBits, the encoding of an entry's tags and of
				a query's tagSpec against a soup's tags array, the tags
				index kept up to date with the entries (AlterTagsIndex,
				UpdateTagsIndex), the tags test a cursor applies, and the
				plain soup's tag methods (AddTags, RemoveTags, ModifyTag,
				HasTags, GetTags).  See Tags.h.

	Reconstructed from the MP2x00 US ROM (0x002d0b04-0x002d1228,
	0x00348074, 0x0034a104-0x0034b03c, 0x0034e7f0).
*/

#include "Tags.h"
#include "Soups.h"
#include "Cursors.h"
#include "NSErrors.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"
#include "OSErrors.h"
#include "NewtonTime.h"
#include <string.h>

extern const ExceptionName exStoreError;

// the store errors (the names are ours; see NSErrors.h)
#define kNSErrBadTagsIndex		(ERRBASE_FRAMES - 25)		// the tags index answered neither found nor not found (0xffff4467)
#define kNSErrTooManyTags		(ERRBASE_FRAMES - 26)		// over kMaxTags (0xffff4466)
#define kNSErrBadTagSpec		(ERRBASE_FRAMES - 28)		// a tagSpec with no mode, ModifyTag to a tag that exists (0xffff4464)

// The index error result raised: an index result over 0 is the key size
// error, a negative one the store error it is.
static void
ThrowTagsIndexError(int result)
{
	if (result != kIndexOK)
		Throw(exStoreError, (void*) (Long) (result > 0 ? kNSErrKeySizeTooBig : result), nil);
}


/*------------------------------------------------------------------------------
	T a g s B i t s
------------------------------------------------------------------------------*/

// ROM 0x002d0b04 SetTag__8TagsBitsFs
// Bit tag set; the bitmap grown (zeroed) to reach it.
void
TagsBits::SetTag(short tag)
{
	long index = tag >> 3;
	long needed = index + 1;
	long size = Size();
	if (size < needed)
	{
		SetSize((short) needed);
		memset(fData + size, 0, needed - size);
	}
	fData[index] |= (UByte) (1 << (tag & 7));
}


// ROM 0x002d0d0c ValidTest__8TagsBitsCFRC8TagsBitsl
// Whether these (an entry's) bits satisfy the query's in the mode:
// equal, all (every query bit set: no when the entry's bitmap is
// shorter), any (some query bit set), none (no query bit set).
Boolean
TagsBits::ValidTest(const TagsBits& query, long mode) const
{
	if (mode == kTagsEqual)
		return Equals(query);
	long mySize = Size();
	long querySize = query.Size();
	long count = mySize;
	if (mySize != querySize)
	{
		if (querySize < mySize)
			count = querySize;
		if (mySize < querySize && mode == kTagsAll)
			return false;
	}
	const UByte* mine = fData;
	const UByte* theirs = query.fData;
	for (long i = 0; i < count; i++)
	{
		UByte both = mine[i] & theirs[i];
		if (mode == kTagsAll)
		{
			if (both != theirs[i])
				return false;
		}
		else if (mode == kTagsAny)
		{
			if (both != 0)
				return true;
		}
		else if (mode == kTagsNone)
		{
			if (both != 0)
				return false;
		}
	}
	return mode != kTagsAny;
}


// ROM 0x002ac044 (unnamed)
// The bits as a binary object of class 'tags (the header and the bitmap).
static Ref
MakeTagsBinary(const TagsBits& bits)
{
	long size = bits.Size() + 2;
	RefVar binary(AllocateBinary(RSSYMtags, size));
	memcpy(BinaryData(binary), &bits, size);
	return binary;
}


/*------------------------------------------------------------------------------
	E n c o d i n g
------------------------------------------------------------------------------*/

// ROM 0x002d0e30 EncodeTags__FRC6RefVarT1P8TagsBits
// A tag (a symbol) or an array of them as bits, each's place in the tags
// array its bit.  ==> whether every tag is in the array.
Boolean
EncodeTags(RefArg tags, RefArg tagOrTags, TagsBits* outBits)
{
	outBits->SetSize(0);
	Boolean allFound = true;
	RefVar none;
	RefVar tag;
	if (!IsArray(tagOrTags))
	{
		if (!IsSymbol(tagOrTags))
			ThrowExFramesWithBadValue(kNSErrNotASymbol, tagOrTags);
		Ref index = FSetContains(none, tags, tagOrTags);
		allFound = ISINT(index);
		if (allFound)
			outBits->SetTag((short) RVALUE(index));
	}
	else
	{
		for (long i = Length(tagOrTags) - 1; i >= 0; i--)
		{
			tag = GetArraySlotRef(tagOrTags, i);
			if (!IsSymbol(tag))
				ThrowExFramesWithBadValue(kNSErrNotASymbol, tag);
			Ref index = FSetContains(none, tags, tag);
			if (ISINT(index))
				outBits->SetTag((short) RVALUE(index));
			else
				allFound = false;
		}
	}
	return allFound;
}


// ROM 0x002ac24c (unnamed)
// One mode of the tagSpec encoded: the mode's tags (when the spec has
// the slot) as [mode, bits] appended to the result.  ==> false when a
// tag of an equal/all mode is not in the soup's tags at all (no entry
// can match).
static Boolean
EncodeQueryTagsMode(RefArg tags, RefArg tagSpec, RefArg slot, long mode, RefArg result)
{
	RefVar spec(GetFrameSlotRef(tagSpec, slot));
	if ((Ref) spec != NILREF)
	{
		TagsBits bits;
		bits.Clear();
		if (!EncodeTags(tags, spec, &bits) && mode < kTagsAny)
			return false;
		AddArraySlot(result, RefVar(MAKEINT(mode)));
		AddArraySlot(result, RefVar(MakeTagsBinary(bits)));
	}
	return true;
}


// ROM 0x002d10bc EncodeQueryTags__FRC6RefVarT1
// A query's tagSpec {equal, all, any, none} encoded against the tags
// index description's tags: an array of [mode, bits binary] pairs; nil
// when no entry of the soup can match.
Ref
EncodeQueryTags(RefArg indexDesc, RefArg tagSpec)
{
	RefVar result(AllocateArray(RSSYMarray, 0));
	RefVar tags(GetFrameSlotRef(indexDesc, RSSYMtags));
	if (!EncodeQueryTagsMode(tags, tagSpec, RSSYMequal, kTagsEqual, result)
	 || !EncodeQueryTagsMode(tags, tagSpec, RSSYMall, kTagsAll, result)
	 || !EncodeQueryTagsMode(tags, tagSpec, RSSYMany, kTagsAny, result)
	 || !EncodeQueryTagsMode(tags, tagSpec, RSSYMnone, kTagsNone, result))
		return NILREF;
	if (Length(result) == 0)
		Throw(exStoreError, (void*) kNSErrBadTagSpec, nil);
	return result;
}


// ROM 0x002d1228 TagsValidTest__FR10TSoupIndexRC6RefVarUl
// Whether the entry (by store object id) passes the encoded query tags:
// its bits from the tags index tested in every mode; an entry with no
// tags in the index passes only a lone 'none, or a lone 'equal of no
// tags.  A nil encoding (no entry can match) fails.
Boolean
TagsValidTest(TSoupIndex& tagsIndex, RefArg queryTags, PSSId id)
{
	if ((Ref) queryTags == NILREF)
		return false;
	SKey key;
	key.Clear();
	key = (long) id;
	TagsBits bits;
	bits.Clear();
	int result = tagsIndex.Find(&key, &key, &bits, true);
	if (result == kIndexOK)
	{
		RefVar binary;
		long count = Length(queryTags);
		for (long i = 0; i < count; i += 2)
		{
			binary = GetArraySlotRef(queryTags, i + 1);
			Long mode = RINT(GetArraySlotRef(queryTags, i));
			if (!bits.ValidTest(*(const TagsBits*) BinaryData(binary), mode))
				return false;
		}
		return true;
	}
	if (result == kIndexNotFound || result == kIndexEnd)
	{
		if (Length(queryTags) == 2)
		{
			Long mode = RINT(GetArraySlotRef(queryTags, 0));
			if (mode == kTagsNone)
				return true;
			if (mode == kTagsEqual)
				return ((const TagsBits*) BinaryData(GetArraySlotRef(queryTags, 1)))->Size() == 0;
		}
		return false;
	}
	Throw(exStoreError, (void*) kNSErrBadTagsIndex, nil);
	return false;
}


/*------------------------------------------------------------------------------
	T h e   t a g s   i n d e x
------------------------------------------------------------------------------*/

// ROM 0x0034e7f0 AlterTagsIndex__FUcR10TSoupIndexUlRC6RefVarN24
// The entry's tags (a symbol or a non-empty array) as bits added to (or
// deleted from) the tags index under the entry's store object id; a tag
// the soup does not know yet is added to it first.
void
AlterTagsIndex(Boolean add, TSoupIndex& tagsIndex, PSSId id, RefArg tagOrTags, RefArg soup, RefArg tags)
{
	if (!IsSymbol(tagOrTags))
	{
		if (!IsArray(tagOrTags) || Length(tagOrTags) < 1)
			return;
	}
	SKey key;
	key.Clear();
	key = (long) id;
	TagsBits bits;
	bits.Clear();
	if (!EncodeTags(tags, tagOrTags, &bits))
	{
		PlainSoupAddTags(soup, tagOrTags);
		EncodeTags(tags, tagOrTags, &bits);
	}
	ThrowTagsIndexError(add ? tagsIndex.Add(&key, &bits) : tagsIndex.Delete(&key, &bits));
}


// ROM 0x00348074 UpdateTagsIndex__FRC6RefVarN31Ul
// The tags index brought from oldEntry's tags to newEntry's (a new tag
// added to the soup); nothing when they encode the same.  ==> whether it
// changed.
Boolean
UpdateTagsIndex(RefArg soup, RefArg indexDesc, RefArg oldEntry, RefArg newEntry, PSSId id)
{
	RefVar path(GetFrameSlotRef(indexDesc, RSSYMpath));
	RefVar oldTags(GetEntryKey(oldEntry, path));
	RefVar newTags(GetEntryKey(newEntry, path));
	if ((Ref) oldTags == NILREF && (Ref) newTags == NILREF)
		return false;
	RefVar tags(GetFrameSlotRef(indexDesc, RSSYMtags));
	TagsBits oldBits;
	TagsBits newBits;
	oldBits.Clear();
	newBits.Clear();
	if ((Ref) oldTags != NILREF)
		EncodeTags(tags, oldTags, &oldBits);
	if ((Ref) newTags != NILREF && !EncodeTags(tags, newTags, &newBits))
	{
		PlainSoupAddTags(soup, newTags);
		EncodeTags(tags, newTags, &newBits);
	}
	if (oldBits.Equals(newBits))
		return false;
	TSoupIndex* index = GetSoupIndexObject(soup, (PSSId) RINT(GetFrameSlotRef(indexDesc, RSSYMindex)));
	SKey key;
	key.Clear();
	key = (long) id;
	int result = kIndexOK;
	if (oldBits.Size() != 0)
		result = index->Delete(&key, &oldBits);
	if (result == kIndexOK && newBits.Size() != 0)
		result = index->Add(&key, &newBits);
	ThrowTagsIndexError(result);
	return true;
}


// ROM 0x0034a104 CountTags__FRC6RefVar
// The tags in the array (nil slots are removed tags).
long
CountTags(RefArg tags)
{
	long count = 0;
	for (long i = Length(tags) - 1; i >= 0; i--)
		if (GetArraySlotRef(tags, i) != NILREF)
			count++;
	return count;
}


// ROM 0x0034a154 AddTag__FRC6RefVarT1
// The tag into the array (made internal): in the first nil slot, else
// appended.  ==> false when it is there already.
Boolean
AddTag(RefArg tags, RefArg tag)
{
	long freeSlot = -1;
	for (long i = Length(tags) - 1; i >= 0; i--)
	{
		Ref existing = GetArraySlotRef(tags, i);
		if (existing == NILREF)
			freeSlot = i;
		else if (EQRef(existing, tag))
			return false;
	}
	RefVar internal(EnsureInternal(tag));
	if (freeSlot < 0)
		AddArraySlot(tags, internal);
	else
		SetArraySlotRef(tags, freeSlot, internal);
	return true;
}


// ROM 0x0034a448 QueryEntriesWithTags__FRC6RefVarT1
// A cursor over the soup's entries with any of the tags.
Ref
QueryEntriesWithTags(RefArg soup, RefArg tagOrTags)
{
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMtype, RSSYMindex);
	RefVar tagSpec(AllocateFrame());
	SetFrameSlot(tagSpec, RSSYMany, tagOrTags);
	SetFrameSlot(spec, RSSYMtagspec, tagSpec);
	return SoupQuery(soup, spec);
}


/*------------------------------------------------------------------------------
	T h e   p l a i n   s o u p ' s   t a g   m e t h o d s
------------------------------------------------------------------------------*/

// The soup's tags index description; throws when it has none.
static Ref
TagsIndexDescOf(RefArg soupPersistent)
{
	RefVar indexDesc(GetTagsIndexDesc(soupPersistent));
	if ((Ref) indexDesc == NILREF)
		Throw(exStoreError, (void*) kNSErrNoTagsIndex, nil);
	return indexDesc;
}


// The soup's indexes changed: the persistent frame's time, flags and
// object written, the cursors told.
static void
TagsChanged(RefArg soup, RefArg soupPersistent)
{
	SetFrameSlot(soupPersistent, RSSYMindexesmodtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
	SoupChanged(soupPersistent, false);
	WriteFaultBlock(soupPersistent);
	EachSoupCursorDo(soup, kSoupCursorTagsChanged);
}


// ROM 0x0034a23c PlainSoupAddTags
// A tag or an array of them added to the soup's tags (at most kMaxTags).
Ref
PlainSoupAddTags(RefArg rcvr, RefArg tagOrTags)
{
	CheckWriteProtect(((TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMtstore))->Store());
	RefVar persistent(GetFrameSlotRef(rcvr, RSSYM_proto));
	RefVar indexDesc(TagsIndexDescOf(persistent));
	RefVar tags(GetFrameSlotRef(indexDesc, RSSYMtags));
	Boolean isArray = IsArray(tagOrTags);
	long count = isArray ? Length(tagOrTags) : 1;
	if (CountTags(tags) + count > kMaxTags)
		Throw(exStoreError, (void*) kNSErrTooManyTags, nil);
	Boolean added = false;
	if (!isArray)
		added = AddTag(tags, tagOrTags);
	else
	{
		RefVar tag;
		for (long i = 0; i < count; i++)
		{
			tag = GetArraySlotRef(tagOrTags, i);
			if (AddTag(tags, tag))
				added = true;
		}
	}
	if (added)
		TagsChanged(rcvr, persistent);
	return NILREF;
}


// ROM 0x0034a4e4 PlainSoupRemoveTags
// The tags taken off every entry that has any of them (the slot on the
// index's path: the tags removed from its array, the slot removed - or
// set to nil for a deeper path - when none are left) and out of the
// soup's tags (their slots left nil so the other tags keep their bits).
Ref
PlainSoupRemoveTags(RefArg rcvr, RefArg tags)
{
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMtstore);
	CheckWriteProtect(wrapper->Store());
	RefVar persistent(GetFrameSlotRef(rcvr, RSSYM_proto));
	RefVar indexDesc(TagsIndexDescOf(persistent));
	RefVar cursor(QueryEntriesWithTags(rcvr, tags));
	long count = Length(tags);
	RefVar path(GetFrameSlotRef(indexDesc, RSSYMpath));
	Boolean pathIsSymbol = IsSymbol(path);
	OSErrIf(wrapper->LockStore());
	newton_try
	{
		RefVar entry(CursorEntry(cursor));
		RefVar value;
		RefVar tag;
		while ((Ref) entry != NILREF)
		{
			value = GetFramePath(entry, path);
			Boolean noneLeft = true;
			if (IsArray(value))
			{
				for (long i = count - 1; i >= 0; i--)
				{
					tag = GetArraySlotRef(tags, i);
					ArrayRemove(value, tag);
				}
				noneLeft = Length(value) == 0;
			}
			if (noneLeft)
			{
				if (pathIsSymbol)
					RemoveSlot(entry, path);
				else
					SetFramePath(entry, path, RefVar(NILREF));
			}
			EntryChange(entry);
			entry = CursorEntry(cursor);			// the entry no longer matches: the cursor has moved on
		}
		RefVar soupTags(GetFrameSlotRef(indexDesc, RSSYMtags));
		RefVar none;
		for (long i = count - 1; i >= 0; i--)
		{
			tag = GetArraySlotRef(tags, i);
			Ref index = FSetContains(none, soupTags, tag);
			if (ISINT(index))
				SetArraySlotRef(soupTags, RVALUE(index), NILREF);
		}
		SetFrameSlot(persistent, RSSYMindexesmodtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
		SoupChanged(persistent, false);
		WriteFaultBlock(persistent);
	}
	newton_catch_all
	{
		OSErrIf(wrapper->Abort());
		rethrow;
	}
	end_try;
	OSErrIf(wrapper->UnlockStore());
	EachSoupCursorDo(rcvr, kSoupCursorTagsChanged);
	return NILREF;
}


// ROM 0x0034a8cc PlainSoupModifyTag
// The tag renamed: in every entry that has it (the slot on the path, or
// its place in the slot's array) and in the soup's tags, where it keeps
// its bit (the entries' index bits stay).  Nothing when the old tag is
// unknown; an error when the new one exists.
Ref
PlainSoupModifyTag(RefArg rcvr, RefArg oldTag, RefArg newTag)
{
	TStoreWrapper* wrapper = (TStoreWrapper*) GetFrameSlotRef(rcvr, RSSYMtstore);
	CheckWriteProtect(wrapper->Store());
	RefVar persistent(GetFrameSlotRef(rcvr, RSSYM_proto));
	RefVar indexDesc(TagsIndexDescOf(persistent));
	if (EQRef(oldTag, newTag))
		return NILREF;
	RefVar tags(GetFrameSlotRef(indexDesc, RSSYMtags));
	RefVar none;
	if (ISINT(FSetContains(none, tags, newTag)))
		Throw(exStoreError, (void*) kNSErrBadTagSpec, nil);
	Ref oldIndex = FSetContains(none, tags, oldTag);
	if (!ISINT(oldIndex))
		return NILREF;
	RefVar cursor(QueryEntriesWithTags(rcvr, oldTag));
	RefVar path(GetFrameSlotRef(indexDesc, RSSYMpath));
	OSErrIf(wrapper->LockStore());
	newton_try
	{
		RefVar entry(CursorEntry(cursor));
		RefVar value;
		while ((Ref) entry != NILREF)
		{
			value = GetFramePath(entry, path);
			if (!IsArray(value))
				SetFramePath(entry, path, newTag);
			else
				SetArraySlotRef(value, RINT(FSetContains(none, value, oldTag)), newTag);
			EntryChangeCommon(entry, kEntryChangeKeepUniqueID | kEntryChangeSetModTime);
			entry = CursorNext(cursor);
		}
		SetArraySlotRef(tags, RVALUE(oldIndex), newTag);
		SetFrameSlot(persistent, RSSYMindexesmodtime, RefVar(MAKEINT(RealClock() & 0x1fffffff)));
		SoupChanged(persistent, false);
		WriteFaultBlock(persistent);
	}
	newton_catch_all
	{
		OSErrIf(wrapper->Abort());
		rethrow;
	}
	end_try;
	OSErrIf(wrapper->UnlockStore());
	return NILREF;
}


// ROM 0x0034acd8 PlainSoupHasTags
Ref
PlainSoupHasTags(RefArg rcvr)
{
	RefVar persistent(GetFrameSlotRef(rcvr, RSSYM_proto));
	return MAKEBOOLEAN(GetTagsIndexDesc(persistent) != NILREF);
}


// ROM 0x0034b03c PlainSoupGetTags
// The soup's tags (the removed ones' nil slots left out); nil when it
// has no tags index.
Ref
PlainSoupGetTags(RefArg rcvr)
{
	RefVar result;
	RefVar persistent(GetFrameSlotRef(rcvr, RSSYM_proto));
	RefVar indexDesc(GetTagsIndexDesc(persistent));
	if ((Ref) indexDesc != NILREF)
	{
		RefVar tags(GetFrameSlotRef(indexDesc, RSSYMtags));
		long count = Length(tags);
		RefVar tag;
		result = AllocateArray(RSSYMarray, 0);
		for (long i = 0; i < count; i++)
		{
			tag = GetArraySlotRef(tags, i);
			if ((Ref) tag != NILREF)
				AddArraySlot(result, tag);
		}
	}
	return result;
}
