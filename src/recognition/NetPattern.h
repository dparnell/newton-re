/*
	File:		recognition/NetPattern.h

	Contains:	The patternizers: what turns a piece of writing into
				the classifier's 384 inputs.

				A **patternizer** knows how to measure one thing about
				a stroke list and where in the net's input array to put
				the answer; a **pattern** is one measurement it has
				taken.  They are a little class system written in C:
				every object begins with a pointer to its type, and the
				type is a name, the size of an instance and eight entry
				points (`NetPatternizerType` below).  There are seven
				kinds, named by the strings in the net's `inputType`.

				The ROM's own net has four input groups, and they come
				to exactly its 384 inputs:

				| | kind | inputs | at |
				|---|---|---|---|
				| 0 | `ImageSplatLimited` | 14 x 14 = 196 | 0 |
				| 1 | `StrokePUD` | 20 x 9 = 180 | 196 |
				| 2 | `AspectNorm` | 1 | 376 |
				| 3 | `StrokeCount` | 7 | 377 |

				So what the classifier is shown is a fourteen-by-
				fourteen picture of the writing, a twenty-by-nine grid
				of where the pen went, how wide the writing is against
				how tall, and how many strokes it took - and nothing
				else.

				`NetPatternizerCreateFromBP` makes the lot: a
				`Multi-Input Grid` patternizer holding one child per
				group, made by looking each group's name up in the
				table.  `WordRecogNetSetInputs` then asks it for a
				pattern, has it measure the strokes (`SLToPat`) and has
				the pattern write itself into the net (`SetInput`).

				A **scalar** patternizer is the simple case and shows
				how a measurement reaches the net.  Its value is 0 to 1
				in 16.16, and it is written either as one byte of
				brightness (when the group is a single input) or as a
				*one-hot* over the group's cells (when it is more than
				one), which is what `NetPatternSetNth` does.

	NOT YET: the two that do the real work - `ImageSplatLimited`, which
	draws the strokes into a grid through the anti-aliased renderer
	(`RenderAA*`, ROM 0x001a69b0), and `StrokePUD`.  Neither the segment
	layer nor the feature extraction is there either, so nothing drives
	these from a real piece of writing yet; a stroke list drives them
	directly, which is what the test does.

	Reconstructed from the MP2x00 US ROM (0x00131f5c-0x00133c7c); each
	function cites its origin.
*/

#ifndef __NETPATTERN_H
#define __NETPATTERN_H

#ifndef __BPNET_H
#include "BPNet.h"
#endif

#ifndef __ROSSTROKES_H
#include "RosStrokes.h"
#endif

struct NetPatternizer;
struct NetPattern;
struct NetPatternizerType;


// What a patternizer is asked to measure.  `strokes` is the writing,
// `pattern` is what the answer goes in, and the rest is the geometry
// the layers above have worked out about the line it is written on.
// The ROM passes twelve arguments; the four that anything reconstructed
// so far reads are named and the rest are kept so that the call is the
// ROM's.
typedef void (*NetPatternSLToPatProc)(BPNet* net, RosStrokeList* strokes, NetPattern* pattern,
									Fixed base, Fixed height, Fixed arg6, Fixed altBase,
									Fixed altHeight, Fixed arg9, Fixed arg10, Fixed arg11,
									Fixed capHeight);

// The class of a patternizer: 0x24 bytes in the ROM, and an object's
// first word points at it.
struct NetPatternizerType
{
	const char*		fName;			// +0x00
	long			fSize;			// +0x04  how big an instance is
	// a patternizer made over one of the net's input groups
	void			(*fInitFromBP)(NetPatternizer* self, BPNet* net, long group, long flag);	// +0x08
	NetPattern*		(*fCreate)(NetPatternizer* self);		// +0x0c
	NetPatternSLToPatProc	fSLToPat;						// +0x10
	void			(*fSetInput)(NetPattern* pattern);		// +0x14
	void			(*fPatternDestroy)(NetPattern* pattern);	// +0x18
	void			(*fGraph)(NetPatternizer* self);		// +0x1c  drawing, for looking at
	void			(*fDestroy)(NetPatternizer* self);		// +0x20
};

// Every patternizer starts with these two; what follows is its own.
struct NetPatternizer
{
	const NetPatternizerType*	fType;	// +0x00
	long			fRefCount;			// +0x04  the patterns made from it
};

// ... and every pattern with these two.
struct NetPattern
{
	NetPatternizer*	fPatternizer;		// +0x00
	UByte			fField04;			// +0x04  0x3f when new
	UByte			fPad05[3];
};

// One measurement between nought and one, written into the net either
// as a single byte of brightness or as a one-hot over the group's
// cells.  The ROM's patternizer is 0x14 bytes and its pattern 0x0c.
struct NetScalarPatternizer
{
	const NetPatternizerType*	fType;
	long			fRefCount;
	long			fCount;			// +0x08  how many input cells
	UByte*			fInputs;		// +0x0c  where they are in the net
	UByte			fOn;			// +0x10  what a lit cell holds
	UByte			fOff;			// +0x11  ... and an unlit one
	UByte			fPad12[2];
};

struct NetScalarPattern
{
	NetPatternizer*	fPatternizer;
	UByte			fField04;
	UByte			fPad05[3];
	Fixed			fValue;			// +0x08  0 to 1
};

// The composite: one child per input group of the net.  The ROM's
// patternizer is 0x10 bytes and so is its pattern.
struct NetMultiPatternizer
{
	const NetPatternizerType*	fType;
	long			fRefCount;
	ULong			fCount;			// +0x08
	NetPatternizer**	fChildren;	// +0x0c
};

struct NetMultiPattern
{
	NetPatternizer*	fPatternizer;
	UByte			fField04;
	UByte			fPad05[3];
	ULong			fCount;			// +0x08
	NetPattern**	fChildren;		// +0x0c
};


// The seven kinds, by the name an input group is given in `inputType`.
extern const NetPatternizerType	NetPatternImageT;			// ROM 0x00374004 NetPatternImageT
extern const NetPatternizerType	NetPatternMultiT;			// ROM 0x00374028 NetPatternMultiT
extern const NetPatternizerType	NetPatternAspectNormT;		// ROM 0x0037404c NetPatternAspectNormT
extern const NetPatternizerType	NetPatternHeightT;			// ROM 0x00374070 NetPatternHeightT
extern const NetPatternizerType	NetPatternCapHeightT;		// ROM 0x00374094 NetPatternCapHeightT
extern const NetPatternizerType	NetPatternBaseT;			// ROM 0x003740b8 NetPatternBaseT
extern const NetPatternizerType	NetPatternCountT;			// ROM 0x003740dc NetPatternCountT
extern const NetPatternizerType	NetPatternStrokePUDT;		// ROM 0x00374100 NetPatternStrokePUDT

// One row of the table a group's name is looked up in.
struct NetPatternTypeEntry
{
	const char*		fName;
	long			fFlag;			// the Image patternizer's "limited"
	const NetPatternizerType*	fType;
};
const long	kNetPatternTypeCount	= 7;
extern const NetPatternTypeEntry	kNetPatternTypes[kNetPatternTypeCount];	// ROM 0x00373fb0 (unnamed)


// Which of the seven a name is; throws `evt.ex.Rosetta` if it is none
// of them.
long	NetPatternLookup(const char* name);					// ROM 0x00131f5c NetPatternLookup

// A patternizer of that class, with its reference count at one.
NetPatternizer*	NetPatternizerNewInstance(const NetPatternizerType* type);	// ROM 0x00133bb8 NetPatternizerNewInstance
void	NetPatternizerInit_(NetPatternizer* self, const NetPatternizerType* type);	// ROM 0x00133c24 NetPatternizerInit_
void	NetPatternInit_(NetPattern* self, NetPatternizer* patternizer);	// ROM 0x00133c34 NetPatternInit_

// The whole set for a net: a Multi holding one child per input group.
NetPatternizer*	NetPatternizerCreateFromBP(BPNet* net);		// ROM 0x00133980 NetPatternizerCreateFromBP
// ... and one child, from the name the net gives that group.
NetPatternizer*	NetPatternizerCreateForGrid(BPNet* net, ULong group);	// ROM 0x00133ae8 NetPatternizerCreateForGrid

// A pattern from a patternizer, which keeps a reference to it.
NetPattern*	NetPatternCreate(NetPatternizer* patternizer);	// ROM 0x00133c44 NetPatternCreate
// The pattern written into the net's inputs.
void	NetPatternSetInput(NetPattern* pattern);			// ROM 0x00133c70 NetPatternSetInput
void	NetPatternDestroy(NetPattern* pattern);				// ROM 0x00133a08 NetPatternDestroy
long	NetPatternizerDestroy(NetPatternizer* patternizer);	// ROM 0x00133a58 NetPatternizerDestroy
void	NetPatternizerUpdateGraphics(NetPatternizer* patternizer);	// ROM 0x00133a50 NetPatternizerUpdateGraphics

// `count` cells filled with `off` and the `index`th with `on` - the
// one-hot a scalar becomes when its group is more than one input.
void	NetPatternSetNth(UByte* cells, long count, long index, UByte on, UByte off);	// ROM 0x00133aa8 NetPatternSetNth

#endif	/* __NETPATTERN_H */
