/*
	File:		text/TXRuler.h

	Contains:	The text engine's rulers - what a paragraph is laid out
				against.

				A ruler is a `TXAttrObject` (text/TXAttributes.h) like a
				style is, so a run of the document points at one and the
				same machinery narrows a list of attributes down to what
				a selection agrees about.  What it carries is the shape
				of a paragraph: its justification, its two margins and
				its first line's indent, how far apart its lines are,
				and where its tab stops are.

				There are two of them.  `TXBasicRuler` has a
				justification and nothing else, and its tabs are the
				default ones - every `gTXDefaultTabVal` pixels, for
				ever.  `TXAdvancedRuler` adds the margins, the indent,
				the line spacing and a `TXTabsArray` of real tab stops.
				A `TXTab` is a position, a kind (left, centre, decimal
				point or right) and the character the run up to it is
				filled with.

				`GetNSObject` and `SetNSObject` are the two rulers as a
				script sees them - a frame of `justification`, `indent`,
				`leftMargin`, `rightMargin`, `lineSpacing` and a `tabs`
				array - and `TXGetRulerAttrValues` turns such a frame
				straight into the attribute list that changes a range's
				rulers without making a ruler at all.

	Reconstructed from the MP2x00 US ROM (0x0022f2b8-0x002306c8,
	0x00242c28-0x00242c68, 0x0024587c-0x00245c18); each function cites
	its origin.
*/

#ifndef __TXRULER_H
#define __TXRULER_H

#ifndef __TXATTRIBUTES_H
#include "TXAttributes.h"
#endif
#include "objects.h"


// How far apart the default tab stops are, in pixels.  It is nought
// until `Textension::TextensionStart` sets it to 30, which is the
// engine's start-up; a ruler asked for a tab before then divides by
// nought, exactly as the ROM does.
extern long	gTXDefaultTabVal;									// ROM 0x0c104d7c gTXDefaultTabVal

// What a `TXTab`'s kind may be.  (The ROM keeps them in a byte; right
// is 0xff, which it reads back as -1.)
const unsigned char	kTXTabLeft			= 0;
const unsigned char	kTXTabCenter		= 1;
const unsigned char	kTXTabDecimalPoint	= 2;
const unsigned char	kTXTabRight			= 0xff;

// The justifications, as TXBasicRuler keeps them.
const char	kTXJustifyLeft		= 1;
const char	kTXJustifyRight		= 2;
const char	kTXJustifyCenter	= 4;
const char	kTXJustifyFull		= 8;


// One tab stop.  The ROM's is eight bytes of which it uses six - the
// position, the kind and the fill character - and it copies six
// wherever it copies one.
struct TXTab
{
	// `fill` of nought against a decimal-point tab becomes a full stop,
	// which is where a table of contents gets its dotted leader from
	// without anyone asking for one.
	void			Set(int position, char kind, unsigned char fill);	// ROM 0x00245984 Set__5TXTabFicUc

	int				fPosition;		// +0x00  pixels from the left margin
	unsigned char	fKind;			// +0x04
	unsigned char	fFillChar;		// +0x05
};


// The tab stops of one ruler, kept sorted by position.
class TXTabsArray : public TXArray
{
public:
					TXTabsArray();									// ROM 0x002300ec __ct__11TXTabsArrayFv
	virtual			~TXTabsArray();									// ROM 0x00230134 __dt__11TXTabsArrayFv

	// Put in at its sorted place; ==> kError_No_Memory if there was no
	// room.  (It does not look at whether SearchTab found the position
	// already taken, so inserting a tab twice gives two of them.)
	NewtonErr		InsertTab(TXTab tab);							// ROM 0x00230174 InsertTab__11TXTabsArrayF5TXTab
	void			RemoveTab(long index);							// ROM 0x002301bc RemoveTab__11TXTabsArrayFl
	// Whether a tab stands at that position; `index` comes back as the
	// one found, or as where it would go.
	Boolean			SearchTab(int position, long* index) const;		// ROM 0x002301c8 SearchTab__11TXTabsArrayCFiPl
	// The first tab past `width` (a Fixed), or a default tab stop when
	// there is none.
	TXTab			WidthToTab(long width) const;					// ROM 0x00230288 WidthToTab__11TXTabsArrayCFl
	Boolean			IsEqual(const TXTabsArray* other) const;		// ROM 0x00230368 IsEqual__11TXTabsArrayCFPC11TXTabsArray
	TXTab			GetIndTab(long index) const;					// ROM 0x00230420 GetIndTab__11TXTabsArrayCFl
};


// What a ruler says about the tab a line has run into: which tab it is,
// and how far it is from where the line had got to.
struct TXPendingTab
{
	Boolean			fPending;		// +0x00  the tab is not a plain left one, so its width is not settled yet
	TXTab			fTab;			// +0x04
	long			fWidth;			// +0x0c  Fixed: from where the line was to the tab
};


// What a ruler is asked to make of a line's height.  PROVISIONAL: only
// the two words `TXAdvancedRuler::AdjustLineHeight` touches are known;
// the rest comes with the lines (`TXLinesHeights`).
struct TXLineHeightInfo
{
	long			fHeight;		// +0x00  the height the line is given
	long			fNaturalHeight;	// +0x04  what it would be at single spacing
};


// The shape of a paragraph, as an attribute object.
class TXRuler : public TXAttrObject
{
public:
					TXRuler();										// ROM 0x00242c28 __ct__7TXRulerFv

	// The blank space at the two ends of a line, as Fixed.
	virtual long	GetLineLeftBlanks(Boolean firstLine) const = 0;	// (pure: vtable +0x54)
	virtual long	GetLineRightBlanks(void) const = 0;				// (pure: vtable +0x58)
	// The tab `x` (a Fixed) has run into, described into `pending`;
	// ==> how wide it is, as Fixed.
	virtual long	GetTabWidth(long x, long unused, TXPendingTab* pending) const = 0;	// (pure: vtable +0x5c)
	virtual void	AdjustLineHeight(TXLineHeightInfo* info) const = 0;	// (pure: vtable +0x60)
	// What a tab that was waiting on the text after it finally comes
	// to, now that the text is `textWidth` wide and `available` is left
	// on the line.
	virtual long	CalcPendingTabWidth(const TXPendingTab& pending, long textWidth, long available) const = 0;	// (pure: vtable +0x64)
};


// A justification, and the default tab stops.
class TXBasicRuler : public TXRuler
{
public:
					TXBasicRuler();									// ROM 0x0024587c __ct__12TXBasicRulerFv

	virtual TXAttrObject* CreateNew(void) const;					// ROM 0x002459b4 CreateNew__12TXBasicRulerCFv
	virtual long	GetClassId(void) const;							// ROM 0x002459a8 GetClassId__12TXBasicRulerCFv - 'brlr'
	virtual void	GetAttributesValues(TXAttrValues* values);		// ROM 0x002458c4 GetAttributesValues__12TXBasicRulerFP12TXAttrValues
	virtual Boolean	IsEqual(const TXAttrObject* other) const;		// ROM 0x00245b64 IsEqual__12TXBasicRulerCFPC12TXAttrObject
	virtual void	Assign(const TXAttrObject* other);				// ROM 0x002458f4 Assign__12TXBasicRulerFPC12TXAttrObject
	virtual Boolean	GetAttributeValue(TXAttrTag tag, void* value) const;	// ROM 0x00245bf8 GetAttributeValue__12TXBasicRulerCFUlPv
	virtual void	SetAttributeValue(TXAttrTag tag, const void* value);	// ROM 0x00245900 SetAttributeValue__12TXBasicRulerFUlPCv
	virtual Ref		GetNSObject(void) const;						// ROM 0x002459bc GetNSObject__12TXBasicRulerCFv
	virtual void	SetNSObject(RefArg obj);						// ROM 0x00245b10 SetNSObject__12TXBasicRulerFRC6RefVar
	virtual Boolean	GetCommonAttrValue(TXAttrTag tag, void* value) const;	// ROM 0x00245bcc GetCommonAttrValue__12TXBasicRulerCFUlPv

	virtual long	GetLineLeftBlanks(Boolean firstLine) const;		// ROM 0x0024590c GetLineLeftBlanks__12TXBasicRulerCFUc
	virtual long	GetLineRightBlanks(void) const;					// ROM 0x00245914 GetLineRightBlanks__12TXBasicRulerCFv
	virtual long	GetTabWidth(long x, long unused, TXPendingTab* pending) const;	// ROM 0x0024591c GetTabWidth__12TXBasicRulerCFlT1P12TXPendingTab
	virtual void	AdjustLineHeight(TXLineHeightInfo* info) const;	// ROM 0x00245978 AdjustLineHeight__12TXBasicRulerCFP16TXLineHeightInfo
	virtual long	CalcPendingTabWidth(const TXPendingTab& pending, long textWidth, long available) const;	// ROM 0x0024597c CalcPendingTabWidth__12TXBasicRulerCFRC12TXPendingTablT2

	char			fJustification;	// +0x08  left to begin with
};


// The whole shape: margins, indent, line spacing and real tab stops.
class TXAdvancedRuler : public TXBasicRuler
{
public:
					TXAdvancedRuler();								// ROM 0x0022f2b8 __ct__15TXAdvancedRulerFv

	virtual void	FreeData(void);									// ROM 0x0022f314 FreeData__15TXAdvancedRulerFv
	virtual TXAttrObject* CreateNew(void) const;					// ROM 0x0022f834 CreateNew__15TXAdvancedRulerCFv
	// A ruler with tab stops is *copied* rather than shared, because
	// its TXTabsArray belongs to it alone.
	virtual TXAttrObject* Reference(void);							// ROM 0x00230234 Reference__15TXAdvancedRulerFv
	virtual long	GetClassId(void) const;							// ROM 0x0023046c GetClassId__15TXAdvancedRulerCFv - 'rulr'
	virtual void	GetAttributesValues(TXAttrValues* values);		// ROM 0x0022f350 GetAttributesValues__15TXAdvancedRulerFP12TXAttrValues
	virtual Boolean	IsEqual(const TXAttrObject* other) const;		// ROM 0x002305c4 IsEqual__15TXAdvancedRulerCFPC12TXAttrObject
	virtual void	Assign(const TXAttrObject* other);				// ROM 0x0022f4ac Assign__15TXAdvancedRulerFPC12TXAttrObject
	virtual Boolean	GetAttributeValue(TXAttrTag tag, void* value) const;	// ROM 0x00230648 GetAttributeValue__15TXAdvancedRulerCFUlPv
	virtual void	SetAttributeValue(TXAttrTag tag, const void* value);	// ROM 0x0022f50c SetAttributeValue__15TXAdvancedRulerFUlPCv
	virtual Ref		GetNSObject(void) const;						// ROM 0x0022fcec GetNSObject__15TXAdvancedRulerCFv
	virtual void	SetNSObject(RefArg obj);						// ROM 0x0022ff58 SetNSObject__15TXAdvancedRulerFRC6RefVar
	virtual Boolean	GetCommonAttrValue(TXAttrTag tag, void* value) const;	// ROM 0x00230508 GetCommonAttrValue__15TXAdvancedRulerCFUlPv
	virtual unsigned long GetAttributeFlags(TXAttrTag tag) const;	// ROM 0x00230478 GetAttributeFlags__15TXAdvancedRulerCFUl
	// The one attribute that is not simply set: a tab is added, moved
	// or taken away by `how` rather than the whole array replaced.
	virtual void	UpdateAttribute(TXAttrTag tag, const void* value, long how);	// ROM 0x0022f5a8 UpdateAttribute__15TXAdvancedRulerFUlPCvl

	virtual long	GetLineLeftBlanks(Boolean firstLine) const;		// ROM 0x0022f6e0 GetLineLeftBlanks__15TXAdvancedRulerCFUc
	virtual long	GetLineRightBlanks(void) const;					// ROM 0x0022f6f4 GetLineRightBlanks__15TXAdvancedRulerCFv
	virtual long	GetTabWidth(long x, long right, TXPendingTab* pending) const;	// ROM 0x0022f754 GetTabWidth__15TXAdvancedRulerCFlT1P12TXPendingTab
	virtual void	AdjustLineHeight(TXLineHeightInfo* info) const;	// ROM 0x0022f808 AdjustLineHeight__15TXAdvancedRulerCFP16TXLineHeightInfo
	virtual long	CalcPendingTabWidth(const TXPendingTab& pending, long textWidth, long available) const;	// ROM 0x0022f700 CalcPendingTabWidth__15TXAdvancedRulerCFRC12TXPendingTablT2

	// The array copied, tab by tab; nil or an empty one leaves none.
	void			SetTabs(const TXTabsArray* tabs);				// ROM 0x0022f424 SetTabs__15TXAdvancedRulerFPC11TXTabsArray
	Boolean			EqualTabs(const TXTabsArray* other) const;		// ROM 0x002304dc EqualTabs__15TXAdvancedRulerCFPC11TXTabsArray

	TXTabsArray*	fTabs;			// +0x0c  nil when there are none
	long			fIndent;		// +0x10  the first line's left edge
	long			fLeftMargin;	// +0x14  every other line's
	long			fRightMargin;	// +0x18
	unsigned char	fLineSpacing;	// +0x1c  1 single, 2 one and a half, 3 double... up to 20
};


// How UpdateAttribute is told what to do with a tab ('tabs).
const long	kTXTabRemove		= 1;	// take the tab at that position away
const long	kTXTabAdd			= 2;	// put this tab in
const long	kTXTabMove			= 4;	// take the old one away and put this one in

// And what the value of such a 'tabs attribute is: not an array of tab
// stops but one change to make to them.  (The ROM's is twenty bytes,
// which is exactly what a TXAttrValues entry holds.)
struct TXTabUpdate
{
	TXTab				fOld;		// +0x00  the tab being moved or taken away
	TXTab				fNew;		// +0x08  the tab being put in
	TXAdvancedRuler*	fRuler;		// +0x10  the ruler the other tabs come from
};


// 'right / 'center / 'decimalPoint / anything else.
unsigned char	TabKindSymbolToNum(RefArg symbol);					// ROM 0x0022f83c TabKindSymbolToNum__FRC6RefVar
// 'right / 'center / 'full / anything else.
char			TXJustSymbolToNum(RefArg symbol);					// ROM 0x00245a84 TXJustSymbolToNum__FRC6RefVar
// A script's array of tab frames as a TXTabsArray; nil when it held none.
void			FromObject(RefArg tabs, TXTabsArray** array);		// ROM 0x0022f8c4 FromObject__FRC6RefVarPP11TXTabsArray
// A script's ruler frame as an attribute list: only the slots it has
// are in it, so it says what to change and nothing else.
TXAttrValues*	TXGetRulerAttrValues(RefArg ruler);					// ROM 0x0022fa14 TXGetRulerAttrValues__FRC6RefVar

#endif	/* __TXRULER_H */
