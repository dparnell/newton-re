/*
	File:		Chunk.h

	Contains:	The cursive reader's digit and number reader - ParaGraph's
				"chunk" reader - as far as it is reconstructed: the context
				GCTryToRecognize keeps for it and the recognition
				configuration it changes while a word it took for a number
				is read.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	In a field that allows numbers (rc +0xb6) the chunk reader goes
	first: `ChunkProcessor` cuts the writing into chunks, reads the
	digits in them (`Digits`, `SearchDigit_S/K/L`, `New_SearchDigit_V`,
	`SearchNumber`, `FindPound`, `RecognizeZCCW`, ...) and, when it found
	a number, `ChunkModifyRC` narrows the configuration to digits for the
	xr reader; after it `ChunkRestoreRC` puts it back, `ChunkSortAnswers`
	and `ChunkCorrectByLexDB` merge the number readings in.

	NOT YET RECONSTRUCTED: `ChunkProcessor` and most of what is under it
	(about 100 functions, 146 KB - docs/next-steps.md has the plan),
	`ChunkPatchXrdata`, `ChunkSortAnswers` and `ChunkCorrectByLexDB`.
	Without the processor no number is ever found, so GCTryToRecognize
	gives the context back straight away, as the ROM does for a word that
	is not a number.
*/

#ifndef __CHUNK_H
#define __CHUNK_H

#include "XrDomains.h"

struct xrdata_type;

// The chunk reader's context (ROM 0x48 bytes; DEVIATION: sizeof on the
// host, whose pointers are wider).
struct ChunkCtx
{
	void*			fData;			// +00  (given back by ChunkCleanUp)
	long			f04;
	void*			fData2;			// +08  (given back)
	void*			fData3;			// +0c  (given back)
	long			f10;
	long			fNumbers;		// +14  whether the processor found numbers (IsChunkNumbers)
	long			fNumbersOnly;	// +18  the writing is numbers alone (ChunkModifyRC's second way)
	long			f1C;
	long			fModified;		// +20  the configuration changed (ChunkModifyRC), to be put back
	long			f24;
	long			f28;
	long			f2C;
	UShort			fSaved[5];		// +30  rc +0x02, +0x08, +0x0a, +0x00, +0x90 as they were
	rc_type*		fRC;			// +3c
	xrdata_type*	fXr;			// +40
	rec_w_type*		fReadings;		// +44
};

// A point of the digit reader's own trace (ROM tag_WORD_TRACE, 8 bytes, no
// pointers): the point, y -1 for a pen-up, then a word of flags - what
// ExtrWordTrace_V marks it as (kTraceLow, kTraceHigh) and where
// GetLineApprox finds a stroke begins and ends (kTraceStrokeStart,
// kTraceStrokeEnd).
struct tag_WORD_TRACE
{
	short		x;					// +00
	short		y;					// +02  -1: the pen was lifted
	int32_t		fFlags;				// +04
};
static_assert(sizeof(tag_WORD_TRACE) == 8, "a tag_WORD_TRACE is 8 bytes, as in the ROM");

enum
{
	kTraceLow			= 0x01,		// a turn at the bottom (y at its greatest, the screen's y growing down)
	kTraceHigh			= 0x02,		// a turn at the top
	kTraceStrokeStart	= 0x04,		// the point after a pen-up
	kTraceStrokeEnd		= 0x08		// the point before one
};

// A node of the polyline GetLineApprox fits to the trace (ROM
// tag_wapx_type, 0x1c bytes, no pointers).  fDir is the direction
// (GetDirection's fifteen-degree steps) the line comes into the node from
// - at a corner (kApxCorner) the two directions either side packed as
// in | out << 8 - and fDirOut the one it leaves in.
struct tag_wapx_type
{
	int32_t		fIndex;				// +00  the trace point
	UByte		fFlags;				// +04  kApx...
	UByte		fFirst;				// +05  the first node a split made in its segment (GetLineApprox)
	UByte		f06[2];
	int32_t		fDir;				// +08
	int32_t		fDirOut;			// +0c
	int32_t		x;					// +10  the trace point's
	int32_t		y;					// +14
	int32_t		f18;
};
static_assert(sizeof(tag_wapx_type) == 0x1c, "a tag_wapx_type is 0x1c bytes, as in the ROM");

enum
{
	kApxStart		= 0x01,			// the trace point was a stroke's start (kTraceStrokeStart)
	kApxEnd			= 0x02,			// a stroke's end (kTraceStrokeEnd)
	kApxHigh		= 0x04,			// a turn at the top (kTraceHigh)
	kApxLow			= 0x08,			// a turn at the bottom (kTraceLow)
	kApxSegStart	= 0x10,			// the first node of a segment between two marked points
	kApxSegEnd		= 0x20,			// the last
	kApxCorner		= 0x40			// the direction turns by 120 degrees or more (SetAllDirections)
};

// A chunk: a piece of the polyline between two marked points (a stroke's
// start or end, a turn at the top or bottom), or the pen's jump between
// two strokes (kind 3) - ROM tag_CHUNK, 0x94 bytes, no pointers.  A
// polyline node's f18 is the chunk it belongs to, from one (negative at
// the last node of a chunk the next one continues from).
struct tag_CHUNK
{
	int32_t		fFrom;				// +00  the first node
	int32_t		fTo;				// +04  the last
	int32_t		fKind;				// +08  1 it goes up, 2 down, 3 the jump between strokes
	int32_t		f0C;
	UByte		fDir;				// +10  GetDirection from its start to its end
	UByte		f11[3];
	int32_t		fLeft;				// +14
	int32_t		fTop;				// +18
	int32_t		fRight;				// +1c
	int32_t		fBottom;			// +20
	int32_t		fTopNode;			// +24  the nodes the extremes are at
	int32_t		fRightNode;			// +28
	int32_t		fLeftNode;			// +2c
	int32_t		fBottomNode;		// +30
	int32_t		fWidth;				// +34
	int32_t		fHeight;			// +38
	int32_t		fX0, fY0;			// +3c  its start
	UByte		fZoneStart;			// +44  where its start is in the line: 60 above the middle half, 45 in it, 30 below (DefHeightsForNumber; only a stroke's first chunk)
	UByte		f45[3];
	int32_t		fX1, fY1;			// +48  its end
	UByte		fZoneEnd;			// +50  where its end is
	UByte		f51[3];
	int32_t		fMidX, fMidY;		// +54  the node its segment was first split at (-1: none)
	int32_t		fLength2;			// +5c  the square of the chord's length
	int32_t		fBulge;				// +60  the square of the middle node's distance from the chord
	int32_t		fPrev;				// +64  the chunk this one continues (-1: a stroke's first)
	int32_t		fNext;				// +68  the one that continues it, plus one (-1: a stroke's last)
	int32_t		f6C;
	int32_t		f70;
	int32_t		f74;
	int32_t		f78;
	int32_t		f7C;				// +7c  the circle GetCircles found starting here (its object; -1 none)
	int32_t		fRealIndex;			// +80  its number among the chunks that are not jumps
	int32_t		fFirstBracket;		// +84  its brackets (ApxToBrackets)
	int32_t		fLastBracket;		// +88
	int32_t		fStroke;			// +8c  the stroke it is in (ChunkMakeStrokes)
	UByte		f90;
	UByte		f91;
	UByte		f92[2];
};
static_assert(sizeof(tag_CHUNK) == 0x94, "a tag_CHUNK is 0x94 bytes, as in the ROM");

// An object the digit searchers found (ROM tag_LOWOBJ, 0x3c bytes, no
// pointers): what class it is, the nodes it spans and their box, and its
// links in its class's list (or the free list).
struct tag_LOWOBJ
{
	int32_t		fClass;				// +00  the class id (100..800, 1100..2200)
	UByte		fGroupCount;		// +04  the objects after it in its group
	UByte		fGroupIndex;		// +05  its place in a group
	UByte		fChunks;			// +06  the chunks its nodes run through
	UByte		f07;
	int32_t		fFrom;				// +08  the first node
	int32_t		fTo;				// +0c  the last
	int32_t		fFirstPoint;		// +10  their trace points
	int32_t		fLastPoint;			// +14
	int32_t		fLeft;				// +18
	int32_t		fTop;				// +1c
	int32_t		fRight;				// +20
	int32_t		fBottom;			// +24
	int32_t		fValue;				// +28
	int32_t		fPrev;				// +2c
	int32_t		fNext;				// +30
	int32_t		fExtra;				// +34
	int32_t		f38;
};
static_assert(sizeof(tag_LOWOBJ) == 0x3c, "a tag_LOWOBJ is 0x3c bytes, as in the ROM");

// A class's list: how many, the first and last, and a cursor.
struct LOClassRec
{
	int32_t		fCount;
	int32_t		fFirst;
	int32_t		fLast;
	int32_t		fCur;
	int32_t		fCurN;
};

const long	kLOMaxObjects	= 300;

// The list of low objects (the ROM's 0x482c-byte block).  DEVIATION: the
// host's layout is its own, the two pointers (+0x24, +0x28) being wider;
// the ROM offsets are noted.
struct LOBlock
{
	int32_t		fCount;				// +00  the objects taken
	int32_t		f04;
	int32_t		fClass;				// +08  the class worked in
	LOClassRec	fWork;				// +0c  its record, taken out
	int32_t		fGroup;				// +20  numbering a group (0: not)
	tag_LOWOBJ*	fGroupObj;			// +24  the group's last object
	Ptr			fData;				// +28  800 bytes
	int32_t		f2C;
	int32_t		fFree;				// +30
	int32_t		fFreeHead;			// +34
	int32_t		fFreeTail;			// +38
	int32_t		f3C;				// +3c  (the free head, again)
	int32_t		f40;
	LOClassRec	fClasses[20];		// +44  100, 200, ... 800, 1100, ... 2200
	int32_t		f1D4, f1D8;
	tag_LOWOBJ	fObjects[kLOMaxObjects];	// +1dc
};

// A box (ROM tag_BOX).
struct tag_BOX
{
	int32_t		left, top, right, bottom;
};

// A bracket: a line (kind 1) or an arc (kind 2, turning fSign's way) a
// chunk is drawn with - ROM brack_type, 0x1c bytes, no pointers.
struct brack_type
{
	int32_t		fChunk;				// +00  (-1: dropped)
	int32_t		fFrom;				// +04  the nodes
	int32_t		fTo;				// +08
	int32_t		fKind;				// +0c  0 one node, 1 a line, 2 an arc
	int32_t		fSign;				// +10  SgnArc's
	int32_t		fLength2;			// +14  L2Arc: the chord's length squared
	int32_t		fHeight2;			// +18  H2Arc: the bulge squared
};
static_assert(sizeof(brack_type) == 0x1c, "a brack_type is 0x1c bytes, as in the ROM");

// A stroke as the chunks make it (ROM tag_STK, 0x30 bytes, no pointers).
struct tag_STK
{
	int32_t		fFirstChunk;		// +00
	int32_t		fLastChunk;			// +04
	int32_t		fTopNode;			// +08
	int32_t		fRightNode;			// +0c
	int32_t		fLeftNode;			// +10
	int32_t		fBottomNode;		// +14
	int32_t		fLeft;				// +18
	int32_t		fTop;				// +1c
	int32_t		fRight;				// +20
	int32_t		fBottom;			// +24
	int32_t		fWidth;				// +28
	int32_t		fHeight;			// +2c
};
static_assert(sizeof(tag_STK) == 0x30, "a tag_STK is 0x30 bytes, as in the ROM");

const long	kMaxChunks	= 100;

// Everything the digit reader works from (ROM tag_CHUNK_STAFF, on
// ChunkProcessor's stack).  DEVIATION: the host's layout is its own, its
// pointers being wider; the ROM offsets are noted.
struct tag_CHUNK_STAFF
{
	void*			fLO;			// +00  the list of low objects
	tag_WORD_TRACE*	fTrace;			// +04
	int32_t			fTraceCount;	// +08
	tag_wapx_type*	fNodes;			// +0c  the polyline
	int32_t			fNodeCount;		// +10
	tag_CHUNK*		fChunks;		// +14
	int32_t			fChunkCount;	// +18
	tag_STK*		fStrokes;		// +1c
	int32_t			fStrokeCount;	// +20
	brack_type*		fBrackets;		// +24
	int32_t			fBracketCount;	// +28
	int32_t*		fRealChunks;	// +2c  the chunks that are not jumps
	int32_t			fRealCount;		// +30
	int32_t			f34;			// +34  100
	int32_t			f38;			// +38  the brackets' room
	int32_t			f3C;			// +3c  the real chunks' room
	int32_t			fHeight;		// +40  the writing's height: fBottomLine - fTopLine (DefHeightsForNumber)
	int32_t			fTopLine;		// +44  the mean top of the strokes' boxes
	int32_t			fBottomLine;	// +48  the mean bottom
	int32_t			f4C;
	int32_t			f50;			// +50  rc +4 bit 16
	int32_t			f54;
	int32_t			f58;
	int32_t			f5C;			// +5c  0x18
	UByte			fDigits[10];	// +60  per digit, the letter-table variants allowed (a bit each)
};

long	SgnArc(tag_wapx_type* nodes, long a, long b, long c);			// ROM 0x002a7cb0 SgnArc__FP13tag_wapx_typeiN22
long	H2Arc(tag_wapx_type* nodes, long a, long b);						// ROM 0x002a7d18 H2Arc__FP13tag_wapx_typeiT2
long	L2Arc(tag_wapx_type* nodes, long a, long b);						// ROM 0x002a7d9c L2Arc__FP13tag_wapx_typeiT2
void	GetBox(tag_wapx_type* nodes, long a, long b, tag_BOX* box);		// ROM 0x002a7e24 GetBox__FP13tag_wapx_typeiT2P7tag_BOX
long	CrossArcs(tag_wapx_type* nodes, long kind1, long a0, long a1, long kind2, long b0, long b1);	// ROM 0x002a7a84 CrossArcs__FP13tag_wapx_typeiN52
long	PreservNextSgn(tag_wapx_type* nodes, brack_type* brackets, long i);	// ROM 0x002a7b2c PreservNextSgn__FP13tag_wapx_typeP10brack_typei
long	GetAngleBetweenTwoDir(ULong a, ULong b);						// ROM 0x00286834 GetAngleBetweenTwoDir__FUiT1
long	midL2Chunks(tag_CHUNK* chunks, long count);						// ROM 0x002a7dd0 midL2Chunks__FP9tag_CHUNKi
long	ChunkFillMainData(tag_CHUNK* chunks, tag_wapx_type* nodes, long count);	// ROM 0x00287f60 ChunkFillMainData__FP9tag_CHUNKP13tag_wapx_typei
long	ChunkMakeStrokes(tag_CHUNK* chunks, tag_wapx_type* nodes, long count, tag_STK** strokes, long* strokeCount);	// ROM 0x002884c8 ChunkMakeStrokes__FP9tag_CHUNKP13tag_wapx_typeiPP7tag_STKPi
long	CreateRealChunkInd(tag_CHUNK* chunks, long count, int32_t** real);	// ROM 0x00287e7c CreateRealChunkInd__FP9tag_CHUNKiPPi
long	ApxToBrackets(tag_wapx_type* nodes, tag_CHUNK* chunks, long count, brack_type** brackets);	// ROM 0x00286a54 ApxToBrackets__FP13tag_wapx_typeP9tag_CHUNKiPP10brack_type
long	ApxToCLine(tag_wapx_type* nodes, brack_type* brackets, long count, tag_CHUNK* chunks, long chunkCount);	// ROM 0x00287aa8 ApxToCLine__FP13tag_wapx_typeP10brack_typeiP9tag_CHUNKT3
long	ChunkPutClassesToLO(void* lo, tag_wapx_type* nodes, tag_CHUNK* chunks, long count);	// ROM 0x00287d48 ChunkPutClassesToLO__FPvP13tag_wapx_typeP9tag_CHUNKi
long	DefRectForChunks(tag_CHUNK* chunks, tag_wapx_type* nodes, long first, long last, tag_BOX* r);	// ROM 0x00287de0 DefRectForChunks__FP9tag_CHUNKP13tag_wapx_typeiT3P5_RECT
// The writing's line: the staff's fTopLine, fBottomLine and fHeight (the
// mean top and bottom of the strokes' boxes, the small ones dropped or
// joined to a neighbour) and each chunk's ends placed in it (fZoneStart,
// fZoneEnd).
void	DefHeightsForNumber(tag_CHUNK_STAFF* staff);					// ROM 0x002853ec DefHeightsForNumber__FP15tag_CHUNK_STAFF
// The circles (an 0, the loop of a 6, 8 or 9) put in the list of low
// objects as class 200, each one's object kept in the chunk it starts at
// (f7C).  ==> 1.
long	GetCircles(tag_CHUNK_STAFF* staff);								// ROM 0x00288d4c GetCircles__FP15tag_CHUNK_STAFF
// The searchers' geometry.  direct_suits: whether a direction lies from lo
// round to hi (wrapping past straight up when hi is below lo);
// distance_between_directions: the steps between two, the short way;
// take_next_point/take_prev_point: the first node after (before) start
// further from it than the limits across, up or down, or in all (0: no
// limit; none at all takes the first node), -1 for none; x_in_line: where
// a line is at a height (-1 when it is level); x_in_curve: where a chunk's
// polyline is; cross_with_line: whether a segment crosses a chunk.
long	direct_suits(long dir, long lo, long hi);						// ROM 0x002a7fd0 direct_suits__FiN21
long	distance_between_directions(long a, long b);					// ROM 0x002a83d8 distance_between_directions__FiT1
long	take_next_point(tag_wapx_type* n, long end, long start, long dx, long dy, long sum);	// ROM 0x002a7868 take_next_point__FP13tag_wapx_typeiN42
long	take_prev_point(tag_wapx_type* n, long start, long dx, long dy, long sum);				// ROM 0x002a7980 take_prev_point__FP13tag_wapx_typeiN32
long	x_in_line(long x1, long y1, long x2, long y2, long y);			// ROM 0x002a82a4 x_in_line__FiN41
long	x_in_curve(tag_wapx_type* n, tag_CHUNK* c, long y);				// ROM 0x002a8174 x_in_curve__FP13tag_wapx_typeP9tag_CHUNKi
long	cross_with_line(tag_wapx_type* n, tag_CHUNK* c, long x1, long y1, long x2, long y2);	// ROM 0x002a82f4 cross_with_line__FP13tag_wapx_typeP9tag_CHUNKiN33

// The digits whose main stroke is a curve down and round (a chunk of
// class 500, value 501) - 2, 3, 4, 5, 7, 9 and the $ - put in the list of
// low objects as class 1300, value 1400 + the digit (0x15 for $), extra 1
// found whole, 2 with a separate bar, 3 from a bar.  ==> 1.
long	SearchDigit_L(tag_CHUNK_STAFF* staff);							// ROM 0x0028dd18 SearchDigit_L__FP15tag_CHUNK_STAFF
// The digits made of lines and arcs - 1, 4 (41 with an open top), 7, 8,
// x, # and % - put in the list of low objects as class 1300, value 1500 +
// the digit.  ==> the last 8's test's answer, -1 with no curve to try.
long	SearchDigit_K(tag_CHUNK_STAFF* staff);							// ROM 0x00289604 SearchDigit_K__FP15tag_CHUNK_STAFF
// The digits and signs found chunk by chunk: each chunk that is not a pen
// jump asked, by its class, what it starts - an upright line (300) a 1, a
// 7 or an "H"; a curve down (500) an 8, 1, 7, 9 or 2; an arc (400) a 0 or 9
// from its circle, a 6, 9, 5, 7, 2 or 8; an S (700) a 2, 5 or 3; three
// brackets (1400) a 3; two short sections a sign - put in the list of low
// objects as class 1300, value 1300 + the digit (or the sign's code: 71
// #, 72 H, 81, 99, 23, 24), a grey 9 as class 2200.  ==> -1.
long	New_SearchDigit_V(void* lo, tag_WORD_TRACE* trace, long traceCount, tag_wapx_type* n, tag_CHUNK* chunks, brack_type* brackets,
						  int32_t* real, long chunkCount, long realCount, tag_BOX box, tag_STK* strokes, long strokeCount, long height);	// ROM 0x00296e04 New_SearchDigit_V__FPvP14tag_WORD_TRACEiP13tag_wapx_typeP9tag_CHUNKP10brack_typePiN237tag_BOXP7tag_STKN23
// Whether the writing is a number, judged from the digits the second looks
// wrote out (class 1900): how many digits and other codes there are and
// how many chunks they took, how their heights, tops and bottoms step from
// one to the next, and (with staff f54 set, the chunks' f6C marked with
// what each was read as) whether a line read as a 1 is a letter's stem.
// Signs coded 13 that belong to a neighbouring stroke are taken out on
// the way.  ==> 1 for a number.
long	SearchNumber(tag_CHUNK_STAFF* staff);							// ROM 0x002a28d0 SearchNumber__FP15tag_CHUNK_STAFF
// The polyline's nodes from..to as a trace of their own between pen-ups.
long	ComposeTrace(tag_wapx_type* n, long from, long to, tag_WORD_TRACE* trace);	// ROM 0x0029bac0 ComposeTrace__FP13tag_wapx_typeiT2P14tag_WORD_TRACE
// The direction from node start to the first node after it take_next_point
// finds (the node before end with none); to node start from the first
// before it take_prev_point finds (the first node with none).
long	find_direct_forward(tag_wapx_type* n, long end, long start, long dx, long dy, long sum);	// ROM 0x002a8090 find_direct_forward__FP13tag_wapx_typeiN42
long	find_direct_backward(tag_wapx_type* n, long start, long dx, long dy, long sum);				// ROM 0x002a810c find_direct_backward__FP13tag_wapx_typeiN32
// Whether two segments meet (their ends, and nodes a-b and c-d's).
long	CheckQIntersecXY(long x1, long y1, long x2, long y2, long x3, long y3, long x4, long y4);	// ROM 0x002a7ee8 CheckQIntersecXY__FiN71
long	CheckQIntersec(tag_wapx_type* n, long a, long b, long c, long d);							// ROM 0x002a8020 CheckQIntersec__FP13tag_wapx_typeiN32
// Digits' second looks at the digits found (count objects of class 1300,
// value 1300 + the digit mod 100): a 3 that turns back sharply at its
// left - a 5 whose bar was not lifted - made 1305 (ThreeToFive: ==> 0
// with no digits, else 1); a curve down that turns the other way and an
// arc up made a 6, or an 8 whose closing line misses its start a 0
// (RecognizeZCCW: ==> 1).
long	ThreeToFive(void* lo, tag_CHUNK* chunks, tag_wapx_type* n, int32_t* real, tag_LOWOBJ** objs, long count);		// ROM 0x0028fa14 ThreeToFive__FPvP9tag_CHUNKP13tag_wapx_typePiPP10tag_LOWOBJi
long	RecognizeZCCW(void* lo, tag_CHUNK* chunks, tag_wapx_type* n, int32_t* real, tag_LOWOBJ** objs, long count);	// ROM 0x0028fd18 RecognizeZCCW__FPvP9tag_CHUNKP13tag_wapx_typePiPP10tag_LOWOBJi
// The "4"s of value 0x605 among the digits found taken out again (value
// 0xffff) when one is taller than twice the digits' mean height or shares
// its chunks with another digit.
void	Check_4(tag_CHUNK_STAFF* staff);								// ROM 0x0028d9d0 Check_4__FP15tag_CHUNK_STAFF
// Digits' second looks (unnamed in the ROM, 0x0029ce20): the digits found
// (class 1300, up to thirty) sorted left to right, twelve corrections made
// to them by their neighbours (a low "1" a comma, a short comma a full
// stop, a slanting "1" a solidus, a straight ")" with no "(" a "1", a "1"
// before an unmatched ")" a "(", a 7 after a leftmost "(" a ")", two "<"s
// a guillemet, a "-" that is the bar of the stroke before it dropped, a
// 3/7/")" after an upright stroke a "B" or "D", a digit written as a
// variant the field does not allow (allowed: the staff's fDigits)
// dropped, ...), ThreeToFive and RecognizeZCCW run, and the digits and
// the gaps between them (class 1200, placed by where they lie between
// the digits and the box's ends) put in the list as class 1900 in order.
// ==> how many objects that is (0 with no digits).
long	DigitsSecondLooks(void* lo, tag_CHUNK* chunks, int32_t* real, tag_STK* strokes, long strokeCount, tag_wapx_type* nodes, const UByte* allowed, tag_BOX box);	// ROM 0x0029ce20 (unnamed)
long	ChunkConstruct(tag_CHUNK_STAFF* staff);							// ROM 0x00285a64 ChunkConstruct__FP15tag_CHUNK_STAFF
long	ChunkDestroyData(tag_CHUNK_STAFF* staff);						// ROM 0x00286eb8 ChunkDestroyData__FP15tag_CHUNK_STAFF

void*	LO_Create(void);									// ROM 0x0029bba8 LO_Create__Fv
long	LO_Destroy(void* list);								// ROM 0x0029bd88 LO_Destroy__FPv
long	LO_Clear(void* list);								// ROM 0x0029bdac LO_Clear__FPv
long	LO_Add(void* list, tag_wapx_type* nodes, ULong classID, long from, long to, ULong value, long extra);	// ROM 0x0029be08 LO_Add__FPvP13tag_wapx_typeUiiT4T3T4
long	LO_SetWorkClass(void* list, ULong classID);			// ROM 0x0029c374 LO_SetWorkClass__FPvUi
ULong	LO_GetWorkClassID(void* list);						// ROM 0x0029c7fc LO_GetWorkClassID__FPv
long	LO_PickFirst(void* list, tag_LOWOBJ** obj);			// ROM 0x0029c804 LO_PickFirst__FPvPP10tag_LOWOBJ
long	LO_PickNext(void* list, tag_LOWOBJ** obj);			// ROM 0x0029c84c LO_PickNext__FPvPP10tag_LOWOBJ
long	LO_PickDirectInd(void* list, long index, tag_LOWOBJ** obj);	// ROM 0x0029c910 LO_PickDirectInd__FPviPP10tag_LOWOBJ
long	LO_HowManyChunks(void* list, tag_LOWOBJ* obj);		// ROM 0x0029bc2c LO_HowManyChunks__FPvP10tag_LOWOBJ
long	LO_GetRealChunkInd(void* list, tag_CHUNK* chunks, tag_wapx_type* nodes, tag_LOWOBJ* obj, long n);	// ROM 0x0029bc74 LO_GetRealChunkInd__FPvP9tag_CHUNKP13tag_wapx_typeP10tag_LOWOBJi

// The turns of the trace marked (kTraceLow/kTraceHigh), each stroke's
// first point and the turns the pen makes by more than *height (the
// height of the stroke half way up the strokes sorted by height) over
// divisor.  ==> 0, -1 for want of memory or with 100 strokes or more.
long	ExtrWordTrace_V(tag_WORD_TRACE* trace, long count, long divisor, long* height);	// ROM 0x0028877c ExtrWordTrace_V__FP14tag_WORD_TRACEiT2Pi
// The polyline through the marked points of the trace, each segment
// between two of them split at its furthest point until the chord is
// close enough (tolerance: a tenth of the chord over this).  ==> how many
// nodes, *nodes the block of them (the caller gives it back); -1 for want
// of memory or with 200 marked points or more.
long	GetLineApprox(tag_WORD_TRACE* trace, long count, long tolerance, tag_wapx_type** nodes);	// ROM 0x00285dc8 GetLineApprox__FP14tag_WORD_TRACEiT2PP13tag_wapx_type
long	SetAllDirections(tag_WORD_TRACE* trace, tag_wapx_type* nodes, long count);	// ROM 0x00286638 SetAllDirections__FP14tag_WORD_TRACEP13tag_wapx_typei

// The point between i1 and i2 furthest from the chord between them (a
// pen-up breaks a run; the middle of a run of equally far points).
long	v_MostFarFromChord(tag_WORD_TRACE* trace, long i1, long i2);	// ROM 0x0028686c v_MostFarFromChord__FP14tag_WORD_TRACEiT2
// The square of (x, y)'s distance from the segment's line, worked out in
// 32-bit integers without overflowing where it can help it.
long	v_QDistFromChord(long x1, long y1, long x2, long y2, long x, long y);	// ROM 0x0028694c v_QDistFromChord__FiN51

// The direction from (x1, y1) to (x2, y2) (y growing downwards) in
// twenty-four fifteen-degree steps counted anticlockwise from straight up:
// 0 and 23 either side of up, 5 and 6 of left, 11 and 12 of down, 17 and
// 18 of right.
long	GetDirection(long x1, long y1, long x2, long y2);		// ROM 0x0028646c GetDirection__FiN31

void	ChunkAllocCtx(void** ctx, rc_type* rc);			// ROM 0x002a65ec ChunkAllocCtx__FPPvP7rc_type
void	ChunkCleanUp(void** ctx);							// ROM 0x002a6404 ChunkCleanUp__FPPv - its three blocks and itself given back, *ctx nil
long	IsChunkNumbers(void* ctx);							// ROM 0x002a65cc IsChunkNumbers__FPv
void	ChunkModifyRC(void* ctx, rc_type* rc);				// ROM 0x002a6464 ChunkModifyRC__FPvP7rc_type
void	ChunkRestoreRC(void* ctx, rc_type* rc);				// ROM 0x002a654c ChunkRestoreRC__FPvP7rc_type
void*	ChunkWriteParamCtx(void* ctx, rc_type* rc, xrdata_type* xr, rec_w_type* readings);	// ROM 0x002a65dc ChunkWriteParamCtx__FPvP7rc_typeP11xrdata_typeP10rec_w_type - ==> &fReadings

#endif	/* __CHUNK_H */
