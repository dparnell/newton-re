/*
	File:		text/Textension.cpp

	Contains:	Textension, a document (Textension.h).

	Reconstructed from the MP2x00 US ROM (0x00252af4-0x002540c0); each
	function cites its origin.
*/

#include "Textension.h"
#include "TXRulerRange.h"
#include "TXUtilities.h"
#include "OSErrors.h"


TXAttrValues*	Textension::fDefaultRunAttrValues = nil;


// ROM 0x00252af4 __ct__10TXHandlersFv
TXHandlers::TXHandlers()
{
	fDisplay = nil;
	fFrames = nil;
	fHilite = nil;
	fChars = nil;
}


// ROM 0x00253d34 __ct__15TXReplaceParamsFv
TXReplaceParams::TXReplaceParams()
{
	fContainer = nil;
	fRun = nil;
	fFlags = 7;
	fTypes = 1;
}


// ROM 0x00253d84 __ct__15TXReplaceParamsFRC16TXTextDescriptor
TXReplaceParams::TXReplaceParams(const TXTextDescriptor& text)
	: TXTextDescriptor(text)
{
	fContainer = nil;
	fRun = nil;
	fFlags = 7;
	fTypes = 1;
}


// ROM 0x00253e44 __ct__15TXReplaceParamsFRC16TXTextDescriptorP5TXRunUc
TXReplaceParams::TXReplaceParams(const TXTextDescriptor& text, TXRun* run, Boolean reference)
	: TXTextDescriptor(text)
{
	fContainer = nil;
	fFlags = 7;
	fRun = run;
	if (reference)
		fFlags = 0xf;
	fTypes = 3;
}


// ROM 0x00252b30 TextensionStart__10TextensionSFv
// The engine's globals.  (Host: gTXLineCharsBuffer is a static array - the
// ROM allocates its 0x100 bytes here.)
NewtonErr
Textension::TextensionStart(void)
{
	gTXHasColor = false;
	gTXDefaultTabVal = 30;
	gTXTempRegions = new TXTempRegions;
	TXDisplay::Start();
	gRegisteredRuns = new TXRegisteredObjects;
	if (gRegisteredRuns != nil)
	{
		gRegisteredRulers = new TXRegisteredObjects;
		if (gRegisteredRulers != nil)
			return noErr;
	}
	return kError_No_Memory;
}


// ROM 0x00253084 RegisterRun__10TextensionSFP5TXRun
// (No look at whether there is room: the list has six.)
void
Textension::RegisterRun(TXRun* run)
{
	gRegisteredRuns->fObjects[gRegisteredRuns->fCount++] = run;
}


// ROM 0x002538ac RegisterRuler__10TextensionSFP7TXRuler
void
Textension::RegisterRuler(TXRuler* ruler)
{
	gRegisteredRulers->fObjects[gRegisteredRulers->fCount++] = ruler;
}


// ROM 0x00253ebc GetNewRunObject__10TextensionSFv
TXAttrObject*
Textension::GetNewRunObject(void)
{
	return gRegisteredRuns->GetIndObject(0)->CreateNew();
}


// ROM 0x00253ec8 GetNewRulerObject__10TextensionSFv
TXAttrObject*
Textension::GetNewRulerObject(void)
{
	return gRegisteredRulers->GetIndObject(0)->CreateNew();
}


// ROM 0x00253ed4 __ct__10TextensionFv
Textension::Textension()
	: fDisplay(nil), fHilite(nil), fFormatter(nil), fFrameFormatter(nil), fRulers(nil), fPendingRun(nil),
	  fPendingRunInvalid(false)
{ }


// ROM 0x00254020 __dt__10TextensionFv
Textension::~Textension()
{
	if (fDisplay != nil)
		delete fDisplay;
	delete fHilite;
	if (fFormatter != nil)
		delete fFormatter;
	fPendingRun->Free();
	if (fRulers != nil)
		delete fRulers;
}


// ROM 0x00253f14 ITextension__10TextensionFP8GrafPortRC10TXHandlersc
NewtonErr
Textension::ITextension(GrafPort* port, const TXHandlers& handlers, char kind)
{
	IStyledText(port, handlers.fChars, kind);
	fHilite = (handlers.fHilite != nil) ? handlers.fHilite : new TXHilite;
	TXRuler* ruler = (TXRuler*) GetNewRulerObject();
	fRulers = new TXRulerRange(fChars, ruler);
	fFormatter = new TXFormatter;
	fDisplay = (handlers.fDisplay != nil) ? handlers.fDisplay : new TXDisplay;
	TXDisplayHandlers display;
	display.fHilite = fHilite;
	display.fText = this;
	display.fFormatter = fFormatter;
	display.fFrames = handlers.fFrames;
	display.fRulers = fRulers;
	fDisplay->SetHandlers(&display);
	fFormatter->SetHandlers(this, display.fFrames, fRulers, kind);
	fHilite->SetHandlers(this, fDisplay);
	fFrameFormatter = display.fFrames->fFormatter;
	fPendingRun = GetNewRunObject();
	fPendingRunInvalid = false;
	if (fDefaultRunAttrValues != nil)
		fPendingRun->Update(fDefaultRunAttrValues, 1);
	return noErr;
}


// ROM 0x00252bf8 SetCharsHandler__10TextensionFP7TXChars
void
Textension::SetCharsHandler(TXChars* chars)
{
	if (fChars != nil)
		delete fChars;
	fChars = chars;
	fRulers->fChars = chars;
}


// ROM 0x00252c34 DisplayChanged__10TextensionFRC16TXDisplayChanges
unsigned long
Textension::DisplayChanged(const TXDisplayChanges& changes)
{
	unsigned long flags = changes.fFlags;
	if (flags == 0)
		return 0;
	if (flags & 1)
	{
		// the width changed: everything formatted again
		Format(false, 0, -1);
		return 0x10;
	}
	if (flags & 2)
	{
		fDisplay->InvalidDraw();
		fFrameFormatter->Format();
		return 0x18;
	}
	fDisplay->InvalidDraw();
	if (flags & 0x10)
		fDisplay->CheckScroll(true);
	if (flags & 4)
	{
		TXOffsetPair lines;
		changes.GetFormatRange(&lines);
		TXEditInfo info;
		fDisplay->BeginEdit(&info);
		TXOffset end = fFormatter->fLineEnds->GetRangeEnd(lines.fEnd);
		TXOffset start = fFormatter->fLineEnds->GetRangeStart(lines.fStart);
		fFormatter->Format(start, end, &lines.fStart, &lines.fEnd);
		EndEdit(info, lines.fStart, lines.fEnd, nil);
		flags = TXDisplay::fLastEditAction | (flags - 4);
	}
	return flags;
}


// ROM 0x00252d6c Format__10TextensionFUclT2
NewtonErr
Textension::Format(Boolean noDisplay, TXOffset start, long length)
{
	if (length < 0)
		length = fChars->Count();
	TXEditInfo info;
	if (!noDisplay)
		fDisplay->BeginEdit(&info);
	long first, last;
	NewtonErr err = fFormatter->Format(start, start + length, &first, &last);
	if (!noDisplay)
	{
		if (err != noErr)
			info.fDoEdit = false;
		EndEdit(info, first, last, nil);
	}
	return err;
}


// ROM 0x00252e28 EndEdit__10TextensionFRC10TXEditInfolT2P8TXOffset
void
Textension::EndEdit(const TXEditInfo& info, long firstLine, long lastLine, TXOffsetPos* caret)
{
	fDisplay->EndEdit(info, firstLine, lastLine, caret);
}


// ROM 0x00252e5c PointToWord__10TextensionF5PointP13TXOffsetRangePUcT3
void
Textension::PointToWord(Point pt, TXOffsetRange* range, unsigned char* outside, unsigned char* past)
{
	fDisplay->PointToChar(pt, range, outside, past);
	long at = range->fStart.fOffset;
	if (at < 0)
		return;
	if (range->fEnd.fOffset != at)
		return;
	range->fStart.fAtStart = false;
	CharToWord(at, range->fStart.fAtStart, range, 0);
}


// ROM 0x00252ecc CharToLine__10TextensionCF8TXOffsetP13TXOffsetRange
long
Textension::CharToLine(TXOffset offset, Boolean atStart, TXOffsetRange* line) const
{
	long index = fFormatter->fLineEnds->OffsetToRangeIndex(offset, atStart);
	if (line != nil)
		fFormatter->GetLineRange(index, line);
	return index;
}


// ROM 0x00252f10 GetRangeBounds__10TextensionFRC13TXOffsetRangeP10TXLongRect
// The range's start line's band, narrowed to what the range covers of it.
void
Textension::GetRangeBounds(const TXOffsetRange& range, TXLongRect* bounds)
{
	long line = CharToLine(range.fStart.fOffset, range.fStart.fAtStart, nil);
	fDisplay->fFrames->GetLineBounds(line, bounds);
	TXLineHilite hilite;
	fDisplay->GetLineHilite(line, range, &hilite, false);
	long left = bounds->left + (short) ((ULong) (hilite.fLeft + 0x8000) >> 16);
	bounds->left = left;
	bounds->right = left + (short) ((ULong) (hilite.fWidth + 0x8000) >> 16);
}


// ROM 0x00252fb4 GetRangeBounds__10TextensionFRC13TXOffsetRangeP4Rect
void
Textension::GetRangeBounds(const TXOffsetRange& range, Rect* bounds)
{
	TXLongRect abs;
	GetRangeBounds(range, &abs);
	fDisplay->fFrames->AbsToDraw(abs, bounds);
}


// ROM 0x00252fec Click__10TextensionFP16TXPointingDevicelP18TXClickCommandInfoPFUcPvT2_vPv
// A click that moved the selection's start means typing takes its style
// from the new place.
void
Textension::Click(TXPointingDevice* pen, long flags, TXClickCommandInfo* command, TXClickLoopProc proc, void* data)
{
	TXOffsetRange before;
	fHilite->GetHiliteRange(&before);
	if (fHilite->Click(pen, flags, command, proc, data))
	{
		TXOffsetRange after;
		fHilite->GetHiliteRange(&after);
		if (!(after.fStart == before.fStart))
			fPendingRunInvalid = true;
	}
}


// ROM 0x00253098 IsRangeGraphicsRun__10TextensionFPC13TXOffsetRange
TXRun*
Textension::IsRangeGraphicsRun(const TXOffsetRange* range)
{
	TXOffsetRange hilite;
	if (range == nil)
	{
		fHilite->GetHiliteRange(&hilite);
		range = &hilite;
	}
	if (range->fEnd.fOffset - range->fStart.fOffset == 1)
	{
		TXRun* run = (TXRun*) fRuns->OffsetToObject(range->fStart.fOffset, false);
		if (!run->IsTextRun())
			return run;
	}
	return nil;
}


// ROM 0x00253128 GetHiliteRangeRuns__10TextensionFP13TXOffsetRange
TXObjectIterator*
Textension::GetHiliteRangeRuns(TXOffsetRange* range)
{
	TXObjectIterator* runs = new TXObjectIterator(fRuns, range->fStart.fOffset);
	if (runs->fObject != nil)
		return runs;
	delete runs;
	return nil;
}


// ROM 0x00253178 UpdatePendingRun__10TextensionFv
// When the selection has moved: the text run at its start copied into the
// pending run, or - none there - a new default run.
TXAttrObject*
Textension::UpdatePendingRun(void)
{
	if (!fPendingRunInvalid)
		return fPendingRun;
	TXAttrObject* run = fPendingRun;
	fPendingRunInvalid = false;
	TXOffsetRange range;
	fHilite->GetHiliteRange(&range);
	TXRun* here = fRuns->CharToTextRun(range.fStart.fOffset, range.fStart.fAtStart);
	if (here != nil)
		run->Assign(here);
	else
	{
		run->Free();
		run = GetNewRunObject();
		fPendingRun = run;
		if (fDefaultRunAttrValues != nil)
			run->Update(fDefaultRunAttrValues, 0);
	}
	return run;
}


// ROM 0x0025321c ReplaceRange__10TextensionFlT1P15TXReplaceParams
// [start, end) replaced by the parameters' characters in their run (or
// the text run at `start`, or the pending run): the rulers', runs' and
// characters' parts, then the line ends, with the display bracketed
// round it; the caret goes after what was put in.  NOT YET: a container
// (params->fContainer) - the host has no TXContainer.
NewtonErr
Textension::ReplaceRange(TXOffset start, TXOffset end, TXReplaceParams* params)
{
	long length = end - start;
	TXEditInfo info;
	fDisplay->BeginEdit(&info);
	TXAttrObject* pendingRuler = nil;
	long extra = fRulers->GetReplaceExtraChars(start, end, &pendingRuler);
	unsigned char type = 0;
	long count = params->fCount;
	fRulers->ReplaceRange(start, length + extra, count + extra, pendingRuler, true);
	if (type != 3)
	{
		TXAttrObject* run = params->fRun;
		Boolean reference;
		if (run == nil)
		{
			run = fRuns->CharToTextRun(start, false);
			if (run == nil)
				run = UpdatePendingRun();
			reference = true;
		}
		else
			reference = (params->fFlags & kTXReplaceReference) != 0;
		fRuns->ReplaceRange(start, length, count, run, reference);
		if (type == 0)
		{
			NewtonErr err = fChars->Replace(start, length, params);
			if (err != noErr)
			{
				info.fDoEdit = false;
				EndEdit(info, 0, 0, nil);
				return err;
			}
		}
	}
	long first, last;
	NewtonErr err = fFormatter->ReplaceRange(start, length + extra, count + extra, params->fFlags, &first, &last);
	TXOffsetPos caret;
	caret.fOffset = count + start;
	caret.fAtStart = (params->fFlags & kTXReplaceCaretAtStart) != 0;
	EndEdit(info, first, last, &caret);
	if (params->fContainer != nil)
		Compact();
	if (params->fFlags & kTXReplaceNewPendingRun)
		fPendingRunInvalid = true;
	return err;
}


// ROM 0x00253530 ClearKeyDown__10TextensionFUsP13TXOffsetRange
// What a delete key takes away: the selection, or (backspace) the
// character or picture before the caret - whose style typing then takes.
// ==> how many characters.
long
Textension::ClearKeyDown(UniChar key, TXOffsetRange* range)
{
	long count = range->fEnd.fOffset - range->fStart.fOffset;
	if (count == 0)
	{
		if (key != 8)
			return 0;
		count = AdvanceOffset(range->fStart.fOffset, false);
		if (count == 0)
			return 0;
		range->fStart.fOffset = range->fStart.fOffset - count;
	}
	long length;
	TXAttrObject* run = fRuns->GetNextObjectRange(range->fStart.fOffset, &length);
	range->fStart.fAtStart = (length <= count);
	if (((TXRun*) run)->IsTextRun())
		fPendingRun->Assign(run);
	return count;
}


// ROM 0x002535f4 GetKeyDownFlags__10TextensionFUs
// 3: a character to put in (anything from space up, tab, line feed,
// return); 0xd: an arrow; 0x17: a delete key that has something to take;
// 4: nothing to do.
unsigned int
Textension::GetKeyDownFlags(UniChar key)
{
	if (key > 0x1f || key == 9 || key == 0x0a || key == 0x0d)
		return 3;
	unsigned int flags;
	if (key >= 0x1c && key <= 0x1f)
		flags = 9;
	else
	{
		flags = 0;
		if (key == 8 || key == 0x1b)
		{
			TXOffsetRange range;
			fHilite->GetHiliteRange(&range);
			long n = range.fEnd.fOffset - range.fStart.fOffset;
			if (n != 0 || key == 8)
			{
				if (n == 0)
					n = AdvanceOffset(range.fStart.fOffset, false);
				if (n != 0)
					flags = 0x13;
			}
		}
	}
	return flags | 4;
}


// ROM 0x002536a0 KeyDown__10TextensionFPCUslT2Ui
// Characters (or, with the clear flag, nothing) replace the selection in
// the pending run's style; arrows move it.
void
Textension::KeyDown(const UniChar* chars, long count, long arrowFlags, unsigned int keyFlags)
{
	if ((keyFlags & 1) == 0)
		return;
	if (keyFlags & 8)
	{
		fHilite->ArrowKey((unsigned char) chars[0], arrowFlags);
		TXDisplay::fLastEditAction |= 0x40;
		fPendingRunInvalid = true;
		return;
	}
	TXOffsetRange range;
	fHilite->GetHiliteRange(&range);
	TXOffset end = range.fEnd.fOffset;
	TXOffset start = range.fStart.fOffset;
	Boolean atStart = false;		// (the ROM leaves this as the register had it on the typing path)
	if ((keyFlags & 0x10) == 0)
		UpdatePendingRun();
	else
	{
		if (ClearKeyDown(chars[0], &range) == 0)
			return;
		count = 0;
		if (end == start || !fFormatter->fLineEnds->IsRangeStart(range.fStart.fOffset, -1))
			atStart = range.fStart.fAtStart;
		else
			atStart = false;
	}
	TXTextDescriptor text;
	text.Set((UniChar*) chars, count);
	TXReplaceParams params(text, (TXRun*) fPendingRun, true);
	unsigned long flags = params.fFlags & ~6UL;
	if (count == 0)
	{
		if (!atStart)
			flags = params.fFlags & ~7UL;
	}
	else if (end != start)
		GetHiliteRangeWithoutSpaces(&range);
	params.fFlags = flags;
	ReplaceRange(range.fStart.fOffset, range.fEnd.fOffset, &params);
}


// ROM 0x002537fc Compact__10TextensionFv
// (The rulers' TXArray::Compact is inline in the ROM.)
NewtonErr
Textension::Compact(void)
{
	fChars->Compact();
	fFormatter->Compact();
	fRuns->Compact();
	return fRulers->TXArray::Compact();
}


// ROM 0x00253838 Activate__10TextensionFUcT1
// Made inactive, the document gives back the room it can.
NewtonErr
Textension::Activate(Boolean active, Boolean show)
{
	fDisplay->Activate(active, show);
	if (active)
		return noErr;
	fChars->Compact();
	fFormatter->Compact();
	fRuns->Compact();
	return fRulers->TXArray::Compact();
}


// ROM 0x00253870 SetHiliteRange__10TextensionFRC13TXOffsetRangeUcT2
void
Textension::SetHiliteRange(const TXOffsetRange& range, Boolean show, Boolean scroll)
{
	if (fHilite->SetHiliteRange(range, show, scroll))
		fPendingRunInvalid = true;
}


// ROM 0x002538c0 GetHiliteRangeWithoutSpaces__10TextensionFP13TXOffsetRange
// A selection made by words, less the spaces at its end (unless it is
// nothing else).
void
Textension::GetHiliteRangeWithoutSpaces(TXOffsetRange* range)
{
	fHilite->GetHiliteRange(range);
	if (!fHilite->fWordSelection)
		return;
	long n = range->fEnd.fOffset - range->fStart.fOffset;
	TXOffset end = range->fEnd.fOffset;
	while (n != 0)
	{
		if (!IsWordSpace(fChars->GetChar(end - 1)))
			break;
		end = end - 1;
		n--;
	}
	if (n != 0)
		range->fEnd.fOffset = end;
}


// ROM 0x0025394c GetContinuousAttrValues__10TextensionFP12TXAttrValues
// The attributes the selection's text runs share (the pending run's, for
// a caret); pictures are left out.
void
Textension::GetContinuousAttrValues(TXAttrValues* values)
{
	TXOffsetRange range;
	GetHiliteRangeWithoutSpaces(&range);
	if (range.fEnd.fOffset - range.fStart.fOffset == 0)
	{
		UpdatePendingRun()->GetAttributesValues(values);
		return;
	}
	TXObjectIterator runs(fRuns, range.fStart.fOffset);
	if (runs.fObject == nil)
		return;
	runs.fObject->GetAttributesValues(values);
	long left = (range.fEnd.fOffset - range.fStart.fOffset) - runs.fLength;
	while (left > 0 && values->GetCount() != 0)
	{
		runs.Next();
		long length = runs.fLength;
		TXOffsetRange here(runs.fOffset, runs.fOffset + runs.fLength, false, true);
		if (IsRangeGraphicsRun(&here) == nil)
			runs.fObject->GetCommonAttrValues(values);
		left -= length;
	}
}


// ROM 0x00253a44 UpdateFormatter__10TextensionFlRC13TXOffsetRangePlT3
// Attributes that change the layout (flags 1 or 2) reformat; the others
// only need the lines the range is on redrawn.
NewtonErr
Textension::UpdateFormatter(unsigned long changed, const TXOffsetRange& range, long* first, long* last)
{
	TXOffset end = range.fEnd.fOffset;
	TXOffset start = range.fStart.fOffset;
	if ((changed & 3) == 0)
	{
		long line = fFormatter->fLineEnds->OffsetToRangeIndex(start, false);
		*first = line;
		if (start == end)
			*last = line;
		else
			*last = fFormatter->fLineEnds->OffsetToRangeIndex(end, true);
		return noErr;
	}
	return fFormatter->Format(start, end, first, last);
}


// ROM 0x00253ae8 UpdateRangeRuns__10TextensionFRC13TXOffsetRangePC12TXAttrValuesl
// For a caret, the pending run changed; otherwise the runs of the range.
NewtonErr
Textension::UpdateRangeRuns(const TXOffsetRange& range, const TXAttrValues* values, long how)
{
	if (range.fEnd.fOffset == range.fStart.fOffset)
	{
		UpdatePendingRun();
		fPendingRun->Update(values, how);
		fHilite->Invalid(true);
		return noErr;
	}
	fPendingRunInvalid = true;
	TXEditInfo info;
	fDisplay->BeginEdit(&info);
	unsigned long changed = fRuns->UpdateRangeObjects(range.fStart.fOffset, range.fEnd.fOffset - range.fStart.fOffset, values, how);
	long first, last;
	NewtonErr err = UpdateFormatter(changed, range, &first, &last);
	EndEdit(info, first, last, nil);
	return err;
}


// ROM 0x00253c68 UpdateRangeRulers__10TextensionFRC13TXOffsetRangePC12TXAttrValuesl
// The rulers of the whole paragraphs the range touches.
NewtonErr
Textension::UpdateRangeRulers(const TXOffsetRange& range, const TXAttrValues* values, long how)
{
	TXOffsetRange paras = range;
	fRulers->CharRangeToParagRange(&paras.fStart, &paras.fEnd);
	TXEditInfo info;
	fDisplay->BeginEdit(&info);
	unsigned long changed = fRulers->UpdateRangeObjects(paras.fStart.fOffset, paras.fEnd.fOffset - paras.fStart.fOffset, values, how);
	long first, last;
	NewtonErr err = UpdateFormatter(changed, paras, &first, &last);
	EndEdit(info, first, last, nil);
	return err;
}
