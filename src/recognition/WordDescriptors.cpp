/*
	File:		recognition/WordDescriptors.cpp

	Contains:	The cursive recogniser's word descriptors
				(WordDescriptors.h).

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "WordDescriptors.h"
#include "InkGroups.h"
#include "XrDomains.h"
#include "ParaGraph.h"
#include "CursiveReader.h"
#include <string.h>

static inline long	Index(GCWordDescrType* words, GCWordDescrType* word)	{ return word - words; }
static inline long	CountExtra(const UByte* extra)
{
	long n = 0;
	while (n < 8 && extra[n] != 0)
		n++;
	return n;
}


#pragma mark - the list

// ROM 0x000d7384 GCNewRecSegment__Fv
Handle
GCNewRecSegment(void)
{
	const ULong size = kWordDescrCount * sizeof(GCWordDescrType);
	Handle h = HWRMemoryAllocHandle(size);
	if (h != nil)
	{
		Ptr p = HWRMemoryLockHandle(h);
		if (p != nil)
		{
			memset(p, 0, size);
			HWRMemoryUnlockHandle(h);
			return h;
		}
		HWRMemoryFreeHandle(h);
	}
	return nil;
}


// ROM 0x000d73e4 GCGetFirstWordDescriptor__FP15GCWordDescrType
// The one with nothing before it.
GCWordDescrType*
GCGetFirstWordDescriptor(GCWordDescrType* words)
{
	if (words != nil)
	{
		long i = 0;
		for ( ; i < kWordDescrCount; i++)
			if (words[i].fPrev == kWordDescrNone)
				break;
		if (i != kWordDescrCount)
			return words + i;
	}
	return nil;
}


// ROM 0x000d7f78 GCGetLastWordDescriptor__FP15GCWordDescrType
GCWordDescrType*
GCGetLastWordDescriptor(GCWordDescrType* words)
{
	if (words != nil)
	{
		long i = 0;
		for ( ; i < kWordDescrCount; i++)
			if (words[i].fNext == kWordDescrNone)
				break;
		if (i != kWordDescrCount)
			return words + i;
	}
	return nil;
}


// ROM 0x000d84f8 GCGetNextWordDescriptor__FP15GCWordDescrTypeT1
GCWordDescrType*
GCGetNextWordDescriptor(GCWordDescrType* words, GCWordDescrType* word)
{
	if (words == nil || word == nil || word->fNext == kWordDescrNone)
		return nil;
	return words + word->fNext;
}


// ROM 0x000d8524 GCGetPrevWordDescriptor__FP15GCWordDescrTypeT1
GCWordDescrType*
GCGetPrevWordDescriptor(GCWordDescrType* words, GCWordDescrType* word)
{
	if (words == nil || word == nil || word->fPrev == kWordDescrNone)
		return nil;
	return words + word->fPrev;
}


// ROM 0x000d8550 GCGetWordDescriptorIndex__FP15GCWordDescrTypeT1
UShort
GCGetWordDescriptorIndex(GCWordDescrType* words, GCWordDescrType* word)
{
	if (words != nil && word != nil)
		return (UShort) Index(words, word);
	return kWordDescrNone;
}


// ROM 0x000d8588 GCNewWordDescriptor__FP15GCWordDescrType
// The first free slot (both links nought) put at the end of the list.
// ROM QUIRK: a descriptor whose word before is slot nought and which has
// slot nought after it would count as free - which the list never makes.
GCWordDescrType*
GCNewWordDescriptor(GCWordDescrType* words)
{
	if (words == nil)
		return nil;
	long i = 0;
	for ( ; i < kWordDescrCount; i++)
		if (words[i].fPrev == 0 && words[i].fNext == 0)
			break;
	if (i == kWordDescrCount)
		return nil;
	GCWordDescrType* last = GCGetLastWordDescriptor(words);
	if (last == nil)
	{
		words[i].fNext = kWordDescrNone;
		words[i].fPrev = kWordDescrNone;
	}
	else
	{
		UShort at = GCGetWordDescriptorIndex(words, last);
		last->fNext = (UShort) i;
		words[i].fPrev = at;
		words[i].fNext = kWordDescrNone;
	}
	return words + i;
}


// ROM 0x000d8650 GCWordDescriptorDispose__FP15GCWordDescrTypeT1
// Taken out of the list, its readings freed and the slot cleared.  (Its
// learning handle is the caller's to free: GCReleaseRecResults.)
long
GCWordDescriptorDispose(GCWordDescrType* words, GCWordDescrType* word)
{
	if (word == nil)
		return -1;
	if (word->fPrev != kWordDescrNone)
		words[word->fPrev].fNext = word->fNext;
	if (word->fNext != kWordDescrNone)
		words[word->fNext].fPrev = word->fPrev;
	if (word->fRecResults != nil)
		HWRMemoryFreeHandle(word->fRecResults);
	memset(word, 0, sizeof(GCWordDescrType));
	return 0;
}


// ROM 0x000d7438 GCGetWordDescWithFlags__FP15GCWordDescrTypeUlUi
GCWordDescrType*
GCGetWordDescWithFlags(GCWordDescrType* words, ULong flags, ULong exact)
{
	if (words == nil)
		return nil;
	for (GCWordDescrType* word = GCGetFirstWordDescriptor(words); word != nil; word = GCGetNextWordDescriptor(words, word))
	{
		if (exact != 0 ? word->fFlags == flags : (word->fFlags & flags) != 0)
			return word;
	}
	return nil;
}


// ROM 0x000d876c GCCountWordDescWithFlags__FP15GCWordDescrTypeUlUi
long
GCCountWordDescWithFlags(GCWordDescrType* words, ULong flags, ULong exact)
{
	short n = 0;
	if (words == nil)
		return 0;
	for (GCWordDescrType* word = GCGetFirstWordDescriptor(words); word != nil; word = GCGetNextWordDescriptor(words, word))
	{
		if (exact != 0 ? word->fFlags == flags : (word->fFlags & flags) != 0)
			n = (short) (n + 1);
	}
	return n;
}


// ROM 0x000d86f0 GCChangeFlagForFirstNWordDescr__FP15GCWordDescrTypeUlsUi
long
GCChangeFlagForFirstNWordDescr(GCWordDescrType* words, ULong flags, short n, ULong set)
{
	if (words == nil)
		return -1;
	GCWordDescrType* word = GCGetFirstWordDescriptor(words);
	for (long i = 0; word != nil && i < n; i++)
	{
		if (set == 0)
			word->fFlags &= ~flags;
		else
			word->fFlags |= flags;
		word = GCGetNextWordDescriptor(words, word);
	}
	return 0;
}


// ROM 0x000d74a8 GCSortWordDescByStrokesOrder__FP15GCWordDescrType
// The list put in the order of the words' first strokes, by swapping
// neighbours until nothing moves.
long
GCSortWordDescByStrokesOrder(GCWordDescrType* words)
{
	if (words == nil)
		return -1;
	Boolean sorted;
	do
	{
		sorted = true;
		GCWordDescrType* next = GCGetFirstWordDescriptor(words);
		if (next == nil)
			return 0;
		GCWordDescrType* word;
		while (word = next, (next = GCGetNextWordDescriptor(words, word)) != nil)
		{
			if (word != nil && next->fFirst < word->fFirst)
			{
				sorted = false;
				if (word->fPrev != kWordDescrNone)
					words[word->fPrev].fNext = (UShort) Index(words, next);
				if (next->fNext != kWordDescrNone)
					words[next->fNext].fPrev = (UShort) Index(words, word);
				UShort prev = word->fPrev;
				word->fPrev = word->fNext;
				word->fNext = next->fNext;
				next->fNext = next->fPrev;
				next->fPrev = prev;
			}
		}
	} while (!sorted);
	return 0;
}


// ROM 0x000d75d8 GCIsWordDescContainsStroke__FP15GCWordDescrTypeUc
Boolean
GCIsWordDescContainsStroke(GCWordDescrType* word, UByte stroke)
{
	if (word == nil)
		return false;
	for (ULong s = word->fFirst; (long) s <= (long) word->fLast; s++)
		if (stroke == s)
			return true;
	for (long i = 0; ; )
	{
		UByte s = word->fExtra[i];
		i++;
		if (s == 0 || 8 < i)
			return false;
		if (stroke == s)
			return true;
	}
}


#pragma mark - the segmenter's words

// ROM 0x000d7640 GCWordDescWriteGroupResults__FP15GCWordDescrTypeUcT2PUcsT5iN27UlP17ws_word_info_type
// A word the segmenter found, written into the descriptors.  A
// descriptor with exactly the word's strokes is kept (its info renewed
// unless it has been read); one holding some of them is thrown away
// (the list is walked again from the start); otherwise a new one is
// made - joined to the one before it when that ended in a dash.  Its
// flags then take the bits 0x186 from `flags`.  ==> 0, -4 for no room.
long
GCWordDescWriteGroupResults(GCWordDescrType* words, UByte first, UByte last, UByte* extra, short joinX, short joinY, long lineHeight, long baseLine, long newLine, ULong flags, ws_word_info_type* info)
{
	UByte none[4];
	if (words == nil)
		return -1;
	if (extra == nil)
	{
		extra = none;
		none[0] = 0;
	}
	GCWordDescrType* word = GCGetFirstWordDescriptor(words);
	while (word != nil)
	{
		// how many of the new word's strokes the descriptor has
		ULong found = 0;
		long newAt = 0;
		ULong n = first;
		ULong stroke = first;
		do
		{
			long wordAt = 0;
			ULong d = word->fFirst;
			ULong ds = word->fFirst;
			Boolean missing = false;
			while (stroke != ds)
			{
				d++;
				if ((long) word->fLast < (long) d)
				{
					if (wordAt < 8)
						ds = word->fExtra[wordAt];
					wordAt++;
				}
				else
					ds = d & 0xff;
				if ((long) word->fLast < (long) d && (ds == 0 || 8 < wordAt))
				{
					missing = true;
					break;
				}
			}
			if (!missing)
				found++;
			n++;
			if ((long) last < (long) n)
			{
				if (newAt < 8)
					stroke = extra[newAt];
				newAt++;
			}
			else
				stroke = n & 0xff;
		} while ((long) n <= (long) last || (stroke != 0 && newAt < 9));
		long extras = CountExtra(word->fExtra);
		Boolean same = found == (ULong) ((word->fLast + extras - word->fFirst + 1) - word->fMerged);
		if (same)
			n -= first;
		if (same && n == found)
		{
			if (info != nil && (word->fFlags & 0xf70) == 0)
				word->fInfo = *info;
			word->fFlags = (word->fFlags & 0xfffffe79) | flags;
			return 0;
		}
		if (found == 0)
			word = GCGetNextWordDescriptor(words, word);
		else
		{
			GCWordDescriptorDispose(words, word);
			word = GCGetFirstWordDescriptor(words);
		}
	}

	GCWordDescrType* made = GCNewWordDescriptor(words);
	if (made == nil)
		return -4;
	made->fFirst = first;
	made->fLast = last;
	// ROM BUG: the extra strokes are copied while the index is less than
	// the stroke number, rather than while the stroke number is not
	// nought - which is the same unless a stroke numbered no more than
	// its place in the list comes first
	for (long i = 0; i < (long) extra[i] && i < 8; i++)
		made->fExtra[i] = extra[i];
	if (info != nil)
		made->fInfo = *info;
	GCWordDescrType* dashed = GCGetWordDescWithFlags(words, 0x80, 0);
	word = made;
	if ((flags & 0x80) != 0 || dashed != nil)
	{
		made->fJoinX = joinX;
		made->fJoinY = joinY;
		if (dashed != nil)
		{
			word = GCMergeWordDesc(words, dashed, made);
			if (word == nil)
			{
				dashed->fJoinX = 0;
				dashed->fJoinY = 0;
				dashed->fMerged = 0;
				dashed->fFlags = (dashed->fFlags & 0xffffff7d) | 4;
				made->fJoinX = 0;
				made->fJoinY = 0;
				made->fMerged = 0;
				word = made;
			}
			else
			{
				if (word == dashed)
					dashed = made;
				GCWordDescriptorDispose(words, dashed);
			}
		}
	}
	word->fLineHeight = (short) lineHeight;
	word->fBaseLine = (short) baseLine;
	word->fNewLine = (short) newLine;
	word->fFlags = (word->fFlags & 0xfffffe79) | flags;
	return 0;
}


// ROM 0x000d798c GCMergeWordDesc__FP15GCWordDescrTypeN21
// Two words joined into the one whose strokes come first: the other's
// strokes added to it (a run continued when the two are neighbours and
// neither has extra strokes), where the two lines met made relative,
// and the other's info appended with its strokes renumbered.  ==> the
// joined word; nil when the strokes do not fit.
GCWordDescrType*
GCMergeWordDesc(GCWordDescrType* words, GCWordDescrType* a, GCWordDescrType* b)
{
	if (words == nil || a == nil || b == nil)
		return nil;
	GCWordDescrType* lo = a;
	GCWordDescrType* hi = b;
	if (b->fFirst < a->fFirst)
	{
		lo = b;
		hi = a;
	}
	long loExtras = CountExtra(lo->fExtra);
	UByte loLast = lo->fLast;
	UByte loFirst = lo->fFirst;
	ULong s = hi->fFirst;
	Boolean joined = false;
	if (s - loLast == 1)
	{
		UByte hiExtra = 0;
		if (lo->fExtra[0] != 0)
			hiExtra = hi->fExtra[0];
		if (lo->fExtra[0] == 0 || hiExtra == 0)
		{
			lo->fLast = hi->fLast;
			for (long i = 0; i < 8 && hi->fExtra[i] != 0; i++)
				lo->fExtra[i] = hi->fExtra[i];
			joined = true;
		}
	}
	if (!joined)
	{
		long nLo = CountExtra(lo->fExtra);
		long nHi = CountExtra(hi->fExtra);
		if ((long) ((hi->fLast - s) + nHi + 1) > 8 - nLo)
			return nil;
		for ( ; (long) s <= (long) hi->fLast; s++, nLo++)
			lo->fExtra[nLo] = (UByte) s;
		for (long i = 0; i < 8 && hi->fExtra[i] != 0; i++, nLo++)
			lo->fExtra[nLo] = hi->fExtra[i];
	}
	lo->fJoinX = (short) ((UShort) lo->fJoinX - (UShort) hi->fJoinX);
	lo->fJoinY = (short) ((UShort) lo->fJoinY - (UShort) hi->fJoinY);
	UByte merged = (UByte) ((loLast + loExtras) - loFirst + 1);
	lo->fMerged = merged;
	long n = 0;
	while (n < 8 && lo->fInfo.fStrokes[n] != 0)
		n++;
	for (long j = 0; ; j++, n++)
	{
		if (7 < (n < 8 ? j : n))
			break;
		if (hi->fInfo.fStrokes[j] == 0)
			return lo;
		lo->fInfo.fStrokes[n] = (UByte) (hi->fInfo.fStrokes[j] + merged);
		lo->fInfo.fSure[n] = hi->fInfo.fSure[j];
	}
	return lo;
}


// ROM 0x000d7bbc GCRecSegmentSetGroupFlags__FP15GCWordDescrTypesUi
// Which words are to be read now: all of them at the end, otherwise the
// settled ones but the last `lineAtATime` (seven at most), which may
// yet change.
long
GCRecSegmentSetGroupFlags(GCWordDescrType* words, short lineAtATime, ULong final)
{
	long n = lineAtATime;
	if (words == nil)
		return -1;
	if (final == 0)
	{
		long settled = GCCountWordDescWithFlags(words, 2, 0);
		if (7 < n)
			n = 7;
		n = (short) (settled - n);
		if (n < 1)
			return 0;
	}
	else
		n = 8;
	GCChangeFlagForFirstNWordDescr(words, final == 0 ? 2 : 0x82, (short) n, 0);
	GCChangeFlagForFirstNWordDescr(words, 4, (short) n, 1);
	return 0;
}


// ROM 0x000d5ff0 GCWriteNewGroupResults__FP15GCWordDescrTypeP15ws_results_typeP17GCGroupParmStructUi
// Each word the segmenter has found (the words of the last line it
// segmented, and none after the first empty slot) cut into runs of the
// strokes the parameters list; each run written into the descriptors,
// settled (2) unless it closes the writing (4), waiting for a dash's
// continuation (0x82) when the segmenter says a dash may end it, and
// marked not contiguous (0x100) when it is not all of its word.
long
GCWriteNewGroupResults(GCWordDescrType* words, ws_results_type* results, GCGroupParmStruct* parm, ULong final)
{
	if (words == nil || results == nil || parm == nil)
		return -1;
	ws_word_type* segWords = results->fWords;
	Boolean started = false;
	long count = results->fNumWords;
	if (parm->fNumStrokes <= (long) results->fNumWords)
		count = parm->fNumStrokes;
	for (long w = 0; w < count; w++)
	{
		ws_word_type* segWord = segWords + w;
		if (segWord->fWord == 0)
		{
			if (started)
				break;
			continue;
		}
		started = true;
		if ((segWord->fFlags & 4) != 0)
			continue;
		ULong strokeCount = segWord->fCount;
		const UByte* list = results->fStrokes + segWord->fFirst;
		long lineHeight, baseLine, newLine;
		if (GetWSBorder(w, results, &lineHeight, &baseLine, &newLine) != 0)
		{
			lineHeight = 0;
			baseLine = 0;
			newLine = 0;
		}
		ws_word_info_type info;
		memset(&info, 0, sizeof(info));
		SetStrokeSureValuesWS(0, w, results, &info);
		Boolean broken = false;
		long end = 0;
		long at = 0;
		if (strokeCount == 0)
			continue;
		do
		{
			// the next run of listed strokes
			while (at < (long) strokeCount && (parm->fStrokes[list[at] >> 3] >> (7 - (list[at] & 7)) & 1) == 0)
				at++;
			if ((long) strokeCount <= at)
				break;
			if (end != at)
				broken = true;
			end = at + 1;
			if (end < (long) strokeCount)
			{
				for (;;)
				{
					UByte s = list[end];
					if ((parm->fStrokes[s >> 3] >> (7 - (s & 7)) & 1) == 0)
					{
						broken = true;
						break;
					}
					if (1 < (long) s - (long) list[end - 1])
						break;
					end++;
					if ((long) strokeCount <= end)
						break;
				}
			}
			long runFirst = (short) list[at];
			long runLast = (short) list[end - 1];
			// DEVIATION: cleared - the ROM leaves the stack's bytes where
			// a stroke the parameters do not list is passed over (k still
			// counts it), so what the word then has there is whatever was
			// on the stack
			UByte more[12];
			memset(more, 0, sizeof(more));
			long k = 0;
			long runStart = at;
			// the rest of the word's listed strokes, as extras
			while (end < (long) strokeCount && k < 8)
			{
				UByte s = list[end];
				if ((parm->fStrokes[s >> 3] >> (7 - (s & 7)) & 1) == 0)
					broken = true;
				else
					more[k] = s;
				k++;
				end++;
			}
			more[k] = 0;
			ULong flags = ((segWord->fFlags & 1) == 0 && final == 0) ? 2 : 4;
			short joinX;
			if ((segWord->fFlags & 8) == 0
			 || (final != 0 && count - 1 <= w)
			 || GCGetWordDescWithFlags(words, 0x80, 0) != nil)
				joinX = segWord->fLeft;
			else
			{
				flags = 0x82;
				joinX = segWord->fRight;
			}
			if (runStart != 0 || broken)
				flags = (flags & ~0x80) | 0x100;
			long failed = GCWordDescWriteGroupResults(words, (UByte) runFirst, (UByte) runLast, more, joinX, segWord->fLineY, lineHeight, baseLine, newLine, flags, &info);
			if (failed != 0)
				return failed;
			at = end;
		} while (end < (long) strokeCount);
	}
	return 0;
}


// ROM 0x000d5ba4 GCGroupResultsCopyFlags__FP15ws_results_typeP15GCWordDescrTypes
long
GCGroupResultsCopyFlags(ws_results_type* results, GCWordDescrType* words, short numStrokes)
{
	if (results == nil || words == nil)
		return -1;
	ws_word_type* segWords = results->fWords;
	Boolean started = false;
	for (long w = 0; ; w++)
	{
		long end = w < (long) results->fNumWords ? numStrokes : results->fNumWords;
		if (end <= w)
			break;
		ws_word_type* segWord = segWords + w;
		if (segWord->fWord == 0)
		{
			if (started)
				break;
			continue;
		}
		started = true;
		for (long i = 0; (segWord->fFlags & 4) == 0 && i < (long) segWord->fCount; i++)
		{
			for (GCWordDescrType* word = GCGetFirstWordDescriptor(words); word != nil; word = GCGetNextWordDescriptor(words, word))
			{
				if ((word->fFlags & 0x184) != 0
				 && GCIsWordDescContainsStroke(word, results->fStrokes[segWord->fFirst + i]))
				{
					segWord->fFlags |= 4;
					break;
				}
			}
		}
	}
	return 0;
}


#pragma mark - reading a word

// ROM 0x000d7c50 GCWDWriteRecResults__FP15GCWordDescrTypeP7rc_typeP10rec_w_typeUliP20RecwordSplitInfoTypeUi
// What reading the word came to: -9 the xr reader failed (0x400), -8
// the low level failed (0x200), nought the readings (up to ten, those
// with a word) copied into a handle with the split information and the
// base line the engine found, anything else (or no memory) 0x800.
long
GCWDWriteRecResults(GCWordDescrType* word, rc_type* rc, rec_w_type* readings, Handle learning, long err, void* splitInfo, ULong alternatives)
{
	if (word == nil)
		return -1;
	if (err == -9)
	{
		word->fFlags |= 0x400;
		return 0;
	}
	if (err == -8)
	{
		word->fFlags |= 0x200;
		return 0;
	}
	if (err == 0)
	{
		word->fField12 = (short) RCGetH(rc, 0xac);
		word->fBase[0] = (short) RCGetH(rc, 0xea);
		word->fBase[1] = (short) RCGetH(rc, 0xec);
		word->fBase[2] = (short) RCGetH(rc, 0xee);
		word->fBase[3] = (short) RCGetH(rc, 0xf0);
		memcpy(word->fInkBox, RCByte(rc, 0xd8), 8);
		word->fLearning = learning;
		long count = 0;
		for ( ; count < 10; count++)
			if (((UByte*) &readings[count])[0] == 0)
				break;
		long splitSize = 0;
		UByte* split = (UByte*) splitInfo;
		if (split != nil && 1 < split[0xf])
		{
			for (ULong i = 0; i < 0xc; i++)
				splitSize += split[0x4c + i];
			splitSize += 0x5b;
		}
		// DEVIATION: the header sized from sizeof (the ROM's is 0xc bytes)
		word->fRecResults = HWRMemoryAllocHandle(splitSize + count * sizeof(rec_w_type) + sizeof(GCRecResults));
		if (word->fRecResults != nil)
		{
			GCRecResults* header = (GCRecResults*) HWRMemoryLockHandle(word->fRecResults);
			if (header != nil)
			{
				header->fCount = (short) count;
				header->fSplitSize = (short) splitSize;
				header->fWords = nil;
				header->fSplit = nil;
				HWRMemoryUnlockHandle(word->fRecResults);
				header = GCLockRecResultsHandle(word->fRecResults);
				if (header != nil)
				{
					if (header->fSplit != nil)
						memcpy(header->fSplit, splitInfo, header->fSplitSize);
					memcpy(header->fWords, readings, count * sizeof(rec_w_type));
					GCUnlockRecResultsHandle(word->fRecResults);
					word->fFlags |= alternatives == 0 ? 0x50 : 0x28;
					return 0;
				}
			}
			HWRMemoryFreeHandle(word->fRecResults);
			word->fRecResults = nil;
		}
	}
	word->fFlags |= 0x800;
	return 0;
}


// ROM 0x000d7e40 GCWDWritePrevBaseLine__FsN31P15GCWordDescrType
long
GCWDWritePrevBaseLine(short a, short b, short c, short d, GCWordDescrType* word)
{
	if (word == nil)
		return -1;
	word->fPrevBase[0] = a;
	word->fPrevBase[1] = b;
	word->fPrevBase[2] = c;
	word->fPrevBase[3] = d;
	return 0;
}


// ROM 0x000d7ea8 GCWDFillBaseLineParameters__FP15GCWordDescrTypeP7rc_typeP13PS_point_type
long
GCWDFillBaseLineParameters(GCWordDescrType* word, rc_type* rc, PS_point_type* trace)
{
	if (word != nil && rc != nil && trace != nil
	 && GCFillBaseLineParameters(word->fLineHeight, word->fBaseLine, word->fNewLine,
			word->fPrevBase[0], word->fPrevBase[1], word->fPrevBase[2], word->fPrevBase[3], rc, trace) == 0)
		return 0;
	return -1;
}


// ROM 0x000d7f24 GCLockRecResultsHandle__FUl
// Locked, and its two pointers pointed at the readings and the split
// information (nil for none).
GCRecResults*
GCLockRecResultsHandle(Handle results)
{
	if (results == nil)
		return nil;
	GCRecResults* header = (GCRecResults*) HWRMemoryLockHandle(results);
	if (header != nil)
	{
		header->fWords = (rec_w_type*) (header + 1);
		header->fSplit = (UByte*) (header + 1) + header->fCount * sizeof(rec_w_type);
		if (header->fSplitSize == 0)
			header->fSplit = nil;
	}
	return header;
}


// ROM 0x000d7fcc GCUnlockRecResultsHandle__FUl
long
GCUnlockRecResultsHandle(Handle results)
{
	if (results == nil)
		return 0;
	HUnlock(results);
	return 1;
}


// ROM 0x000d7fd8 GCWDGetTrace__FP13PS_point_typeP15GCWordDescrTypePUcPP13PS_point_typePsPUi
// The trace a word is read from.  A run of strokes is simply the part of
// the unit's trace it takes up (`*copied` nought); with extra strokes,
// or joined after a dash, a copy is made with each extra stroke's
// points put after the run's (the pen-up between them shared), and a
// joined word's second line moved up to the end of the first
// (GCMergeLinesAndRemoveDash) - the dash's stroke then taken out of the
// word's info, the later strokes' numbers moved down.  ==> 0, -1 for a
// stroke not in the trace, -2 for no memory.
long
GCWDGetTrace(PS_point_type* trace, GCWordDescrType* word, UByte* strokes, PS_point_type** wordTrace, short* nPoints, ULong* copied)
{
	PS_point_type* firstStroke;
	PS_point_type* lastStroke;
	PS_point_type* stroke;
	short count = 0;
	long err;
	long n = 0;
	long extra = 0;
	PS_point_type* at = nil;
	*copied = 0;
	*wordTrace = nil;
	*nPoints = 0;
	if (word == nil || trace == nil || strokes == nil)
		goto failed;
	GCGetStrokeFromTrace(trace, 0x7fff, (short) GCGetRealStrokeIndex(strokes, word->fFirst), &firstStroke, nil);
	GCGetStrokeFromTrace(trace, 0x7fff, (short) GCGetRealStrokeIndex(strokes, word->fLast), &lastStroke, &count);
	if (firstStroke == nil || lastStroke == nil)
		goto failed;
	n = (short) (count + (lastStroke - firstStroke));
	*nPoints = (short) n;
	for (long pass = 0; pass < 2; pass++)
	{
		if (pass != 0)
		{
			if (*copied != 0)
			{
				*wordTrace = (PS_point_type*) HWRMemoryAlloc(*nPoints * sizeof(PS_point_type));
				if (*wordTrace == nil)
				{
					err = -2;
					goto freed;
				}
				memcpy(*wordTrace, firstStroke, n * sizeof(PS_point_type));
			}
			else
				*wordTrace = firstStroke;
			at = *wordTrace + n - 1;
		}
		for (extra = 0; word->fExtra[extra] != 0 && extra < 8; extra++)
		{
			GCGetStrokeFromTrace(trace, 0x7fff, (short) GCGetRealStrokeIndex(strokes, word->fExtra[extra]), &stroke, &count);
			if (stroke == nil)
				goto failed;
			if (pass == 0)
			{
				*nPoints = (short) ((UShort) *nPoints + count - 1);
				*copied = 1;
			}
			else
			{
				memcpy(at, stroke, count * sizeof(PS_point_type));
				at = at + count - 1;
			}
		}
		if (word->fMerged != 0 && pass == 0)
			*copied = 1;
	}
	if (word->fMerged != 0)
	{
		long dash = GCMergeLinesAndRemoveDash(*wordTrace, nPoints, word->fJoinX, word->fJoinY, word->fMerged, extra);
		if (dash >= 0)
		{
			long k = (short) (dash + 1);
			Boolean removed = false;
			for (long i = 0; i < 8; i++)
			{
				UByte s = word->fInfo.fStrokes[i];
				if (s == 0)
					break;
				if (s == k)
				{
					word->fInfo.fStrokes[i] = 0;
					word->fInfo.fSure[i] = 0;
					removed = true;
				}
				else
				{
					// ROM BUG: an entry is moved down one, but it is the
					// one left behind whose number is lowered, so the
					// moved copy keeps its old number and the list keeps
					// a stale last entry
					if (removed)
					{
						word->fInfo.fStrokes[i - 1] = s;
						word->fInfo.fSure[i - 1] = word->fInfo.fSure[i];
					}
					if ((long) s > k)
						word->fInfo.fStrokes[i] = (UByte) (s - 1);
				}
			}
		}
	}
	return 0;

failed:
	err = -1;
freed:
	if (*copied != 0 && *wordTrace != nil)
		HWRMemoryFree((Ptr) *wordTrace);
	return err;
}


// ROM 0x000d8300 GCWDRemoveStrokesFromList__FP15GCWordDescrTypePUc
long
GCWDRemoveStrokesFromList(GCWordDescrType* word, UByte* strokes)
{
	if (word == nil || strokes == nil)
		return -1;
	for (ULong s = word->fFirst; (long) s <= (long) word->fLast; s++)
		strokes[s >> 3] &= (UByte) ~(1 << (7 - (s & 7)));
	// ROM BUG: as in GCWordDescWriteGroupResults, the extra strokes are
	// walked while the index is less than the stroke number
	for (long i = 0; i < 8 && i < (long) word->fExtra[i]; i++)
	{
		UByte s = word->fExtra[i];
		strokes[s >> 3] &= (UByte) ~(1 << (7 - (s & 7)));
	}
	return 0;
}


// ROM 0x000d6d24 GCMergeLinesAndRemoveDash__FP13PS_point_typePssT3Uc
// Of the first `merged` strokes, the one reaching furthest right is the
// dash: its points are taken out, and every point of the strokes after
// the first part moved by (dx, dy).  ROM QUIRK: when no stroke reaches
// right of nought the answer is whatever the caller had in r8 -
// GCWDGetTrace's count of extra strokes, which it passes as `callerR8`.
long
GCMergeLinesAndRemoveDash(PS_point_type* trace, short* nPoints, short dx, short dy, UByte merged, long callerR8)
{
	if (trace == nil || nPoints == nil || merged < 1)
		return -1;
	long n = *nPoints;
	if (n < 3)
		return -1;
	long lastUp = 0, maxX = 0, dashStart = 0, dashLen = 0, seen = 0, i = 1;
	long dash = callerR8;
	for ( ; seen < (long) merged && i < n; i++)
	{
		if (trace[i].y < 0)
		{
			if (dashStart == lastUp)
				dashLen = i - dashStart;
			lastUp = i;
			seen++;
		}
		else if (trace[i].x > maxX)
		{
			maxX = trace[i].x;
			dashStart = lastUp;
			dash = (short) seen;
		}
	}
	i -= dashLen;
	*nPoints = (short) (n - dashLen);
	for (long j = dashStart + 1; j < *nPoints; j++)
	{
		PS_point_type* from = trace + j + dashLen;
		if (j >= i && from->y >= 0)
		{
			trace[j].x = (short) ((UShort) from->x + dx);
			trace[j].y = (short) ((UShort) from->y + dy);
		}
		else
			trace[j] = *from;
	}
	return dash;
}


#pragma mark - the segmenter's answers

// ROM 0x0019f464 GetWSBorder__FiP15ws_results_typePiN23
long
GetWSBorder(long word, ws_results_type* results, long* lineHeight, long* baseLine, long* newLine)
{
	if (results != nil && word < (long) results->fNumWords)
	{
		ws_word_type* w = results->fWords + word;
		*lineHeight = w->fLineHeight;
		*baseLine = w->fLineY + w->fLineHeight / 2;
		if ((w->fFlags & 0x20) != 0 && 0 < word)
		{
			ws_word_type* before = w - 1;
			if (1 < word && ((w - 2)->fFlags & 8) != 0)
				before = w - 2;
			*newLine = w->fLine != before->fLine;
			return 0;
		}
	}
	*newLine = 1;
	return 1;
}


// ROM 0x0019f15c SetStrokeSureValuesWS__FiT1P15ws_results_typeP17ws_word_info_type
// The word's gaps, least sure first, added until the info is full or
// every gap is in it.  ROM QUIRK: a gap already there is recognised by
// its number less one, which is right only when numbering from one.
long
SetStrokeSureValuesWS(long fromZero, long word, ws_results_type* results, ws_word_info_type* info)
{
	ws_word_type* w = results->fWords + word;
	long n = 0;
	while (info->fStrokes[n] != 0 && n < 8)
		n++;
	if (8 <= n)
		return 1;
	long rounds = 0;
	if (w->fCount != 0)
	{
		for (;;)
		{
			long best = 100;
			long bestAt = 0;
			if ((long) (w->fCount - 1) < 1)
				break;
			for (long i = 0; i < (long) (w->fCount - 1); i++)
			{
				Boolean there = false;
				for (long j = 0; j < n; j++)
					if ((long) info->fStrokes[j] - 1 == i)
					{
						there = true;
						break;
					}
				if (there)
					continue;
				long v = HWRAbs(results->fSure[w->fFirst + i]);
				if (v < best)
				{
					bestAt = i + 1;
					best = v;
				}
			}
			if (best == 100)
				break;
			info->fStrokes[n] = (UByte) (bestAt - (fromZero != 0));
			info->fSure[n] = results->fSure[w->fFirst + bestAt - 1];
			n++;
			if (7 < n)
				break;
			rounds++;
			if ((long) w->fCount <= rounds)
				break;
		}
	}
	return 0;
}
