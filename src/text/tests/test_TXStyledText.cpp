// TXStyledText test (src/text/TXStyledText.h): the characters and runs a
// line is laid out over - its port, the word round an offset (a double
// tap), and how far the caret moves over a character or a picture.  The
// ROM image is imported for its U.S. locale bundle (the break tables).
#include "TXStyledText.h"
#include "TXChars.h"
#include "TXGraphicsRun.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "Ports.h"
#include "Locale.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const ULong kUSABundle = 0x004a4d09;		// the ROM's locale bundle 'USA (as test_Dates)


// A chunked storage over heap blocks (as test_TXLinesHeights's).
class TestChars : public TXChunkedChars
{
public:
			TestChars() : TXChunkedChars(8), fBlockCount(0)	{ memset(fBlocks, 0, sizeof(fBlocks)); }
	virtual	~TestChars()
			{
				for (long i = 0; i < fBlockCount; i++)
					delete[] fBlocks[i];
				gDeleted = true;
			}
	virtual UniChar* GetChunkPtr(long chunk, Boolean, Boolean)	{ return fBlocks[chunk]; }
	virtual NewtonErr AllocateChunks(long at, long count)
			{
				for (long i = fBlockCount - 1; i >= at; i--)
					fBlocks[i + count] = fBlocks[i];
				for (long i = 0; i < count; i++)
					fBlocks[at + i] = new UniChar[fChunkSize];
				fBlockCount += count;
				return noErr;
			}
	virtual void RemoveChunks(long at, long count)
			{
				for (long i = 0; i < count; i++)
					delete[] fBlocks[at + i];
				for (long i = at; i + count < fBlockCount; i++)
					fBlocks[i] = fBlocks[i + count];
				fBlockCount -= count;
			}
	void	SetText(const char* text)
			{
				UniChar buffer[256];
				long n = (long) strlen(text);
				for (long i = 0; i < n; i++)
					buffer[i] = (UniChar) text[i];
				TXTextDescriptor source;
				source.Set(buffer, n);
				Replace(0, Count(), &source);
			}
	UniChar*	fBlocks[64];
	long		fBlockCount;
	static Boolean	gDeleted;
};
Boolean TestChars::gDeleted = false;


// a picture, as far as the caret is concerned
class TestPicture : public TXNewtGraphicsRun
{ };


static void
TestPort()
{
	TXStyledText text;
	TestChars* chars = new TestChars;
	GrafPort own;
	text.IStyledText(nil, chars, 2);
	EXPECT(text.fChars == chars && text.fRuns != nil);
	GrafPtr current;
	GetPort(&current);
	EXPECT(text.GetTextPort() == current);						// none of its own: the current one
	text.SetTextPort(&own);
	EXPECT(text.GetTextPort() == &own);
	EXPECT(text.IsWordSpace(' ') && text.IsWordSpace('\t') && !text.IsWordSpace('a') && !text.IsWordSpace('\r'));
}


static void
TestWords()
{
	TXStyledText* text = new TXStyledText;
	TestChars* chars = new TestChars;
	text->IStyledText(nil, chars, 2);
	chars->SetText("hello   world again");						// 0-5 hello, 5-8 spaces, 8-13 world

	// a word takes the spaces after it
	TXOffsetRange range;
	EXPECT(text->CharToWord(2, false, &range, 0));
	EXPECT(range.fStart.fOffset == 0 && range.fEnd.fOffset == 8);
	EXPECT(!range.fStart.fAtStart && range.fEnd.fAtStart);
	// unless asked for the word alone
	EXPECT(text->CharToWord(2, false, &range, kTXWordNoSpaces));
	EXPECT(range.fStart.fOffset == 0 && range.fEnd.fOffset == 5);
	EXPECT(text->CharToWord(10, false, &range, kTXWordNoSpaces | kTXWordLineBreaks));
	EXPECT(range.fStart.fOffset == 8 && range.fEnd.fOffset == 13);
	// the last word: to the end
	EXPECT(text->CharToWord(17, false, &range, 0));
	EXPECT(range.fStart.fOffset == 14 && range.fEnd.fOffset == 19);
	// at the start of a word, looking back: what is before it - the space
	// (the locale's break table makes a run of spaces a word of its own)
	EXPECT(text->CharToWord(14, true, &range, kTXWordNoSpaces));
	EXPECT(range.fStart.fOffset == 13 && range.fEnd.fOffset == 14);

	// no further than 64 characters either way: a long word is cut there
	char longText[200];
	memset(longText, 'x', 150);
	longText[150] = 0;
	chars->SetText(longText);
	EXPECT(text->CharToWord(100, false, &range, 0));
	EXPECT(range.fStart.fOffset == 36 && range.fEnd.fOffset == 150);	// 100 - 64 .. the end (<= 100 + 64)
	EXPECT(text->CharToWord(10, false, &range, 0));
	EXPECT(range.fStart.fOffset == 0 && range.fEnd.fOffset == 74);	// 0 .. 10 + 64

	// deleting the styled text deletes its characters
	TestChars::gDeleted = false;
	delete text;
	EXPECT(TestChars::gDeleted);
}


static void
TestAdvance()
{
	TXStyledText text;
	TestChars* chars = new TestChars;
	text.IStyledText(nil, chars, 2);
	chars->SetText("ab\x01\x01\x01" "cd");						// a picture over 2..5

	// one character at a time over ordinary runs, all of a picture at once
	TXRunRange* runs = text.fRuns;
	class TextObj : public TXGraphicsRun
	{
	public:
		virtual TXAttrObject* CreateNew(void) const		{ return new TextObj; }
		virtual long	GetClassId(void) const			{ return 'tobj'; }
		virtual unsigned long GetObjFlags(void) const	{ return 0; }
		virtual Ref		GetNSObject(void) const			{ return NILREF; }
		virtual void	SetNSObject(RefArg)				{ }
		virtual void	GetDimensions(int* h, int* w)	{ *h = *w = 0; }
		virtual void	DrawContent(const Rect&)		{ }
	};
	runs->InsertObjectRange(0, 2, new TextObj, false);
	runs->InsertObjectRange(1, 5, new TestPicture, false);
	runs->InsertObjectRange(2, 7, new TextObj, false);
	EXPECT(runs->GetCount() == 3);
	EXPECT(text.AdvanceOffset(0, true) == 1);
	EXPECT(text.AdvanceOffset(1, true) == 1);
	EXPECT(text.AdvanceOffset(2, true) == 3);					// over the picture: all of it
	EXPECT(text.AdvanceOffset(5, true) == 1);
	EXPECT(text.AdvanceOffset(7, true) == 0);					// the end
	EXPECT(text.AdvanceOffset(5, false) == 3);					// back over the picture
	EXPECT(text.AdvanceOffset(2, false) == 1);
	EXPECT(text.AdvanceOffset(0, false) == 0);					// the start
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_TXStyledText: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(kUSABundle)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, RefVar(AllocateFrame()));
	InitInternationalUtils();
	GrafPort port;
	OpenPort(&port);
	newton_try
	{
		TestPort();
		TestWords();
		TestAdvance();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&port);
	printf("test_TXStyledText: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
