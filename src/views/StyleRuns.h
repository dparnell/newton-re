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

#endif	/* __STYLERUNS_H */
