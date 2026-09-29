/*
	File:		text/TXView.cpp

	Contains:	TXView, protoTXView's view, and the Newton's handlers for
				the text engine (TXView.h).

	Reconstructed from the MP2x00 US ROM (0x0024659c-0x0024dff4); each
	function cites its origin.
*/

#include "TXView.h"
#include "TXCommand.h"
#include "TXContainer.h"
#include "TXNewtContainer.h"
#include "TXBinaryChars.h"
#include "TXNewtTextRun.h"
#include "TXGraphicsRun.h"
#include "TXRuler.h"
#include "TXRulerRange.h"
#include "TXFormatter.h"
#include "TXFrameFormatter.h"
#include "TXFrames.h"
#include "TXStream.h"
#include "TXUtilities.h"
#include "RootView.h"
#include "ViewFlags.h"
#include "Commands.h"
#include "Application.h"
#include "Keyboard.h"
#include "UnitPublic.h"
#include "Stroke.h"
#include "StrokeQueue.h"
#include "SoundSettings.h"
#include "NewtWorld.h"
#include "Soups.h"
#include "Fonts.h"
#include "Screen.h"
#include "Regions.h"
#include "Rects.h"
#include "Unicode.h"
#include "SortTables.h"
#include "Frames.h"
#include "Objects.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Interpreter.h"
#include "NewtonExceptions.h"
#include "NSErrors.h"
#include "OSErrors.h"

#include <new>
#include <string.h>

// the errors a protoTXView throws (evt.ex)
const NewtonErr	kTXViewErrBadData		= -8701;	// replacement data that does not add up
const NewtonErr	kTXViewErrBadOffset		= -8704;	// an offset outside the text (FTX... natives)
const NewtonErr	kTXViewErrBadRange		= -8703;	// a range outside the text (FromObject)
const NewtonErr	kTXViewErrBadBounds		= -8705;	// SetGeometry's margins not a bounds frame
const NewtonErr	kTXViewErrBadStore		= -48017;	// SetStore's store not valid (evt.ex.fr.store)

// The engine started by the first TXView (the ROM keeps it in an unnamed
// byte at 0x0c104e98).
static Boolean	gTXViewStarted = false;


/*------------------------------------------------------------------------------
	T X V i e w
------------------------------------------------------------------------------*/

// ROM 0x0024659c ClassID__6TXViewCFv
long
TXView::ClassID(void) const
{
	return clTXView;
}


// ROM 0x0024af18 DerivedFrom__6TXViewCFl
Boolean
TXView::DerivedFrom(long id) const
{
	return id == clTXView || TView::DerivedFrom(id);
}


// ROM 0x0024c400 __dt__6TXViewFv
TXView::~TXView()
{
	if (fText != nil)
		delete fText;
	if (fRulerUI != nil)
	{
		// NOT YET RECONSTRUCTED: TXRulerUI - the ruler deleted and
		// gTXRulerPixMaps released (fRulerUI is never set on the host)
	}
}


// ROM 0x0024b930 Constructor__6TXViewFRC6RefVarP5TView
// The first one starts the engine: the stream factory, TextensionStart,
// the Newton's default text run, graphics run and ruler, and the ruler
// bar's globals.  Then the view is made as any view is (the ROM has
// TView::Constructor's body inlined here).
void
TXView::Constructor(RefArg context, TView* parent)
{
	fText = nil;
	fTXFlags = kTXViewModified;
	fRulerUI = nil;
	fTotalHeight = -1;
	fPageWidth = 0;
	memset(&fMargins, 0, sizeof(fMargins));
	if (!gTXViewStarted)
	{
		TXSetTempStreamFactory(new TXNewtStreamFactory);
		NewtonErr err = Textension::TextensionStart();
		if (err != noErr)
			Throw(exRootException, (void*) (long) err, nil);
		Textension::RegisterRun(new TXNewtTextRun);
		Textension::RegisterRun(new TXNewtGraphicsRun);
		Textension::RegisterRuler(new TXAdvancedRuler);
		// NOT YET RECONSTRUCTED: TXRulerUI::Start({0x16, 0x10, 3}) - the
		// ruler bar's measurements
		gTXViewStarted = true;
	}
	TView::Constructor(context, parent);
}


// ROM 0x0024c3e0 TextFlags__6TXViewCFv
// Unless the view says otherwise, text is typed into it (and read, when it
// is not read-only or protected).
long
TXView::TextFlags(void) const
{
	ULong flags = fFlags;
	long text = TView::TextFlags();
	if ((text & 0x1c000) == 0)
	{
		if ((flags & 0x82) == 0)
			text |= 0xc000;
		else
			text |= 0x4000;
	}
	return text;
}


// ROM 0x0024af4c SetBounds__6TXViewFRC5TRect
// (the ROM has SyncViewRgn's body inlined here)
void
TXView::SetBounds(const Rect& bounds)
{
	TView::SetBounds(bounds);
	if (fText == nil)
		return;
	SyncViewRgn();
}


// ROM 0x0024af78 SyncViewRgn__6TXViewFv
// The display's view region and the frames' origin set to the view's
// bounds.
void
TXView::SyncViewRgn(void)
{
	Rect r = viewBounds;
	if (fRulerUI != nil)
	{
		// NOT YET RECONSTRUCTED: TXRulerUI - the ruler's 0x26 pixels
		// above the text and its bounds set
	}
	fText->fDisplay->fFrames->SetFramesOrigin(r.left, r.top);
	RgnHandle region = (RgnHandle) gTXTempRegions->Get();
	RectRgn(region, &r);
	fText->fDisplay->SetViewRgn(region);
	gTXTempRegions->Done(region);
	Dirty(nil);
}


// ROM 0x0024b058 GeometryChanged__6TXViewFUc
// The page's size (the view's region less the margins, unless SetGeometry
// gave one) and margins given to the frames; redisplayed when asked.
void
TXView::GeometryChanged(Boolean redisplay)
{
	Rect bounds = (**fText->fDisplay->fViewRgn).rgnBBox;
	if (fPageWidth < 1)
	{
		fPageWidth = (bounds.right - bounds.left) - (fMargins.left + fMargins.right);
		fPageHeight = (bounds.bottom - bounds.top) - (fMargins.top + fMargins.bottom);
	}
	TXFrames* frames = fText->fDisplay->fFrames;
	TXDisplayChanges changes;
	TXLongPoint size;
	size.v = fPageHeight;
	size.h = fPageWidth;
	frames->SetTextBoundsSize(size, &changes, 0);
	frames->SetFramesMargins(fMargins, &changes);
	fText->fFormatter->CheckRulerSettings();
	if (redisplay)
	{
		fText->DisplayChanged(changes);
		UpdateRuler(false);
		Dirty(nil);
		Edited(false, true, true);
	}
}


// ROM 0x0024dea4 SetGeometry__6TXViewFUciT2RC5TRect
// The page's size and margins; a size under 10 wide or 16 high means the
// view's, and margins that would leave less than that are dropped.  Pages
// can only be asked for before the document is made.
void
TXView::SetGeometry(Boolean paginate, int width, int height, const Rect& margins)
{
	if (fReadOnly)
		return;
	if (fText == nil && paginate)
		fTXFlags |= kTXViewPaginated;
	Rect r = margins;
	if ((fTXFlags & kTXViewPaginated) && r.top < 1)
		r.top = 1;
	long w, h;
	if (width < 10)
		w = -1;
	else
	{
		w = width - (r.left + r.right);
		if (w < 10)
		{
			r.right = 0;
			r.left = 0;
			w = width;
		}
	}
	if (height < 0x10)
		h = -1;
	else
	{
		h = height - (r.bottom + r.top);
		if (h < 0x10)
		{
			r.bottom = 0;
			r.top = 0;
			h = height;
		}
	}
	fPageHeight = h;
	fPageWidth = w;
	fMargins = r;
	if (fText != nil)
		GeometryChanged(true);
	fTXFlags |= kTXViewModified;
}


// ROM 0x0024dfd8 SetDrawOrigin__6TXViewFRC11TXLongPoint
void
TXView::SetDrawOrigin(const TXLongPoint& origin)
{
	TXFrames* frames = fText->fDisplay->fFrames;
	frames->fDrawOriginH = origin.h;
	frames->fDrawOriginV = origin.v;
}


// ROM 0x0024d78c SetStore__6TXViewFRC6RefVar
// The store the text is to be kept on - only before the document is made.
void
TXView::SetStore(RefArg store)
{
	if (fText != nil)
		return;
	if (NOTNIL(fStore))
		return;
	if (StoreIsValid(store) == NILREF)
		Throw(exStoreError, (void*) (long) kTXViewErrBadStore, nil);
	fStore = store;
}


// ROM 0x0024dba8 SetReadOnly__6TXViewFUc
void
TXView::SetReadOnly(Boolean readOnly)
{
	fReadOnly = readOnly;
}


// ROM 0x0024b568 IsModified__6TXViewFv
Boolean
TXView::IsModified(void)
{
	return (fTXFlags & kTXViewModified) != 0;
}


// ROM 0x0024be00 CountChars__6TXViewFv
long
TXView::CountChars(void)
{
	return fText->fChars->Count();
}


// ROM 0x0024be10 GetTotalHeight__6TXViewFv
long
TXView::GetTotalHeight(void)
{
	return fText->fDisplay->fFrames->GetTotalHeight();
}


// ROM 0x0024be24 GetTotalWidth__6TXViewFv
long
TXView::GetTotalWidth(void)
{
	return fText->fDisplay->fFrames->GetTotalWidth();
}


// ROM 0x0024d358 GetCountPages__6TXViewFv
// ROM BUG: with no pages the function answers whatever r0 held - the
// view's own address - which the caller turns into an integer.
long
TXView::GetCountPages(void)
{
	if (fTXFlags & kTXViewPaginated)
	{
		// NOT YET RECONSTRUCTED: TXPageFrames - its page count (vtable
		// +0x3c); a paginated document is never made on the host
		return 0;
	}
	return (long) (intptr_t) this;
}


// ROM 0x0024bfe4 GetTextViewRgn__6TXViewFv
RgnHandle
TXView::GetTextViewRgn(void)
{
	return fText->fDisplay->fViewRgn;
}


// ROM 0x0024d1bc CreateNewTextension__6TXViewFv
// The document: the characters in a string (or, with a store, on it),
// the Newton's hilite and display, and - when pages were asked for - the
// page frames.
void
TXView::CreateNewTextension(void)
{
	TXHandlers handlers;
	fText = new Textension;
	if (fText != nil)
	{
		TXChars* chars;
		if (ISNIL(fStore))
			chars = new TXBinaryChars(RefVar(NILREF));
		else
		{
			// NOT YET RECONSTRUCTED: TXVBOChars(fStore) - the text on a
			// store; SetStore's store is ignored on the host
			chars = new TXBinaryChars(RefVar(NILREF));
		}
		if (chars != nil)
		{
			handlers.fChars = chars;
			TXNewtHilite* hilite = new TXNewtHilite(this);
			if (hilite != nil)
			{
				handlers.fHilite = hilite;
				TXNewtDisplay* display = new TXNewtDisplay(this);
				if (display != nil)
				{
					handlers.fDisplay = display;
					if (fTXFlags & kTXViewPaginated)
					{
						// NOT YET RECONSTRUCTED: TXPageFrames /
						// TXNewtPageFrames - the document is made on one
						// frame instead
					}
					NewtonErr err = fText->ITextension(nil, handlers, 0);
					if (err != noErr)
						Throw(exRootException, (void*) (long) err, nil);
					SyncViewRgn();
					GeometryChanged(false);
					return;
				}
				delete hilite;
			}
			delete chars;
		}
	}
	Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
}


// ROM 0x0024be38 SetupDone__6TXViewFv
// The document made, the view's font (or the user's) the default style,
// the hilite made active, the view set up with the display quiet, and
// the scrollers told.
void
TXView::SetupDone(void)
{
	CreateNewTextension();
	RefVar font(GetProto(RSSYMviewfont));
	if (ISNIL(font))
		font = NSCallGlobalFn(RSSYMgetuserconfig, RSSYMuserfont);
	if (NOTNIL(font))
	{
		TXAttrValues* old = Textension::fDefaultRunAttrValues;
		if (old != nil)
			delete old;
		TXAttrValues* values = new TXAttrValues;
		TXNewtFontFamilyInfo* family = new TXNewtFontFamilyInfo(RefVar(GetFontFamilySym(font)));
		values->Add(kTXAttrFont, &family, sizeof(family), true);
		long size = GetFontSize(font);
		values->Add(kTXAttrSize, &size, sizeof(size), false);
		long face = GetFontFace(font);
		values->Add(kTXAttrFace, &face, sizeof(face), false);
		Textension::fDefaultRunAttrValues = values;
	}
	fText->Activate(true, true);
	TXDisplay* display = fText->fDisplay;
	display->DisableDrawing();
	TView::SetupDone();
	display->EnableDrawing();
	if (NOTNIL(GetProto(RSSYMviewupdatescrollersscript)))
		fTXFlags |= kTXViewHasScrollers;
	UpdateScrollers(true, true);
}


// ROM 0x0024b57c RealDraw__6TXViewFR5TRect
void
TXView::RealDraw(Rect& bounds)
{
	if (fRulerUI != nil)
	{
		// NOT YET RECONSTRUCTED: TXRulerUI::Draw where the ruler meets
		// the rectangle
	}
	fText->fDisplay->Draw(bounds);
}


// ROM 0x0024c158 NarrowVisByIntersectingObscuringSiblingsAndUncles__6TXViewFP5TViewP5TRect
void
TXView::NarrowVisByIntersectingObscuringSiblingsAndUncles(TView* upTo, Rect* bounds)
{
	TView::NarrowVisByIntersectingObscuringSiblingsAndUncles(upTo, bounds);
	if (fRulerUI == nil)
		return;
	// NOT YET RECONSTRUCTED: TXRulerUI - the ruler's bounds taken out of
	// the port's clip region
}


// ROM 0x0024b5e4 Idle__6TXViewFl
// A tap on the hilite that no second tap followed is a tap after all.
long
TXView::Idle(long /*arg*/)
{
	if (fTXFlags & kTXViewTapPending)
	{
		TXNewtPen pen(fTapPt);
		Click(&pen, aeTap);
		fTXFlags &= ~kTXViewTapPending;
	}
	long result = 0;
	RefVar value(RunCacheScript(kIndexViewIdleScript, RefVar(NILREF)));
	if (NOTNIL(value) && ISINT(value))
		result = RVALUE(value);
	return result;
}


/*------------------------------------------------------------------------------
	T h e   c o m m a n d s
------------------------------------------------------------------------------*/

// ROM 0x0024b19c RealDoCommand__6TXViewFRC6RefVar
// The keys, a string, the pen (a click, a tap, a hilite click), the scrub
// and caret gestures over the text, a new font for the selection (0x49)
// and the view's own undo command (0xd3); anything else is TView's.
Boolean
TXView::RealDoCommand(RefArg cmd)
{
	Long result = 0;
	long id = CommandID(cmd);
	switch (id)
	{
	case aeKeyDown:
	case aeKeyRepeat:
		{
			Boolean isCommandKey;
			result = TView::HandleKeyEvent(cmd, id, &isCommandKey);
			if (result == 0)
			{
				result = 1;
				if (!isCommandKey)
				{
					ULong parameter = CommandParameter(cmd);
					KeyDown((UniChar) parameter, (parameter & 0x2000000) != 0);
				}
			}
		}
		break;
	case aeKeyString:
		result = TView::HandleKeyEvent(cmd, aeKeyString, nil);
		if (result == 0)
		{
			RefVar string(CommandFrameParameter(cmd));
			LockRef(string);
			UniChar* chars = (UniChar*) BinaryData(string);
			KeyString(chars, Ustrlen(chars));
			UnlockRef(string);
			result = 1;
		}
		break;
	case aeClick:
	case aeTap:
	case aeHiliteClick:
		{
			TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
			TXNewtPen pen(unit->Stroke());
			result = Click(&pen, id);
		}
		break;
	case aeScrub:
	case aeCaret:
		{
			TUnitPublic* unit = (TUnitPublic*) CommandParameter(cmd);
			Rect unitBounds;
			unit->Bounds(&unitBounds);
			Rect bounds = viewBounds;
			if (fRulerUI != nil)
				bounds.top += 0x26;
			if (CoveredBy(&unitBounds, &bounds) < 0x4c)
				result = 1;
			else if (id == aeCaret)
				result = HandleCaretGesture(unit);
			else
				result = Scrub(unit);
		}
		break;
	case 0x49:
		{
			RefVar spec(CommandFrameParameter(cmd));
			RefVar style(NILREF);
			if (ISNIL(spec))
				spec = MAKEINT(CommandParameter(cmd));
			if (ISINT(spec))
			{
				// ROM BUG: the size and the face go into the frame as the
				// bare numbers GetFontSize and GetFontFace answer, not as
				// integer Refs (the family, a Ref already, is right)
				style = Clone(RefVar(Rcanonicalfontspec));
				SetFrameSlot(style, RSSYMsize, RefVar((Ref) GetFontSize(spec)));
				SetFrameSlot(style, RSSYMface, RefVar((Ref) GetFontFace(spec)));
				SetFrameSlot(style, RSSYMfamily, RefVar(GetFontFamilyNum(spec)));
			}
			else
				style = spec;
			TXOffsetRange range;
			fText->fHilite->GetHiliteRange(&range);
			ChangeRangeRuns(range, style, false, false);
			result = 1;
		}
		break;
	case 0xd3:
		{
			RefVar command(CommandFrameParameter(cmd));
			ExecuteCommand(command);
		}
		break;
	}
	CommandSetResult(cmd, result);
	if (result == 0)
		result = TView::RealDoCommand(cmd);
	return result != 0;
}


/*------------------------------------------------------------------------------
	T h e   p e n
------------------------------------------------------------------------------*/

// ROM 0x0024b6ac Click__6TXViewFP9TXNewtPenUl
// A click: the ruler's, else a drag of the selection.  A tap or a hilite
// click in the text: the hilite follows the pen - a tap on the hilite
// waits a double tap's interval first (Idle), since a second tap would
// select the word - and the view becomes the key view.
long
TXView::Click(TXNewtPen* pen, unsigned long command)
{
	if (command == aeClick)
	{
		long result = RulerClick(pen);
		if (result == 0)
			result = CheckDrag(pen);
		fTXFlags &= ~kTXViewTapPending;
		return result;
	}
	Point pt = pen->FirstLocation();
	if (!PtInRgn(pt, fText->fDisplay->fViewRgn))
		return 0;
	TXClickLoopProc proc = nil;
	TXNewtHilite* hilite = (TXNewtHilite*) fText->fHilite;
	if (command == aeTap)
	{
		if ((fTXFlags & kTXViewTapPending) == 0 && hilite->IsPointInHilite(pt))
		{
			fTXFlags |= kTXViewTapPending;
			fTapPt = pt;
			gRootView->AddIdler(this, gDoubleTapInterval << 4, 0);
			return 0;
		}
		hilite->fDoubleTap = false;
		FClicker(RefVar(NILREF));
	}
	else
	{
		BusyBoxSend(0x37);
		hilite->fDoubleTap = true;
		FPlaySound(RefVar(NILREF), RefVar(Rhilitesound));
		if ((fTXFlags & kTXViewHasScrollers) || fRulerUI != nil)
			proc = ::ClickLoop;
	}
	pen->InkOff();
	TXClickCommandInfo info;
	info.fCommand = 0;
	if (gRootView->fCaretView != this)
	{
		TXOffsetRange range;
		fText->fHilite->GetHiliteRange(&range);
		RefVar selection(Clone(RefVar(Rcanonicalparacaretinfo)));
		SetFrameSlot(selection, RSSYMoffset, RefVar(MAKEINT(range.fStart.fOffset)));
		SetFrameSlot(selection, RSSYMlength, RefVar(MAKEINT(range.fEnd.fOffset - range.fStart.fOffset)));
		gRootView->SetKeyViewSelection(this, selection, true);
	}
	fText->Click(pen, Modifiers(true), &info, proc, this);
	UpdateRuler(true);
	return 1;
}


// ROM 0x0024b650 ClickLoop__6TXViewFUcPv
// While the pen drags a selection along: the scrollers kept up.
void
TXView::ClickLoop(Boolean inLoop, void* scroll)
{
	if (inLoop != 1)
		return;
	if (((TXLongPoint*) scroll)->h != 0 && fRulerUI != nil)
	{
		// NOT YET RECONSTRUCTED: TXRulerUI::Scrolled
	}
	if (fTXFlags & kTXViewHasScrollers)
	{
		RefVar args(MakeArray(2));
		SetArraySlot(args, 0, RefVar(NILREF));
		SetArraySlot(args, 1, RefVar(TRUEREF));
		RunScript(RSSYMviewupdatescrollersscript, args);
	}
}


// ROM 0x0024b694 ClickLoop__FUcPvT2
// (the ROM has the method's body inlined here)
void
ClickLoop(unsigned char inLoop, void* scroll, void* view)
{
	((TXView*) view)->ClickLoop(inLoop, scroll);
}


// ROM 0x0024ba60 RulerClick__6TXViewFP9TXNewtPen
Boolean
TXView::RulerClick(TXNewtPen* /*pen*/)
{
	if (fRulerUI != nil)
	{
		// NOT YET RECONSTRUCTED: TXRulerUI::HitTest and Click - the
		// ruler's changes made a paragraph command (NewAttrCommand 3)
	}
	return false;
}


// ROM 0x002483ec CheckDrag__6TXViewFP9TXNewtPen
// NOT YET RECONSTRUCTED: the selection dragged out of the view
// (GetDragInfo, AddTextDragItem, DragAndDrop) - no drag is started.
Boolean
TXView::CheckDrag(TXNewtPen* /*pen*/)
{
	return false;
}


// ROM 0x00246a34 HandleCaretGesture__6TXViewFP11TUnitPublic
// NOT YET RECONSTRUCTED: a caret gesture opening space in the text - the
// gesture goes on to TView.
long
TXView::HandleCaretGesture(TUnitPublic* /*unit*/)
{
	return 0;
}


// ROM 0x002496c8 Scrub__6TXViewFP11TUnitPublic
// NOT YET RECONSTRUCTED: the scrub deleting the characters, words or
// lines under it (IsLinesScrub, IsCharOrWordsScrub) - the gesture goes on
// to TView.
long
TXView::Scrub(TUnitPublic* /*unit*/)
{
	return 0;
}


// ROM 0x0024bd0c Scroll__6TXViewFP11TXLongPoint
void
TXView::Scroll(TXLongPoint* d)
{
	gRootView->HideCaret();
	fText->fDisplay->Scroll(d);
	gRootView->ShowCaret();
	UpdateScrollers(false, true);
	if (d->h != 0 && fRulerUI != nil)
	{
		// NOT YET RECONSTRUCTED: TXRulerUI - the ruler's tab bar redrawn
		// when the text moved under it
	}
}


// ROM 0x0024bd74 GetScrollValues__6TXViewFP11TXLongPoint
void
TXView::GetScrollValues(TXLongPoint* scrolled)
{
	TXFrames* frames = fText->fDisplay->fFrames;
	scrolled->h = frames->fScrollH;
	scrolled->v = frames->fScrollV;
}


// ROM 0x0024bd80 SetHiliteRange__6TXViewFRC13TXOffsetRangeUcT2
// The selection set (shown); with `reveal`, scrolled into sight.
void
TXView::SetHiliteRange(const TXOffsetRange& range, Boolean reveal, Boolean scroll)
{
	fText->SetHiliteRange(range, true, scroll);
	if (reveal)
	{
		TXLongPoint d;
		if (!fText->fDisplay->IsHiliteVisible(&d, false))
			Scroll(&d);
	}
	// (the ROM has UpdateRuler(true)'s body inlined here)
	UpdateRuler(true);
}


// ROM 0x0024bdf4 GetHiliteRange__6TXViewFP13TXOffsetRange
void
TXView::GetHiliteRange(TXOffsetRange* range)
{
	*range = fText->fHilite->fRange;
}


// ROM 0x0024d4ec GetHiliteBounds__6TXViewFP4Rect
// The box of the selection's region inside the view.
void
TXView::GetHiliteBounds(Rect* bounds)
{
	RgnHandle rgn = fText->fHilite->GetHiliteRgn(false, false);
	SectRgn(rgn, fText->fDisplay->fViewRgn, rgn);
	*bounds = (**rgn).rgnBBox;
	DisposeRgn(rgn);
}


// ROM 0x0024bff4 PointToChar__6TXViewF5PointP13TXOffsetRange
Boolean
TXView::PointToChar(Point pt, TXOffsetRange* range)
{
	unsigned char outside, past;
	fText->fDisplay->PointToChar(pt, range, &outside, &past);
	return range->fStart.fOffset >= 0;
}


// ROM 0x0024c044 CharToPoint__6TXViewF8TXOffsetPi
Point
TXView::CharToPoint(TXOffsetPos offset, int* height)
{
	long h = 0;
	Point pt = fText->fDisplay->CharToPoint(offset.fOffset, offset.fAtStart, &h, nil);
	if (height != nil)
		*height = (int) h;
	return pt;
}


// ROM 0x0024c080 OffsetToCaret__6TXViewFlP5TRect
// Read from the assembly.  The caret at the start of the *selection* (the
// offset is not looked at): a line from where the character is down by
// the line's ascent.
void
TXView::OffsetToCaret(long /*offset*/, Rect* caret)
{
	TXOffsetRange range;
	fText->fHilite->GetHiliteRange(&range);
	Point pt = fText->fDisplay->CharToPoint(range.fStart.fOffset, range.fStart.fAtStart, nil, nil);
	long line = fText->CharToLine(range.fStart.fOffset, range.fStart.fAtStart, nil);
	TXLineHeightInfo info;
	fText->fFrameFormatter->GetLineHeightInfo(line, &info);
	caret->top = pt.v;
	caret->bottom = (short) (info.fAscent + pt.v);
	caret->left = pt.h;
	caret->right = caret->left;
}


// ROM 0x0024d37c GetWordRange__6TXViewF8TXOffsetP13TXOffsetRange
// The word at the offset - the next one, past any run of spaces or
// punctuation.
Boolean
TXView::GetWordRange(TXOffsetPos offset, TXOffsetRange* range)
{
	Boolean found = fText->CharToWord(offset.fOffset, offset.fAtStart, range, 1);
	while (found)
	{
		UniChar c = fText->fChars->GetChar(range->fStart.fOffset);
		if (!IsWhiteSpace(c) && !IsDelimiter(c))
			return true;
		offset.fOffset = (range->fEnd.fOffset - range->fStart.fOffset) + offset.fOffset;
		offset.fAtStart = false;
		found = fText->CharToWord(offset.fOffset, offset.fAtStart, range, 1);
	}
	return false;
}


// ROM 0x0024d448 GetLineRange__6TXViewF8TXOffsetP13TXOffsetRange
Boolean
TXView::GetLineRange(TXOffsetPos offset, TXOffsetRange* range)
{
	long line = fText->CharToLine(offset.fOffset, offset.fAtStart, nil);
	if (line >= 0)
	{
		fText->fFormatter->GetLineRange(line, range);
		return true;
	}
	return false;
}


// ROM 0x0024d48c GetParagraphRange__6TXViewF8TXOffsetP13TXOffsetRange
Boolean
TXView::GetParagraphRange(TXOffsetPos offset, TXOffsetRange* range)
{
	TXChars* chars = fText->fChars;
	long before = TXGetParagStartOffset(chars, offset.fOffset);
	long after = TXGetParagEndOffset(chars, offset.fOffset);
	range->Set(offset.fOffset - before, offset.fOffset + after, false, true);
	return true;
}


/*------------------------------------------------------------------------------
	T y p i n g   a n d   e d i t i n g
------------------------------------------------------------------------------*/

// ROM 0x002465ac KeyDown__6TXViewFUsUc
// A repeating key is only taken when it is one that may repeat.
void
TXView::KeyDown(UniChar key, Boolean repeat)
{
	unsigned int flags = fText->GetKeyDownFlags(key);
	if (!repeat || (flags & 8))
	{
		Boolean showEnd = key != 0x1e && key != 0x1c;
		NewKey(&key, 1, flags, Modifiers(true), showEnd);
	}
}


// ROM 0x002470d4 KeyString__6TXViewFPUsl
void
TXView::KeyString(UniChar* chars, long count)
{
	if (count == 0)
		return;
	NewKey(chars, count, 3, 0, true);
}


// ROM 0x00247448 NewKey__6TXViewFPCUsliT2Uc
// A key that edits (flags bit 2) goes into the key command on top of the
// undo stack when it follows on (TXKeyCommand::NewKey), or starts a new
// one; anything else - an arrow - is simply done.  (3 from NewKey: a new
// command; bit 1 of it: the undo cleared.)
void
TXView::NewKey(const UniChar* chars, long count, int keyFlags, long modifiers, Boolean showEnd)
{
	if (fReadOnly || (keyFlags & 1) == 0)
		return;
	Boolean edited;
	if ((keyFlags & 2) == 0)
	{
		fText->KeyDown(chars, count, Modifiers(true), keyFlags);
		edited = false;
	}
	else
	{
		edited = true;
		fTXFlags |= kTXViewModified;
		TXKeyCommand* current = GetCurrentKeyCommand();
		long result;
		if (current == nil)
			result = 2;
		else
			result = current->NewKey(chars, count, modifiers, keyFlags, nil);
		if (result != 0)
		{
			fKeyCommand = NILREF;
			if ((result & 2) == 0)
			{
				if (result & 1)
					gApplication->ClearUndo();
			}
			else
			{
				// DEVIATION: sized from sizeof on the host (the ROM's 0x74)
				RefVar object(AllocateFramesCObject(sizeof(TXKeyCommand), GCDeleteTXCommand, nil, nil));
				TXKeyCommand* command = new (BinaryData(object)) TXKeyCommand;
				unsigned char failed;
				NewtonErr err = command->ITXKeyCommand(fText, chars, count, keyFlags, &failed);
				if (failed)
					err = kError_No_Memory;
				if (err != noErr)
					Throw(exRootException, (void*) (long) err, nil);
				command->NewKey(chars, count, modifiers, keyFlags, nil);
				fKeyCommand = PostUndo(object);
			}
		}
	}
	if (fRulerUI != nil && (keyFlags & 0x18) != 0)
		UpdateRuler(true);
	Edited(true, showEnd, edited);
}


// ROM 0x00247388 GetCurrentKeyCommand__6TXViewFv
// The key command typing goes on adding to - while it is still the last
// thing on the undo stack.
TXKeyCommand*
TXView::GetCurrentKeyCommand(void)
{
	if (NOTNIL(fKeyCommand))
	{
		RefVar stack(gApplication->GetUndoStack(0));
		long n = Length(stack);
		if (n != 0)
		{
			RefVar last(GetArraySlotRef(stack, n - 1));
			if (EQRef(last, fKeyCommand))
				return (TXKeyCommand*) BinaryData(CommandFrameParameter(fKeyCommand));
		}
	}
	return nil;
}


// ROM 0x00247654 PostUndo__6TXViewFRC6RefVar
// The command object posted to the undo stack as a 0xd3 command to the
// view.  ==> the command frame.
Ref
TXView::PostUndo(RefArg command)
{
	fKeyCommand = NILREF;
	RefVar cmd(MakeCommand(0xd3, this, kNoParameter));
	CommandSetFrameParameter(cmd, command);
	gApplication->PostUndoCommand(cmd);
	return cmd;
}


// ROM 0x002476c0 ExecuteCommand__6TXViewFRC6RefVar
// The command done, undone or redone (by its state) with the screen held,
// and posted back for the next undo - or the undo cleared when it cannot
// be undone.
void
TXView::ExecuteCommand(RefArg command)
{
	TXCommand* cmd = (TXCommand*) BinaryData(command);
	StartDrawing(nil, nil);
	newton_try
	{
		int action;
		NewtonErr err = cmd->Execute(&action);
		if (err != noErr)
			Throw(exRootException, (void*) (long) err, nil);
		fTXFlags |= kTXViewModified;
		Edited(true, cmd->fKind == kTXKeyCommand || cmd->fKind == kTXReplaceCommand, true);
		UpdateRuler(true);
		if (!cmd->fCanUndo)
			gApplication->ClearUndo();
		else
			PostUndo(command);
	}
	newton_catch_all
	{
		StopDrawing(nil, nil);
		NextHandler(&_info);
	}
	end_try;
	StopDrawing(nil, nil);
}


// The three ways a command object is made and done.
template <class T>
static T*
NewCommandObject(RefVar* object)
{
	// DEVIATION: sized from sizeof on the host
	*object = AllocateFramesCObject(sizeof(T), GCDeleteTXCommand, nil, nil);
	return new (BinaryData(*object)) T;
}


// ROM 0x00246fdc NewAttrCommand__6TXViewFiRC13TXOffsetRangeP12TXAttrValuesl
// A restyle (kind 2) or a paragraph change (3) done undoably; the
// command deletes the values.  An undo container that could not be made
// throws here, where ITXEditCommand only noted it.
void
TXView::NewAttrCommand(int kind, const TXOffsetRange& range, TXAttrValues* values, long how)
{
	TXOffsetRange selection;
	fText->fHilite->GetHiliteRange(&selection);
	if (selection.fEnd.fOffset - selection.fStart.fOffset > 0x200)
		BusyBoxSend(0x33);
	RefVar object;
	TXEditCommand* command = NewCommandObject<TXEditCommand>(&object);
	unsigned char failed;
	NewtonErr err = command->ITXEditCommand(fText, kind, values, how, range, &failed);
	if (failed)
		err = kError_No_Memory;
	else if (err == noErr)
	{
		ExecuteCommand(object);
		return;
	}
	Throw(exRootException, (void*) (long) err, nil);
}


// ROM 0x00247108 NewReplaceTextCommand__6TXViewFRC13TXOffsetRangeP15TXReplaceParams
void
TXView::NewReplaceTextCommand(const TXOffsetRange& range, TXReplaceParams* params)
{
	RefVar object;
	TXReplaceTextCommand* command = NewCommandObject<TXReplaceTextCommand>(&object);
	unsigned char failed;
	NewtonErr err = command->ITXReplaceTextCommand(fText, range, params, &failed);
	if (failed)
		err = kError_No_Memory;
	else if (err == noErr)
	{
		ExecuteCommand(object);
		return;
	}
	Throw(exRootException, (void*) (long) err, nil);
}


// ROM 0x002472b8 NewMoveTextCommand__6TXViewFRC13TXOffsetRangeRC8TXOffsetUc
void
TXView::NewMoveTextCommand(const TXOffsetRange& range, const TXOffsetPos& to, Boolean copy)
{
	RefVar object;
	TXMoveTextCommand* command = NewCommandObject<TXMoveTextCommand>(&object);
	NewtonErr err = command->ITXMoveTextCommand(fText, range, to, copy);
	if (err != noErr)
		Throw(exRootException, (void*) (long) err, nil);
	ExecuteCommand(object);
}


// ROM 0x00246c60 ChangeRangeRuns__6TXViewFRC13TXOffsetRangeRC6RefVarUcT3
// The range restyled with the font spec.  `toggle`: a face that the
// whole range already has is taken off (8) rather than added (4).
void
TXView::ChangeRangeRuns(const TXOffsetRange& range, RefArg style, Boolean toggle, Boolean undoable)
{
	if (fReadOnly)
		return;
	TXAttrValues* values = TXGetRunAttrValues(style);
	long how = 0;
	if (toggle)
	{
		long face;
		Boolean has = values->GetValue(kTXAttrFace, &face);
		if (has && face != 0)
		{
			TXAttrValues common;
			fText->GetContinuousAttrValues(&common);
			long commonFace;
			Boolean found = common.GetValue(kTXAttrFace, &commonFace);
			if (found && (commonFace & face) != 0)
				how = 8;
			else
				how = 4;
		}
	}
	if (!undoable)
	{
		gApplication->ClearUndo();
		fText->UpdateRangeRuns(range, values, how);
		if (values != nil)
			delete values;
		fTXFlags |= kTXViewModified;
		Edited(true, false, true);
	}
	else
		NewAttrCommand(kTXRunsCommand, range, values, how);
}


// ROM 0x00246db8 ChangeRangeRulers__6TXViewFRC13TXOffsetRangeRC6RefVarUc
void
TXView::ChangeRangeRulers(const TXOffsetRange& range, RefArg ruler, Boolean undoable)
{
	if (fReadOnly)
		return;
	TXAttrValues* values = TXGetRulerAttrValues(ruler);
	if (!undoable)
	{
		gApplication->ClearUndo();
		fText->UpdateRangeRulers(range, values, 0);
		if (values != nil)
			delete values;
		fTXFlags |= kTXViewModified;
		Edited(true, false, true);
		// (the ROM has UpdateRuler(true)'s body inlined here)
		UpdateRuler(true);
	}
	else
		NewAttrCommand(kTXRulersCommand, range, values, 0);
}


// ROM 0x002477e0 Replace__6TXViewFRC13TXOffsetRangeRC6RefVarUcT3
// The range replaced by a frame of text (and styles, and - with `all` -
// rulers).
void
TXView::Replace(const TXOffsetRange& range, RefArg data, Boolean undoable, Boolean all)
{
	if (!IsFrame(data))
		Throw((ExceptionName) "evt.ex.fr.type", (void*) (long) kNSErrNotAFrame, nil);
	CheckReplaceData(data, range.fEnd.fOffset - range.fStart.fOffset);
	TXNewtContainer container(data);
	TXReplaceParams params(&container, all ? kTXImportAll : (kTXImportText | kTXImportRuns));
	if (!undoable)
	{
		gApplication->ClearUndo();
		NewtonErr err = fText->ReplaceRange(range.fStart.fOffset, range.fEnd.fOffset, &params);
		if (err != noErr)
			Throw(exRootException, (void*) (long) err, nil);
		fTXFlags |= kTXViewModified;
		Edited(true, true, true);
		UpdateRuler(true);
	}
	else
		NewReplaceTextCommand(range, &params);
}


// ROM 0x00247910 CheckReplaceData__6TXViewFRC6RefVarl
// A replacement's text a string and its styles an array of pairs whose
// lengths add up to it (to the range, when there is no text).
void
TXView::CheckReplaceData(RefArg data, long length)
{
	if (EQRef(RefVar(ClassOf(data)), RSSYMgraphics))
		return;
	RefVar text(GetFrameSlotRef(data, RSSYMtext));
	if (NOTNIL(text))
	{
		if (!IsString(text))
			ThrowBadTypeWithFrameData(kNSErrNotAString, text);
		length = ((unsigned long) Length(text) >> 1) - 1;
	}
	RefVar styles(GetFrameSlotRef(data, RSSYMstyles));
	if (NOTNIL(styles))
	{
		if (!IsArray(styles))
			ThrowBadTypeWithFrameData(kNSErrNotAnArray, styles);
		long n = Length(styles);
		if (n % 2 != 0)
			Throw(exRootException, (void*) (long) kTXViewErrBadData, nil);
		long total = 0;
		while ((n = n - 2) >= 0)
			total = RINT(GetArraySlotRef(styles, n)) + total;
		if (total != length)
			Throw(exRootException, (void*) (long) kTXViewErrBadData, nil);
	}
}


// ROM 0x00247ac0 ReplaceAll__6TXViewFPUslRC6RefVar
// Every occurrence from `start` on replaced by the frame, the lines
// formatted once at the end.  ==> how many.
// ROM BUG: when the last search finds nothing the length handed to
// Format is -1 less the first match's offset rather than "to the end";
// and the error the loop may stop on is never thrown (the variable it
// would be kept in is never set).
long
TXView::ReplaceAll(UniChar* find, long start, RefArg data)
{
	long length = Ustrlen(find);
	if (length == 0)
		return 0;
	BusyBoxSend(0x33);
	long count = 0;
	long step = -1;
	long first = -1;
	gApplication->ClearUndo();
	TXFormatter* formatter = fText->fFormatter;
	TXDisplay* display = fText->fDisplay;
	formatter->fSuspended--;
	display->DisableDrawing();
	TXNewtContainer container(data);
	TXReplaceParams params(&container, kTXImportAll);
	long at;
	while ((at = FindString(find, start)) >= 0)
	{
		if (first < 0)
			first = at;
		count++;
		if (fText->ReplaceRange(at, at + length, &params) != noErr)
			break;
		if (step < 0)
		{
			TXOffsetRange selection;
			fText->fHilite->GetHiliteRange(&selection);
			step = selection.fEnd.fOffset - at;
		}
		start = at + step;
	}
	formatter->fSuspended++;
	display->EnableDrawing();
	if (count != 0)
	{
		fText->Format(false, first, at - first);
		Edited(true, true, true);
		UpdateRuler(true);
		fTXFlags |= kTXViewModified;
	}
	return count;
}


// ROM 0x00247cd0 InsertPageBreak__6TXViewFRC13TXOffsetRange
// A line feed put in over the range.
void
TXView::InsertPageBreak(const TXOffsetRange& range)
{
	if (fReadOnly)
		return;
	UniChar pageBreak = 0x0a;
	TXTextDescriptor text;
	text.Set(&pageBreak, 1);
	TXReplaceParams params(text);
	NewReplaceTextCommand(range, &params);
}


// ROM 0x00248d20 Cut__6TXViewFv
void
TXView::Cut(void)
{
	Copy();
	if (fReadOnly)
		return;
	TXOffsetRange selection;
	fText->fHilite->GetHiliteRange(&selection);
	TXReplaceParams nothing;
	NewReplaceTextCommand(selection, &nothing);
}


// ROM 0x00249550 Copy__6TXViewFv
// NOT YET RECONSTRUCTED: the selection made a clipping (GetDragInfo and
// TClipboard::NewClipboard over GetHiliteBounds) - nothing is copied.
void
TXView::Copy(void)
{
}


// ROM 0x002495d4 Paste__6TXViewFv
// NOT YET RECONSTRUCTED: the clipping put in over the selection
// (TXNewtPasteCommand) - nothing is pasted.
Boolean
TXView::Paste(void)
{
	return false;
}


// ROM 0x00249678 Clear__6TXViewFv
void
TXView::Clear(void)
{
	if (fReadOnly)
		return;
	TXOffsetRange selection;
	fText->fHilite->GetHiliteRange(&selection);
	TXReplaceParams nothing;
	NewReplaceTextCommand(selection, &nothing);
}


// ROM 0x00247c6c DoEditCommand__6TXViewFl
Boolean
TXView::DoEditCommand(long command)
{
	switch (command)
	{
	case 0:
		Cut();
		break;
	case 1:
		Copy();
		break;
	case 2:
		Paste();
		gRootView->RemoveClipboard();
		break;
	case 3:
		Paste();
		break;
	case 4:
		Clear();
		break;
	}
	return true;
}


// ROM 0x0024d54c TXFindString__FP7TXCharsPUsl
// The string looked for 512 characters at a time, the last few of each
// piece kept in front of the next so that one across two is found.
long
TXFindString(TXChars* chars, UniChar* find, long start)
{
	long length = Ustrlen(find);
	if (length == 0)
		return -1;
	UniChar* buffer = (UniChar*) operator new(length * 2 + 0x400);
	if (buffer == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	long total = chars->Count();
	long kept = 0;
	long result = -1;
	if (start < total)
	{
		long overlap = length - 1;
		do
		{
			long n = total - start;
			if (n > 0x1ff)
				n = 0x200;
			TXTextDescriptor into;
			into.Set(buffer + kept, n);
			chars->CopyTo(&into, start, n);
			start = start + n;
			long have = n + kept;
			const UniChar* found = ::FindString(buffer, have, find);
			if (found != nil)
			{
				result = (start - have) + (found - buffer);
				break;
			}
			kept = overlap;
			if (have <= overlap)
				kept = have;
			BlockMove(buffer + (have - kept), buffer, kept << 1);
		} while (start < total);
	}
	operator delete(buffer);
	return result;
}


// ROM 0x0024d6ac FindString__6TXViewFPUsl
// (the ROM has TXFindString's body inlined here, over the document's
// characters)
long
TXView::FindString(UniChar* find, long start)
{
	return TXFindString(fText->fChars, find, start);
}


// ROM 0x00246e78 Edited__6TXViewFUcN21
void
TXView::Edited(Boolean show, Boolean end, Boolean scrollers)
{
	if (scrollers && (fTXFlags & kTXViewHasScrollers))
	{
		long height = fText->fDisplay->fFrames->GetTotalHeight();
		if (fTotalHeight == height)
			scrollers = false;
		else
			fTotalHeight = height;
	}
	if (show)
	{
		TXLongPoint d;
		if (!fText->fDisplay->IsHiliteVisible(&d, end))
			Scroll(&d);
	}
	if (!scrollers)
		return;
	if ((fTXFlags & kTXViewHasScrollers) == 0)
		return;
	RefVar args(MakeArray(2));
	SetArraySlot(args, 0, RefVar(TRUEREF));
	SetArraySlot(args, 1, RefVar(NILREF));
	RunScript(RSSYMviewupdatescrollersscript, args);
}


// ROM 0x00246f1c UpdateScrollers__6TXViewFUcT1
// viewUpdateScrollersScript(h, v): which of the two changed.
void
TXView::UpdateScrollers(Boolean h, Boolean v)
{
	if ((fTXFlags & kTXViewHasScrollers) == 0)
		return;
	RefVar args(MakeArray(2));
	SetArraySlot(args, 0, RefVar(h ? TRUEREF : NILREF));
	SetArraySlot(args, 1, RefVar(v ? TRUEREF : NILREF));
	RunScript(RSSYMviewupdatescrollersscript, args);
}


// ROM 0x0024bb9c GetContinuousRun__6TXViewFv
// The style the whole selection has as a font spec - a slot left out
// where it differs - or a picture's frame.
Ref
TXView::GetContinuousRun(void)
{
	RefVar result;
	TXRun* picture = fText->IsRangeGraphicsRun(nil);
	if (picture == nil)
	{
		result = Clone(RefVar(Rcanonicalfontspec));
		TXAttrValues values;
		fText->GetContinuousAttrValues(&values);
		TXNewtFontFamilyInfo* family;
		if (values.GetValue(kTXAttrFont, &family))
			SetFrameSlot(result, RSSYMfamily, family->fFamily);
		long size;
		if (values.GetValue(kTXAttrSize, &size))
			SetFrameSlot(result, RSSYMsize, RefVar(MAKEINT(size)));
		long face;
		if (values.GetValue(kTXAttrFace, &face))
			SetFrameSlot(result, RSSYMface, RefVar(MAKEINT(face)));
	}
	else
		result = picture->GetNSObject();
	return result;
}


/*------------------------------------------------------------------------------
	T h e   r u l e r   b a r
------------------------------------------------------------------------------*/

// ROM 0x0024c6d4 ShowRuler__6TXViewFRC6RefVar
// NOT YET RECONSTRUCTED: TXNewtRulerUI over gTXRulerPixMaps - no ruler
// is shown.
void
TXView::ShowRuler(RefArg /*info*/)
{
	if (fRulerUI != nil)
		return;
}


// ROM 0x0024c794 HideRuler__6TXViewFv
void
TXView::HideRuler(void)
{
	if (fRulerUI == nil)
		return;
	// NOT YET RECONSTRUCTED: TXRulerUI - the ruler deleted, the pixel
	// maps released, the view region taken back and redrawn
}


// ROM 0x0024c7ec UpdateRulerInfo__6TXViewFRC6RefVar
void
TXView::UpdateRulerInfo(RefArg /*info*/)
{
	if (fRulerUI == nil)
		return;
	// NOT YET RECONSTRUCTED: TXRulerTabsBar::SetRulerMeasure
}


// ROM 0x0024bb88 UpdateRuler__6TXViewFUc
void
TXView::UpdateRuler(Boolean /*redraw*/)
{
	if (fRulerUI == nil)
		return;
	// NOT YET RECONSTRUCTED: TXRulerUI - the ruler shown for the ruler at
	// the selection
}


/*------------------------------------------------------------------------------
	T h e   s e l e c t i o n   a s   a   v i e w   s e e s   i t
------------------------------------------------------------------------------*/

// ROM 0x0024c48c GetRangeText__6TXViewFlT1
Ref
TXView::GetRangeText(long start, long length)
{
	TXOffsetRange range(start, start + length, false, true);
	return GetRangeData(&range, RSSYMtext);
}


// ROM 0x0024c4d4 GetValue__6TXViewFRC6RefVarT1
// The hilites as the text they select, or as [view, start, end].
Ref
TXView::GetValue(RefArg slot, RefArg type)
{
	RefVar result;
	TXOffsetRange range;
	fText->fHilite->GetHiliteRange(&range);
	if (EQRef(slot, RSSYMhilites) && EQRef(type, RSSYMstring))
	{
		result = MakeArray(0);
		RefVar text(GetRangeData(&range, RSSYMtext));
		AddArraySlot(result, text);
	}
	else if (EQRef(slot, RSSYMhilites) && EQRef(type, RSSYMoffset))
	{
		if (range.fEnd.fOffset - range.fStart.fOffset > 0)
		{
			result = MakeArray(1);
			RefVar hilite(MakeArray(3));
			SetArraySlot(result, 0, hilite);
			SetArraySlot(hilite, 0, fContext);
			SetArraySlot(hilite, 1, RefVar(MAKEINT(range.fStart.fOffset)));
			SetArraySlot(hilite, 2, RefVar(MAKEINT(range.fEnd.fOffset)));
		}
	}
	else
		result = TView::GetValue(slot, type);
	return result;
}


// ROM 0x0024c1cc SetCaretOffset__6TXViewFPlT1
void
TXView::SetCaretOffset(long* offset, long* length)
{
	long count = CountChars();
	if (*offset > count)
		*offset = count;
	if (*offset + *length > count)
		*length = count - *offset;
}


// ROM 0x0024c20c SetSelection__6TXViewFRC6RefVarPlT2
void
TXView::SetSelection(RefArg selection, long* start, long* end)
{
	if (ISNIL(selection))
		return;
	*start = RINT(GetProtoVariable(selection, RSSYMoffset, nil));
	RefVar length(GetProtoVariable(selection, RSSYMlength, nil));
	*end = ISNIL(length) ? 0 : RINT(length);
	SetCaretOffset(start, end);
	TXOffsetRange range;
	range.Set(*start, *start + *end, false, true);
	SetHiliteRange(range, false, false);
}


// ROM 0x0024c304 GetSelection__6TXViewFv
Ref
TXView::GetSelection(void)
{
	TXOffsetRange range;
	fText->fHilite->GetHiliteRange(&range);
	RefVar selection(Clone(RefVar(Rcanonicalparacaretinfo)));
	SetFrameSlot(selection, RSSYMoffset, RefVar(MAKEINT(range.fStart.fOffset)));
	SetFrameSlot(selection, RSSYMlength, RefVar(MAKEINT(range.fEnd.fOffset - range.fStart.fOffset)));
	return selection;
}


// ROM 0x0024c3b0 ActivateSelection__6TXViewFUc
// Going inactive gives back the room the document is not using.
void
TXView::ActivateSelection(Boolean on)
{
	TView::ActivateSelection(on);
	fText->fDisplay->Activate(on, true);
	if (on)
		return;
	fText->fChars->Compact();
	fText->fFormatter->Compact();
	fText->fRuns->Compact();
	fText->fRulers->Compact();
}


/*------------------------------------------------------------------------------
	T h e   d o c u m e n t   a s   f r a m e s
------------------------------------------------------------------------------*/

// ROM 0x0024d07c GetRangeData__6TXViewFP13TXOffsetRangeRC6RefVar
// The range's text, styles or rulers - or, for anything else, the frame
// with all three.
Ref
TXView::GetRangeData(TXOffsetRange* range, RefArg what)
{
	RefVar frame(Clone(RefVar(Rtxlocalprototype)));
	TXNewtContainer container(frame);
	unsigned char types;
	if (EQRef(what, RSSYMtext))
		types = kTXImportText;
	else if (EQRef(what, RSSYMstyles))
		types = kTXImportRuns;
	else if (EQRef(what, RSSYMrulers))
		types = kTXImportRulers;
	else
		types = kTXImportAll;
	NewtonErr err = fText->Export(range, &container, types);
	if (err != noErr)
		Throw(exRootException, (void*) (long) err, nil);
	if (types == kTXImportAll)
		return frame;
	if (types == kTXImportText)
		return GetFrameSlotRef(frame, RSSYMtext);
	if (types == kTXImportRuns)
		return GetFrameSlotRef(frame, RSSYMstyles);
	return GetFrameSlotRef(frame, RSSYMrulers);
}


// ROM 0x0024c7fc Externalize__6TXViewFv
// The document as a frame: the characters (the string itself), the
// styles and rulers, and in `txData` a byte saying whether the line
// breaks follow (only for more than 60 lines, which are worth not working
// out again) and then the page width and the formatter's lines.
Ref
TXView::Externalize(void)
{
	TXChars* chars = fText->fChars;
	RefVar result;
	if (ISNIL(fStore))
	{
		result = Clone(RefVar(Rtxexternalprototype));
		SetFrameSlot(result, RSSYMtext, ((TXBinaryChars*) chars)->fString);
	}
	else
	{
		// NOT YET RECONSTRUCTED: TXVBOChars - txExternalVBOPrototype with
		// the characters' large binary in txText (a store is ignored on
		// the host: the text is still a string)
		result = Clone(RefVar(Rtxexternalprototype));
		SetFrameSlot(result, RSSYMtext, ((TXBinaryChars*) chars)->fString);
	}
	RefVar data(AllocateBinary(RSSYMbinary, 0));
	TXBinaryStream stream(data, false, 0x20, true);
	unsigned char formatted = fText->fFormatter->fLastLine + 1 > 0x3c;
	NewtonErr err = stream.WriteBytes(&formatted, 1);
	if (err != noErr)
		Throw(exRootException, (void*) (long) err, nil);
	if (formatted)
	{
		unsigned char width[2];
		width[0] = (unsigned char) (fPageWidth >> 8);
		width[1] = (unsigned char) fPageWidth;
		err = stream.WriteBytes(width, 2);
		if (err != noErr)
			Throw(exRootException, (void*) (long) err, nil);
		err = fText->fFormatter->WriteToStream(&stream);
		if (err != noErr)
			Throw(exRootException, (void*) (long) err, nil);
	}
	SetFrameSlot(result, RSSYMtxdata, data);
	TXOffsetRange all(0, chars->Count(), false, true);
	TXNewtContainer container(result);
	err = fText->Export(&all, &container, kTXImportRuns | kTXImportRulers);
	if (err != noErr)
		Throw(exRootException, (void*) (long) err, nil);
	fTXFlags &= ~kTXViewModified;
	return result;
}


// ROM 0x0024cad0 InternalizeChars__6TXViewFRC6RefVar
// The characters an Externalize frame holds made the document's - the
// string itself taken over.
TXChars*
TXView::InternalizeChars(RefArg data)
{
	TXChars* chars = fText->fChars;
	TXChars* newChars = chars;
	RefVar text(GetFrameSlotRef(data, RSSYMtxtext));
	if (ISNIL(text))
	{
		text = GetFrameSlotRef(data, RSSYMtext);
		if (ISNIL(text))
			Throw(exRootException, (void*) (long) kTXViewErrBadData, nil);
		if (ISNIL(fStore))
			((TXBinaryChars*) chars)->fString = text;
		else
		{
			fStore = NILREF;
			newChars = new TXBinaryChars(text);
		}
	}
	else
	{
		// NOT YET RECONSTRUCTED: TXVBOChars - the text's large binary
		// (FGetBinaryStore) made the document's storage
		Throw(exRootException, (void*) (long) kTXViewErrBadData, nil);
	}
	if (newChars == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	if (newChars != chars)
		fText->SetCharsHandler(newChars);
	return newChars;
}


// ROM 0x0024cc6c InternalizeFormattingData__6TXViewFP8TXStreamc
// The saved line breaks taken back - when there are some, and they were
// worked out for this page width.  ==> whether they were.
Boolean
TXView::InternalizeFormattingData(TXStream* stream, char flags)
{
	Boolean result = false;
	TXFormatter* formatter = fText->fFormatter;
	unsigned char width[2];
	if ((flags & 1) != 0 && stream->ReadBytes(width, 2) == noErr)
	{
		long saved = (short) ((width[0] << 8) | width[1]);
		if (fPageWidth == saved && (((fTXFlags & kTXViewPaginated) == 0) == ((flags & 2) == 0)))
		{
			if (formatter->ReadFromStream(stream) == noErr)
				result = true;
		}
		else
			fTXFlags |= kTXViewModified;
	}
	return result;
}


// ROM 0x0024cd18 Internalize__6TXViewFRC6RefVar
// An Externalize frame made the document: the characters taken over, the
// styles and rulers put in over them, the lines taken back or worked out
// afresh.  A failure leaves a new empty document.
void
TXView::Internalize(RefArg data)
{
	fTXFlags &= ~kTXViewModified;
	TXStream* stream = nil;
	long count = fText->fChars->Count();
	if (count != 0)
	{
		TXReplaceParams nothing;
		fText->ReplaceRange(0, count, &nothing);
	}
	Boolean failed = false;
	newton_try
	{
		TXChars* chars = InternalizeChars(data);
		RefVar txData(GetFrameSlotRef(data, RSSYMtxdata));
		char flags = 0;
		NewtonErr err = noErr;
		if (ISNIL(txData))
		{
			if (NOTNIL(fStore))
				Throw(exRootException, (void*) (long) kTXViewErrBadData, nil);
		}
		else
		{
			stream = new TXBinaryStream(txData, true, 0, true);
			if (stream == nil)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			err = stream->ReadBytes(&flags, 1);
			if (err == noErr && NOTNIL(fStore))
				err = ((TXChunkedChars*) chars)->ReadChunksRanges(stream);
		}
		if (err == noErr)
		{
			TXDisplay* display = fText->fDisplay;
			TXFormatter* formatter = fText->fFormatter;
			formatter->fSuspended--;
			display->DisableDrawing();
			TXNewtContainer container(data);
			TXReplaceParams params(&container, kTXImportRuns | kTXImportRulers);
			err = fText->ReplaceRange(0, chars->Count(), &params);
			formatter->fSuspended++;
			display->EnableDrawing();
			if (err == noErr)
			{
				display->InvalidDraw();
				if (!InternalizeFormattingData(stream, flags))
				{
					BusyBoxSend(0x33);
					fText->fFrameFormatter->CharRangeChanged(chars, 0, 0, chars->Count(), 4);
					err = formatter->Format(0, -1, nil, nil);
				}
			}
		}
		if (err != noErr)
			Throw(exRootException, (void*) (long) err, nil);
	}
	newton_catch_all
	{
		failed = true;
		if (stream != nil)
			delete stream;
		if (fText != nil)
			delete fText;
		CreateNewTextension();
		Dirty(nil);
		Edited(true, false, true);
		UpdateRuler(false);
		NextHandler(&_info);
	}
	end_try;
	if (stream != nil)
		delete stream;
	Dirty(nil);
	Edited(true, false, true);
	UpdateRuler(false);
}


/*------------------------------------------------------------------------------
	T X N e w t D i s p l a y
------------------------------------------------------------------------------*/

// ROM 0x0024d6b8 __ct__13TXNewtDisplayFP5TView
TXNewtDisplay::TXNewtDisplay(TView* view)
{
	fView = view;
}


// ROM 0x0024d708 BeginEdit__13TXNewtDisplayFP10TXEditInfo
void
TXNewtDisplay::BeginEdit(TXEditInfo* info)
{
	StartDrawing(nil, nil);
	gRootView->HideCaret();
	TXDisplay::BeginEdit(info);
}


// ROM 0x0024d748 EndEdit__13TXNewtDisplayFRC10TXEditInfolT2P8TXOffset
void
TXNewtDisplay::EndEdit(const TXEditInfo& info, long firstLine, long lastLine, TXOffsetPos* caret)
{
	TXDisplay::EndEdit(info, firstLine, lastLine, caret);
	gRootView->ShowCaret();
	StopDrawing(nil, nil);
}


// ROM 0x0024d800 (unnamed)
// Whether the port is recording a picture.
static Handle
PortPicSave(void)
{
	GrafPtr port;
	GetPort(&port);
	return port->picSave;
}


// ROM 0x0024d824 Focus__13TXNewtDisplayFPPP6RegionP5Point
// The port's visible region narrowed to the view's (the old one kept) and
// the clip set to the text's region - unless a picture is being recorded.
void
TXNewtDisplay::Focus(RgnHandle* savedClip, Point* /*savedOrigin*/)
{
	if (PortPicSave() != nil)
	{
		*savedClip = nil;
		return;
	}
	TRegion saved(fView->SetupVisRgn());
	fSavedVis = saved;
	*savedClip = (RgnHandle) gTXTempRegions->Get();
	GetClip(*savedClip);
	SetClip(fViewRgn);
}


// ROM 0x0024d898 UnFocus__13TXNewtDisplayFPP6Region5Point
void
TXNewtDisplay::UnFocus(RgnHandle savedClip, Point /*savedOrigin*/)
{
	if (PortPicSave() != nil)
		return;
	SetClip(savedClip);
	gTXTempRegions->Done(savedClip);
	GrafPtr port;
	GetPort(&port);
	CopyRgn(fSavedVis, port->visRgn);
}


/*------------------------------------------------------------------------------
	T X N e w t H i l i t e
------------------------------------------------------------------------------*/

// ROM 0x0024d9f4 __ct__12TXNewtHiliteFP5TView
TXNewtHilite::TXNewtHilite(TView* view)
{
	fView = view;
	fDoubleTap = false;
}


// ROM 0x0024da44 SetHiliteRange__12TXNewtHiliteFRC13TXOffsetRangeUcT2
// With `scroll` and the view the key view: the key view's selection
// follows (and a selection of something makes it the hilited view).
Boolean
TXNewtHilite::SetHiliteRange(const TXOffsetRange& range, Boolean show, Boolean scroll)
{
	gRootView->HideCaret();
	if (scroll && gRootView->fCaretView == fView)
	{
		long length = range.fEnd.fOffset - range.fStart.fOffset;
		if (length > 0)
			gRootView->SetHilitedView(fView);
		gRootView->SetKeyView(fView, range.fStart.fOffset, length, false);
	}
	Boolean changed = TXHilite::SetHiliteRange(range, show, true);
	gRootView->ShowCaret();
	return changed;
}


// ROM 0x0024dae8 CalcCountClicks__12TXNewtHiliteF5PointlT2
long
TXNewtHilite::CalcCountClicks(Point /*pt*/, long /*now*/, long /*doubleClickTime*/)
{
	return fDoubleTap ? 2 : 1;
}


/*------------------------------------------------------------------------------
	T X N e w t P e n
------------------------------------------------------------------------------*/

// ROM 0x0024dbb0 __ct__9TXNewtPenFP13TStrokePublic
TXNewtPen::TXNewtPen(TStrokePublic* stroke)
{
	fStroke = stroke;
}


// ROM 0x0024dbf4 __ct__9TXNewtPenF5Point
TXNewtPen::TXNewtPen(Point pt)
{
	fStroke = nil;
	fPoint = pt;
}


// ROM 0x0024dc48 IsStillDown__9TXNewtPenFv
Boolean
TXNewtPen::IsStillDown(void)
{
	return fStroke != nil && !fStroke->Done();
}


// ROM 0x0024dc7c FirstLocation__9TXNewtPenFv
Point
TXNewtPen::FirstLocation(void)
{
	if (fStroke != nil)
		return fStroke->FirstPoint();
	return fPoint;
}


// ROM 0x0024dcbc CurrentLocation__9TXNewtPenFv
Point
TXNewtPen::CurrentLocation(void)
{
	if (fStroke != nil)
		return fStroke->FinalPoint();
	return fPoint;
}


// ROM 0x0024dcfc GetDoubleClickTime__9TXNewtPenFv
long
TXNewtPen::GetDoubleClickTime(void)
{
	return 0;
}


// ROM 0x0024dd04 InkOff__9TXNewtPenFv
// (the ROM has TStrokePublic::InkOff(true)'s body inlined here)
void
TXNewtPen::InkOff(void)
{
	if (fStroke == nil)
		return;
	fStroke->InkOff(true);
}


/*------------------------------------------------------------------------------
	T h e   o b j e c t s   a   s c r i p t   s e e s
------------------------------------------------------------------------------*/

// ROM 0x002465a4 GCDeleteTXCommand__FPv
// A command's C object collected: the command destroyed in place.
void
GCDeleteTXCommand(void* command)
{
	((TXCommand*) command)->~TXCommand();
}


// ROM 0x00249d38 GCDeleteTXChars__FPv
void
GCDeleteTXChars(void* chars)
{
	((TXChars*) chars)->~TXChars();
}


// ROM 0x00249968 ToObject__FRC13TXOffsetRange
Ref
ToObject(const TXOffsetRange& range)
{
	RefVar obj(Clone(RefVar(Rtxrangeprototype)));
	SetFrameSlot(obj, RSSYMfirst, RefVar(MAKEINT(range.fStart.fOffset)));
	SetFrameSlot(obj, RSSYMlast, RefVar(MAKEINT(range.fEnd.fOffset)));
	return obj;
}


// ROM 0x002499fc FromObject__FRC6RefVarP13TXOffsetRangeP6TXView
// Read from the assembly.
void
FromObject(RefArg obj, TXOffsetRange* range, TXView* view)
{
	Boolean trailingFirst = false;
	Boolean trailingLast = true;
	if (FrameHasSlot(obj, RSSYMtrailingfirst))
		trailingFirst = GetFrameSlotRef(obj, RSSYMtrailingfirst) != NILREF;
	if (FrameHasSlot(obj, RSSYMtrailinglast))
		trailingLast = GetFrameSlotRef(obj, RSSYMtrailinglast) != NILREF;
	long last = RINT(GetFrameSlotRef(obj, RSSYMlast));
	long first = RINT(GetFrameSlotRef(obj, RSSYMfirst));
	range->Set(first, last, trailingFirst, trailingLast);
	if (view == nil)
		return;
	if (range->fStart.fOffset >= 0 && range->fEnd.fOffset >= 0
	 && range->fEnd.fOffset <= view->CountChars() && range->fStart.fOffset <= range->fEnd.fOffset)
		return;
	Throw(exRootException, (void*) (long) kTXViewErrBadRange, nil);
}


// ROM 0x0024a35c FailGetTXView__FRC6RefVar
TXView*
FailGetTXView(RefArg context)
{
	TView* view = FailGetView(context);
	if (!view->DerivedFrom(clTXView))
		ThrowMsg("not a TX view");
	return (TXView*) view;
}
