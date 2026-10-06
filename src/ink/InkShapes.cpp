/*
	File:		ink/InkShapes.cpp

	Contains:	Ink as a shape frame, and the two editing operations over
				one - InkShapes.h.

				A shape is what the rest of the system passes writing
				about: MakePolygonForm's ink verb over a viewBounds, with
				the ink itself in an `ink` slot.  GetPolyAsTStrokes opens
				one back up, expanding the ink at the place the bounds
				put it, and SplitInkAt and MergeInk work on the strokes
				and pack them up again - which is all a word being cut in
				two at a caret, or two words being joined, really are.
*/

#include "InkShapes.h"
#include "Stroke.h"
#include "objects.h"
#include "ObjectHeap.h"
#include "RichString.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "NewtonMemory.h"
#include "Rects.h"
#include "Ports.h"			// RoundFixed
#include "Locale.h"			// GetPreference
#include "DrawShape.h"		// MakePolygonForm, kInkVerb
#include "View.h"
#include "ViewFlags.h"		// clEditView
#include "EditView.h"
#include "ParagraphView.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "StrokeBundle.h"
#include "FixedMath.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "CICCodec.h"		// ConvertData
#include "ParaGraph.h"		// HWRMemoryFree
#include "host/RomBugs.h"


/*------------------------------------------------------------------------------
	A   s h a p e   t a k e n   a p a r t
------------------------------------------------------------------------------*/

// ROM 0x001a3340 GetPolyAsTStrokes__FRC6RefVarUl
// The strokes of an ink shape, put back where the shape's viewBounds say
// they are.  The group is the thinning the codec is to use.
TStroke**
GetPolyAsTStrokes(RefArg form, ULong group)
{
	RefVar ink(GetProtoVariable(form, RSSYMink, nil));
	if (ISNIL(ink))
		return nil;
	RefVar bounds(GetProtoVariable(form, RSSYMviewbounds, nil));
	Long x = RINT(GetFrameSlot(bounds, RSSYMleft));
	Long y = RINT(GetFrameSlot(bounds, RSSYMtop));
	return InkExpand(ink, group, x, y);
}


/*------------------------------------------------------------------------------
	A   s h a p e   g i v e n   t o   a   v i e w
------------------------------------------------------------------------------*/

// ROM 0x001a2b70 AddInk__FP5TViewRC6RefVar
// An ink shape made a child of an edit view.  The frame that comes in is
// remade rather than used as it stands: the word's own pen size is what
// the new one is given, and its bounds are taken from the old one's
// viewBounds, which is where the caller has put it.
void
AddInk(TView* view, RefArg form)
{
	if (!view->DerivedFrom(clEditView))
		return;
	RefVar ink(GetProtoVariable(form, RSSYMink, nil));
	InkWordInfo info;
	GetInkWordInfo(ink, &info);
	Rect box;
	RefVar bounds(GetProtoVariable(form, RSSYMviewbounds, nil));
	FromObject(bounds, box);
	RefVar shape(MakePolygonForm(nil, 0, kInkVerb, box, (long) info.fPenSize));
	SetFrameSlot(shape, RSSYMink, ink);
	((TEditView*) view)->AddForm(shape);
}


/*------------------------------------------------------------------------------
	A   w o r d   c u t   i n   t w o
------------------------------------------------------------------------------*/

// ROM 0x001a2c60 SplitInkAt__FRC6RefVarlT2
// An ink word cut at x, which is a coordinate of the view the shape sits
// in: the word may have been scaled, so x is first put back into the
// ink's own coordinates - as far along the unscaled width as it is along
// the drawn one.
//
// Each stroke then goes to whichever side of the cut its middle is on,
// and the two heaps are packed up as ink words of their own carrying the
// original's scale and pen size.  Two things stop it: a stroke that
// crosses the cut with slop to spare on both sides (a letter written
// across it, which cannot be divided), and one side coming out empty.
// Either way the answer is nil and nothing is changed.
//
// The strokes are not copied.  Clone marks one as having another user,
// so the same stroke can be in the list it came from and in one of the
// two heaps at once, and all three lists can be given back at the end.
Ref
SplitInkAt(RefArg form, long x, long slop)
{
	RefVar result;
	Rect box;
	RefVar bounds(GetProtoVariable(form, RSSYMviewbounds, nil));
	FromObject(bounds, box);
	RefVar ink(GetProtoVariable(form, RSSYMink, nil));
	if (!IsInkWord(ink))
		return result;

	InkWordInfo info;
	GetInkWordInfo(ink, &info);
	x = box.left + (long) (((ULong) info.fWidth * (ULong) (x - box.left))
						   / (ULong) (box.right - box.left));

	TStroke** strokes = GetPolyAsTStrokes(form, 0);
	if (strokes == nil)
		return result;
	Size size = GetPtrSize((Ptr) strokes);
	TStroke** left = (TStroke**) NewPtrClear(size);
	TStroke** right = (TStroke**) NewPtrClear(size);
	newton_try
	{
		if (left != nil && right != nil)
		{
			long nLeft = 0;
			long nRight = 0;
			long lo = x - slop;
			long hi = x + slop;
			Boolean crossed = false;
			for (long i = 0; strokes[i] != nil; i++)
			{
				TStroke* stroke = strokes[i];
				long a = (short) RoundFixed(stroke->fBBox.left);
				long b = (short) RoundFixed(stroke->fBBox.right);
				if (lo > a && hi < b)
				{
					crossed = true;
					break;
				}
				stroke->Clone();
				if (x > ((a + b) >> 1))
					left[nLeft++] = stroke;
				else
					right[nRight++] = stroke;
			}
			if (!crossed && nLeft != 0 && nRight != 0)
			{
				result = MakeArray(2);
				RefVar half(MakeInkWordPoly(left));
				RefVar halfInk(GetFrameSlot(half, RSSYMink));
				halfInk = SetInkWordScale(halfInk, info.fScale);
				halfInk = SetInkWordPenSize(halfInk, info.fPenSize);
				SetArraySlot(result, 0, half);
				half = MakeInkWordPoly(right);
				halfInk = GetFrameSlot(half, RSSYMink);
				halfInk = SetInkWordScale(halfInk, info.fScale);
				halfInk = SetInkWordPenSize(halfInk, info.fPenSize);
				SetArraySlot(result, 1, half);
			}
		}
	}
	newton_catch(exRootException)
	{
		// (whatever has been made so far is the answer; the ROM swallows
		// an object-system exception here rather than let the strokes go
		// unreturned)
	}
	end_try;
	DisposeTStrokes(strokes);
	DisposeTStrokes(right);
	DisposeTStrokes(left);
	return result;
}


/*------------------------------------------------------------------------------
	T w o   w o r d s   j o i n e d
------------------------------------------------------------------------------*/

// ROM 0x001a2fc4 MergeInk__FRC6RefVarT1
// Two ink words put together.  The second's strokes are moved along so
// that its left edge sits at the first's right, the two lists are laid
// end to end, and the word that comes out carries the first one's scale
// and pen size.
//
// The joined list holds the same strokes the two came out of rather than
// copies, so it is emptied before being given back - its strokes belong
// to the lists that are disposed above it.
Ref
MergeInk(RefArg first, RefArg second)
{
	RefVar result;
	RefVar firstInk(GetProtoVariable(first, RSSYMink, nil));
	RefVar secondInk(GetProtoVariable(second, RSSYMink, nil));
	if (!IsInkWord(firstInk) || !IsInkWord(secondInk))
		return result;

	InkWordInfo info;
	GetInkWordInfo(firstInk, &info);
	TStroke** a = GetPolyAsTStrokes(first, 0);
	TStroke** b = GetPolyAsTStrokes(second, 0);
	Rect boxA;
	Rect boxB;
	InkBounds(a, &boxA);
	InkBounds(b, &boxB);
	OffsetStrokes(b, (long) ((ULong) (boxA.right - boxB.left) << 16), 0);
	TStroke** both = (TStroke**) NewPtrClear((CountTStrokes(a) + CountTStrokes(b) + 1)
											 * (long) sizeof(TStroke*));
	if (a != nil && b != nil && both != nil)
	{
		long n = 0;
		for (long i = 0; (both[n] = a[i]) != nil; i++)
			n++;
		for (long i = 0; (both[n] = b[i]) != nil; i++)
			n++;
		result = MakeInkWordPoly(both);
		RefVar ink(GetFrameSlot(result, RSSYMink));
		SetInkWordScale(ink, info.fScale);
		SetInkWordPenSize(ink, info.fPenSize);
	}
	DisposeTStrokes(a);
	DisposeTStrokes(b);
	if (both != nil)
		both[0] = nil;
	DisposeTStrokes(both);
	return result;
}


/*------------------------------------------------------------------------------
	S t r o k e   b u n d l e s   a n d   i n k
------------------------------------------------------------------------------*/

// ROM 0x001406bc StrokeBundleToInkWord__FRC6RefVar
// The ink word a bundle of strokes makes.  The answer is kept in the
// bundle's own `inkWord` slot, so a bundle handed round several views is
// only packed once.
Ref
StrokeBundleToInkWord(RefArg bundle)
{
	RefVar ink(GetProtoVariable(bundle, RSSYMinkword, nil));
	if (ISNIL(ink))
	{
		TStroke** strokes = StrokeBundleToTStrokes(bundle);
		ink = TStrokesToInkWord(strokes, nil);
		SetFrameSlot(bundle, RSSYMinkword, ink);
		DisposeTStrokes(strokes);
	}
	return ink;
}


// ROM 0x001a2090 CompressStrokes__FRC6RefVar
// A bundle as a shape: an ink word, laid out as a word would be.
Ref
CompressStrokes(RefArg bundle)
{
	TStroke** strokes = StrokeBundleToTStrokes(bundle);
	if (strokes == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	RefVar form(MakeInkWordPoly(strokes));
	DisposeTStrokes(strokes);
	return form;
}


// ROM 0x001a2100 CompressStrokesToInk__FRC6RefVar
// ... and as a sketch, kept the size it was drawn.
Ref
CompressStrokesToInk(RefArg bundle)
{
	TStroke** strokes = StrokeBundleToTStrokes(bundle);
	if (strokes == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	RefVar form(MakeInkPoly(strokes));
	DisposeTStrokes(strokes);
	return form;
}


// ROM 0x001a2344 ExpandInk__FRC6RefVarl
// The other way about: an ink shape opened back up into a bundle of
// strokes, which is what a script gets when it asks for the writing
// behind a piece of ink.  (The format is read and thrown away - the
// strokes always come back in eighths of a pixel.)
Ref
ExpandInk(RefArg form, long format)
{
	RefVar bundle;
	TStroke** strokes = GetPolyAsTStrokes(form, 0);
	if (strokes != nil)
	{
		long count = CountTStrokes(strokes);
		RefVar list(AllocateArray(RSSYMarray, count));
		for (long i = 0; strokes[i] != nil; i++)
			SetArraySlot(list, i, RefVar(MakeStrokeRef(strokes[i])));
		bundle = Clone(RefVar(Rstrokebundle));
		SetFrameSlot(bundle, RSSYMbounds,
					 RefVar(GetProtoVariable(form, RSSYMviewbounds, nil)));
		SetFrameSlot(bundle, RSSYMstrokes, list);
		DisposeTStrokes(strokes);
	}
	return bundle;
}


/*------------------------------------------------------------------------------
	T h e   i n k   i n   a   p a r a g r a p h
------------------------------------------------------------------------------*/

// ROM 0x001a163c NextInkIndex__FRC6RefVarl
// The next ink word in a paragraph's text after an offset.  An ink word
// stands in the text as the character 0xf701, so the walk is simply for
// the next one of those; ==> its offset, or -1 when the text runs out
// first.
long
NextInkIndex(RefArg para, long index)
{
	long found = -1;
	RefVar text(GetProtoVariable(para, RSSYMtext, nil));
	if (IsString(text))
	{
		TRichString rich(text);
		const UniChar* s = rich.GrabPtr();
		ULong length = Ustrlen(s);
		ULong i = (ULong) (index + 1);
		if (i < length)
		{
			while (s[i] != 0)
			{
				found = (long) i;
				if (s[i] == kInkWordChar)
					break;
				found = -1;
				i++;
			}
		}
	}
	return found;
}


// ROM 0x001a170c GetInkAt__FP14TParagraphViewl
// The ink word at an offset in a paragraph, as a shape frame placed
// where the paragraph draws it.  The pen is the user's own rather than
// the word's, which is how ink pulled out of a paragraph comes out at
// the width the user is writing with now.
Ref
GetInkAt(TParagraphView* para, long offset)
{
	RefVar form;
	Rect box;
	RefVar ink(para->GetInkRefAndBounds(offset, &box));
	if (!IsInkWord(ink))
		ink = NILREF;
	else if (NOTNIL(ink))
	{
		form = MakePolygonForm(nil, 0, kInkVerb, box, RINT(GetPreference(RSSYMuserpensize)));
		SetFrameSlot(form, RSSYMink, ink);
	}
	return form;
}


/*------------------------------------------------------------------------------
	T h e   N e w t o n S c r i p t   f u n c t i o n s
------------------------------------------------------------------------------*/

// ROM 0x001a015c FAddInk
static Ref
FAddInk(RefArg rcvr, RefArg view, RefArg form)
{
	AddInk(FailGetView(view), form);
	return NILREF;
}


// ROM 0x001a0184 FSplitInkAt
static Ref
FSplitInkAt(RefArg rcvr, RefArg form, RefArg x, RefArg slop)
{
	return SplitInkAt(form, RINT(x), RINT(slop));
}


// ROM 0x001a01e0 FMergeInk
static Ref
FMergeInk(RefArg rcvr, RefArg first, RefArg second)
{
	return MergeInk(first, second);
}


// ROM 0x001a3a10 FNextInkIndex
static Ref
FNextInkIndex(RefArg rcvr, RefArg para, RefArg index)
{
	long next = NextInkIndex(para, ISNIL(index) ? -1 : RINT(index));
	return next < 0 ? NILREF : MAKEINT(next);
}


// ROM 0x0014074c FStrokeBundleToInkWord
static Ref
FStrokeBundleToInkWord(RefArg rcvr, RefArg bundle)
{
	return StrokeBundleToInkWord(bundle);
}


// ROM 0x0019fef8 FCompressStrokes
static Ref
FCompressStrokes(RefArg rcvr, RefArg bundle)
{
	return CompressStrokes(bundle);
}


// ROM 0x0019ff00 FCompressStrokesToInk
static Ref
FCompressStrokesToInk(RefArg rcvr, RefArg bundle)
{
	return CompressStrokesToInk(bundle);
}


// ROM 0x0019ff08 FExpandInk
static Ref
FExpandInk(RefArg rcvr, RefArg form, RefArg format)
{
	return ExpandInk(form, RINT(format));
}


// ROM 0x001a3a60 FGetInkAt
static Ref
FGetInkAt(RefArg rcvr, RefArg view, RefArg offset)
{
	return GetInkAt((TParagraphView*) FailGetView(view), RINT(offset));
}


// ROM 0x001a15c8 PolyContainsInk__FRC6RefVar
// Whether a shape frame has writing in it - which for the polygon form
// an ink word takes is simply whether it carries an `ink` slot.
Boolean
PolyContainsInk(RefArg form)
{
	return NOTNIL(GetProtoVariable(form, RSSYMink, nil));
}


// ROM 0x001a15f4 ParaContainsInk__FRC6RefVar
// ... and whether a paragraph's data frame has any ink word among its
// style runs.
Boolean
ParaContainsInk(RefArg para)
{
	return NextInkIndex(para, -1) != -1;
}


// ROM 0x001a3798 FPolyContainsInk
static Ref
FPolyContainsInk(RefArg /*rcvr*/, RefArg form)
{
	return MAKEBOOLEAN(PolyContainsInk(form));
}


// ROM 0x001a37bc FParaContainsInk
static Ref
FParaContainsInk(RefArg /*rcvr*/, RefArg para)
{
	return MAKEBOOLEAN(ParaContainsInk(para));
}


// ROM 0x001a3aa4 FCalcInkBounds
// CalcInkBounds(shape): the box the writing of an ink shape covers,
// worked out from the strokes themselves and written back into the
// shape's `viewBounds`.  nil for a shape that is not ink.
static Ref
FCalcInkBounds(RefArg /*rcvr*/, RefArg form)
{
	RefVar bounds;
	RefVar bundle(ExpandInk(form, 0));
	if (NOTNIL(bundle))
	{
		Rect box;
		GetBundleBounds(bundle, &box);
		bounds = ToObject(box);
		SetFrameSlot(form, RSSYMviewbounds, bounds);
	}
	return bounds;
}


// ROM 0x0019f918 FGetInkWordInfo__FRC6RefVarT1
// GetInkWordInfo(word): what an ink word measures, as a frame - the
// measurements it was written at (`orig...`) and the ones it is drawn at
// now (`cur...`), with the face and the scale between them.  The scale
// is a percentage here, where the word itself holds a 16.16 fraction.
static Ref
FGetInkWordInfo(RefArg /*rcvr*/, RefArg word)
{
	InkWordInfo info;
	GetInkWordInfo(word, &info);
	RefVar result(Clone(RefVar(Rcanonicalinkwordinfo)));
	SetFrameSlot(result, RSSYMorigwidth, RefVar(MAKEINT(info.fWidth)));
	SetFrameSlot(result, RSSYMorigascent, RefVar(MAKEINT(info.fAscent)));
	SetFrameSlot(result, RSSYMorigdescent, RefVar(MAKEINT(info.fDescent)));
	SetFrameSlot(result, RSSYMorigxheight, RefVar(MAKEINT(info.fXHeight)));
	SetFrameSlot(result, RSSYMfontface, RefVar(MAKEINT(info.fFace)));
	SetFrameSlot(result, RSSYMscale, RefVar(MAKEINT(RoundFixed(FixedMultiply(ToFixed(100), info.fScale)))));
	SetFrameSlot(result, RSSYMorigpensize, RefVar(MAKEINT(info.fPenSize)));
	SetFrameSlot(result, RSSYMorigfontsize, RefVar(MAKEINT(info.fFontSize)));
	SetFrameSlot(result, RSSYMcurfontsize, RefVar(MAKEINT(info.fScaledFontSize)));
	SetFrameSlot(result, RSSYMcurwidth, RefVar(MAKEINT(info.fScaledWidth)));
	SetFrameSlot(result, RSSYMcurheight, RefVar(MAKEINT(info.fScaledHeight)));
	SetFrameSlot(result, RSSYMcurascent, RefVar(MAKEINT(info.fScaledAscent)));
	SetFrameSlot(result, RSSYMcurxheight, RefVar(MAKEINT(info.fScaledXHeight)));
	SetFrameSlot(result, RSSYMcurdescent, RefVar(MAKEINT(info.fScaledDescent)));
	return result;
}


// ROM 0x001feaa8 FNumInkWordsInRange
// NumInkWordsInRange(string, start, count): how many ink words a rich
// string has in that range of its characters; a nil count asks about the
// whole of it.
static Ref
FNumInkWordsInRange(RefArg /*rcvr*/, RefArg string, RefArg start, RefArg count)
{
	TRichString rich(string);
	if (ISNIL(count))
		return MAKEINT(rich.NumInkWords());
	ULong n = (ULong) RINT(count);
	ULong from = (ULong) RINT(start);
	return MAKEINT(rich.NumInkWordsInRange(from, n));
}


// ROM 0x00140dec InkConvert__FRC6RefVarT1
// Ink re-encoded as another class: 'ink (the older code book, 2), 'ink2 or
// 'inkWord (the newer, 3) - anything else meaning whichever of the two it
// is not (old raw ink becomes 'ink2, the rest 'ink).  Ink already of the
// class is cloned.  The data goes through the codec's converter; an ink
// word's eight bytes of measurements are left off going in and, for an
// 'inkWord, the strokes are expanded, scaled to an ink word's size and
// packed again, which writes them anew.  nil when it is not ink, or the
// converter fails.
//
// ROM BUG (fixed): the copy the converter is handed is never given back.
// The fix gives it back once the converted data is in the result (or the
// conversion has failed).
Ref
InkConvert(RefArg ink, RefArg cls)
{
	RefVar result;
	if (!IsInk(ink))
		return result;
	RefVar target(cls);
	long kind;
	UShort format;
	if (EQRef(cls, RSSYMink))
	{
		kind = 0;
		format = 2;
	}
	else if (EQRef(cls, RSSYMink2))
	{
		kind = 1;
		format = 3;
	}
	else if (EQRef(cls, RSSYMinkword))
	{
		kind = 2;
		format = 3;
	}
	else if (IsOldRawInk(ink))
	{
		target = RSSYMink2;
		kind = 1;
		format = 3;
	}
	else
	{
		target = RSSYMink;
		kind = 0;
		format = 2;
	}
	if (EQRef(ClassOf(ink), target))
		return Clone(ink);
	ULong size = Length(ink);
	if (IsInkWord(ink))
		size -= sizeof(PackedInkWordInfo);
	Ptr copy = NewPtr(size);
	BlockMove(BinaryData(ink), copy, size);
	void* data = copy;
	if (ConvertData(&data, &size, format))
	{
		result = AllocateBinary(target, kind == 2 ? size + sizeof(PackedInkWordInfo) : size);
		BlockMove(data, BinaryData(result), size);
		if (data != copy)
			HWRMemoryFree((Ptr) data);
		if (RomBugFixed())
		{
			DisposPtr(copy);
			copy = nil;
		}
		if (kind == 2)
		{
			TStroke** strokes = InkExpand(result, 0, 0, 0);
			if (strokes == nil)
				return NILREF;
			Rect bounds;
			UnionBounds(strokes, &bounds);
			ScaleStrokesForInkWord(strokes, &bounds);
			result = InkCompress(strokes, true);
			DisposeTStrokes(strokes);
		}
	}
	if (RomBugFixed() && copy != nil)
		DisposPtr(copy);
	return result;
}


// ROM 0x0014108c FInkConvert
// InkConvert(ink, class)
static Ref
FInkConvert(RefArg /*rcvr*/, RefArg ink, RefArg cls)
{
	return InkConvert(ink, cls);
}


void
RegisterInkNatives(void)
{
	RegisterNativeFunction("FAddInk", (void*) FAddInk, 2);
	RegisterNativeFunction("FSplitInkAt", (void*) FSplitInkAt, 3);
	RegisterNativeFunction("FMergeInk", (void*) FMergeInk, 2);
	RegisterNativeFunction("FNextInkIndex", (void*) FNextInkIndex, 2);
	RegisterNativeFunction("FGetInkAt", (void*) FGetInkAt, 2);
	RegisterNativeFunction("FStrokeBundleToInkWord", (void*) FStrokeBundleToInkWord, 1);
	RegisterNativeFunction("FCompressStrokes", (void*) FCompressStrokes, 1);
	RegisterNativeFunction("FCompressStrokesToInk", (void*) FCompressStrokesToInk, 1);
	RegisterNativeFunction("FExpandInk", (void*) FExpandInk, 2);
	RegisterNativeFunction("FPolyContainsInk", (void*) FPolyContainsInk, 1);
	RegisterNativeFunction("FParaContainsInk", (void*) FParaContainsInk, 1);
	RegisterNativeFunction("FCalcInkBounds", (void*) FCalcInkBounds, 1);
	RegisterNativeFunction("FGetInkWordInfo__FRC6RefVarT1", (void*) FGetInkWordInfo, 1);
	RegisterNativeFunction("FNumInkWordsInRange", (void*) FNumInkWordsInRange, 3);
	RegisterNativeFunction("FInkConvert", (void*) FInkConvert, 2);
}
