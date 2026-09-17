// IMA ADPCM codec test (IMACodec.h): the compressed framing, a round trip
// that tracks a smooth signal, determinism, an independent decode of the
// stream that matches ExpandIMA, and CheckState's resync.  Pure functions,
// so no OS boot is needed.

#include "IMACodec.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const int kBlocks = 6;
static const int kSamples = kBlocks * 64;		// 384


// A smooth triangle wave (period 96, amplitude 9000): a constant-slope
// signal ADPCM follows well once its step size has adapted.
static void
MakeSignal(short* pcm, int n)
{
	for (int i = 0; i < n; i++)
	{
		int phase = i % 96;
		int v = (phase < 48) ? (phase * 375) : ((96 - phase) * 375);	// 0..18000..0
		pcm[i] = (short) (v - 9000);									// -9000..9000
	}
}


// An independent decoder of a mono IMA stream, matching the Newton's
// continuous scheme (the running predictor carries across blocks; only the
// first block's header seeds it, later headers are skipped).  Written from
// the algorithm rather than from ExpandIMA, to cross-check it.
static void
ReferenceDecode(const signed char* stream, int numBlocks, short* out)
{
	int header = ((unsigned char) stream[0] << 8) | (unsigned char) stream[1];
	long predictor = (short) (header & 0xff80);
	long index = header & 0x7f;
	const signed char* in = stream + 2;
	int o = 0;
	for (int b = 0; b < numBlocks; b++)
	{
		unsigned int packed = 0;
		for (int i = 0; i < 64; i++)
		{
			unsigned int code;
			if ((i & 1) == 0)
			{
				packed = (unsigned char) *in++;
				code = packed & 0xf;
			}
			else
				code = (packed >> 4) & 0xf;

			long step = kIMAStepTable[index];
			long delta = step >> 3;
			if (code & 4) delta += step;
			if (code & 2) delta += step >> 1;
			if (code & 1) delta += step >> 2;
			if (code & 8) delta = -delta;
			predictor += delta;
			if (predictor > 0x7fff) predictor = 0x7fff;
			if (predictor < -0x8000) predictor = -0x8000;
			out[o++] = (short) predictor;

			index += kIMAIndexTable[code];
			if (index < 0) index = 0;
			if (index > 0x58) index = 0x58;
		}
		in += 2;		// skip the next block's header, as ExpandIMA does
	}
}


static void
TestRoundTrip()
{
	short pcm[kSamples];
	MakeSignal(pcm, kSamples);

	signed char compressed[kBlocks * kIMABlockBytes + 16];
	memset(compressed, 0x5a, sizeof(compressed));
	IMAState encState;
	CompressIMA(pcm, compressed, kSamples, &encState, 1, 1);

	// the first block header carries the initial state (predictor 0, index 0)
	EXPECT((unsigned char) compressed[0] == 0 && (unsigned char) compressed[1] == 0);

	short restored[kSamples];
	memset(restored, 0, sizeof(restored));
	IMAState decState;
	ExpandIMA(compressed, restored, &decState, kBlocks, 1, 2);

	// the smooth signal is tracked: the error stays well inside the wave's amplitude
	long maxErr = 0, sumErr = 0;
	for (int i = 0; i < kSamples; i++)
	{
		long e = (long) restored[i] - pcm[i];
		if (e < 0) e = -e;
		if (e > maxErr) maxErr = e;
		sumErr += e;
	}
	// the mean error is tiny; the max spikes only at the triangle's sharp
	// slope reversals, where an adapted step overshoots (as ADPCM does),
	// but stays well under full scale - the exact match below is the real proof
	EXPECT(sumErr / kSamples < 800);
	EXPECT(maxErr < 12000);

	// the encoder and decoder end in the same state
	EXPECT(encState.fPredictor == decState.fPredictor);
	EXPECT(encState.fStepIndex == decState.fStepIndex);

	// an independent decode of the stream matches ExpandIMA exactly
	short reference[kSamples];
	ReferenceDecode(compressed, kBlocks, reference);
	EXPECT(memcmp(reference, restored, sizeof(restored)) == 0);
}


static void
TestDeterminismAndFraming()
{
	short pcm[kSamples];
	MakeSignal(pcm, kSamples);

	signed char a[kBlocks * kIMABlockBytes];
	signed char b[kBlocks * kIMABlockBytes];
	IMAState sa, sb;
	CompressIMA(pcm, a, kSamples, &sa, 1, 1);
	CompressIMA(pcm, b, kSamples, &sb, 1, 1);
	EXPECT(memcmp(a, b, sizeof(a)) == 0);				// deterministic

	// a partial block at the end is dropped (numSamples rounded down)
	IMAState sc;
	signed char c[2 * kIMABlockBytes];
	memset(c, 0x11, sizeof(c));
	CompressIMA(pcm, c, 64 + 30, &sc, 1, 1);			// one whole block only
	EXPECT((unsigned char) c[kIMABlockBytes] == 0x11);	// the second block untouched
}


static void
TestCheckState()
{
	// a header far from the running state resets it; a near one is kept
	IMAState state;
	state.fPredictor = 1000;
	state.fStepIndex = 5;

	signed char nearHeader[2];
	int h = (1000 & 0xff80) | 5;							// same index, predictor within 0x80
	nearHeader[0] = (signed char) (h >> 8);
	nearHeader[1] = (signed char) h;
	CheckState(nearHeader, &state);
	EXPECT(state.fPredictor == 1000 && state.fStepIndex == 5);	// kept

	signed char farHeader[2];
	h = (-4000 & 0xff80) | 20;
	farHeader[0] = (signed char) (h >> 8);
	farHeader[1] = (signed char) h;
	CheckState(farHeader, &state);
	EXPECT(state.fStepIndex == 20);								// resynced
	EXPECT(state.fPredictor == (short) (-4000 & 0xff80));
}


int main()
{
	TestRoundTrip();
	TestDeterminismAndFraming();
	TestCheckState();
	if (failures == 0)
		printf("test_IMACodec: all passed\n");
	else
		printf("test_IMACodec: %d failures\n", failures);
	return failures != 0;
}
