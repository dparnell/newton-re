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


/*------------------------------------------------------------------------------
	T h e   h e l p   b o o k ' s   p i c t u r e s
------------------------------------------------------------------------------*/

// The alphabet as the built-in help shows it (romsrc/rex/help_book: "Write
// with unistrokes"): `test_Unistroke --pict letters|others <file.pict>`
// writes a QuickDraw picture - the 512-byte header a PICT file has, then a
// version 2 picture of one 1-bit bitmap (PackBitsRect), as the help's own
// pictures are - of the letters, or of the digits and the four strokes
// that are not characters.  Each cell is a label in a 5x7 font and the
// stroke from the classifier's template, 2 pixels wide, with a dot where
// the pen goes down.

// a 5x7 font: the labels' characters, seven rows of five
struct Glyph { char ch; const char* rows[7]; };
static const Glyph kFont[] =
{
	{ 'A', { " ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #" } },
	{ 'B', { "#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### " } },
	{ 'C', { " ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### " } },
	{ 'D', { "#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### " } },
	{ 'E', { "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####" } },
	{ 'F', { "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    " } },
	{ 'G', { " ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####" } },
	{ 'H', { "#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #" } },
	{ 'I', { " ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### " } },
	{ 'J', { "  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  " } },
	{ 'K', { "#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #" } },
	{ 'L', { "#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####" } },
	{ 'M', { "#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #" } },
	{ 'N', { "#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #" } },
	{ 'O', { " ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " } },
	{ 'P', { "#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    " } },
	{ 'Q', { " ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #" } },
	{ 'R', { "#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #" } },
	{ 'S', { " ####", "#    ", "#    ", " ### ", "    #", "    #", "#### " } },
	{ 'T', { "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  " } },
	{ 'U', { "#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " } },
	{ 'V', { "#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  " } },
	{ 'W', { "#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # " } },
	{ 'X', { "#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #" } },
	{ 'Y', { "#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  " } },
	{ 'Z', { "#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####" } },
	{ '0', { " ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### " } },
	{ '1', { "  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### " } },
	{ '2', { " ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####" } },
	{ '3', { "#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### " } },
	{ '4', { "   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # " } },
	{ '5', { "#####", "#    ", "#### ", "    #", "    #", "#   #", " ### " } },
	{ '6', { "  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### " } },
	{ '7', { "#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   " } },
	{ '8', { " ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### " } },
	{ '9', { " ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  " } },
};

struct Bitmap
{
	int				fWidth, fHeight, fRowBytes;
	unsigned char	fBits[26 * 200];

	void	Clear(int w, int h)	{ fWidth = w; fHeight = h; fRowBytes = ((w + 15) / 16) * 2; memset(fBits, 0, sizeof(fBits)); }
	void	Set(int x, int y)
	{
		if (x >= 0 && y >= 0 && x < fWidth && y < fHeight)
			fBits[y * fRowBytes + x / 8] |= (unsigned char) (0x80 >> (x % 8));
	}
	void	Text(int x, int y, const char* text)
	{
		for (; *text; text++, x += 6)
			for (const Glyph& g : kFont)
				if (g.ch == *text)
					for (int r = 0; r < 7; r++)
						for (int c = 0; c < 5; c++)
							if (g.rows[r][c] == '#')
								Set(x + c, y + r);
	}
	// a 2-pixel pen along the points, and a dot where they start
	void	Stroke(int x, int y, const double* xy, long count)
	{
		for (long i = 1; i < count; i++)
		{
			double x0 = xy[2 * i - 2], y0 = xy[2 * i - 1], x1 = xy[2 * i], y1 = xy[2 * i + 1];
			double len = sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
			int steps = (int) (len * 4) + 1;
			for (int k = 0; k <= steps; k++)
			{
				int px = x + (int) floor(x0 + (x1 - x0) * k / steps + 0.5);
				int py = y + (int) floor(y0 + (y1 - y0) * k / steps + 0.5);
				Set(px, py); Set(px + 1, py); Set(px, py + 1); Set(px + 1, py + 1);
			}
		}
		int dx = x + (int) floor(xy[0] + 0.5), dy = y + (int) floor(xy[1] + 0.5);
		for (int j = -2; j <= 3; j++)
			for (int i = -2; i <= 3; i++)
				if (!((i == -2 || i == 3) && (j == -2 || j == 3)))
					Set(dx + i, dy + j);
	}
};

static void
PutWord(FILE* f, int w)
{
	fputc((w >> 8) & 0xff, f);
	fputc(w & 0xff, f);
}

static void
PutRect(FILE* f, int top, int left, int bottom, int right)
{
	PutWord(f, top); PutWord(f, left); PutWord(f, bottom); PutWord(f, right);
}

static int
WritePict(const char* which, const char* path)
{
	struct Entry { UniChar ch; bool digit; const char* label; };
	Entry entries[40];
	int n = 0;
	char labels[40][2];
	if (strcmp(which, "letters") == 0)
		for (const char* p = kLetters; *p; p++, n++)
		{
			labels[n][0] = (char) (*p - 'a' + 'A');
			labels[n][1] = 0;
			entries[n] = { (UniChar) *p, false, labels[n] };
		}
	else if (strcmp(which, "others") == 0)
	{
		for (const char* p = kDigits; *p; p++, n++)
		{
			labels[n][0] = *p;
			labels[n][1] = 0;
			entries[n] = { (UniChar) *p, true, labels[n] };
		}
		entries[n++] = { kUnistrokeSpace, false, "SPC" };
		entries[n++] = { kUnistrokeBackspace, false, "DEL" };
		entries[n++] = { kUnistrokeReturn, false, "RET" };
		entries[n++] = { kUnistrokeShift, false, "CAP" };
	}
	else
		return 1;
	const int columns = 7, cellW = 29, cellH = 28;
	int rows = (n + columns - 1) / columns;
	static Bitmap bm;
	bm.Clear(columns * cellW, rows * cellH);
	for (int i = 0; i < n; i++)
	{
		int x = (i % columns) * cellW, y = (i / columns) * cellH;
		// (the label at the top left, the stroke below and to the right of
		//  it, so that a dot at the stroke's top left does not touch it)
		bm.Text(x + 1, y + 1, entries[i].label);
		double xy[2 * 40];
		long count = UnistrokeTemplatePath(entries[i].ch, entries[i].digit, 16, xy, 40);
		bm.Stroke(x + 9, y + 9, xy, count);
	}

	FILE* f = fopen(path, "wb");
	if (f == nullptr)
		return 1;
	for (int i = 0; i < 512; i++)
		fputc(0, f);
	long sizeAt = ftell(f);
	PutWord(f, 0);										// picSize, filled in below
	PutRect(f, 0, 0, bm.fHeight, bm.fWidth);			// picFrame
	PutWord(f, 0x0011); PutWord(f, 0x02ff);				// version 2
	PutWord(f, 0x0c00);									// header: version -1, the frame as Fixed
	PutWord(f, 0xffff); PutWord(f, 0xffff);
	PutWord(f, 0); PutWord(f, 0); PutWord(f, 0); PutWord(f, 0);
	PutWord(f, bm.fWidth); PutWord(f, 0); PutWord(f, bm.fHeight); PutWord(f, 0);
	PutWord(f, 0); PutWord(f, 0);
	PutWord(f, 0x001e);									// DefHilite
	PutWord(f, 0x0001); PutWord(f, 10);					// ClipRgn: the frame
	PutRect(f, 0, 0, bm.fHeight, bm.fWidth);
	PutWord(f, 0x0098);									// PackBitsRect
	PutWord(f, bm.fRowBytes);
	PutRect(f, 0, 0, bm.fHeight, bm.fWidth);			// bounds
	PutRect(f, 0, 0, bm.fHeight, bm.fWidth);			// srcRect
	PutRect(f, 0, 0, bm.fHeight, bm.fWidth);			// dstRect
	PutWord(f, 0);										// srcCopy
	long data = 0;
	for (int y = 0; y < bm.fHeight; y++)
	{
		// each row as literal runs of at most 128 bytes, its length a byte
		int length = 0;
		for (int at = 0; at < bm.fRowBytes; at += 128)
			length += 1 + (bm.fRowBytes - at < 128 ? bm.fRowBytes - at : 128);
		fputc(length, f);
		for (int at = 0; at < bm.fRowBytes; at += 128)
		{
			int run = bm.fRowBytes - at < 128 ? bm.fRowBytes - at : 128;
			fputc(run - 1, f);
			fwrite(&bm.fBits[y * bm.fRowBytes + at], 1, run, f);
		}
		data += 1 + length;
	}
	if (data & 1)
		fputc(0, f);									// (opcodes are word-aligned)
	PutWord(f, 0x00ff);									// OpEndPic
	long end = ftell(f);
	fseek(f, sizeAt, SEEK_SET);
	PutWord(f, (int) ((end - sizeAt) & 0xffff));
	fclose(f);
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc == 3 && strcmp(argv[1], "--card") == 0)
		return WriteCard(argv[2]);
	if (argc == 4 && strcmp(argv[1], "--pict") == 0)
		return WritePict(argv[2], argv[3]);
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
