// The map from the Marker's own panel points to newton's framebuffer, learnt
// from AppLoad's pen events (host/remarkable/PenFit.h): not ready until three
// pairs off a line agree; then exact for the geometry AppLoad v0.4.2 shows
// with the folio attached (the portrait framebuffer turned a quarter and
// scaled into the middle of the landscape screen) and for the plain one
// (the framebuffer square on the panel); a pair that disagrees - the tablet
// turned - drops it, and it is learnt again for the new geometry; noise of
// a few pixels is taken.
#include "../PenFit.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// the panel's point turned a quarter and scaled by 0.75 into a 1215 x 1620
// area 472 from the left of the landscape screen (one way of AppLoad's)
static void
Turned(double u, double v, double* x, double* y)
{
	*x = (2159 - v - 472.5) / 0.75;
	*y = u / 0.75;
}

static void
Square(double u, double v, double* x, double* y)
{
	*x = u;
	*y = v;
}

static const double kPoints[][2] = { { 300, 900 }, { 1200, 1000 }, { 700, 1500 }, { 800, 1100 }, { 1000, 1300 }, { 500, 1200 } };


static void
Learn(PenFit& fit, void (*geometry)(double, double, double*, double*), double noise)
{
	for (int i = 0; i < 6; i++)
	{
		double x, y;
		geometry(kPoints[i][0], kPoints[i][1], &x, &y);
		fit.Add(kPoints[i][0], kPoints[i][1], x + (i & 1 ? noise : -noise), y + (i & 2 ? noise : -noise));
	}
}


static bool
Maps(const PenFit& fit, void (*geometry)(double, double, double*, double*), double within)
{
	for (double u = 400; u < 1300; u += 150)
		for (double v = 900; v < 1600; v += 170)
		{
			double x, y, mx, my;
			geometry(u, v, &x, &y);
			fit.Map(u, v, &mx, &my);
			if (fabs(mx - x) > within || fabs(my - y) > within)
				return false;
		}
	return true;
}


int
main()
{
	PenFit fit;
	EXPECT(!fit.Ready());
	// two pairs, or three in a line: not enough
	fit.Add(100, 100, 100, 100);
	fit.Add(200, 200, 200, 200);
	EXPECT(!fit.Ready());
	fit.Add(300, 300, 300, 300);
	EXPECT(!fit.Ready());
	fit.Reset();

	// AppLoad's turned and scaled window, learnt exactly
	Learn(fit, Turned, 0);
	EXPECT(fit.Ready());
	EXPECT(Maps(fit, Turned, 0.01));
	// the tablet turned back: the first pair that disagrees drops the map,
	// and the new geometry is learnt
	double x, y;
	Square(600, 700, &x, &y);
	EXPECT(!fit.Add(600, 700, x, y));
	EXPECT(!fit.Ready());
	Learn(fit, Square, 0);
	EXPECT(fit.Ready());
	EXPECT(Maps(fit, Square, 0.01));
	// a few pixels of noise in AppLoad's points: still learnt, close
	fit.Reset();
	Learn(fit, Turned, 3);
	EXPECT(fit.Ready());
	EXPECT(Maps(fit, Turned, 6));
	if (failures == 0)
		printf("test_PenFit: all passed\n");
	return failures != 0;
}
