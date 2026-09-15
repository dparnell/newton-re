// Host unit test for the 64-bit arithmetic (src/toolbox/CompMath.cpp):
// CompAdd/CompSub with carries, CompCompare, CompMul, CompDiv (rounding,
// truncation with remainder, saturation) and CompShift (rounding).

#include "Newton.h"
#include "CompMath.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Int64 Make(SLong hi, ULong lo)
{
	Int64 x;
	x.hi = hi;
	x.lo = lo;
	return x;
}

int main()
{
	// add with a carry out of the low word
	Int64 a = Make(0, 0xFFFFFFFF), one = Make(0, 1);
	CompAdd(&one, &a);
	EXPECT(a.hi == 1 && a.lo == 0);
	// and back
	CompSub(&one, &a);
	EXPECT(a.hi == 0 && a.lo == 0xFFFFFFFF);

	Int64 big = Make(5, 10), small = Make(5, 9), negative = Make(-1, 0xFFFFFFFF);
	EXPECT(CompCompare(&big, &small) > 0 && CompCompare(&small, &big) < 0 && CompCompare(&big, &big) == 0);
	EXPECT(CompCompare(&negative, &small) < 0);

	// multiply: signs and a product past 32 bits
	Int64 product;
	CompMul(0x10000, 0x10000, &product);
	EXPECT(product.hi == 1 && product.lo == 0);
	CompMul(-3, 7, &product);
	EXPECT(product.hi == -1 && product.lo == (ULong) 0xFFFFFFEB);
	CompMul(-3, -7, &product);
	EXPECT(product.hi == 0 && product.lo == 21);

	// divide: exact, rounded to nearest without a remainder pointer, truncated with one
	Int64 n = Make(1, 0);						// 2^32
	long remainder = -1;
	EXPECT(CompDiv(&n, 0x10000, &remainder) == 0x10000 && remainder == 0);
	Int64 seven = Make(0, 7);
	EXPECT(CompDiv(&seven, 2, nil) == 4);		// 3.5 rounds up
	EXPECT(CompDiv(&seven, 2, &remainder) == 3 && remainder == 1);
	Int64 minusSeven = Make(-1, 0xFFFFFFF9);
	EXPECT(CompDiv(&minusSeven, 2, &remainder) == -3 && remainder == -1);
	EXPECT(CompDiv(&seven, -2, &remainder) == -3 && remainder == 1);
	// saturation: 2^40 / 1 does not fit a word
	Int64 huge = Make(0x100, 0);
	EXPECT(CompDiv(&huge, 1, nil) == 0x7fffffff);
	EXPECT(CompDiv(&huge, -1, &remainder) == (long) (SLong) -0x7fffffff - 1 && remainder == (long) (SLong) -0x7fffffff - 1);

	// shift: right rounds half up, left is plain
	Int64 s = Make(0, 5);
	CompShift(&s, 1);							// 2.5 -> 3
	EXPECT(s.hi == 0 && s.lo == 3);
	s = Make(0, 4);
	CompShift(&s, 1);
	EXPECT(s.lo == 2);
	s = Make(0, 1);
	CompShift(&s, -33);
	EXPECT(s.hi == 2 && s.lo == 0);
	s = Make(2, 0);
	CompShift(&s, 33);
	EXPECT(s.hi == 0 && s.lo == 1);
	s = Make(3, 0);								// 3 * 2^32 >> 33 = 1.5 -> 2
	CompShift(&s, 33);
	EXPECT(s.hi == 0 && s.lo == 2);

	if (failures == 0)
		printf("test_CompMath: all passed\n");
	return failures != 0;
}
