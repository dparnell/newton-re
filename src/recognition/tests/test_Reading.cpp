// The Rosetta engine reading handwriting, end to end: strokes of pen
// points in at `RosettaClassify`, words out at the callback.
//
// The writing is drawn here - letters made of arcs and lines at the
// size a person writes on the Newton's screen, a point a pixel or so
// apart as the tablet samples them - and handed to the engine exactly as
// `TRosRecognizer` hands it the tablet's strokes.  The engine reads them
// against the ROM's own lexicons, grammar and trained classifier, so the
// ROM image is imported for its dictionaries.

#include "Rosetta.h"
#include "RosEngine.h"
#include "WordRecog.h"
#include "ROMDictionaryData.h"
#include "ROMImport.h"
#include "NewtErrors.h"
#include "NewtonMemory.h"
#include "memory/host/KernelHeap.h"

#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include "SkiaHeap.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }
static Fixed	FD(double d)	{ return (Fixed) (int) floor(d * 65536.0 + 0.5); }


/*--------------------------------------------------------------------
	What the engine answers.
--------------------------------------------------------------------*/

static char		gWords[16][64];
static int		gScores[16];
static long		gWordCount = 0;
static long		gCalls = 0;

static void	CheckTheHeap(void);

static void
TestCheckWords(char** words, UniChar* scores, ULong strokes, ULong count)
{
	if (getenv("ROSETTA_HEAPCHECK") != nil)
		CheckTheHeap();
	gCalls++;
	gWordCount = (long) count;
	for (ULong i = 0; i < count && i < 16; i++)
	{
		strncpy(gWords[i], words[i], 63);
		gWords[i][63] = 0;
		gScores[i] = scores[i];
	}
	fprintf(stderr, "  read (%lu strokes):", (unsigned long) strokes);
	for (ULong i = 0; i < count && i < 16; i++)
		fprintf(stderr, " \"%s\"/%d", words[i], (int) scores[i]);
	fprintf(stderr, "\n");
}


/*--------------------------------------------------------------------
	A pen.
--------------------------------------------------------------------*/

static FPoint	gPen[1024];
static long		gPenCount = 0;
static ULong	gTime = 1000;

static void		PenStart(void)				{ gPenCount = 0; }
static void		PenTo(double x, double y)
{
	// the tablet reports a point every pixel or so of travel
	if (gPenCount > 0)
	{
		double lx = gPen[gPenCount - 1].x / 65536.0;
		double ly = gPen[gPenCount - 1].y / 65536.0;
		double d = sqrt((x - lx) * (x - lx) + (y - ly) * (y - ly));
		int steps = (int) (d / 1.2);
		for (int s = 1; s < steps && gPenCount < 1023; s++)
		{
			gPen[gPenCount].x = FD(lx + (x - lx) * s / steps);
			gPen[gPenCount].y = FD(ly + (y - ly) * s / steps);
			gPenCount++;
		}
	}
	if (gPenCount < 1024)
	{
		gPen[gPenCount].x = FD(x);
		gPen[gPenCount].y = FD(y);
		gPenCount++;
	}
}

// The stroke handed to the engine, which takes the points over.
static void
PenUp(void)
{
	FPoint* points = (FPoint*) NewPtr(gPenCount * (long) sizeof(FPoint));
	memcpy(points, gPen, gPenCount * sizeof(FPoint));
	ULong start = gTime;
	gTime += (ULong) gPenCount * 8;
	EXPECT(RosettaClassify((ULong) gPenCount, points, start, gTime) == noErr);
	gTime += 60;
}

static void
Arc(double cx, double cy, double rx, double ry, double from, double to, Boolean first)
{
	int n = 40;
	for (int i = 0; i <= n; i++)
	{
		double a = (from + (to - from) * i / n) * 3.14159265358979 / 180.0;
		if (i == 0 && !first)
			continue;
		PenTo(cx + rx * cos(a), cy - ry * sin(a));
	}
}


/*--------------------------------------------------------------------
	Letters.  `x` is the left of the letter, `base` the baseline; the
	x-height is `h` and ascenders go to twice that.  Angles are
	anticlockwise from three o'clock, y growing downwards as the
	tablet's does.
--------------------------------------------------------------------*/

static double	h = 14;

static double
LetterO(double x, double base)
{
	PenStart();
	Arc(x + h * 0.45, base - h / 2, h * 0.45, h / 2, 80, 440, true);
	PenUp();
	return x + h * 0.9;
}

static double
LetterC(double x, double base)
{
	PenStart();
	Arc(x + h * 0.35, base - h / 2, h * 0.32, h / 2, 50, 310, true);
	PenUp();
	return x + h * 0.85;
}

static double
LetterA(double x, double base)
{
	PenStart();
	Arc(x + h * 0.45, base - h / 2, h * 0.45, h / 2, 20, 380, true);
	PenTo(x + h * 0.9, base - h);
	PenTo(x + h * 0.9, base);
	PenUp();
	return x + h * 1.05;
}

static double
LetterL(double x, double base)
{
	PenStart();
	PenTo(x + h * 0.15, base - 2 * h);
	PenTo(x + h * 0.15, base);
	PenUp();
	return x + h * 0.45;
}

static double
LetterT(double x, double base)
{
	PenStart();
	PenTo(x + h * 0.35, base - 1.7 * h);
	PenTo(x + h * 0.35, base);
	PenUp();
	PenStart();
	PenTo(x, base - h);
	PenTo(x + h * 0.75, base - h);
	PenUp();
	return x + h * 0.85;
}

static double
LetterN(double x, double base)
{
	PenStart();
	PenTo(x, base - h);
	PenTo(x, base);
	PenTo(x, base - h * 0.5);
	Arc(x + h * 0.35, base - h * 0.55, h * 0.35, h * 0.45, 180, 0, false);
	PenTo(x + h * 0.7, base);
	PenUp();
	return x + h * 0.95;
}

static double
LetterI(double x, double base)
{
	PenStart();
	PenTo(x + h * 0.15, base - h);
	PenTo(x + h * 0.15, base);
	PenUp();
	PenStart();
	PenTo(x + h * 0.15, base - h * 1.5);
	PenTo(x + h * 0.2, base - h * 1.45);
	PenUp();
	return x + h * 0.45;
}


typedef double (*LetterProc)(double x, double base);

static LetterProc
Letter(char c)
{
	switch (c)
	{
		case 'o':	return LetterO;
		case 'c':	return LetterC;
		case 'a':	return LetterA;
		case 'l':	return LetterL;
		case 't':	return LetterT;
		case 'n':	return LetterN;
		case 'i':	return LetterI;
	}
	return nil;
}


// A word written and the engine asked what it was.
static const char*
Write(const char* word, double x, double base)
{
	gCalls = 0;
	gWordCount = 0;
	fprintf(stderr, "writing \"%s\"\n", word);
	for (const char* p = word; *p != 0; p++)
		x = Letter(*p)(x, base) + h * 0.25;
	// the end of the word
	EXPECT(RosettaClassify(0, nil, 0, 0) == noErr);
	return gWordCount > 0 ? gWords[0] : "";
}


// A fault is turned into a sanitizer report, which prints the stack
// the fault happened on - there is no debugger on this host.
static void
OnFault(int)
{
	volatile char buffer[8] = { 0 };
	volatile int* misaligned = (volatile int*) (buffer + 1);
	*misaligned = 1;
}


// A pointer given back whose block is already free has been given back
// twice: stop there, with the stack that did it.
static void
CheckDispose(void* p)
{
	SkiaBlock* block = SkiaBlock::Of(p);
	if (getenv("ROSETTA_HEAPCHECK") != nil)
		CheckTheHeap();
	if (block->IsFree())
	{
		fprintf(stderr, "pointer %p given back twice\n", p);
		OnFault(0);
	}
}


// With ROSETTA_HEAPCHECK set, the heap is walked after every allocation
// and the first damaged block stops the test with the stack that made it.
static long	gHeapChecks = 0;
static void
CheckTheHeap(void)
{
	SkiaHeap* heap = (SkiaHeap*) GetCurrentHeap();
	gHeapChecks++;
	SkiaBlock* b = heap->HeaderBlock();
	SkiaBlock* end = heap->Sentinel();
	while (b < end)
	{
		Boolean parentBad = !b->IsFree() && (b->fFlags & kBlockFlag_Direct) != 0
					&& (b->fFlags & kBlockFlag_Private) == 0 && b->fParent != (void*) heap;
		if (b->fSize < kBlockHeaderSize || (b->fSize & (kBlockAlign - 1)) != 0
			|| (char*) b + b->fSize > heap->fEnd || parentBad)
		{
			if (parentBad)
				fprintf(stderr, "block %p (data %p, size %lx) has parent %p, not the heap\n",
						(void*) b, b->Data(), (unsigned long) b->fSize, b->fParent);
			fprintf(stderr, "heap damaged at block %p (size %lx) after %ld checks\n",
					(void*) b, (unsigned long) b->fSize, gHeapChecks);
			OnFault(0);
		}
		b = b->Following();
	}
}


#ifdef _WIN32
#include <windows.h>
// Where a fault happened, relative to the executable, and the return
// addresses on the stack above it - to be looked up in the link map.
static LONG CALLBACK
OnException(EXCEPTION_POINTERS* info)
{
	if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;
	uintptr_t base = (uintptr_t) GetModuleHandleA(nil);
	typedef BOOL (WINAPI *InitProc)(HANDLE, const char*, BOOL);
	typedef BOOL (WINAPI *FromAddrProc)(HANDLE, DWORD64, DWORD64*, void*);
	HMODULE help = LoadLibraryA("dbghelp.dll");
	InitProc init = help ? (InitProc) GetProcAddress(help, "SymInitialize") : nil;
	FromAddrProc from = help ? (FromAddrProc) GetProcAddress(help, "SymFromAddr") : nil;
	HANDLE process = GetCurrentProcess();
	if (init != nil)
		init(process, nil, TRUE);
	static unsigned char buffer[sizeof(DWORD) * 32 + 512];
	fprintf(stderr, "fault at %p\n", info->ExceptionRecord->ExceptionAddress);
	uintptr_t* sp = (uintptr_t*) info->ContextRecord->Rsp;
	uintptr_t addrs[401];
	addrs[0] = (uintptr_t) info->ExceptionRecord->ExceptionAddress;
	long n = 1;
	for (int i = 0; i < 400; i++)
		if (sp[i] > base && sp[i] < base + 0x2000000)
			addrs[n++] = sp[i];
	for (long i = 0; i < n; i++)
	{
		memset(buffer, 0, sizeof(buffer));
		ULONG* sym = (ULONG*) buffer;
		sym[0] = 88;						// SizeOfStruct of SYMBOL_INFO
		sym[20] = 256;						// MaxNameLen
		DWORD64 displacement = 0;
		if (from != nil && from(process, (DWORD64) addrs[i], &displacement, buffer))
			fprintf(stderr, "  %s+0x%llx\n", (const char*) (buffer + 84),
					(unsigned long long) displacement);
		else
			fprintf(stderr, "  +0x%llx (err %lu)\n", (unsigned long long) (addrs[i] - base),
					(unsigned long) GetLastError());
	}
	fflush(stderr);
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif


int
main()
{
#ifdef _WIN32
	AddVectoredExceptionHandler(1, OnException);
#endif
	signal(SIGSEGV, OnFault);
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Reading: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	InitROMDictionaryData();

	if (getenv("ROSETTA_HEAPCHECK") != nil)
		((SkiaHeap*) GetCurrentHeap())->fAfterAllocHook = CheckTheHeap;
	gHostDisposeCheck = CheckDispose;

	EXPECT(RosettaInitialize(F(72), F(72), TestCheckWords) == noErr);
	EXPECT(RosettaAwaken() == noErr);

	// a field of ordinary words
	RosettaAreaInfo area;
	memset(&area, 0, sizeof(area));
	for (long i = 0; i < 8; i++)
		area.fSymbolSet[i] = 0xffffffff;
	for (long i = 0; i < 5; i++)
	{
		area.fMap[i][0] = -1;
		area.fMap[i][1] = -1;
	}
	area.fLetterSpace = 4;
	area.fFlags = kRosAreaLetters;
	EXPECT(RosettaSetArea(&area) == noErr);

	// Each word written, and where the right reading came among the
	// engine's answers.  These letters are drawn by a program, not a
	// hand, and some are ambiguous in ways real writing is not - a
	// lone `o` or an `n` as tall as its neighbours looks as much like
	// a capital as not - so what is checked is that the right word is
	// among the first two, and first for the three that are clear.
	static const char* const kWords[] = { "o", "l", "to", "cat", "no", "tin", "on", "ton" };
	static const int kWithin[] = { 2, 2, 1, 2, 2, 1, 2, 1 };
	long right = 0;
	for (unsigned long i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++)
	{
		const char* read = Write(kWords[i], 40, 100);
		if (strcmp(read, kWords[i]) == 0)
			right++;
		long at = -1;
		for (long k = 0; k < gWordCount && at < 0; k++)
			if (strcmp(gWords[k], kWords[i]) == 0
				|| (kWords[i][0] == 'c' && strcmp(gWords[k], "Cat") == 0))
				at = k;
		EXPECT(at >= 0 && at < kWithin[i]);
		fprintf(stderr, "  -> \"%s\"\n", read);
	}
	fprintf(stderr, "%ld of %lu read right\n", right,
			(unsigned long) (sizeof(kWords) / sizeof(kWords[0])));

	RosettaSleep();
	if (failures != 0)
	{
		fprintf(stderr, "test_Reading: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_Reading: all passed\n");
	return 0;
}
