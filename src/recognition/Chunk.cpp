/*
	File:		Chunk.cpp

	Contains:	The cursive reader's digit and number reader: its context
				and the configuration it changes (see Chunk.h).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x0028686c-0x00286a54, 0x002a6404-0x002a6650); each
				function cites its origin.
*/

#include "Chunk.h"
#include "ParaGraph.h"		// HWRMemoryAlloc, HWRMemoryFree

// ROM 0x002a65ec ChunkAllocCtx__FPPvP7rc_type
// A context for reading a word: nothing found yet, the configuration it
// reads under.
// DEVIATION: sized by the host (sizeof); +04, +10 and the saved halves are
// left as the allocation had them, as the ROM leaves them.
void
ChunkAllocCtx(void** ctx, rc_type* rc)
{
	ChunkCtx* c = (ChunkCtx*) HWRMemoryAlloc(sizeof(ChunkCtx));
	*ctx = c;
	if (c == nil)
		return;
	c->fData = nil;
	c->fData2 = nil;
	c->fData3 = nil;
	c->fNumbers = 0;
	c->fNumbersOnly = 0;
	c->f1C = 0;
	c->fModified = 0;
	c->f24 = 0;
	c->f28 = 0;
	c->f2C = 0;
	c->fRC = rc;
	c->fXr = nil;
	c->fReadings = nil;
}


// ROM 0x002a6404 ChunkCleanUp__FPPv
void
ChunkCleanUp(void** ctx)
{
	ChunkCtx* c = (ChunkCtx*) *ctx;
	if (c == nil)
		return;
	if (c->fData != nil)
		HWRMemoryFree((Ptr) c->fData);
	c->fData = nil;
	if (c->fData2 != nil)
		HWRMemoryFree((Ptr) c->fData2);
	c->fData2 = nil;
	if (c->fData3 != nil)
		HWRMemoryFree((Ptr) c->fData3);
	c->fData3 = nil;
	HWRMemoryFree((Ptr) *ctx);
	*ctx = nil;
}


// ROM 0x002a65cc IsChunkNumbers__FPv
long
IsChunkNumbers(void* ctx)
{
	return ctx == nil ? 0 : ((ChunkCtx*) ctx)->fNumbers;
}


// ROM 0x002a6464 ChunkModifyRC__FPvP7rc_type
// When the processor found numbers, the configuration made one the xr
// reader reads digits under: the five halves it changes kept to be put
// back, rc +0x02 made 0x3f, and then either (numbers alone) +0x90 0x62 and
// +0x92 one, or +0x90 0x422, bits 0 and 2 of +0x08 cleared and +0x0a two.
void
ChunkModifyRC(void* ctx, rc_type* rc)
{
	ChunkCtx* c = (ChunkCtx*) ctx;
	if (c == nil || c->fNumbers == 0)
		return;
	c->fModified = 1;
	c->fSaved[0] = RCGetH(rc, 0x02);
	c->fSaved[4] = RCGetH(rc, 0x90);
	c->fSaved[1] = RCGetH(rc, 0x08);
	c->fSaved[2] = RCGetH(rc, 0x0a);
	c->fSaved[3] = RCGetH(rc, 0x00);
	RCSetH(rc, 0x02, 0x3f);
	if (c->fNumbersOnly != 0)
	{
		RCSetH(rc, 0x90, 0x62);
		RCSetH(rc, 0x92, 1);
		return;
	}
	RCSetH(rc, 0x90, 0x422);
	RCSetH(rc, 0x08, RCGetH(rc, 0x08) & ~5);
	RCSetH(rc, 0x0a, 2);
}


// ROM 0x002a654c ChunkRestoreRC__FPvP7rc_type
// The configuration put back as ChunkModifyRC found it.
// ROM BUG: +0x92, which the numbers-alone way sets to one, is not put back.
void
ChunkRestoreRC(void* ctx, rc_type* rc)
{
	ChunkCtx* c = (ChunkCtx*) ctx;
	if (c == nil || c->fModified == 0)
		return;
	RCSetH(rc, 0x02, c->fSaved[0]);
	RCSetH(rc, 0x90, c->fSaved[4]);
	RCSetH(rc, 0x08, c->fSaved[1]);
	RCSetH(rc, 0x0a, c->fSaved[2]);
	RCSetH(rc, 0x00, c->fSaved[3]);
	c->fModified = 0;
}


// ROM 0x002a65dc ChunkWriteParamCtx__FPvP7rc_typeP11xrdata_typeP10rec_w_type
// What the rest of the reading will need: the configuration, the xrs and
// the readings.  ==> the address of the readings' field (ctx + 0x44), or
// nil for no context.
void*
ChunkWriteParamCtx(void* ctx, rc_type* rc, xrdata_type* xr, rec_w_type* readings)
{
	ChunkCtx* c = (ChunkCtx*) ctx;
	if (c == nil)
		return nil;
	c->fRC = rc;
	c->fXr = xr;
	c->fReadings = readings;
	return &c->fReadings;
}


#pragma mark - the trace

// 32-bit arithmetic as the ARM does it, wrapping
static inline int32_t	Mul32(int32_t a, int32_t b)		{ return (int32_t) ((uint32_t) a * (uint32_t) b); }
static inline int32_t	Add32(int32_t a, int32_t b)		{ return (int32_t) ((uint32_t) a + (uint32_t) b); }
static inline int32_t	Sub32(int32_t a, int32_t b)		{ return (int32_t) ((uint32_t) a - (uint32_t) b); }


// ROM 0x0028686c v_MostFarFromChord__FP14tag_WORD_TRACEiT2
// The point from i1+1 to i2 furthest from the line through i1 and i2
// (the distance as the cross product, unscaled); a pen-up is skipped and
// breaks a run.  A later point as far as the furthest so far, straight
// after it, moves the answer on one point every second such point, so a
// flat run answers its middle.  ==> its index; i1 when none is further
// than nought.
// DEVIATION: the ROM keeps the every-second-point toggle in r5 without
// setting it first, so a first point at distance nought reads whatever
// the caller left there; the host starts it at nought.
long
v_MostFarFromChord(tag_WORD_TRACE* trace, long i1, long i2)
{
	int32_t dx = trace[i2].x - trace[i1].x;
	int32_t dy = trace[i2].y - trace[i1].y;
	int32_t c = Sub32(Mul32(dy, trace[i1].x), Mul32(dx, trace[i1].y));
	int32_t best = 0;
	long at = i1;
	Boolean following = true;
	Boolean toggle = false;
	for (long i = i1 + 1; i <= i2; i++)
	{
		if (trace[i].y == -1)
		{
			following = false;
			continue;
		}
		int32_t d = Add32(Sub32(Mul32(dx, trace[i].y), Mul32(dy, trace[i].x)), c);
		if (d < 0)
			d = Sub32(0, d);
		if (d > best)
		{
			best = d;
			at = i;
			toggle = false;
			following = true;
		}
		else if (following && d == best)
		{
			if (toggle)
			{
				at++;
				toggle = false;
			}
			else
				toggle = true;
		}
		else
			following = false;
	}
	return at;
}


// ROM 0x0028694c v_QDistFromChord__FiN51
// The square of the distance of (x, y) from the line through (x1, y1) and
// (x2, y2) - |p|^2 less the square of its projection on the chord,
// dot^2/len2 - with the quotient and the remainder of dot/len2 taken
// apart so the products stay in 32 bits: a remainder too big to square is
// halved, and the length quartered, until it is small enough (or the
// length is down to 64), and then the remainder's part is worked out one
// way or the other.  A chord of no length answers the square of the
// distance from its point.
long
v_QDistFromChord(long x1, long y1, long x2, long y2, long x, long y)
{
	int32_t dx = (int32_t) (x - x1);
	int32_t dy = (int32_t) (y - y1);
	int32_t cx = (int32_t) (x2 - x1);
	int32_t cy = (int32_t) (y2 - y1);
	if (x1 == x2 && y1 == y2)
		return Add32(Mul32(dx, dx), Mul32(dy, dy));
	int32_t dot = Add32(Mul32(cx, dx), Mul32(cy, dy));
	int32_t len2 = Add32(Mul32(cx, cx), Mul32(cy, cy));
	int32_t q = dot / len2;
	int32_t rem = dot % len2;
	int32_t absRem = rem < 0 ? -rem : rem;
	int32_t t;
	if (absRem <= 0x7fff)
		t = Sub32(0, Mul32(rem, rem)) / len2;
	else
	{
		int32_t r = absRem;
		int32_t l = len2;
		while (r >= 0x7fff && l > 0x40)
		{
			r >>= 1;
			l = (l + 2) >> 2;
		}
		if (l > 0x40)
			t = Sub32(0, Mul32(r, r)) / l;
		else
			t = Mul32(r, -(r + (l >> 1)) / l);
		if (rem < 0)
			t = -t;
	}
	int32_t result = Add32(Mul32(dy, dy), t);
	result = Sub32(result, Mul32(q, dot));
	result = Add32(Mul32(dx, dx), result);
	result = Sub32(result, Mul32(rem, q));
	return result;
}
