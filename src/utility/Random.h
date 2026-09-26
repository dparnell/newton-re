/*
	File:		utility/Random.h

	Contains:	The ROM's C library random numbers: rand() and srand(),
				which everything random on the machine draws from -
				NewtonScript's Random, the Handwriting Practice words, the
				IrDA and LocalTalk addresses, a new package's id - and the
				state GetRandomState/SetRandomState save and restore.

				It is the "minimal standard" generator of Park and Miller:
				the state is multiplied by 16807 and taken modulo 2^31 - 1
				(CompMul and CompDiv in the ROM).  A state of nought stays
				nought, which is what the machine starts from until
				UserBoot seeds it.

				Named NewtonRand and NewtonSrand here so that they do not
				take the host C library's names.

	Reconstructed from the MP2x00 US ROM (0x003503d0-0x0035051c); each
	function cites its origin.
*/

#ifndef __RANDOM_H
#define __RANDOM_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

long	NewtonRand(void);						// ROM 0x003503d0 rand - 1 .. 2^31 - 2 (0 while the seed is 0)
void	NewtonSrand(ULong seed);				// ROM 0x003504dc srand
long	SizeofRandState(void);					// ROM 0x003504ec sizeof_rand_state - 4
void	GetRandState(void* state);				// ROM 0x003504f4 get_rand_state - the state word, big-endian
void	SetRandState(const void* state);		// ROM 0x00350508 set_rand_state

#endif	/* __RANDOM_H */
