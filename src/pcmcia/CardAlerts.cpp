/*
	File:		pcmcia/CardAlerts.cpp

	Contains:	The card server's alerts (CardAlerts.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CardAlerts.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "ObjectHeap.h"
#include "Unicode.h"
#include "NumberFormat.h"

// the reinsert alert's text when there is a reason (a package's)
static UniChar*	gCardReinsertReason = nil;		// ROM 0x0c100914 (unnamed)


static short
Coordinate(Ref frame, Ref slot)
{
	return (short) RINT(GetFrameSlotRef(frame, slot));
}


// ROM 0x0004ac54 __ct__16TCardAlertDialogFv
// One text item, in kCardAlertTextBounds of kCardAlertBounds.
TCardAlertDialog::TCardAlertDialog()
{
	fTextCount = 1;
	fButtonCount = 0;
	fTextOffset = (long) ((char*) fText - (char*) this);
	fButtonOffset = (long) ((char*) fButton - (char*) this);
	fSize = sizeof(TCardAlertDialog);		// (the ROM: 0x50)
	fBounds.top = Coordinate(Rkcardalertbounds, RSSYMtop);
	fBounds.left = Coordinate(Rkcardalertbounds, RSSYMleft);
	fBounds.bottom = Coordinate(Rkcardalertbounds, RSSYMbottom);
	fBounds.right = Coordinate(Rkcardalertbounds, RSSYMright);
	fText[0].fBounds.top = Coordinate(Rkcardalerttextbounds, RSSYMtop);
	fText[0].fBounds.left = Coordinate(Rkcardalerttextbounds, RSSYMleft);
	fText[0].fBounds.bottom = Coordinate(Rkcardalerttextbounds, RSSYMbottom);
	fText[0].fBounds.right = Coordinate(Rkcardalerttextbounds, RSSYMright);
}


// ROM 0x0004ae9c __ct__15TCardAlertEventFv
TCardAlertEvent::TCardAlertEvent()
{
	fDialog = &fCardDialog;
}


// ROM 0x0004aee0 Init__24TCardReinsertAlertDialogFPFPvUlT1_UcPv
// (the ROM's is a branch to SetFilterProc)
void
TCardReinsertAlertDialog::Init(AlertFilterProcPtr proc, void* refCon)
{
	SetFilterProc(proc, refCon);
}


// ROM 0x0004aee4 Setup__24TCardReinsertAlertDialogFv
// The alert made ready to go up: its standard height, and its text - the
// reason, 32 pixels taller, when there is one.
void
TCardReinsertAlertDialog::Setup(void)
{
	fBounds.bottom = Coordinate(Rkcardalertbounds, RSSYMbottom);
	fText[0].fBounds.bottom = Coordinate(Rkcardalerttextbounds, RSSYMbottom);
	if (gCardReinsertReason == nil)
	{
		fText[0].fText = (UniChar*) BinaryData(Rucardreinsertalerttext);
		return;
	}
	fText[0].fText = gCardReinsertReason;
	fBounds.bottom += 0x20;
	fText[0].fBounds.bottom += 0x20;
}


// ROM 0x0004afd4 Done__24TCardReinsertAlertDialogFv
void
TCardReinsertAlertDialog::Done(void)
{
	if (gCardReinsertReason != nil)
		operator delete(gCardReinsertReason);
	gCardReinsertReason = nil;
}


// ROM 0x0004afe0 Init__24TCardPositionAlertDialogFPFPvUlT1_UcPv
void
TCardPositionAlertDialog::Init(AlertFilterProcPtr proc, void* refCon)
{
	SetFilterProc(proc, refCon);
	fText[0].fText = (UniChar*) BinaryData(Rucardpositionalerttext);
}


// ROM 0x0004b010 SetCardReinsertReason__FPCUsUc
// The reason given (a copy, or - ask - the package's name put into
// uPackNeedsCardAlertText), or taken away.
void
SetCardReinsertReason(const UniChar* reason, UChar ask)
{
	UniChar* text = nil;
	if (gCardReinsertReason != nil)
		operator delete(gCardReinsertReason);
	if (reason != nil)
	{
		if (!ask)
		{
			long length = Ustrlen(reason);
			text = (UniChar*) operator new((length + 1) * sizeof(UniChar));
			if (text != nil)
				Ustrcpy(text, reason);
		}
		else
		{
			const UniChar* proto = (const UniChar*) BinaryData(Rupackneedscardalerttext);
			long protoLength = Ustrlen(proto);
			long length = Ustrlen(reason);
			text = (UniChar*) operator new((protoLength + length + 1) * sizeof(UniChar));
			if (text != nil)
				ParamString(text, protoLength + length, proto, reason);
		}
	}
	gCardReinsertReason = text;
}
