// Random test (Random.h): the ROM's C library rand() is Park and Miller's
// minimal standard generator, whose sequence from a seed of 1 is
// published (16807, 282475249, 1622650073, ... and 1043618065 as the
// 10000th), and whose state of nought stays nought.

#include "Random.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


int
main()
{
	// the machine's state before UserBoot seeds it
	NewtonSrand(0);
	EXPECT(NewtonRand() == 0 && NewtonRand() == 0);

	NewtonSrand(1);
	EXPECT(NewtonRand() == 16807);
	EXPECT(NewtonRand() == 282475249);
	EXPECT(NewtonRand() == 1622650073);
	NewtonSrand(1);
	long r = 0;
	for (long i = 0; i < 10000; i++)
		r = NewtonRand();
	EXPECT(r == 1043618065);

	// A negative seed - which is what a machine whose clock has outgrown a
	// NewtonScript integer hands the generator (src/host/demo/thirdparty-apps.ns:
	// Mahjongg seeds it with TimeInSeconds() mod 5 + 1).  The ROM brings the
	// remainder back above nought with `SUBLT r0, r0, #0x80000001` in an ARM
	// register (0x00350414), where the subtraction wraps to adding 0x7fffffff;
	// in a wider word it does not wrap, the state stays negative for ever and
	// every number after it is negative too.
	NewtonSrand((ULong) -3);
	EXPECT(NewtonRand() == 2147433226);				// -3 * 16807 + 0x7fffffff
	Boolean allInRange = true;
	for (long i = 0; i < 1000; i++)
	{
		long v = NewtonRand();
		if (v < 0 || v >= 0x7fffffff)
			allInRange = false;
	}
	EXPECT(allInRange);

	// back to the published sequence for what follows
	NewtonSrand(1);
	for (long i = 0; i < 10000; i++)
		r = NewtonRand();

	// the state round-trips through its four big-endian bytes
	unsigned char state[4];
	EXPECT(SizeofRandState() == 4);
	GetRandState(state);
	EXPECT(state[0] == 0x3e && state[1] == 0x34 && state[2] == 0x59 && state[3] == 0x11);	// 1043618065
	long next = NewtonRand();
	SetRandState(state);
	EXPECT(NewtonRand() == next);

	printf("test_Random: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
