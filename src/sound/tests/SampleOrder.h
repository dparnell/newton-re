// The tests' side of sound/SampleWords.h: 16-bit samples lie in memory
// big-endian on every host, so a test's own signal (host shorts) is put
// into that order before the sound code is given it, and what the sound
// code writes is taken back out of it before the test looks at the values.
// (On a big-endian host both are the identity.)

#ifndef __SAMPLEORDER_H
#define __SAMPLEORDER_H

#include "SampleWords.h"

// host shorts -> samples in memory, in place
static inline void
SamplesToMemory(short* samples, long count)
{
	for (long i = 0; i < count; i++)
	{
		short value = samples[i];
		PutSampleWord(&samples[i], value);
	}
}

// samples in memory -> host shorts, in place
static inline void
SamplesFromMemory(short* samples, long count)
{
	for (long i = 0; i < count; i++)
		samples[i] = GetSampleWord(&samples[i]);
}

#endif
