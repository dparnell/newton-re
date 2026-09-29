/*
	File:		text/TXDisplay.cpp

	Contains:	The display (TXDisplay.h).

	Reconstructed from the MP2x00 US ROM (0x00235b00-0x00237540); each
	function cites its origin.
*/

#include "TXDisplay.h"
#include "TXHilite.h"
#include "TXFormatter.h"
#include "TXStyledText.h"
#include "TXUtilities.h"
#include "Rects.h"
#include "Regions.h"
#include "Draw.h"


TXTempReferences*	gTXTempLines = nil;
unsigned long		TXDisplay::fLastEditAction = 0;
long				TXDisplay::fDrawVisLevel = 0;


// A TXSectFrames the frames have yet to fill in: GetNextFrame answers
// nothing if they do not.
static void
ClearSectFrames(TXSectFrames* frames)
{
	frames->fCurrent = -2;
	frames->fListCount = 0;
}


// ROM 0x00235b00 CreateNewReference__11TXTempLinesFv
void*
TXTempLines::CreateNewReference(void)
{
	return new TXArray(sizeof(TXSectLine), 2);
}


// ROM 0x00235b10 FreeReference__11TXTempLinesFPv
void
TXTempLines::FreeReference(void* ref)
{
	if (ref == nil)
		return;
	delete (TXArray*) ref;
}


// ROM 0x002365c4 Start__9TXDisplaySFv
void
TXDisplay::Start(void)
{
	gTXTempLines = new TXTempLines;
}


// ROM 0x00237390 __ct__9TXDisplayFv
TXDisplay::TXDisplay()
	: fHilite(nil), fFrames(nil), fText(nil), fFormatter(nil), fFrameFormatter(nil), fLine(nil)
{
	fDrawLevel = 0;
	fViewRgn = NewRgn();
	SetRectRgn(fViewRgn, 0, 0, 0, 0);
}


// ROM 0x002373f0 __dt__9TXDisplayFv
TXDisplay::~TXDisplay()
{
	if (fFrames != nil)
		delete fFrames;
	if (fLine != nil)
		delete fLine;
	DisposeRgn(fViewRgn);
}


// ROM 0x0023745c SetHandlers__9TXDisplayFP17TXDisplayHandlers
void
TXDisplay::SetHandlers(TXDisplayHandlers* handlers)
{
	fHilite = handlers->fHilite;
	fText = handlers->fText;
	fFormatter = handlers->fFormatter;
	if (handlers->fFrames == nil)
		handlers->fFrames = new TXMonoFrame;
	fFrames = handlers->fFrames;
	fFrameFormatter = handlers->fFrames->fFormatter;
	fLine = new TXLine(handlers->fText, handlers->fRulers);
}


// ROM 0x002374cc FreeData__9TXDisplayFv
void
TXDisplay::FreeData(void)
{
	InvalidDraw();
	fFrames->FreeData();
}


// ROM 0x002374f0 DisableDrawing__9TXDisplayFv
void
TXDisplay::DisableDrawing(void)
{
	fDrawVisLevel = fDrawVisLevel - 1;
	fHilite->fVisible = fHilite->fVisible - 1;
}


// ROM 0x00237518 EnableDrawing__9TXDisplayFv
void
TXDisplay::EnableDrawing(void)
{
	fDrawVisLevel = fDrawVisLevel + 1;
	fHilite->fVisible = fHilite->fVisible + 1;
}


// ROM 0x002365b4 SetViewRgn__9TXDisplayFPP6Region
void
TXDisplay::SetViewRgn(RgnHandle rgn)
{
	CopyRgn(rgn, fViewRgn);
}


// ROM 0x00236604 Focus__9TXDisplayFPPP6RegionP5Point
// The port's clip and origin saved, the origin put at nought and the clip
// made the view region.
void
TXDisplay::Focus(RgnHandle* savedClip, Point* savedOrigin)
{
	*savedClip = (RgnHandle) gTXTempRegions->Get();
	GetClip(*savedClip);
	GrafPtr port;
	GetPort(&port);
	savedOrigin->h = port->portRect.left;
	savedOrigin->v = port->portRect.top;
	SetOrigin(0, 0);
	SetClip(fViewRgn);
}


// ROM 0x00236688 UnFocus__9TXDisplayFPP6Region5Point
void
TXDisplay::UnFocus(RgnHandle savedClip, Point savedOrigin)
{
	SetClip(savedClip);
	gTXTempRegions->Done(savedClip);
	SetOrigin(savedOrigin.h, savedOrigin.v);
}


// ROM 0x002366cc SetDrawEnv__9TXDisplayFP9TXDrawEnv
// Nested: only the outermost call sets the port up.
void
TXDisplay::SetDrawEnv(TXDrawEnv* env)
{
	if (fDrawLevel++ != 0)
		return;
	GetPort(&env->fPort);
	SetPort(fText->GetTextPort());
	Focus(&env->fClip, &env->fOrigin);
}


// ROM 0x00236720 RestoreDrawEnv__9TXDisplayFRC9TXDrawEnv
void
TXDisplay::RestoreDrawEnv(const TXDrawEnv& env)
{
	if (--fDrawLevel != 0)
		return;
	UnFocus(env.fClip, env.fOrigin);
	SetPort(env.fPort);
}


// ROM 0x00236760 InvalidDraw__9TXDisplayFv
void
TXDisplay::InvalidDraw(void)
{
	fHilite->Invalid(false);
	fLine->fStart = -1;
}


// ROM 0x0023678c GetViewFrames__9TXDisplayCFP12TXSectFrames
void
TXDisplay::GetViewFrames(TXSectFrames* frames) const
{
	Rect view = (*fViewRgn)->rgnBBox;
	fFrames->SectFrames(view, frames);
}


// ROM 0x00236ff0 DoLineLayout__9TXDisplayFl
void
TXDisplay::DoLineLayout(long line)
{
	TXOffsetRange range;
	fFormatter->GetLineRange(line, &range);
	long width = fFrames->GetLineMaxWidth(line);
	fLine->DoLineLayout(range.fStart.fOffset, range.fEnd.fOffset - range.fStart.fOffset, width);
}


// ROM 0x002367d0 DrawLineGroup__9TXDisplayFRC11TXSectLinesPP6Region
// Each line of the band laid out and drawn in its rectangle - the ones a
// clip region that is not a rectangle actually shows.
void
TXDisplay::DrawLineGroup(const TXSectLine& band, RgnHandle clip)
{
	if (band.fCount == 0)
		return;
	Rect r = band.fRect;
	long end = band.fCount + band.fLine;
	long height = r.bottom - r.top;
	r.top = r.top - height;
	r.bottom = r.bottom - height;
	for (long line = band.fLine; line < end; line++)
	{
		r.top = r.top + height;
		r.bottom = r.bottom + height;
		if (clip == nil || RectInRgn(&r, clip))
		{
			DoLineLayout(line);
			fLine->Draw(r, band.fAscent);
		}
	}
}


// ROM 0x002368c4 DrawFrameText__9TXDisplayFlPC4Rect
// The frame's text inside `r` (nil: all of it) erased and drawn again.
Boolean
TXDisplay::DrawFrameText(long frame, const Rect* r)
{
	Rect bounds;
	fFrames->GetTextBounds(frame, &bounds);
	if (r != nil && !SectRect(r, &bounds, &bounds))
		return false;
	if (!TXCalcClipRect(&bounds))
		return false;
	TXArray* bands = (TXArray*) gTXTempLines->Get();
	long first, count;
	if (!fFrames->SectLines(&bounds, frame, &first, &count, bands))
	{
		gTXTempLines->Done(bands);
		return false;
	}
	RgnHandle saved = (RgnHandle) gTXTempRegions->Get();
	Boolean shows = TXClipFurther(&bounds, saved);
	EraseRect(&bounds);
	if (shows)
	{
		long n = bands->GetCount();
		GrafPtr port;
		GetPort(&port);
		RgnHandle clip = port->clipRgn;
		if ((*clip)->rgnSize == 0xc)
			clip = nil;
		TXSectLine* band = (TXSectLine*) bands->Lock(false);
		while (--n >= 0)
		{
			DrawLineGroup(*band, clip);
			band++;
		}
		bands->Unlock();
		SetClip(saved);
	}
	gTXTempRegions->Done(saved);
	gTXTempLines->Done(bands);
	return shows;
}


// ROM 0x00235b20 Draw__9TXDisplayFRC4Rect
void
TXDisplay::Draw(const Rect& r)
{
	if (fDrawVisLevel < 0)
		return;
	TXDrawEnv env;
	SetDrawEnv(&env);
	TXSectFrames frames;
	ClearSectFrames(&frames);
	fFrames->SectFrames(r, &frames);
	for (long frame = frames.GetNextFrame(); frame >= 0; frame = frames.GetNextFrame())
	{
		DrawFrameText(frame, &r);
		fFrames->Draw(frame);
	}
	fHilite->Draw();
	RestoreDrawEnv(env);
}


// ROM 0x00236a5c ScrollRect__9TXDisplayFRC4RectlT2PP6RegionUc
Boolean
TXDisplay::ScrollRect(const Rect& r, long dh, long dv, RgnHandle update, Boolean extend)
{
	return TXScrollRect(r, dh, dv, update, extend);
}


// ROM 0x00235be8 AdjustScrollValues__9TXDisplayFP11TXLongPoint
// A scroll towards the top (positive) no further than the view has
// scrolled down; towards the bottom no further than the text below the
// view.
void
TXDisplay::AdjustScrollValues(TXLongPoint* d)
{
	Rect view = (*fViewRgn)->rgnBBox;
	long scrolled = fFrames->fScrollV;
	if (d->v > 0)
	{
		if (d->v > scrolled)
			d->v = scrolled;
	}
	else
	{
		long below = (fFrames->GetTotalHeight() - (view.bottom - view.top)) - scrolled;
		if (below <= 0)
			d->v = 0;
		else
		{
			if (-d->v < below)
				below = -d->v;
			d->v = -below;
		}
	}
	scrolled = fFrames->fScrollH;
	if (d->h > 0)
	{
		if (d->h > scrolled)
			d->h = scrolled;
	}
	else
	{
		long beyond = (fFrames->GetTotalWidth() - (view.right - view.left)) - scrolled;
		if (beyond <= 0)
			d->h = 0;
		else
		{
			if (-d->h < beyond)
				beyond = -d->h;
			d->h = -beyond;
		}
	}
}


// ROM 0x00235cf4 Scroll__9TXDisplayFP11TXLongPoint
// The view's bits moved and what was uncovered drawn.
void
TXDisplay::Scroll(TXLongPoint* d)
{
	AdjustScrollValues(d);
	if (d->h == 0 && d->v == 0)
		return;
	fFrames->FramesScrolled(d->h, d->v);
	if (fDrawVisLevel < 0)
		return;
	TXDrawEnv env;
	SetDrawEnv(&env);
	RgnHandle update = (RgnHandle) gTXTempRegions->Get();
	Rect view = (*fViewRgn)->rgnBBox;
	ScrollRect(view, d->h, d->v, update, false);
	RgnHandle saved = (RgnHandle) gTXTempRegions->Get();
	GetClip(saved);
	SetClip(update);
	gTXTempRegions->Done(update);
	Draw(view);
	SetClip(saved);
	gTXTempRegions->Done(saved);
	RestoreDrawEnv(env);
}


// ROM 0x00235df4 CheckScroll__9TXDisplayFUc
Boolean
TXDisplay::CheckScroll(Boolean draw)
{
	TXLongPoint scrolled;
	GetScrolledValues(&scrolled);
	if (scrolled.h == 0 && scrolled.v == 0)
		return false;
	Rect view = (*fViewRgn)->rgnBBox;
	if (scrolled.v != 0)
	{
		scrolled.v = ((view.bottom - view.top) - fFrames->GetTotalHeight()) + scrolled.v;
		if (scrolled.v < 0)
			scrolled.v = 0;
	}
	if (scrolled.h != 0)
	{
		scrolled.h = ((view.right - view.left) - fFrames->GetTotalWidth()) + scrolled.h;
		if (scrolled.h < 0)
			scrolled.h = 0;
	}
	if (scrolled.v <= 0 && scrolled.h <= 0)
		return false;
	if (draw)
		Scroll(&scrolled);
	else
		fFrames->FramesScrolled(scrolled.h, scrolled.v);
	return true;
}


// ROM 0x00235f1c GetScrolledValues__9TXDisplayFP11TXLongPoint
void
TXDisplay::GetScrolledValues(TXLongPoint* scrolled)
{
	scrolled->h = fFrames->fScrollH;
	scrolled->v = fFrames->fScrollV;
}


// ROM 0x00235f38 Activate__9TXDisplayFUcT1
void
TXDisplay::Activate(Boolean active, Boolean show)
{
	fHilite->Activate(active, show);
}


// ROM 0x00235f48 IsHiliteVisible__9TXDisplayFP11TXLongPointUc
// The baseline of the hilite's start (or end) against the view: `d` the
// scroll that would bring it to the middle, nought each way it shows.
Boolean
TXDisplay::IsHiliteVisible(TXLongPoint* d, Boolean end)
{
	Rect view = (*fViewRgn)->rgnBBox;
	TXOffsetRange range;
	fHilite->GetHiliteRange(&range);
	TXOffsetPos at = end ? range.fEnd : range.fStart;
	TXLongPoint pt;
	long height, ascent;
	CharToPoint(at.fOffset, at.fAtStart, &pt, &height, &ascent);
	pt.v = pt.v + ascent;
	TXLongRect abs;
	fFrames->DrawToAbs(view, &abs);
	if (pt.v >= abs.top && abs.bottom >= pt.v)
		d->v = 0;
	else
		d->v = (abs.bottom - pt.v) - (view.bottom - view.top) / 2;
	if (pt.h < abs.left || pt.h > abs.right)
		d->h = (abs.right - pt.h) - (view.right - view.left) / 2;
	else
		d->h = 0;
	return d->v == 0 && d->h == 0;
}


// ROM 0x00236094 BeginEdit__9TXDisplayFP10TXEditInfo
// The hilite taken away and each frame of the view caught, so EndEdit
// knows how their text's height changed.
void
TXDisplay::BeginEdit(TXEditInfo* info)
{
	info->fDoEdit = true;
	if (fDrawVisLevel < 0)
	{
		info->fEnv.fClip = (RgnHandle) -1;
		return;
	}
	SetDrawEnv(&info->fEnv);
	fLastEditAction = 0;
	info->fOldHiliteState = fHilite->fState;
	fHilite->SetHiliteState(kTXHiliteOff);
	InvalidDraw();
	info->fOldCountFrames = fFrameFormatter->GetCountFrames();
	fFrameFormatter->BeginEdit();
	TXSectFrames frames;
	ClearSectFrames(&frames);
	GetViewFrames(&frames);
	for (long frame = frames.GetNextFrame(); frame >= 0; frame = frames.GetNextFrame())
		fFrameFormatter->CatchFrame(frame);
}


// ROM 0x00236180 EndEdit__9TXDisplayFRC10TXEditInfolT2P8TXOffset
// Each frame the edit touched redrawn as little as it can be: the lines
// [firstLine, lastLine] that changed, the rest moved by a blit when the
// text's height changed, the frame's bottom erased when it got shorter;
// then the caret put at `caret` and the hilite shown again.
void
TXDisplay::EndEdit(const TXEditInfo& info, long firstLine, long lastLine, TXOffsetPos* caret)
{
	fLastEditAction |= 0x50;
	if (caret != nil)
	{
		fHilite->AdjustCharOffset(caret);
		TXOffsetRange range(*caret, *caret);
		fHilite->SetHiliteRange(range, false, true);
	}
	if (info.fEnv.fClip == (RgnHandle) -1)
		return;
	if (!info.fDoEdit || fDrawVisLevel < 0)
	{
		fHilite->SetHiliteState(info.fOldHiliteState);
		RestoreDrawEnv(info.fEnv);
		fFrameFormatter->EndEdit();
		return;
	}
	long countFrames = fFrameFormatter->GetCountFrames();
	Boolean changed = (firstLine != lastLine);
	for (TXFrameEditInfo* e = fFrameFormatter->GetNextFrameEditInfo(); e != nil; e = fFrameFormatter->GetNextFrameEditInfo())
	{
		long frame = e->fFrame;
		if (frame >= countFrames)
			e->fFlags |= 1;
		if ((e->fFlags & 0xff) != 0)
		{
			fFrames->InvalFramePart(frame, e->fFlags, e->fField08, nil);
			fLastEditAction |= 0x20;
			if (e->fFlags & 1)
				continue;
		}
		if (e->fFlags & 0x100)
		{
			DrawFrameText(frame, nil);
			continue;
		}
		if (firstLine < 0)
		{
			if (e->fField10 != 0)
				ScrollFrame(*e);
			else if (e->fField14 != 0)
				UpdateOverflowLines(*e);
			continue;
		}
		TXOffsetPair lines;
		if (!fFrameFormatter->GetFrameLineRange(frame, &lines))
			continue;
		if (firstLine <= lines.fStart && lines.fEnd <= lastLine && changed)
		{
			DrawFrameText(frame, nil);
			continue;
		}
		if (lines.fStart > lastLine || lines.fEnd < firstLine)
		{
			// the frame is past the lines that changed
			if (e->fField10 == 0 && e->fField14 == 0 && e->fHeightChange == 0)
				continue;
			if (lines.fEnd >= firstLine)
				ScrollFrame(*e);
			else
				EraseFrameBottom(frame);
			continue;
		}
		long from = (firstLine > lines.fStart) ? firstLine : lines.fStart;
		long to = (lastLine < lines.fEnd) ? lastLine : lines.fEnd;
		long above = (from > lines.fStart) ? fFrameFormatter->GetLinesHeight(lines.fStart, from - 1) : 0;
		if (lines.fEnd > lastLine || !changed)
		{
			FrameEndEdit(*e, above, fFrameFormatter->GetLinesHeight(from, to));
			if (e->fField14 != 0)
				UpdateOverflowLines(*e);
		}
		else
			FrameEndEdit(*e, above, -1);
	}
	if (info.fOldCountFrames < countFrames)
	{
		fLastEditAction |= 0x20;
		TXSectFrames frames;
		ClearSectFrames(&frames);
		GetViewFrames(&frames);
		for (long frame = frames.GetNextFrame(); frame >= 0; frame = frames.GetNextFrame())
			if (info.fOldCountFrames <= frame)
				fFrames->InvalFramePart(frame, 1, 0, nil);
	}
	CheckScroll(true);
	fHilite->SetHiliteState(info.fOldHiliteState);
	RestoreDrawEnv(info.fEnv);
	fFrameFormatter->EndEdit();
}


// ROM 0x00236a98 UpdateScrolledArea__9TXDisplayFPP6RegionRC15TXFrameEditInfo
// What a scroll uncovered drawn - clipped to the region unless it is a
// rectangle (or was made one, when lines went off the frame's end).
void
TXDisplay::UpdateScrolledArea(RgnHandle update, const TXFrameEditInfo& info)
{
	long frame = info.fFrame;
	Rect r = (*update)->rgnBBox;
	Boolean rectangular = (*update)->rgnSize == 0xc;
	if (info.fField14 < 0)
	{
		Rect text;
		fFrames->GetTextBounds(frame, &text);
		long end = fFrameFormatter->GetFrameTextHeight(frame) + text.top + info.fField14;
		if (end < r.top)
		{
			r.top = end;
			rectangular = true;
		}
	}
	RgnHandle saved = nil;
	if (!rectangular)
	{
		saved = (RgnHandle) gTXTempRegions->Get();
		GetClip(saved);
		SetClip(update);
	}
	DrawFrameText(frame, &r);
	if (!rectangular)
	{
		SetClip(saved);
		gTXTempRegions->Done(saved);
	}
}


// ROM 0x00236ba4 FrameEndEdit__9TXDisplayFRC15TXFrameEditInfolT2
// Lines `height` tall, `above` pixels down the frame's text, redrawn; with
// a height given the text after them is first blitted by the height
// change - unless that is more than the frame, when all of it below
// `above` is redrawn (height -1).  Read from the assembly.
void
TXDisplay::FrameEndEdit(const TXFrameEditInfo& info, long above, long height)
{
	long frame = info.fFrame;
	TXLongRect text;
	fFrames->GetAbsTextBounds(frame, &text);
	long top = text.top;
	text.top = top + above;
	long shift;
	if (height < 0)
	{
		height = text.bottom - text.top;
		shift = 0;
	}
	else
	{
		shift = info.fHeightChange + info.fField10;
		long magnitude = (shift >= 0) ? shift : -shift;
		if (magnitude >= text.bottom - top)
		{
			FrameEndEdit(info, above, -1);
			return;
		}
	}
	Boolean haveMoved = false;
	Rect moved;
	if (shift != 0)
	{
		TXLongRect rest = text;
		rest.top = (height - shift) + rest.top;
		if (text.bottom <= rest.top)
		{
			FrameEndEdit(info, above, -1);
			return;
		}
		if (rest.top < text.top)
			rest.top = text.top;
		Rect r;
		fFrames->AbsToDraw(rest, &r);
		RgnHandle update = (RgnHandle) gTXTempRegions->Get();
		if (ScrollRect(r, 0, shift, update, shift < 0))
		{
			if (shift < 0 || (*update)->rgnSize != 0xc)
				UpdateScrolledArea(update, info);
			else
			{
				moved = (*update)->rgnBBox;
				haveMoved = true;
			}
		}
		gTXTempRegions->Done(update);
	}
	text.bottom = text.top + height;
	Rect r;
	fFrames->AbsToDraw(text, &r);
	if (haveMoved)
	{
		if (moved.bottom > r.bottom)
			r.bottom = moved.bottom;
		if (moved.top < r.top)
			r.top = moved.top;
	}
	if (!DrawFrameText(frame, &r) && haveMoved)
		DrawFrameText(frame, &moved);
}


// ROM 0x00236e10 EraseFrameBottom__9TXDisplayFl
// The frame below its text erased (drawn with nothing in it).
void
TXDisplay::EraseFrameBottom(long frame)
{
	TXLongRect text;
	fFrames->GetAbsTextBounds(frame, &text);
	text.top = fFrameFormatter->GetFrameTextHeight(frame) + text.top;
	if (text.bottom != text.top)
	{
		Rect r;
		fFrames->AbsToDraw(text, &r);
		DrawFrameText(frame, &r);
	}
}


// ROM 0x00236e9c ScrollFrame__9TXDisplayFRC15TXFrameEditInfo
// The frame's text blitted by its height change (redrawn whole when that
// is more than the frame).
void
TXDisplay::ScrollFrame(const TXFrameEditInfo& info)
{
	long shift = info.fHeightChange + info.fField10;
	Rect r;
	fFrames->GetTextBounds(info.fFrame, &r);
	long magnitude = (shift >= 0) ? shift : -shift;
	if (magnitude >= r.bottom - r.top)
	{
		DrawFrameText(info.fFrame, nil);
		return;
	}
	RgnHandle update = (RgnHandle) gTXTempRegions->Get();
	if (ScrollRect(r, 0, shift, update, false))
		UpdateScrolledArea(update, info);
	gTXTempRegions->Done(update);
	if (info.fField14 > 0)
		EraseFrameBottom(info.fFrame);
}


// ROM 0x00236f7c UpdateOverflowLines__9TXDisplayFRC15TXFrameEditInfo
void
TXDisplay::UpdateOverflowLines(const TXFrameEditInfo& info)
{
	if (info.fHeightChange + info.fField10 != 0 || info.fField14 > 0)
	{
		EraseFrameBottom(info.fFrame);
		return;
	}
	FrameEndEdit(info, fFrameFormatter->GetFrameTextHeight(info.fFrame) + info.fField14, -1);
}


// ROM 0x00237048 PointToChar__9TXDisplayF5PointP13TXOffsetRangePUcT3
// Below the last line: the text's end; above the first: its start;
// otherwise the line's own answer (TXLine::PixelToCharacter).
TXAttrObject*
TXDisplay::PointToChar(Point pt, TXOffsetRange* range, unsigned char* outside, unsigned char* past)
{
	long line = PointToLine(pt, nil, outside, past);
	if (line < 0)
	{
		range->fStart.fOffset = -1;
		return nil;
	}
	Rect bounds;
	fFrames->GetLineBounds(line, &bounds);
	if (fFormatter->fLastLine == line && bounds.bottom < pt.v)
	{
		range->fStart.fOffset = fFormatter->fLineEnds->GetRangeEnd(line);
		range->fStart.fAtStart = true;
		fHilite->AdjustCharOffset(&range->fStart);
	}
	else if (line == 0 && pt.v < bounds.top)
	{
		range->fStart.fOffset = 0;
		range->fStart.fAtStart = false;
	}
	else
	{
		DoLineLayout(line);
		return fLine->PixelToCharacter((Fixed) ((ULong) (pt.h - bounds.left) << 16), range);
	}
	range->fEnd = range->fStart;
	return fText->fRuns->OffsetToObject(range->fStart.fOffset, range->fStart.fAtStart);
}


// ROM 0x00237178 CharToPoint__9TXDisplayF8TXOffsetP11TXLongPointPiT3
// Where the character boundary is: the caret's rectangle when it is the
// caret's (and no ascent is asked for), else the line's.
void
TXDisplay::CharToPoint(TXOffset offset, Boolean atStart, TXLongPoint* pt, long* height, long* ascent)
{
	TXOffsetRange range;
	fHilite->GetHiliteRange(&range);
	if (ascent == nil && range.fEnd.fOffset == range.fStart.fOffset && offset == range.fStart.fOffset)
	{
		TXLongRect caret;
		fHilite->GetCaretRect(&caret);
		pt->v = caret.top;
		pt->h = caret.left;
		if (height != nil)
			*height = caret.bottom - caret.top;
		return;
	}
	long line = fFormatter->fLineEnds->OffsetToRangeIndex(offset, atStart);
	TXLongRect bounds;
	fFrames->GetLineBounds(line, &bounds);
	DoLineLayout(line);
	pt->v = bounds.top;
	pt->h = fLine->CharacterToPixel(offset, atStart) + bounds.left;
	if (height != nil)
		*height = bounds.bottom - bounds.top;
	if (ascent != nil)
	{
		TXLineHeightInfo info;
		fFrameFormatter->GetLineHeightInfo(line, &info);
		*ascent = info.fAscent;
	}
}


// ROM 0x0023729c CharToPoint__9TXDisplayF8TXOffsetPiT2
Point
TXDisplay::CharToPoint(TXOffset offset, Boolean atStart, long* height, long* ascent)
{
	TXLongPoint pt;
	CharToPoint(offset, atStart, &pt, height, ascent);
	return fFrames->AbsToDraw(pt);
}


// ROM 0x002372f0 PointToLine__9TXDisplayCF5PointP13TXOffsetRangePUcT3
long
TXDisplay::PointToLine(Point pt, TXOffsetRange* range, unsigned char* outside, unsigned char* past) const
{
	long line = fFrames->PointToLine(pt, outside, past);
	if (range != nil && line >= 0)
		fFormatter->GetLineRange(line, range);
	return line;
}


// ROM 0x00237340 GetLineHilite__9TXDisplayFl13TXOffsetRangeP12TXLineHiliteUc
void
TXDisplay::GetLineHilite(long line, TXOffsetRange range, TXLineHilite* hilite, Boolean wholeLine)
{
	DoLineLayout(line);
	fLine->GetLineHilite(range, hilite, wholeLine);
}
