/*
	File:		views/Rerecognize.cpp

	Contains:	Deferred recognition - writing already on a view read
				again.  See Rerecognize.h.
*/

#include <stdio.h>
#include "Rerecognize.h"
#include "ParagraphView.h"
#include "PolygonView.h"
#include "RootView.h"
#include "Commands.h"
#include "Application.h"
#include "DrawShape.h"
#include "StyleRuns.h"
#include "Controller.h"
#include "Areas.h"
#include "Unit.h"
#include "UnitPublic.h"
#include "Recognizer.h"
#include "StrokeCentral.h"
#include "StrokeBundle.h"
#include "CorrectInfo.h"
#include "RecConfig.h"
#include "Words.h"
#include "Stroke.h"
#include "Ink.h"
#include "InkShapes.h"
#include "Pictures.h"
#include "Screen.h"
#include "Ports.h"
#include "Locale.h"
#include "RichString.h"
#include "Unicode.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"

StrokeCentral*	gBulkStrokes = nil;					// ROM 0x0c1008a4 gBulkStrokes
static Boolean	gCheckmarkUp = false;				// (the ROM's byte at 0x0c101730) which of the two arrows DrawCheckmark shows next


// ROM 0x001714c8 DrawCheckmark__FR5TRect
// The arrow the machine shows over something it is reading: an outline
// 26 wide and 15 tall centred on the box's top edge and the arrow inside
// it, pointing down and up by turns so that it seems to work.
void
DrawCheckmark(Rect& bounds)
{
	RefVar inside(NILREF);
	RefVar outside(NILREF);
	if (gCheckmarkUp)
	{
		outside = Rrecogarrowupoutside;
		inside = Rrecogarrowupinside;
	}
	else
	{
		outside = Rrecogarrowdownoutside;
		inside = Rrecogarrowdowninside;
	}
	short middle = (short) ((bounds.left + bounds.right) / 2);
	Rect outer, inner;
	outer.top = bounds.top;
	outer.left = middle - 13;
	outer.right = outer.left + 26;
	outer.bottom = outer.top + 15;
	inner.left = middle - 11;
	inner.top = outer.top + 1;
	inner.right = inner.left + 23;
	inner.bottom = inner.top + 13;
	StartDrawing(nil, nil);
	DrawBitmap(RefVar(GetFrameSlotRef(outside, RSSYMbits)), &outer, 1);
	DrawBitmap(RefVar(GetFrameSlotRef(inside, RSSYMbits)), &inner, 3);
	StopDrawing(nil, nil);
	gRootView->SmartInvalidate(outer);
	gCheckmarkUp = !gCheckmarkUp;
}


#pragma mark - reading a view's ink again

// ROM 0x00035bf8 PolygonWordHandler__FP5TUnitUl
// A word read out of an ink shape: put down on the page as a word (the
// command the page takes a word with no script), and the shape removed.
// Anything that is not a word, or a word nobody could read, leaves the
// shape as it was.
ULong
PolygonWordHandler(TUnit* unit, ULong arg)
{
	TView* view = (TView*) arg;
	newton_try
	{
		if (unit->fType == gWordID)
		{
			TUnitPublic pub(unit, 0);
			pub.SetWordBase();
			RefVar info(pub.WordInfo());
			if (Length(GetFrameSlotRef(info, RSSYMwords)) != 0)
			{
				PostAndDoCommand(aeWord17, &pub, 0x1000);
				RefVar cmd(MakeCommand(aeRemoveData, view->fParent, view->fId));
				gApplication->DispatchCommand(cmd);
			}
		}
	}
	newton_catch("evt.ex")
	{
		SafeExceptionNotify(CurrentException());
	}
	end_try;
	return 0;
}


// ROM 0x00035d4c RerecognizeWord__FP12TPolygonViewP8TRecArea
// The shape's ink taken apart into strokes, moved to where the shape is
// on the page, and read in the area.
void
RerecognizeWord(TPolygonView* view, TRecArea* area)
{
	RefVar ink(view->GetProto(RSSYMink));
	if (ISNIL(ink))
		return;
	TStroke** strokes = InkExpand(ink, 0, 0, 0);
	if (strokes == nil)
		return;
	// DEVIATION: an entry holds a host pointer (the ROM's are four bytes)
	TArray* list = TDArray::Make(sizeof(TStroke*), 0);
	if (list != nil)
	{
		for (long i = 0; strokes[i] != nil; i++)
		{
			strokes[i]->Offset(ToFixed(view->viewBounds.left), ToFixed(view->viewBounds.top));
			*(TStroke**) list->AddEntry() = strokes[i];		// (the ROM does not check the entry either)
		}
		if (area != nil)
			gController->RecognizeInArea(list, area, PolygonWordHandler, (ULong) view);
		list->Dispose();
	}
	DisposPtr((Ptr) strokes);
}


// ROM 0x00035e70 ParagraphViewWordHandler__FP5TUnitUl
// A word read out of a paragraph's ink word put in its place - the
// command's start and stop (a length) saying where - through the path
// everything put into a paragraph takes, and the command's stop made
// the length of what went in.  A word nobody could read goes back in as
// its word info when the paragraph keeps ink words; in one that does not,
// or for anything that is not a word at all, the ink word is replaced by
// nothing.
ULong
ParagraphViewWordHandler(TUnit* unit, ULong arg)
{
	RefVar cmd(*(RefVar*) arg);
	newton_try
	{
		TUnitPublic pub(unit, 0);
		TView* view = (TView*) CommandReceiver(cmd);
		Boolean allowsInkWords = ViewAllowsInkWords(view);
		Boolean isWord = unit->fType == gWordID;
		if (isWord || !allowsInkWords)
		{
			RefVar item(NILREF);
			Long start = RINT(GetFrameSlotRef(cmd, RSSYMstart));
			Long stop = RINT(GetFrameSlotRef(cmd, RSSYMstop));
			if (isWord)
			{
				pub.SetWordBase();
				RefVar info(pub.WordInfo());
				long count = Length(GetFrameSlotRef(info, RSSYMwords));
				if (allowsInkWords || count != 0)
					item = info;
			}
			if (ISNIL(item))
				item = MakeString("");
			RefVar style(((TParagraphView*) view)->GetStyleForInsertion(start, false, true));
			RefVar spec(DoInsertItems(view, item, false, true, start, stop, true, style));
			SetFrameSlot(cmd, RSSYMstop, RefVar(GetFrameSlotRef(spec, RSSYMreplacechars)));
		}
	}
	newton_catch("evt.ex")
	{
		SafeExceptionNotify(CurrentException());
	}
	end_try;
	return 0;
}


// ROM 0x00036110 RerecognizeWord__FP14TParagraphViewRC6RefVarP8TRecArea
// The ink word at the command's start taken apart into strokes and read
// in the area.  (The ROM reads the command's stop as well and drops it.)
void
RerecognizeWord(TParagraphView* view, RefArg cmd, TRecArea* area)
{
	Long start = RINT(GetFrameSlotRef(cmd, RSSYMstart));
	RINT(GetFrameSlotRef(cmd, RSSYMstop));
	RefVar ink(view->GetStyleAtOffset(start, nil, nil));
	TStroke** strokes = InkExpand(ink, 0, 0, 0);
	if (strokes == nil)
		return;
	TArray* list = TDArray::Make(sizeof(TStroke*), 0);		// DEVIATION: host pointers
	if (list != nil)
	{
		for (long i = 0; strokes[i] != nil; i++)
			*(TStroke**) list->AddEntry() = strokes[i];
		if (area != nil)
		{
			RefVar command(cmd);
			gController->RecognizeInArea(list, area, ParagraphViewWordHandler, (ULong) &command);
		}
		list->Dispose();
	}
	DisposPtr((Ptr) strokes);
}


#pragma mark - reading a script's strokes

// ROM 0x00036340 HandleBulkStrokes__FRC6RefVarT1
// A group of strokes nobody read, as a stroke bundle: added to the
// correct info as a word info with no words.  ROM QUIRK: AddWordInfo
// keeps only word infos whose first reading is a word, so this one is
// dropped and Recognize answers nothing for strokes it could not read,
// though the ink grouping has run.
void
HandleBulkStrokes(RefArg correctInfo, RefArg bundle)
{
	RefVar info(Clone(RefVar(Rprotowordinfo)));
	SetFrameSlot(info, RSSYMstrokes, bundle);
	AddWordInfo(correctInfo, info);
}


// ROM 0x00036398 BulkUnitHandler__FP5TUnitUl
// A word read: its word info added to the correct info.  Anything else
// is a stroke nobody read, handed to the bulk stroke world to be grouped
// into ink (which comes back through HandleBulkStrokes).
ULong
BulkUnitHandler(TUnit* unit, ULong arg)
{
	RefVar correctInfo(*(RefVar*) arg);
	if (unit->fType == gWordID)
	{
		TUnitPublic pub(unit, 0);
		RefVar info(pub.WordInfo());
		AddWordInfo(correctInfo, info);
	}
	else
		gBulkStrokes->AddExpiredStroke((TStrokeUnit*) unit);
	return 0;
}


// A stroke bundle's strokes, or an ink shape's; nil for anything else.
static TStroke**
StrokesOf(RefArg item)
{
	if (EQRef(ClassOf(item), RSSYMstrokebundle))
		return StrokeBundleToTStrokes(item);
	if (FrameHasSlotRef(item, RSSYMink))
		return GetPolyAsTStrokes(item, 0);
	return nil;
}


// ROM 0x00036448 RecognizeStrokes__FRC6RefVarT1Uc
// An array of stroke bundles and ink shapes read in an area built out of
// the configuration.  `together` reads all their strokes as one piece of
// writing; otherwise each is read on its own, and the word info the same
// index names (the ROM counts one per item, which is only so when each
// item was one word) is given the item's ink.  ==> a correct info frame
// with a word info for each word read, or nil when an item could not be
// taken apart or there was no room.
Ref
RecognizeStrokes(RefArg strokes, RefArg config, Boolean together)
{
	RefVar holder(NILREF);			// (the ROM's RefHandle the handler is given the address of)
	Boolean failed = false;
	TRecArea* area = MakeRerecognizeArea(gController, config);
	RefVar correctInfo(NewCorrectInfo());
	holder = correctInfo;
	gBulkStrokes = StrokeCentral::New();
	gBulkStrokes->fExpireProc = HandleBulkStrokes;
	*gBulkStrokes->fCompressBundle = correctInfo;
	if (!together)
	{
		for (long i = 0; i < Length(strokes) && !failed; i++)
		{
			RefVar item(GetArraySlotRef(strokes, i));
			TStroke** list = StrokesOf(item);
			TArray* array = TDArray::Make(sizeof(TStroke*), 0);	// DEVIATION: host pointers
			if (list == nil || array == nil || area == nil)
				failed = true;
			else
			{
				for (long j = 0; list[j] != nil; j++)
				{
					TStroke** entry = (TStroke**) array->AddEntry();
					if (entry == nil)
						failed = true;
					else
						*entry = list[j];
				}
				if (!failed)
				{
					gController->RecognizeInArea(array, area, BulkUnitHandler, (ULong) &holder);
					gBulkStrokes->ExpireAll();
					if (FrameHasSlotRef(item, RSSYMink))
					{
						RefVar infos(GetFrameSlotRef(correctInfo, RSSYMinfo));
						if (i < Length(infos))
						{
							infos = GetArraySlotRef(infos, i);
							SetFrameSlot(infos, RSSYMink, RefVar(GetFrameSlotRef(item, RSSYMink)));
						}
					}
				}
			}
			gBulkStrokes->ExpireAll();
			if (list != nil)
				DisposPtr((Ptr) list);
			if (array != nil)
				array->Dispose();
		}
	}
	else
	{
		TArray* array = TDArray::Make(sizeof(TStroke*), 0);	// DEVIATION: host pointers
		for (long i = 0; i < Length(strokes) && !failed; i++)
		{
			RefVar item(GetArraySlotRef(strokes, i));
			TStroke** list = StrokesOf(item);
			if (list == nil || array == nil || area == nil)
				failed = true;
			else
			{
				for (long j = 0; list[j] != nil && !failed; j++)
				{
					TStroke** entry = (TStroke**) array->AddEntry();
					if (entry == nil)
						failed = true;
					else
						*entry = list[j];
				}
			}
			if (list != nil)
				DisposPtr((Ptr) list);
		}
		if (!failed)
			gController->RecognizeInArea(array, area, BulkUnitHandler, (ULong) &holder);
		if (array != nil)
			array->Dispose();
	}
	gBulkStrokes->ExpireAll();
	if (area != nil)
		area->Dispose();
	if (failed)
		correctInfo = NILREF;
	if (gBulkStrokes != nil)
		delete gBulkStrokes;
	gBulkStrokes = nil;
	return correctInfo;
}


// ROM 0x00036938 FRecognize
// Recognize(strokes, config, together)
static Ref
FRecognize(RefArg rcvr, RefArg strokes, RefArg config, RefArg together)
{
	return RecognizeStrokes(strokes, config, NOTNIL(together));
}


#pragma mark - reading ink words in text

// ROM 0x0017ddcc RecognizeInkWord__FRC6RefVar
// One ink word read on its own: made into an ink shape as tall as the
// word and as wide, expanded into a stroke bundle and read.  ==> the
// first word info's words; nil when nothing came back, and - a quirk
// kept - the empty info array itself when the correct info has no word
// info at all.
Ref
RecognizeInkWord(RefArg ink)
{
	RefVar result(NILREF);
	RefVar strokes(MakeArray(1));
	InkWordInfo info;
	GetInkWordInfo(ink, &info);
	Rect box;
	box.top = 0;
	box.left = 0;
	box.bottom = (short) (info.fAscent + info.fDescent);
	box.right = (short) info.fWidth;
	result = MakePolygonForm(nil, 0, 14, box, 1);
	SetFrameSlot(result, RSSYMink, ink);
	result = ExpandInk(result, 0);
	SetArraySlotRef(strokes, 0, result);
	result = RecognizeStrokes(strokes, RefVar(NILREF), false);
	if (NOTNIL(result))
	{
		result = GetFrameSlotRef(result, RSSYMinfo);
		if (NOTNIL(result) && Length(result) > 0)
		{
			result = GetArraySlotRef(result, 0);
			result = GetFrameSlotRef(result, RSSYMwords);
			if (Length(result) <= 0)
				result = NILREF;
		}
	}
	return result;
}


// ROM 0x0017df70 RecognizeTextInStyles__FRC6RefVarT1
// A text-and-styles frame with every ink word read: each one's run
// replaced by the first reading (as long as it is) in the last font the
// runs before it named - or the font given, or the user's font - and the
// runs merged where they now agree.  A frame with no ink words comes back
// as it was; one with any comes back as a new canonicalTextAndStyles.
// ROM BUG (latent): the text's characters are read through a pointer
// taken before the ink words are read, which allocates - a collection
// that moved the string would leave it pointing at the old place.
Ref
RecognizeTextInStyles(RefArg textAndStyles, RefArg fontSpec)
{
	RefVar result(textAndStyles);
	RefVar text(GetFrameSlotRef(textAndStyles, RSSYMtext));
	UniChar* chars = GetCString(text);
	RefVar font(fontSpec);
	if (ISNIL(fontSpec))
		font = GetPreference(RSSYMuserfont);
	RefVar styles(GetFrameSlotRef(textAndStyles, RSSYMstyles));
	if (NOTNIL(styles))
	{
		RefVar newText(NILREF);
		RefVar newStyles(NILREF);
		RefVar run(NILREF);
		long textOut = 0;
		long textIn = 0;
		Boolean made = false;
		long runs = Length(styles) / 2;
		for (long i = 0; i < runs; i++)
		{
			Long length = RINT(GetArraySlotRef(styles, i * 2));
			run = GetArraySlotRef(styles, i * 2 + 1);
			if (!IsInkWord(run))
			{
				if (ISINT(run) || IsFontFrame(run))
					font = run;
				if (made)
				{
					SetLength(newText, (textOut + length + 1) * (long) sizeof(UniChar));
					Ustrncpy(GetCString(newText) + textOut, chars + textIn, length);
				}
				textOut += length;
			}
			else
			{
				UniChar* word = nil;
				long wordLength = 0;
				text = RecognizeInkWord(run);
				if (NOTNIL(text))
				{
					text = GetArraySlotRef(text, 0);
					text = GetFrameSlotRef(text, RSSYMword);
					if (NOTNIL(text))
					{
						word = GetCString(text);
						wordLength = Ustrlen(word);
					}
				}
				long newLength = textOut + wordLength;
				long size = (newLength + 1) * (long) sizeof(UniChar);
				if (made)
					SetLength(newText, size);
				else
				{
					made = true;
					newText = AllocateBinary(RSSYMstring, size);
					Ustrncpy(GetCString(newText), chars, textIn);
					newStyles = Clone(styles);
				}
				if (wordLength > 0)
				{
					Ustrncpy(GetCString(newText) + textOut, word, wordLength);
					textOut = newLength;
				}
				SetArraySlotRef(newStyles, i * 2, MAKEINT(wordLength));
				SetArraySlotRef(newStyles, i * 2 + 1, font);
			}
			textIn += length;
		}
		if (made)
		{
			result = Clone(RefVar(Rcanonicaltextandstyles));
			CompactStyleRuns(newStyles);
			SetFrameSlot(result, RSSYMstyles, newStyles);
			SetFrameSlot(result, RSSYMtext, newText);
		}
	}
	return result;
}


// ROM 0x001febfc FRecognizeInkWord
static Ref
FRecognizeInkWord(RefArg rcvr, RefArg ink)
{
	return RecognizeInkWord(ink);
}


// ROM 0x001fec04 FRecognizeTextInStyles
static Ref
FRecognizeTextInStyles(RefArg rcvr, RefArg textAndStyles, RefArg fontSpec)
{
	return RecognizeTextInStyles(textAndStyles, fontSpec);
}


#pragma mark - asking a view

// ROM 0x001a269c RecognizePara__FP5TViewlT2UcRC6RefVar
// The paragraph asked to read the ink words between the offsets.  ==>
// where the range ends now.
long
RecognizePara(TView* view, long start, long stop, Boolean doHilite, RefArg config)
{
	RefVar cmd(Clone(RefVar(Rprotocommand)));
	SetFrameSlot(cmd, RSSYMid, RefVar(MAKEINT(aeRecognizeRange)));
	SetFrameSlot(cmd, RSSYMreceiver, RefVar(view->fContext));
	SetFrameSlot(cmd, RSSYMstart, RefVar(MAKEINT(start)));
	SetFrameSlot(cmd, RSSYMstop, RefVar(MAKEINT(stop)));
	SetFrameSlot(cmd, RSSYMdohilite, RefVar(MAKEBOOLEAN(doHilite)));
	SetFrameSlot(cmd, RSSYMrecconfig, config);
	gApplication->DispatchCommand(cmd);
	return RINT(GetFrameSlotRef(cmd, RSSYMstop));
}


// ROM 0x001a27f4 RecognizePoly__FP5TViewUcRC6RefVar
// An ink shape asked to read its ink.
void
RecognizePoly(TView* view, Boolean doHilite, RefArg config)
{
	RefVar cmd(MakeCommand(aeRecognizeInk, view, view->fId));
	SetFrameSlot(cmd, RSSYMdohilite, RefVar(MAKEBOOLEAN(doHilite)));
	SetFrameSlot(cmd, RSSYMrecconfig, config);
	gApplication->DispatchCommand(cmd);
}


// ROM 0x0019ff5c FRecognizePara
// RecognizePara(view, start, stop, doHilite, config) ==> the range's new end
static Ref
FRecognizePara(RefArg rcvr, RefArg view, RefArg start, RefArg stop, RefArg doHilite, RefArg config)
{
	TView* theView = FailGetView(view);
	Boolean hilite = NOTNIL(doHilite);
	Long end = RINT(stop);
	Long begin = RINT(start);
	return MAKEINT(RecognizePara(theView, begin, end, hilite, config));
}


// ROM 0x0019ffec FRecognizePoly
// RecognizePoly(view, doHilite, config)
static Ref
FRecognizePoly(RefArg rcvr, RefArg view, RefArg doHilite, RefArg config)
{
	TView* theView = FailGetView(view);
	RecognizePoly(theView, NOTNIL(doHilite), config);
	return NILREF;
}


void
RegisterRerecognizeNatives(void)
{
	RegisterNativeFunction("FRecognize", (void*) FRecognize, 3);
	RegisterNativeFunction("FRecognizeInkWord", (void*) FRecognizeInkWord, 1);
	RegisterNativeFunction("FRecognizeTextInStyles", (void*) FRecognizeTextInStyles, 2);
	RegisterNativeFunction("FRecognizePara", (void*) FRecognizePara, 5);
	RegisterNativeFunction("FRecognizePoly", (void*) FRecognizePoly, 3);
}
