/*
	File:		views/StyleRuns.h

	Contains:	Style runs: a paragraph's styles slot is an array of
				[length, style, length, style, ...] pairs covering its text
				(a length in characters, a style a font spec); the runs
				are kept the text's length when text is inserted or
				deleted.

	Reconstructed from the MP2100 D ROM (0x0012a938-0x0012ab00,
	0x0017c8c4-0x0017c9f4); each function cites its origin.
*/

#ifndef __STYLERUNS_H
#define __STYLERUNS_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

long	TotalRunLength(RefArg styles);							// the characters the runs cover
void	RunsInsert(RefArg styles, long offset, long count);		// count characters inserted at the offset: its run grows
void	RunsDelete(RefArg styles, long offset, long count);		// count characters deleted from the offset: their runs shrink, an emptied run goes
void	CorrectAnyBadStyleRuns(RefArg styles, long textLength);	// the runs made to cover the text (evt.ex -8009 for read-only runs)
Ref		SaveStylesAndTabStopsArrays(RefArg styles, RefArg tabs);	// a canonical styles frame {styles, tabs}
Ref		GetStyleAtOffset(RefArg styles, long offset, long* run, long* offsetInRun);	// ROM 0x0017f8dc GetStyleAtOffset__FRC6RefVarlPlT3 - the style of the character (a single spec: itself)
Ref		GetStylesOfRange(RefArg styles, long offset, long length, Boolean clone);	// ROM 0x0017fa94 GetStylesOfRange__FRC6RefVarlT2Uc - the runs covering the range
long	CountStylesForLength(RefArg styles, long run, long length);	// ROM 0x0017fd68 CountStylesForLength__FRC6RefVarlT2 - the runs from run covering length characters
void	SetStyleOfRange(RefArg styles, RefArg style, long start, long end);	// ROM 0x0017bb10 SetStyleOfRange__FRC6RefVarT1ClT3 - the range's runs replaced by one
void	CompactStyleRuns(RefArg styles);							// ROM 0x0017cb94 CompactStyleRuns__FRC6RefVar - equal neighbours merged
Ref		ExtractStylesArray(RefArg frame);							// ROM 0x0017ca88 ExtractStylesArray__FRC6RefVar - an array as it is, a frame's styles
Ref		ExtractTabStopsArray(RefArg frame);							// ROM 0x0017cae4 ExtractTabStopsArray__FRC6RefVar - a frame's tabs (nil for an array)
Ref		ExtractCorrectInfo(RefArg frame);							// ROM 0x0017cb3c ExtractCorrectInfo__FRC6RefVar - a frame's correctInfo

#endif	/* __STYLERUNS_H */
