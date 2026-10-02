// The sums that put the Newton's display onto the reMarkable's panel image
// turned (host/remarkable/PanelTurn.h): for each quarter turn, every pixel
// of a small display lands on its own square inside the image; a
// rectangle's image is the squares of its pixels and turns back to
// itself; a pen point anywhere in a pixel's square comes back as that
// pixel; and which turn goes with how the tablet is held and how the
// Newton's screen is turned.
#include "../PanelTurn.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
TestTurn(long quarters, long width, long height, long scale)
{
	PanelTurn turn = { quarters, width, height, scale };
	long iw = turn.ImageWidth(), ih = turn.ImageHeight();
	EXPECT(iw * ih == width * height * scale * scale);
	static unsigned char covered[64 * 64];
	memset(covered, 0, sizeof(covered));
	for (long y = 0; y < height; y++)
		for (long x = 0; x < width; x++)
		{
			long X, Y;
			turn.Pixel(x, y, &X, &Y);
			EXPECT(X >= 0 && Y >= 0 && X + scale <= iw && Y + scale <= ih);
			EXPECT(X % scale == 0 && Y % scale == 0);
			if (X < 0 || Y < 0 || X + scale > iw || Y + scale > ih)
				continue;
			covered[(Y / scale) * (iw / scale) + X / scale]++;
			// the pixel's rectangle is its square
			long L, T, R, B;
			turn.Rect(x, y, x + 1, y + 1, &L, &T, &R, &B);
			EXPECT(L == X && T == Y && R == X + scale && B == Y + scale);
			// every point of the square is the pixel again
			for (long j = 0; j < scale; j++)
				for (long i = 0; i < scale; i++)
				{
					long x8, y8;
					turn.Point8(X + i, Y + j, &x8, &y8);
					EXPECT(x8 / 8 == x && y8 / 8 == y);
				}
		}
	for (long i = 0; i < (iw / scale) * (ih / scale); i++)
		EXPECT(covered[i] == 1);
	// a rectangle there and back
	long l = 1, t = 2, r = width - 1, b = height - 3;
	long L, T, R, B, l2, t2, r2, b2;
	turn.Rect(l, t, r, b, &L, &T, &R, &B);
	EXPECT(L >= 0 && T >= 0 && R <= iw && B <= ih && (R - L) * (B - T) == (r - l) * (b - t) * scale * scale);
	turn.DisplayRect(L, T, R, B, &l2, &t2, &r2, &b2);
	EXPECT(l2 == l && t2 == t && r2 == r && b2 == b);
	// a part of a square is the whole pixel
	turn.DisplayRect(L + 1, T + 1, L + 2, T + 2, &l2, &t2, &r2, &b2);
	EXPECT(r2 - l2 == 1 && b2 - t2 == 1);
}


static void
TestQuarters()
{
	// held upright, the display portrait: as it is
	EXPECT(PanelTurnQuarters(0, false, 2, false) == 0);
	// the interface turned left (the folio folded back), the Newton landscape:
	// a quarter clockwise; turned right: a quarter anticlockwise
	EXPECT(PanelTurnQuarters(1, true, 1, false) == 1);
	EXPECT(PanelTurnQuarters(2, true, 3, false) == 3);
	EXPECT(PanelTurnQuarters(1, true, 1, true) == 3);		// NEWTON_RM_TURN_FLIP
	EXPECT(PanelTurnQuarters(3, false, 2, false) == 2);
	// the Newton turned by its own Rotate while the tablet is upright: a
	// quarter its way
	EXPECT(PanelTurnQuarters(0, true, 1, false) == 1);
	EXPECT(PanelTurnQuarters(0, true, 3, false) == 3);
	// held landscape but the Newton turned back to portrait
	EXPECT(PanelTurnQuarters(1, false, 2, false) == 2);
	// every choice leaves a display of either shape fitting the portrait image
	for (long rotation = 0; rotation < 4; rotation++)
		for (int landscape = 0; landscape < 2; landscape++)
			for (long orientation = 0; orientation < 4; orientation++)
			{
				PanelTurn turn = { PanelTurnQuarters(rotation, landscape != 0, orientation, false),
								   landscape ? 48 : 32, landscape ? 32 : 48, 2 };
				EXPECT(turn.ImageWidth() == 64 && turn.ImageHeight() == 96);
			}
}


int
main()
{
	for (long q = 0; q < 4; q++)
	{
		TestTurn(q, 12, 7, 1);
		TestTurn(q, 12, 7, 2);
		TestTurn(q, 5, 9, 3);
	}
	TestQuarters();
	if (failures == 0)
		printf("test_PanelTurn: all passed\n");
	return failures != 0;
}
