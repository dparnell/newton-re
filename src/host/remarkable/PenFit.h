/*
	File:		host/remarkable/PenFit.h

	Contains:	The Marker read straight from its own input device
				(NEWTON_RM_PEN=evdev), its points mapped into newton's
				framebuffer by what AppLoad's own pen events show
				(docs/host-remarkable.md, "The pen read directly").

				AppLoad hands newton the Marker through xochitl's event
				loop, which newton's own updates keep busy redrawing the
				window, so its points come late and in bunches - and the
				Newton, sampling the latest point eighty times a second,
				draws a bunch as one straight jump (jagged ink).  The
				device's own events come at the Marker's pace, but in the
				panel's pixels, and AppLoad does not always show the
				framebuffer square on the panel: v0.4.2, with the type
				folio turning the interface to landscape, shows it turned
				and scaled into a portrait area in the middle.  Rather than
				guess AppLoad's geometry, it is learnt: the first point of
				each stroke (and its last) is the same place in both
				streams, so each stroke gives two pairs of a panel point and
				a framebuffer point, and an affine map fitted to the last
				few pairs (least squares) maps the device's points from then
				on.  Until it fits - and again from scratch when a new pair
				is more than `tolerance` framebuffer pixels from what the
				map says (the tablet turned, the folio moved) - AppLoad's
				own points are the pen.

				Plain sums, no headers: tests/test_PenFit.cpp.
*/

#ifndef __PENFIT_H
#define __PENFIT_H

#include <math.h>

struct PenFit
{
	enum { kPairs = 8 };
	double	fU[kPairs], fV[kPairs], fX[kPairs], fY[kPairs];
	int		fCount = 0, fNext = 0;
	bool	fReady = false;
	double	fA = 1, fB = 0, fC = 0, fD = 0, fE = 1, fF = 0;	// x = a u + b v + c, y = d u + e v + f
	double	fTolerance = 24;

	void	Reset(void)		{ fCount = fNext = 0; fReady = false; }
	bool	Ready(void) const	{ return fReady; }

	void	Map(double u, double v, double* x, double* y) const
	{
		*x = fA * u + fB * v + fC;
		*y = fD * u + fE * v + fF;
	}

	// a panel point (u, v) and the framebuffer point AppLoad gave for it.
	// ==> false if it disagreed with the map, which is then learnt afresh
	bool	Add(double u, double v, double x, double y)
	{
		bool agreed = true;
		if (fReady)
		{
			double mx, my;
			Map(u, v, &mx, &my);
			if (hypot(mx - x, my - y) > fTolerance)
			{
				Reset();
				agreed = false;
			}
		}
		fU[fNext] = u; fV[fNext] = v; fX[fNext] = x; fY[fNext] = y;
		fNext = (fNext + 1) % kPairs;
		if (fCount < kPairs)
			fCount++;
		Fit();
		return agreed;
	}

private:
	static double	Det3(const double m[3][3])
	{
		return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
			 - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
			 + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	}

	// the least-squares solution of [u v 1] . (p q r) = t over the pairs
	bool	Solve(const double* t, double* p, double* q, double* r) const
	{
		double m[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } }, b[3] = { 0, 0, 0 };
		for (int i = 0; i < fCount; i++)
		{
			double row[3] = { fU[i], fV[i], 1 };
			for (int j = 0; j < 3; j++)
			{
				for (int k = 0; k < 3; k++)
					m[j][k] += row[j] * row[k];
				b[j] += row[j] * t[i];
			}
		}
		double det = Det3(m);
		if (fabs(det) < 1e-6 * (m[0][0] * m[1][1] * m[2][2] + 1))
			return false;							// the points in a line: not yet
		double s[3];
		for (int col = 0; col < 3; col++)
		{
			double c[3][3];
			for (int j = 0; j < 3; j++)
				for (int k = 0; k < 3; k++)
					c[j][k] = k == col ? b[j] : m[j][k];
			s[col] = Det3(c) / det;
		}
		*p = s[0]; *q = s[1]; *r = s[2];
		return true;
	}

	void	Fit(void)
	{
		fReady = false;
		if (fCount < 3)
			return;
		double a, b, c, d, e, f;
		if (!Solve(fX, &a, &b, &c) || !Solve(fY, &d, &e, &f))
			return;
		// every pair within the tolerance of it, or it is not to be trusted
		for (int i = 0; i < fCount; i++)
		{
			double mx = a * fU[i] + b * fV[i] + c, my = d * fU[i] + e * fV[i] + f;
			if (hypot(mx - fX[i], my - fY[i]) > fTolerance)
				return;
		}
		fA = a; fB = b; fC = c; fD = d; fE = e; fF = f;
		fReady = true;
	}
};

#endif	/* __PENFIT_H */
