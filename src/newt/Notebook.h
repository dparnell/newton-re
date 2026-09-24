/*
	File:		newt/Notebook.h

	Contains:	TNotebook, the Newton application (gApplication is one: a
				TARMNotebook, the MessagePad's): a TApplication whose
				Constructor makes the root view (from the ROM's viewRoot
				template) and the librarian, whose InitToolbox sets the
				screen up (the offscreen bitmaps and the port, the inker,
				the orientation preference, the splash screen and the boot
				sound, the printers, the font loader, the international
				utilities, the recognition system at its full level, the
				init scripts, DarkStar), whose Idle idles the application,
				the recognition system and the views and re-arms the idle
				from the recogniser's next time, and whose Run makes up to
				ten idle passes (each followed by the root view's update)
				while an idle is due.  The exception and error notifiers
				(ExceptionNotify, ErrorNotify, Notify: the root view's
				notify and actionNotify methods) live here too.

				NOT YET RECONSTRUCTED: TLibrarian (the librarian and its
				library soup), InitScriptGlobals' NewtonScript boot (vars
				from varsMapStarter, the classes, the funky functions,
				bootInitNSGlobals), the inker task, the splash screen, the
				boot sound, the print drivers, the font loader, the
				international utilities' init, RunInitScripts, DarkStar;
				on the host the root view is InitViewSystem's (its own root
				template, the ROM's needing the whole system) and the
				recognition system starts at the clicks level.

	Reconstructed from the MP2x00 US ROM (0x00145f78-0x00146da8); each
	function cites its origin.
*/

#ifndef __NOTEBOOK_H
#define __NOTEBOOK_H

#include "Application.h"
#include "NewtWorld.h"

const long clNotebook = 0x44;
const long clARMNotebook = 0x46;

class TNotebook : public TApplication
{
public:
	virtual long		ClassID(void) const;					// (the ROM has no TNotebook::ClassID: TARMNotebook's)
	virtual Boolean		DerivedFrom(long id) const;				// ROM 0x00145fb4 DerivedFrom__9TNotebookCFl
	virtual void		Constructor(void);						// ROM 0x001467f8 Constructor__9TNotebookFv
	virtual void		Run(void);								// ROM 0x00146410 Run__9TNotebookFv
	virtual void		Idle(void);								// ROM 0x00146478 Idle__9TNotebookFv
	virtual void		Quit(void);								// ROM 0x00146b1c Quit__9TNotebookFv
	virtual void		InitToolbox(void);						// ROM 0x00146b28 InitToolbox__9TNotebookFv
	virtual Boolean		NeedsIdle(void);						// ROM 0x001464d0 NeedsIdle__9TNotebookFv (+0x28: an idle time is set and has passed)
	virtual Boolean		InitOffscreenBitmaps(void);				// ROM 0x00146c50 InitOffscreenBitmaps__9TNotebookFv (+0x2c: the port and the screen regions)

	void				DrawSplashScreen(void);					// ROM 0x0014602c DrawSplashScreen__9TNotebookFv (NOT YET)
	TAlarmEvent			fAlarmEvent;			// +0x20  the one system alarm (NewtWorld.h); SetSysAlarm fills it in
	void				InitInker(void);						// ROM 0x00146ca8 InitInker__9TNotebookFv (the inker task: NOT YET - the host's stand-in)
};

class TARMNotebook : public TNotebook
{
public:
	virtual long		ClassID(void) const;					// ROM 0x00145f78 ClassID__12TARMNotebookCFv
	virtual Boolean		DerivedFrom(long id) const;				// ROM 0x00145f80 DerivedFrom__12TARMNotebookCFl
};

// the screen regions the notebook keeps (the ROM's at 0x0c103abc)
extern RgnHandle	gScreenRgn;				// the whole screen
extern RgnHandle	gWideRgn;				// a copy of QuickDraw's wideOpen region

// the notifiers
void	SetActionDescription(long errorCode);				// ROM 0x00146528 SetActionDescription__Fl - vars.actionDescription
Ref		Notify(RefArg args);								// ROM 0x00146584 Notify__FRC6RefVar - the root view's notify method
// (ErrorNotify 0x001480fc: views/Application.h)
void	ActionErrorNotify(long errorCode, long kind);		// ROM 0x00146648 ActionErrorNotify__FlT1 - actionNotify([kind, errorCode, nil])
void	ExceptionNotify(Exception* exception);				// ROM 0x001468d4 ExceptionNotify__FP9Exception - vars.lastEx/lastExMessage/lastExError/lastExData set and the error shown

#endif	/* __NOTEBOOK_H */
