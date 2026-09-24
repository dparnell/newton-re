/*
	File:		recognition/NetPattern.cpp

	Contains:	The patternizers - see NetPattern.h.

	The framework, the composite and the five scalars.  The two that do
	the real work, `ImageSplatLimited` and `StrokePUD`, are NOT YET and
	their type records name entry points that are not there yet.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "NetPattern.h"
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
	NOT YET: the two that do the real work.
--------------------------------------------------------------------*/

// ROM 0x00132078 NetPatternizerImageInitFromBP
static void	NetPatternizerImageInitFromBP(NetPatternizer*, BPNet*, long, long)	{ }
// ROM 0x00132108 NetPatternImageCreate
static NetPattern*	NetPatternImageCreate(NetPatternizer*)			{ return nil; }
// ROM 0x00132200 NetPatternImageSLToPat
static void	NetPatternImageSLToPat(BPNet*, RosStrokeList*, NetPattern*, Fixed, Fixed, Fixed,
								Fixed, Fixed, Fixed, Fixed, Fixed, Fixed)	{ }
// ROM 0x0013243c NetPatternImageSetInput
static void	NetPatternImageSetInput(NetPattern*)					{ }
// ROM 0x001324e8 NetPatternImageDestroy
static void	NetPatternImageDestroy(NetPattern*)						{ }
// ROM 0x00132104 NetPatternizerImageGraph
static void	NetPatternizerImageGraph(NetPatternizer*)				{ }
// ROM 0x001320e4 NetPatternizerImageDestroy
static void	NetPatternizerImageDestroy(NetPatternizer*)				{ }

// ROM 0x001330d4 NetPatternizerStrokeInitFromBP
static void	NetPatternizerStrokeInitFromBP(NetPatternizer*, BPNet*, long, long)	{ }
// ROM 0x00133130 NetPatternStrokeCreate
static NetPattern*	NetPatternStrokeCreate(NetPatternizer*)			{ return nil; }
// ROM 0x0013322c NetPatternStrokePUDSLToPat
static void	NetPatternStrokePUDSLToPat(BPNet*, RosStrokeList*, NetPattern*, Fixed, Fixed, Fixed,
									Fixed, Fixed, Fixed, Fixed, Fixed, Fixed)	{ }
// ROM 0x001338c0 NetPatternStrokeSetInput
static void	NetPatternStrokeSetInput(NetPattern*)					{ }
// ROM 0x00133950 NetPatternStrokeDestroy
static void	NetPatternStrokeDestroy(NetPattern*)					{ }
// ROM 0x0013312c NetPatternizerStrokeGraph
static void	NetPatternizerStrokeGraph(NetPatternizer*)				{ }
// ROM 0x00133128 NetPatternizerStrokeDestroy
static void	NetPatternizerStrokeDestroy(NetPatternizer*)			{ }


#pragma mark -
/*--------------------------------------------------------------------
	The classes themselves, and the table a name is looked up in.
--------------------------------------------------------------------*/

// ROM 0x00374004 NetPatternImageT
const NetPatternizerType	NetPatternImageT = {
	"BasicImage", 0x28,
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
	"PenUpStroke", 0x18,
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
