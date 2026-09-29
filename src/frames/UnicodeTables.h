/*
	File:		frames/UnicodeTables.h

	Contains:	InitUnicode: the character tables of the ROM's 'unicode
				frame (Runicode: charEncodings - the mapping binaries of the
				encodings 1-4 each way -, charClass, typelist, upperList,
				lowerList, upperNoMarkList, noMarkList) and the ASCII break
				table installed into utility/Unicode.h's converters and case
				functions.  The ROM does it in TNewtWorld::MainConstructor
				after InitObjects; the host in InitObjects, when the ROM's
				objects are there (nothing without them).  The frame's
				'sortTables array goes to gSortTables (frames/SortTables.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#ifndef __UNICODETABLES_H
#define __UNICODETABLES_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void	InitUnicode(void);			// ROM 0x00256acc InitUnicode__Fv
void	InstallBuiltInEncodings(void);	// ROM 0x002577e4 InstallBuiltInEncodings__Fv

#endif	/* __UNICODETABLES_H */
