/*
	File:		alert/AlertManager.h

	Contains:	The system alerts: small boxes of text (and buttons) the
				system puts up over everything else when something needs the
				user at once - a card pulled out while it was in use, a card
				not seated properly, an operating system error - drawn
				straight into the screen's bits with a font of their own and
				blitted onto the display, below QuickDraw and the views,
				which may be in no state to draw them.

				An alert is a TAlertDialog: a rectangle and a number of text
				items and buttons (TAlertItem), found by their offsets from
				the dialog, so that the whole thing can be copied as a block
				of its own size.  Whoever wants one shown sends the alert
				manager ('alrt, TAlertManager, InitAlertManager) a
				TAlertEvent naming it; the manager copies it to the front of
				its list and puts it up, and every 200 ms asks it whether it
				is done (a button tapped - the pen is polled directly - or its
				filter proc saying so: the card server's says the card is
				back).  When the last one has gone the application is told to
				redraw the screen ('draw).

				The drawing (DrawDChar, AlertFastLine and the rectangle verbs)
				works on the screen's bits a 32-bit big-endian word at a time
				at depths 1, 2 and 4; the text is the ROM's alertFont, an
				'sfnt with bitmap strikes, read glyph by glyph (TAlertGlyph).

				Not in the DDK.  Reconstructed from the MP2x00 US ROM
				(0x0002e738-0x00030bcc).  Layouts: TAlertDialog 0x28 bytes, a
				TAlertItem 0x14, TAlertEvent 0x14, TAlertManager 200; the
				host's are bigger (a text item holds a pointer).
*/

#ifndef __ALERTMANAGER_H
#define __ALERTMANAGER_H

#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __PORTS_H
#include "Ports.h"
#endif
#ifndef __LIST_H
#include "List.h"
#endif
#ifndef __FRAMES_H
#include "Frames.h"
#endif


// The modes the rectangle and line verbs draw in (TDMode)
enum { kAlertModeSet = 0, kAlertModeInvert = 1, kAlertModeClear = 2 };

// A text item or a button (0x14 bytes in the ROM)
class TAlertItem
{
public:
					TAlertItem();													// ROM 0x0002e870 __ct__10TAlertItemFv
	void			DrawText(UChar centred);										// ROM 0x0002f104 DrawText__10TAlertItemFUc
	void			DrawButton(void);												// ROM 0x0002ffe4 DrawButton__10TAlertItemFv

	Rect			fBounds;			// +00 in the dialog
	UniChar*		fText;				// +08
	Rect			fHitRect;			// +0C a button's, on the display
};


// A filter proc: the refCon it was given, the button tapped (-1: none),
// the filter data.  ==> whether the alert is done.
typedef UChar (*AlertFilterProcPtr)(void* refCon, ULong button, void* data);

// An alert (0x28 bytes in the ROM, and its items after it)
class TAlertDialog
{
public:
					TAlertDialog();													// ROM 0x0003011c __ct__12TAlertDialogFv
	void			SetFilterProc(AlertFilterProcPtr proc, void* refCon);			// ROM 0x00030168 SetFilterProc__12TAlertDialogFPFPvUlT1_UcPv
	void			SetFilterData(void* data);										// ROM 0x00030174 SetFilterData__12TAlertDialogFPv
	long			Alert(ULong* button);											// ROM 0x0003017c Alert__12TAlertDialogFPUl
	long			DisplayAlert(void);												// ROM 0x000301bc DisplayAlert__12TAlertDialogFv
	long			RemoveAlert(void);												// ROM 0x000302bc RemoveAlert__12TAlertDialogFv
	void			DrawAlert(void);												// ROM 0x0002e908 DrawAlert__12TAlertDialogFv
	ULong			CheckButton(void);												// ROM 0x0002eaa4 CheckButton__12TAlertDialogFv
	UChar			CheckAlertDone(ULong* button);									// ROM 0x0002e8c0 CheckAlertDone__12TAlertDialogFPUl

	TAlertItem*		TextItems(void)		{ return (TAlertItem*) ((char*) this + fTextOffset); }
	TAlertItem*		Buttons(void)		{ return (TAlertItem*) ((char*) this + fButtonOffset); }

	Rect				fBounds;		// +00 where it is drawn in the screen's bits
	ULong				fTextCount;		// +08
	ULong				fButtonCount;	// +0C
	long				fTextOffset;	// +10 from the dialog
	long				fButtonOffset;	// +14
	ULong				fSize;			// +18 the whole dialog's
	AlertFilterProcPtr	fFilterProc;	// +1C
	void*				fFilterRefCon;	// +20
	void*				fFilterData;	// +24
};


// An operating system error: a text and one button (0x50 bytes)
class TOSErrorAlertDialog : public TAlertDialog
{
public:
					TOSErrorAlertDialog();											// ROM 0x0002f8bc __ct__19TOSErrorAlertDialogFv
	void			Init(UniChar* text, UniChar* button);							// ROM 0x0002fbc8 Init__19TOSErrorAlertDialogFPUsT1

	TAlertItem		fText[1];			// +28
	TAlertItem		fButton[1];			// +3C
};


// The "erase the persistent data?" alert: a text and two buttons (100 bytes)
class TErasePersistentDataAlert : public TAlertDialog
{
public:
					TErasePersistentDataAlert();									// ROM 0x0002fbd4 __ct__25TErasePersistentDataAlertFv
	void			Init(UniChar* text, UniChar* button0, UniChar* button1);		// ROM 0x0002ffac Init__25TErasePersistentDataAlertFPUsN21
	void			Init(Rect* textBounds, UniChar* text, UniChar* button0, UniChar* button1);	// ROM 0x0002ffbc Init__25TErasePersistentDataAlertFP4RectPUsN22

	TAlertItem		fText[1];			// +28
	TAlertItem		fButtons[2];		// +3C
};


// A glyph of the alert font read out of its bitmap strike (0x30 bytes)
class TAlertGlyph
{
public:
					TAlertGlyph();													// ROM 0x0003033c __ct__11TAlertGlyphFv
					TAlertGlyph(RefArg font, long size);							// ROM 0x0003037c __ct__11TAlertGlyphFRC6RefVarl
	void			InitGlyph(RefArg font, long size);								// ROM 0x000303c0 InitGlyph__11TAlertGlyphFRC6RefVarl
	ULong			GetAlertHeight(void);											// ROM 0x000304c4 GetAlertHeight__11TAlertGlyphFv
	UChar			GetAlertGlyphWidth(long ch);									// ROM 0x000304d8 GetAlertGlyphWidth__11TAlertGlyphFl
	UChar			GetAlertGlyph(long ch, PixelMap* map);							// ROM 0x00030680 GetAlertGlyph__11TAlertGlyphFlP8PixelMap
	void			Portrait(PixelMap* map);										// ROM 0x00030708 Portrait__11TAlertGlyphFP8PixelMap

	long			(*fMap)(long ch, const void* cmap);		// +00 the cmap format's
	const char*		fCmap;				// +04
	const char*		fIndexArray;		// +08 the strike's index subtables
	const char*		fBdat;				// +0C
	ULong			fFirstGlyph;		// +10
	ULong			fLastGlyph;			// +14
	long			fDescender;			// +18
	long			fAscender;			// +1C
	UChar			fCellWidth;			// +20 the pixel map's
	UChar			fCellHeight;		// +21
	UChar			fWidth;				// +22 the glyph's metrics
	UChar			fHeight;			// +23
	SChar			fBearingX;			// +24
	SChar			fBearingY;			// +25
	UChar			fAdvance;			// +26
	UChar			fTop;				// +27 the glyph's first row in the cell
	long			fRowBytes;			// +28 of the glyph's bitmap
	const UByte*	fBits;				// +2C
};


// The event an alert is sent in (0x14 bytes)
class TAlertEvent : public TAEvent
{
public:
					TAlertEvent();													// ROM 0x00030860 __ct__11TAlertEventFv

	ULong			fCommand;			// +08 1: show it
	long			fResult;			// +0C
	TAlertDialog*	fDialog;			// +10
};


class TAlertManager;

class TAlertEventHandler : public TAEventHandler
{
public:
	void			Init(TAlertManager* manager);									// ROM 0x000308cc Init__18TAlertEventHandlerFP13TAlertManager
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);		// ROM 0x00030920 AEHandlerProc__18TAlertEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x000309e8 AECompletionProc__18TAlertEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);			// ROM 0x000309ec IdleProc__18TAlertEventHandlerFP10TUMsgTokenPUlP7TAEvent

	TAlertManager*	fManager;			// +14
};


// The redraw the application is sent when the last alert has gone
struct TAlertRedrawEvent : public TAEvent
{
	ULong			fType;				// +08 'draw
	Rect			fRect;				// +0C
};


// The 'alrt world (200 bytes)
class TAlertManager : public TAppWorld
{
public:
					TAlertManager();
	virtual ULong	GetSizeOf();													// ROM 0x00030bac GetSizeOf__13TAlertManagerFv
	virtual long	MainConstructor();												// ROM 0x00030b60 MainConstructor__13TAlertManagerFv

	TAlertEventHandler	fHandler;		// +70
	TUPort*				fPort;			// +88
	CList				fAlerts;		// +8C the alerts up, the front one first
	TUAsyncMessage		fRedrawMessage;	// +A4
	TAlertRedrawEvent	fRedraw;		// +B4
};

void	InitAlertManager(void);												// ROM 0x000307d4 InitAlertManager__Fv
long	OSErrorAlert(char* text);											// ROM 0x00030bb4 OSErrorAlert__FPc
long	OSErrorAlert(UniChar* text);										// ROM 0x00030bc0 OSErrorAlert__FPUs
long	OSWarningAlert(char* text);											// ROM 0x000308b4 OSWarningAlert__FPc
long	OSWarningAlert(UniChar* text);										// ROM 0x000308c0 OSWarningAlert__FPUs

// the drawing, on gAlertScreenInfo's bits
void	AlertFastLine(Point from, Point to, long mode);					// ROM 0x0002f4dc AlertFastLine__F5PointT1l
void	DrawDLine(short fromV, short fromH, short toV, short toH);			// ROM 0x0002ef30 DrawDLine__FsN31
void	DrawDChar(long v, long h, UByte* bits);							// ROM 0x0002ecb8 DrawDChar__FlT1PUc
Boolean	PtInDRect(long h, long v, Rect* r);								// ROM 0x0002efa4 PtInDRect__FlT1P4Rect
void	InsetDRect(Rect* r, short dv, short dh);							// ROM 0x0002efe4 InsetDRect__FP4RectsT2
void	PaintDRect(Rect* r, long mode);									// ROM 0x0002f058 PaintDRect__FP4Rect6TDMode
void	EraseDRect(Rect* r);												// ROM 0x0002f0f4 EraseDRect__FP4Rect
void	InvertDRect(Rect* r);												// ROM 0x0002f0fc InvertDRect__FP4Rect
void	FrameDRect(Rect* r, long mode);									// ROM 0x0002f3d4 FrameDRect__FP4Rect6TDMode
UChar	AlertGetPoint(long* point);										// ROM 0x0002f830 AlertGetPoint__FPl
// (the string helpers before them, Astrcpy and the rest, are recognition/Airus.h's)

extern Rect		gDisplayRect;				// ROM 0x0c10085c gDisplayRect - where the alert is on the display
extern Boolean	gLastPollingState;			// ROM 0x0c100864 gLastPollingState

#endif	/* __ALERTMANAGER_H */
