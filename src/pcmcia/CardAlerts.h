/*
	File:		pcmcia/CardAlerts.h

	Contains:	The card server's alerts (alert/AlertManager.h): "Newton
				still needs the card you removed" - put up when a task is
				held on a card that has gone, taken down when the card is
				back - and "The PCMCIA card is not installed correctly".
				Each is a TCardAlertDialog (one text item) carried in a
				TCardAlertEvent to the alert manager.

				Not in the DDK.  Reconstructed from the MP2x00 US ROM
				(0x0004ac54-0x0004b0d0).
*/

#ifndef __CARDALERTS_H
#define __CARDALERTS_H

#ifndef __ALERTMANAGER_H
#include "AlertManager.h"
#endif

// (0x50 bytes)
class TCardAlertDialog : public TAlertDialog
{
public:
					TCardAlertDialog();										// ROM 0x0004ac54 __ct__16TCardAlertDialogFv

	TAlertItem		fText[1];			// +28
	TAlertItem		fButton[1];			// +3C (none is shown)
};


class TCardReinsertAlertDialog : public TCardAlertDialog
{
public:
	void			Init(AlertFilterProcPtr proc, void* refCon);			// ROM 0x0004aee0 Init__24TCardReinsertAlertDialogFPFPvUlT1_UcPv
	void			Setup(void);											// ROM 0x0004aee4 Setup__24TCardReinsertAlertDialogFv
	void			Done(void);												// ROM 0x0004afd4 Done__24TCardReinsertAlertDialogFv
};


class TCardPositionAlertDialog : public TCardAlertDialog
{
public:
	void			Init(AlertFilterProcPtr proc, void* refCon);			// ROM 0x0004afe0 Init__24TCardPositionAlertDialogFPFPvUlT1_UcPv
};


// (100 bytes)
class TCardAlertEvent : public TAlertEvent
{
public:
					TCardAlertEvent();										// ROM 0x0004ae9c __ct__15TCardAlertEventFv

	TCardAlertDialog	fCardDialog;	// +14
};

// Why the card is wanted back (a package still using it), for the next
// reinsert alert; nil for the plain text.  ask: the reason is a package's
// name, to be put into the "The package ... still needs the card" text.
void	SetCardReinsertReason(const UniChar* reason, UChar ask);			// ROM 0x0004b010 SetCardReinsertReason__FPCUsUc

#endif	/* __CARDALERTS_H */
