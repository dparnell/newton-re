/*
	File:		text/TXViewNatives.cpp

	Contains:	The protoTXView methods (and the three of the text finder
				that searches a TXView's saved text) - TXView.h.  Each
				answers through FailGetTXView, which throws "not a TX view"
				for any other view; an offset outside the text throws
				-8704, a range -8703 (FromObject).

	Reconstructed from the MP2x00 US ROM (0x00249b7c-0x0024af18); each
	function cites its origin.
*/

#include "TXView.h"
#include "TXBinaryChars.h"
#include "TXVBOChars.h"
#include "TXStream.h"
#include "Application.h"
#include "Frames.h"
#include "objects.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "RootView.h"
#include "Fonts.h"
#include "host/RomBugs.h"

#include <new>

const NewtonErr	kTXNativeErrBadOffset	= -8704;
const NewtonErr	kTXNativeErrBadBounds	= -8705;
const NewtonErr	kTXNativeErrBadRange	= -8703;
const NewtonErr	kTXNativeErrBadData		= -8701;


// An integer offset inside the view's text, or -8704.
static long
CheckOffset(TXView* view, RefArg offset)
{
	Long at = RINT(offset);
	if (at < 0 || view->CountChars() < at)
		Throw(exRootException, (void*) (long) kTXNativeErrBadOffset, nil);
	return at;
}


// ROM 0x00249b7c FTXGetCountCharacters
Ref
FTXGetCountCharacters(RefArg rcvr)
{
	return MAKEINT(FailGetTXView(rcvr)->CountChars());
}


// ROM 0x00249b98 FTXSetHiliteRange
// SetHiliteRange(range, reveal, keyView): with keyView, the view is made
// the key view with the range as its selection first.
Ref
FTXSetHiliteRange(RefArg rcvr, RefArg range, RefArg reveal, RefArg keyView)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetRange r;
	FromObject(range, &r, view);
	if (NOTNIL(keyView))
		gRootView->SetKeyView(view, r.fStart.fOffset, r.fEnd.fOffset - r.fStart.fOffset, false);
	view->SetHiliteRange(r, NOTNIL(reveal), true);
	return NILREF;
}


// ROM 0x00249c34 FTXGetHiliteRange
Ref
FTXGetHiliteRange(RefArg rcvr)
{
	TXOffsetRange r;
	FailGetTXView(rcvr)->GetHiliteRange(&r);
	return ToObject(r);
}


// ROM 0x00249c5c FTXGetContinuousRun
Ref
FTXGetContinuousRun(RefArg rcvr)
{
	return FailGetTXView(rcvr)->GetContinuousRun();
}


// ROM 0x00249c74 FTXShowRuler
Ref
FTXShowRuler(RefArg rcvr, RefArg info)
{
	FailGetTXView(rcvr)->ShowRuler(info);
	return NILREF;
}


// ROM 0x00249c98 FTXHideRuler
Ref
FTXHideRuler(RefArg rcvr)
{
	FailGetTXView(rcvr)->HideRuler();
	return NILREF;
}


// ROM 0x00249cb4 FTXUpdateRulerInfo
Ref
FTXUpdateRulerInfo(RefArg rcvr, RefArg info)
{
	FailGetTXView(rcvr)->UpdateRulerInfo(info);
	return NILREF;
}


// ROM 0x00249cd8 FTXIsRulerShown
Ref
FTXIsRulerShown(RefArg rcvr)
{
	return FailGetTXView(rcvr)->fRulerUI == nil ? NILREF : TRUEREF;
}


// ROM 0x00249cfc FTXExternalize
Ref
FTXExternalize(RefArg rcvr)
{
	return FailGetTXView(rcvr)->Externalize();
}


// ROM 0x00249d14 FTXInternalize
Ref
FTXInternalize(RefArg rcvr, RefArg data)
{
	FailGetTXView(rcvr)->Internalize(data);
	return NILREF;
}


// ROM 0x00249d40 FTXIsModified
Ref
FTXIsModified(RefArg rcvr)
{
	return FailGetTXView(rcvr)->IsModified() ? TRUEREF : NILREF;
}


// ROM 0x00249d64 FTXGetRangeData
Ref
FTXGetRangeData(RefArg rcvr, RefArg range, RefArg what)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetRange r;
	FromObject(range, &r, view);
	return view->GetRangeData(&r, what);
}


// ROM 0x00249da8 FTXReplace
// Replace(range, data, undoable): an undoable one starts a new undo batch.
Ref
FTXReplace(RefArg rcvr, RefArg range, RefArg data, RefArg undoable)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetRange r;
	FromObject(range, &r, view);
	if (NOTNIL(undoable))
		gApplication->fNewUndoBatch = true;
	view->Replace(r, data, NOTNIL(undoable), false);
	return NILREF;
}


// ROM 0x00249e30 FTXReplaceAll
// ReplaceAll(string, start, <unused>, data) ==> how many.
Ref
FTXReplaceAll(RefArg rcvr, RefArg find, RefArg start, RefArg /*unused*/, RefArg data)
{
	TXView* view = FailGetTXView(rcvr);
	long at = CheckOffset(view, start);
	LockRef(find);
	long count = view->ReplaceAll((UniChar*) BinaryData(find), at, data);
	UnlockRef(find);
	return MAKEINT(count);
}


// ROM 0x00249ee0 FTXGetWordRange
Ref
FTXGetWordRange(RefArg rcvr, RefArg offset)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetPos at;
	at.fOffset = CheckOffset(view, offset);
	at.fAtStart = false;
	TXOffsetRange r;
	if (!view->GetWordRange(at, &r))
		return NILREF;
	return ToObject(r);
}


// ROM 0x00249f78 FTXGetLineRange
Ref
FTXGetLineRange(RefArg rcvr, RefArg offset)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetPos at;
	at.fOffset = CheckOffset(view, offset);
	at.fAtStart = false;
	TXOffsetRange r;
	if (!view->GetLineRange(at, &r))
		return NILREF;
	return ToObject(r);
}


// ROM 0x0024a024 FTXGetParagraphRange
Ref
FTXGetParagraphRange(RefArg rcvr, RefArg offset)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetPos at;
	at.fOffset = CheckOffset(view, offset);
	at.fAtStart = false;
	TXOffsetRange r;
	if (!view->GetParagraphRange(at, &r))
		return NILREF;
	return ToObject(r);
}


// ROM 0x0024a0d0 FTXPointToChar
// PointToChar({x, y}) ==> the range there, nil for none.
Ref
FTXPointToChar(RefArg rcvr, RefArg point)
{
	Point pt;
	pt.h = (short) RINT(GetFrameSlotRef(point, RSSYMx));
	pt.v = (short) RINT(GetFrameSlotRef(point, RSSYMy));
	TXView* view = FailGetTXView(rcvr);
	TXOffsetRange r;
	if (!view->PointToChar(pt, &r))
		return NILREF;
	return ToObject(r);
}


// ROM 0x0024a184 FTXCharToPoint
// CharToPoint(offset) ==> {x, y, lineHeight}.
Ref
FTXCharToPoint(RefArg rcvr, RefArg offset)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetPos at;
	at.fOffset = CheckOffset(view, offset);
	at.fAtStart = false;
	int height;
	Point pt = view->CharToPoint(at, &height);
	RefVar result(Clone(RefVar(Rtxchartopointresult)));
	SetFrameSlot(result, RSSYMx, RefVar(MAKEINT(pt.h)));
	SetFrameSlot(result, RSSYMy, RefVar(MAKEINT(pt.v)));
	SetFrameSlot(result, RSSYMlineheight, RefVar(MAKEINT(height)));
	return result;
}


// ROM 0x0024a2c8 FTXScroll
// Scroll({x, y}): the text moved by that much (the view's scroll the
// other way).
Ref
FTXScroll(RefArg rcvr, RefArg delta)
{
	Long x = RINT(GetFrameSlotRef(delta, RSSYMx));
	TXLongPoint d;
	d.v = RINT(GetFrameSlotRef(delta, RSSYMy));
	d.v = -d.v;
	d.h = -x;
	FailGetTXView(rcvr)->Scroll(&d);
	return NILREF;
}


// ROM 0x0024a3a4 FTXGetScrollValues
Ref
FTXGetScrollValues(RefArg rcvr)
{
	TXLongPoint scrolled;
	FailGetTXView(rcvr)->GetScrollValues(&scrolled);
	RefVar result(Clone(RefVar(Rcanonicalpoint)));
	SetFrameSlot(result, RSSYMx, RefVar(MAKEINT(scrolled.h)));
	SetFrameSlot(result, RSSYMy, RefVar(MAKEINT(scrolled.v)));
	return result;
}


// ROM 0x0024a444 FTXGetTotalHeight
Ref
FTXGetTotalHeight(RefArg rcvr)
{
	return MAKEINT(FailGetTXView(rcvr)->GetTotalHeight());
}


// ROM 0x0024a460 FTXGetTotalWidth
Ref
FTXGetTotalWidth(RefArg rcvr)
{
	return MAKEINT(FailGetTXView(rcvr)->GetTotalWidth());
}


// ROM 0x0024a47c FTXGetTextViewRect
Ref
FTXGetTextViewRect(RefArg rcvr)
{
	RgnHandle rgn = FailGetTXView(rcvr)->GetTextViewRgn();
	Rect r = (**rgn).rgnBBox;
	return ToObject(r);
}


// ROM 0x0024a4b0 FTXViewFindString
// FindString(string, start, <unused>) ==> the offset, nil for none.
Ref
FTXViewFindString(RefArg rcvr, RefArg find, RefArg start, RefArg /*unused*/)
{
	TXView* view = FailGetTXView(rcvr);
	long at = CheckOffset(view, start);
	LockRef(find);
	long found = view->FindString((UniChar*) BinaryData(find), at);
	UnlockRef(find);
	return found < 0 ? NILREF : MAKEINT(found);
}


// ROM 0x0024a574 FTXGetCountPages
// (with no pages, the view's address as an integer - TXView::GetCountPages)
Ref
FTXGetCountPages(RefArg rcvr)
{
	return MAKEINT(FailGetTXView(rcvr)->GetCountPages());
}


// ROM 0x0024a590 FTXInsertPageBreak
Ref
FTXInsertPageBreak(RefArg rcvr, RefArg range)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetRange r;
	FromObject(range, &r, view);
	view->InsertPageBreak(r);
	return NILREF;
}


// ROM 0x0024a5d0 FTXSetStore
Ref
FTXSetStore(RefArg rcvr, RefArg store)
{
	FailGetTXView(rcvr)->SetStore(store);
	return NILREF;
}


// ROM 0x0024a5f4 FTXSetGeometry
// SetGeometry(paginate, width, height, margins).
Ref
FTXSetGeometry(RefArg rcvr, RefArg paginate, RefArg width, RefArg height, RefArg margins)
{
	Rect r;
	if (!FromObject(margins, r))
		Throw(exRootException, (void*) (long) kTXNativeErrBadBounds, nil);
	Long h = RINT(height);
	Long w = RINT(width);
	Boolean pages = NOTNIL(paginate);
	FailGetTXView(rcvr)->SetGeometry(pages, (int) w, (int) h, r);
	return NILREF;
}


// ROM 0x0024a6b0 FTXSetDrawOrigin
Ref
FTXSetDrawOrigin(RefArg rcvr, RefArg origin)
{
	Long x = RINT(GetFrameSlotRef(origin, RSSYMx));
	TXLongPoint p;
	p.v = RINT(GetFrameSlotRef(origin, RSSYMy));
	p.h = x;
	FailGetTXView(rcvr)->SetDrawOrigin(p);
	return NILREF;
}


// ROM 0x0024a740 FTXCut
Ref
FTXCut(RefArg rcvr)
{
	FailGetTXView(rcvr)->Cut();
	return NILREF;
}


// ROM 0x0024a75c FTXSetReadOnly
Ref
FTXSetReadOnly(RefArg rcvr, RefArg readOnly)
{
	Boolean on = NOTNIL(readOnly);
	FailGetTXView(rcvr)->SetReadOnly(on);
	return NILREF;
}


// ROM 0x0024a78c FTXFinderFindString
// The finder's FindString(frame, string, start): the characters of a
// saved text (an Externalize frame) are made once and kept in the
// finder's txCharsObj while it looks at the same frame.
Ref
FTXFinderFindString(RefArg rcvr, RefArg frame, RefArg find, RefArg start, RefArg /*unused*/)
{
	RefVar kept(GetFrameSlotRef(rcvr, RSSYMtxcharsobj));
	TXChars* chars = nil;
	if (NOTNIL(kept) && EQRef(frame, RefVar(GetFrameSlotRef(rcvr, RSSYMframe))))
		chars = (TXChars*) BinaryData(kept);
	if (chars == nil)
	{
		RefVar object;
		RefVar text(GetFrameSlotRef(frame, RSSYMtxtext));
		if (ISNIL(text))
		{
			text = GetFrameSlotRef(frame, RSSYMtext);
			if (ISNIL(text))
				Throw(exRootException, (void*) (long) kTXNativeErrBadData, nil);
			// DEVIATION: sized from sizeof on the host
			object = AllocateFramesCObject(sizeof(TXBinaryChars), GCDeleteTXChars, nil, nil);
			chars = new (BinaryData(object)) TXBinaryChars(text);
		}
		else
		{
			// DEVIATION: sized from sizeof on the host
			object = AllocateFramesCObject(sizeof(TXVBOChars), GCDeleteTXChars, nil, nil);
			TXVBOChars* vbo = new (BinaryData(object)) TXVBOChars(RefVar(NILREF));
			chars = vbo;
			vbo->SetCharsVBO(text);
			RefVar data(GetFrameSlotRef(frame, RSSYMtxdata));
			if (ISNIL(data))
				Throw(exRootException, (void*) (long) kTXNativeErrBadData, nil);
			TXBinaryStream stream(data, true, 0, true);
			unsigned char flags;
			NewtonErr err = stream.ReadBytes(&flags, 1);
			if (err == noErr)
				err = vbo->ReadChunksRanges(&stream);
			if (err != noErr)
				Throw(exRootException, (void*) (long) err, nil);
		}
		SetFrameSlot(rcvr, RSSYMtxcharsobj, object);
		SetFrameSlot(rcvr, RSSYMframe, frame);
	}
	Long at = RINT(start);
	if (at < 0 || chars->Count() < at)
		Throw(exRootException, (void*) (long) kTXNativeErrBadOffset, nil);
	LockRef(find);
	long found = TXFindString(chars, (UniChar*) BinaryData(find), at);
	UnlockRef(find);
	return found < 0 ? NILREF : MAKEINT(found);
}


// ROM 0x0024ab04 FTXFinderGetRangeText
// The text of a range of the characters FindString kept, as a string.
Ref
FTXFinderGetRangeText(RefArg rcvr, RefArg range)
{
	TXChars* chars = (TXChars*) BinaryData(GetFrameSlotRef(rcvr, RSSYMtxcharsobj));
	TXOffsetRange r;
	FromObject(range, &r, nil);
	if (r.fStart.fOffset < 0 || r.fEnd.fOffset > chars->Count() || r.fStart.fOffset > r.fEnd.fOffset)
		Throw(exRootException, (void*) (long) kTXNativeErrBadRange, nil);
	long n = r.fEnd.fOffset - r.fStart.fOffset;
	RefVar string(AllocateBinary(RSSYMstring, (n + 1) * 2));
	LockRef(string);
	UniChar* into = (UniChar*) BinaryData(string);
	TXTextDescriptor from, to;
	from.Set(chars, r.fStart.fOffset, n);
	to.Set(into, n);
	NewtonErr err = from.CopyTo(&to, n);
	into[n] = 0;
	UnlockRef(string);
	if (err != noErr)
		Throw(exRootException, (void*) (long) err, nil);
	return string;
}


// ROM 0x0024ac60 FTXFinderGetCountCharacters
Ref
FTXFinderGetCountCharacters(RefArg rcvr)
{
	TXChars* chars = (TXChars*) BinaryData(GetFrameSlotRef(rcvr, RSSYMtxcharsobj));
	return MAKEINT(chars->Count());
}


// ROM 0x0024aca0 FTXCopy
Ref
FTXCopy(RefArg rcvr)
{
	FailGetTXView(rcvr)->Copy();
	return NILREF;
}


// ROM 0x0024acbc FTXPaste
Ref
FTXPaste(RefArg rcvr)
{
	FailGetTXView(rcvr)->Paste();
	return NILREF;
}


// ROM 0x0024acd8 FTXClear
Ref
FTXClear(RefArg rcvr)
{
	FailGetTXView(rcvr)->Clear();
	return NILREF;
}


// ROM 0x0024acf4 FTXChangeRangeRuns
// ChangeRangeRuns(range, style, toggle, undoable); a packed font spec is
// opened out into a frame first.
// ROM BUG (fixed): GetFontFamilyNum already answers an integer Ref, and it is
// shifted again, so the family of a packed spec comes out four times its
// number.  The fix puts the Ref in as it is.
Ref
FTXChangeRangeRuns(RefArg rcvr, RefArg range, RefArg style, RefArg toggle, RefArg undoable)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetRange r;
	FromObject(range, &r, view);
	if (NOTNIL(undoable))
		gApplication->fNewUndoBatch = true;
	if (ISINT(style))
	{
		RefVar spec(Clone(RefVar(Rcanonicalfontspec)));
		SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(GetFontSize(style))));
		SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(GetFontFace(style))));
		if (RomBugFixed())
			SetFrameSlot(spec, RSSYMfamily, RefVar(GetFontFamilyNum(style)));
		else
			SetFrameSlot(spec, RSSYMfamily, RefVar((Ref) (GetFontFamilyNum(style) << 2)));
		view->ChangeRangeRuns(r, spec, NOTNIL(toggle), NOTNIL(undoable));
	}
	else
		view->ChangeRangeRuns(r, style, NOTNIL(toggle), NOTNIL(undoable));
	return NILREF;
}


// ROM 0x0024ae90 FTXChangeRangeRulers
Ref
FTXChangeRangeRulers(RefArg rcvr, RefArg range, RefArg ruler, RefArg undoable)
{
	TXView* view = FailGetTXView(rcvr);
	TXOffsetRange r;
	FromObject(range, &r, view);
	Boolean undo = false;
	if (NOTNIL(undoable))
	{
		gApplication->fNewUndoBatch = true;
		undo = true;
	}
	view->ChangeRangeRulers(r, ruler, undo);
	return NILREF;
}


// the text engine's view's arms of the views' style natives
// (views/FontNatives.cpp, ViewNatives.cpp: FChangeStylesOfRange 0x001eeba8
// and FGetStylesOfRange 0x001eea68)
static Ref
TXViewGetRangeStyles(TView* view, long start, long end)
{
	TXOffsetRange range(start, end, false, true);
	return ((TXView*) view)->GetRangeData(&range, RSSYMstyles);
}


static void
TXViewChangeRangeRuns(TView* view, long start, long end, RefArg style, Boolean redraw)
{
	TXOffsetRange range(start, end, false, true);
	((TXView*) view)->ChangeRangeRuns(range, style, false, redraw);
}


void
RegisterTXViewNatives(void)
{
	gTXViewStylesHooks.fGetRangeStyles = TXViewGetRangeStyles;
	gTXViewStylesHooks.fChangeRangeRuns = TXViewChangeRangeRuns;
	RegisterNativeFunction("FTXGetCountCharacters", (void*) FTXGetCountCharacters, 0);
	RegisterNativeFunction("FTXSetHiliteRange", (void*) FTXSetHiliteRange, 3);
	RegisterNativeFunction("FTXGetHiliteRange", (void*) FTXGetHiliteRange, 0);
	RegisterNativeFunction("FTXGetContinuousRun", (void*) FTXGetContinuousRun, 0);
	RegisterNativeFunction("FTXShowRuler", (void*) FTXShowRuler, 1);
	RegisterNativeFunction("FTXHideRuler", (void*) FTXHideRuler, 0);
	RegisterNativeFunction("FTXUpdateRulerInfo", (void*) FTXUpdateRulerInfo, 1);
	RegisterNativeFunction("FTXIsRulerShown", (void*) FTXIsRulerShown, 0);
	RegisterNativeFunction("FTXExternalize", (void*) FTXExternalize, 0);
	RegisterNativeFunction("FTXInternalize", (void*) FTXInternalize, 1);
	RegisterNativeFunction("FTXIsModified", (void*) FTXIsModified, 0);
	RegisterNativeFunction("FTXGetRangeData", (void*) FTXGetRangeData, 2);
	RegisterNativeFunction("FTXReplace", (void*) FTXReplace, 3);
	RegisterNativeFunction("FTXReplaceAll", (void*) FTXReplaceAll, 4);
	RegisterNativeFunction("FTXGetWordRange", (void*) FTXGetWordRange, 1);
	RegisterNativeFunction("FTXGetLineRange", (void*) FTXGetLineRange, 1);
	RegisterNativeFunction("FTXGetParagraphRange", (void*) FTXGetParagraphRange, 1);
	RegisterNativeFunction("FTXPointToChar", (void*) FTXPointToChar, 1);
	RegisterNativeFunction("FTXCharToPoint", (void*) FTXCharToPoint, 1);
	RegisterNativeFunction("FTXScroll", (void*) FTXScroll, 1);
	RegisterNativeFunction("FTXGetScrollValues", (void*) FTXGetScrollValues, 0);
	RegisterNativeFunction("FTXGetTotalHeight", (void*) FTXGetTotalHeight, 0);
	RegisterNativeFunction("FTXGetTotalWidth", (void*) FTXGetTotalWidth, 0);
	RegisterNativeFunction("FTXGetTextViewRect", (void*) FTXGetTextViewRect, 0);
	RegisterNativeFunction("FTXViewFindString", (void*) FTXViewFindString, 3);
	RegisterNativeFunction("FTXGetCountPages", (void*) FTXGetCountPages, 0);
	RegisterNativeFunction("FTXInsertPageBreak", (void*) FTXInsertPageBreak, 1);
	RegisterNativeFunction("FTXSetStore", (void*) FTXSetStore, 1);
	RegisterNativeFunction("FTXSetGeometry", (void*) FTXSetGeometry, 4);
	RegisterNativeFunction("FTXSetDrawOrigin", (void*) FTXSetDrawOrigin, 1);
	RegisterNativeFunction("FTXCut", (void*) FTXCut, 0);
	RegisterNativeFunction("FTXSetReadOnly", (void*) FTXSetReadOnly, 1);
	RegisterNativeFunction("FTXFinderFindString", (void*) FTXFinderFindString, 4);
	RegisterNativeFunction("FTXFinderGetRangeText", (void*) FTXFinderGetRangeText, 1);
	RegisterNativeFunction("FTXFinderGetCountCharacters", (void*) FTXFinderGetCountCharacters, 0);
	RegisterNativeFunction("FTXCopy", (void*) FTXCopy, 0);
	RegisterNativeFunction("FTXPaste", (void*) FTXPaste, 0);
	RegisterNativeFunction("FTXClear", (void*) FTXClear, 0);
	RegisterNativeFunction("FTXChangeRangeRuns", (void*) FTXChangeRangeRuns, 4);
	RegisterNativeFunction("FTXChangeRangeRulers", (void*) FTXChangeRangeRulers, 3);
}
