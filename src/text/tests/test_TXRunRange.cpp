// The text engine's places and stretches (TXOffset.h), its runs and the
// ranges of them (TXRun.h), and the ruler ranges with their pending
// ruler and paragraph measuring (TXRulerRange.h).
#include "TXOffset.h"
#include "TXRun.h"
#include "TXRulerRange.h"
#include "OSErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


/*------------------------------------------------------------------------------
	P l a c e s   a n d   s t r e t c h e s
------------------------------------------------------------------------------*/

static void
TestOffsets()
{
	TXOffsetPos a = { 5, true };
	TXOffsetPos b = { 5, false };
	TXOffsetPos c = { 5, true };
	EXPECT(a == c);
	EXPECT(!(a == b));			// the same offset told apart by the flag

	TXOffsetRange r(3, 9, true, false);
	EXPECT(r.fStart.fOffset == 3 && r.fStart.fAtStart && r.fEnd.fOffset == 9 && !r.fEnd.fAtStart);
	EXPECT(r.Length() == 6);
	TXOffsetRange s(r.fStart, r.fEnd);
	EXPECT(s == r);
	s.Offset(10);
	EXPECT(s.fStart.fOffset == 13 && s.fEnd.fOffset == 19);
	EXPECT(!(s == r));
	s.Set(1, 2, false, true);
	EXPECT(s.fStart.fOffset == 1 && !s.fStart.fAtStart && s.fEnd.fOffset == 2 && s.fEnd.fAtStart);

	// the wrong way round: the offsets are swapped, the flags stay where
	// they were (the ROM's quirk)
	TXOffsetRange w(9, 3, true, false);
	w.CheckBounds();
	EXPECT(w.fStart.fOffset == 3 && w.fEnd.fOffset == 9);
	EXPECT(w.fStart.fAtStart && !w.fEnd.fAtStart);
	TXOffsetRange ok(3, 9, false, true);
	ok.CheckBounds();
	EXPECT(ok.fStart.fOffset == 3 && ok.fEnd.fOffset == 9);

	long x = 1, y = 2;
	TXSwapLong(&x, &y);
	EXPECT(x == 2 && y == 1);
}


/*------------------------------------------------------------------------------
	R u n s
------------------------------------------------------------------------------*/

// A run that is either text or a picture, and nothing more.
class TestRun : public TXRun
{
public:
			TestRun(Boolean text, long id) : fText(text), fId(id)	{ }

	virtual TXAttrObject* CreateNew(void) const			{ return new TestRun(fText, 0); }
	virtual long	GetClassId(void) const				{ return 'trun'; }
	virtual unsigned long GetObjFlags(void) const		{ return fText ? 0 : kTXObjIndivisible; }
	virtual Boolean	IsEqual(const TXAttrObject* other) const
			{
				return other != nil && other->GetClassId() == GetClassId()
					&& ((const TestRun*) other)->fId == fId && ((const TestRun*) other)->fText == fText;
			}
	virtual Ref		GetNSObject(void) const				{ return NILREF; }
	virtual void	SetNSObject(RefArg)					{ }

	virtual Boolean	IsTextRun(void) const				{ return fText; }
	virtual void	GetHeightInfo(int*, int*, int*)		{ }
	virtual void	PixelToChar(const TXLineRunDisplayInfo&, Fixed, TXOffsetRange*)	{ }
	virtual long	CharToPixel(const TXLineRunDisplayInfo&, long)	{ return 0; }
	virtual void	Draw(const TXLineRunDisplayInfo&, long, const Rect&, int)	{ }
	virtual long	MeasureWidth(const TXLineRunDisplayInfo&)	{ return 0; }
	virtual long	LineBreak(const UniChar*, long, long, long*, Boolean, long*)	{ return 0; }

	Boolean		fText;
	long		fId;
};


static void
TestRuns()
{
	TestRun base(true, 1);
	EXPECT(base.VisibleLen(nil, 7) == 7);			// the base's: everything shows

	// text 0..5, a picture 5..6, another 6..7, text 7..10
	TXRunRange runs(2);
	TestRun* text1 = new TestRun(true, 1);
	TestRun* pic1 = new TestRun(false, 2);
	TestRun* pic2 = new TestRun(false, 3);
	TestRun* text2 = new TestRun(true, 4);
	runs.InsertObjectRange(0, 5, text1, false);
	runs.InsertObjectRange(1, 6, pic1, false);
	runs.InsertObjectRange(2, 7, pic2, false);
	runs.InsertObjectRange(3, 10, text2, false);
	EXPECT(runs.GetCount() == 4);

	EXPECT(runs.IsTextRun(0) == text1);
	EXPECT(runs.IsTextRun(1) == nil);
	EXPECT(runs.CharToTextRun(2, false) == text1);
	EXPECT(runs.CharToTextRun(8, false) == text2);
	// in a picture: the nearest text run before it
	EXPECT(runs.CharToTextRun(6, false) == text1);
	EXPECT(runs.SearchTextRunForward(1) == text2);
	EXPECT(runs.SearchTextRunBackward(2) == text1);

	// a picture first: the text run after it
	TXRunRange lead(1);
	TestRun* pic3 = new TestRun(false, 5);
	TestRun* text3 = new TestRun(true, 6);
	lead.InsertObjectRange(0, 1, pic3, false);
	lead.InsertObjectRange(1, 4, text3, false);
	EXPECT(lead.CharToTextRun(0, false) == text3);

	// all pictures: none
	TXRunRange pics(1);
	pics.InsertObjectRange(0, 1, new TestRun(false, 7), false);
	EXPECT(pics.CharToTextRun(0, false) == nil);
}


/*------------------------------------------------------------------------------
	R u l e r s
------------------------------------------------------------------------------*/

// A chunked storage over heap blocks (as test_TXStream's).
class TestChars : public TXChunkedChars
{
public:
			TestChars() : TXChunkedChars(8), fBlockCount(0)	{ memset(fBlocks, 0, sizeof(fBlocks)); }
	virtual	~TestChars()
			{
				for (long i = 0; i < fBlockCount; i++)
					delete[] fBlocks[i];
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
				UniChar buffer[128];
				long n = (long) strlen(text);
				for (long i = 0; i < n; i++)
					buffer[i] = (UniChar) text[i];
				TXTextDescriptor source;
				source.Set(buffer, n);
				Replace(0, Count(), &source);
			}

	UniChar*	fBlocks[64];
	long		fBlockCount;
};


static TXBasicRuler*
MakeRuler(char justification)
{
	TXBasicRuler* ruler = new TXBasicRuler;
	ruler->fJustification = justification;
	return ruler;
}


static void
TestParagraphs()
{
	TestChars chars;
	chars.SetText("one\ntwo\nthree");			// 13 characters: paragraphs at 0, 4, 8
	EXPECT(chars.Count() == 13);

	EXPECT(TXGetParagStartOffset(&chars, 6) == 2);	// "tw|o": two back to the t
	EXPECT(TXGetParagStartOffset(&chars, 4) == 0);	// at a paragraph's start
	EXPECT(TXGetParagStartOffset(&chars, 10) == 2);
	EXPECT(TXGetParagStartOffset(&chars, 2) == 2);	// no break before: all the way back
	EXPECT(TXGetParagStartOffset(&chars, 0) == 0);
	EXPECT(TXGetParagEndOffset(&chars, 5) == 3);	// "w", "o", the break
	EXPECT(TXGetParagEndOffset(&chars, 9) == 4);	// no break after: to the end
	EXPECT(TXGetParagEndOffset(&chars, 13) == 0);

	TXRulerRange rulers(&chars, MakeRuler(9));
	TXBasicRuler* first = MakeRuler(1);
	TXBasicRuler* second = MakeRuler(2);
	rulers.InsertObjectRange(0, 8, first, false);
	rulers.InsertObjectRange(1, 13, second, false);

	// a stretch widened to whole paragraphs
	TXOffsetPos start = { 5, true };
	TXOffsetPos end = { 6, false };
	rulers.CharRangeToParagRange(&start, &end);
	EXPECT(start.fOffset == 4 && !start.fAtStart);
	EXPECT(end.fOffset == 8 && end.fAtStart);
	// an end already just past a break, taken as the next paragraph's start: kept
	TXOffsetPos s2 = { 0, false };
	TXOffsetPos e2 = { 4, true };
	rulers.CharRangeToParagRange(&s2, &e2);
	EXPECT(s2.fOffset == 0 && e2.fOffset == 4);
	// ... but taken as the end of the line, it is widened
	TXOffsetPos e3 = { 4, false };
	rulers.CharRangeToParagRange(&s2, &e3);
	EXPECT(e3.fOffset == 8);

	// no pending ruler: the text does not end in a break
	EXPECT(rulers.GetPendingRuler(13, 0) == nil);
	EXPECT(rulers.OffsetToObject(10, false) == second);
	EXPECT(rulers.OffsetToObject(2, false) == first);

	// the edit crosses from first's paragraphs into second's: the rest
	// of the paragraph it ends in goes with it
	TXAttrObject* pending = nil;
	EXPECT(rulers.GetReplaceExtraChars(5, 10, &pending) == 3);
	EXPECT(pending == nil);
	EXPECT(rulers.GetReplaceExtraChars(1, 3, &pending) == 0);	// inside one ruler's paragraphs
	EXPECT(rulers.GetReplaceExtraChars(2, 2, &pending) == 0);	// nothing selected
}


static void
TestPendingRuler()
{
	// the text ends in a break: the place after it is the paragraph not yet typed
	TestChars chars;
	chars.SetText("ab\n");
	TXBasicRuler* pendingRuler = MakeRuler(9);
	TXRulerRange rulers(&chars, pendingRuler);
	TXBasicRuler* only = MakeRuler(3);
	rulers.InsertObjectRange(0, 3, only, false);

	TXRuler* pending = rulers.GetPendingRuler(3, 0);
	EXPECT(pending == pendingRuler);
	EXPECT(((TXBasicRuler*) pending)->fJustification == 3);	// brought up to date from the ruler before
	EXPECT(rulers.GetPendingRuler(3, 1) == nil);				// a selection is never there
	EXPECT(rulers.GetPendingRuler(2, 0) == nil);
	EXPECT(rulers.OffsetToObject(3, false) == pendingRuler);
	EXPECT(rulers.OffsetToObject(1, false) == only);

	// asked again without being invalidated: left as it is
	only->fJustification = 4;
	EXPECT(((TXBasicRuler*) rulers.GetPendingRuler(3, 0))->fJustification == 3);
	rulers.NukePendingRuler();
	EXPECT(((TXBasicRuler*) rulers.GetPendingRuler(3, 0))->fJustification == 4);

	// an edit reaching the end, when the end is still the paragraph not
	// yet typed, brings it up to date there and then
	rulers.NukePendingRuler();
	only->fJustification = 5;
	rulers.InvalidatePendingRuler(1, 2);
	EXPECT(!rulers.fPendingInvalid);
	EXPECT(pendingRuler->fJustification == 5);

	// an empty text: the pending ruler is made afresh
	TestChars empty;
	TXBasicRuler* old = MakeRuler(7);
	TXRulerRange none(&empty, old);
	TXRuler* fresh = none.GetPendingRuler(0, 0);
	EXPECT(fresh != nil && fresh != old);
	EXPECT(none.fDefaultRuler == fresh);
	EXPECT(none.GetPendingRuler(0, 0) == fresh);				// only once
}


static void
TestValidate()
{
	TestChars chars;
	chars.SetText("one\ntwo\nthree");			// paragraphs at 0, 4, 8
	TXRulerRange rulers(&chars, MakeRuler(9));
	TXBasicRuler* first = MakeRuler(1);
	TXBasicRuler* second = MakeRuler(2);
	rulers.InsertObjectRange(0, 8, first, false);
	rulers.InsertObjectRange(1, 13, second, false);
	EXPECT(rulers.ValidateRuler(0));				// the first always is
	EXPECT(rulers.ValidateRuler(1));				// 8 is just after a break

	// a range starting mid-paragraph (6, in "two") is run into the one before
	TXRulerRange bad(&chars, MakeRuler(9));
	bad.InsertObjectRange(0, 6, MakeRuler(1), false);
	bad.InsertObjectRange(1, 13, MakeRuler(2), false);
	EXPECT(!bad.ValidateRuler(1));
	for (long i = 1; i < bad.GetCount(); i++)
	{
		TXOffset at = bad.GetRangeStart(i);
		EXPECT(chars.GetChar(at - 1) == 0x0a);		// every range now starts a paragraph
	}
	EXPECT(bad.GetLastRangeEnd() == 13);
}


int
main()
{
	InitHostStandaloneHeap();
	TestOffsets();
	TestRuns();
	TestParagraphs();
	TestPendingRuler();
	TestValidate();
	if (failures == 0)
		printf("test_TXRunRange: all passed\n");
	return failures != 0;
}
