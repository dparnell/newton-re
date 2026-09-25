/*
	File:		recognition/RosEngine.cpp

	Contains:	The engine's other layers - see RosEngine.h.

	Two of them are real: `CharInitialize`/`RSfRcl`, which make and
	give back the common info out of the ROM's own template, and
	`BiGrammarsLoad`, which answers the ROM's own bigram grammar.  The
	rest are NOT YET; the destroyers do nothing, which is safe because
	nothing makes the objects they would give back.

	The work below this file, in the order it wants doing, is in
	`docs/recognition/README.md` under "The Rosetta engine".
*/

#include "RosEngine.h"
#include "RosStrokes.h"
#include "NewtonMemory.h"
#include "FixedMath.h"
#include "Segment.h"
#include "WordRecog.h"

#include <string.h>
#include <stdio.h>


// ROM 0x0c100b08 RosCI
RosCommonInfo*	RosCI = nil;


// ROM 0x00057074 CharInitialize
// The common info made: the ROM's template copied into a block of its
// own, because an area may put a character set of its own in it.  What
// comes back is how many output nodes the classifier has, which is the
// one thing the caller wants and which the ROM simply writes down.
long
CharInitialize(long /*unused*/)
{
	RosCI = (RosCommonInfo*) RosAllocate((long) sizeof(RosCommonInfo));
	*RosCI = rosCI;
	return 0x86;
}


// ROM 0x001b8334 RSfRcl
// ... and given back.  A character set the engine made for an area goes
// first, which is what the test against the ROM's own is for.
void
RSfRcl(void)
{
	if (RosCI->fLegalUse != rosCharLegalUse)
	{
		DisposPtr((Ptr) RosCI->fLegalUse);
		RosCI->fLegalUse = rosCharLegalUse;
	}
	DisposPtr((Ptr) RosCI);
	RosCI = nil;
}


Boolean
RosEngineLayersAreReconstructed(void)
{
	return false;
}


// ROM 0x0003e0b4 BiGrammarsLoad
// The ROM's is one instruction: it answers `ROMGrammar` and never
// looks at what it is asked for.  Ours does the same when it is asked
// for the ROM's (`kROMGrammars`, the constant the engine passes) and
// otherwise answers what it is handed, so a caller may bring a grammar
// of its own.
const BiGrammars*
BiGrammarsLoad(const BiGrammars* source)
{
	if (source == kROMGrammars)
		return &ROMGrammar;
	return source;
}


// ROM 0x0003de8c BiGrammarNew
// A grammar with room for `capacity` kinds of word.  The array of slice
// pointers lives **behind the struct in the same block**, which is why
// `fSlices` points at the byte after it and why the whole thing is one
// `DisposPtr`.  Nothing else in the engine allocates that way.
BiGrammar*
BiGrammarNew(short capacity)
{
	// DEVIATION: the ROM asks for `4 * capacity + 0x20` - its header is
	// 0x20 bytes and its pointers four - and points `fSlices` at the
	// byte after the header.  A host pointer is eight, so the size and
	// the place are worked out from the struct instead.
	BiGrammar* grammar = (BiGrammar*) RosAllocate(
					(long) (sizeof(BiGrammar) + capacity * sizeof(BiGSlice*)));
	grammar->fName = nil;
	grammar->fField04 = 0;
	grammar->fCount = 0;
	grammar->fCapacity = capacity;
	grammar->fSlices = (const BiGSlice* const*) (grammar + 1);
	// the ROM clears the eleven bytes from 0x14 to 0x1e one at a time;
	// here they are the fields they belong to
	grammar->fField14 = 0;
	grammar->fField15 = 0;
	grammar->fField16 = 0;
	grammar->fField17 = 0;
	grammar->fField18 = 0;
	grammar->fField1c = 0;
	return grammar;
}


// ROM 0x0003df2c BiGrammarCreate
// The same, taking a name - which it accepts and never stores: the
// register holding it is overwritten with the capacity before the call
// and `fName` is set to nil afterwards.  So a grammar the engine builds
// for a field has no name, and `BiGrammarClone` does not copy one
// either.  Nothing reads it, so nothing notices.
BiGrammar*
BiGrammarCreate(const char* /*name*/, short capacity)
{
	BiGrammar* grammar = BiGrammarNew(capacity);
	grammar->fName = nil;
	return grammar;
}


// ROM 0x0003dfa8 BiGSliceDestroy
void
BiGSliceDestroy(const BiGSlice* slice)
{
	DisposPtr((Ptr) slice);
}


// ROM 0x0003df4c BiGrammarDestroy
// Its slices and then itself.  Only ever called on a grammar the engine
// cloned for a field: `RosettaSleep` checks `fContextIndex < 0` first,
// and the ROM's own eight are in ROM.
void
BiGrammarDestroy(const BiGrammar* grammar)
{
	if (grammar == nil)
		return;
	if (grammar->fSlices != nil)
		for (long i = 0; i < grammar->fCount; i++)
			BiGSliceDestroy(grammar->fSlices[i]);
	DisposPtr((Ptr) grammar);
}


// ROM 0x0003dfb4 BiGSliceNew
// One kind of word, with room for `capacity` kinds that may follow it.
// Both of those arrays live behind the struct in the same block - first
// the pointers to the kinds, then a score for each - so a slice is one
// allocation of `6 * capacity + 0x30` bytes.  A slice with nowhere to
// go has nil for both rather than a pointer past its own end.
BiGSlice*
BiGSliceNew(short capacity)
{
	long n = capacity;
	// DEVIATION: the ROM asks for `6 * capacity + 0x30`, a 0x30-byte
	// header followed by `capacity` four-byte pointers and then as many
	// two-byte scores.  A host pointer is eight, so the size and the
	// two places are worked out from the struct.
	BiGSlice* slice = (BiGSlice*) RosAllocate(
					(long) (sizeof(BiGSlice) + n * (sizeof(BiGSlice*) + sizeof(short))));
	slice->fName = nil;
	slice->fNextCount = 0;
	slice->fNextCapacity = n;

	const BiGSlice** behind = (n != 0) ? (const BiGSlice**) (slice + 1) : nil;
	slice->fNext = behind;
	slice->fWeights = (n != 0) ? (const short*) (behind + n) : nil;
	slice->fField2c = 0xff;
	return slice;
}


// ROM 0x000ffd60 LEquiesant
void	LEquiesant(void)									{ }
// ROM 0x000d9cc8 GeoContextClearCache
// Three numbers the geometry context caches about the piece it was
// last asked about; the rest of `GeoContext*` is NOT YET.
void	GeoContextClearCache(void)							{ }
// ROM 0x000d9ce4 GeoCQuiesence
void	GeoCQuiesence(void)									{ }

// ROM 0x0011343c ListZap
void	ListZap(void)											{ }


#pragma mark -
/*--------------------------------------------------------------------
	Building a grammar for a field.
--------------------------------------------------------------------*/

// ROM 0x0003e048 BiGSliceCreate
// One kind of word.  The seven doubles and the long between them are
// the ParaGraph engine's training interface showing through: nothing
// here reads them, and the one caller passes 0.0, then six times 1.0,
// with nought for the long.  Kept in the signature because the ROM's
// stack frame has them.
BiGSlice*
BiGSliceCreate(const char* name, ULong dictionary,
			double /*a*/, double /*b*/, double /*c*/, long /*flag*/,
			double /*d*/, double /*e*/, double /*f*/, double /*g*/,
			short capacity)
{
	BiGSlice* slice = BiGSliceNew(capacity);
	newton_try
	{
		slice->fDictionary = dictionary;
		slice->fName = name;
	}
	cleanup
	{
		BiGSliceDestroy(slice);
	}
	end_try;
	return slice;
}


// ROM 0x0003e45c (unnamed)
// The kind of word with this dictionary and this name, added to the
// grammar if it is not there already.  Two slices are the same when
// their dictionaries match *and* their names are the same string or
// compare equal.
//
// The search is what makes cloning work: when the transitions are
// copied afterwards, each one asks for the kind it points at and gets
// back the copy that was already made rather than a second one.
BiGSlice*
BiGrammarAddSlice(BiGrammar* grammar, ULong dictionary, const char* name, short capacity)
{
	for (long i = 0; i < grammar->fCount; i++)
	{
		const BiGSlice* slice = grammar->fSlices[i];
		if (slice->fDictionary == dictionary
			&& (slice->fName == name || strcmp(slice->fName, name) == 0))
			return (BiGSlice*) grammar->fSlices[i];
	}

	// (the ROM pushes six doubles of one and one of nought, which
	//  `BiGSliceCreate` does not look at)
	BiGSlice* made = BiGSliceCreate(name, dictionary, 0.0, 1.0, 1.0, 0,
								1.0, 1.0, 1.0, 1.0, capacity);
	long at = grammar->fCount;
	grammar->fCount = at + 1;
	((const BiGSlice**) grammar->fSlices)[at] = made;
	return made;
}


// ROM 0x0003e534 BiGrammarClone
// A grammar copied whole: first every kind of word, then every
// transition between them.  Two passes, because a transition may point
// at a kind that has not been copied yet.
//
// **A ROM bug, kept.**  The first pass copies the shorts at +0x08,
// +0x0a and +0x0c but *not* the one at +0x0e, and `BiGSliceNew` does
// not clear it either - so a cloned slice's `fField0e` is whatever was
// in the heap.  It is nought in all 46 of the ROM's own slices, so
// nothing has ever depended on it; the reconstruction leaves it
// uncopied as the ROM does rather than tidying it.
BiGrammar*
BiGrammarClone(const BiGrammar* src)
{
	// (`volatile` because the handler reads it after a longjmp)
	BiGrammar* volatile copy = nil;
	newton_try
	{
		copy = BiGrammarCreate(src->fName, (short) src->fCount);
		copy->fField04 = src->fField04;
		copy->fField14 = src->fField14;
		copy->fField15 = src->fField15;
		copy->fField16 = src->fField16;
		copy->fField17 = src->fField17;
		copy->fField18 = src->fField18;
		copy->fField1c = src->fField1c;

		// every kind of word
		for (long i = 0; i < src->fCount; i++)
		{
			const BiGSlice* from = src->fSlices[i];
			BiGSlice* to = BiGrammarAddSlice((BiGrammar*) copy, from->fDictionary,
									from->fName, (short) from->fNextCount);
			to->fScore = from->fScore;
			to->fField0a = from->fField0a;
			to->fField0c = from->fField0c;
			// (+0x0e is not copied - see above)
			to->fField10 = from->fField10;
			to->fField14 = from->fField14;
			to->fField16 = from->fField16;
			to->fField18 = from->fField18;
			to->fField1a = from->fField1a;
			to->fField2c = from->fField2c;
		}

		// ... and then what may follow each of them, which by now is
		// always a kind the clone already has
		for (long i = 0; i < src->fCount; i++)
		{
			BiGSlice* to = (BiGSlice*) copy->fSlices[i];
			const BiGSlice* from = src->fSlices[i];
			for (long j = 0; j < from->fNextCount; j++)
			{
				const BiGSlice* target = from->fNext[j];
				((const BiGSlice**) to->fNext)[j] =
					BiGrammarAddSlice((BiGrammar*) copy, target->fDictionary,
									target->fName, (short) target->fNextCount);
				to->fNextCount++;
				((short*) to->fWeights)[j] = from->fWeights[j];
			}
		}
	}
	cleanup
	{
		BiGrammarDestroy(copy);
	}
	end_try;
	return (BiGrammar*) copy;
}


// The probability a score stands for, and the score a probability
// costs - the two halves of the arithmetic coder's table lookup, as
// `BiGrammarModifyContext` uses them.
static Fixed
ArProbDecode(long score)
{
	if (score >= kArProbMaxScore)
		return 0;
	if (score == 0)
		return 0x00010000;
	return (Fixed) ArProbDecodeLu[score >> 3];
}

static short
ArProbEncode(Fixed probability)
{
	if (probability < 1)
		return (short) kArProbNever;
	if (probability < 0x400)
		return ArProbEncodeLu2[probability];
	if (probability < 0x00010000)
		return ArProbEncodeLu1[probability >> 7];
	return 0;
}


// ROM 0x0003e0c0 BiGrammarModifyContext
// A grammar cloned and then reweighed, which is how the General grammar
// becomes the grammar for one field.
//
// The `count` kinds of word whose indices are in `slices` - the ones
// the field expects - are to share `weight` of the probability between
// them, and everything else shares what is left.  So the arithmetic is:
// turn every kind's score back into a probability, add up the two
// groups, work out the factor each group needs to come to its share,
// and turn the scaled probabilities back into scores.
//
// `slices` is in increasing order and walked with a cursor rather than
// searched, which is why the loop below advances it only on a match.
// If it does not end up having matched `count` of them the ROM writes a
// complaint into a buffer on its own stack and does nothing with it.
//
// Finally every score has the smallest subtracted from it, so the
// likeliest kind of word in the field costs nothing and the rest are
// priced relative to it.
BiGrammar*
BiGrammarModifyContext(const BiGrammar* grammar, long count, const long* slices,
					Fixed weight)
{
	BiGrammar* copy = BiGrammarClone(grammar);
	if (copy == nil)
		return nil;

	Fixed* volatile probs = nil;
	newton_try
	{
		probs = (Fixed*) RosAllocate(copy->fCount * (long) sizeof(Fixed));

		// what each kind is worth now, and the two groups' totals
		Fixed wanted = 0;
		Fixed rest = 0;
		long at = 0;
		for (long i = 0; i < copy->fCount; i++)
		{
			Fixed p = ArProbDecode(copy->fSlices[i]->fScore);
			probs[i] = p;
			if (slices[at] == i)
			{
				at++;
				wanted += p;
			}
			else
				rest += p;
		}
		if (at != count)
		{
			// (the ROM writes this into 512 bytes of its own stack and
			//  never looks at it)
			char message[512];
			sprintf(message, "numSliceMatches (%ld) != requested numSlices (%ld)"
						" in BiGrammarModifyContext", at, count);
		}

		Fixed toWanted = FixedDivide(weight, wanted);
		Fixed toRest = FixedDivide(0x00010000 - weight, rest);

		// ... and what it is worth in the field
		long cursor = 0;
		long least = kArProbNever;
		for (long i = 0; i < copy->fCount; i++)
		{
			short score;
			if (slices[cursor] == i)
			{
				cursor++;
				score = ArProbEncode(FixedMultiply(probs[i], toWanted));
			}
			else
				score = ArProbEncode(FixedMultiply(probs[i], toRest));
			((BiGSlice*) copy->fSlices[i])->fScore = score;
			if (copy->fSlices[i]->fScore <= least)
				least = copy->fSlices[i]->fScore;
		}

		// the likeliest kind of word costs nothing
		for (long i = 0; i < copy->fCount; i++)
		{
			BiGSlice* slice = (BiGSlice*) copy->fSlices[i];
			if (slice->fScore < kArProbNever)
				slice->fScore = (short) (slice->fScore - least);
		}
	}
	cleanup
	{
		if (probs != nil)
			DisposPtr((Ptr) probs);
		BiGrammarDestroy(copy);
	}
	end_try;
	DisposPtr((Ptr) probs);
	return copy;
}


// ROM 0x00056b7c CharGetAvgBoxBHW
// **B, H and W**: the base, the height and the width of a word, which
// is how the engine works out how big the writing in front of it is.
// It answers six numbers, three means and three extremes, and they are
// what every patternizer is then told about the writing.
//
// The mean base is the mean of the segments' bottoms - only of the
// segments that are more than a dot, because the dot over an i sits
// nowhere near the line.  The mean height and the mean width take only
// the segments that are at least two fifths of the size the word
// started at, so that punctuation does not drag them down; when there
// is nothing that big it falls back, first to twice the mean width and
// then to three times the least a stroke may be.
//
// The two extremes are the tallest and the widest segment, and the
// tallest is then **raised to at least what the word's overall shape
// suggests**: 1.8 times the widest letter, or a quarter of the word's
// height above the baseline plus the widest letter, whichever is more.
// A word written in short wide letters is therefore still measured as
// though it had a full-height letter in it somewhere, which is what
// stops a word of nothing but o's being read as a word of full stops.
//
// (A latent bug: `segments[0]` is read for its top and its bottom
//  before anything checks that there is a segment at all.  Every
//  caller has at least one.)
void
CharGetAvgBoxBHW(RosSegment* const* segments, short count, Fixed leastStroke,
				Fixed meanSize, Fixed widthScale, Fixed heightScale, short how,
				Fixed* base, Fixed* height, Fixed* width,
				Fixed* altBase, Fixed* maxHeight, Fixed* maxWidth)
{
	short nBase = 0;
	short nHeight = 0;
	short nWidth = 0;
	*base = 0;
	*height = 0;
	*width = 0;
	Fixed twice = leastStroke << 1;
	*maxHeight = twice;
	*maxWidth = leastStroke;

	Fixed top = segments[0]->fBounds.top;
	// two fifths of the size the word started at
	Fixed enough = FixedMultiply(meanSize, 0x6666);

	Fixed meanBase;
	if (count < 1)
		meanBase = segments[0]->fBounds.bottom;
	else
	{
		for (short i = 0; i < count; i++)
		{
			FPoint size;
			FixedRectSize(&size, &segments[i]->fBounds);
			// measured inclusively, so a single point is one by one
			size.x += 0x00010000;
			size.y += 0x00010000;

			RosSegment* seg = segments[i];
			// more than just the dot over an i
			if ((long) seg->fHasDot < (long) seg->fCount)
			{
				nBase++;
				*base += seg->fBounds.bottom;
				if (enough < size.y)
				{
					nHeight++;
					*height += size.y;
				}
				if (enough < size.x)
				{
					nWidth++;
					*width += size.x;
				}
			}
			if (*maxHeight < size.y)
				*maxHeight = size.y;
			if (*maxWidth < size.x)
				*maxWidth = size.x;
			if (seg->fBounds.top <= top)
				top = seg->fBounds.top;
		}
		meanBase = (nBase < 1)
				? segments[0]->fBounds.bottom
				: FixedDivide(*base, (Fixed) (int) ((unsigned int) nBase << 16));
	}
	*base = meanBase;
	*altBase = *base;

	if (nHeight < 1)
	{
		if (nWidth < 1)
			*height = leastStroke * 3;
		else
		{
			// nothing tall enough to measure: twice the mean width,
			// but never under twice the least a stroke may be
			Fixed twiceWidth = FixedDivide(*width,
								(Fixed) (int) ((unsigned int) nWidth << 16)) << 1;
			*height = (twice < twiceWidth) ? twiceWidth : twice;
		}
	}
	else
		*height = FixedDivide(*height, (Fixed) (int) ((unsigned int) nHeight << 16));

	*width = (nWidth < 1)
			? FixedMultiply(*height, 0x8000)
			: FixedDivide(*width, (Fixed) (int) ((unsigned int) nWidth << 16));

	if (widthScale != 0)
		*maxWidth = FixedMultiply(*maxWidth, widthScale);

	// the tallest letter, raised to what the word's shape suggests
	if (how <= 1)
	{
		Fixed fromWidth = FixedMultiply(0x1cccc, *maxWidth);			// 1.8 x
		Fixed fromShape = FixedMultiply((*altBase - top) + *maxHeight, 0x4000)
						+ *maxWidth;
		Fixed want = (fromWidth >= fromShape) ? fromWidth : fromShape;
		if (want > *maxHeight)
			*maxHeight = want;
	}
	else if (how == 2)
	{
		Fixed fromWidth = FixedMultiply(0x18000, *maxWidth);			// 1.5 x
		Fixed fromShape = FixedMultiply(FixedMultiply(0x20000, *maxWidth)
							+ (*altBase - top) + *maxHeight, 0x553f);
		Fixed want = (fromWidth >= fromShape) ? fromWidth : fromShape;
		if (want > *maxHeight)
			*maxHeight = want;
	}
	if (heightScale != 0)
		*maxHeight = FixedMultiply(*maxHeight, heightScale);
}


// ROM 0x00057108 CharModifyProbs
// NOT YET: the classifier's opinion of a piece of writing leaned on by
// where and how big it is - a letter that sits below the line is more
// likely to be a `g` than a `q`, and so on.  Without it the net's
// probabilities go to the search unmodified.
void
CharModifyProbs(const FRect* /*bounds*/, long /*strokes*/, UByte /*hasDot*/,
			UByte /*fragment*/, UByte /*joinsNext*/, Fixed /*size*/,
			Fixed /*run18*/, Fixed /*run19*/, Fixed /*run20*/, Fixed /*run21*/,
			Fixed /*startSize*/, Fixed /*wordSize*/, Fixed* /*scratch*/,
			Fixed /*altBase*/, Fixed /*altHeight*/, Fixed /*arg9*/, Fixed* /*probs*/)
{
}


