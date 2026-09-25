/*
	File:		recognition/UnitPublic.cpp

	Contains:	TUnitPublic, the face of a unit the view system sees.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "UnitPublic.h"
#include "ShapeDomain.h"
#include "EdgeList.h"
#include "Recognizer.h"
#include "RootView.h"
#include "ViewFlags.h"
#include "Rects.h"
#include "StrokeCentral.h"
#include "WordList.h"
#include "WordUnit.h"
#include "WordInfo.h"
#include "StrokeBundle.h"
#include "Words.h"			// gWordID, LookupWord, ExpandWord
#include "Learning.h"			// gWordID, LookupWord, ExpandWord
#include "Unicode.h"
#include "Ports.h"			// RoundFixed
#include "Frames.h"
#include "RSSymbols.h"
#include "NewtonMemory.h"


// ROM 0x0022ced0 __ct__11TUnitPublicFP5TUnitUl
// Nothing made yet: the stroke face, shapes, word list and view hit come
// when asked for; the word base is unknown.
TUnitPublic::TUnitPublic(TUnit* unit, ULong /*unused*/)
{
	fWordInfo = AllocateRefHandle(NILREF);
	fWordInfo->stackPos = 0;
	fCleanShape = nil;
	fUnit = unit;
	fRoughShape = nil;
	fStroke = nil;
	fViewHit = nil;
	fWordList = nil;
	fWordInfo->ref = NILREF;
	fWordBase.top = (short) 0x8000;
	fWordBase.bottom = (short) 0x8000;
}


// ROM 0x0022cf60 __dt__11TUnitPublicFv
// The stroke face, the polygons, the word list and the word info gone
// (the unit itself is the recogniser's).
TUnitPublic::~TUnitPublic()
{
	if (fStroke != nil)
		delete fStroke;
	if (fCleanShape != nil)
		DisposHandle(fCleanShape);		// (the ROM: KillPoly)
	if (fRoughShape != nil)
		DisposHandle(fRoughShape);
	// NOT YET RECONSTRUCTED: TWordList
	DisposeRefHandle(fWordInfo);
}


// ROM 0x0022d678 GetType__11TUnitPublicFv
ULong
TUnitPublic::GetType(void)
{
	return fUnit->fType;
}


// ROM 0x0022d934 StartTime__11TUnitPublicFv
ULong
TUnitPublic::StartTime(void)
{
	return fUnit->fStartTime;
}


// ROM 0x0022da68 EndTime__11TUnitPublicFv
// The end of the last stroke the unit covers.
// NOT YET RECONSTRUCTED: the controller's stroke unit at fMaxStroke (its
// start plus its duration); the unit's own end time serves the host.
ULong
TUnitPublic::EndTime(void)
{
	return fUnit->EndTime();
}


// ROM 0x0022d18c ContextID__11TUnitPublicFv
ULong
TUnitPublic::ContextID(void)
{
	return fUnit->ContextID();
}


// ROM 0x0022dc28 Bounds__11TUnitPublicFP5TRect
// The unit's bounds in pixels, a pixel wider and taller than its box - the
// stroke's box for a recogniser flagged kRecognizerStrokeBounds.  With no
// recogniser for the type (or no stroke) the rect's top and bottom are
// left at -0x8000.
void
TUnitPublic::Bounds(Rect* rect)
{
	TRecognizer* recognizer = gRecognition.fRecognizers->FindRecognizer(GetType());
	rect->top = (short) 0x8000;
	rect->bottom = (short) 0x8000;
	if (recognizer == nil)
		return;
	if (!recognizer->TestFlags(kRecognizerStrokeBounds))
	{
		FRect box;
		fUnit->GetBBox(&box);
		UnfixRect(&box, rect);
	}
	else
	{
		TStrokePublic* stroke = Stroke();
		if (stroke == nil)
			return;
		UnfixRect(&stroke->fStroke->fBBox, rect);
	}
	rect->bottom++;
	rect->right++;
}


// ROM 0x0022db84 IsTap__11TUnitPublicFv
// A tap: the bounds under 6 pixels each way.
Boolean
TUnitPublic::IsTap(void)
{
	Rect bounds;
	Bounds(&bounds);
	return (UShort) (bounds.bottom - bounds.top) < 6 && (UShort) (bounds.right - bounds.left) < 6;
}


// ROM 0x0022dbdc Stroke__11TUnitPublicFv
// The face of the unit's first stroke, made once (the face does not own
// the stroke); nil when the unit has none.
TStrokePublic*
TUnitPublic::Stroke(void)
{
	if (fStroke == nil)
	{
		TStroke* stroke = fUnit->GetStroke(0);
		if (stroke != nil)
			fStroke = TStrokePublic::Make(stroke, false);
	}
	return fStroke;
}


// ROM 0x0022d940 FindView__11TUnitPublicFUl
// The view under the unit with the flags: the one found last is answered
// when it was found with the same flags; otherwise the view under the
// centre of the bounds, and failing that the closest within 10 pixels
// each way.  While the arbiter is arbitrating for the whole screen
// (NOT YET RECONSTRUCTED: gArbiter) the view under the screen's centre
// is taken instead.
TView*
TUnitPublic::FindView(ULong flags)
{
	if (fViewHit != nil && fViewHitFlags == flags)
		return fViewHit;
	Rect bounds;
	Bounds(&bounds);
	TView* view = gRootView->FindView(MidPoint(bounds), flags, nil);
	if (view == nil)
	{
		Point slop = MakePoint(10, 10);
		view = gRootView->FindView(MidPoint(bounds), flags, &slop);
	}
	SetViewHit(view, flags);
	return view;
}


// ROM 0x0022daa0 SetViewHit__11TUnitPublicFP5TViewUl
void
TUnitPublic::SetViewHit(TView* view, ULong flags)
{
	fViewHit = view;
	fViewHitFlags = flags;
}


// ROM 0x0022daac InputMask__11TUnitPublicFv
// The recognition bits of the view under the unit (found with the
// recogniser's required mask); 0 for no view.
ULong
TUnitPublic::InputMask(void)
{
	TView* view = FindView(RequiredMask());
	return view != nil ? view->fFlags & vRecognitionAllowed : 0;
}


// ROM 0x0022db28 RequiredMask__11TUnitPublicFv
// The services the unit's recogniser has enabled, as view flags; a stroke
// recogniser's take the shape and word bits along, a gesture recogniser's
// the clicks.
ULong
TUnitPublic::RequiredMask(void)
{
	ULong mask = 0;
	TRecognizer* recognizer = gRecognition.fRecognizers->FindRecognizer(fUnit->fType);
	if (recognizer != nil)
		mask = recognizer->ServicesEnabled();
	if (mask & vStrokesAllowed)
		mask |= 0x17ef000;
	if (mask & vGesturesAllowed)
		mask |= vClickable;
	return mask;
}


// ROM 0x0022dae8 Cleanup__11TUnitPublicFv
// A click's ink taken off the screen once it is handled, and the stroke
// world's current stroke forgotten.
void
TUnitPublic::Cleanup(void)
{
	if (fUnit->fType != kClickUnit)
		return;
	Stroke()->InkOff(true);
	gStrokeWorld.InvalidateCurrentStroke();
}


// ROM 0x0022dcfc Invalidate__11TUnitPublicFv
// What the unit's strokes inked given to the root view to redraw: with no
// stroke face yet, the bounds (let out for the ink) and every stroke's
// rect - the strokes not drawn are marked to draw no ink and their rects
// go to the screen-dirty region, the drawn ones' rects are invalidated;
// with a face, its inked rect unless the stroke draws no ink.
void
TUnitPublic::Invalidate(void)
{
	if (fStroke == nil)
	{
		Rect bounds;
		Bounds(&bounds);
		AdjustForInk(&bounds);
		gRootView->SmartInvalidate(bounds);
		Rect drawn, notDrawn;
		SetEmptyRect(&drawn);
		SetEmptyRect(&notDrawn);
		ULong count = fUnit->CountStrokes();
		for (ULong i = 0; i < count; i++)
		{
			TStroke* stroke = fUnit->GetStroke(i);
			Rect rect;
			GetStrokeRect(stroke, &rect);
			AdjustForInk(&rect);
			if (!stroke->TestFlags(kStrokeDrawn))
			{
				UnionRect(&notDrawn, &rect, &notDrawn);
				stroke->SetFlags(kStrokeNoInk);
			}
			else
				UnionRect(&drawn, &rect, &drawn);
		}
		if (!EmptyRect(&drawn))
			gRootView->SmartInvalidate(drawn);
		if (!EmptyRect(&notDrawn))
			gRootView->SmartScreenDirty(notDrawn);
	}
	else if (!fStroke->fStroke->TestFlags(kStrokeNoInk))
	{
		Rect inked;
		fStroke->GetInkedRect(&inked);
		gRootView->SmartInvalidate(inked);
	}
}


// ROM 0x0022cfc8 CaretType__11TUnitPublicFv
// The first interpretation's label when it is one of the caret gestures
// (2, 3, 5, 6); else 0.
long
TUnitPublic::CaretType(void)
{
	long label = ((TSIUnit*) fUnit)->GetInterpretation(0)->label;
	if (label != 2 && label != 3 && label != 5 && label != 6)
		label = 0;
	return label;
}


// ROM 0x0022d01c GesturePoint__11TUnitPublicFl
// A corner of the gesture's polyline as a pixel point, rounded.  The
// caret and line gestures are placed by these: the point of the caret,
// where its arms end, and the ends of a line.
Point
TUnitPublic::GesturePoint(long index)
{
	TDArray* corners = ((TEdgeListUnit*) fUnit)->GetCorners();
	const FPoint* corner = (const FPoint*) corners->GetEntry((ULong) index);
	Point pt;
	pt.v = (short) ((corner->y + 0x8000) >> 16);
	pt.h = (short) ((corner->x + 0x8000) >> 16);
	return pt;
}


// ROM 0x0022d0a8 CountGesturePoints__FP11TUnitPublic
// The corners of the unit's polyline (a gesture unit's interpretation),
// which is what the gesture was recognised from.
long
CountGesturePoints(TUnitPublic* unit)
{
	return ((TEdgeListUnit*) unit->fUnit)->GetCorners()->fCount;
}


// ROM 0x0022d0b8 GestureAngle__11TUnitPublicFv
// The first interpretation's angle (Fixed, rounded to degrees), snapped to
// 0, 90, -90, 180 or 135 when within the tolerance - 30 degrees for label
// 5, 20 for the rest; otherwise as it is.
long
TUnitPublic::GestureAngle(void)
{
	UnitInterpretation* interp = ((TSIUnit*) fUnit)->GetInterpretation(0);
	long angle = (short) ((interp->angle + 0x8000) >> 16);
	long tolerance = interp->label == 5 ? 30 : 20;
	long diff = angle < 0 ? -angle : angle;
	if (diff <= tolerance)
		return 0;
	diff = angle - 90 < 0 ? 90 - angle : angle - 90;
	if (diff <= tolerance)
		return 90;
	diff = angle + 90 < 0 ? -(angle + 90) : angle + 90;
	if (diff <= tolerance)
		return -90;
	diff = angle - 180 < 0 ? 180 - angle : angle - 180;
	if (diff <= tolerance)
		return 180;
	diff = angle - 135 < 0 ? 135 - angle : angle - 135;
	if (diff <= tolerance)
		return 135;
	diff = angle + 180 < 0 ? -(angle + 180) : angle + 180;
	if (diff <= tolerance)
		return 180;
	return angle;
}


/*------------------------------------------------------------------------------
	W h a t   w a s   r e a d
------------------------------------------------------------------------------*/

// ROM 0x0022d268 MakeWordList__11TUnitPublicFUcT1
// The unit's interpretations gathered into a word list.  Only the unit
// type the word recogniser in use makes has readings to gather, so any
// other answers nothing at all.
//
// The list is built in two passes, which is what orders it: with `raw`
// false, the first pass takes every reading *except* an ordinary word
// the dictionaries have never heard of, and the second takes exactly
// those - so what the machine knows comes before what it is guessing
// at.  A two-character reading ending in '.' or ')' is left alone,
// being an abbreviation or a list marker rather than a word, and so is
// anything that does not start with a letter.
//
// `raw` says the readings are to be taken as they come: no variant of a
// word is offered alongside it and no capital is forced, and only
// single-character readings are considered at all.  The passes then
// split on the try string instead of on the dictionaries.
//
// Five readings is as many as are kept, and a list that ends up with
// none at all gets one empty reading scoring 1000 - which is what a
// unit that was never read comes to, and what makes it ink.
//
// (NOT YET: `LookupWord` and `ExpandWord` are the dictionaries', which
//  are NOT YET - see Words.cpp.  With no dictionary nothing is found,
//  so every ordinary word falls through to the second pass and no
//  variants are offered; the same readings come out, in the order a
//  machine with an empty dictionary would put them.)
TWordList*
TUnitPublic::MakeWordList(Boolean raw, Boolean tryString)
{
	if (GetType() != gWordID)
		return nil;
	TWordList* list = new TWordList;
	if (list == nil)
		return nil;

	long count = fUnit->InterpretationCount();
	long added = 0;
	ULong mask = InputMask();
	// a field that is restricted in some way and asks for capitals
	Boolean upperFirst = ((mask & vAnythingAllowed) != vAnythingAllowed)
						 && (mask & vCapsRequired) != 0;
	// a field that takes numbers and nothing wordlike
	Boolean numbersOnly = (mask & vNumbersAllowed) != 0
						  && (mask & (vCharsAllowed | vLettersAllowed | vMathAllowed
									  | vPhoneField | vDateField | vTimeField
									  | vAddressField | vNameField
									  | vCustomDictionaries)) == 0;

	for (long pass = 0; pass < 2; pass++)
	{
		for (long i = 0; i < count; i++)
		{
			// (the ROM dispatches straight through the vtable: the type gate
			//  above says the unit is the word recogniser's own, which is a
			//  TStdWordUnit)
			TStdWordUnit* unit = (TStdWordUnit*) fUnit;
			UnitInterpretation* interp = unit->GetInterpretation((ULong) i);
			long score = interp->score;
			long label = interp->label;
			if (label == -1)
				continue;
			Handle word = unit->GetString((ULong) i);
			long length = Ustrlen((UniChar*) *word);
			UniChar first = *(UniChar*) *word;
			if (first == 0)
				continue;

			if (!raw)
			{
				// an ordinary word that is neither an abbreviation nor
				// something starting with a digit is taken in the pass
				// its being in the dictionaries puts it in; everything
				// else belongs to the first pass
				Boolean wanted = (pass == 0);
				if (label == kWordLabelWord
					&& !(length == 2 && (((UniChar*) *word)[1] == '.'
										 || ((UniChar*) *word)[1] == ')'))
					&& IsAlphabet(first))
				{
					ULong junk;
					Boolean known = (LookupWord((UniChar*) *word, &junk) != -1);
					wanted = (known == (pass == 0));
				}
				if (!wanted)
					continue;
				if (upperFirst)
					UppercaseText((UniChar*) *word, 1);
			}
			else
			{
				if (label != kWordLabelWord || length > 1
					|| (tryString && InTryString(first) == (pass == 0))
					|| (numbersOnly && !IsDigit(first)))
					continue;
			}

			if (word == nil)
				break;
			if (!raw)
			{
				// the word as the dictionaries would expand it goes in
				// ahead of the word itself
				HLock(word);
				Handle variant = (Handle) ExpandWord((UniChar*) *word);
				if (variant != nil)
				{
					if (list->Find((UniChar**) variant) < 0)
					{
						list->InsertLast((UniChar**) variant, score, label);
						added++;
					}
					// (BUG, kept: the unlock is inside this arm, so a
					//  word with no variant stays locked for ever)
					HUnlock(word);
					DisposHandle(variant);
				}
			}
			if (list->Find((UniChar**) word) < 0)
			{
				list->InsertLast((UniChar**) word, score, label);
				added++;
			}
			if (added >= 5)
				break;
		}

		if (!raw)
		{
			if (numbersOnly)
				break;
		}
		else
		{
			// one character, written in one stroke, with one character
			// in the try string: the guesses are reordered by what the
			// writer has lately been choosing
			if (tryString && pass == 0 && TryStringLength() == 1
				&& fUnit->CountStrokes() == 1)
				list->Reorder();
			if (!tryString)
				break;
		}
	}

	if (added == 0)
	{
		UniChar empty[1];
		empty[0] = 0;
		UniChar* p = empty;
		list->InsertLast(&p, 1000, -1);
	}
	return list;
}


// ROM 0x0022d6c4 ExtractWords__11TUnitPublicFv
// The word list made once and kept.
void
TUnitPublic::ExtractWords(void)
{
	if (fWordList == nil)
		fWordList = MakeWordList(false, false);
}


// ROM 0x0022d6f8 Word__11TUnitPublicFv
Handle
TUnitPublic::Word(void)
{
	ExtractWords();
	return fWordList->Word(0);
}


// ROM 0x0022d71c WordScore__11TUnitPublicFv
ULong
TUnitPublic::WordScore(void)
{
	ExtractWords();
	return (ULong) fWordList->Score(0);
}


// ROM 0x0022d740 Words__11TUnitPublicFv
// The word list handed over rather than lent: whoever asks owns it, and
// the unit will make another if it is asked again.
TWordList*
TUnitPublic::Words(void)
{
	ExtractWords();
	TWordList* list = fWordList;
	fWordList = nil;
	return list;
}


// ROM 0x0022d684 WordInfo__11TUnitPublicFv
// The word info frame, made once per unit.
Ref
TUnitPublic::WordInfo(void)
{
	if (ISNIL(fWordInfo->ref))
		fWordInfo->ref = MakeWordInfo(this);
	return fWordInfo->ref;
}


// ROM 0x0022d764 SetWordBase__11TUnitPublicFv
// Where the writing stands, remembered as a rectangle from the left end
// of the base line to the right - which is a line rather than a box,
// the "top" being the left end's height and the "bottom" the right
// end's, so that writing running uphill can be laid out along its own
// slope.
//
// A single '?' or '!' is the exception: it has a descender a recogniser
// measures the base from, so the base it answers is too low.  The
// bottom of the unit's bounds is used for both ends instead.
void
TUnitPublic::SetWordBase(void)
{
	FPoint left;
	FPoint right;
	((TStdWordUnit*) fUnit)->GetWordBase(&left, &right, 0);
	fWordBase.top = (short) RoundFixed(left.y);
	fWordBase.left = (short) RoundFixed(left.x);
	fWordBase.bottom = (short) RoundFixed(right.y);
	fWordBase.right = (short) RoundFixed(right.x);

	Handle word = ((TStdWordUnit*) fUnit)->GetString(0);
	if (word == nil)
		return;
	if (Ustrlen((UniChar*) *word) != 1)
		return;
	UniChar c = *(UniChar*) *word;
	if (c != '?' && c != '!')
		return;
	Rect bounds;
	Bounds(&bounds);
	fWordBase.bottom = bounds.bottom;
	fWordBase.top = bounds.bottom;
}


// ROM 0x0022d8b4 TrainingData__11TUnitPublicFv
// What the recogniser wants kept about how this was written, so that it
// can learn from it - nothing at all unless the user has asked for the
// learning to be saved.
Ref
TUnitPublic::TrainingData(void)
{
	RefVar data(NILREF);
	if (gSaveWordTrainingData)
	{
		TRecognizer* recognizer =
			gRecognition.fRecognizers->FindRecognizer(GetType());
		if (recognizer != nil)
			data = recognizer->GetLearningData(this);
	}
	return data;
}


// ROM 0x0022d870 Strokes__11TUnitPublicFv
// The word's strokes as a stroke bundle: the word info frame's strokes
// slot.
Ref
TUnitPublic::Strokes(void)
{
	RefVar info(WordInfo());
	return GetFrameSlot(info, RSSYMstrokes);
}


// ROM 0x0022d1d8 CleanShape__11TUnitPublicFv
// A shape unit's fitted shape as a polygon, made once and kept (nil for
// any other unit, or a shape with nothing to draw).
Handle
TUnitPublic::CleanShape(void)
{
	if (fCleanShape == nil && fUnit->fType == kShapeUnit)
	{
		TStroke* stroke = ((TGeneralShapeUnit*) fUnit)->GetGSAsStroke();
		if (stroke != nil)
		{
			fCleanShape = AsPolygon(stroke);
			stroke->Dispose();
		}
	}
	return fCleanShape;
}


// ROM 0x0022d234 ShapeType__11TUnitPublicFv
// A shape unit's type (its label: ShapeDomain.h's kShape...), nought for
// any other unit.
ULong
TUnitPublic::ShapeType(void)
{
	if (fUnit->fType != kShapeUnit)
		return 0;
	return (ULong) ((TSIUnit*) fUnit)->GetLabel(0);
}


// ROM 0x0022d198 RoughShape__11TUnitPublicFv
// The unit's first stroke as a polygon, made once and kept.  It is the
// outline a hilite stroke is walked along (TParagraphView::HiliteRange)
// and what the shape recogniser would fit a shape to.
Handle
TUnitPublic::RoughShape(void)
{
	if (fRoughShape == nil)
		fRoughShape = AsPolygon(fUnit->GetStroke(0));
	return fRoughShape;
}
