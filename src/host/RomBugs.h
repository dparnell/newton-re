/*
	RomBugs.h - the switch between the ROM's known bugs and their fixes.

	The reconstruction ports the ROM's behaviour exactly, bugs included
	(src/README.md); a bug found on the way is ported as the ROM has it and
	then, by the owner's decision of 2026-10-06, fixed beside it as a marked
	DEVIATION.  The fix is in force by default; NEWTON_ROM_BUGS=1 in the
	environment brings back the ROM's own behaviour everywhere, so the
	faithful machine stays the one the suite can compare against
	(docs/rom-bugs.md lists every bug and its fix).

	A fix reads:

		if (RomBugFixed())
			...the corrected code...
		else
			...the ROM's code, as ported...

	The year-2010 fix (intl/Dates.h) predates this switch and keeps its own,
	NEWTON_ROM_2010_BUG.

	Header-only, so every library can ask without a link dependency; the
	environment is read once (the Windows CRT's getenv locks).
*/
#ifndef __ROMBUGS_H
#define __ROMBUGS_H

#include <stdlib.h>

inline int&
RomBugFixedState(void)
{
	static int sFixed = -1;		// -1: not yet read from the environment
	return sFixed;
}

// the ROM's known bugs are fixed (the default) rather than reproduced
inline bool
RomBugFixed(void)
{
	int& fixed = RomBugFixedState();
	if (fixed < 0)
	{
		const char* bugs = getenv("NEWTON_ROM_BUGS");
		fixed = !(bugs != NULL && bugs[0] != 0 && bugs[0] != '0');
	}
	return fixed != 0;
}

// tests: fixed or faithful, whatever NEWTON_ROM_BUGS says
inline void
SetRomBugFixed(bool fixed)
{
	RomBugFixedState() = fixed ? 1 : 0;
}

#endif	/* __ROMBUGS_H */
