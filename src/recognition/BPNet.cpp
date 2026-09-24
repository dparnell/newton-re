/*
	File:		recognition/BPNet.cpp

	Contains:	The handwriting engine's classifier net - see BPNet.h.

	Making one, hanging its unit array off it and giving it back.  The
	net itself - `BPNetEvaluate` - is NOT YET; what has been read out
	of its assembly so far is `docs/recognition/bpnet.md`.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.
*/

#include "BPNet.h"
#include "RosStrokes.h"			// the engine's own allocation
#include "NewtonMemory.h"
#include "NewtErrors.h"

#include <string.h>


// ROM 0x0003b1cc BPNetCreateNumOut
// The ROM's template copied into a block of its own.  It is handed the
// number of output nodes the caller wants and does not look at it: the
// net built into the ROM has 134 and there is no other.
BPNet*
BPNetCreateNumOut(long /*outputs*/)
{
	BPNet* net = (BPNet*) RosAllocate((long) sizeof(BPNet));
	if (net == nil)
		return nil;
	*net = bpNet;
	return net;
}


// ROM 0x0003b14c BPNetAllocateNet
// The unit array: one byte per unit, inputs first, then the hidden
// units, then the outputs.  (The ROM reads the two counts out of one
// word with an unaligned load - `ldr r2,[r4,#0x46]` - which rotates
// the word at +0x44 and takes its top half.)
NewtonErr
BPNetAllocateNet(BPNet* net)
{
	net->fBlock = (UByte*) RosAllocate(net->fUnitCount);
	net->fUnits = net->fBlock;
	net->fOutputs = net->fBlock + (net->fUnitCount - net->fOutputCount);
	return noErr;
}


// ROM 0x0003b280 BPNetLoad
// All of it: the weights are in the ROM, so there is nothing to read
// in and the only work is making room for the units.
NewtonErr
BPNetLoad(BPNet* net, void* /*from*/)
{
	BPNetAllocateNet(net);
	return noErr;
}


// ROM 0x0003b268 BPNetFree
void
BPNetFree(BPNet* net)
{
	if (net->fBlock != nil)
		DisposPtr((Ptr) net->fBlock);
}


// ROM 0x0003b240 BPNetDestroy
void
BPNetDestroy(BPNet* net)
{
	if (net == nil)
		return;
	BPNetFree(net);
	DisposPtr((Ptr) net);
}


// ROM 0x0003b278 BPNetLearnEnable
// One instruction: the byte at the end of the net.  The handwriting
// engine turns learning off when it makes the net and never turns it
// on again.
void
BPNetLearnEnable(BPNet* net, long enable)
{
	net->fLearning = (UByte) enable;
}


// ROM 0x0001a260 BPNetEvaluate
// NOT YET.  The ROM's is nine hundred and fifty bytes of hand-written
// assembly: an unrolled inner loop entered through a computed jump,
// with the four current weight bytes held in four registers and the
// connection program driving both.  What is known about it, and the
// one thing that does not add up, is in `docs/recognition/bpnet.md`.
void
BPNetEvaluate(BPNet* net)
{
	net->fField7d = 0;
}
