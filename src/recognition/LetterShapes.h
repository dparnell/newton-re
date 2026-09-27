/*
	File:		recognition/LetterShapes.h

	Contains:	The Letter Shapes preference: every way the cursive
				recogniser knows of writing a letter, drawn, and the ones
				the writer does and does not use picked with the pen.

				The pictures are the ROM's `letterimages` binary (in
				`charsetInfoResources`, big-endian):

				  a word: how many letters
				  for each letter  a byte, the character; a byte, how many
				                   variants; a halfword per variant, its
				                   offset in the binary
				  a variant        a byte, its group; a byte, how many
				                   strokes; a halfword per stroke, its
				                   offset
				  a stroke         a byte, how many points; a byte of x
				                   and a byte of y for each, in a box 0-255
				                   high with the letter's body between the
				                   lines at 0x55 and 0xaa

				A *group* is the variants the recogniser weighs together:
				what a picture's weight is, and what a tap changes, is the
				group's - through the word domain's selectors 0x20016 and
				0x20017 (`LIGetVariantWeight`/`LISetVariantWeight`: 0 used,
				1 used less, 2 not used; 3 means the letter set does not
				have the group at all, and it is not drawn).

				The slip (`LetterShapesPreferencesForm`) is a view whose
				`origin` is the letter it starts at and `indent` how far
				in the pictures begin; its `viewDrawScript` calls
				`DrawLetterScript`, which lays the letter and the letters
				after it out a group to a 35-pixel cell (a group's variants
				side by side), a letter to a line with its title in the
				view's font and a rule between letters, until the view is
				full.  The letters come in pairs - a capital and its small
				letter, '(' and ')', ',' and '.' (`PairedChar`) - and the
				view shows one pair.  `ClickLetterScript` finds the group
				under the pen and hilites it (`gLIHiliteIndex` is the
				hilited group's first variant, counted across the
				letters), and `GetLetterHilite`/`SetLetterHilite` read and
				set its weight - refusing to leave a letter with no group
				in use at all.

	Reconstructed from the MP2x00 US ROM (0x00105de4-0x001082f8); each
	function cites its origin.
*/

#ifndef __LETTERSHAPES_H
#define __LETTERSHAPES_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __NEWTONMEMORY_H
#include "NewtonMemory.h"
#endif
#include "Ports.h"

class TView;

typedef UByte LILetterInfoType;			// a letter's entry in the images
typedef UByte LILetterVarType;			// a variant
typedef UByte LIStrokeType;				// a stroke

// ROM 0x0c1010cc gLIHiliteIndex - the hilited group's first variant,
// counted across the letters.
extern ULong	gLIHiliteIndex;

void	LIInit(void);											// ROM 0x00105de4 LIInit__Fv - nothing
UByte*	LIGetImageData(void);									// ROM 0x00107444 LIGetImageData__Fv - the letterimages binary's data
ULong	LILetterCount(void* image);								// ROM 0x00107fa8 LILetterCount__FPv
LILetterInfoType*	LIFirstInfo(void* image);					// ROM 0x00108284 LIFirstInfo__FPv
LILetterInfoType*	GetNextLetter(LILetterInfoType* info);		// ROM 0x001082e8 GetNextLetter__FP16LILetterInfoType
long	LIVariantCount(void* image);							// ROM 0x0010828c LIVariantCount__FPv - how many pictures there are in all
LILetterInfoType*	LIGetLetterInfo(void* image, UByte c);		// ROM 0x00106004 LIGetLetterInfo__FPvUc - nil when the letter has none
LILetterInfoType*	GetLetterHeaderOffset(void* image, UByte c);	// ROM 0x00105de8 GetLetterHeaderOffset__FPvUc - the same
// A letter's variant counted across the letters before it (all of them
// when the letter is not there).
long	LIGetVariantIndex(void* image, UByte c, short variant);	// ROM 0x00105e3c LIGetVariantIndex__FPvUcs
void	LIGetIndexedLetterInfo(void* image, ULong index, LILetterInfoType** info, short* variant);	// ROM 0x00105ea4 LIGetIndexedLetterInfo__FPvUlPP16LILetterInfoTypePs
long	LIGetGroupNumber(void* image, LILetterInfoType* info, short variant);	// ROM 0x0010600c LIGetGroupNumber__FPvP16LILetterInfoTypes - -1 past the last
// How many of the letter's variants from this one on are in its group -
// every later one of the group, not only the ones next to it.
long	LIGetLengthGroup(void* image, LILetterInfoType* info, short variant);	// ROM 0x00106040 LIGetLengthGroup__FPvP16LILetterInfoTypes
LILetterVarType*	LIGetVariantInfo(void* image, LILetterInfoType* info, short variant);	// ROM 0x001060b0 LIGetVariantInfo__FPvP16LILetterInfoTypes
LIStrokeType*	LIGetStrokeInfo(void* image, LILetterVarType* variant, short stroke);	// ROM 0x001060d4 LIGetStrokeInfo__FPvP15LILetterVarTypes
long	LIGetPoint(LIStrokeType* stroke, short index, Point* pt);	// ROM 0x00106180 LIGetPoint__FP12LIStrokeTypesP5Point
// A variant's box: its strokes' extent across, 0-255 down.
long	LIGetVariantBBox(void* image, LILetterVarType* variant, Rect* box);	// ROM 0x001061c4 LIGetVariantBBox__FPvP15LILetterVarTypeP4Rect
long	LIGetVariantBaseLine(void* image, LILetterVarType* variant, Rect* lines);	// ROM 0x0010630c LIGetVariantBaseLine__FPvP15LILetterVarTypeP4Rect - the box with the body's two lines as its top and bottom

// A parameter block for the word domain made and filled in, through
// which the weights are asked; and thrown away.
Handle	LIBeginWeights(void);									// ROM 0x001060f8 LIBeginWeights__Fv
void	LIEndWeights(Handle weights);							// ROM 0x00107068 LIEndWeights__FPPc
UByte	LIGetVariantWeight(Handle weights, UByte c, short group);	// ROM 0x00105f94 LIGetVariantWeight__FPPcUcs
void	LISetVariantWeight(Handle weights, UByte c, short group, UByte weight);	// ROM 0x00105f34 LISetVariantWeight__FPPcUcsT2

// A variant's picture fitted into a rectangle and drawn: the two body
// lines in gray, the strokes two pixels wide in black, gray (weight 2)
// or dark gray (weight 1).  `pause` ticks after each stroke and
// `animate` a stroke drawn at a fortieth of its points a tick.
long	DrawLetterImage(void* image, LILetterVarType* variant, Rect* rect, UByte weight, ULong pause, ULong animate, UByte frame);	// ROM 0x00106364 DrawLetterImage__FPvP15LILetterVarTypeP4RectUcUlT5T4
// A group drawn in cells 35 pixels apart from `rect`; the `hilite`th
// variant is the one drawn slowly, and the lines run through into the
// next cell except after the last.
long	DrawLetterGroup(void* image, LILetterInfoType* info, short start, short hilite, Rect* rect, UByte weight, ULong pause, ULong animate, UByte frame);	// ROM 0x00106b30 DrawLetterGroup__FPvP16LILetterInfoTypesT3P4RectUcUlT7T6
// The box a picture's extent is fitted into inside a rectangle (a pixel
// in all round, the aspect kept); -1 when either is empty.
long	CalculateScreenRect(Rect* extent, Rect* rect, Rect* screen);	// ROM 0x001066dc CalculateScreenRect__FP4RectN21
long	ConvertToScreenCoord(Point* pt, Rect* extent, Rect* screen);	// ROM 0x001069bc ConvertToScreenCoord__FP5PointP4RectT2
// The other letter of a pair: a capital's small letter, and '(' ')',
// '"' '\'', '$' '£', ',' '.', ';' ':' (and three accented ones in a
// locale with an eight-bit language); 0 for the end.
long	PairedChar(long c);										// ROM 0x00106a98 PairedChar__Fl

// Where the pictures are drawn: the cell, the page's size in cells and
// the position on it, and the letter and group being drawn.
struct LetterCursor
{
	Rect				fBounds;		// +0x00  the cell (31 square)
	long				fLines;			// +0x08
	long				fColumns;		// +0x0c
	long				fLine;			// +0x10
	long				fColumn;		// +0x14
	long				fIndex;			// +0x18  the group's first variant, across the letters
	UShort				fLetter;		// +0x1c
	UByte*				fImage;			// +0x20
	Handle				fWeights;		// +0x24
	LILetterInfoType*	fInfo;			// +0x28
	LILetterVarType*	fVariant;		// +0x2c
	short				fGroupStart;	// +0x30
	short				fGroup;			// +0x32
	short				fGroupLength;	// +0x34
	UByte				fWeight;		// +0x36
};

Boolean	InitLetterCursor(LetterCursor* cursor);					// ROM 0x0010701c InitLetterCursor__FP12LetterCursor
void	DoneLetterCursor(LetterCursor* cursor);					// ROM 0x001070b4 DoneLetterCursor__FP12LetterCursor
void	InitLetterBounds(TView* view, LetterCursor* cursor);	// ROM 0x001070bc InitLetterBounds__FP5TViewP12LetterCursor
void	ShowTitleLetter(LetterCursor* cursor, TView* view);		// ROM 0x00107198 ShowTitleLetter__FP12LetterCursorP5TView
void	SetLetter(LetterCursor* cursor, UShort c);				// ROM 0x001072b4 SetLetter__FP12LetterCursorUs
UShort	NextLetter(LetterCursor* cursor);						// ROM 0x001072c4 NextLetter__FP12LetterCursor
void	InitLetter(LetterCursor* cursor);						// ROM 0x001072f8 InitLetter__FP12LetterCursor
Boolean	MoreLetters(LetterCursor* cursor);						// ROM 0x00107330 MoreLetters__FP12LetterCursor
Boolean	InitLetterGroup(LetterCursor* cursor);					// ROM 0x0010735c InitLetterGroup__FP12LetterCursor - ==> whether the group is drawn
Boolean	MoreGroups(LetterCursor* cursor);						// ROM 0x00107400 MoreGroups__FP12LetterCursor
Boolean	GroupFitsOnLine(LetterCursor* cursor);					// ROM 0x00107420 GroupFitsOnLine__FP12LetterCursor
Boolean	PointInGroup(LetterCursor* cursor, Point pt, long* index, long* offset);	// ROM 0x001074ac PointInGroup__FP12LetterCursor6TPointPlT3
void	DisplayNextLine(LetterCursor* cursor);					// ROM 0x00107548 DisplayNextLine__FP12LetterCursor
Boolean	GroupHilited(LetterCursor* cursor, long index);			// ROM 0x00107588 GroupHilited__FP12LetterCursorl
void	DrawGroup(LetterCursor* cursor, long hilite);			// ROM 0x001075b4 DrawGroup__FP12LetterCursorl
void	DrawGroup(LetterCursor* cursor, long which, long pause, long animate, UByte frame);	// ROM 0x00107608 DrawGroup__FP12LetterCursorlN22Uc
void	DisplayNextGroup(LetterCursor* cursor);					// ROM 0x0010765c DisplayNextGroup__FP12LetterCursor
void	GetNextGroup(LetterCursor* cursor);						// ROM 0x0010769c GetNextGroup__FP12LetterCursor
Boolean	EndOfPage(LetterCursor* cursor);						// ROM 0x001076cc EndOfPage__FP12LetterCursor
UniChar	ToUni(UShort c);										// ROM 0x001076e8 ToUni__FUs
UByte	FromUni(UShort c);										// ROM 0x0010772c FromUni__FUs
UByte	GetTitleLetter(TView* view);							// ROM 0x00107778 GetTitleLetter__FP5TView - the view's origin, eight-bit
void	HiliteLetter(TView* view, ULong index, long offset, UByte on);	// ROM 0x0010793c HiliteLetter__FP5TViewUllUc
long	CountEnabledGroups(void* image, LILetterInfoType* info);	// ROM 0x00107dc0 CountEnabledGroups__FPvP16LILetterInfoType - how many groups the letter set uses

Ref		FDrawLetterShapes(RefArg rcvr);							// ROM 0x001077cc FDrawLetterShapes
Ref		FClickLetterShapes(RefArg rcvr, RefArg unit);			// ROM 0x00107a7c FClickLetterShapes
Ref		FCountLetterShapes(RefArg rcvr);						// ROM 0x00107cdc FCountLetterShapes
Ref		FGetHiliteWeight(RefArg rcvr);							// ROM 0x00107d04 FGetHiliteWeight
Ref		FSetHiliteWeight(RefArg rcvr, RefArg weight);			// ROM 0x00107e54 FSetHiliteWeight
Ref		FGetHiliteIndex(RefArg rcvr);							// ROM 0x00107f94 FGetHiliteIndex
Ref		FSetHiliteIndex(RefArg rcvr, RefArg index);				// ROM 0x00107fb0 FSetHiliteIndex
Ref		FGetIndexChar(RefArg rcvr, RefArg index);				// ROM 0x00107fe8 FGetIndexChar
Ref		FGetLetterIndex(RefArg rcvr, RefArg c);					// ROM 0x0010804c FGetLetterIndex
Ref		FDrawStringShapes(RefArg rcvr, RefArg string, RefArg top);	// ROM 0x001080b0 FDrawStringShapes

void	RegisterLetterShapesNatives(void);

#endif	/* __LETTERSHAPES_H */
