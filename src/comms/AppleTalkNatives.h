/*
	File:		comms/AppleTalkNatives.h

	Contains:	The AppleTalk natives a script uses to name printers and
				zones: GetNames (the names of NBP addresses, which the Print
				slip's printer chooser shows its printers by).

				The rest of them - OpenAppleTalk, the NBP lookups, the zones -
				are NOT YET.
*/

#if !defined(__APPLETALKNATIVES_H)
#define __APPLETALKNATIVES_H 1

#include "objects.h"

Ref		ExtractNameFromNetAddress(RefArg address);	// ROM 0x000669d4 ExtractNameFromNetAddress__FRC6RefVar
void	RegisterAppleTalkNatives(void);

#endif	/* __APPLETALKNATIVES_H */
