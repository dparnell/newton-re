/*
	File:		text/TXHilite.cpp

	Contains:	The hilite (TXHilite.h).

	Reconstructed from the MP2x00 US ROM (0x0023b124-0x0023cba8); each
	function cites its origin.
*/

#include "TXHilite.h"
#include "Textension.h"
#include "TXUtilities.h"
#include "Rects.h"
#include "Regions.h"
#include "Draw.h"
#include "NewtonTime.h"
#include "Polygons.h"
#include "LocalToGlobal.h"


// ROM 0x0024dd18 TXCurrentTicks__Fv
unsigned long
TXCurrentTicks(void)
{
	return Ticks();
}


// ROM 0x0023b124 __ct__8TXHiliteFv
TXHilite::TXHilite()
	: fText(nil), fDisplay(nil)
{
	fState = kTXHiliteInactive;
	fDrawn = false;
	fVisible = 0;
	fLastClickTime = TXCurrentTicks();
	fClickCount = 0;
	fLastClickPt.v = 0;
	fLastClickPt.h = 0;
	fAutoScroll = true;
	fRange.fStart.fOffset = 0;
	fRange.fStart.fAtStart = false;
	fRange.fEnd = fRange.fStart;
	fCaretRect.top = -1;
	fCaretRect.left = fCaretRect.bottom = fCaretRect.right = 0;
	fCaretFrame = 0;
	fUpDownH = -1;
	fWordSelection = false;
}


// ROM 0x0023b1bc SetHandlers__8TXHiliteFP10TextensionP9TXDisplay
void
TXHilite::SetHandlers(Textension* text, TXDisplay* display)
{
	fText = text;
	fDisplay = display;
}


// ROM 0x0023b1c4 CalcCountClicks__8TXHiliteF5PointlT2
// One more than last time when within the double-click time and a pixel
// of where it was, else one.
long
TXHilite::CalcCountClicks(Point pt, long now, long doubleClickTime)
{
	if (now - (long) fLastClickTime > doubleClickTime)
		return 1;
	short dh = (short) (pt.h - fLastClickPt.h);
	if (dh < 0)
		dh = -dh;
	if (dh > 1)
		return 1;
	short dv = (short) (pt.v - fLastClickPt.v);
	if (dv < 0)
		dv = -dv;
	if (dv > 1)
		return 1;
	return (fClickCount + 1) & 0xff;
}


// ROM 0x0023b244 Click__8TXHiliteFP16TXPointingDevicelP18TXClickCommandInfoPFUcPvT2_vPv
// Read from the assembly.  The clicks counted, the range they select
// found (a boundary, a word, a line), and the selection set - or, with
// the extend flag, grown to it and anchored at the end that did not move
// - then the pen followed (DragHilite).  ==> whether the click was taken.
Boolean
TXHilite::Click(TXPointingDevice* pen, long flags, TXClickCommandInfo* command, TXClickLoopProc proc, void* data)
{
	if (fState == kTXHiliteInactive)
		return false;
	GrafPtr saved;
	GetPort(&saved);
	SetPort(fText->GetTextPort());
	Point pt = pen->FirstLocation();
	SetPort(saved);
	unsigned long now = TXCurrentTicks();
	fClickCount = CalcCountClicks(pt, now, pen->GetDoubleClickTime());
	fLastClickTime = now;
	fLastClickPt = pt;
	TXOffsetRange range;
	if (!GetClickRange(pt, fClickCount, &range))
		return false;
	TXRun* run = nil;
	if (flags & kTXClickExtend)
	{
		TXOffsetPos savedStart = fRange.fStart;
		TXOffsetPos savedEnd = fRange.fEnd;
		ExtendHilite(range);
		Boolean startMoved = fRange.fStart.fOffset != savedStart.fOffset;
		if (!startMoved && fRange.fEnd.fOffset == savedEnd.fOffset)
			startMoved = (range.fStart.fOffset == savedStart.fOffset);
		TXOffsetPos anchor = startMoved ? savedEnd : savedStart;
		range.fEnd = anchor;
		range.fStart = anchor;
	}
	else
	{
		run = fText->IsRangeGraphicsRun(nil);
		if (run == nil || !(fRange == range))
		{
			SetHiliteRange(range, true, true);
			run = fText->IsRangeGraphicsRun(nil);
		}
	}
	DragHilite(range, pen, flags, run, command, proc, data);
	if (fRange.fEnd.fOffset == fRange.fStart.fOffset)
		fWordSelection = false;
	else
	{
		fWordSelection = (fClickCount == 2);
		fRange.fStart.fAtStart = false;
	}
	return true;
}


// ROM 0x0023b4b4 DoClickLoop__8TXHiliteFUcPv
void
TXHilite::DoClickLoop(Boolean /*inLoop*/, void* /*scroll*/)
{ }


// ROM 0x0023b4b8 AdjustCharOffset__8TXHiliteFP8TXOffset
void
TXHilite::AdjustCharOffset(TXOffsetPos* offset)
{
	if (offset->fOffset != 0 && !fText->fFormatter->IsLineFeed(offset->fOffset - 1))
	{
		offset->fAtStart = offset->fAtStart || fText->fChars->Count() == offset->fOffset;
		return;
	}
	offset->fAtStart = false;
}


// ROM 0x0023b53c ArrowKey__8TXHiliteFUcl
// 0x1c left, 0x1d right, 0x1e up, 0x1f down: the caret moved, or with the
// extend flag the selection grown.  The column up and down keep to, and
// whether the selection is by words, survive the move.
void
TXHilite::ArrowKey(unsigned char key, long flags)
{
	fWordSelection = false;
	TXOffsetPos to;
	Boolean moved;
	if (key == 0x1e)
		moved = UpDownArrows(true, flags, &to);
	else if (key == 0x1f)
		moved = UpDownArrows(false, flags, &to);
	else
		moved = LeftRightArrows(key == 0x1d, flags, &to);
	if (!moved)
		return;
	AdjustCharOffset(&to);
	TXOffsetRange range;
	range.fStart = to;
	range.fEnd = to;
	long upDownH = fUpDownH;
	Boolean words = fWordSelection;
	if (flags & kTXClickExtend)
		ExtendHilite(range);
	else
		SetHiliteRange(range, true, true);
	fUpDownH = upDownH;
	fWordSelection = words;
}


// ROM 0x0023b62c LeftRightArrows__8TXHiliteFUclP8TXOffset
// From the selection's end in the direction moved: a character (or a
// picture: TXStyledText::AdvanceOffset), a word, or to the line's end.
// A selection collapses to its end without moving.
Boolean
TXHilite::LeftRightArrows(Boolean right, long flags, TXOffsetPos* offset)
{
	TXOffsetRange range;
	GetHiliteRange(&range);
	fUpDownH = -1;
	*offset = right ? range.fEnd : range.fStart;
	if (flags & (kTXClickLines | kTXClickWords))
	{
		TXOffsetRange to;
		if (flags & kTXClickWords)
		{
			offset->fAtStart = !right;
			if (!fText->CharToWord(offset->fOffset, offset->fAtStart, &to, 0))
				return false;
			fWordSelection = true;
		}
		else
			fText->CharToLine(offset->fOffset, offset->fAtStart, &to);
		*offset = right ? to.fEnd : to.fStart;
	}
	else if ((flags & kTXClickExtend) || range.fEnd.fOffset == range.fStart.fOffset)
	{
		long n = fText->AdvanceOffset(offset->fOffset, right);
		offset->fOffset = right ? offset->fOffset + n : offset->fOffset - n;
	}
	offset->fAtStart = right;
	return true;
}


// ROM 0x0023b738 UpDownArrows__8TXHiliteFUclP8TXOffset
// To the line above or below, at the column the caret started from
// (remembered in fUpDownH across a run of up and down keys); by line, to
// the text's start or end.
Boolean
TXHilite::UpDownArrows(Boolean up, long flags, TXOffsetPos* offset)
{
	if (flags & kTXClickLines)
	{
		if (up)
		{
			offset->fOffset = 0;
			offset->fAtStart = false;
		}
		else
		{
			offset->fOffset = fText->fChars->Count();
			offset->fAtStart = true;
		}
		return true;
	}
	TXOffsetRange range;
	GetHiliteRange(&range);
	*offset = up ? range.fStart : range.fEnd;
	if (fUpDownH < 0)
	{
		TXLongPoint pt = { 0, 0 };
		fDisplay->CharToPoint(offset->fOffset, offset->fAtStart, &pt, nil, nil);
		fUpDownH = pt.h;
	}
	long line = fText->CharToLine(offset->fOffset, offset->fAtStart, nil);
	if (up)
	{
		line = line - 1;
		if (line < 0)
		{
			offset->fOffset = 0;
			offset->fAtStart = false;
		}
	}
	else
	{
		line = line + 1;
		if (fText->fFormatter->fLastLine + 1 <= line)
		{
			offset->fOffset = fText->fChars->Count();
			offset->fAtStart = true;
			return true;
		}
	}
	if (line >= 0)
	{
		TXLongRect bounds;
		fDisplay->fFrames->GetLineBounds(line, &bounds);
		TXLongPoint at = { bounds.top, fUpDownH };
		Point pt = fDisplay->fFrames->AbsToDraw(at);
		TXOffsetRange found;
		unsigned char outside, past;
		fText->fDisplay->PointToChar(pt, &found, &outside, &past);
		if (found.fStart.fOffset < 0)
			return false;
		*offset = found.fStart;
	}
	return true;
}


// ROM 0x0023b8e4 Activate__8TXHiliteFUcT1
void
TXHilite::Activate(Boolean active, Boolean show)
{
	SetHiliteState(active ? (show ? kTXHiliteOn : kTXHiliteOff) : kTXHiliteInactive);
}


// ROM 0x0023b90c Draw__8TXHiliteFv
// Drawn in XOR: drawing it again takes it away.
void
TXHilite::Draw(void)
{
	fDrawn = true;
	if (fVisible < 0)
		return;
	TXRun* custom = IsCustomHilite(nil);
	if (custom == nil && fState == kTXHiliteOff)
		return;
	TXDrawEnv env;
	fDisplay->SetDrawEnv(&env);
	if (custom != nil)
	{
		TXRunPositionInfo where;
		CalcRangePosition(fRange, &where);
		custom->DrawHilite(where);
	}
	else
		HiliteRange(fRange);
	fDisplay->RestoreDrawEnv(env);
}


// ROM 0x0023b9dc SetHiliteState__8TXHiliteFc
// The old hilite drawn away and the new one drawn; a picture changes its
// own frame.
void
TXHilite::SetHiliteState(char state)
{
	if (fState == state)
		return;
	Boolean visible = fVisible >= 0;
	TXRun* custom = IsCustomHilite(nil);
	if (custom == nil && visible)
	{
		TXDrawEnv env;
		fDisplay->SetDrawEnv(&env);
		if (fDrawn)
			Draw();
		fState = state;
		Draw();
		fDisplay->RestoreDrawEnv(env);
		return;
	}
	fState = state;
	if (custom == nil)
		return;
	if (state == kTXHiliteInactive)
		state = kTXHiliteOff;
	TXRunPositionInfo where;
	if (visible)
		CalcRangePosition(fRange, &where);
	TXDrawEnv env;
	fDisplay->SetDrawEnv(&env);
	custom->SetHilite(state, where, visible);
	fDisplay->RestoreDrawEnv(env);
}


// ROM 0x0023bae8 Invalid__8TXHiliteFUc
void
TXHilite::Invalid(Boolean hide)
{
	if (hide)
		SetHiliteState(kTXHiliteOff);
	fCaretRect.top = -1;
	fUpDownH = -1;
}


// ROM 0x0023bb24 GetHiliteRange__8TXHiliteCFP13TXOffsetRange
void
TXHilite::GetHiliteRange(TXOffsetRange* range) const
{
	*range = fRange;
}


// ROM 0x0023bb38 HiliteRect__8TXHiliteCFRC10TXLongRectl
// Inverted - or, inactive, framed in XOR; into a region being recorded,
// framed.
void
TXHilite::HiliteRect(const TXLongRect& r, long frame) const
{
	TXFrames* frames = fDisplay->fFrames;
	TXLongRect text;
	frames->GetAbsTextBounds(frame, &text);
	if (!text.Sect(r, &text))
		return;
	Rect dr;
	frames->AbsToDraw(text, &dr);
	GrafPtr port;
	GetPort(&port);
	if (port->rgnSave != nil)
	{
		FrameRect(&dr);
		return;
	}
	if (fState == kTXHiliteInactive)
	{
		PenState pen;
		GetPenState(&pen);
		PenNormal();
		PenSize(1, 1);
		PenMode(10);
		FrameRect(&dr);
		SetPenState(&pen);
	}
	else
		InvertRect(&dr);
}


// ROM 0x0023bc24 HiliteLine__8TXHiliteCFlT113TXOffsetRangeP10TXLongRect
// The part of line `line` the range covers, from `r`'s top; `r->top`
// moves down past the line.
void
TXHilite::HiliteLine(long line, long frame, TXOffsetRange range, TXLongRect* r) const
{
	Boolean whole = fText->fFormatter->fLastLine != line;
	TXLineHilite lh;
	fDisplay->GetLineHilite(line, range, &lh, whole);
	TXLongRect q;
	q.top = r->top;
	q.bottom = fText->fFrameFormatter->GetLinesHeight(line, line) + q.top;
	q.left = r->left + (short) ((ULong) (lh.fLeft + 0x8000) >> 16);
	q.right = r->left + (short) ((ULong) (lh.fWidth + lh.fLeft + 0x8000) >> 16);
	if (q.right > r->right)
		q.right = r->right;
	HiliteRect(q, frame);
	r->top = q.bottom;
}


// ROM 0x0023bd1c HiliteFrame__8TXHiliteCFl13TXOffsetRangeN21
// The lines [firstLine, lastLine] in this frame: the first and last
// partly, the ones between as one block.
void
TXHilite::HiliteFrame(long frame, TXOffsetRange range, long firstLine, long lastLine) const
{
	TXFrameFormatter* heights = fText->fFrameFormatter;
	TXOffsetPair lines;
	if (!heights->GetFrameLineRange(frame, &lines))
		return;
	if (!(lines.fEnd >= firstLine && lastLine >= lines.fStart))
		return;
	TXLongRect text;
	fDisplay->fFrames->GetAbsTextBounds(frame, &text);
	if (firstLine < lines.fStart && lines.fEnd < lastLine)
	{
		text.bottom = heights->GetFrameTextHeight(frame) + text.top;
		HiliteRect(text, frame);
		return;
	}
	long from = (firstLine > lines.fStart) ? firstLine : lines.fStart;
	long to = (lastLine < lines.fEnd) ? lastLine : lines.fEnd;
	if (from > lines.fStart)
		text.top = heights->GetLinesHeight(lines.fStart, from - 1) + text.top;
	if (from == firstLine)
	{
		HiliteLine(from, frame, range, &text);
		from++;
		if (from > to)
			return;
	}
	if (!(from == to && to == lastLine))
	{
		long blockEnd = (to == lastLine) ? to - 1 : to;
		text.bottom = heights->GetLinesHeight(from, blockEnd) + text.top;
		HiliteRect(text, frame);
		if (blockEnd == to)
			return;
		text.top = text.bottom;
	}
	HiliteLine(to, frame, range, &text);
}


// ROM 0x0023bef8 CalcRangePosition__8TXHiliteF13TXOffsetRangeP17TXRunPositionInfo
void
TXHilite::CalcRangePosition(TXOffsetRange range, TXRunPositionInfo* where)
{
	TXLongRect bounds;
	fText->GetRangeBounds(range, &bounds);
	Rect r;
	fDisplay->fFrames->AbsToDraw(bounds, &r);
	where->fTop = r.top;
	where->fHeight = r.bottom - r.top;
	where->fLeft = (Fixed) ((ULong) r.left << 16);
	where->fWidth = (Fixed) ((ULong) (r.right - r.left) << 16);
}


// ROM 0x0023bf90 CalcCaretRect__8TXHiliteFv
void
TXHilite::CalcCaretRect(void)
{
	TXLongRect bounds;
	fText->GetRangeBounds(fRange, &bounds);
	fCaretRect = bounds;
	fCaretFrame = fText->fFrameFormatter->CharToFrame(fRange.fStart.fOffset, fRange.fStart.fAtStart);
}


// ROM 0x0023bfdc GetHiliteRgn__8TXHiliteFUcT1
RgnHandle
TXHilite::GetHiliteRgn(Boolean frameOnly, Boolean global)
{
	GrafPtr saved;
	GetPort(&saved);
	SetPort(fText->GetTextPort());
	OpenRgn();
	Draw();
	RgnHandle rgn = NewRgn();
	CloseRgn(rgn);
	if (frameOnly)
	{
		RgnHandle inner = (RgnHandle) gTXTempRegions->Get();
		CopyRgn(rgn, inner);
		InsetRgn(inner, 1, 1);
		DiffRgn(rgn, inner, rgn);
		gTXTempRegions->Done(inner);
	}
	if (global)
	{
		Point origin;
		origin.v = 0;
		origin.h = 0;
		LocalToGlobal(&origin);
		OffsetRgn(rgn, origin.h, origin.v);
	}
	SetPort(saved);
	return rgn;
}


// ROM 0x0023c0bc IsPointInHilite__8TXHiliteF5Point
Boolean
TXHilite::IsPointInHilite(Point pt)
{
	if (fRange.fEnd.fOffset == fRange.fStart.fOffset)
		return false;
	TXOffsetRange at;
	unsigned char outside, past;
	fText->fDisplay->PointToChar(pt, &at, &outside, &past);
	if (past)
		return false;
	if (at.fStart.fOffset > fRange.fStart.fOffset && at.fEnd.fOffset < fRange.fEnd.fOffset)
		return true;
	return at.fStart == fRange.fStart || at.fEnd == fRange.fEnd;
}


// ROM 0x0023c178 GetCaretRect__8TXHiliteFP10TXLongRect
void
TXHilite::GetCaretRect(TXLongRect* r)
{
	if (fCaretRect.top < 0)
		CalcCaretRect();
	*r = fCaretRect;
}


// ROM 0x0023c1ac HiliteRange__8TXHiliteF13TXOffsetRange
// Every frame of the view the range's lines are in.  An empty range at
// the selection's start - the caret - is not drawn here.
void
TXHilite::HiliteRange(TXOffsetRange range)
{
	if (range.fEnd.fOffset == range.fStart.fOffset && fRange.fStart.fOffset == range.fStart.fOffset)
		return;
	long first = fText->CharToLine(range.fStart.fOffset, range.fStart.fAtStart, nil);
	long last = fText->CharToLine(range.fEnd.fOffset, range.fEnd.fAtStart, nil);
	TXSectFrames frames;
	frames.fCurrent = -2;
	frames.fListCount = 0;
	fText->fDisplay->GetViewFrames(&frames);
	for (long frame = frames.GetNextFrame(); frame >= 0; frame = frames.GetNextFrame())
		HiliteFrame(frame, range, first, last);
}


// ROM 0x0023c288 IsCustomHilite__8TXHiliteFPC13TXOffsetRange
TXRun*
TXHilite::IsCustomHilite(const TXOffsetRange* range)
{
	TXRun* run = fText->IsRangeGraphicsRun(range);
	if (run != nil && (run->GetObjFlags() & 1))
		return run;
	return nil;
}


// ROM 0x0023c2c8 SetHiliteRange__8TXHiliteFRC13TXOffsetRangeUcT2
// A new range: the old hilite taken away, the caret's rectangle
// forgotten, and the new one shown (`show`) or left as the state was.
Boolean
TXHilite::SetHiliteRange(const TXOffsetRange& range, Boolean show, Boolean /*scroll*/)
{
	char state = fState;
	if (state == kTXHiliteInactive)
		show = false;
	Boolean same = (fRange == range);
	if (!same || (show && state != kTXHiliteOn))
	{
		fWordSelection = true;
		Invalid(true);
		fRange = range;
		SetHiliteState(show ? kTXHiliteOn : state);
	}
	return !same;
}


// ROM 0x0023c35c SetHiliteStart__8TXHiliteF8TXOffset
// The selection's start moved: only the stretch between the old start and
// the new one is inverted.
void
TXHilite::SetHiliteStart(TXOffsetPos start)
{
	long diff = fRange.fStart.fOffset - start.fOffset;
	TXOffsetRange range(start, fRange.fEnd);
	if (diff == 0)
		return;
	Boolean custom = IsCustomHilite(nil) != nil || IsCustomHilite(&range) != nil;
	if (fRange.fEnd.fOffset == fRange.fStart.fOffset || custom)
	{
		SetHiliteState(kTXHiliteOff);
		SetHiliteRange(range, range.fEnd.fOffset != range.fStart.fOffset, true);
		return;
	}
	TXOffsetRange delta(fRange.fStart, start);
	if (diff > 0)
		delta.fStart.fAtStart = true;
	delta.CheckBounds();
	HiliteRange(delta);
	fVisible--;
	SetHiliteState(fRange.fEnd.fOffset != start.fOffset);
	SetHiliteRange(range, false, true);
	fVisible++;
}


// ROM 0x0023c4e4 SetHiliteEnd__8TXHiliteF8TXOffset
void
TXHilite::SetHiliteEnd(TXOffsetPos end)
{
	long diff = end.fOffset - fRange.fEnd.fOffset;
	TXOffsetRange range(fRange.fStart, end);
	if (diff == 0)
		return;
	Boolean custom = IsCustomHilite(nil) != nil || IsCustomHilite(&range) != nil;
	if (fRange.fEnd.fOffset == fRange.fStart.fOffset || custom)
	{
		SetHiliteState(kTXHiliteOff);
		SetHiliteRange(range, range.fEnd.fOffset != range.fStart.fOffset, true);
		return;
	}
	TXOffsetRange delta(fRange.fEnd, end);
	if (diff <= 0)
		delta.fEnd.fAtStart = false;
	else
		delta.fStart.fAtStart = false;
	delta.CheckBounds();
	HiliteRange(delta);
	fVisible--;
	SetHiliteState(fRange.fStart.fOffset != end.fOffset);
	SetHiliteRange(range, false, true);
	fVisible++;
}


// ROM 0x0023c674 ExtendHilite__8TXHiliteF13TXOffsetRange
// Whichever end the range goes past is moved; inside the selection, the
// nearer end.
void
TXHilite::ExtendHilite(TXOffsetRange range)
{
	TXDrawEnv env;
	fDisplay->SetDrawEnv(&env);
	long ds = fRange.fStart.fOffset - range.fStart.fOffset;
	long de = range.fEnd.fOffset - fRange.fEnd.fOffset;
	Boolean outward = (ds > 0 || de > 0);
	if (ds > 0)
		SetHiliteStart(range.fStart);
	else if (outward)
		SetHiliteEnd(range.fEnd);
	else
	{
		if (ds < 0)
			ds = -ds;
		if (de < 0)
			de = -de;
		if (ds < de)
			SetHiliteStart(range.fStart);
		else
			SetHiliteEnd(range.fEnd);
	}
	fDisplay->RestoreDrawEnv(env);
}


// ROM 0x0023c720 GetClickRange__8TXHiliteF5PointiP13TXOffsetRange
// One click: a boundary; two: a word; more: a line.
Boolean
TXHilite::GetClickRange(Point pt, int clicks, TXOffsetRange* range)
{
	unsigned char outside, past;
	if (clicks == 1)
		fText->fDisplay->PointToChar(pt, range, &outside, &past);
	else if (clicks == 2)
	{
		fText->PointToWord(pt, range, &outside, &past);
		if (range->fEnd.fOffset < range->fStart.fOffset)
			return false;
	}
	else if (fText->fDisplay->PointToLine(pt, range, &outside, &past) < 0)
		return false;
	return range->fStart.fOffset >= 0;
}


// ROM 0x0023c7e8 CalcAutoScrollParams__8TXHiliteFP5PointlP11TXLongPoint
// Outside the view: a scroll towards the pen of 18 pixels, doubled for
// each second it has been out (up to three times).
void
TXHilite::CalcAutoScrollParams(Point* pt, long elapsed, TXLongPoint* scroll)
{
	scroll->v = 0;
	scroll->h = 0;
	Rect view = (*fText->fDisplay->fViewRgn)->rgnBBox;
	Point p = *pt;
	if (PtInRect(p, &view))
		return;
	long seconds = elapsed / 60;
	if (seconds >= 3)
		seconds = 3;
	long step = 0x12 << seconds;
	if (p.v - view.bottom > 0)
		scroll->v = -step;
	else if (view.top - p.v > 0)
		scroll->v = step;
	if (p.h - view.right > 0)
		scroll->h = -step;
	else if (view.left - p.h > 0)
		scroll->h = step;
	fDisplay->AdjustScrollValues(scroll);
}


// ROM 0x0023c8ec DragHilite__8TXHiliteF13TXOffsetRangeP16TXPointingDevicelP5TXRunP18TXClickCommandInfoPFUcPvT2_vPv
// Read from the assembly.  A picture is offered the click first; then, as
// long as the pen is down, the selection is grown from `anchor` to where
// it is, the view scrolled when it leaves it (while scrolling, by line).
void
TXHilite::DragHilite(TXOffsetRange anchor, TXPointingDevice* pen, long flags, TXRun* run, TXClickCommandInfo* command, TXClickLoopProc proc, void* data)
{
	TXDrawEnv env;
	fDisplay->SetDrawEnv(&env);
	unsigned long start = TXCurrentTicks();
	TXRunPositionInfo where;
	Rect bounds;
	if (run != nil)
	{
		CalcRangePosition(anchor, &where);
		long frame = fText->fFrameFormatter->CharToFrame(anchor.fStart.fOffset, anchor.fStart.fAtStart);
		fDisplay->fFrames->GetTextBounds(frame, &bounds);
		if (run->Click(where, pen, flags, fClickCount, bounds, command) & 1)
		{
			fDisplay->RestoreDrawEnv(env);
			return;
		}
	}
	while (pen->IsStillDown())
	{
		Point pt = pen->CurrentLocation();
		TXLongPoint scroll;
		CalcAutoScrollParams(&pt, TXCurrentTicks() - start, &scroll);
		fLastClickPt = pt;
		long scrolling = scroll.v | scroll.h;
		long clicks;
		if (scrolling != 0)
			clicks = 3;
		else
		{
			clicks = fClickCount;
			start = TXCurrentTicks();
		}
		if (run != nil && (run->Click(where, pen, flags, fClickCount, bounds, command) & 1))
			break;
		TXOffsetRange range;
		if (GetClickRange(pt, clicks, &range))
		{
			if (range.fEnd.fOffset > anchor.fEnd.fOffset)
			{
				SetHiliteStart(anchor.fStart);
				SetHiliteEnd(range.fEnd);
			}
			else if (range.fStart.fOffset < anchor.fStart.fOffset)
			{
				SetHiliteStart(range.fStart);
				SetHiliteEnd(anchor.fEnd);
			}
		}
		if (scrolling != 0)
		{
			if (fAutoScroll)
				fDisplay->Scroll(&scroll);
			fDisplay->RestoreDrawEnv(env);
			DoClickLoop(true, &scroll);
			if (proc != nil)
				proc(1, &scroll, data);
			fDisplay->SetDrawEnv(&env);
		}
	}
	fDisplay->RestoreDrawEnv(env);
}
