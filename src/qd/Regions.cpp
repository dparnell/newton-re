/*
	File:		qd/Regions.cpp

	Contains:	QuickDraw's regions (Regions.h describes the format).

	The operations work on change lists: a region's row is the list of x
	positions where membership flips from that row down, so XOR-ing the
	rows in order (XorScan) yields each pixel row's transitions; two such
	scanlines are combined by a scan procedure (ShareScan for union,
	intersection and difference, XorScan, InsetScan) and the result's
	difference from the row above becomes the output's row - as points
	(y, x) that SortPoints orders and PackRgn packs into a region.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	The scan-conversion mask (SeekRgn) is laid out in the current port's
	pixel depth: NOT YET RECONSTRUCTED (ports), so the host uses one bit
	per pixel.
*/

#include "Regions.h"
#include "Ports.h"
#include "OSErrors.h"
#include <string.h>

// ROM 0x00376fbc: log2 of the pixels in a word, by bits per pixel
static const unsigned char kPixelShift[33] = {
	0, 5, 4, 0, 3, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
// ROM 0x00377000: the pixels in a word less one, by bits per pixel
static const unsigned char kPixelMask[33] = {
	0, 31, 15, 0, 7, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const long	kExpandedRgnSize = 0x1e;		// a rectangle as rows: the header and nine shorts
const long	kPointsGrowth = 0x100;			// a point buffer grows by this


static inline short*
RgnRows(Region* rgn)
{
	return (short*) ((char*) rgn + kRgnRowsOffset);
}


// The current port's bits per pixel, which a region's mask is made at
// (InitRgnRec 0x003428f0 reads the port's pixMapFlags); one bit when
// there is no port yet (host).
static long
CurrentPortDepth(void)
{
	GrafPort* port = GetCurrentPort();
	if (port == nil)
		return 1;
	long depth = port->portBits.pixMapFlags & kPixMapDepth;
	return (depth != 0 && depth <= 32) ? depth : 1;
}


/*------------------------------------------------------------------------------
	T e m p o r a r y   m e m o r y
------------------------------------------------------------------------------*/

// ROM 0x0033f554 QDNewTempPtr__Fl
// NOT YET RECONSTRUCTED: the 1 KB scratch area in the Newt globals the
// ROM hands out first; the heap here.
void*
QDNewTempPtr(long size)
{
	if (size < 4)
		size = 4;
	return NewPtr(size);
}


// ROM 0x0033f630 QDSafeLock__FPPc
// A handle locked for QuickDraw's use; ==> its state before, for HSetState.
char
QDSafeLock(Handle h)
{
	char state = HGetState(h);
	HLock(h);
	return state;
}


// ROM 0x0033f5d0 QDDisposeTempPtr__FPc
void
QDDisposeTempPtr(void* p)
{
	if (p != nil)
		DisposPtr((Ptr) p);
}


/*------------------------------------------------------------------------------
	M a k i n g   r e g i o n s
------------------------------------------------------------------------------*/

// ROM 0x00340ff8 NewRgn__Fv
// An empty rectangular region.
RgnHandle
NewRgn(void)
{
	RgnHandle rgn = (RgnHandle) NewHandle(kRectRgnSize);
	if (rgn != nil)
	{
		(*rgn)->rgnSize = kRectRgnSize;
		(*rgn)->filler = 0;
		SetEmptyRect(&(*rgn)->rgnBBox);
	}
	return rgn;
}


// ROM 0x00341904 DisposeRgn__FPP6Region
void
DisposeRgn(RgnHandle rgn)
{
	DisposHandle((Handle) rgn);
}


// ROM 0x00342b90 SetRectRgn__FPP6RegionlN32
// The region made the rectangle (empty when it is).
void
SetRectRgn(RgnHandle rgn, long left, long top, long right, long bottom)
{
	if ((*rgn)->rgnSize != kRectRgnSize)
	{
		(*rgn)->rgnSize = kRectRgnSize;
		SetHandleSize((Handle) rgn, kRectRgnSize);
	}
	if (left < right && top < bottom)
		SetRect(&(*rgn)->rgnBBox, left, top, right, bottom);
	else
		SetEmptyRect(&(*rgn)->rgnBBox);
}


// ROM 0x00342c18 RectRgn__FPP6RegionP4Rect
void
RectRgn(RgnHandle rgn, const Rect* r)
{
	SetRectRgn(rgn, r->left, r->top, r->right, r->bottom);
}


// ROM 0x00342aec SetEmptyRgn__FPP6Region
void
SetEmptyRgn(RgnHandle rgn)
{
	SetRectRgn(rgn, 0, 0, 0, 0);
}


// ROM 0x00342064 CopyRgn__FPP6RegionT1
Boolean
CopyRgn(RgnHandle src, RgnHandle dst)
{
	if (src != dst)
	{
		long size = (*src)->rgnSize;
		if (size != (*dst)->rgnSize)
		{
			if (SetHandleSize((Handle) dst, size) != noErr)
				return false;
			(*dst)->rgnSize = (short) size;
		}
		BlockMove(*src, *dst, size);
	}
	return true;
}


// ROM 0x00342c54 OffsetRgn__FPP6RegionlT2
// The bounding box and every row moved.
void
OffsetRgn(RgnHandle rgn, long dh, long dv)
{
	OffsetRect(&(*rgn)->rgnBBox, dh, dv);
	if ((*rgn)->rgnSize == kRectRgnSize)
		return;
	short* row = RgnRows(*rgn);
	while (*row != kRgnEnd)
	{
		*row = (short) (*row + (short) dv);
		short* x = row + 1;
		do
		{
			x[0] = (short) (x[0] + (short) dh);
			x[1] = (short) (x[1] + (short) dh);
			x += 2;
		} while (*x != kRgnEnd);
		row = x + 1;
	}
}


/*------------------------------------------------------------------------------
	T e s t s
------------------------------------------------------------------------------*/

// ROM 0x00341408 EmptyRgn__FPP6Region
Boolean
EmptyRgn(RgnHandle rgn)
{
	const Rect* box = &(*rgn)->rgnBBox;
	return !(box->left < box->right && box->top < box->bottom);
}


// ROM 0x00341350 EqualRgn__FPP6RegionT1
// The same size, bounding box and rows.
// DEVIATION: the ROM compares the rows from their second short to one
// short past the region (the first row's y is the box's top anyway);
// the rows are compared exactly here.
Boolean
EqualRgn(RgnHandle a, RgnHandle b)
{
	if (a == b)
		return true;
	long size = (*a)->rgnSize;
	if (size != (*b)->rgnSize || !EqualRect(&(*a)->rgnBBox, &(*b)->rgnBBox))
		return false;
	return memcmp(RgnRows(*a), RgnRows(*b), size - kRgnRowsOffset) == 0;
}


// ROM 0x00342b10 IsWideOpenRgn__FPP6Region
// No region (nil), or the rectangle that reaches every coordinate.
Boolean
IsWideOpenRgn(RgnHandle rgn)
{
	if (rgn == nil)
		return true;
	Region* r = *rgn;
	if (r->rgnSize != kRectRgnSize)
		return false;
	return r->rgnBBox.left < -0x7ffe && r->rgnBBox.top < -0x7ffe && r->rgnBBox.right > 0x7ffd && r->rgnBBox.bottom > 0x7ffd;
}


// ROM 0x00341504 PtInRgn__F5PointPP6Region
// Whether the point is in the region: within the bounding box, the rows
// above it applied, each x at or left of the point flipping membership.
// (The ROM applies a row only to points strictly below its y, so for a
// non-rectangular region this answers for the pixel row above the point
// - one row off what SeekRgn draws; kept as it is, see the test.)
Boolean
PtInRgn(Point pt, RgnHandle rgn)
{
	if (!PtInRect(pt, &(*rgn)->rgnBBox))
		return false;
	if ((*rgn)->rgnSize == kRectRgnSize)
		return true;
	Boolean inside = false;
	const short* row = RgnRows(*rgn);
	if (pt.v <= *row)
		return false;
	for (;;)
	{
		const short* x = row + 1;
		for (; *x != kRgnEnd; x++)
			if (*x <= pt.h)
				inside = !inside;
		row = x + 1;
		if (!(*row < pt.v))
			break;
	}
	return inside;
}


/*------------------------------------------------------------------------------
	S c a n   c o n v e r s i o n
------------------------------------------------------------------------------*/

// ROM 0x003428f0 InitRgnRec__FP6RegionP8RgnStatelN23
// The state set to scan the region between left and right, the mask's
// first pixel at origin, in the current port's depth.
void
InitRgnRec(Region* rgn, RgnState* state, long left, long right, long origin)
{
	state->fRegion = rgn;
	state->fLeft = left;
	state->fRight = right;
	state->fOrigin = origin;
	state->fTop = -0x7fff;
	state->fRow = RgnRows(rgn);
	state->fBottom = rgn->rgnBBox.top;
	long depth = CurrentPortDepth();
	state->fShift = kPixelShift[depth & 0xff];
	state->fScanWords = (right - origin) >> state->fShift;
	state->fDepth = depth;
}


// ROM 0x003418c0 InitRgn__FP6RegionP8RgnStatelN23Pc
// The same with the mask's storage, cleared.
void
InitRgn(Region* rgn, RgnState* state, long left, long right, long origin, char* scan)
{
	InitRgnRec(rgn, state, left, right, origin);
	state->fScan = (ULong32*) scan;
	memset(scan, 0, (state->fScanWords + 1) * sizeof(ULong32));
}


// the pixels [x1, x2) of the mask inverted
static void
ToggleScan(RgnState* state, long x1, long x2)
{
	long depth = state->fDepth;
	long mask = kPixelMask[depth & 0xff];
	long headBits = depth * (x1 & mask);
	long tailBits = depth * (x2 & mask);
	ULong32 head = (ULong32) 0xffffffff >> headBits;
	ULong32 tail = (tailBits != 0) ? (ULong32) 0xffffffff << (32 - tailBits) : 0;
	ULong32* word = state->fScan + (x1 >> state->fShift);
	long span = (x2 >> state->fShift) - (x1 >> state->fShift);
	if (span < 1)
		*word ^= head & tail;
	else
	{
		*word++ ^= head;
		while (--span > 0)
		{
			*word = ~*word;
			word++;
		}
		*word ^= tail;
	}
}


// ROM 0x00341908 SeekRgn__FP8RgnStatel
// The mask made that of pixel row y: the rows down to y applied (from the
// top again when y is above the current row).  ==> false when the mask
// already was row y's.
Boolean
SeekRgn(RgnState* state, long y)
{
	if (y < state->fBottom)
	{
		if (state->fTop <= y)
			return false;
		memset(state->fScan, 0, (state->fScanWords + 1) * sizeof(ULong32));
		state->fBottom = state->fRegion->rgnBBox.top;
		state->fRow = RgnRows(state->fRegion);
		state->fTop = -0x7fff;
		if (y < state->fBottom)
			return true;
	}
	const short* row = state->fRow;
	do
	{
		state->fTop = *row;
		const short* x = row + 1;
		while (*x != kRgnEnd)
		{
			long x1 = x[0];
			if (x1 < state->fLeft)
				x1 = state->fLeft;
			long x2 = x[1];
			if (x2 > state->fRight)
				x2 = state->fRight;
			if (x1 < x2)
				ToggleScan(state, x1 - state->fOrigin, x2 - state->fOrigin);
			x += 2;
		}
		row = x + 1;
		state->fRow = row;
		state->fBottom = *row;
	} while (state->fBottom <= y);
	return true;
}


// ROM 0x00341208 RectInRgn__FP4RectPP6Region
// Whether any pixel of the rectangle is in the region: its rows over the
// rectangle scanned until a mask has a bit.
Boolean
RectInRgn(const Rect* r, RgnHandle rgn)
{
	Rect box;
	if (!RSect(&box, 2, &(*rgn)->rgnBBox, r))
		return false;
	if ((*rgn)->rgnSize == kRectRgnSize)
		return true;
	long shift = kPixelShift[CurrentPortDepth() & 0xff];
	char* scan = (char*) QDNewTempPtr((((box.right - box.left) >> shift) + 2) * sizeof(ULong32));
	if (scan == nil)
		return false;
	RgnState state;
	InitRgn(*rgn, &state, box.left, box.right, box.left, scan);
	Boolean found = false;
	long y = box.top;
	for (;;)
	{
		SeekRgn(&state, y);
		for (long i = 0; i <= state.fScanWords; i++)
			if (state.fScan[i] != 0)
			{
				found = true;
				break;
			}
		if (found)
			break;
		y = state.fBottom;
		if (y >= box.bottom)
			break;
	}
	QDDisposeTempPtr(scan);
	return found;
}


/*------------------------------------------------------------------------------
	T h e   s c a n   p r o c e d u r e s
	Each combines two rows of x transitions (0x7fff-ended) into a third.
------------------------------------------------------------------------------*/

typedef void (*ScanProc)(const short* a, const short* b, short* out, long inset);

// ROM 0x00341b88 XorScan__FPsN21l
// The transitions in one row but not the other; ==> where a's row ends
// (past its 0x7fff: the next row of a region).
static const short*
XorScan(const short* a, const short* b, short* out, long /*inset*/)
{
	short xa = *a++;
	short xb = *b++;
	for (;;)
	{
		if (xa == kRgnEnd && xb == kRgnEnd)
		{
			*out = kRgnEnd;
			return a;
		}
		if (xa == xb)
		{
			xa = *a++;
			xb = *b++;
		}
		else if (xb < xa)
		{
			*out++ = xb;
			xb = *b++;
		}
		else
		{
			*out++ = xa;
			xa = *a++;
		}
	}
}


static void
XorScanProc(const short* a, const short* b, short* out, long inset)
{
	XorScan(a, b, out, inset);
}


// ROM 0x00341d58 ShareScan__FPsN21lT4
// The rows combined: a transition of one row is kept when the other row is
// in the state the flags say (aOutside: a is outside when b's transitions
// count; bOutside likewise), a shared transition when both flags agree.
static void
ShareScan(const short* a, const short* b, short* out, long aOutside, long bOutside)
{
	short xa = *a++;
	short xb = *b++;
	for (;;)
	{
		if (xa == kRgnEnd && xb == kRgnEnd)
		{
			*out = kRgnEnd;
			return;
		}
		if (xa == xb)
		{
			if (aOutside == bOutside)
				*out++ = xa;
			xa = *a++;
			aOutside = ~aOutside;
			xb = *b++;
			bOutside = ~bOutside;
		}
		else if (xb < xa)
		{
			if (aOutside != 0)
				*out++ = xb;
			xb = *b++;
			bOutside = ~bOutside;
		}
		else
		{
			if (bOutside != 0)
				*out++ = xa;
			xa = *a++;
			aOutside = ~aOutside;
		}
	}
}


// ROM 0x00341d00 UnionScan__FPsN21l
static void
UnionScan(const short* a, const short* b, short* out, long /*inset*/)
{
	ShareScan(a, b, out, -1, -1);
}


// ROM 0x00341d1c DiffScan__FPsN21l
static void
DiffScan(const short* a, const short* b, short* out, long /*inset*/)
{
	ShareScan(a, b, out, 0, -1);
}


// ROM 0x00341d3c SectScan__FPsN21l
static void
SectScan(const short* a, const short* b, short* out, long /*inset*/)
{
	ShareScan(a, b, out, 0, 0);
}


// ROM 0x00341c20 InsetScan__FPsN21l
// Each span of a's row narrowed by inset on both sides (dropped when
// nothing is left); widened for a negative inset, spans that then touch
// merging.
static void
InsetScan(const short* a, const short* /*b*/, short* out, long inset)
{
	if (inset < 0)
	{
		long lastRight = -0x7fff;
		while (*a != kRgnEnd)
		{
			long left = a[0] + inset;
			long right = a[1] - inset;
			a += 2;
			if (lastRight < left)
				*out++ = (short) left;
			else
				out--;								// joins the span before: its right edge is replaced
			*out++ = (short) right;
			lastRight = right;
		}
	}
	else
	{
		while (*a != kRgnEnd)
		{
			long left = a[0] + inset;
			long right = a[1] - inset;
			a += 2;
			if (left < right)
			{
				*out++ = (short) left;
				*out++ = (short) right;
			}
		}
	}
	*out = kRgnEnd;
}


/*------------------------------------------------------------------------------
	R e g i o n   o p e r a t i o n s
------------------------------------------------------------------------------*/

// ROM 0x00341ab0 Expand__FPPP10TrueRegionPP10TrueRegionP10TrueRegion
// A rectangular region given rows (in the caller's buffer, through the
// caller's handle) so that the operations see one form only.
static void
Expand(RgnHandle* rgn, Region** tempHandle, char* buffer)
{
	Region* r = **rgn;
	if (r->rgnSize != kRectRgnSize)
		return;
	*rgn = (RgnHandle) tempHandle;
	*tempHandle = (Region*) buffer;
	Region* t = (Region*) buffer;
	t->rgnSize = kRectRgnSize;
	t->filler = 0;
	t->rgnBBox = r->rgnBBox;
	short* rows = RgnRows(t);
	rows[0] = r->rgnBBox.top;
	rows[1] = r->rgnBBox.left;
	rows[2] = r->rgnBBox.right;
	rows[3] = kRgnEnd;
	rows[4] = r->rgnBBox.bottom;
	rows[5] = r->rgnBBox.left;
	rows[6] = r->rgnBBox.right;
	rows[7] = kRgnEnd;
	rows[8] = kRgnEnd;
}


static inline void
Swap(short*& a, short*& b)
{
	short* t = a;
	a = b;
	b = t;
}


// ROM 0x00342214 RgnOp__FPP10TrueRegionT1PPclN24Uc
// The operation on the two regions as points: the rows of both walked in
// y order, each pixel row's transitions built by XOR-ing the rows above,
// combined by the operation's scan procedure, and the change from the
// previous result row written as points (y, x) - two per span - into the
// points handle (grown by 0x100 bytes at a time when canGrow, otherwise
// the points stop at its end).  For kRgnOpInset b is ignored and inset
// is the amount.  ==> the number of points; -1 when no memory.
long
RgnOp(RgnHandle a, RgnHandle b, Handle points, long pointsSize, long op, long inset, Boolean canGrow)
{
	Region* tempA;
	Region* tempB;
	char bufferA[kExpandedRgnSize];
	char bufferB[kExpandedRgnSize];
	Expand(&a, &tempA, bufferA);
	Expand(&b, &tempB, bufferB);

	ScanProc scan;
	switch (op)
	{
	case kRgnOpSect:	scan = SectScan;		break;
	case kRgnOpDiff:	scan = DiffScan;		break;
	case kRgnOpUnion:	scan = UnionScan;		break;
	case kRgnOpXor:		scan = XorScanProc;		break;
	case kRgnOpInset:	scan = InsetScan;		break;
	default:			return -1;
	}

	pointsSize &= ~7;
	long outLimit = pointsSize;
	long outAt = 0;

	long bufferSize = ((*a)->rgnSize + (*b)->rgnSize) * 2;
	if (bufferSize < 0x28)
		bufferSize = 0x28;
	else if (bufferSize > 30000)
		bufferSize = 30000;
	short* buffers[7];
	for (;;)
	{
		long i;
		for (i = 0; i < 7; i++)
		{
			buffers[i] = (short*) QDNewTempPtr(bufferSize);
			if (buffers[i] == nil)
				break;
		}
		if (i == 7)
			break;
		while (--i >= 0)
			QDDisposeTempPtr(buffers[i]);
		bufferSize >>= 1;
		if (bufferSize < 200)
			return -1;
	}
	short* curA = buffers[0];
	short* nextA = buffers[1];
	short* curB = buffers[2];
	short* nextB = buffers[3];
	short* result = buffers[4];
	short* prev = buffers[5];
	short* delta = buffers[6];
	*curA = kRgnEnd;
	*curB = kRgnEnd;
	*prev = kRgnEnd;

	const short* rowA = RgnRows(*a);
	long yA = *rowA++;
	const short* rowB = RgnRows(*b);
	long yB = *rowB++;
	if (op == kRgnOpInset)
		yB = kRgnEnd;
	Boolean failed = false;
	for (;;)
	{
		if (yA == kRgnEnd && yB == kRgnEnd)
			break;
		long y;
		if (yA == yB)
		{
			y = yA;
			rowA = XorScan(rowA, curA, nextA, inset);
			yA = *rowA++;
			Swap(curA, nextA);
			rowB = XorScan(rowB, curB, nextB, inset);
			yB = *rowB++;
			Swap(curB, nextB);
		}
		else if (yB < yA)
		{
			y = yB;
			rowB = XorScan(rowB, curB, nextB, inset);
			yB = *rowB++;
			Swap(curB, nextB);
		}
		else
		{
			y = yA;
			rowA = XorScan(rowA, curA, nextA, inset);
			yA = *rowA++;
			Swap(curA, nextA);
		}
		scan(curA, curB, result, inset);
		XorScan(result, prev, delta, inset);
		Boolean full = false;
		for (const short* d = delta; *d != kRgnEnd; d += 2)
		{
			if (outAt >= outLimit)
			{
				if (!canGrow)
				{
					full = true;
					break;
				}
				outLimit += kPointsGrowth;
				long rowAOffset = rowA - (const short*) *a;			// the heap may move the regions with the points
				long rowBOffset = rowB - (const short*) *b;
				if (SetHandleSize(points, outLimit) != noErr)
				{
					failed = true;
					break;
				}
				rowA = (const short*) *a + rowAOffset;
				rowB = (const short*) *b + rowBOffset;
			}
			short* out = (short*) (*points + outAt);
			out[0] = (short) y;
			out[1] = d[0];
			out[2] = (short) y;
			out[3] = d[1];
			outAt += 8;
		}
		if (full || failed)
			break;
		Swap(result, prev);
	}
	for (long i = 0; i < 7; i++)
		QDDisposeTempPtr(buffers[i]);
	return failed ? -1 : outAt / 4;
}


// ROM 0x00342a0c QDQuickSort__FP5PointT1
// The points from first to last (inclusive) ordered by y, then x.
static inline Boolean
PointLess(Point a, Point b)
{
	return a.v < b.v || (a.v == b.v && a.h < b.h);
}

static void
QDQuickSort(Point* first, Point* last)
{
	Point pivot = first[(last - first) / 2];
	Point* lo = first;
	Point* hi = last;
	do
	{
		while (PointLess(*lo, pivot))
			lo++;
		while (PointLess(pivot, *hi))
			hi--;
		if (lo <= hi)
		{
			Point t = *lo;
			*lo++ = *hi;
			*hi-- = t;
		}
	} while (lo <= hi);
	if (first < hi)
		QDQuickSort(first, hi);
	if (lo < last)
		QDQuickSort(lo, last);
}


// ROM 0x00342974 SortPoints__FP5Pointl
void
SortPoints(Point* points, long count)
{
	if (count != 0)
		QDQuickSort(points, points + count - 1);
}


// ROM 0x00342988 CullPoints__FP5PointPl
// Adjacent equal points (a transition twice: nothing) dropped in pairs.
void
CullPoints(Point* points, long* count)
{
	long n = *count;
	if (n == 0)
		return;
	Point* end = points + n - 1;
	Point* in = points;
	Point* out = points;
	while (in < end)
	{
		if (EqualPt(in[1], in[0]))
			in += 2;
		else
			*out++ = *in++;
	}
	if (in == end)
		*out++ = *in;
	*count = out - points;
}


// ROM 0x003426c4 PackRgn__FPPclPP6Region
// The sorted points (y, x) packed into the region: four points make a
// rectangular region, more make rows (the x list of each y), none an empty
// region.  The region's handle is sized to fit.
long
PackRgn(Handle points, long count, RgnHandle rgn)
{
	Region* r = *rgn;
	const Point* pt = (const Point*) *points;
	long size = kRectRgnSize;
	SetEmptyRect(&r->rgnBBox);
	if (count == 4)
	{
		r->rgnBBox.top = pt[0].v;
		r->rgnBBox.left = pt[0].h;
		r->rgnBBox.bottom = pt[3].v;
		r->rgnBBox.right = pt[3].h;
	}
	else if (count > 4)
	{
		r->rgnBBox.top = pt[0].v;
		long left = pt[0].h;
		long right = pt[0].h;
		for (long i = 1; i < count; i++)
		{
			long x = pt[i].h;
			if (x < left)
				left = x;
			else if (x > right)
				right = x;
		}
		r->rgnBBox.left = (short) left;
		r->rgnBBox.right = (short) right;
		r->rgnBBox.bottom = pt[count - 1].v;
		if (SetHandleSize((Handle) rgn, count * 4 + 0xe) != noErr)
		{
			SetRectRgn(rgn, 0, 0, 0, 0);
			return -1;
		}
		r = *rgn;
		pt = (const Point*) *points;
		short* out = RgnRows(r);
		long y = pt[0].v;
		*out++ = (short) y;
		for (long i = 1; i < count; i++)
		{
			*out++ = pt[i - 1].h;
			if (pt[i].v != y)
			{
				*out++ = kRgnEnd;
				y = pt[i].v;
				*out++ = (short) y;
			}
		}
		*out++ = pt[count - 1].h;
		*out++ = kRgnEnd;
		*out++ = kRgnEnd;
		size = (char*) out - (char*) r;
	}
	r->rgnSize = (short) size;
	if (size != GetHandleSize((Handle) rgn))
		return SetHandleSize((Handle) rgn, size);
	return 0;
}


// ROM 0x003407cc PutRect__FP4RectPP6RegionPlT3
// The rectangle's four points appended to the point buffer (grown by 0x100
// when full), offset moved past them.
void
PutRect(const Rect* r, Handle points, long* offset, long* limit)
{
	if (*limit < *offset + 16)
	{
		long size = *limit + kPointsGrowth;
		if (SetHandleSize(points, size) != noErr)
			return;
		*limit = size;
	}
	short* out = (short*) (*points + *offset);
	*offset += 16;
	out[0] = r->top;	out[1] = r->left;
	out[2] = r->top;	out[3] = r->right;
	out[4] = r->bottom;	out[5] = r->left;
	out[6] = r->bottom;	out[7] = r->right;
}


// ROM 0x003420f4 PutRgn__FPP6RegionPPcPlT3
// The region's rows appended to the point buffer as points, two per span.
Boolean
PutRgn(RgnHandle rgn, Handle points, long* offset, long* limit)
{
	Region* r = *rgn;
	if (r->rgnSize == kRectRgnSize)
	{
		PutRect(&r->rgnBBox, points, offset, limit);
		return true;
	}
	long needed = *offset + r->rgnSize * 2;
	if (*limit < needed)
	{
		needed += kPointsGrowth;
		if (SetHandleSize(points, needed) != noErr)
			return false;
		*limit = needed;
	}
	r = *rgn;
	short* out = (short*) (*points + *offset);
	const short* row = RgnRows(r);
	while (*row != kRgnEnd)
	{
		short y = *row;
		const short* x = row + 1;
		do
		{
			*out++ = y;
			*out++ = x[0];
			*out++ = y;
			*out++ = x[1];
			x += 2;
		} while (*x != kRgnEnd);
		row = x + 1;
	}
	*offset = (char*) out - *points;
	return true;
}


// the points of an operation made the destination region
static void
OperateRgn(RgnHandle a, RgnHandle b, RgnHandle dst, long op)
{
	long size = ((*a)->rgnSize + (*b)->rgnSize) * 2;
	Handle points = NewHandle(size);
	if (points == nil)
	{
		SetEmptyRgn(dst);
		return;
	}
	long count = RgnOp(a, b, points, size, op, 0, true);
	if (count >= 0)
		PackRgn(points, count, dst);
	DisposHandle(points);
}


// ROM 0x003411b8 SectRgn__FPP6RegionN21
// The intersection: a copy when the regions are equal, empty when their
// boxes do not meet, the boxes' intersection when both are rectangles.
void
SectRgn(RgnHandle a, RgnHandle b, RgnHandle dst)
{
	if (EqualRgn(a, b))
	{
		CopyRgn(a, dst);
		return;
	}
	Rect box;
	if (RSect(&box, 2, &(*a)->rgnBBox, &(*b)->rgnBBox))
	{
		if ((*a)->rgnSize == kRectRgnSize && (*b)->rgnSize == kRectRgnSize)
		{
			RectRgn(dst, &box);
			return;
		}
		OperateRgn(a, b, dst, kRgnOpSect);
		return;
	}
	SetEmptyRgn(dst);
}


// ROM 0x003411cc UnionRgn__FPP6RegionN21
// The union: a copy of the other when one is empty or they are equal.
void
UnionRgn(RgnHandle a, RgnHandle b, RgnHandle dst)
{
	if (EqualRgn(a, b) || EmptyRgn(b))
	{
		CopyRgn(a, dst);
		return;
	}
	if (EmptyRgn(a))
	{
		CopyRgn(b, dst);
		return;
	}
	OperateRgn(a, b, dst, kRgnOpUnion);
}


// ROM 0x003411e0 DiffRgn__FPP6RegionN21
// a less b: a copy of a when b is empty or the boxes do not meet, empty
// when they are equal.
void
DiffRgn(RgnHandle a, RgnHandle b, RgnHandle dst)
{
	if (EqualRgn(a, b))
	{
		SetEmptyRgn(dst);
		return;
	}
	Rect box;
	if (EmptyRgn(b) || !RSect(&box, 2, &(*a)->rgnBBox, &(*b)->rgnBBox))
	{
		CopyRgn(a, dst);
		return;
	}
	OperateRgn(a, b, dst, kRgnOpDiff);
}


// ROM 0x003411f4 XorRgn__FPP6RegionN21
// The pixels in one region only: a copy of the other when one is empty,
// empty when they are equal.
void
XorRgn(RgnHandle a, RgnHandle b, RgnHandle dst)
{
	if (EqualRgn(a, b))
	{
		SetEmptyRgn(dst);
		return;
	}
	if (EmptyRgn(b))
	{
		CopyRgn(a, dst);
		return;
	}
	if (EmptyRgn(a))
	{
		CopyRgn(b, dst);
		return;
	}
	OperateRgn(a, b, dst, kRgnOpXor);
}


// ROM 0x00341ec8 DoRgnOp__FlPP6RegionN22
// The four operations through one entry (the QuickDraw library driver's).
void
DoRgnOp(long op, RgnHandle a, RgnHandle b, RgnHandle dst)
{
	if (EqualRgn(a, b))
	{
		if (op == kRgnOpSect || op == kRgnOpUnion)
			CopyRgn(a, dst);
		else
			SetEmptyRgn(dst);
		return;
	}
	Rect box;
	if (op == kRgnOpDiff)
	{
		if (EmptyRgn(b) || !RSect(&box, 2, &(*a)->rgnBBox, &(*b)->rgnBBox))
		{
			CopyRgn(a, dst);
			return;
		}
	}
	else if (op == kRgnOpSect)
	{
		if (!RSect(&box, 2, &(*a)->rgnBBox, &(*b)->rgnBBox))
		{
			SetEmptyRgn(dst);
			return;
		}
		if ((*a)->rgnSize == kRectRgnSize && (*b)->rgnSize == kRectRgnSize)
		{
			RectRgn(dst, &box);
			return;
		}
	}
	else
	{
		if (EmptyRgn(b))
		{
			CopyRgn(a, dst);
			return;
		}
		if (EmptyRgn(a))
		{
			CopyRgn(b, dst);
			return;
		}
	}
	OperateRgn(a, b, dst, op);
}


// ROM 0x0034108c InsetRgn__FPP6RegionlT2
// The region shrunk (or grown, for negative amounts) by dh on the left
// and right and dv on the top and bottom: a rectangle's box inset, or two
// inset passes - the second on the region transposed, so that the same
// row-wise scan does the vertical inset.
void
InsetRgn(RgnHandle rgn, long dh, long dv)
{
	if (dh == 0 && dv == 0)
		return;
	Region* r = *rgn;
	if (r->rgnSize == kRectRgnSize)
	{
		InsetRect(&r->rgnBBox, dh, dv);
		if (!(r->rgnBBox.top < r->rgnBBox.bottom && r->rgnBBox.left < r->rgnBBox.right))
			SetEmptyRect(&r->rgnBBox);
		return;
	}
	long size = r->rgnSize * 2;
	Handle points = NewHandle(size);
	if (points == nil)
		return;
	long inset = dh;
	for (long pass = 0; pass < 2; pass++)
	{
		long count = RgnOp(rgn, rgn, points, size, kRgnOpInset, inset, true);
		if (count == -1)
			break;
		Point* pt = (Point*) *points;
		for (long i = 0; i < count; i++)
		{
			short t = pt[i].v;
			pt[i].v = pt[i].h;
			pt[i].h = t;
		}
		SortPoints(pt, count);
		PackRgn(points, count, rgn);
		inset = dv;
	}
	DisposHandle(points);
}


// ROM 0x00341670 MapRgn__FPP6RegionP4RectT2
// The region mapped from one rectangle's coordinates to another's: a
// rectangle's box mapped, otherwise every point mapped and the region
// re-packed (coinciding points cancelling).
void
MapRgn(RgnHandle rgn, const Rect* src, const Rect* dst)
{
	if (EqualRect(src, dst))
		return;
	if ((*rgn)->rgnSize == kRectRgnSize)
	{
		MapRect(&(*rgn)->rgnBBox, src, dst);
		return;
	}
	long limit = kPointsGrowth;
	Handle points = NewHandle(limit);
	if (points == nil)
		return;
	long offset = 0;
	if (PutRgn(rgn, points, &offset, &limit))
	{
		long count = offset / 4;
		Point* pt = (Point*) *points;
		for (long i = 0; i < count; i++)
			MapPt(&pt[i], src, dst);
		SortPoints(pt, count);
		CullPoints(pt, &count);
		PackRgn(points, count, rgn);
	}
	DisposHandle(points);
}


// ROM 0x003417b4 TrimRect__FPP6RegionP4Rect
// The rectangle cut down to the region: when the intersection is a
// rectangle it replaces r and 0 is answered; a negative answer means an
// empty intersection, a positive one a more complex shape (r untouched).
long
TrimRect(RgnHandle rgn, Rect* r)
{
	Region rect;
	rect.rgnSize = kRectRgnSize;
	rect.filler = 0;
	rect.rgnBBox = *r;
	Region* rectHandle = &rect;
	char pointBuffer[24];
	char* pointsHandle = pointBuffer;
	long count = RgnOp(rgn, (RgnHandle) &rectHandle, &pointsHandle, sizeof(pointBuffer), kRgnOpSect, 0, false);
	if (count == 4)
	{
		const Point* pt = (const Point*) pointBuffer;
		SetRect(r, pt[0].h, pt[0].v, pt[3].h, pt[3].v);
	}
	return count - 4;
}
