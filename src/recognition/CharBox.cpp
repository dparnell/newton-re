/*
	File:		recognition/CharBox.cpp

	Contains:	The boxed-character recogniser - see CharBox.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "CharBox.h"
#include "RosEngine.h"
#include "Segment.h"
#include "RosStrokes.h"
#include "BPNet.h"
#include "GeoContext.h"
#include "FixedGeometry.h"
#include "FixedMath.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "host/RomBugs.h"

#include <stdlib.h>


// Which of the net's own parameters the strokes are tidied with.  They
// are byte offsets into `arBPParam` in the ROM, so a quarter of each.
const long	kArParamSmoothWeight		= 0xc4 / 4;
const long	kArParamDeQuantWeight		= 0xcc / 4;
const long	kArParamDeQuantTolerance	= 0xd0 / 4;
const long	kArParamPasses				= 0xd4 / 4;


// ROM 0x00056354 CharBoxStateNew
// The block, with the pointers the destructor touches nilled.
CharBox*
CharBoxStateNew(void)
{
	CharBox* self = (CharBox*) RosAllocate((long) sizeof(CharBox));
	self->fSegment = nil;
	self->fSpare = nil;
	self->fStrokeCount = 0;
	self->fStrokes = nil;
	self->fPatternizer = nil;
	self->fPattern = nil;
	return self;
}


// ROM 0x00056684 CharBoxDestroy
void
CharBoxDestroy(CharBox* self)
{
	if (self == nil)
		return;
	SegmentDestroy(self->fSpare);
	SegmentDestroy(self->fSegment);
	NetPatternDestroy(self->fPattern);
	NetPatternizerDestroy(self->fPatternizer);
	if (self->fStrokes != nil)
	{
		for (long i = 0; i < self->fStrokeCount; i++)
			StrokeDestroy(self->fStrokes[i]);
		DisposPtr((Ptr) self->fStrokes);
	}
	DisposPtr((Ptr) self);
}


// ROM 0x000561c8 CharBoxIntialize
// A recogniser over one box.  (The ROM spells the name `Intialize`,
// and so does the symbol, so the spelling is kept.)
//
// The two segments are made before anything else and the strokes are
// an array of six pointers; on the way out of the handler the whole
// state goes back, which is why `CharBoxStateNew` nils every pointer
// the destructor touches before it is used.
void
CharBoxIntialize(CharBox** out, long field00, const FRect* box,
				long field04, BPNet* net)
{
	*out = nil;

	// The ROM asks both questions about the box and does nothing with
	// the answer: `ValidFixedRect(box) && EmptyFixedRect(box);` is a
	// statement with no effect, all that is left of what was presumably
	// an assertion in the original source.  Kept, because neither call
	// has a side effect and it is what the ROM does.
	if (ValidFixedRect(box))
		(void) EmptyFixedRect(box);

	// (`volatile` because the handler below reads it after a longjmp)
	CharBox* volatile self = nil;
	newton_try
	{
		self = CharBoxStateNew();
		self->fSpare = SegmentCreate();
		self->fSegment = SegmentCreate();
		// DEVIATION: the ROM asks for 0x18 bytes, six of its four-byte
		// pointers; a host pointer is eight, so the size is worked out.
		self->fStrokes = (RosStroke**) RosAllocate(
							kCharBoxMaxStrokes * (long) sizeof(RosStroke*));
		self->fPatternizer = NetPatternizerCreateFromBP(net);
		self->fPattern = NetPatternCreate(self->fPatternizer);
	}
	cleanup
	{
		CharBoxDestroy(self);
	}
	end_try;

	CharBox* state = (CharBox*) self;
	CopyFixedRect(&state->fBox, box);
	state->fField00 = field00;
	state->fNet = net;
	state->fField04 = field04;
	for (long i = 0; i < kCharBoxMaxStrokes; i++)
		state->fStrokes[i] = nil;
	for (long i = 0; i < kCharBoxCodeCount; i++)
		state->fScores[i] = kCharBoxNever;
	*out = state;
}


// ROM 0x00056708 CharBoxAddStroke
// A stroke put into the box.  It is tidied first - smoothed and
// dequantised with the four numbers the net's own parameters give -
// and that may make more than one stroke out of it, which is why the
// count is checked against the whole list.
//
// **ROM BUG (fixed):** the answer is meant to be nought when the stroke
// was taken and one when the box was full, but the function ends in a
// tail call to `SLDestroy` and so gives back *whatever that answers*
// instead.  The flag does reach `SLDestroy`, which is what makes it
// destroy the strokes of a list that was refused; it is only the caller
// who never learns.  `SLDestroy` answers nothing in particular, so this
// answers nought however it went.  The fix answers the flag: one when
// the box was full, nought when the stroke was taken.
long
CharBoxAddStroke(CharBox* self, RosStroke* stroke)
{
	const ULong* params = self->fNet->fArParams;
	RosStrokeList* list = StrokePreprocess(stroke,
							(Fixed) params[kArParamSmoothWeight],
							(Fixed) params[kArParamDeQuantWeight],
							(Fixed) params[kArParamDeQuantTolerance],
							(short) params[kArParamPasses]);
	if (list == nil)
		return 0;

	short refused;
	if (self->fStrokeCount + list->fCount < kCharBoxMaxStrokes + 1)
	{
		for (long i = 0; i < list->fCount; i++)
			self->fStrokes[self->fStrokeCount + i] = list->fStrokes[i];
		self->fStrokeCount += list->fCount;
		refused = 0;
	}
	else
		refused = 1;

	// the list goes, and its strokes with it only when the box would
	// not have them - otherwise they belong to the box now
	SLDestroy(list, refused);
	if (RomBugFixed())
		return refused;
	// (and this is where the ROM's answer comes from)
	return 0;
}


// ROM 0x000567c0 CharBoxStrokeInBox
// Whether a stroke's middle falls inside the box.  Every comparison is
// strict, so a stroke centred exactly on an edge is outside.
Boolean
CharBoxStrokeInBox(const CharBox* self, const RosStroke* stroke)
{
	FPoint middle;
	StrokeCentroid(stroke, &middle);
	return middle.x > self->fBox.left && middle.x < self->fBox.right
		&& middle.y > self->fBox.top && middle.y < self->fBox.bottom;
}


// ROM 0x000563c4 CharBoxNetSetInputs
// The writing measured into the pattern, and the pattern written into
// the net's inputs.  Two of the geometry numbers are passed twice: the
// height again as the tenth argument and the alternative height again
// as the eleventh, which is how the composite patternizer's children
// come to see a pair each.
void
CharBoxNetSetInputs(NetPattern* pattern, BPNet* net, RosStrokeList* strokes,
				Fixed base, Fixed height, Fixed arg6, Fixed altBase,
				Fixed altHeight, Fixed arg9, Fixed capHeight)
{
	const NetPatternizerType* type = pattern->fPatternizer->fType;
	type->fSLToPat(net, strokes, pattern, base, height, arg6, altBase,
				altHeight, arg9, height, altHeight, capHeight);
	type->fSetInput(pattern);
}


// ROM 0x00056a44 CharBoxNetEvaluate
// The classifier run over a piece of writing, and what it thinks of
// each of the 256 character codes left in `out` as a probability out of
// 0x10000.
//
// This is where the common info's character tables earn their keep.  A
// code the area will not have scores nothing at all.  A code that
// stands for one shape takes the output of the net node it maps to,
// widened from a byte to 16.16 by a shift of eight - so a node that is
// certain, 0xff, comes to 0xff00 rather than 0x10000, and nothing is
// ever quite sure.  A code that is really **two** characters takes the
// *product* of its two parts' outputs, which is how a net that was
// never shown the pair still has an opinion about it; and when the two
// parts happen to map to the same node the product is dropped and the
// single output used, so a doubled letter is not charged twice for
// being written twice.
void
CharBoxNetEvaluate(CharBox* self, RosStrokeList* strokes,
				Fixed base, Fixed height, Fixed arg6, Fixed altBase,
				Fixed altHeight, Fixed arg9, Fixed capHeight,
				Fixed* out)
{
	BPNet* net = self->fNet;
	CharBoxNetSetInputs(self->fPattern, net, strokes, base, height, arg6,
					altBase, altHeight, arg9, capHeight);
	BPNetEvaluate(net);

	const UByte* outputs = net->fOutputs;
	for (long code = 0; code < kCharBoxCodeCount; code++)
	{
		if ((RosCI->fLegalUse[code >> 5] & (1UL << (code & 31))) == 0)
		{
			out[code] = 0;
			continue;
		}
		UByte part1 = RosCI->fCompoundPart1[code];
		if (part1 == 0)
		{
			// one shape: the node this code maps to
			out[code] = (Fixed) ((ULong) outputs[RosCI->fCharToNetNode[code]] << 8);
			continue;
		}
		UByte node1 = RosCI->fCharToNetNode[part1];
		UByte node2 = RosCI->fCharToNetNode[RosCI->fCompoundPart2[code]];
		if (node1 == node2)
			out[code] = (Fixed) ((ULong) outputs[node1] << 8);
		else
			out[code] = FixedMultiply((Fixed) ((ULong) outputs[node1] << 8),
								(Fixed) ((ULong) outputs[node2] << 8));
	}
}


// ROM 0x000565a4 (unnamed) - the box as a segment
// The spare segment is given the box itself for its bounds, so that the
// geometry below can measure the writing against the box as though the
// box were the letter after it.
static void
CharBoxSetBoxSegment(CharBox* self, RosSegment* box)
{
	CopyFixedRect(&box->fBounds, &self->fBox);
}

// ROM 0x000565b4 (unnamed) - the box's strokes as a segment
static void
CharBoxSetStrokeSegment(CharBox* self, RosSegment* seg)
{
	seg->fCount = (short) self->fStrokeCount;
	SegmentSetStrokes(seg, (short) self->fStrokeCount, self->fStrokes);
	SegmentBoundsDotsEtc(seg);
}

// ROM 0x000565f8 (unnamed) - the geometry the classifier is told
// A box has no word around it to measure the writing by, so the seven
// numbers the word recogniser works out of a whole word
// (`CharGetAvgBoxBHW`) come from the box instead: its bottom is the
// base line, three quarters of its height is the height a letter is
// expected to be and four fifths of its width its width, and the box's
// own height and width stand for the tallest and widest.  The two
// fractions are the only floating point outside the word spacing - the
// FPA's FLT, MUF and FIX (rounding toward nought), which is what a C
// cast from double does.
static void
CharBoxGeometry(Fixed out[7], const FRect* box)
{
	FPoint size;
	FixedRectSize(&size, box);
	out[0] = box->bottom;
	out[1] = (Fixed) ((double) size.y * 0.75);
	out[2] = (Fixed) ((double) size.x * 0.8);
	out[3] = box->bottom;
	out[4] = size.y;
	out[5] = size.x;
	out[6] = size.y;
}

// ROM 0x00056878 CharBoxEvaluate
// What the box's writing scores as each of the 256 character codes.
//
// The strokes are made a segment and the box another, the classifier is
// run with the box's geometry (`CharBoxGeometry`), and `CharModifyProbs`
// leans on its answer with the height model - with nought for
// everything a word would have told it, so only the segment's own box,
// its stroke count and whether it has a dot count.  The probabilities
// become scores the search's way, and then each legal code the
// classifier did not rule out is charged, at the net's weight, for how
// badly the writing sits in the box: `GeoContextPenalty` asked about
// the code followed by character 0x1f written as the box itself - the
// same question the search asks about two letters side by side, with
// the box standing in for the letter after.
void
CharBoxEvaluate(CharBox* self)
{
	Fixed probs[kCharBoxCodeCount];
	RosSegment* seg = self->fSegment;
	CharBoxSetBoxSegment(self, self->fSpare);
	CharBoxSetStrokeSegment(self, self->fSegment);
	Fixed geometry[7];
	CharBoxGeometry(geometry, &self->fBox);
	CharBoxNetEvaluate(self, seg->fStrokes, geometry[0], geometry[1], geometry[2],
					geometry[3], geometry[4], geometry[5], geometry[6], probs);
	CharModifyProbs(&seg->fBounds, seg->fStrokes->fCount, seg->fHasDot, 0, 0,
					0, 0, 0, 0, 0, 0, 0, nil,
					geometry[3], geometry[4], geometry[5], probs);

	for (long code = 0; code < kCharBoxCodeCount; code++)
		self->fScores[code] = (short) ArProbEncode(probs[code]);

	GeoContextClearCache();
	for (long code = 0; code < kCharBoxCodeCount; code++)
	{
		if ((RosCI->fLegalUse[code >> 5] & (1UL << (code & 31))) == 0)
			continue;
		ULong score = (ULong) (UShort) self->fScores[code];
		if (score >= (ULong) kCharBoxNever)
			continue;
		ULong weighted = (ULong) (((long) (RosCI->fNetScoreWeight * (long) score)) >> 16) & 0xffff;
		long penalty = GeoContextPenalty((UByte) code, self->fSegment, 0x1f, self->fSpare, 0);
		self->fScores[code] = (short) (penalty + (long) weighted);
	}
}



// ROM 0x00056590 (unnamed)
// Ascending by score, the two halves compared *unsigned* - which puts
// the codes the engine will not have (0x7ffe) at the end.
static int
CompareChoices(const void* a, const void* b)
{
	ULong left = *(const ULong*) a;
	ULong right = *(const ULong*) b;
	return (int) (long) ((left >> 16) - (right >> 16));
}


// ROM 0x0005682c CharBoxGetChars
// The characters the box was written with, best first.  `count` says
// how many there is room for on the way in and how many were answered
// on the way out; the scores are sorted and copied until the first one
// that says never, and the rest of the caller's array is cleared.
void
CharBoxGetChars(CharBox* self, CharBoxChoice* out, short* count)
{
	if (self->fStrokeCount < 1)
	{
		*count = 0;
		return;
	}
	CharBoxEvaluate(self);

	long room = *count;
	*count = 0;

	// one word per code, with the score in the top half so that the
	// sort is a comparison of whole words
	ULong* words = (ULong*) RosAllocate(kCharBoxCodeCount * (long) sizeof(ULong));
	for (long code = 0; code < kCharBoxCodeCount; code++)
		words[code] = ((ULong) (UShort) self->fScores[code] << 16)
					| ((ULong) code << 8);
	qsort(words, kCharBoxCodeCount, sizeof(ULong), CompareChoices);

	long limit = (room > kCharBoxCodeCount) ? kCharBoxCodeCount : room;
	long taken = limit;
	for (long i = 0; i < limit; i++)
	{
		if ((words[i] >> 16) == (ULong) (UShort) kCharBoxNever)
		{
			taken = i;
			break;
		}
		out[i].fScore = (short) (words[i] >> 16);
		out[i].fCode = (UByte) (words[i] >> 8);
		out[i].fPad = (UByte) words[i];
	}
	// whatever the caller asked for and did not get, cleared
	for (long i = taken; i < room; i++)
	{
		out[i].fScore = 0;
		out[i].fCode = 0;
	}
	DisposPtr((Ptr) words);
	*count = (short) taken;
}
