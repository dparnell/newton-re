// The cursive reader's orthographic learning (recognition/Ortho.h): the
// sixteen-point DCT, the square root, a letter's trace made a sample, the
// letter-shape database's adding, searching and Occam's verdict, and a
// word's learn array built from a word graph and trained into the
// database.
#include "Ortho.h"
#include "ParaGraph.h"
#include "XrReader.h"
#include "LowLevel.h"
#include "CursiveReader.h"
#include "WordSegment.h"
#include "XrDomains.h"
#include "ByteOrder.h"
#include "memory/host/KernelHeap.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const double kPi = 3.14159265358979323846;

static int failures = 0;
#define EXPECT(c) do { if (!(c)) { failures++; fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)


// A constant goes all into the first coefficient (the sum of the sixteen),
// and back into sixteen equal values; the square root is the floor of the
// true one.
static void
TestArithmetic(void)
{
	int32_t a[16];
	for (int i = 0; i < 16; i++)
		a[i] = 1024;
	FDCT16(a);
	EXPECT(a[0] == 16 * 1024);
	for (int i = 1; i < 16; i++)
		EXPECT(a[i] > -4 && a[i] < 4);
	IDCT16(a);
	for (int i = 1; i < 16; i++)
		EXPECT(a[i] == a[0]);
	// a ramp has its energy in the odd coefficients, and turned back is a
	// ramp again
	for (int i = 0; i < 16; i++)
		a[i] = (i - 8) * 256;
	int32_t b[16];
	memcpy(b, a, sizeof(a));
	FDCT16(b);
	EXPECT(b[1] < -1000 || b[1] > 1000);
	for (int i = 2; i < 16; i += 2)
		EXPECT(b[i] > -64 && b[i] < 64);
	IDCT16(b);
	for (int i = 1; i < 16; i++)
		EXPECT(b[i] > b[i - 1]);

	static const ULong kValues[] = { 0, 1, 2, 3, 4, 15, 16, 17, 99, 100, 65535, 65536, 1000000, 0x3fffffff, 0x40000000, 0xfffffffe, 0xffffffff };
	for (ULong v : kValues)
		EXPECT(SQRT32_ORTO(v) == (long) floor(sqrt((double) v)));
}


// a letter's points as TrainTrajectory is given them: a pen-up first,
// each stroke ended with one, the list with an empty stroke
static long
MakeLetter(PS_point_type* out, const PS_point_type* pts, int n, int strokeBreak = -1)
{
	long k = 0;
	out[k].x = 0; out[k].y = -1; k++;
	for (int i = 0; i < n; i++)
	{
		if (i == strokeBreak)
		{
			out[k].x = 0; out[k].y = -1; k++;
		}
		out[k++] = pts[i];
	}
	out[k].x = 0; out[k].y = -1; k++;
	out[k].x = 0; out[k].y = -1; k++;
	return k;
}

// an o: sixteen points round a circle of the radius, written from the top
static int
MakeO(PS_point_type* pts, int cx, int cy, int r)
{
	for (int i = 0; i < 16; i++)
	{
		double a = -kPi / 2 - i * 2 * kPi / 15;
		pts[i].x = (short) (cx + r * cos(a) + 0.5);
		pts[i].y = (short) (cy + r * sin(a) + 0.5);
	}
	return 16;
}

// an l: a stroke straight down
static int
MakeL(PS_point_type* pts, int x, int top, int h)
{
	for (int i = 0; i < 12; i++)
	{
		pts[i].x = (short) x;
		pts[i].y = (short) (top + i * h / 11);
	}
	return 12;
}


// The letter normalised to its box - the larger side 32 * 1024 across,
// centred - a repeated point left out, and the sample of a letter the
// same whatever its size; a different letter a different sample.
static void
TestSample(void)
{
	PS_point_type pts[20], letter[40];
	int n = MakeO(pts, 100, 100, 40);
	pts[n] = pts[n - 1];			// the same point twice running
	MakeLetter(letter, pts, n + 1);
	ODATA* odata = (ODATA*) calloc(0x80, sizeof(ODATA));
	long strokes = 0;
	long count = TraceToOdata(odata, letter, &strokes);
	EXPECT(count == 16);
	EXPECT(strokes == 1);
	int32_t minX = 0x7fffffff, maxX = -0x7fffffff;
	for (long i = 0; i < count; i++)
	{
		if (odata[i].x < minX) minX = odata[i].x;
		if (odata[i].x > maxX) maxX = odata[i].x;
	}
	EXPECT(maxX - minX >= 32 * 1024 - 64 && maxX - minX <= 32 * 1024);
	EXPECT(odata[0].arc == 0 && odata[count - 1].arc > 0);
	EXPECT(odata[count - 1].arc == odata[count - 2].arc + odata[count - 1].len);
	// a point is not a letter; nor is one under four across
	PS_point_type dot[2] = { { 5, 5 }, { 5, 5 } };
	MakeLetter(letter, dot, 2);
	EXPECT(TraceToOdata(odata, letter, &strokes) == 0);
	PS_point_type tiny[2] = { { 5, 5 }, { 7, 6 } };
	MakeLetter(letter, tiny, 2);
	EXPECT(TraceToOdata(odata, letter, &strokes) == 0);
	free(odata);

	NWTSAMPLE small, big, again, line;
	memset(small, 0, sizeof(small));
	memset(big, 0, sizeof(big));
	memset(again, 0, sizeof(again));
	memset(line, 0, sizeof(line));
	MakeLetter(letter, pts, MakeO(pts, 100, 100, 40));
	EXPECT(FillNwtSample(letter, small) == 1);
	EXPECT(FillNwtSample(letter, again) == 1);
	EXPECT(memcmp(small, again, sizeof(small)) == 0);
	EXPECT(GetBigEndianHalf(small + 2) == 1);
	MakeLetter(letter, pts, MakeO(pts, 300, 200, 80));
	EXPECT(FillNwtSample(letter, big) == 1);
	int far = 0;
	for (int i = kNwtCoefficients; i < kNwtSampleSize; i++)
		if (abs(small[i] - big[i]) > 2)
			far++;
	EXPECT(far == 0);
	MakeLetter(letter, pts, MakeL(pts, 100, 50, 100));
	EXPECT(FillNwtSample(letter, line) == 1);
	far = 0;
	for (int i = kNwtCoefficients; i < kNwtSampleSize; i++)
		if (abs(small[i] - line[i]) > 8)
			far++;
	EXPECT(far > 2);
}


// The database: a class for each letter and number of strokes, the
// newest sample first, 32 at most; the search gathers what is near by
// letter; Occam keeps a new sample of a letter only when it is not
// already there.
static void
TestDatabase(void)
{
	UByte* db = (UByte*) malloc(ORGetDBSize());
	ORInitDB(db, ORGetDBSize());
	EXPECT(GetBigEndianWord(db) == 0x71);
	EXPECT(GetBigEndianWord(db + kOrtoDBSize) == 0x6000);
	EXPECT(GetBigEndianWord(db + kOrtoDBUsed) == 0x10);

	PS_point_type pts[20], letter[40];
	NWTSAMPLE o;
	MakeLetter(letter, pts, MakeO(pts, 100, 100, 40));
	EXPECT(FillNwtSample(letter, o) == 1);
	EXPECT(AddToDataBase(db, o, 'o') == 1);
	EXPECT(GetBigEndianHalf(db + kOrtoDBClasses) == 1);
	EXPECT(GetBigEndianWord(db + kOrtoDBUsed) == 0x10 + 0xc + 0x14);
	UByte* cls = db + kOrtoDBClassTable;
	EXPECT(GetBigEndianHalf(cls) == 'o' && GetBigEndianHalf(cls + 2) == 1 && GetBigEndianHalf(cls + 6) == 1);
	EXPECT(GetBigEndianWord(cls + 8) == 0x1c);
	EXPECT(GetBigEndianHalf(db + 0x1c) == 'o' && GetBigEndianHalf(db + 0x1e) == 0);

	ALIST* list = CreateAlist(0x100);
	ClearAlist(list);
	EXPECT(SearchInDataBase(list, o, db, nil) == 1);
	EXPECT(list->count == 1 && list->e[0].sym == 'o' && list->e[0].dist == 0 && list->e[0].count == 1);
	EXPECT(list->e[0].classOffset == kOrtoDBClassTable && list->e[0].sampleOffset == 0x1c);
	EXPECT(Occam('o', list) == 0);				// already there
	EXPECT(Occam('c', list) == 1);				// the nearest is another letter
	// with a character set leaving the o out, nothing is found at all
	static const UByte kDigits[] = { '1', '2', 0 };
	ClearAlist(list);
	EXPECT(SearchInDataBase(list, o, db, kDigits) == 0);
	DestroyAlist(list);

	// training: the same o again is not kept; the same shape as a c is
	EXPECT(TrainTrajectory(letter, db, 'o') == 0);
	EXPECT(GetBigEndianHalf(db + kOrtoDBClasses) == 1);
	EXPECT(TrainTrajectory(letter, db, 'c') == 1);
	EXPECT(GetBigEndianHalf(db + kOrtoDBClasses) == 2);
	EXPECT(GetBigEndianWord(db + kOrtoDBUsed) == 0x50);
	EXPECT(GetBigEndianWord(cls + 8) == 0x28);						// the o's samples moved along by the new class
	EXPECT(GetBigEndianWord(cls + kOrtoDBClassSize + 8) == 0x3c);
	EXPECT(GetBigEndianHalf(db + 0x3c) == 'c' && GetBigEndianHalf(db + 0x3e) == 1);
	// an l is nearest the o or the c, and is kept
	MakeLetter(letter, pts, MakeL(pts, 100, 50, 100));
	EXPECT(TrainTrajectory(letter, db, 'l') == 1);
	EXPECT(GetBigEndianHalf(db + kOrtoDBClasses) == 3);

	// a class holds 32 samples, newest first; the 33rd pushes the oldest out
	ULong used = GetBigEndianWord(db + kOrtoDBUsed);
	for (int i = 0; i < 40; i++)
	{
		NWTSAMPLE s;
		memcpy(s, o, sizeof(s));
		s[kNwtCoefficients] = (UByte) i;
		EXPECT(AddToDataBase(db, s, 'o') == 1);
	}
	EXPECT(GetBigEndianHalf(cls + 6) == 0x20);
	EXPECT(GetBigEndianWord(db + kOrtoDBUsed) == used + 31 * kNwtSampleSize);
	EXPECT(db[GetBigEndianWord(cls + 8) + kNwtCoefficients] == 39);
	free(db);
}


// A word graph of "to" over five xrs: the learn array has an entry for
// each symbol (and one for the end of the alternatives), each letter
// tied to the stretch of the trace its xrs cover; training the word
// learns both letters into the database.
static void
TestLearnArray(void)
{
	// the trace: a t (points 1-6) and an o (points 7-22), pen-ups at 0 and 23
	PS_point_type trace[30];
	int k = 0;
	trace[k].x = 0; trace[k].y = -1; k++;
	for (int i = 0; i < 6; i++)
	{
		trace[k].x = 20;
		trace[k].y = (short) (10 + i * 12);
		k++;
	}
	PS_point_type o[16];
	MakeO(o, 60, 50, 20);
	for (int i = 0; i < 16; i++)
		trace[k++] = o[i];
	trace[k].x = 0; trace[k].y = -1; k++;

	xrd_el_type el[6];
	memset(el, 0, sizeof(el));
	el[0].type = 1;
	el[1].type = 0x10; XrSetH(el[1].begpoint, 1); XrSetH(el[1].endpoint, 3);
	el[2].type = 0x10; XrSetH(el[2].begpoint, 3); XrSetH(el[2].endpoint, 6);
	el[3].type = 1;
	el[4].type = 0x10; XrSetH(el[4].begpoint, 7); XrSetH(el[4].endpoint, 22);
	el[5].type = 1;
	xrdata_type xr = { 6, 6, el };

	RWS_type rws[3];
	memset(rws, 0, sizeof(rws));
	rws[0].sym = 't'; rws[0].type = 1; rws[0].xrStart = 1; rws[0].xrLen = 2;
	rws[1].sym = 'o'; rws[1].type = 1; rws[1].xrStart = 4; rws[1].xrLen = 1;
	rws[2].type = 3;
	RWG_type rwg;
	memset(&rwg, 0, sizeof(rwg));
	rwg.size = 3;
	rwg.rws = rws;

	void* ortl = nil;
	ULong size = 0;
	ORCreateLearnInfo(&xr, &rwg, &ortl, &size);
	EXPECT(ortl != nil);
	Ptr a = (Ptr) ortl;
	EXPECT(size == 0x18 + 3 * 4 + 2 * 4);
	EXPECT(OrtoSize(a) == size);
	EXPECT(GetBigEndianHalf(a + kOrtoEntries) == 3 && GetBigEndianHalf(a + kOrtoMaxEntries) == 3);
	EXPECT(GetBigEndianHalf(a + kOrtoParts) == 2 && GetBigEndianHalf(a + kOrtoGroups) == 1);
	EXPECT(GetBigEndianWord(a + kOrtoPartsOff) == 0x18 + 3 * 4);
	const UByte* e = (const UByte*) a + kOrtoEntry;
	EXPECT(e[0] == 0 && e[1] == 0 && e[2] == 0 && e[3] == 't');
	EXPECT(e[4] == 1 && e[5] == 1 && e[6] == 1 && e[7] == 'o');
	EXPECT(e[8] == 1 && e[9] == 0 && e[10] == 2 && e[11] == 0);		// the end of the alternatives
	const UByte* parts = (const UByte*) a + GetBigEndianWord(a + kOrtoPartsOff);
	EXPECT(GetBigEndianHalf(parts) == 1 && GetBigEndianHalf(parts + 2) == 6);
	EXPECT(GetBigEndianHalf(parts + 4) == 7 && GetBigEndianHalf(parts + 6) == 22);

	// a letter's points copied out: a pen-up first and after each stretch
	PS_point_type out[0x100];
	EXPECT(LearnPartsCopy(trace, out, (const Part_of_letter*) parts, 1) == 9);
	EXPECT(out[0].y == -1 && out[1].x == 20 && out[1].y == 10 && out[7].y == -1 && out[8].y == -1);

	// trained into a database that already has an l (so the searches
	// find something and Occam has an answer to look at)
	UByte* db = (UByte*) malloc(ORGetDBSize());
	ORInitDB(db, ORGetDBSize());
	PS_point_type pts[20], letter[40];
	MakeLetter(letter, pts, MakeL(pts, 100, 50, 100));
	NWTSAMPLE l;
	EXPECT(FillNwtSample(letter, l) == 1);
	EXPECT(AddToDataBase(db, l, 'x') == 1);
	rec_w_type word;
	memset(&word, 0, sizeof(word));
	strcpy((char*) word.fWord, "to");
	ORTraining(db, trace, &word, ortl);
	EXPECT(GetBigEndianHalf(db + kOrtoDBClasses) == 3);
	EXPECT(GetBigEndianHalf(db + kOrtoDBClassTable + kOrtoDBClassSize) == 't');
	EXPECT(GetBigEndianHalf(db + kOrtoDBClassTable + 2 * kOrtoDBClassSize) == 'o');
	// a word the array does not spell trains nothing
	strcpy((char*) word.fWord, "on");
	ORTraining(db, trace, &word, ortl);
	EXPECT(GetBigEndianHalf(db + kOrtoDBClasses) == 3);
	free(db);

	ORLArrayDelete(&ortl);
	EXPECT(ortl == nil);
}


// The stretches of a letter: one holding the first crossing among the
// letter's inner xrs is taken out, and the rest sorted by where they
// start.
static void
TestRemovePointAndSort(void)
{
	xrd_el_type el[5];
	memset(el, 0, sizeof(el));
	el[0].type = 0x10;
	el[1].type = 0x10;
	el[2].type = 0x34; XrSetH(el[2].begpoint, 11); XrSetH(el[2].endpoint, 12);
	el[3].type = 0x10;
	el[4].type = 0x10;
	xrdata_type xr = { 5, 5, el };
	Part_of_letter parts[3];
	XrSetH(parts[0].beg, 20); XrSetH(parts[0].end, 30);
	XrSetH(parts[1].beg, 10); XrSetH(parts[1].end, 15);
	XrSetH(parts[2].beg, 1); XrSetH(parts[2].end, 5);
	short count = 3;
	RemovePointAndSort(&xr, 0, 4, parts, &count);
	EXPECT(count == 2);
	EXPECT(XrGetH(parts[0].beg) == 1 && XrGetH(parts[1].beg) == 20);
	// no crossing: only sorted
	el[2].type = 0x10;
	XrSetH(parts[0].beg, 20); XrSetH(parts[1].beg, 1);
	count = 2;
	RemovePointAndSort(&xr, 0, 4, parts, &count);
	EXPECT(count == 2 && XrGetH(parts[0].beg) == 1 && XrGetH(parts[1].beg) == 20);
}


int
main()
{
	InitHostStandaloneHeap();
	TestArithmetic();
	TestSample();
	TestDatabase();
	TestLearnArray();
	TestRemovePointAndSort();
	if (failures == 0)
		printf("test_Ortho: all passed\n");
	return failures == 0 ? 0 : 1;
}
