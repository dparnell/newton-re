// Ink test: the three classes of ink binary told apart, and the eight
// bytes an ink word carries about itself packed, read back, opened out
// and changed.  No ROM image is needed - nothing here reads one - but
// the object heap is, because ink lives in binaries.

#include "Ink.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "FixedMath.h"
#include "Ports.h"		// ToFixed, RoundFixed
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// An ink word of `data` bytes of (made-up) stroke data with its eight
// bytes of information after them.
static Ref
MakeInkWord(long data, ULong width, ULong ascent, ULong descent, ULong xHeight,
			Fixed scale, ULong face, ULong penSize)
{
	RefVar ink(AllocateBinary(RSSYMinkword, data + (long) sizeof(PackedInkWordInfo)));
	memset(BinaryData(ink), 0x5a, (size_t) data);
	PackedInkWordInfo packed;
	PackInkWordInfo(&packed, width, ascent, descent, xHeight, scale, face, penSize);
	SetPackedInkWordInfo(ink, &packed);
	return ink;
}


static void
TestClasses()
{
	RefVar word(MakeInkWord(4, 40, 12, 4, 7, 0x10000, 0, 1));
	RefVar raw(AllocateBinary(RSSYMink2, 8));
	RefVar old(AllocateBinary(RSSYMink, 8));
	RefVar other(AllocateBinary(RSSYMstring, 8));

	EXPECT(IsInk(word) && IsInk(raw) && IsInk(old) && !IsInk(other));
	EXPECT(!IsRawInk(word) && IsRawInk(raw) && IsRawInk(old));
	EXPECT(!IsOldRawInk(word) && !IsOldRawInk(raw) && IsOldRawInk(old));
	EXPECT(IsInkWord(word) && !IsInkWord(raw) && !IsInkWord(old));
	// a frame is not ink, whatever its class
	EXPECT(!IsInk(RefVar(AllocateFrame())));
}


static void
TestFaces()
{
	// bold, italic, underline and outline stay where they are; the two
	// script bits come down to 0x10 and 0x20 and go back up again
	for (ULong face = 0; face < 16; face++)
		EXPECT(GetQDFace(GetRawFace(face)) == face);
	EXPECT(GetRawFace(0x80) == 0x10 && GetQDFace(0x10) == 0x80);
	EXPECT(GetRawFace(0x100) == 0x20 && GetQDFace(0x20) == 0x100);
	EXPECT(GetRawFace(0x183) == 0x33 && GetQDFace(0x33) == 0x183);
}


static void
TestSizes()
{
	// the font size is seven quarters of the x-height, rounded
	EXPECT(GetInkWordFontSize(0) == 0);
	EXPECT(GetInkWordFontSize(4) == 7);
	EXPECT(GetInkWordFontSize(7) == 12);		// 12.25
	EXPECT(GetInkWordFontSize(8) == 14);
	// the pen is one pixel up to ten, then forty over the size and two
	EXPECT(GetStdInkWordPenWidth(9) == 1 && GetStdInkWordPenWidth(10) == 1);
	EXPECT(GetStdInkWordPenWidth(11) == 5);
	EXPECT(GetStdInkWordPenWidth(20) == 4);
	EXPECT(GetStdInkWordPenWidth(41) == 2);
}


static void
TestPacking()
{
	PackedInkWordInfo packed;
	PackInkWordInfo(&packed, 100, 20, 6, 9, 0x18000, 0x101, 3);
	InkWordInfo info;
	ExpandPackedInkWordInfo(&packed, &info);
	EXPECT(info.fWidth == 100 && info.fAscent == 20 && info.fDescent == 6);
	EXPECT(info.fXHeight == 9 && info.fPenSize == 3);
	EXPECT(info.fFace == 0x101);
	EXPECT(info.fScale == 0x18000);			// a scale and a half, to eight fractional bits
	// what follows: the font size the x-height comes to, that size
	// scaled, and the pen that wants
	EXPECT(info.fFontSize == GetInkWordFontSize(9));
	EXPECT(info.fScaledFontSize == RoundFixed(FixedMultiply(ToFixed(info.fFontSize), 0x18000)));
	EXPECT(info.fPenWidth == GetStdInkWordPenWidth((ULong) info.fScaledFontSize));
	EXPECT(info.fScaledWidth == info.fPenWidth + 150);
	EXPECT(info.fScaledAscent == info.fPenWidth + 30);
	EXPECT(info.fScaledDescent == 9);
	EXPECT(info.fScaledHeight == info.fPenWidth + 39);
	EXPECT(info.fScaledXHeight == RoundFixed(FixedMultiply(ToFixed(9), 0x18000)));

	// the scale keeps only eight fractional bits
	PackInkWordInfo(&packed, 1, 1, 1, 1, 0x123ff, 0, 1);
	ExpandPackedInkWordInfo(&packed, &info);
	EXPECT(info.fScale == 0x12300);
}


static void
TestInkWord()
{
	RefVar ink(MakeInkWord(16, 100, 20, 6, 9, 0x10000, 0, 1));
	EXPECT(Length(ink) == 16 + (long) sizeof(PackedInkWordInfo));
	InkWordInfo info;
	GetInkWordInfo(ink, &info);
	EXPECT(info.fWidth == 100 && info.fAscent == 20 && info.fXHeight == 9 && info.fPenSize == 1);

	// the stroke data in front of the eight bytes is left alone
	SetInkWordPenSize(ink, 4);
	SetInkWordFontFace(ink, 0x83);
	SetInkWordScale(ink, 0x20000);
	const unsigned char* data = (const unsigned char*) BinaryData(ink);
	for (long i = 0; i < 16; i++)
		EXPECT(data[i] == 0x5a);
	GetInkWordInfo(ink, &info);
	EXPECT(info.fPenSize == 4 && info.fFace == 0x83 && info.fScale == 0x20000);
	EXPECT(info.fWidth == 100 && info.fAscent == 20 && info.fDescent == 6 && info.fXHeight == 9);

	// a font size is kept as the scale that gets there from the size the
	// x-height comes to
	SetInkWordScale(ink, 0x10000);
	SetInkWordFontSize(ink, 32);
	GetInkWordInfo(ink, &info);
	EXPECT(info.fScaledFontSize == 32);
}


static void
TestXHeight()
{
	// a word of letters: the x-height is left alone unless the word is
	// more than four times as tall as it is wide
	RefVar wide(MakeInkWord(4, 100, 20, 6, 9, 0x10000, 0, 1));
	AdjustInkWordXHeight(wide, false);
	InkWordInfo info;
	GetInkWordInfo(wide, &info);
	EXPECT(info.fXHeight == 9);

	RefVar narrow(MakeInkWord(4, 5, 20, 6, 9, 0x10000, 0, 1));
	AdjustInkWordXHeight(narrow, false);
	GetInkWordInfo(narrow, &info);
	EXPECT(info.fXHeight == (ULong) RoundFixed(FixedMultiply(0x6666, ToFixed(20))));	// two fifths of the ascent

	// numbers: an x-height of more than three fifths of the ascent, with
	// almost nothing below the baseline, is the recogniser having found
	// no ascenders or descenders to measure against
	RefVar digits(MakeInkWord(4, 40, 20, 1, 18, 0x10000, 0, 1));
	AdjustInkWordXHeight(digits, true);
	GetInkWordInfo(digits, &info);
	EXPECT(info.fXHeight == (ULong) RoundFixed(FixedMultiply(0x8ccd, ToFixed(20))));	// 0.55 of it

	// but a word with a real descender is left alone
	RefVar descender(MakeInkWord(4, 40, 20, 8, 18, 0x10000, 0, 1));
	AdjustInkWordXHeight(descender, true);
	GetInkWordInfo(descender, &info);
	EXPECT(info.fXHeight == 18);
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x100000;
	InitObjects();

	TestClasses();
	TestFaces();
	TestSizes();
	TestPacking();
	TestInkWord();
	TestXHeight();

	if (failures == 0)
		printf("test_Ink: all passed\n");
	else
		printf("test_Ink: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
