/*
	File:		Chunk.cpp

	Contains:	The cursive reader's digit and number reader: its context
				and the configuration it changes (see Chunk.h).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM
				(0x002a6404-0x002a6650); each function cites its origin.
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
