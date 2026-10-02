/*
	File:		host/remarkable/Folio.h

	Contains:	The reMarkable Paper Pro's type folio read from its own
				input device (docs/host-remarkable.md, "The type folio").

				The folio is "rM_Keyboard" on the pogo connector
				(/dev/input/event4 on the owner's tablet), there only while
				it is attached.  AppLoad forwards keys to its application
				only when its own window has the keyboard focus, which the
				AppLoad on a February 2026 tablet did not give (nothing came
				through), so newton reads the device itself, and takes it
				for its own (EVIOCGRAB) so xochitl underneath does not type
				too; NEWTON_RM_KEYBOARD=off leaves it to AppLoad,
				NEWTON_RM_KEYBOARD_GRAB=0 reads it without taking it.  The
				folio can come and go while newton runs: it is looked for
				again every second while it is away.

				RemarkableFolioAttached says whether it is there now, which
				is what the window starts the Newton landscape on (the
				tablet's interface turns to landscape with the folio).
*/

#ifndef __REMARKABLE_FOLIO_H
#define __REMARKABLE_FOLIO_H

#include "Panel.h"

class RemarkableFolio
{
public:
				RemarkableFolio() : fFd(-1), fLastLook(0), fOff(false), fGrab(true), fStarted(false) {}
				~RemarkableFolio() { Close(); }
	void		Look(void);						// opened if it is attached and not yet open (at most once a second)
	int			Fd(void) const { return fFd; }	// -1 while it is not
	bool		Read(RemarkableEvent* event);	// one key (down or up) if one is waiting: ==> whether it was
	void		Close(void);

private:
	int			fFd;
	long long	fLastLook;						// milliseconds
	bool		fOff, fGrab, fStarted;
};

bool	RemarkableFolioAttached(void);			// the folio's keyboard device is there now

#endif	/* __REMARKABLE_FOLIO_H */
