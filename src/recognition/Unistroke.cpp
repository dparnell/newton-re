/*
	File:		recognition/Unistroke.cpp

	Contains:	The unistroke classifier - Unistroke.h.

				The templates are written in a unit square, x to the right
				and y down, as a path: a start point, then lines to points
				and arcs.  An arc is a centre, two radii and the angles it
				runs between, in degrees measured as on paper - 0 to the
				right, 90 up - so an arc whose angle grows turns
				anticlockwise on the screen.
*/

#include "Unistroke.h"

#include <math.h>
#include <string.h>


/*------------------------------------------------------------------------------
	T h e   a l p h a b e t
------------------------------------------------------------------------------*/

static const double kPi = 3.14159265358979323846;

enum { kOpEnd, kOpMove, kOpLine, kOpArc };

struct PathOp
{
	int		fOp;
	double	fA, fB, fC, fD, fE, fF;		// move/line: x, y; arc: cx, cy, rx, ry, from, to
};

#define MOVE(x, y)						{ kOpMove, x, y, 0, 0, 0, 0 }
#define LINE(x, y)						{ kOpLine, x, y, 0, 0, 0, 0 }
#define ARC(cx, cy, rx, ry, from, to)	{ kOpArc, cx, cy, rx, ry, from, to }
#define END								{ kOpEnd, 0, 0, 0, 0, 0, 0 }

struct Template
{
	UniChar			fChar;
	Boolean			fDigit;			// written in Graffiti's number area
	Boolean			fStraight;		// a line: compared without stretching
	PathOp			fPath[8];
};

// Graffiti's alphabet, as its reference card draws each one.  Several
// letters have a second form that Palm also took (marked "also").
static const Template kTemplates[] =
{
	// the letters
	{ 'a', false, false, { MOVE(0, 1), LINE(0.5, 0), LINE(1, 1), END } },
	{ 'b', false, false, { MOVE(0, 0), LINE(0, 1), LINE(0, 0), ARC(0, 0.25, 0.8, 0.25, 90, -90), ARC(0, 0.75, 1, 0.25, 90, -90), END } },
	{ 'c', false, false, { MOVE(0.85, 0.15), ARC(0.5, 0.5, 0.5, 0.5, 45, 315), END } },
	{ 'd', false, false, { MOVE(0, 0), LINE(0, 1), LINE(0, 0), ARC(0, 0.5, 1, 0.5, 90, -90), END } },
	{ 'e', false, false, { MOVE(0.95, 0.1), ARC(0.6, 0.25, 0.4, 0.25, 30, 270), ARC(0.6, 0.75, 0.4, 0.25, 90, 330), END } },
	{ 'f', false, false, { MOVE(1, 0), LINE(0, 0), LINE(0, 1), END } },
	{ 'g', false, false, { MOVE(0.85, 0.15), ARC(0.5, 0.5, 0.5, 0.5, 45, 330), LINE(1, 0.55), LINE(0.55, 0.55), END } },
	{ 'h', false, false, { MOVE(0, 0), LINE(0, 1), LINE(0, 0.6), ARC(0.5, 0.6, 0.5, 0.3, 180, 0), LINE(1, 1), END } },
	{ 'i', false, true,  { MOVE(0.5, 0), LINE(0.5, 1), END } },
	{ 'j', false, false, { MOVE(0.75, 0), LINE(0.75, 0.7), ARC(0.4, 0.7, 0.35, 0.3, 0, -180), END } },
	{ 'k', false, false, { MOVE(1, 0), LINE(0, 0.5), LINE(1, 1), END } },
	{ 'l', false, false, { MOVE(0, 0), LINE(0, 1), LINE(1, 1), END } },
	{ 'm', false, false, { MOVE(0, 1), LINE(0.2, 0), LINE(0.5, 0.7), LINE(0.8, 0), LINE(1, 1), END } },
	{ 'n', false, false, { MOVE(0, 1), LINE(0, 0), LINE(1, 1), LINE(1, 0), END } },
	{ 'o', false, false, { MOVE(0.5, 0), ARC(0.5, 0.5, 0.5, 0.5, 90, 450), END } },
	{ 'p', false, false, { MOVE(0, 1), LINE(0, 0), ARC(0, 0.27, 0.9, 0.27, 90, -90), END } },
	{ 'q', false, false, { MOVE(0.5, 0), ARC(0.5, 0.45, 0.5, 0.45, 90, 450), LINE(1, 1), END } },
	{ 'r', false, false, { MOVE(0, 1), LINE(0, 0), ARC(0, 0.27, 0.9, 0.27, 90, -90), LINE(1, 1), END } },
	{ 's', false, false, { MOVE(0.95, 0.12), ARC(0.5, 0.25, 0.5, 0.25, 20, 270), ARC(0.5, 0.75, 0.5, 0.25, 90, -160), END } },
	{ 't', false, false, { MOVE(0, 0), LINE(1, 0), LINE(1, 1), END } },
	{ 'u', false, false, { MOVE(0, 0), LINE(0, 0.55), ARC(0.5, 0.55, 0.5, 0.45, 180, 360), LINE(1, 0), END } },
	{ 'v', false, false, { MOVE(0, 0), LINE(0.5, 1), LINE(1, 0), END } },
	{ 'w', false, false, { MOVE(0, 0), LINE(0.25, 1), LINE(0.5, 0.35), LINE(0.75, 1), LINE(1, 0), END } },
	{ 'x', false, true,  { MOVE(0, 0), LINE(1, 1), END } },
	{ 'y', false, false, { MOVE(0, 0), LINE(0.5, 0.5), LINE(1, 0), LINE(0.3, 1), END } },
	{ 'z', false, false, { MOVE(0, 0), LINE(1, 0), LINE(0, 1), LINE(1, 1), END } },
	// also: T as a 7 without its slant, the stroke straight down from the middle
	{ 't', false, false, { MOVE(0, 0), LINE(1, 0), LINE(0.5, 0), LINE(0.5, 1), END } },
	// also: V with a curl at the end, which Palm took to tell it from U
	{ 'v', false, false, { MOVE(0, 0), LINE(0.5, 1), LINE(1, 0), LINE(0.85, 0.1), END } },

	// the strokes that are not characters
	{ kUnistrokeSpace,     false, true, { MOVE(0, 0.5), LINE(1, 0.5), END } },
	{ kUnistrokeBackspace, false, true, { MOVE(1, 0.5), LINE(0, 0.5), END } },
	{ kUnistrokeReturn,    false, true, { MOVE(1, 0), LINE(0, 1), END } },
	{ kUnistrokeShift,     false, true, { MOVE(0.5, 1), LINE(0.5, 0), END } },

	// the digits
	{ '0', true, false, { MOVE(0.5, 0), ARC(0.5, 0.5, 0.5, 0.5, 90, 450), END } },
	{ '1', true, true,  { MOVE(0.5, 0), LINE(0.5, 1), END } },
	{ '2', true, false, { MOVE(0.05, 0.25), ARC(0.5, 0.3, 0.45, 0.3, 160, -20), LINE(0, 1), LINE(1, 1), END } },
	{ '3', true, false, { MOVE(0.1, 0.1), ARC(0.5, 0.25, 0.45, 0.25, 150, -90), ARC(0.5, 0.75, 0.45, 0.25, 90, -150), END } },
	{ '4', true, false, { MOVE(0.6, 0), LINE(0, 0.7), LINE(1, 0.7), END } },
	{ '5', true, false, { MOVE(1, 0), LINE(0.1, 0), LINE(0.05, 0.45), ARC(0.45, 0.7, 0.5, 0.3, 135, -150), END } },
	{ '6', true, false, { MOVE(0.85, 0), LINE(0.3, 0.3), LINE(0, 0.68), ARC(0.5, 0.68, 0.5, 0.32, 180, 520), END } },
	{ '7', true, false, { MOVE(0, 0), LINE(1, 0), LINE(0.3, 1), END } },
	{ '8', true, false, { MOVE(0.95, 0.12), ARC(0.5, 0.25, 0.5, 0.25, 20, 270), ARC(0.5, 0.75, 0.5, 0.25, 90, -180), LINE(1, 0), END } },
	{ '9', true, false, { MOVE(1, 0.3), ARC(0.5, 0.3, 0.5, 0.3, 0, 360), LINE(1, 1), END } },
};
static const long kTemplateCount = sizeof(kTemplates) / sizeof(kTemplates[0]);


/*------------------------------------------------------------------------------
	S t r o k e s   a s   p o i n t s
------------------------------------------------------------------------------*/

enum { kSamples = 40, kMaxRaw = 400 };

struct Shape
{
	double	fX[kSamples];
	double	fY[kSamples];
};

// the template's path as a polyline
static long
TemplatePolyline(const Template* t, double* xy, long max)
{
	long n = 0;
	for (const PathOp* op = t->fPath; op->fOp != kOpEnd && n < max; op++)
	{
		if (op->fOp == kOpMove || op->fOp == kOpLine)
		{
			xy[2 * n] = op->fA;
			xy[2 * n + 1] = op->fB;
			n++;
		}
		else
		{
			// an arc in steps of about ten degrees
			double from = op->fE * kPi / 180, to = op->fF * kPi / 180;
			long steps = (long) (fabs(op->fF - op->fE) / 10) + 1;
			for (long i = 0; i <= steps && n < max; i++)
			{
				double a = from + (to - from) * i / steps;
				xy[2 * n] = op->fA + op->fC * cos(a);
				xy[2 * n + 1] = op->fB - op->fD * sin(a);
				n++;
			}
		}
	}
	return n;
}


// the polyline's length
static double
PathLength(const double* xy, long count)
{
	double length = 0;
	for (long i = 1; i < count; i++)
		length += hypot(xy[2 * i] - xy[2 * i - 2], xy[2 * i + 1] - xy[2 * i - 1]);
	return length;
}


// kSamples points evenly spaced along the polyline
static void
Resample(const double* xy, long count, Shape* out)
{
	double length = PathLength(xy, count);
	double step = length / (kSamples - 1);
	out->fX[0] = xy[0];
	out->fY[0] = xy[1];
	long made = 1;
	double carried = 0;
	double px = xy[0], py = xy[1];
	for (long i = 1; i < count && made < kSamples; i++)
	{
		double qx = xy[2 * i], qy = xy[2 * i + 1];
		double d = hypot(qx - px, qy - py);
		while (d > 0 && carried + d >= step && made < kSamples)
		{
			double t = (step - carried) / d;
			px = px + t * (qx - px);
			py = py + t * (qy - py);
			out->fX[made] = px;
			out->fY[made] = py;
			made++;
			d = hypot(qx - px, qy - py);
			carried = 0;
		}
		carried += d;
		px = qx;
		py = qy;
	}
	for (; made < kSamples; made++)
	{
		out->fX[made] = xy[2 * count - 2];
		out->fY[made] = xy[2 * count - 1];
	}
}


struct Box
{
	double	fLeft, fTop, fRight, fBottom;
	double	Width(void) const	{ return fRight - fLeft; }
	double	Height(void) const	{ return fBottom - fTop; }
};

static Box
BoundsOf(const Shape* s)
{
	Box b = { s->fX[0], s->fY[0], s->fX[0], s->fY[0] };
	for (long i = 1; i < kSamples; i++)
	{
		if (s->fX[i] < b.fLeft) b.fLeft = s->fX[i];
		if (s->fX[i] > b.fRight) b.fRight = s->fX[i];
		if (s->fY[i] < b.fTop) b.fTop = s->fY[i];
		if (s->fY[i] > b.fBottom) b.fBottom = s->fY[i];
	}
	return b;
}


// Centred on its box and scaled into a unit square: stretched to fill it
// (`stretch`), or the same in both directions so that its longer side is
// one.
static void
Normalise(const Shape* in, Boolean stretch, Shape* out)
{
	Box b = BoundsOf(in);
	double cx = (b.fLeft + b.fRight) / 2, cy = (b.fTop + b.fBottom) / 2;
	double w = b.Width(), h = b.Height();
	double longer = w > h ? w : h;
	if (longer <= 0)
		longer = 1;
	double sx = 1 / longer, sy = 1 / longer;
	if (stretch)
	{
		// (a side of almost nothing is not blown up into a shape)
		sx = 1 / (w > longer / 8 ? w : longer / 8);
		sy = 1 / (h > longer / 8 ? h : longer / 8);
	}
	for (long i = 0; i < kSamples; i++)
	{
		out->fX[i] = (in->fX[i] - cx) * sx;
		out->fY[i] = (in->fY[i] - cy) * sy;
	}
}


static double
Distance(const Shape* a, const Shape* b)
{
	double sum = 0;
	for (long i = 0; i < kSamples; i++)
		sum += hypot(a->fX[i] - b->fX[i], a->fY[i] - b->fY[i]);
	return sum / kSamples;
}


/*------------------------------------------------------------------------------
	R e a d i n g   a   s t r o k e
------------------------------------------------------------------------------*/

// the templates resampled and normalised, made once
static Shape	gTemplateShapes[kTemplateCount];
static Boolean	gTemplatesMade = false;

static void
MakeTemplates(void)
{
	if (gTemplatesMade)
		return;
	for (long i = 0; i < kTemplateCount; i++)
	{
		double raw[2 * kMaxRaw];
		long n = TemplatePolyline(&kTemplates[i], raw, kMaxRaw);
		Shape s;
		Resample(raw, n, &s);
		Normalise(&s, !kTemplates[i].fStraight, &gTemplateShapes[i]);
	}
	gTemplatesMade = true;
}


long
UnistrokeClassify(const double* xy, long count, UnistrokeMode mode, UnistrokeMatch* out, long max)
{
	MakeTemplates();
	if (count < 2 || max <= 0)
		return 0;
	Shape sampled;
	Resample(xy, count, &sampled);
	Box b = BoundsOf(&sampled);
	double longer = b.Width() > b.Height() ? b.Width() : b.Height();
	double shorter = b.Width() > b.Height() ? b.Height() : b.Width();
	if (longer <= 0 || PathLength(xy, count) <= 0)
		return 0;
	// how nearly a line the stroke is: 0 for a line, 1 for a square
	double flatness = shorter / longer;

	Shape uniform, stretched;
	Normalise(&sampled, false, &uniform);
	Normalise(&sampled, true, &stretched);

	long found = 0;
	for (long i = 0; i < kTemplateCount; i++)
	{
		const Template* t = &kTemplates[i];
		double d = Distance(t->fStraight ? &uniform : &stretched, &gTemplateShapes[i]);
		// a figure compared with a stroke that is all but a line has had
		// the line's wobble stretched into a shape: it is not that figure
		if (!t->fStraight && flatness < 0.2)
			d += (0.2 - flatness) * 2;
		// the shapes Graffiti read by where they were written: the one not
		// wanted here is a little further
		Boolean shared = t->fChar == 'o' || t->fChar == 'i' || t->fChar == 's'
					  || t->fChar == '0' || t->fChar == '1' || t->fChar == '5';
		if (shared && (t->fDigit != (mode == kUnistrokeDigits)))
			d += 0.03;
		long score = (long) (d * 1000 + 0.5);

		// one answer per character, the nearest of its forms
		long at = -1;
		for (long j = 0; j < found; j++)
			if (out[j].fChar == t->fChar)
				at = j;
		if (at >= 0)
		{
			if (score >= out[at].fScore)
				continue;
			// taken out, to go back in where it now belongs
			for (long j = at; j < found - 1; j++)
				out[j] = out[j + 1];
			found--;
		}
		long j = found;
		while (j > 0 && out[j - 1].fScore > score)
		{
			if (j < max)
				out[j] = out[j - 1];
			j--;
		}
		if (j < max)
		{
			out[j].fChar = t->fChar;
			out[j].fScore = score;
			if (found < max)
				found++;
		}
	}
	return found;
}


long
UnistrokeTemplatePath(UniChar ch, Boolean digit, double size, double* xy, long count)
{
	for (long i = 0; i < kTemplateCount; i++)
		if (kTemplates[i].fChar == ch && kTemplates[i].fDigit == digit)
		{
			double raw[2 * kMaxRaw];
			long n = TemplatePolyline(&kTemplates[i], raw, kMaxRaw);
			Shape s;
			Resample(raw, n, &s);
			long made = 0;
			for (long k = 0; k < count && k < kSamples; k++)
			{
				xy[2 * k] = s.fX[k] * size;
				xy[2 * k + 1] = s.fY[k] * size;
				made++;
			}
			return made;
		}
	return 0;
}
