/*
	File:		host/remarkable/PanelTurn.h

	Contains:	The Newton's display put onto the reMarkable's panel image
				turned: which way, and the sums for a pixel, a rectangle
				and a pen point (docs/host-remarkable.md, "Rotation").

				The panel's image is always the portrait one AppLoad was
				asked for, and with the manifest's supportsRotation AppLoad
				shows it square on the glass however the tablet is held -
				it paints it turned against the way its own interface is
				turned, and hands pen and touch points back in the image's
				pixels.  So the window draws the Newton's display into it
				turned the way the device is held: when AppLoad turns its
				interface a quarter to the left (ROTATION_L90: the folio
				folded back, the tablet on its side) it paints the image a
				quarter counter-clockwise, and the display is drawn a quarter
				clockwise into it, so that it comes out upright.

				A turn is a number of quarters clockwise (0..3) taking the
				display's pixel (x, y) to the image: the display is
				`width` by `height` (as it is turned now) and each pixel
				`scale` image pixels square.  Plain integer sums, no
				Newton headers (the window library is built without them),
				tested by tests/test_PanelTurn.cpp.
*/

#ifndef __PANELTURN_H
#define __PANELTURN_H

struct PanelTurn
{
	long	quarters;			// clockwise, 0..3
	long	width, height;		// the display, as turned now
	long	scale;

	// the image's size: the display's, turned and scaled
	long	ImageWidth(void) const		{ return ((quarters & 1) ? height : width) * scale; }
	long	ImageHeight(void) const		{ return ((quarters & 1) ? width : height) * scale; }

	// the display's pixel (x, y): the image's (X, Y), the top left of its square
	void	Pixel(long x, long y, long* X, long* Y) const
	{
		switch (quarters & 3)
		{
		default:
		case 0:	*X = x * scale;						*Y = y * scale;						break;
		case 1:	*X = (height - 1 - y) * scale;		*Y = x * scale;						break;
		case 2:	*X = (width - 1 - x) * scale;		*Y = (height - 1 - y) * scale;		break;
		case 3:	*X = y * scale;						*Y = (width - 1 - x) * scale;		break;
		}
	}

	// the display's rectangle [l, r) x [t, b): the image's
	void	Rect(long l, long t, long r, long b, long* L, long* T, long* R, long* B) const
	{
		switch (quarters & 3)
		{
		default:
		case 0:	*L = l; *T = t; *R = r; *B = b;									break;
		case 1:	*L = height - b; *T = l; *R = height - t; *B = r;				break;
		case 2:	*L = width - r; *T = height - b; *R = width - l; *B = height - t;	break;
		case 3:	*L = t; *T = width - r; *R = b; *B = width - l;					break;
		}
		*L *= scale; *T *= scale; *R *= scale; *B *= scale;
	}

	// the image's rectangle: the display's that covers it
	void	DisplayRect(long L, long T, long R, long B, long* l, long* t, long* r, long* b) const
	{
		// in whole display pixels of the image first, rounded outwards
		long il = L / scale, it = T / scale, ir = (R + scale - 1) / scale, ib = (B + scale - 1) / scale;
		switch (quarters & 3)
		{
		default:
		case 0:	*l = il; *t = it; *r = ir; *b = ib;								break;
		case 1:	*l = it; *t = height - ir; *r = ib; *b = height - il;			break;
		case 2:	*l = width - ir; *t = height - ib; *r = width - il; *b = height - it;	break;
		case 3:	*l = width - ib; *t = il; *r = width - it; *b = ir;				break;
		}
	}

	// an image point (X, Y): the display's, in eighths of a display pixel
	void	Point8(long X, long Y, long* x8, long* y8) const
	{
		long w = width * scale, h = height * scale;		// the display, scaled
		switch (quarters & 3)
		{
		default:
		case 0:	*x8 = X * 8 / scale;					*y8 = Y * 8 / scale;					break;
		case 1:	*x8 = Y * 8 / scale;					*y8 = (h - 1 - X) * 8 / scale;			break;
		case 2:	*x8 = (w - 1 - X) * 8 / scale;			*y8 = (h - 1 - Y) * 8 / scale;			break;
		case 3:	*x8 = (w - 1 - Y) * 8 / scale;			*y8 = X * 8 / scale;					break;
		}
	}
};

// How many quarters clockwise the display goes onto the image: AppLoad's
// rotation (0 upright, 1 its interface turned left, 2 right, 3 upside
// down) undone, and a quarter more when the display is the other shape
// from the way the device is held (the Newton turned by its own Rotate
// button: a quarter clockwise for its orientation 1, anticlockwise for
// 3).  `flip` (NEWTON_RM_TURN_FLIP) swaps the two sideways turns, for a
// tablet that turns the other way.
inline long
PanelTurnQuarters(long rotation, bool displayLandscape, long orientation, bool flip)
{
	long quarters;
	switch (rotation)
	{
	default:
	case 0:	quarters = 0; break;
	case 1:	quarters = flip ? 3 : 1; break;
	case 2:	quarters = flip ? 1 : 3; break;
	case 3:	quarters = 2; break;
	}
	bool heldLandscape = rotation == 1 || rotation == 2;
	if (displayLandscape != heldLandscape)
		quarters += orientation == 3 ? 3 : 1;
	return quarters & 3;
}

#endif	/* __PANELTURN_H */
