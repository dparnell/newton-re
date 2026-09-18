/*
	File:		stores/Tags.h

	Contains:	Soup tags: a soup's tags index (an index description of
				type 'tags with a tags array: the tag symbols, each tag's
				place in it its bit) maps every entry's store object id to
				its TagsBits, a bitmap of the tags the entry's slot on the
				index's path holds (one symbol or an array of them).  A
				query's tagSpec {equal, all, any, none} is encoded against
				each soup's tags as [mode, bits binary] pairs and tested
				against the entry's bits from the index (TagsValidTest) -
				without reading the entry.

	Reconstructed from the MP2x00 US ROM (0x002d0b04-0x002d1228,
	0x00348074, 0x0034a104-0x0034b03c, 0x0034e7f0).
*/

#ifndef __TAGS_H
#define __TAGS_H

#ifndef __SOUPINDEX_H
#include "SoupIndex.h"
#endif

// the tagSpec's modes, in the encoded pairs
enum
{
	kTagsEqual = 0,		// exactly these tags
	kTagsAll,			// all of these (and maybe more)
	kTagsAny,			// at least one of these
	kTagsNone			// none of these
};

const long kMaxTags = 624;		// a tags index holds at most this many tags (0x270)

// An entry's tags as bits: an SKey whose data is the bitmap, bit n for the
// nth tag of the index's tags array, only as many bytes as the highest tag
// set needs.
struct TagsBits : public SKey
{
	void		SetTag(short tag);
	Boolean		ValidTest(const TagsBits& query, long mode) const;
};

Boolean	EncodeTags(RefArg tags, RefArg tagOrTags, TagsBits* outBits);	// ==> every tag is in the tags array
Ref		EncodeQueryTags(RefArg indexDesc, RefArg tagSpec);			// [mode, bits]...; nil: no entry can match
Boolean	TagsValidTest(TSoupIndex& tagsIndex, RefArg queryTags, PSSId id);
void	AlterTagsIndex(Boolean add, TSoupIndex& tagsIndex, PSSId id, RefArg tagOrTags, RefArg soup, RefArg tags);
Boolean	UpdateTagsIndex(RefArg soup, RefArg indexDesc, RefArg oldEntry, RefArg newEntry, PSSId id);
long	CountTags(RefArg tags);
Boolean	AddTag(RefArg tags, RefArg tag);
Ref		QueryEntriesWithTags(RefArg soup, RefArg tagOrTags);

// the plain soup's tag methods
Ref		PlainSoupAddTags(RefArg rcvr, RefArg tagOrTags);
Ref		PlainSoupRemoveTags(RefArg rcvr, RefArg tags);
Ref		PlainSoupModifyTag(RefArg rcvr, RefArg oldTag, RefArg newTag);
Ref		PlainSoupHasTags(RefArg rcvr);
Ref		PlainSoupGetTags(RefArg rcvr);

// the union soup's (UnionSoups.cpp)
Ref		UnionSoupAddTags(RefArg rcvr, RefArg tagOrTags);
Ref		UnionSoupRemoveTags(RefArg rcvr, RefArg tags);
Ref		UnionSoupModifyTag(RefArg rcvr, RefArg oldTag, RefArg newTag);
Ref		UnionSoupHasTags(RefArg rcvr);
Ref		UnionSoupGetTags(RefArg rcvr);

#endif	/* __TAGS_H */
