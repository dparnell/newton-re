/*
	File:		recognition/ShapeSolver.cpp

	Contains:	SolveEquations: how the shape domain tidies a shape.

				FindEquations writes what a shape ought to be - these two
				sides parallel, those two the same length, this corner a
				right angle - as linear equations over the shape's edge
				vectors (x[2i-1], x[2i] is the step from key point i-1 to
				key point i; x[0] is 1).  They are generally more than the
				shape has freedoms, so they are not solved but *minimised*:
				each equation is squared into one quadratic form
				(SquareLinear, AddBilinears), its gradient worked out once
				as a linear form per variable (FindGradient), and the
				variables started from the shape as drawn and moved by
				conjugate gradients (Minimize) until the sum of the squares
				stops falling.  What comes out is scaled so that the shape
				covers the box it covered as drawn (MapSolutionToBounds).

				The minimiser is Numerical Recipes' frprmn with linmin,
				mnbrak and dbrent, in 16.16 fixed point: Minimize is the
				Polak-Ribiere direction set, LineMinimize the search along
				one direction (Func1D and DFunc1D being the function and its
				slope along it), BracketMin brackets a minimum by golden
				steps and parabolic guesses, and Minimize1D is Brent's
				method with derivatives.  The ROM's differences from the
				book are kept and noted where they are.

				A MixFunc can also carry up to five product terms whose
				absolute values are added to the function, with their own
				gradient; nothing in the ROM ever makes one, so they are
				always nought, but TheFunction and TheGradient evaluate
				them all the same.

	Reconstructed from the MP2x00 US ROM (0x0020fae8-0x002105c0 and
	0x00219128-0x0021a000); each function cites its origin.
*/

#include "ShapeGeometry.h"
#include "RecObject.h"
#include "NewtonMemory.h"
#include "FixedMath.h"

#include <string.h>


MixFunc		currFunction;			// ROM 0x0c106f10 currFunction
MixGradEl*	currGradient;			// ROM 0x0c104c90 currGradient


// The absolute value of a 32-bit word as the ARM takes it: the most
// negative stays as it is.
static inline long
WAbs(long v)
{
	return v < 0 ? (long) (int32_t) (0u - (uint32_t) v) : v;
}


// ROM 0x0020fae8 SolveEquations__FP8EqSystemPl
// Moves `values` (x[1..n], the shape's edge vectors, x[0] being 1) to where
// the sum of the squares of the system's equations is least, keeping the
// size the shape was drawn at.  False if it could not be done or the least
// sum is not small (under 8 in 16.16).
Boolean
SolveEquations(EqSystem* system, long* values)
{
	Boolean solved = false;
	// DEVIATION: the ROM sets only the count of its scratch Bilinear, so
	// when it gives it back without InitFunction having run it deletes
	// whatever handle its stack held in the first row; zeroed here.
	Bilinear square;
	memset(&square, 0, sizeof(square));
	currFunction.fQuad.fN = 0;
	// DEVIATION: 37 host MixGradEls are larger than the ROM's 0x4a0 bytes.
	currGradient = (MixGradEl*) NewPtr(sizeof(MixGradEl) * 37);
	Boolean failed = (currGradient == nil);
	if (!failed)
	{
		InitGradient(currGradient);
		if (system->fN < 37 && system->fCount < 41)
		{
			values[0] = 0x10000;
			long n = system->fN;
			long count = system->fCount;
			Linear* eqs = system->fEqs;
			failed = InitFunction(n, &currFunction, &square);
			if (!failed)
			{
				for (long i = 0; i < count; i++)
				{
					if (eqs[i].fKind == 0)
					{
						SquareLinear(&eqs[i], &square);
						AddBilinears(&currFunction.fQuad, &square, &currFunction.fQuad);
					}
				}
				ReleaseBilin(&square);
				failed = FindGradient(&currFunction, currGradient);
				if (!failed)
				{
					FRect bounds;
					CalcSolutionBounds(values, n, &bounds);
					// an eighth of the size, so that the squares stay in range
					for (long i = 1; i <= n; i++)
						values[i] = values[i] >> 3;
					long iterations, least;
					Boolean done = Minimize(values, n, 0x200, &iterations, &least, TheFunction, TheGradient);
					solved = false;
					if (done)
						solved = WAbs(least) < 0x80000;
					for (long i = 1; i <= n; i++)
						values[i] = ShiftLeft(values[i], 3);
					if (solved)
						MapSolutionToBounds(values, n, bounds);
				}
			}
		}
	}
	ReleaseBilin(&square);
	ReleaseBilin(&currFunction.fQuad);
	if (currGradient != nil)
	{
		ReleaseGradient(currGradient);
		DisposPtr((Ptr) currGradient);
		currGradient = nil;
	}
	if (failed)
		solved = false;
	return solved;
}


// ROM 0x0020fcfc CalcSolutionBounds__FPCllP5FRect
// The box the shape covers, relative to its first point: the edge vectors
// added up in turn.  (It always includes the first point, the box starting
// as nought.)
void
CalcSolutionBounds(const long* x, long n, FRect* bounds)
{
	memset(bounds, 0, sizeof(FRect));
	long h = 0, v = 0;
	for (long i = 1; i < n; i += 2)
	{
		h = x[i] + h;
		if (h < bounds->left)
			bounds->left = h;
		else if (bounds->right < h)
			bounds->right = h;
		v = x[i + 1] + v;
		if (v < bounds->top)
			bounds->top = v;
		else if (bounds->bottom < v)
			bounds->bottom = v;
	}
}


// ROM 0x0020fd94 InitFunction__FlP7MixFuncP8Bilinear
// Both quadratic forms made n+1 rows of n+1 noughts ('cof2' for the
// function, 'cof3' for the scratch), and no mix terms.  True if the memory
// ran out.
Boolean
InitFunction(long n, MixFunc* function, Bilinear* scratch)
{
	scratch->fN = n;
	function->fQuad.fN = n;
	for (long i = 0; i <= n; i++)
	{
		function->fQuad.fRows[i] = nil;
		scratch->fRows[i] = nil;
	}
	Handle last = nil;
	for (long i = 0; i <= n; i++)
	{
		Handle row = MakeHandle((n + 1) * 4);
		NameHandle(row, 'cof2');
		if (row == nil)
			return true;
		function->fQuad.fRows[i] = row;
		for (long j = 0; j <= n; j++)
			((Fixed*) *row)[j] = 0;
		last = MakeHandle((n + 1) * 4);
		NameHandle(last, 'cof3');
		if (last == nil)
			return true;
		scratch->fRows[i] = last;
		for (long j = 0; j <= n; j++)
			((Fixed*) *last)[j] = 0;
	}
	function->fMixCount = 0;
	for (long t = 0; t < 5; t++)
	{
		function->fMix[t].fKind = 0;
		for (long k = 0; k < 4; k++)
			function->fMix[t].fV[k] = 0;
	}
	// ROM QUIRK: with no rows (n < 0) this tests a register nothing set.
	if (last == nil)
		return true;
	return false;
}


// ROM 0x0020fee4 TheFunction__FlPlT2
// The function the minimiser minimises: the quadratic at x (with x[0]
// made 1), then each mix term's value into terms[1..] and its absolute
// value added.  terms[0] is the quadratic alone, which TheGradient looks
// at.  Nought when n is not the function's size.
long
TheFunction(long n, long* x, long* terms)
{
	long sum = 0;
	if (n == currFunction.fQuad.fN)
	{
		x[0] = 0x10000;
		for (long i = 0; i <= n; i++)
		{
			Fixed* row = (Fixed*) *currFunction.fQuad.fRows[i];
			for (long j = i; j <= n; j++)
				sum = FixedMultiply(row[j], FixedMultiply(x[i], x[j])) + sum;
		}
		terms[0] = sum;
		for (long t = 0; t < currFunction.fMixCount; t++)
		{
			MixTerm* term = &currFunction.fMix[t];
			long value;
			if (term->fKind == 1)
				value = FixedMultiply(x[term->fV[0]], x[term->fV[1]]) + FixedMultiply(x[term->fV[2]], x[term->fV[3]]);
			else
				value = FixedMultiply(x[term->fV[0]], x[term->fV[1]]) - FixedMultiply(x[term->fV[2]], x[term->fV[3]]);
			terms[t + 1] = value;
			sum = sum + WAbs(value);
		}
	}
	return sum;
}


// ROM 0x00210028 TheGradient__FlPlN22
// The gradient at x: each variable's linear form (only while the quadratic
// is above 2 in 16.16 - ROM QUIRK: below that its slope is left out, as if
// it were flat) plus each mix term's derivative, signed by the term and
// scaled by it once it is under 1 (the absolute value smoothed at nought).
void
TheGradient(long n, long* x, long* terms, long* gradient)
{
	x[0] = 0x10000;
	for (long i = 1; i <= n; i++)
	{
		gradient[i] = 0;
		MixGradEl* el = &currGradient[i];
		if (terms[0] > 2)
		{
			Fixed* coeffs = (Fixed*) *el->fCoeffs;
			for (long j = 0; j <= n; j++)
				gradient[i] = FixedMultiply(coeffs[j], x[j]) + gradient[i];
		}
		for (long t = 0; t < currFunction.fMixCount; t++)
		{
			long var = el->fMix[t];
			long d;
			if (var < 0)
				d = -x[-var];
			else if (var > 0)
				d = x[var];
			else
				continue;
			long value = terms[t + 1];
			long size = value;
			if (value < 0)
			{
				d = -d;
				size = WAbs(value);
			}
			if (size < 0x10000)
				d = FixedMultiply(d, WAbs(value));
			gradient[i] = gradient[i] + d;
		}
	}
}


// ROM 0x00210150 MapSolutionToBounds__FPllRC5FRect
// The solution scaled, across and down separately, so that it covers the
// box the shape covered before.
void
MapSolutionToBounds(long* x, long n, const FRect& bounds)
{
	FRect now;
	CalcSolutionBounds(x, n, &now);
	Fixed across = FixedDivide(bounds.right - bounds.left, now.right - now.left);
	Fixed down = FixedDivide(bounds.bottom - bounds.top, now.bottom - now.top);
	for (ULong i = 1; (long) i <= n; i++)
		x[i] = FixedMultiply(x[i], (i & 1) ? across : down);
}


// ROM 0x002101e8 SquareLinear__FP6LinearP8Bilinear
// The square of a linear form as an upper triangle: c[i]^2 on the
// diagonal, 2 c[i] c[j] above it.  Nothing when the sizes differ.
void
SquareLinear(Linear* linear, Bilinear* square)
{
	long n = linear->fN;
	if (n < 0 || square->fN != n)
		return;
	for (long i = 0; i <= linear->fN; i++)
	{
		Fixed* row = (Fixed*) *square->fRows[i];
		for (long j = 0; j < i; j++)
			row[j] = 0;
		Fixed ci = ((Fixed*) *linear->fCoeffs)[i];
		row[i] = FixedMultiply(ci, ci);
		for (long j = i + 1; j <= linear->fN; j++)
		{
			Fixed* c = (Fixed*) *linear->fCoeffs;
			row[j] = ShiftLeft(FixedMultiply(c[i], c[j]), 1);
		}
	}
}


// ROM 0x002102cc AddBilinears__FP8BilinearN21
// sum = a + b over the upper triangle (sum may be a).  Nothing when a and b
// differ in size.
void
AddBilinears(Bilinear* a, Bilinear* b, Bilinear* sum)
{
	long n = a->fN;
	if (n != b->fN)
		return;
	sum->fN = n;
	for (long i = 0; i <= n; i++)
		for (long j = i; j <= n; j++)
			((Fixed*) *sum->fRows[i])[j] = ((Fixed*) *a->fRows[i])[j] + ((Fixed*) *b->fRows[i])[j];	// (Fixed: the rows are the ARM's words, MakeHandle((n+1)*4))
}


// ROM 0x0021034c InitGradient__FP9MixGradEl
void
InitGradient(MixGradEl* gradient)
{
	for (ULong i = 0; i < 0x25; i++)
	{
		gradient[i].fN = 0;
		gradient[i].fCoeffs = nil;
	}
}


// ROM 0x00210370 FindGradient__FP7MixFuncP9MixGradEl
// The derivative of the quadratic by each variable as a linear form
// ('cof1'): column i of the triangle above the diagonal, twice the diagonal,
// row i after it.  And for each mix term, which variable its derivative by
// x[i] is.  True if the memory ran out.
Boolean
FindGradient(MixFunc* function, MixGradEl* gradient)
{
	long n = function->fQuad.fN;
	for (long i = 1; i <= n; i++)
	{
		MixGradEl* el = &gradient[i];
		el->fN = n;
		el->fFlag = 0;
		Handle h = MakeHandle((n + 1) * 4);
		NameHandle(h, 'cof1');
		if (h == nil)
			return true;
		el->fCoeffs = h;
		Fixed* c = (Fixed*) *h;
		for (long k = 0; k < i; k++)
			c[k] = ((Fixed*) *function->fQuad.fRows[k])[i];
		c[i] = ShiftLeft(((Fixed*) *function->fQuad.fRows[i])[i], 1);
		for (long k = i + 1; k <= n; k++)
			c[k] = ((Fixed*) *function->fQuad.fRows[i])[k];
		for (long t = 0; t < function->fMixCount; t++)
		{
			MixTerm* term = &function->fMix[t];
			long k;
			for (k = 1; k < 5; k++)
				if (term->fV[k - 1] == i)
					break;
			switch (k)
			{
			case 0:
				return true;
			case 1:
				el->fMix[t] = term->fV[1];
				break;
			case 2:
				el->fMix[t] = term->fV[0];
				break;
			case 3:
				el->fMix[t] = term->fKind * term->fV[3];
				break;
			case 4:
				el->fMix[t] = term->fKind * term->fV[2];
				break;
			case 5:
				el->fMix[t] = 0;
				break;
			default:
				return true;
			}
		}
	}
	return false;
}


// ROM 0x0021052c ReleaseGradient__FP9MixGradEl
void
ReleaseGradient(MixGradEl* gradient)
{
	for (long i = 0; i < 0x25; i++)
	{
		if (gradient[i].fCoeffs != nil)
		{
			DeleteHandle(gradient[i].fCoeffs);
			gradient[i].fCoeffs = nil;
		}
	}
}


// ROM 0x0021056c ReleaseBilin__FP8Bilinear
void
ReleaseBilin(Bilinear* bilinear)
{
	for (long i = 0; i <= bilinear->fN; i++)
	{
		if (bilinear->fRows[i] != nil)
		{
			DeleteHandle(bilinear->fRows[i]);
			bilinear->fRows[i] = nil;
		}
	}
	bilinear->fN = 0;
}


#pragma mark - The minimiser


// ROM 0x00219128 Minimize__FPllT2N21PFlPlT2_lPFlPlN22_v
// Numerical Recipes' frprmn: p moved to a minimum of f by line searches
// along Polak-Ribiere conjugate directions, at most 100 of them.  True when
// it settled (the value stopped falling by more than ftol of itself, fell
// under 8, or the gradient vanished), false when it ran out of iterations
// or n is too large.
Boolean
Minimize(long* p, long n, long ftol, long* iterations, long* fret, NFunction f, NGradient df)
{
	long terms[6];			// the ROM's has 5, the sixth being xi[0], which nothing uses
	long xi[37], g[37], h[37];
	long fp = f(n, p, terms);
	df(n, p, terms, xi);
	if (n >= 0x25)
		return false;
	Boolean flat = true;
	for (long j = 1; j <= n; j++)
	{
		long v = -xi[j];
		g[j] = v;
		h[j] = v;
		xi[j] = v;
		if (v != 0)
			flat = false;
	}
	if (n > 0 && !flat)
	{
		for (long its = 1; its < 0x65; its++)
		{
			*iterations = its;
			LineMinimize(n, p, xi, fret, terms, f, df);
			Fixed limit = FixedMultiply(ftol, WAbs(*fret) + WAbs(fp) + 1);
			if (ShiftLeft(WAbs(*fret - fp), 1) <= limit)
				return true;
			// ROM QUIRK: f is worked out again and its value thrown away
			// (it leaves the terms the gradient reads); fp is the line
			// search's answer.
			f(n, p, terms);
			fp = *fret;
			df(n, p, terms, xi);
			if (fp < 8 || n < 1)
				return true;
			long gg = 0, dgg = 0;
			for (long j = 1; j <= n; j++)
			{
				gg = FixedMultiply(g[j], g[j]) + gg;
				dgg = FixedMultiply(xi[j] + g[j], xi[j]) + dgg;
			}
			if (gg == 0)
				return true;
			Fixed gam = FixedDivide(dgg, gg);
			for (long j = 1; j <= n; j++)
			{
				g[j] = -xi[j];
				// the book's h = g + gam h; with gam over 10 the ROM takes
				// h + g / gam instead, the same direction scaled down
				long step, base;
				if (WAbs(gam) < 0xa0001)
				{
					step = FixedMultiply(gam, h[j]);
					base = g[j];
				}
				else
				{
					step = FixedDivide(-xi[j], gam);
					base = h[j];
				}
				h[j] = step + base;
				xi[j] = step + base;
			}
		}
		return false;
	}
	*iterations = 0;
	*fret = fp;
	return true;
}


// ROM 0x002193a0 LineMinimize__FlPlN32PFlPlT2_lPFlPlN22_v
// Numerical Recipes' linmin: the minimum along dir from p, bracketed from
// the step 0..1/8 and found by Minimize1D to 1/128 of the step; p moved
// there, dir made the step taken and fret the value found.
void
LineMinimize(long n, long* p, long* dir, long* fret, long* terms, NFunction f, NGradient df)
{
	long ax = 0, xx = 0x2000, bx, fa, fx, fb;
	// DEVIATION: the ROM leaves xmin as its stack had it when Minimize1D
	// runs out of iterations without setting it.
	long xmin = 0;			// (Minimize1D's out-parameter: the ROM's word, like ax and bx)
	BracketMin(&ax, &xx, &bx, &fa, &fx, &fb, Func1D, n, p, dir, f);
	*fret = Minimize1D(ax, xx, bx, Func1D, DFunc1D, n, p, dir, f, df, 0x200, &xmin, terms);
	for (long j = 1; j <= n; j++)
	{
		Fixed step = FixedMultiply(dir[j], xmin);
		dir[j] = step;
		p[j] = p[j] + step;
	}
}


// ROM 0x002194a0 Func1D__FlT1PlT3PFlPlT2_lT3
// f at p + t dir.
long
Func1D(long t, long n, long* p, long* dir, NFunction f, long* terms)
{
	long xt[37];			// xt[0] as the stack had it: f sets it
	for (long j = 1; j <= n; j++)
		xt[j] = FixedMultiply(t, dir[j]) + p[j];
	return f(n, xt, terms);
}


// ROM 0x00219510 DFunc1D__FlT1PlT3PFlPlN22_vT3
// The slope of f along dir at p + t dir: the gradient there projected
// on dir, over dir's length (so a slope per unit of distance, not per
// unit of t).
long
DFunc1D(long t, long n, long* p, long* dir, NGradient df, long* terms)
{
	long xt[37], gradient[37];
	for (long j = 1; j <= n; j++)
		xt[j] = FixedMultiply(t, dir[j]) + p[j];
	df(n, xt, terms, gradient);
	long sq = 0;
	for (long j = 1; j <= n; j++)
		sq = FixedMultiply(dir[j], dir[j]) + sq;
	Fixed length = (FractSquareRoot(sq) + 0x40) >> 7;
	long slope = 0;
	for (long j = 1; j <= n; j++)
		slope = FixedMultiply(gradient[j], dir[j]) + slope;
	return FixedDivide(slope, length);
}


// ROM 0x002195fc BracketMin__FPlN51PFlT1PlT3PFlPlT2_lT3_llN21PFlPlT2_l
// Numerical Recipes' mnbrak: from a and b, a, b and c with f(b) below
// f(a) and f(c), by golden steps (1.618) and parabolic guesses limited to
// 30 times the last step (the book has 100).
void
BracketMin(long* a, long* b, long* c, long* fa, long* fb, long* fc, Function1D f, long n, long* p, long* dir, NFunction nf)
{
	const Fixed kGold = 0x19e37;
	// the terms f leaves are this function's own: the line search does
	// not want them
	long terms[6];
	*fa = f(*a, n, p, dir, nf, terms);
	*fb = f(*b, n, p, dir, nf, terms);
	if (*fa < *fb)
	{
		long t = *a; *a = *b; *b = t;
		t = *fb; *fb = *fa; *fa = t;
	}
	*c = FixedMultiply(kGold, *b - *a) + *b;
	*fc = f(*c, n, p, dir, nf, terms);
	if (!(*fc < *fb))
		return;
	long u, fu;
	do
	{
		Fixed r = FixedMultiply(*b - *a, *fb - *fc);
		Fixed q = FixedMultiply(*b - *c, *fb - *fa);
		// SIGN(max(|q-r|, 1), q-r) - ROM QUIRK: q = r counts as negative
		long qr = q - r;
		long m = WAbs(qr);
		if (m < 2)
			m = 1;
		if (qr < 1)
			m = -WAbs(m);
		m = ShiftLeft(m, 1);
		Fixed num = FixedMultiply(*b - *c, q) - FixedMultiply(*b - *a, r);
		u = *b - FixedDivide(num, m);
		long ulim = (long) (int32_t) ((uint32_t) *c * 30 - (uint32_t) *b * 29);
		if (FixedMultiply(*b - u, u - *c) >= 1)
		{
			// the parabola's minimum is between b and c
			fu = f(u, n, p, dir, nf, terms);
			if (fu < *fc)
			{
				*a = *b;
				*b = u;
				*fa = *fb;
				*fb = fu;
				return;
			}
			if (*fb < fu)
			{
				*c = u;
				*fc = fu;
				return;
			}
			u = FixedMultiply(kGold, *c - *b) + *c;
		}
		else if (FixedMultiply(*c - u, u - ulim) >= 1)
		{
			// between c and the limit
			fu = f(u, n, p, dir, nf, terms);
			if (fu < *fc)
			{
				*b = *c;
				*c = u;
				u = FixedMultiply(kGold, u - *b) + *c;
				*fb = *fc;
				*fc = fu;
			}
			else
				goto shift;
		}
		else if (FixedMultiply(u - ulim, ulim - *c) >= 0)
			u = ulim;
		else
			u = FixedMultiply(kGold, *c - *b) + *c;
		fu = f(u, n, p, dir, nf, terms);
shift:
		*a = *b;
		*b = *c;
		*c = u;
		*fa = *fb;
		*fb = *fc;
		*fc = fu;
	} while (fu < *fb);
}


// ROM 0x002199d8 Minimize1D__FlN21PFlT1PlT3PFlPlT2_lT3_lPFlT1PlT3PFlPlN22_vT3_lT1PlT7PFlPlT2_lPFlPlN22_vT1N27
// Numerical Recipes' dbrent: the minimum of f in the bracket (ax, bx, cx)
// by Brent's method with the slope, to tol of the position.  The value
// there, and the position in xmin; nought if 100 steps did not find it,
// and then xmin is not set.  ROM QUIRKS: the step taken when the proposed
// one is too small also gives up when it lands on the bracket's end, and
// a new point replaces x only when strictly lower.
long
Minimize1D(long ax, long bx, long cx, Function1D f, Gradient1D df, long n, long* p, long* dir, NFunction nf, NGradient ndf, long tol, long* xmin, long* terms)
{
	long e = 0;
	long a = cx, b = cx;
	if (ax < cx)
		a = ax;
	if (cx < ax)
		b = ax;
	long x = bx, w = bx, v = bx;
	long fx = f(x, n, p, dir, nf, terms);
	long dx = df(x, n, p, dir, ndf, terms);
	long fw = fx, fv = fx, dw = dx, dv = dx;
	long d = 0;
	for (long iter = 1; ; )
	{
		long xm = (a + b) >> 1;
		long tol1 = FixedMultiply(tol, WAbs(x)) + 1;
		long tol2 = tol1 * 2;
		if (WAbs(x - xm) <= tol2 - ((b - a) >> 1))
		{
			*xmin = x;
			return fx;
		}
		long step;
		if (tol1 < WAbs(e))
		{
			long d2 = (b - a) * 2;
			long d1 = d2;
			if (dw != dx)
				d1 = FixedMultiplyDivide(w - x, dx, dx - dw);
			if (dv != dx)
				d2 = FixedMultiplyDivide(v - x, dx, dx - dv);
			Boolean ok1 = !(FixedMultiply(a - (x + d1), (x + d1) - b) < 1 || FixedMultiply(dx, d1) > 0);
			Boolean ok2 = !(FixedMultiply(a - (x + d2), (x + d2) - b) < 1 || FixedMultiply(dx, d2) > 0);
			Boolean bisect = false;
			if (!ok1)
			{
				if (ok2)
					d1 = d2;
				else
					bisect = true;
			}
			else if (ok2 && WAbs(d2) <= WAbs(d1))
				d1 = d2;
			if (!bisect && (WAbs(e) >> 1) < WAbs(d1))
				bisect = true;
			if (bisect)
			{
				e = (dx < 0) ? b - x : a - x;
				step = e >> 1;
			}
			else
			{
				step = d1;
				long u = x + d1;
				long room = u - a;
				if (tol2 <= room)
					room = b - u;
				e = d;
				if (room < tol2)
					step = (xm - x < 1) ? -WAbs(tol1) : WAbs(tol1);
			}
		}
		else
		{
			e = (dx < 0) ? b - x : a - x;
			step = e >> 1;
		}

		long u, fu;
		if (tol1 < WAbs(step))
		{
			u = x + step;
			fu = f(u, n, p, dir, nf, terms);
		}
		else
		{
			u = x + ((step < 1) ? -WAbs(tol1) : WAbs(tol1));
			fu = f(u, n, p, dir, nf, terms);
			if (fx < fu || u == a || u == b)
			{
				*xmin = x;
				return fx;
			}
		}
		long du = df(u, n, p, dir, ndf, terms);
		if (fu < fx)
		{
			if (x <= u)
				a = x;
			else
				b = x;
			v = w; fv = fw; dv = dw;
			w = x; fw = fx; dw = dx;
			x = u; fx = fu; dx = du;
		}
		else
		{
			if (u < x)
				a = u;
			else
				b = u;
			if (!(fw < fu) || w == x)
			{
				v = w; fv = fw; dv = dw;
				w = u; fw = fu; dw = du;
			}
			else if (fu < fv || v == x || v == w)
			{
				v = u; fv = fu; dv = du;
			}
		}
		d = step;
		iter++;
		if (100 < iter)
			return 0;
	}
}
