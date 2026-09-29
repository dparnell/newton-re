/*
	File:		text/TXRulerUI.h

	Contains:	The ruler bar: what protoTXView's ShowRuler puts above the
				text (TXView.h) - the ruler of the paragraph the selection
				is in, shown and changed with the pen.

				It is two bars, 0x26 pixels together.  The *tabs bar* on
				top is the ruler itself, spanning the text's frame: a
				measure in inches or centimetres (`SetRulerMeasure`, the
				ruler info's `type`), the tab stops as small markers, and
				the left margin, indent and right margin as three more
				(`DrawRuler`).  Below, the *icons bar* holds three clusters
				of icons (`TXRulerBitMapCluster`): the four justifications,
				the four kinds of tab to drag up into the ruler, and two
				arrows either side of the line spacing (`TXLineSpacingCluster`).

				A click is an attribute change for the paragraph, handed
				back as a TXAttrValues list and a `how` for
				UpdateRangeRulers (TXView::RulerClick makes it an undoable
				command): a justification or line spacing icon tapped
				('just', 'lspc'), a tab dragged in from the icons (added),
				dragged along the ruler (moved) or off it (removed), a
				margin marker dragged ('lMrg' - taking the indent along
				unless a modifier says otherwise - 'ndnt', 'rMrg').  The
				dragging is one loop (`TXDragRulerBitMaps`) that follows the
				pen, xor-drawing the icons as they go, keeping each within
				its bounds and, for icons coming up from the icons bar,
				snapping them into the ruler or back.

				The pictures are the ROM's rulerPicts - seventeen bitmaps
				made pixel maps once and shared while any ruler shows
				(`TXRulerPixMaps`): 0-3 the justification icons, 4-7 the
				tab markers, 8-11 the tab icons, 12-14 the margin and
				indent markers, 15-16 the arrows.  TXRulerUI::Start keeps
				the bar's measurements (`TXRulerUIData`).

				`TXNewtRulerUI` is the Newton's: focused through the view's
				visible region.

	Reconstructed from the MP2x00 US ROM (0x00243240-0x00245880,
	0x0024d900-0x0024d9f4, 0x0024dd1c-0x0024dea4); each function cites
	its origin.  The drag loop, the hit distance, the tabs bar's click and
	the bars' bounds were read from the assembly.
*/

#ifndef __TXRULERUI_H
#define __TXRULERUI_H

#ifndef __TEXTENSION_H
#include "Textension.h"
#endif
#ifndef __TXRULER_H
#include "TXRuler.h"
#endif
#ifndef __REGIONVARS_H
#include "RegionVars.h"
#endif

class TView;

// TXRulerUI::Start's measurements (the ROM's globals at 0x0c104e78).
struct TXRulerUIData
{
	long			fTabsBarHeight;	// +0x00  0x16
	long			fIconsBarHeight;	// +0x04  0x10
	long			fIconSpacing;	// +0x08  3
	long			fMeasureBase;	// +0x0c  0xb - the measure's line, down from the tabs bar's top
	long			fHitSlop;		// +0x10  5 - how near a marker a tap may be
};

extern TXRulerUIData	gTXRulerUIData;		// (ROM 0x0c104e78)
extern PixelMap*		gTXRulerBitMaps;	// (ROM 0x0c104e74) the seventeen pictures in use

// The pictures, made once and shared.  (The ROM's is at 0x0c104e90.)
struct TXRulerPixMaps
{
	NewtonErr		Get(PixelMap** maps);							// ROM 0x0024dd1c Get__14TXRulerPixMapsFPPA17_8PixelMap
	void			Release(void);									// ROM 0x0024de70 Release__14TXRulerPixMapsFv

	long			fUsers;			// +0x00
	PixelMap*		fMaps;			// +0x04  seventeen
};

extern TXRulerPixMaps	gTXRulerPixMaps;

// Where to go back to after drawing the bar.
struct TXRulerUIFocusInfo
{
	GrafPtr			fPort;			// +0x00
	RgnHandle		fClip;			// +0x04
};


// A row of icons, `count` of the pictures from `bitmap` on, answering to
// indices `first` on.  The ROM's is 0x24 bytes.
class TXRulerBitMapCluster
{
public:
					TXRulerBitMapCluster();							// ROM 0x00245110 __ct__20TXRulerBitMapClusterFv
	virtual void	Draw(const TXRuler* ruler);						// ROM 0x00245320 Draw__20TXRulerBitMapClusterFPC7TXRuler
	virtual void	CalcDimensions(int* width, int* height) const;	// ROM 0x002451a8 CalcDimensions__20TXRulerBitMapClusterCFPiT1
	virtual			~TXRulerBitMapCluster()	{ }						// (host)

	void			IRulerBitMapCluster(int first, int bitmap, int count, int spacing);	// ROM 0x00245144 IRulerBitMapCluster__20TXRulerBitMapClusterFiN31
	void			SetTopLeft(int top, int left);					// ROM 0x002451a0 SetTopLeft__20TXRulerBitMapClusterFiT1
	void			CalcBitMapRect(int index, Rect* r) const;		// ROM 0x002451d4 CalcBitMapRect__20TXRulerBitMapClusterCFiP4Rect
	// The picture's own size, centred in the icon's place.
	void			CalcDragBitMapRect(int index, Rect* r) const;	// ROM 0x0024524c CalcDragBitMapRect__20TXRulerBitMapClusterCFiP4Rect
	void			InvertBitMap(int index) const;					// ROM 0x002453b8 InvertBitMap__20TXRulerBitMapClusterCFi
	int				PointToBitMapIndex(Point pt) const;				// ROM 0x002453ec PointToBitMapIndex__20TXRulerBitMapClusterCF5Point - -1: none

	int				fTop;			// +0x04
	int				fLeft;			// +0x08
	int				fWidth;			// +0x0c  one icon's
	int				fHeight;		// +0x10
	int				fSpacing;		// +0x14
	int				fFirst;			// +0x18
	int				fCount;			// +0x1c
	int				fBitMap;		// +0x20
};


// The two line spacing arrows, and the spacing itself after them.
class TXLineSpacingCluster : public TXRulerBitMapCluster
{
public:
	virtual void	Draw(const TXRuler* ruler);						// ROM 0x0024571c Draw__20TXLineSpacingClusterFPC7TXRuler
	virtual void	CalcDimensions(int* width, int* height) const;	// ROM 0x00245544 CalcDimensions__20TXLineSpacingClusterCFPiT1

	void			GetLineSpacingStringBounds(Rect* r);			// ROM 0x00245568 GetLineSpacingStringBounds__20TXLineSpacingClusterFP4Rect
	void			DrawLineSpacingString(const TXRuler* ruler);	// ROM 0x002455b0 DrawLineSpacingString__20TXLineSpacingClusterFPC7TXRuler
};


// A bar of the ruler.  The ROM's is 0x14 bytes.
class TXRulerBar
{
public:
					TXRulerBar();									// ROM 0x00243bb4 __ct__10TXRulerBarFv
	virtual void	SetBounds(const Rect& r);						// ROM 0x00243c14 SetBounds__10TXRulerBarFRC4Rect
	virtual void	Activate(Boolean on);							// ROM 0x00243c34 Activate__10TXRulerBarFUc - nothing
	virtual void	Draw(void) = 0;									// (pure: +0x08)
	virtual Boolean	HitTest(Point pt) = 0;							// (pure: +0x0c)
	// ==> whether the click changed anything: `values` and `*how` say what.
	virtual Boolean	Click(TXPointingDevice* pen, long modifiers, TXAttrValues* values, long* how) = 0;	// (pure: +0x10)
	virtual void	CheckUpdate(const TXRuler* ruler) = 0;			// (pure: +0x14) the ruler shown made this one
	virtual			~TXRulerBar()	{ }								// (host)

	void			IRulerBar(Textension* text, TXRuler* ruler);	// ROM 0x00243be8 IRulerBar__10TXRulerBarFP10TextensionP7TXRuler
	void			GetBounds(Rect* r) const;						// ROM 0x00243c24 GetBounds__10TXRulerBarCFP4Rect

	Textension*		fText;			// +0x04
	TXRuler*		fRuler;			// +0x08  the ruler shown (TXRulerUI's)
	Rect			fBounds;		// +0x0c
};


// The ruler itself.  The ROM's is 0x18 bytes.
class TXRulerTabsBar : public TXRulerBar
{
public:
					TXRulerTabsBar();								// ROM 0x0024447c __ct__14TXRulerTabsBarFv
	virtual void	Draw(void);										// ROM 0x00244b44 Draw__14TXRulerTabsBarFv
	virtual Boolean	HitTest(Point pt);								// ROM 0x00244c78 HitTest__14TXRulerTabsBarF5Point
	virtual Boolean	Click(TXPointingDevice* pen, long modifiers, TXAttrValues* values, long* how);	// ROM 0x00244ca4 Click__14TXRulerTabsBarFP16TXPointingDevicelP12TXAttrValuesPl
	virtual void	CheckUpdate(const TXRuler* ruler);				// ROM 0x0024510c CheckUpdate__14TXRulerTabsBarFPC7TXRuler - DrawRuler

	void			IRulerTabsBar(Textension* text, TXRuler* ruler, int measure);	// ROM 0x002444c4 IRulerTabsBar__14TXRulerTabsBarFP10TextensionP7TXRuleri
	// ==> whether it changed.
	Boolean			SetRulerMeasure(int measure);					// ROM 0x002444f8 SetRulerMeasure__14TXRulerTabsBarFi - 0 inches, 1 centimetres
	int				GetTabBitMapIndex(TXTab tab) const;				// ROM 0x00244600 GetTabBitMapIndex__14TXRulerTabsBarCF5TXTab
	void			GetTabRect(TXTab tab, Rect* r) const;			// ROM 0x00244638 GetTabRect__14TXRulerTabsBarCF5TXTabP4Rect
	// A margin marker's place (12 left margin, 13 indent, 14 right margin).
	void			GetBitMapRect(const TXRuler* ruler, int index, Rect* r) const;	// ROM 0x002446ac GetBitMapRect__14TXRulerTabsBarCFPC7TXRuleriP4Rect
	int				TabRectToTabValue(const Rect& r) const;			// ROM 0x002447c4 TabRectToTabValue__14TXRulerTabsBarCFRC4Rect
	void			DrawRuler(const TXRuler* ruler);				// ROM 0x002447e8 DrawRuler__14TXRulerTabsBarFPC7TXRuler
	void			DrawRulerMeasure(void);							// ROM 0x00244934 DrawRulerMeasure__14TXRulerTabsBarFv
	// The marker a point is on or near; `*tab` the tab when it is one.
	int				PointToBitMapIndex(Point pt, TXTab* tab) const;	// ROM 0x00244b4c PointToBitMapIndex__14TXRulerTabsBarCF5PointP5TXTab

	int				fMeasure;		// +0x14  -1 until set
};


// The icons.  The ROM's is 0x84 bytes.
class TXRulerIconsBar : public TXRulerBar
{
public:
					TXRulerIconsBar();								// ROM 0x00243c38 __ct__15TXRulerIconsBarFv
	virtual void	SetBounds(const Rect& r);						// ROM 0x00243d20 SetBounds__15TXRulerIconsBarFRC4Rect
	virtual void	Draw(void);										// ROM 0x00243e84 Draw__15TXRulerIconsBarFv
	virtual Boolean	HitTest(Point pt);								// ROM 0x00244238 HitTest__15TXRulerIconsBarF5Point
	virtual Boolean	Click(TXPointingDevice* pen, long modifiers, TXAttrValues* values, long* how);	// ROM 0x00244298 Click__15TXRulerIconsBarFP16TXPointingDevicelP12TXAttrValuesPl
	virtual void	CheckUpdate(const TXRuler* ruler);				// ROM 0x00244358 CheckUpdate__15TXRulerIconsBarFPC7TXRuler

	void			IRulerIconsBar(Textension* text, TXRuler* ruler, TXRulerTabsBar* tabsBar);	// ROM 0x00243c9c IRulerIconsBar__15TXRulerIconsBarFP10TextensionP7TXRulerP14TXRulerTabsBar
	Boolean			DoJustClick(int index, TXAttrValues* values, long* how);	// ROM 0x00243fa4 DoJustClick__15TXRulerIconsBarFiP12TXAttrValuesPl
	Boolean			DoTabsClick(TXPointingDevice* pen, int index, TXAttrValues* values, long* how);	// ROM 0x00244010 DoTabsClick__15TXRulerIconsBarFP16TXPointingDeviceiP12TXAttrValuesPl
	Boolean			DoLineSpaceClick(int index, TXAttrValues* values, long* how);	// ROM 0x00244170 DoLineSpaceClick__15TXRulerIconsBarFiP12TXAttrValuesPl
	int				JustValueToBitMapIndex(char just) const;		// ROM 0x00244450 JustValueToBitMapIndex__15TXRulerIconsBarCFc

	TXRulerBitMapCluster	fJust;	// +0x14
	TXRulerBitMapCluster	fTabs;	// +0x38
	TXLineSpacingCluster	fLineSpacing;	// +0x5c
	TXRulerTabsBar*	fTabsBar;		// +0x80
};


// The bar.  The ROM's is 0xb8 bytes.
class TXRulerUI
{
public:
					TXRulerUI(Textension* text, PixelMap* maps, RefArg info);	// ROM 0x00244510 __ct__9TXRulerUIFP10TextensionP8PixelMapRC6RefVar
	virtual			~TXRulerUI();									// ROM 0x002450c8 __dt__9TXRulerUIFv
	virtual void	Focus(TXRulerUIFocusInfo* info);				// ROM 0x002433d4 Focus__9TXRulerUIFP18TXRulerUIFocusInfo
	virtual void	Unfocus(const TXRulerUIFocusInfo& info);		// ROM 0x00243430 Unfocus__9TXRulerUIFRC18TXRulerUIFocusInfo

	static void		Start(const TXRulerUIData& data);				// ROM 0x00243f70 Start__9TXRulerUISFRC13TXRulerUIData

	TXAttrObject*	CalcCurrentRulerObject(void) const;				// ROM 0x00243468 CalcCurrentRulerObject__9TXRulerUICFv - the selection's paragraph's
	void			GetCurrFrameTextBounds(Rect* r) const;			// ROM 0x002434ac GetCurrFrameTextBounds__9TXRulerUICFP4Rect
	// The tabs bar kept to the text's frame; ==> whether it moved.
	Boolean			CheckTextBounds(void);							// ROM 0x00243500 CheckTextBounds__9TXRulerUIFv
	void			Draw(void);										// ROM 0x00243580 Draw__9TXRulerUIFv
	Boolean			HitTest(Point pt);								// ROM 0x002435e4 HitTest__9TXRulerUIF5Point
	Boolean			Click(TXPointingDevice* pen, long modifiers, TXAttrValues* values, long* how);	// ROM 0x00243660 Click__9TXRulerUIFP16TXPointingDevicelP12TXAttrValuesPl
	// The ruler shown made the selection's (redrawn when asked).
	void			CheckUpdate(Boolean redraw);					// ROM 0x0024375c CheckUpdate__9TXRulerUIFUc
	void			Scrolled(void);									// ROM 0x00243b58 Scrolled__9TXRulerUIFv
	void			SetBounds(const Rect& r);						// ROM 0x00245474 SetBounds__9TXRulerUIFRC4Rect
	void			GetBounds(Rect* r) const;						// ROM 0x0024586c GetBounds__9TXRulerUICFP4Rect
	void			UpdateRulerInfo(RefArg info);					// ROM 0x00245794 UpdateRulerInfo__9TXRulerUIFRC6RefVar
	int				GetRulerType(RefArg info);						// ROM 0x00245800 GetRulerType__9TXRulerUIFRC6RefVar - 1 for 'metric

	Textension*		fText;			// +0x04
	TXRuler*		fRuler;			// +0x08  a ruler object of the shown paragraph's
	Rect			fBounds;		// +0x0c
	Rect			fTextBounds;	// +0x14  the tabs bar's
	TXRulerIconsBar	fIconsBar;		// +0x1c
	TXRulerTabsBar	fTabsBar;		// +0xa0
};


// The Newton's: drawn through the view's visible region.  The ROM's is
// 0xc0 bytes.
class TXNewtRulerUI : public TXRulerUI
{
public:
					TXNewtRulerUI(TView* view, Textension* text, PixelMap* maps, RefArg info);	// ROM 0x0024d900 __ct__13TXNewtRulerUIFP5TViewP10TextensionP8PixelMapRC6RefVar
	virtual void	Focus(TXRulerUIFocusInfo* info);				// ROM 0x0024d968 Focus__13TXNewtRulerUIFP18TXRulerUIFocusInfo
	virtual void	Unfocus(const TXRulerUIFocusInfo& info);		// ROM 0x0024d9b4 Unfocus__13TXNewtRulerUIFRC18TXRulerUIFocusInfo

	TView*			fView;			// +0xb8
	TRegionStruct	fSavedVis;		// +0xbc
};


// One icon being dragged.  The ROM's is 0x28 bytes.
struct TXRulerDragItem
{
	PixelMap*		fMap;			// +0x00
	Rect			fStart;			// +0x04  where it started
	Rect			fLimit;			// +0x0c  where it may go
	Rect			fCurrent;		// +0x14
	Point			fDelta;			// +0x1c  from where it started
	Rect			fSnap;			// +0x20  (snapping) where it would be followed only up and down
};

// Read from the assembly (unnamed in the ROM, 0x00243840).
Boolean	TXDragRulerBitMaps(TXPointingDevice* pen, TXRulerDragItem* items, int count, Boolean drawn, Boolean snap);
// (unnamed, 0x00243240) How far a point is from a marker: 0 inside it, the
// nearer side's distance when it is within gTXRulerUIData.fHitSlop of both
// a side and a top or bottom, else -1.
int		TXRulerPointDistance(Point pt, const Rect* r);
// (unnamed, 0x00243354) The rectangle moved to lie within `bounds`.
void	TXRulerPinRect(Rect* r, const Rect* bounds);

#endif	/* __TXRULERUI_H */
