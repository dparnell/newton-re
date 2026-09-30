/*
	File:		views/PrintView.h

	Contains:	TPrintView (class 94, clPrintView), the view a print job
				is drawn from, and the job's C side: MakePrinter, the
				progress and problem reports to the transport.

				A printing transport (the ROM's print and fax transports)
				builds a print view from its template (a clPrintView whose
				children are the item's print format), hands it the item
				as `fields`, opens it and posts it command 56.  The view
				forks its world - the fork takes the Newton's event loop
				over, so the rest of the machine goes on while the job runs
				in this task - makes the printer the item's printer frame
				names (MakePrinter: its imagingName, constructed with its
				driverName, print/Printer.h), opens it with the fields,
				runs the view's viewShowScript with the printer's port
				current and prints the pages (PrintPages): each page is
				opened, the view drawn into the printer port once per band
				while RepeatPage asks for it, and closed, the print format's
				printNextPageScript saying whether another page follows.
				Progress goes to the transport's setPrintProgress, the
				stages (connecting, preparing, disconnecting, idle) to its
				setStatus, and the job's error ends in the fields' `error`
				slot, where the transport's RenderIt reads it.  Command 57
				cancels the job (DoUserAbort).

	Reconstructed from the MP2x00 US ROM (0x00192f90-0x001944b0; SetStatus
	0x000e895c); each function cites its origin.  The ROM's TPrintView is
	a TView of 0x48 bytes; its fields are named from their uses.
*/

#ifndef __VIEWS_PRINTVIEW_H
#define __VIEWS_PRINTVIEW_H

#include "View.h"
#include "print/Printer.h"

class TPrintView : public TView
{
public:
	// (the ROM's view memory comes cleared; the host's is set so here)
	TPrintView() : fPrinter(nil), fPort(nil), fPageCount(0), fIsPAP(false), fUseFullPage(false) { fFullPage.top = fFullPage.left = fFullPage.bottom = fFullPage.right = 0; }

	virtual long	ClassID(void) const;						// ROM 0x00192f90 ClassID__10TPrintViewCFv
	virtual Boolean	DerivedFrom(long id) const;					// ROM 0x00192f98 DerivedFrom__10TPrintViewCFl
	virtual Boolean	RealDoCommand(RefArg cmd);					// ROM 0x00192fcc RealDoCommand__10TPrintViewFRC6RefVar

	Boolean			ROMRealDoCommand(RefArg cmd);				// ROM 0x00193054 ROMRealDoCommand__10TPrintViewFRC6RefVar
	NewtonErr		PrintPages(RefArg transport);				// ROM 0x00193618 PrintPages__10TPrintViewFRC6RefVar

	TPrinter*		fPrinter;			// +0x30
	GrafPort*		fPort;				// +0x34  the printer's port
	long			fPageCount;			// +0x38  the pages still to print (0x7fff: as many as there are)
	Rect			fFullPage;			// +0x3c  the print form's bounds, for a format that uses the full page
	Boolean			fIsPAP;				// +0x44  a PostScript printer over AppleTalk's PAP, printing a received fax
	Boolean			fUseFullPage;		// +0x45
};

void		SetStatus(RefArg transport, RefArg status, RefArg args);				// ROM 0x000e895c SetStatus__FRC6RefVarN21
void		SetPrintProgress(RefArg transport, long page, long percent);			// ROM 0x00193dbc SetPrintProgress__FRC6RefVarlT2
void		SetFaxPrintProgress(RefArg transport, long page, long percent);		// ROM 0x00193e70 SetFaxPrintProgress__FRC6RefVarlT2
PrProblemResolution	HandleProblem(RefArg connectInfo, TPrinter* printer, NewtonErr problem, TTimeout retryTime, Boolean fixedButton);	// ROM 0x00193e9c HandleProblem__FRC6RefVarP8TPrinterlUlUc
TPrinter*	MakePrinter(RefArg printer);											// ROM 0x00194070 MakePrinter__FRC6RefVar
void		DisposePrinter(TPrinter* printer);										// ROM 0x001941b4 DisposePrinter__FP8TPrinter

#endif	/* __VIEWS_PRINTVIEW_H */
