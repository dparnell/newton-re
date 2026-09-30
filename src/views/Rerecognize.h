/*
	File:		views/Rerecognize.h

	Contains:	Deferred recognition: writing that is already on a view -
				ink words in a paragraph, an ink shape, stroke bundles and
				ink a script holds - read again, now, by the recognition
				system.

				The one mechanism under all of it is the controller's
				`RecognizeInArea` (recognition/Controller.h): the strokes
				are made into stroke units as if just written, in an area
				built for the purpose (`MakeRerecognizeArea`), and the
				controller is idled until every stroke has been read or
				expired, each winning unit going to a handler the caller
				names.  The handlers here are the three callers':

				- a paragraph's ink word (`RerecognizeWord` over the
				  paragraph, command 0x19): the word read replaces the ink
				  word through the insert-items path (DoInsertItems), and
				  the command's `stop` says how long the replacement is;
				- an ink shape (`RerecognizeWord` over the polygon view,
				  command 0x19): the word read is put down as a word on
				  the page (aeWord17) and the shape removed;
				- a script's strokes (`RecognizeStrokes`, the `Recognize`
				  native): every word read added to a fresh correct info
				  frame, which is the answer.

				`RecognizePara` asks a paragraph to read every ink word in
				a range (command 0x1a, one 0x19 per ink word, the range's
				end moved by what each one became); `RecognizePoly` asks a
				polygon view to read its ink.  `RecognizeInkWord` reads one
				ink word and answers its words; `RecognizeTextInStyles`
				reads every ink word in a text-and-styles frame and answers
				the frame with them replaced by their text.  `DrawCheckmark`
				is the arrow the machine shows over what it is reading.

				The strokes nobody read are grouped into ink as the
				ROM does (recognition/InkGroups.h), and handed to
				HandleBulkStrokes - which, as in the ROM, drops them (see
				there), so a script's strokes that are not words come back
				as nothing.  The paragraph's ProcessStyles calls
				RecognizePara, and the text engine's FixupDropData
				(text/TXView.cpp) RecognizeTextInStyles.

	Reconstructed from the MP2x00 US ROM (0x00035bf8-0x00036960,
	0x0017ddcc-0x0017e340, 0x001714c8, 0x0019ff5c-0x001a0028,
	0x001a269c-0x001a2888, 0x001febfc-0x001fec10); each function cites
	its origin.
*/

#ifndef __RERECOGNIZE_H
#define __RERECOGNIZE_H

#include "Newton.h"
#include "objects.h"

class TView;
class TParagraphView;
class TPolygonView;
class TRecArea;
class TUnit;
class StrokeCentral;
struct Rect;

// the command a view is asked to read its ink again with (the ROM's name
// is not known), and the one a paragraph is asked to read a range with
enum
{
	aeRecognizeInk		= 0x19,		// start, stop (a length), doHilite, recConfig; a paragraph answers stop = the new length
	aeRecognizeRange	= 0x1a		// start, stop, doHilite, recConfig; stop answers where the range ends now
};

extern StrokeCentral*	gBulkStrokes;						// ROM 0x0c1008a4 gBulkStrokes - RecognizeStrokes' own stroke world, for the strokes nobody read

void	DrawCheckmark(Rect& bounds);						// ROM 0x001714c8 DrawCheckmark__FR5TRect - the arrow over what is being read (up and down by turns)

ULong	PolygonWordHandler(TUnit* unit, ULong view);		// ROM 0x00035bf8 PolygonWordHandler__FP5TUnitUl
void	RerecognizeWord(TPolygonView* view, TRecArea* area);	// ROM 0x00035d4c RerecognizeWord__FP12TPolygonViewP8TRecArea
ULong	ParagraphViewWordHandler(TUnit* unit, ULong cmd);	// ROM 0x00035e70 ParagraphViewWordHandler__FP5TUnitUl - cmd: the address of a RefVar of the command
void	RerecognizeWord(TParagraphView* view, RefArg cmd, TRecArea* area);	// ROM 0x00036110 RerecognizeWord__FP14TParagraphViewRC6RefVarP8TRecArea

void	HandleBulkStrokes(RefArg correctInfo, RefArg bundle);	// ROM 0x00036340 HandleBulkStrokes__FRC6RefVarT1
ULong	BulkUnitHandler(TUnit* unit, ULong correctInfo);	// ROM 0x00036398 BulkUnitHandler__FP5TUnitUl - correctInfo: the address of a RefVar of it
Ref		RecognizeStrokes(RefArg strokes, RefArg config, Boolean together);	// ROM 0x00036448 RecognizeStrokes__FRC6RefVarT1Uc - a correct info frame, nil on failure

Ref		RecognizeInkWord(RefArg ink);						// ROM 0x0017ddcc RecognizeInkWord__FRC6RefVar
Ref		RecognizeTextInStyles(RefArg textAndStyles, RefArg fontSpec);	// ROM 0x0017df70 RecognizeTextInStyles__FRC6RefVarT1

long	RecognizePara(TView* view, long start, long stop, Boolean doHilite, RefArg config);	// ROM 0x001a269c RecognizePara__FP5TViewlT2UcRC6RefVar
void	RecognizePoly(TView* view, Boolean doHilite, RefArg config);	// ROM 0x001a27f4 RecognizePoly__FP5TViewUcRC6RefVar

void	RegisterRerecognizeNatives(void);					// Recognize, RecognizePara, RecognizePoly, RecognizeInkWord, RecognizeTextInStyles

#endif	/* __RERECOGNIZE_H */
