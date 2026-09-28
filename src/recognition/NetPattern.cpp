/*
	File:		recognition/NetPattern.cpp

	Contains:	The patternizers - see NetPattern.h.

	The framework, the composite, the five scalars, and the two that do
	the real work: `ImageSplatLimited` (the writing drawn into a 14x14
	picture) and `StrokePUD` (the pen-up/down grid).

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "NetPattern.h"
#include "Render.h"
#include "RosEngine.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "FixedMath.h"

#include <stdio.h>
#include <string.h>


extern const ExceptionName exRosetta;	// ROM 0x003774f8 exRosetta


#pragma mark -
/*--------------------------------------------------------------------
	The little class system.
--------------------------------------------------------------------*/

// ROM 0x00133c24 NetPatternizerInit_
void
NetPatternizerInit_(NetPatternizer* self, const NetPatternizerType* type)
{
	self->fType = type;
	self->fRefCount = 1;
}


// ROM 0x00133c34 NetPatternInit_
void
NetPatternInit_(NetPattern* self, NetPatternizer* patternizer)
{
	self->fPatternizer = patternizer;
	self->fField04 = 0x3f;
}


// ROM 0x00133bb8 NetPatternizerNewInstance
// The class says how big one of its instances is.
NetPatternizer*
NetPatternizerNewInstance(const NetPatternizerType* type)
{
	NetPatternizer* self = (NetPatternizer*) RosAllocate(type->fSize);
	NetPatternizerInit_(self, type);
	return self;
}


// ROM 0x00133c44 NetPatternCreate
// A pattern, and the patternizer now has one more of them.
NetPattern*
NetPatternCreate(NetPatternizer* patternizer)
{
	NetPattern* pattern = patternizer->fType->fCreate(patternizer);
	patternizer->fRefCount++;
	return pattern;
}


// ROM 0x00133c70 NetPatternSetInput
void
NetPatternSetInput(NetPattern* pattern)
{
	pattern->fPatternizer->fType->fSetInput(pattern);
}


// ROM 0x00133a50 NetPatternizerUpdateGraphics
void
NetPatternizerUpdateGraphics(NetPatternizer* patternizer)
{
	patternizer->fType->fGraph(patternizer);
}


// ROM 0x00133a58 NetPatternizerDestroy
// Answers 1 while anything still holds it.
long
NetPatternizerDestroy(NetPatternizer* patternizer)
{
	if (patternizer != nil)
	{
		if (--patternizer->fRefCount != 0)
			return 1;
		patternizer->fType->fDestroy(patternizer);
		DisposPtr((Ptr) patternizer);
	}
	return 0;
}


// ROM 0x00133a08 NetPatternDestroy
// The pattern given back, and the patternizer with it when that was
// the last one.
void
NetPatternDestroy(NetPattern* pattern)
{
	if (pattern == nil)
		return;
	NetPatternizer* patternizer = pattern->fPatternizer;
	patternizer->fType->fPatternDestroy(pattern);
	if (--patternizer->fRefCount > 0)
		return;
	patternizer->fType->fDestroy(patternizer);
}


// ROM 0x00133aa8 NetPatternSetNth
// One-hot: every cell `off` and the one named `on`.  An index outside
// the cells lights none of them, which is what a value of exactly one
// comes to (see `NetPatternScalarSetInput`).
void
NetPatternSetNth(UByte* cells, long count, long index, UByte on, UByte off)
{
	for (long i = 0; i < count; i++)
		cells[i] = off;
	if (index < 0)
		return;
	if (index < count)
		cells[index] = on;
}


#pragma mark -
/*--------------------------------------------------------------------
	The composite: one child per input group of the net.
--------------------------------------------------------------------*/

// ROM 0x0013252c NetPatternizerMultiInit
static NetPatternizer*
NetPatternizerMultiInit(NetPatternizer* self, ULong count)
{
	if (self == nil)
		return nil;
	NetMultiPatternizer* multi = (NetMultiPatternizer*) self;
	multi->fChildren = nil;
	multi->fCount = count;
	multi->fChildren = (NetPatternizer**) RosAllocate(count * (long) sizeof(NetPatternizer*));
	for (ULong i = 0; i < count; i++)
		multi->fChildren[i] = nil;
	return self;
}


// ROM 0x001325c4 NetPatternizerMultiInitFromBP
// One child per input group the net names.
static void
NetPatternizerMultiInitFromBP(NetPatternizer* self, BPNet* net, long /*group*/, long /*flag*/)
{
	long groups = net->fGroupCount;
	if (groups <= 0)
		return;
	NetMultiPatternizer* multi = (NetMultiPatternizer*) NetPatternizerMultiInit(self, (ULong) groups);
	if (multi == nil)
		return;
	for (ULong i = 0; i < multi->fCount; i++)
		multi->fChildren[i] = NetPatternizerCreateForGrid(net, i);
}


// ROM 0x0013263c NetPatternizerMultiDestroy
static void
NetPatternizerMultiDestroy(NetPatternizer* self)
{
	NetMultiPatternizer* multi = (NetMultiPatternizer*) self;
	if (multi == nil || multi->fChildren == nil)
		return;
	for (ULong i = 0; i < multi->fCount; i++)
		NetPatternizerDestroy(multi->fChildren[i]);
	DisposPtr((Ptr) multi->fChildren);
	multi->fChildren = nil;
}


// ROM 0x00132698 NetPatternizerMultiGraph
static void
NetPatternizerMultiGraph(NetPatternizer* self)
{
	NetMultiPatternizer* multi = (NetMultiPatternizer*) self;
	for (ULong i = 0; i < multi->fCount; i++)
		NetPatternizerUpdateGraphics(multi->fChildren[i]);
}


// ROM 0x001326d8 NetPatternMultiCreate
// A pattern of its own, holding one pattern per child.
static NetPattern*
NetPatternMultiCreate(NetPatternizer* self)
{
	if (self == nil)
		return nil;
	NetMultiPatternizer* multi = (NetMultiPatternizer*) self;
	NetMultiPattern* pattern = (NetMultiPattern*) RosAllocate((long) sizeof(NetMultiPattern));
	NetPatternInit_((NetPattern*) pattern, self);
	pattern->fChildren = nil;

	newton_try
	{
		pattern->fCount = multi->fCount;
		pattern->fChildren = (NetPattern**) NewPtrClear(pattern->fCount * (long) sizeof(NetPattern*));
		if (pattern->fChildren == nil)
			Throw(exOutOfStack, (void*) "", nil);
		SetPtrName((Ptr) pattern->fChildren, kRosettaMemoryTag);
		for (ULong i = 0; i < pattern->fCount; i++)
			pattern->fChildren[i] = NetPatternCreate(multi->fChildren[i]);
	}
	cleanup
	{
		NetPatternDestroy((NetPattern*) pattern);
	}
	end_try;
	return (NetPattern*) pattern;
}


// ROM 0x00132928 NetPatternMultiDestroy
static void
NetPatternMultiDestroy(NetPattern* self)
{
	if (self == nil)
		return;
	NetMultiPattern* pattern = (NetMultiPattern*) self;
	if (pattern->fChildren != nil)
	{
		for (ULong i = 0; i < pattern->fCount; i++)
			NetPatternDestroy(pattern->fChildren[i]);
		DisposPtr((Ptr) pattern->fChildren);
		pattern->fChildren = nil;
	}
	DisposPtr((Ptr) pattern);
}


// ROM 0x0013284c NetPatternMultiSLToPat
// Every child measures the same writing.
static void
NetPatternMultiSLToPat(BPNet* net, RosStrokeList* strokes, NetPattern* self,
					Fixed base, Fixed height, Fixed arg6, Fixed altBase, Fixed altHeight,
					Fixed arg9, Fixed arg10, Fixed arg11, Fixed capHeight)
{
	if (self == nil)
		return;
	NetMultiPattern* pattern = (NetMultiPattern*) self;
	for (ULong i = 0; i < pattern->fCount; i++)
	{
		NetPattern* child = pattern->fChildren[i];
		child->fPatternizer->fType->fSLToPat(net, strokes, child, base, height, arg6,
											altBase, altHeight, arg9, arg10, arg11, capHeight);
	}
}


// ROM 0x001328e8 NetPatternMultiSetInput
static void
NetPatternMultiSetInput(NetPattern* self)
{
	NetMultiPattern* pattern = (NetMultiPattern*) self;
	for (ULong i = 0; i < pattern->fCount; i++)
		NetPatternSetInput(pattern->fChildren[i]);
}


#pragma mark -
/*--------------------------------------------------------------------
	The scalars: one number between nought and one.
--------------------------------------------------------------------*/

// ROM 0x001329e4 NetPatternizerScalarInit
static void
NetPatternizerScalarInit(NetPatternizer* self, const char* /*name*/, UByte on, UByte off,
						long count, UByte* inputs)
{
	NetScalarPatternizer* scalar = (NetScalarPatternizer*) self;
	scalar->fOn = on;
	scalar->fOff = off;
	scalar->fInputs = inputs;
	scalar->fCount = count;
}


// ROM 0x00132a00 NetPatternizerScalarInitFromBP
// The group says how many cells it has and where they are; what a lit
// and an unlit cell hold comes out of the net's own parameters.
static void
NetPatternizerScalarInitFromBP(NetPatternizer* self, const char* name, BPNet* net, long group)
{
	const ULong* ngs = net->fNGS + group * 2;
	NetPatternizerScalarInit(self, name,
							(UByte) (net->fArParams[6] & 0xff),
							(UByte) (net->fArParams[5] & 0xff),
							(long) (((int) ngs[0]) >> 16),
							net->fUnits + (((int) ngs[1]) >> 16));
}


// ROM 0x00132d84 NetPatternScalarCreate
static NetPattern*
NetPatternScalarCreate(NetPatternizer* self)
{
	NetScalarPattern* pattern = (NetScalarPattern*) RosAllocate((long) sizeof(NetScalarPattern));
	NetPatternInit_((NetPattern*) pattern, self);
	pattern->fValue = 0x00010000;
	return (NetPattern*) pattern;
}


// ROM 0x00132e88 NetPatternScalarDestroy
static void
NetPatternScalarDestroy(NetPattern* self)
{
	if (self != nil)
		DisposPtr((Ptr) self);
}


// ROM 0x00132d7c NetPatternizerScalarDestroy
static void	NetPatternizerScalarDestroy(NetPatternizer* /*self*/)	{ }
// ROM 0x00132d80 NetPatternizerScalarGraph
static void	NetPatternizerScalarGraph(NetPatternizer* /*self*/)		{ }


// ROM 0x00132df8 NetPatternScalarSetInput
// A single-cell group takes the value as a byte of brightness; a group
// of more takes a one-hot.
//
// ROM QUIRK: the cell is chosen by multiplying the value by `count`
// plus 0.99, so a value of exactly one - which every one of the
// measurements below can reach, since they all clamp at one - comes to
// `count` and lights no cell at all.  Kept as it is.
static void
NetPatternScalarSetInput(NetPattern* self)
{
	NetScalarPattern* pattern = (NetScalarPattern*) self;
	NetScalarPatternizer* scalar = (NetScalarPatternizer*) self->fPatternizer;
	if (scalar->fCount != 0)
	{
		if ((ULong) scalar->fCount <= 1)
			scalar->fInputs[0] = (UByte) (FixedMultiply(pattern->fValue, 0x0000ff00) >> 8);
		else
		{
			Fixed scale = (Fixed) (int) (0x0000fd70 + ((unsigned int) scalar->fCount << 16));
			long cell = (long) (short) (FixedMultiply(pattern->fValue, scale) >> 16);
			NetPatternSetNth(scalar->fInputs, scalar->fCount, cell, scalar->fOn, scalar->fOff);
		}
	}
	NetPatternizerScalarGraph(self->fPatternizer);
}


// The clamp every one of the measurements below ends with.  (The ROM
// writes it out each time, and works the value out again at each
// step: the compiler did not take the common expression out.)
static Fixed
ClampToOne(Fixed value)
{
	if (value > 0x00010000)
		return 0x00010000;
	if (value < 0)
		return 0;
	return value;
}


// ROM 0x00132ec8 NetPatternAspectNormSLToPat
// How wide the writing is against how tall, as a fraction of one and a
// half - so a square hand reads two thirds and anything wider reads
// one.
static void
NetPatternAspectNormSLToPat(BPNet* /*net*/, RosStrokeList* strokes, NetPattern* self,
						Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed)
{
	if (self == nil)
		return;
	FRect bounds;
	SLFindBounds(strokes, &bounds);
	FPoint size;
	FixedRectSize(&size, &bounds);
	Fixed width = size.x + 0x00010000;
	Fixed height = size.y + 0x00010000;
	Fixed value = FixedDivide(width, height);
	if (value > 0x00018000)
		value = 0x00018000;
	((NetScalarPattern*) self)->fValue = FixedDivide(value, 0x00018000);
}


// ROM 0x00132a4c NetPatternCapHeightSLToPat
// How tall the writing is against the hand's cap height.
static void
NetPatternCapHeightSLToPat(BPNet* /*net*/, RosStrokeList* strokes, NetPattern* self,
						Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed capHeight)
{
	if (self == nil)
		return;
	FRect bounds;
	SLFindBounds(strokes, &bounds);
	FPoint size;
	FixedRectSize(&size, &bounds);
	((NetScalarPattern*) self)->fValue =
		ClampToOne(FixedDivide(size.y + 0x00010000, capHeight));
}


// ROM 0x00132f4c NetPatternHeightSLToPat
// ... and against the line's own height, which the net's parameters
// say to take either as twice the height given or as four fifths of
// it.
static void
NetPatternHeightSLToPat(BPNet* net, RosStrokeList* strokes, NetPattern* self,
					Fixed, Fixed height, Fixed, Fixed, Fixed altHeight, Fixed, Fixed, Fixed, Fixed)
{
	if (self == nil)
		return;
	if ((net->fArParams[16] & 0xff) != 0)
		height = altHeight;
	FRect bounds;
	SLFindBounds(strokes, &bounds);
	FPoint size;
	FixedRectSize(&size, &bounds);
	Fixed measured = size.y + 0x00010000;

	Fixed value;
	if ((net->fArParams[15] & 0xff) == 0)
		value = FixedDivide(measured, height * 2);
	else
		value = FixedMultiply(FixedDivide(measured, height), 0x0000cccc);
	((NetScalarPattern*) self)->fValue = ClampToOne(value);
}


// ROM 0x00132b20 NetPatternBaseSLToPat
// Where the writing sits against the line it is written on: the foot
// of it, measured from the baseline in units of the line's height,
// moved up by a half and scaled by seven tenths, so that sitting on
// the line reads about a third.  The net's parameters say which way
// round the baseline is measured, and which of the two pairs of
// numbers to use.
static void
NetPatternBaseSLToPat(BPNet* net, RosStrokeList* strokes, NetPattern* self,
					Fixed base, Fixed height, Fixed, Fixed altBase, Fixed altHeight,
					Fixed, Fixed, Fixed, Fixed)
{
	if (self == nil)
		return;
	if ((net->fArParams[16] & 0xff) != 0)
	{
		height = altHeight;
		base = altBase;
	}
	FRect bounds;
	SLFindBounds(strokes, &bounds);

	Fixed from = ((net->fArParams[15] & 0xff) == 0)
				? bounds.bottom - base
				: base - bounds.bottom;
	Fixed value = FixedMultiply(FixedDivide(from, height) + 0x00008000, 0x0000b333);
	((NetScalarPattern*) self)->fValue = ClampToOne(value);
}


// ROM 0x00132d34 NetPatternCountSLToPat
// How many strokes the writing took, as a fraction of the most the net
// can be told about.
static void
NetPatternCountSLToPat(BPNet* /*net*/, RosStrokeList* strokes, NetPattern* self,
					Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed)
{
	if (self == nil)
		return;
	ULong most = (ULong) ((NetScalarPatternizer*) self->fPatternizer)->fCount;
	ULong count = (ULong) strokes->fCount;
	if (count > most)
		count = most;
	((NetScalarPattern*) self)->fValue =
		FixedDivide((Fixed) (int) (count << 16), (Fixed) (int) (most << 16));
}


// The five of them differ only in the name they are given and the
// measurement they take, so the ROM has one `InitFromBP` per name.
// ROM 0x00132e94 NetPatternizerAspectNormInitFromBP
static void	NetPatternizerAspectNormInitFromBP(NetPatternizer* self, BPNet* net, long group, long)
			{ NetPatternizerScalarInitFromBP(self, "AspectNorm", net, group); }
// ROM 0x00132af0 NetPatternizerBaseInitFromBP
static void	NetPatternizerBaseInitFromBP(NetPatternizer* self, BPNet* net, long group, long)
			{ NetPatternizerScalarInitFromBP(self, "Base", net, group); }
// ROM 0x00132c98 NetPatternizerHeightInitFromBP
static void	NetPatternizerHeightInitFromBP(NetPatternizer* self, BPNet* net, long group, long)
			{ NetPatternizerScalarInitFromBP(self, "Height", net, group); }
// ROM 0x00132cc8 NetPatternizerCapHeightInitFromBP
static void	NetPatternizerCapHeightInitFromBP(NetPatternizer* self, BPNet* net, long group, long)
			{ NetPatternizerScalarInitFromBP(self, "CapHeight", net, group); }
// ROM 0x00132cfc NetPatternizerCountInitFromBP
static void	NetPatternizerCountInitFromBP(NetPatternizer* self, BPNet* net, long group, long)
			{ NetPatternizerScalarInitFromBP(self, "Stroke Count", net, group); }


#pragma mark -
/*--------------------------------------------------------------------
	The image: the writing drawn into the grid.
--------------------------------------------------------------------*/

static void	NetPatternImageDestroy(NetPattern* self);

// ROM 0x00132010 NetPatternizerImageInit
static NetPatternizer*
NetPatternizerImageInit(NetPatternizer* self, long limited, Fixed field14, UByte on, UByte off,
						long width, long height, UByte* inputs)
{
	NetImagePatternizer* image = (NetImagePatternizer*) self;
	image->fLimited = limited;
	image->fField14 = field14;
	image->fOn = on;
	image->fOff = off;
	image->fInputs = inputs;
	image->fAA = nil;
	image->fWidth = width;
	image->fHeight = height;
	image->fAA = RenderAACreate(width, height, kNetPatternImageShift);
	image->fPixels = image->fAA->fPixels;
	return self;
}


// ROM 0x00132078 NetPatternizerImageInitFromBP
// The grid is the group's own size - fourteen by fourteen for the
// ROM's net - and it is drawn at four times that.
static void
NetPatternizerImageInitFromBP(NetPatternizer* self, BPNet* net, long group, long flag)
{
	const ULong* ngs = net->fNGS + group * 2;
	NetPatternizerImageInit(self, flag, (Fixed) net->fArParams[31],
						(UByte) (net->fArParams[6] & 0xff),
						(UByte) (net->fArParams[5] & 0xff),
						(long) (short) (ngs[0] >> 16),
						// (the ROM takes the second measure with an
						//  unaligned `ldr`, so it is the low half)
						(long) (short) (ngs[0] & 0xffff),
						net->fUnits + (((int) ngs[1]) >> 16));
}


// ROM 0x001320e4 NetPatternizerImageDestroy
static void
NetPatternizerImageDestroy(NetPatternizer* self)
{
	NetImagePatternizer* image = (NetImagePatternizer*) self;
	if (image == nil || image->fAA == nil)
		return;
	// the patterns borrow the renderer's grid, so its own is put back
	// before it is given away
	image->fAA->fPixels = image->fPixels;
	RenderAADestroy(image->fAA);
	image->fAA = nil;
}


// ROM 0x00132104 NetPatternizerImageGraph
static void	NetPatternizerImageGraph(NetPatternizer* /*self*/)		{ }


// ROM 0x00132108 NetPatternImageCreate
// A pattern of its own, with a grid of its own for the renderer to
// fill in.
static NetPattern*
NetPatternImageCreate(NetPatternizer* self)
{
	NetImagePatternizer* image = (NetImagePatternizer*) self;
	NetImagePattern* pattern = (NetImagePattern*) RosAllocate((long) sizeof(NetImagePattern));
	pattern->fPixels = nil;
	newton_try
	{
		NetPatternInit_((NetPattern*) pattern, self);
		pattern->fPixels = (UByte*) RosAllocate(image->fHeight * image->fWidth);
	}
	cleanup
	{
		NetPatternImageDestroy((NetPattern*) pattern);
	}
	end_try;
	return (NetPattern*) pattern;
}


// ROM 0x001324e8 NetPatternImageDestroy
static void
NetPatternImageDestroy(NetPattern* self)
{
	NetImagePattern* pattern = (NetImagePattern*) self;
	if (pattern == nil)
		return;
	if (pattern->fPixels != nil)
		DisposPtr((Ptr) pattern->fPixels);
	DisposPtr((Ptr) pattern);
}


// ROM 0x00132200 NetPatternImageSLToPat
// The writing drawn into the grid.
//
// The scale is where the work is.  Each axis wants to fill the grid,
// but it is held to at most two and a half times life size and at
// most 1.6 of what the line's own height would give - and then the two
// axes are held to within three times each other, which is what
// *SplatLimited* means: a lower-case `l` is not blown up into a
// letter-shaped smear, and an `m` is not squashed flat.  With the
// scales settled the writing is centred in the grid and drawn.
static void
NetPatternImageSLToPat(BPNet* net, RosStrokeList* strokes, NetPattern* self,
					Fixed /*a4*/, Fixed down, Fixed across, Fixed /*a7*/,
					Fixed altDown, Fixed altAcross, Fixed, Fixed, Fixed)
{
	if (self == nil)
		return;
	NetImagePattern* pattern = (NetImagePattern*) self;
	NetImagePatternizer* image = (NetImagePatternizer*) self->fPatternizer;

	// the renderer draws into this pattern's own grid
	image->fAA->fPixels = pattern->fPixels;
	RenderClear(image->fAA->fRec);

	if ((net->fArParams[16] & 0xff) != 0)
	{
		across = altAcross;
		down = altDown;
	}

	FRect bounds;
	SLFindBounds(strokes, &bounds);
	FPoint size;
	FixedRectSize(&size, &bounds);

	Fixed gridWidth = (Fixed) (int) ((unsigned int) image->fWidth << 16);
	Fixed gridHeight = (Fixed) (int) ((unsigned int) image->fHeight << 16);
	Fixed fitWidth = gridWidth - 0x00010000;
	Fixed fitHeight = gridHeight - 0x00010000;

	Fixed xScale = 0;
	Fixed yScale = 0;
	if (image->fLimited == 1)
	{
		Fixed most = (Fixed) net->fArParams[21];		// two and a half
		Fixed factor = (Fixed) net->fArParams[22];		// one and six tenths

		xScale = (fitWidth < FixedMultiply(most, size.x))
				? FixedDivide(fitWidth, size.x) : most;
		Fixed byLine = FixedMultiply(factor, FixedDivide(fitWidth, across));
		if (byLine < xScale)
			xScale = byLine;

		yScale = (fitHeight < FixedMultiply(most, size.y))
				? FixedDivide(fitHeight, size.y) : most;
		byLine = FixedMultiply(factor, FixedDivide(fitHeight, down));
		if (byLine < yScale)
			yScale = byLine;

		// neither axis more than three times the other
		if (xScale * 3 < yScale)
			yScale = xScale * 3;
		if (yScale * 3 < xScale)
			xScale = yScale * 3;
	}

	// centred in the grid
	Fixed x = FixedMultiply(gridWidth - FixedMultiply(size.x, xScale), 0x8000)
			- FixedMultiply(bounds.left, xScale);
	Fixed y = FixedMultiply(gridHeight - FixedMultiply(size.y, yScale), 0x8000)
			- FixedMultiply(bounds.top, yScale);
	SLDrawAAAt(strokes, image->fAA, x, y, xScale, yScale);
	RenderAAFlush(image->fAA);
}


// ROM 0x0013243c NetPatternImageSetInput
// The grid copied into the net's inputs, straight when a lit cell is
// 255 and an unlit one nought, and remapped into the two otherwise.
static void
NetPatternImageSetInput(NetPattern* self)
{
	NetImagePattern* pattern = (NetImagePattern*) self;
	NetImagePatternizer* image = (NetImagePatternizer*) self->fPatternizer;
	const UByte* src = pattern->fPixels;
	const UByte* end = src + image->fHeight * image->fWidth;
	UByte* dst = image->fInputs;

	if (image->fOff != 0 || image->fOn != 0xff)
		for (; src < end; src++)
		{
			Fixed scaled = FixedMultiply((Fixed) (*src * 0x101),
										(Fixed) ((image->fOn - image->fOff) * 0x100));
			*dst++ = (UByte) (image->fOff + (scaled >> 8));
		}
	else
		for (; src < end; src++)
			*dst++ = *src;
}

#pragma mark -
/*--------------------------------------------------------------------
	The pen-up/down grid.
--------------------------------------------------------------------*/

// ROM 0x001330ac NetPatternizerStrokeInit
static NetPatternizer*
NetPatternizerStrokeInit(NetPatternizer* self, UByte on, UByte off,
						long width, long height, UByte* inputs)
{
	NetStrokePatternizer* pud = (NetStrokePatternizer*) self;
	pud->fOn = on;
	pud->fOff = off;
	pud->fWidth = width;
	pud->fHeight = height;
	pud->fInputs = inputs;
	return self;
}


// ROM 0x001330d4 NetPatternizerStrokeInitFromBP
static void
NetPatternizerStrokeInitFromBP(NetPatternizer* self, BPNet* net, long group, long /*flag*/)
{
	const ULong* ngs = net->fNGS + group * 2;
	NetPatternizerStrokeInit(self,
						(UByte) (net->fArParams[6] & 0xff),
						(UByte) (net->fArParams[5] & 0xff),
						(long) (short) (ngs[0] >> 16),
						// (the ROM takes this one with an unaligned `ldr`)
						(long) (short) (ngs[0] & 0xffff),
						net->fUnits + (((int) ngs[1]) >> 16));
}


// ROM 0x00133128 NetPatternizerStrokeDestroy
static void	NetPatternizerStrokeDestroy(NetPatternizer* /*self*/)	{ }
// ROM 0x0013312c NetPatternizerStrokeGraph
static void	NetPatternizerStrokeGraph(NetPatternizer* /*self*/)		{ }


// ROM 0x00133950 NetPatternStrokeDestroy
static void
NetPatternStrokeDestroy(NetPattern* self)
{
	NetStrokePattern* pattern = (NetStrokePattern*) self;
	if (pattern == nil)
		return;
	if (pattern->fCells != nil)
		DisposPtr((Ptr) pattern->fCells);
	DisposPtr((Ptr) pattern);
}


// ROM 0x00133130 NetPatternStrokeCreate
static NetPattern*
NetPatternStrokeCreate(NetPatternizer* self)
{
	NetStrokePatternizer* pud = (NetStrokePatternizer*) self;
	NetStrokePattern* pattern = nil;
	newton_try
	{
		pattern = (NetStrokePattern*) RosAllocate((long) sizeof(NetStrokePattern));
		NetPatternInit_((NetPattern*) pattern, self);
		pattern->fCells = nil;
		pattern->fCells = (UByte*) RosAllocate(pud->fHeight * pud->fWidth);
	}
	cleanup
	{
		NetPatternStrokeDestroy((NetPattern*) pattern);
	}
	end_try;
	return (NetPattern*) pattern;
}


// ROM 0x001337d8 ApproxFixATan2Cycles
// The arctangent in *cycles*: a whole turn is 0x10000, so the answer
// runs from -0x8000 to 0x8000.  It is a cubic in the smaller of the
// two over the larger - `0x28be` is a sixth of a turn per radian, near
// enough - with the octant added on afterwards, and no table.
Fixed
ApproxFixATan2Cycles(Fixed y, Fixed x)
{
	if (y == 0)
		return (x < 0) ? -0x8000 : 0;

	Fixed absY = (y < 0) ? -y : y;
	Fixed absX = (x < 0) ? -x : x;
	if (absX < absY)
	{
		// steeper than a diagonal: the angle is measured off the
		// vertical and a quarter turn added
		Fixed t = -FixedDivide(x, y);
		Fixed cube = FixedMultiply(FixedMultiply(t, t), t);
		Fixed angle = FixedMultiply(0x28be, t - cube) + (cube >> 3);
		return angle + ((x < 0) ? -0x4000 : 0x4000);
	}

	Fixed t = FixedDivide(y, x);
	Fixed cube = FixedMultiply(FixedMultiply(t, t), t);
	Fixed angle = FixedMultiply(0x28be, t - cube) + (cube >> 3);
	if (x >= 0)
		return angle;
	// the other half turn, brought back into range
	return angle + ((angle < 0) ? 0x8000 : -0x8000);
}


// ROM 0x0013322c NetPatternStrokePUDSLToPat
// The writing walked at a steady speed, and what the pen was doing
// written down twenty times along the way.
//
// Every point of every stroke goes into four parallel arrays - where
// it was, how far it is from the one before, and whether the pen was
// *down* getting there (the first point of a stroke is a jump, not a
// stroke of the pen).  The whole length is then divided into twenty
// equal steps, and for each step the engine works out where it has
// got to and which way it is going.
//
// A column of the grid is nine cells.  Eight of them are the direction
// of travel, spread between two neighbouring buckets by how far
// between them it falls - and the buckets wrap round, because a
// direction does.  The ninth, the first, is how much of that step the
// pen was *up*: 255 for a jump between strokes and nought for a stroke
// drawn on the paper.  That is what the name says: pen up, pen down.
static void
NetPatternStrokePUDSLToPat(BPNet* /*net*/, RosStrokeList* strokes, NetPattern* self,
						Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed, Fixed)
{
	if (self == nil)
		return;
	NetStrokePattern* pattern = (NetStrokePattern*) self;
	NetStrokePatternizer* pud = (NetStrokePatternizer*) self->fPatternizer;

	// how many points there are in all
	long total = 0;
	for (short s = 0; s < strokes->fCount; s++)
		total += strokes->fStrokes[s]->fCount;

	// one block carved into four arrays: x, y, the length of the step
	// that reached the point, and whether the pen was down for it
	Fixed* volatile block = nil;
	newton_try
	{
		block = (Fixed*) RosAllocate(total * 13);
		Fixed* xs = block;
		Fixed* ys = block + total;
		Fixed* lens = block + total * 2;
		UByte* down = (UByte*) (block + total * 3);

		long at = 0;
		Fixed length = 0;
		Fixed lastX = 0;
		Fixed lastY = 0;
		for (short s = 0; s < strokes->fCount; s++)
		{
			RosStroke* stroke = strokes->fStrokes[s];
			for (short i = 0; i < stroke->fCount; i++)
			{
				if (at >= total)
					return;
				// the first point of a stroke was reached with the pen
				// off the paper
				down[at] = (UByte) (i != 0);
				xs[at] = stroke->fPoints[i].x;
				ys[at] = stroke->fPoints[i].y;
				if (at != 0)
				{
					Fixed dx = xs[at] - lastX;
					Fixed dy = ys[at] - lastY;
					Fract square = FixedMultiply(dx, dx) + FixedMultiply(dy, dy);
					Fixed step = (FractSquareRoot(square) + 0x40) >> 7;
					length += step;
					lens[at] = step;
				}
				lastX = xs[at];
				lastY = ys[at];
				at++;
			}
		}

		// twenty equal steps along the whole of it
		Fixed each = FixedDivide(length, (Fixed) (int) ((unsigned int) pud->fWidth << 16));
		Fixed prevX = xs[0];
		Fixed prevY = ys[0];
		long point = 0;
		Fixed carried = 0;
		Fixed target = each;
		for (long cell = 0; cell < pud->fWidth; cell++)
		{
			Fixed penDown = 0;
			// walk on until the next point is further than the step
			while (point < at - 1 && lens[point + 1] <= target)
			{
				point++;
				target -= lens[point];
				if (down[point] != 0)
					penDown += lens[point] - carried;
				carried = 0;
			}

			Fixed x, y;
			if (point < at - 1)
			{
				// part way along the segment
				Fixed t = (lens[point + 1] == 0)
						? 0x00010000
						: FixedDivide(target, lens[point + 1]);
				x = xs[point] + FixedMultiply(t, xs[point + 1] - xs[point]);
				y = ys[point] + FixedMultiply(t, ys[point + 1] - ys[point]);
				if (down[point + 1] != 0)
				{
					Fixed sofar = FixedMultiply(t, lens[point + 1]);
					penDown += sofar - carried;
					carried = sofar;
				}
			}
			else
			{
				// ... or at the end of the writing
				x = xs[at - 1];
				y = ys[at - 1];
				if (down[at - 1] != 0)
				{
					penDown += lens[at - 1] - carried;
					carried = lens[at - 1];
				}
			}

			// which way it is going, over eight buckets that wrap
			Fixed angle = ApproxFixATan2Cycles(x - prevX, y - prevY) + 0x8000;
			long buckets = pud->fHeight - 1;
			long spread = buckets * angle;
			long between = (spread >> 8) & 0xff;
			long first = ((spread >> 16) % buckets) + 1;
			long second = (first % buckets) + 1;

			for (long r = 0; r < pud->fHeight; r++)
				pattern->fCells[r * pud->fWidth + cell] = 0;
			pattern->fCells[first * pud->fWidth + cell] = (UByte) (0xff - between);
			pattern->fCells[second * pud->fWidth + cell] = (UByte) between;

			// ... and how much of the step the pen was up for
			Fixed part = FixedDivide(penDown, each);
			pattern->fCells[cell] = (part < 0x00010000)
								? (UByte) (0xff - (part >> 8))
								: 0;

			prevX = x;
			prevY = y;
			target += each;
		}
	}
	cleanup
	{
		if (block != nil)
			DisposPtr((Ptr) block);
	}
	end_try;
	if (block != nil)
		DisposPtr((Ptr) block);
}


// ROM 0x001338c0 NetPatternStrokeSetInput
// The grid copied into the net's inputs, the same way the picture is.
static void
NetPatternStrokeSetInput(NetPattern* self)
{
	NetStrokePattern* pattern = (NetStrokePattern*) self;
	NetStrokePatternizer* pud = (NetStrokePatternizer*) self->fPatternizer;
	const UByte* src = pattern->fCells;
	const UByte* end = src + pud->fHeight * pud->fWidth;
	UByte* dst = pud->fInputs;

	if (pud->fOff != 0 || pud->fOn != 0xff)
		for (; src < end; src++)
			*dst++ = (UByte) (pud->fOff
						+ ((*src * 0x101 * (pud->fOn - pud->fOff)) >> 8));
	else
		for (; src < end; src++)
			*dst++ = *src;
}


#pragma mark -
/*--------------------------------------------------------------------
	The classes themselves, and the table a name is looked up in.
--------------------------------------------------------------------*/

// ROM 0x00374004 NetPatternImageT
const NetPatternizerType	NetPatternImageT = {
	// (the ROM's instance is 0x28 bytes; a host pointer is twice as wide)
	"BasicImage", (long) sizeof(NetImagePatternizer),
	NetPatternizerImageInitFromBP, NetPatternImageCreate, NetPatternImageSLToPat,
	NetPatternImageSetInput, NetPatternImageDestroy,
	NetPatternizerImageGraph, NetPatternizerImageDestroy
};

// ROM 0x00374028 NetPatternMultiT
const NetPatternizerType	NetPatternMultiT = {
	"Multi-Input Grid", (long) sizeof(NetMultiPatternizer),
	NetPatternizerMultiInitFromBP, NetPatternMultiCreate, NetPatternMultiSLToPat,
	NetPatternMultiSetInput, NetPatternMultiDestroy,
	NetPatternizerMultiGraph, NetPatternizerMultiDestroy
};

// ROM 0x0037404c NetPatternAspectNormT
const NetPatternizerType	NetPatternAspectNormT = {
	"Image Aspect Normalized", (long) sizeof(NetScalarPatternizer),
	NetPatternizerAspectNormInitFromBP, NetPatternScalarCreate, NetPatternAspectNormSLToPat,
	NetPatternScalarSetInput, NetPatternScalarDestroy,
	NetPatternizerScalarGraph, NetPatternizerScalarDestroy
};

// ROM 0x00374070 NetPatternHeightT
const NetPatternizerType	NetPatternHeightT = {
	"Image Height", (long) sizeof(NetScalarPatternizer),
	NetPatternizerHeightInitFromBP, NetPatternScalarCreate, NetPatternHeightSLToPat,
	NetPatternScalarSetInput, NetPatternScalarDestroy,
	NetPatternizerScalarGraph, NetPatternizerScalarDestroy
};

// ROM 0x00374094 NetPatternCapHeightT
// (the ROM gives this one the same name as the height patternizer)
const NetPatternizerType	NetPatternCapHeightT = {
	"Image Height", (long) sizeof(NetScalarPatternizer),
	NetPatternizerCapHeightInitFromBP, NetPatternScalarCreate, NetPatternCapHeightSLToPat,
	NetPatternScalarSetInput, NetPatternScalarDestroy,
	NetPatternizerScalarGraph, NetPatternizerScalarDestroy
};

// ROM 0x003740b8 NetPatternBaseT
const NetPatternizerType	NetPatternBaseT = {
	"Image Base", (long) sizeof(NetScalarPatternizer),
	NetPatternizerBaseInitFromBP, NetPatternScalarCreate, NetPatternBaseSLToPat,
	NetPatternScalarSetInput, NetPatternScalarDestroy,
	NetPatternizerScalarGraph, NetPatternizerScalarDestroy
};

// ROM 0x003740dc NetPatternCountT
const NetPatternizerType	NetPatternCountT = {
	"Stroke Count", (long) sizeof(NetScalarPatternizer),
	NetPatternizerCountInitFromBP, NetPatternScalarCreate, NetPatternCountSLToPat,
	NetPatternScalarSetInput, NetPatternScalarDestroy,
	NetPatternizerScalarGraph, NetPatternizerScalarDestroy
};

// ROM 0x00374100 NetPatternStrokePUDT
const NetPatternizerType	NetPatternStrokePUDT = {
	// (the ROM's is 0x18)
	"PenUpStroke", (long) sizeof(NetStrokePatternizer),
	NetPatternizerStrokeInitFromBP, NetPatternStrokeCreate, NetPatternStrokePUDSLToPat,
	NetPatternStrokeSetInput, NetPatternStrokeDestroy,
	NetPatternizerStrokeGraph, NetPatternizerStrokeDestroy
};


// ROM 0x00373fb0 (unnamed)
// The names an input group may be given, and the class each one is.
const NetPatternTypeEntry	kNetPatternTypes[kNetPatternTypeCount] = {
	{ "AspectNorm",			0, &NetPatternAspectNormT },
	{ "Base",				0, &NetPatternBaseT },
	{ "Height",				0, &NetPatternHeightT },
	{ "CapHeight",			0, &NetPatternCapHeightT },
	{ "StrokeCount",		0, &NetPatternCountT },
	{ "ImageSplatLimited",	1, &NetPatternImageT },
	{ "StrokePUD",			0, &NetPatternStrokePUDT }
};


// ROM 0x00131f5c NetPatternLookup
long
NetPatternLookup(const char* name)
{
	long found = -1;
	for (ULong i = 0; i < (ULong) kNetPatternTypeCount; i++)
		if (strcmp(name, kNetPatternTypes[i].fName) == 0)
		{
			found = (long) (short) i;
			break;
		}
	if (found == -1)
	{
		char message[512];
		sprintf(message, "Unknown pattern type \"%s\"", name);
		Throw(exRosetta, (void*) 1, nil);
	}
	return found;
}


// ROM 0x00133ae8 NetPatternizerCreateForGrid
// The patternizer for one of the net's input groups, by the name the
// net gives it.
NetPatternizer*
NetPatternizerCreateForGrid(BPNet* net, ULong group)
{
	if (group >= (ULong) net->fGroupCount)
		return nil;
	long which = NetPatternLookup(net->fInputType[group]);
	if (which < 0)
		return nil;
	const NetPatternTypeEntry* entry = &kNetPatternTypes[which];
	if (entry->fType == nil)
		return nil;

	NetPatternizer* self = NetPatternizerNewInstance(entry->fType);
	newton_try
	{
		entry->fType->fInitFromBP(self, net, (long) group, entry->fFlag);
	}
	cleanup
	{
		NetPatternizerDestroy(self);
	}
	end_try;
	return self;
}


// ROM 0x00133980 NetPatternizerCreateFromBP
// The whole set for a net: a Multi holding one child per input group.
NetPatternizer*
NetPatternizerCreateFromBP(BPNet* net)
{
	NetPatternizer* self = NetPatternizerNewInstance(&NetPatternMultiT);
	newton_try
	{
		self->fType->fInitFromBP(self, net, 0, 0);
	}
	cleanup
	{
		NetPatternizerDestroy(self);
	}
	end_try;
	return self;
}
