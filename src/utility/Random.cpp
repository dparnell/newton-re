/*
	File:		utility/Random.cpp

	Contains:	The ROM's C library random numbers.  See Random.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Random.h"
#include "ByteOrder.h"
#include <stdint.h>

// ROM 0x0c10595c (unnamed) - the state, which the ROM's initialised data
// starts at nought
static long	gRandState = 0;


// ROM 0x003503d0 rand
// The state times 16807, modulo 2^31 - 1 - the remainder of CompDiv on
// the 64-bit product, brought back above nought when it is negative
// (only a negative seed makes it so).
//
// The ROM brings it back with `SUBLT r0, r0, #0x80000001` (0x00350414) in
// an ARM register, where subtracting 0x80000001 wraps to adding 0x7fffffff
// - so the arithmetic is the ARM's 32-bit word (Long32/ULong32), not the
// host's `long`.  Done in 64 bits it does not wrap at all and the state
// stays negative for good, so every Random after a negative seed answers
// its low bound: on Linux that dealt Mahjongg a board with a tile index of
// -6 (the game seeds the machine's generator with TimeInSeconds() mod 5 + 1,
// which is negative now that TimeInSeconds has outgrown a NewtonScript
// integer - see src/host/demo/thirdparty-apps.ns).
long
NewtonRand(void)
{
	int64_t product = (int64_t) gRandState * 16807;
	long remainder = (long) (product % 0x7fffffff);
	if (remainder < 0)
		remainder = (Long32) ((ULong32) remainder - 0x80000001u);
	gRandState = remainder;
	return remainder;
}


// ROM 0x003504dc srand
void
NewtonSrand(ULong seed)
{
	gRandState = (long) seed;
}


// ROM 0x003504ec sizeof_rand_state
long
SizeofRandState(void)
{
	return 4;
}


// ROM 0x003504f4 get_rand_state
// (host: the word stored big-endian, as the ROM leaves it in the
// randomState binary)
void
GetRandState(void* state)
{
	PutBigEndianWord(state, (ULong) gRandState);
}


// ROM 0x00350508 set_rand_state
void
SetRandState(const void* state)
{
	gRandState = (Long32) GetBigEndianWord(state);		// (Long32: the ROM's word, negative when its top bit is set)
}
