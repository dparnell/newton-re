/*
	File:		recognition/BPNet.cpp

	Contains:	The handwriting engine's classifier net - see BPNet.h.

	Making one, hanging its unit array off it, giving it back, and the
	net itself - `BPNetEvaluate` (its assembly written up in
	`docs/recognition/bpnet.md`).

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
// The net run: every unit after the inputs worked out in order, so
// that a unit may read any unit before it.
//
// The ROM's is nine hundred and fifty bytes of hand-written assembly -
// the inner loop unrolled sixteen ways, four blocks for the four
// weight bytes held in registers, entered through a computed jump that
// resumes at the right weight byte and the right activation byte.  All
// of that is how it goes fast; what it *does* is below, and the three
// numbers in `docs/recognition/bpnet.md` say the two agree: 618 units,
// 90,540 connections (which is what the net writes down in
// `fConnectionCount`), and the last weight byte touched is the last
// byte of `bpWeight`.
//
// DEVIATION: the ROM reads the weights through virtual 0x03500000,
// which the MMU maps to the same eight megabytes of ROM *uncached*
// (`g8MegContinuousTableStart`, ROM 0x100) - ninety-one kilobytes
// streamed once would otherwise flush the StrongARM's whole data
// cache.  The host has one mapping and reads them where they are.
void
BPNetEvaluate(BPNet* net)
{
	net->fField7d = 0;

	const UByte* weights = net->fWeights;
	const ULong* connection = net->fConnects;
	UByte* unit = net->fUnits + net->fInputCount;

	// Which four weight bytes are in hand, as a flat byte index.  The
	// ROM keeps them in four registers and starts on the last of the
	// four, so that the first connection word - which always asks for
	// more - winds back to the first and loads them.
	long cursor = -4;

	ULong word = *connection++;
	for (;;)
	{
		// the low halfword of the word that ended the last unit is
		// this one's bias, in units of 256
		long sum = (long) (short) (word & 0xffff) * 256;
		long count;

		for (;;)
		{
			word = *connection++;
			count = (long) (((int) word) >> kBPNetCountShift);
			if (count >= 0)
				break;					// this word ends the unit

			// either flag steps the four bytes in hand on by one; the
			// difference is only whether four more words are read
			if ((word & (kBPNetNextWeights | kBPNetNextRegister)) != 0)
				cursor += 4;

			// where the activations start, counted back from the unit
			// being worked out.  Both streams are read a word at a
			// time, so the first connection of a group starts at the
			// same byte of the weight word as of the activation word.
			long back = (long) (word & 0xffff);
			long first = ((unit - net->fUnits) - back) & 3;
			const UByte* activations = unit - back - first;
			long total = 4 - count;

			for (long i = 0; i < total; i++)
				// a weight is biased by 128; the ROM works that out as
				// `a*w` minus `a` shifted up seven
				sum += (long) activations[first + i]
					 * ((long) weights[cursor + first + i] - 128);

			// ... and the four bytes in hand are now the group the
			// last connection came out of
			cursor += 4 * ((first + total - 1) / 4);
		}

		// the unit itself.  The sigmoid is a table: the magnitude of
		// the sum (as `~x` for a negative one, not `-x`), clamped,
		// shifted down six bits and looked up, and a negative sum
		// answers 255 less that.
		UByte value;
		if (count != 0)
			// (nothing in the ROM's own table has a count above
			//  nought, so this is the arm that never runs)
			value = (UByte) sum;
		else
		{
			long magnitude = (sum < 0) ? ~sum : sum;
			if (magnitude >= kBPNetSigmoidLimit)
				value = 0xff;
			else
				value = QSigLu[magnitude >> kBPNetSigmoidShift];
			if (sum < 0)
				value = (UByte) (value ^ 0xff);
		}
		*unit++ = value;

		// and the word that ended it says whether it was the last
		if ((word & kBPNetNextRegister) != 0)
			return;
	}
}
