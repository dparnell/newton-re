// The unistroke classifier (Unistroke.h): every character of the
// alphabet, drawn as its template and then as a person might draw it -
// stretched, sheared, a little turned, at another size, with the pen's
// wobble - is read as itself; the space and backspace strokes are told
// apart by their direction; a dot is not read; and the shapes Graffiti
// told apart by where they were written (0/O, 1/I, 5/S) are read by the
// mode asked for.

#include "Unistroke.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char kLetters[] = "abcdefghijklmnopqrstuvwxyz";
static const char kDigits[] = "0123456789";
static const UniChar kCommands[] = { kUnistrokeSpace, kUnistrokeBackspace, kUnistrokeReturn, kUnistrokeShift };

// a fixed sequence of pseudo-random numbers in [-1, 1], so the test is
// the same every run
static unsigned long gSeed = 12345;
static double
Wobble(void)
{
	gSeed = gSeed * 1103515245 + 12345;
	return ((gSeed >> 16) & 0x7fff) / 16383.5 - 1;
}

static const char*
Name(UniChar c)
{
	static char buf[8];
	switch (c)
	{
	case kUnistrokeSpace:		return "space";
	case kUnistrokeBackspace:	return "backspace";
	case kUnistrokeReturn:		return "return";
	case kUnistrokeShift:		return "shift";
	case kUnistrokeNone:		return "nothing";
	}
	snprintf(buf, sizeof(buf), "%c", (char) c);
	return buf;
}

// The character's template drawn at 40 pixels, through a transform: x
// scaled by sx and y by sy, sheared by `shear`, turned by `turn` degrees,
// each point moved up to `noise` pixels.  ==> what it is read as.
static UniChar
ReadDrawn(UniChar ch, bool digit, double sx, double sy, double shear, double turn, double noise,
		  UnistrokeMode mode, long* score = nullptr)
{
	double xy[2 * 40];
	long n = UnistrokeTemplatePath(ch, digit, 40, xy, 40);
	if (n == 0)
		return kUnistrokeNone;
	double a = turn * 3.14159265358979 / 180;
	for (long i = 0; i < n; i++)
	{
		double x = xy[2 * i] * sx, y = xy[2 * i + 1] * sy;
		x += shear * y;
		double rx = x * cos(a) - y * sin(a), ry = x * sin(a) + y * cos(a);
		xy[2 * i] = 100 + rx + noise * Wobble();
		xy[2 * i + 1] = 100 + ry + noise * Wobble();
	}
	UnistrokeMatch m[4];
	long found = UnistrokeClassify(xy, n, mode, m, 4);
	if (score != nullptr)
		*score = found > 0 ? m[0].fScore : -1;
	return found > 0 ? m[0].fChar : kUnistrokeNone;
}


// each one drawn as its template is read as itself, and well
static void
TestClean(void)
{
	for (const char* p = kLetters; *p; p++)
	{
		long score;
		UniChar got = ReadDrawn(*p, false, 1, 1, 0, 0, 0, kUnistrokeLetters, &score);
		if (got != *p || score >= kUnistrokeGoodScore)
			fprintf(stderr, "  %c read as %s (%ld)\n", *p, Name(got), score);
		EXPECT(got == *p && score < kUnistrokeGoodScore);
	}
	for (const char* p = kDigits; *p; p++)
	{
		UniChar got = ReadDrawn(*p, true, 1, 1, 0, 0, 0, kUnistrokeDigits);
		if (got != *p)
			fprintf(stderr, "  digit %c read as %s\n", *p, Name(got));
		EXPECT(got == *p);
	}
	for (UniChar c : kCommands)
	{
		UniChar got = ReadDrawn(c, false, 1, 1, 0, 0, 0, kUnistrokeLetters);
		if (got != c)
			fprintf(stderr, "  %s read as %s\n", Name(c), Name(got));
		EXPECT(got == c);
	}
}


// Drawn as a hand draws them: every combination of a range of
// stretches, shears and turns, with wobble.  Nearly all are read as
// themselves (the rate is printed); none of the commands is mistaken.
static void
TestDistorted(void)
{
	static const double kStretch[] = { 0.7, 1.0, 1.3 };
	static const double kShear[] = { -0.15, 0, 0.15 };
	static const double kTurn[] = { -8, 0, 8 };
	long tried = 0, right = 0;
	for (int pass = 0; pass < 2; pass++)
	{
		const char* set = pass == 0 ? kLetters : kDigits;
		bool digit = pass == 1;
		for (const char* p = set; *p; p++)
			for (double sx : kStretch)
				for (double sy : kStretch)
					for (double sh : kShear)
						for (double tu : kTurn)
						{
							UniChar got = ReadDrawn(*p, digit, sx, sy, sh, tu, 1.5,
													digit ? kUnistrokeDigits : kUnistrokeLetters);
							tried++;
							if (got == *p)
								right++;
							else if (getenv("UNISTROKE_VERBOSE"))
								fprintf(stderr, "  %c (%.1f %.1f %.2f %.0f) read as %s\n", *p, sx, sy, sh, tu, Name(got));
						}
	}
	printf("test_Unistroke: %ld of %ld distorted characters read right (%.1f%%)\n",
		   right, tried, 100.0 * right / tried);
	EXPECT(right * 100 >= tried * 95);

	// the commands, which do things, are never taken for each other
	for (UniChar c : kCommands)
		for (double tu : kTurn)
			for (double sx : kStretch)
			{
				UniChar got = ReadDrawn(c, false, sx, sx, 0, tu, 1.0, kUnistrokeLetters);
				if (got != c)
					fprintf(stderr, "  %s (%.1f %.0f) read as %s\n", Name(c), sx, tu, Name(got));
				EXPECT(got == c);
			}
}


// what only the direction tells apart, and what is not a stroke
static void
TestDirections(void)
{
	double right[] = { 10, 50, 30, 50, 50, 51, 70, 50 };
	double left[] = { 70, 50, 50, 51, 30, 50, 10, 50 };
	double down[] = { 50, 10, 50, 30, 51, 50, 50, 70 };
	double up[] = { 50, 70, 51, 50, 50, 30, 50, 10 };
	UnistrokeMatch m[2];
	EXPECT(UnistrokeClassify(right, 4, kUnistrokeLetters, m, 2) > 0 && m[0].fChar == kUnistrokeSpace);
	EXPECT(UnistrokeClassify(left, 4, kUnistrokeLetters, m, 2) > 0 && m[0].fChar == kUnistrokeBackspace);
	EXPECT(UnistrokeClassify(down, 4, kUnistrokeLetters, m, 2) > 0 && m[0].fChar == 'i');
	EXPECT(UnistrokeClassify(down, 4, kUnistrokeDigits, m, 2) > 0 && m[0].fChar == '1');
	EXPECT(UnistrokeClassify(up, 4, kUnistrokeLetters, m, 2) > 0 && m[0].fChar == kUnistrokeShift);
	// a dot
	double dot[] = { 40, 40, 40, 40, 40, 40 };
	EXPECT(UnistrokeClassify(dot, 3, kUnistrokeLetters, m, 2) == 0);
}


// the shapes the two areas told apart, read by the mode
static void
TestModes(void)
{
	EXPECT(ReadDrawn('o', false, 1, 1.2, 0, 0, 0, kUnistrokeLetters) == 'o');
	EXPECT(ReadDrawn('o', false, 1, 1.2, 0, 0, 0, kUnistrokeDigits) == '0');
	EXPECT(ReadDrawn('s', false, 1, 1, 0, 0, 0, kUnistrokeLetters) == 's');
	EXPECT(ReadDrawn('i', false, 1, 1, 0, 0, 0, kUnistrokeDigits) == '1');
	// and a digit no letter is drawn like is read in either
	EXPECT(ReadDrawn('3', true, 1, 1, 0, 0, 0, kUnistrokeLetters) == '3');
	EXPECT(ReadDrawn('8', true, 1, 1, 0, 0, 0, kUnistrokeLetters) == '8');
}


// The alphabet as a reference card: each character's stroke as the
// engine expects it, a dot where the pen goes down and an arrowhead where
// it comes up.  `test_Unistroke --card <file.svg>` writes it
// (docs/recognition/unistroke-card.svg).
static int
WriteCard(const char* path)
{
	FILE* f = fopen(path, "w");
	if (f == nullptr)
		return 1;
	struct Entry { UniChar ch; bool digit; const char* label; };
	Entry entries[64];
	int n = 0;
	for (const char* p = kLetters; *p; p++)
		entries[n++] = { (UniChar) *p, false, nullptr };
	for (const char* p = kDigits; *p; p++)
		entries[n++] = { (UniChar) *p, true, nullptr };
	entries[n++] = { kUnistrokeSpace, false, "space" };
	entries[n++] = { kUnistrokeBackspace, false, "backspace" };
	entries[n++] = { kUnistrokeReturn, false, "return" };
	entries[n++] = { kUnistrokeShift, false, "shift" };
	const int columns = 8, cell = 90, size = 50;
	int rows = (n + columns - 1) / columns;
	fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" font-family=\"sans-serif\">\n",
			columns * cell, rows * cell);
	fprintf(f, "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n");
	for (int i = 0; i < n; i++)
	{
		int x0 = (i % columns) * cell + 20, y0 = (i / columns) * cell + 22;
		double xy[2 * 40];
		long count = UnistrokeTemplatePath(entries[i].ch, entries[i].digit, size, xy, 40);
		char name[16];
		if (entries[i].label)
			snprintf(name, sizeof(name), "%s", entries[i].label);
		else
			snprintf(name, sizeof(name), "%c", (char) entries[i].ch);
		fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"13\" fill=\"#555\">%s</text>\n", x0 - 14, y0 - 8, name);
		// a straight stroke is drawn at its length; any other fills the cell
		fprintf(f, "<polyline fill=\"none\" stroke=\"black\" stroke-width=\"2.5\" stroke-linejoin=\"round\" points=\"");
		for (long k = 0; k < count; k++)
			fprintf(f, "%.1f,%.1f ", x0 + xy[2 * k], y0 + xy[2 * k + 1]);
		fprintf(f, "\"/>\n");
		if (count > 1)
		{
			fprintf(f, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"4\" fill=\"#c00\"/>\n", x0 + xy[0], y0 + xy[1]);
			double ex = x0 + xy[2 * count - 2], ey = y0 + xy[2 * count - 1];
			double dx = xy[2 * count - 2] - xy[2 * count - 4], dy = xy[2 * count - 1] - xy[2 * count - 3];
			double len = sqrt(dx * dx + dy * dy);
			if (len > 0)
			{
				dx /= len;
				dy /= len;
				fprintf(f, "<polygon fill=\"black\" points=\"%.1f,%.1f %.1f,%.1f %.1f,%.1f\"/>\n",
						ex + dx * 6, ey + dy * 6, ex - dy * 5, ey + dx * 5, ex + dy * 5, ey - dx * 5);
			}
		}
	}
	fprintf(f, "</svg>\n");
	fclose(f);
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc == 3 && strcmp(argv[1], "--card") == 0)
		return WriteCard(argv[2]);
	TestClean();
	TestDistorted();
	TestDirections();
	TestModes();
	if (failures == 0)
		printf("test_Unistroke: all passed\n");
	else
		printf("test_Unistroke: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
