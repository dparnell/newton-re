/*
	File:		text/TXRuler.cpp

	Contains:	The text engine's rulers - see TXRuler.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXRuler.h"
#include "Frames.h"
#include "ROMConstants.h"
#include "NewtonMemory.h"
#include "FixedMath.h"
#include "OSErrors.h"
#include "host/RomBugs.h"

#include <string.h>


// ROM 0x0c104d7c gTXDefaultTabVal
// Nought until Textension::TextensionStart (ROM 0x00252b48) makes it 30.
long	gTXDefaultTabVal = 0;


// A Fixed rounded to the nearest whole pixel, as every one of these
// does it: add a half and take the top half-word.  (Written out by
// hand rather than through qd/Ports.h's RoundFixed, because the ROM
// narrows the result to a short first and that is what makes a tab
// past 32767 pixels wrap.)
static short
TXRoundFixed(long fixed)
{
	return (short) ((fixed + 0x8000) >> 16);
}


// And the other way: a whole number of pixels as a Fixed.  It goes
// through a 32-bit word because the ROM's `lsl #16` wraps rather than
// trapping, and a tab past 32767 pixels is exactly what would.
static long
TXPixelsToFixed(long pixels)
{
	return (long) (int) ((unsigned int) pixels << 16);
}


#pragma mark -
/*--------------------------------------------------------------------
	TXTab and TXTabsArray.
--------------------------------------------------------------------*/

// ROM 0x00245984 Set__5TXTabFicUc
void
TXTab::Set(int position, char kind, unsigned char fill)
{
	// a decimal-point tab with no fill character asked for gets a full
	// stop, which is where the dotted leader of a table of contents
	// comes from without anyone having asked for one
	if ((unsigned char) kind == kTXTabDecimalPoint && fill == 0)
		fill = '.';
	fPosition = position;
	fKind = (unsigned char) kind;
	fFillChar = fill;
}


// ROM 0x002300ec __ct__11TXTabsArrayFv
// The ROM's element is eight bytes (a TXTab), grown one at a time.
TXTabsArray::TXTabsArray()
	: TXArray(sizeof(TXTab), 0)
{ }


// ROM 0x00230134 __dt__11TXTabsArrayFv
TXTabsArray::~TXTabsArray()
{ }


// ROM 0x002301c8 SearchTab__11TXTabsArrayCFiPl
// The tabs are sorted, so this walks until it finds the position or
// passes it; `index` is left at the one found, or at where one would go.
Boolean
TXTabsArray::SearchTab(int position, long* index) const
{
	*index = 0;
	long count = fCount;
	if (count <= 0)
		return false;
	do
	{
		long at = ((const TXTab*) GetElementPtr(*index))->fPosition;
		if (position == at)
			return true;
		if (at > position)
			break;
		(*index)++;
	}
	while (*index < count);
	return false;
}


// ROM 0x00230174 InsertTab__11TXTabsArrayF5TXTab
// ROM BUG (fixed): it does not look at what SearchTab answered, so a
// tab put in twice at the same position gives two entries there rather
// than replacing the one already in it.  The fix replaces it.
NewtonErr
TXTabsArray::InsertTab(TXTab tab)
{
	long index;
	Boolean there = SearchTab((int) tab.fPosition, &index);
	if (RomBugFixed() && there)
		return Replace(index, 1, &tab, 1);
	if (Insert(&tab, 1, index) == nil)
		return kError_No_Memory;
	return noErr;
}


// ROM 0x002301bc RemoveTab__11TXTabsArrayFl
void
TXTabsArray::RemoveTab(long index)
{
	Remove(index, 1);
}


// ROM 0x00230288 WidthToTab__11TXTabsArrayCFl
// The first tab past `width`; past the last of them the default tab
// stops go on for ever, every gTXDefaultTabVal pixels.
TXTab
TXTabsArray::WidthToTab(long width) const
{
	short w = TXRoundFixed(width);
	const TXTab* tab = (const TXTab*) GetElementPtr(0);
	long count = fCount;
	for (long i = 0; i < count; i++, tab++)
	{
		if (w < tab->fPosition)
			return *tab;
	}

	TXTab beyond;
	beyond.fPosition = (w / gTXDefaultTabVal + 1) * gTXDefaultTabVal;
	beyond.fKind = kTXTabLeft;
	// ROM BUG (fixed): it never sets the fill character here, and the
	// six bytes it copies out take in whatever the stack held.  Nothing
	// looks at a left tab's fill character, so it has never shown; we
	// clear it, having no stack rubbish to hand on - which is also the
	// fix, a default tab having no fill, so both ways it is nought.
	beyond.fFillChar = 0;
	return beyond;
}


// ROM 0x00230368 IsEqual__11TXTabsArrayCFPC11TXTabsArray
Boolean
TXTabsArray::IsEqual(const TXTabsArray* other) const
{
	long count = fCount;
	if (other->fCount != count)
		return false;
	for (long i = count - 1; i >= 0; i--)
	{
		TXTab mine = GetIndTab(i);
		TXTab theirs = other->GetIndTab(i);
		if (mine.fPosition != theirs.fPosition
		 || mine.fKind != theirs.fKind
		 || mine.fFillChar != theirs.fFillChar)
			return false;
	}
	return true;
}


// ROM 0x00230420 GetIndTab__11TXTabsArrayCFl
TXTab
TXTabsArray::GetIndTab(long index) const
{
	return *(const TXTab*) GetElementPtr(index);
}


#pragma mark -
/*--------------------------------------------------------------------
	TXRuler and TXBasicRuler.
--------------------------------------------------------------------*/

// ROM 0x00242c28 __ct__7TXRulerFv
TXRuler::TXRuler()
{ }


// ROM 0x0024587c __ct__12TXBasicRulerFv
TXBasicRuler::TXBasicRuler()
{
	fJustification = kTXJustifyLeft;
}


// ROM 0x002459b4 CreateNew__12TXBasicRulerCFv
TXAttrObject*
TXBasicRuler::CreateNew(void) const
{
	return new TXBasicRuler;
}


// ROM 0x002459a8 GetClassId__12TXBasicRulerCFv
long
TXBasicRuler::GetClassId(void) const
{
	return 0x62726c72;		// 'brlr'
}


// ROM 0x002458c4 GetAttributesValues__12TXBasicRulerFP12TXAttrValues
void
TXBasicRuler::GetAttributesValues(TXAttrValues* values)
{
	values->Add(kTXAttrJustification, &fJustification, 1, false);
}


// ROM 0x00245b64 IsEqual__12TXBasicRulerCFPC12TXAttrObject
Boolean
TXBasicRuler::IsEqual(const TXAttrObject* other) const
{
	if (other == this)
		return true;
	if (GetClassId() != other->GetClassId())
		return false;
	return fJustification == ((const TXBasicRuler*) other)->fJustification;
}


// ROM 0x002458f4 Assign__12TXBasicRulerFPC12TXAttrObject
void
TXBasicRuler::Assign(const TXAttrObject* other)
{
	fJustification = ((const TXBasicRuler*) other)->fJustification;
}


// ROM 0x00245bf8 GetAttributeValue__12TXBasicRulerCFUlPv
Boolean
TXBasicRuler::GetAttributeValue(TXAttrTag tag, void* value) const
{
	if (tag != kTXAttrJustification)
		return TXAttrObject::GetAttributeValue(tag, value);
	*(char*) value = fJustification;
	return true;
}


// ROM 0x00245900 SetAttributeValue__12TXBasicRulerFUlPCv
// It does not look at the tag at all: whatever is asked for, the
// justification is what is set.  (Every caller comes through
// TXAdvancedRuler::SetAttributeValue, which has already dealt with the
// tags it knows.)
void
TXBasicRuler::SetAttributeValue(TXAttrTag /*tag*/, const void* value)
{
	fJustification = *(const char*) value;
}


// ROM 0x00245bcc GetCommonAttrValue__12TXBasicRulerCFUlPv
Boolean
TXBasicRuler::GetCommonAttrValue(TXAttrTag tag, void* value) const
{
	if (tag != kTXAttrJustification)
		return TXAttrObject::GetCommonAttrValue(tag, value);
	return fJustification == *(const char*) value;
}


// ROM 0x002459bc GetNSObject__12TXBasicRulerCFv
Ref
TXBasicRuler::GetNSObject(void) const
{
	RefVar frame(Clone(RefVar(Rtxcanonicalruler)));
	RefVar justification;
	if (fJustification == kTXJustifyRight)
		justification = RSSYMright;
	else if (fJustification == kTXJustifyCenter)
		justification = RSSYMcenter;
	else if (fJustification == kTXJustifyFull)
		justification = RSSYMfull;
	else
		justification = RSSYMleft;
	SetFrameSlot(frame, RSSYMjustification, justification);
	return frame;
}


// ROM 0x00245a84 TXJustSymbolToNum__FRC6RefVar
char
TXJustSymbolToNum(RefArg symbol)
{
	if (EQRef(symbol, RSSYMright))
		return kTXJustifyRight;
	if (EQRef(symbol, RSSYMcenter))
		return kTXJustifyCenter;
	if (EQRef(symbol, RSSYMfull))
		return kTXJustifyFull;
	return kTXJustifyLeft;
}


// ROM 0x00245b10 SetNSObject__12TXBasicRulerFRC6RefVar
void
TXBasicRuler::SetNSObject(RefArg obj)
{
	RefVar justification(GetFrameSlotRef(obj, RSSYMjustification));
	fJustification = TXJustSymbolToNum(justification);
}


// ROM 0x0024590c GetLineLeftBlanks__12TXBasicRulerCFUc
long
TXBasicRuler::GetLineLeftBlanks(Boolean /*firstLine*/) const
{
	return 0;
}


// ROM 0x00245914 GetLineRightBlanks__12TXBasicRulerCFv
long
TXBasicRuler::GetLineRightBlanks(void) const
{
	return 0;
}


// ROM 0x0024591c GetTabWidth__12TXBasicRulerCFlT1P12TXPendingTab
// There are no tab stops of its own, so it is always the next default
// one; and a left tab's width is settled as soon as it is met.
long
TXBasicRuler::GetTabWidth(long x, long /*unused*/, TXPendingTab* pending) const
{
	short w = TXRoundFixed(x);
	long at = (w / gTXDefaultTabVal + 1) * gTXDefaultTabVal;
	pending->fTab.fPosition = at;
	pending->fTab.fKind = kTXTabLeft;
	pending->fPending = false;
	pending->fWidth = TXPixelsToFixed(at) - x;
	return pending->fWidth;
}


// ROM 0x00245978 AdjustLineHeight__12TXBasicRulerCFP16TXLineHeightInfo
void
TXBasicRuler::AdjustLineHeight(TXLineHeightInfo* /*info*/) const
{ }


// ROM 0x0024597c CalcPendingTabWidth__12TXBasicRulerCFRC12TXPendingTablT2
long
TXBasicRuler::CalcPendingTabWidth(const TXPendingTab& /*pending*/, long /*textWidth*/, long /*available*/) const
{
	return 0;
}


#pragma mark -
/*--------------------------------------------------------------------
	TXAdvancedRuler.
--------------------------------------------------------------------*/

// ROM 0x0022f2b8 __ct__15TXAdvancedRulerFv
TXAdvancedRuler::TXAdvancedRuler()
{
	fTabs = nil;
	fIndent = 0;
	fLeftMargin = 0;
	fRightMargin = 0;
	fLineSpacing = 1;
}


// ROM 0x0022f314 FreeData__15TXAdvancedRulerFv
void
TXAdvancedRuler::FreeData(void)
{
	if (fTabs != nil)
	{
		delete fTabs;
		fTabs = nil;
	}
	TXAttrObject::FreeData();
}


// ROM 0x0022f834 CreateNew__15TXAdvancedRulerCFv
TXAttrObject*
TXAdvancedRuler::CreateNew(void) const
{
	return new TXAdvancedRuler;
}


// ROM 0x00230234 Reference__15TXAdvancedRulerFv
// A ruler with no tabs is shared like any other attribute object.  One
// *with* tabs cannot be: its TXTabsArray is a plain owned pointer, so
// the answer is a fresh copy instead of another reference.
TXAttrObject*
TXAdvancedRuler::Reference(void)
{
	if (fTabs == nil)
		return TXAttrObject::Reference();

	TXAttrObject* copy = CreateNew();
	if (copy != nil)
		copy->Assign(this);
	return copy;
}


// ROM 0x0023046c GetClassId__15TXAdvancedRulerCFv
long
TXAdvancedRuler::GetClassId(void) const
{
	return 0x72756c72;		// 'rulr'
}


// ROM 0x0022f350 GetAttributesValues__15TXAdvancedRulerFP12TXAttrValues
void
TXAdvancedRuler::GetAttributesValues(TXAttrValues* values)
{
	// the tabs go in as the pointer itself, not owned by the list.
	// (The ROM says 4 for each of the first four; here it is whatever
	// the host makes of a pointer and a long.)
	values->Add(kTXAttrTabs, &fTabs, sizeof(fTabs), false);
	values->Add(kTXAttrIndent, &fIndent, sizeof(fIndent), false);
	values->Add(kTXAttrLeftMargin, &fLeftMargin, sizeof(fLeftMargin), false);
	values->Add(kTXAttrRightMargin, &fRightMargin, sizeof(fRightMargin), false);
	values->Add(kTXAttrLineSpacing, &fLineSpacing, sizeof(fLineSpacing), false);
	TXBasicRuler::GetAttributesValues(values);
}


// ROM 0x0022f424 SetTabs__15TXAdvancedRulerFPC11TXTabsArray
// The array is copied rather than taken over, tab by tab and backwards
// (InsertTab puts each one at its sorted place, so the order does not
// matter).  An empty array is the same as none at all.
void
TXAdvancedRuler::SetTabs(const TXTabsArray* tabs)
{
	if (fTabs != nil)
		delete fTabs;

	long count = 0;
	if (tabs != nil)
		count = tabs->GetCount();
	if (tabs == nil || count == 0)
	{
		fTabs = nil;
		return;
	}

	fTabs = new TXTabsArray;
	if (fTabs == nil)
		return;
	for (long i = count - 1; i >= 0; i--)
		fTabs->InsertTab(tabs->GetIndTab(i));
}


// ROM 0x002304dc EqualTabs__15TXAdvancedRulerCFPC11TXTabsArray
Boolean
TXAdvancedRuler::EqualTabs(const TXTabsArray* other) const
{
	if (other != nil && fTabs != nil)
		return fTabs->IsEqual(other);
	return fTabs == other;		// both of them nothing
}


// ROM 0x0022f4ac Assign__15TXAdvancedRulerFPC12TXAttrObject
void
TXAdvancedRuler::Assign(const TXAttrObject* other)
{
	TXBasicRuler::Assign(other);
	if (other == this)
		return;
	FreeData();
	const TXAdvancedRuler* from = (const TXAdvancedRuler*) other;
	fIndent = from->fIndent;
	fLeftMargin = from->fLeftMargin;
	fRightMargin = from->fRightMargin;
	fLineSpacing = from->fLineSpacing;
	SetTabs(from->fTabs);
}


// ROM 0x002305c4 IsEqual__15TXAdvancedRulerCFPC12TXAttrObject
Boolean
TXAdvancedRuler::IsEqual(const TXAttrObject* other) const
{
	if (!TXBasicRuler::IsEqual(other))
		return false;
	if (other == this)
		return true;
	const TXAdvancedRuler* them = (const TXAdvancedRuler*) other;
	if (fIndent != them->fIndent
	 || fLeftMargin != them->fLeftMargin
	 || fRightMargin != them->fRightMargin
	 || fLineSpacing != them->fLineSpacing)
		return false;
	return EqualTabs(them->fTabs);
}


// ROM 0x00230648 GetAttributeValue__15TXAdvancedRulerCFUlPv
Boolean
TXAdvancedRuler::GetAttributeValue(TXAttrTag tag, void* value) const
{
	switch (tag)
	{
	case kTXAttrIndent:			*(long*) value = fIndent;				return true;
	case kTXAttrLeftMargin:		*(long*) value = fLeftMargin;			return true;
	case kTXAttrRightMargin:	*(long*) value = fRightMargin;			return true;
	case kTXAttrTabs:			*(TXTabsArray**) value = fTabs;			return true;
	case kTXAttrLineSpacing:	*(unsigned char*) value = fLineSpacing;	return true;
	}
	return TXBasicRuler::GetAttributeValue(tag, value);
}


// ROM 0x0022f50c SetAttributeValue__15TXAdvancedRulerFUlPCv
void
TXAdvancedRuler::SetAttributeValue(TXAttrTag tag, const void* value)
{
	switch (tag)
	{
	case kTXAttrIndent:			fIndent = *(const long*) value;			return;
	case kTXAttrLeftMargin:		fLeftMargin = *(const long*) value;		return;
	case kTXAttrRightMargin:	fRightMargin = *(const long*) value;	return;
	case kTXAttrTabs:			SetTabs(*(TXTabsArray* const*) value);	return;
	case kTXAttrLineSpacing:
		{
			// (the ROM stores it and then stores it again clamped; the
			// clamped one is what stays)
			unsigned char spacing = *(const unsigned char*) value;
			fLineSpacing = spacing;
			if (spacing < 1)
				spacing = 1;
			else if (spacing <= 0x14)
				return;
			else
				spacing = 0x14;
			fLineSpacing = spacing;
		}
		return;
	}
	TXBasicRuler::SetAttributeValue(tag, value);
}


// ROM 0x00230478 GetAttributeFlags__15TXAdvancedRulerCFUl
// 1 says the attribute belongs to the whole paragraph rather than to a
// run of characters; 2, that it changes how tall its lines are.
unsigned long
TXAdvancedRuler::GetAttributeFlags(TXAttrTag tag) const
{
	unsigned long flags = 0;
	if (tag == kTXAttrTabs || tag == kTXAttrIndent
	 || tag == kTXAttrLeftMargin || tag == kTXAttrRightMargin)
		flags = 1;
	else if (tag == kTXAttrLineSpacing)
		flags = 2;
	return flags | TXAttrObject::GetAttributeFlags(tag);
}


// ROM 0x00230508 GetCommonAttrValue__15TXAdvancedRulerCFUlPv
Boolean
TXAdvancedRuler::GetCommonAttrValue(TXAttrTag tag, void* value) const
{
	switch (tag)
	{
	case kTXAttrIndent:			return fIndent == *(const long*) value;
	case kTXAttrLeftMargin:		return fLeftMargin == *(const long*) value;
	case kTXAttrRightMargin:	return fRightMargin == *(const long*) value;
	case kTXAttrLineSpacing:	return fLineSpacing == *(const unsigned char*) value;
	case kTXAttrTabs:			return EqualTabs(*(TXTabsArray* const*) value);
	}
	return TXBasicRuler::GetCommonAttrValue(tag, value);
}


// ROM 0x0022f5a8 UpdateAttribute__15TXAdvancedRulerFUlPCvl
// Everything else is simply set; the tabs are not, because a ruler slip
// asks for one tab to be added, moved or taken away rather than for the
// whole array.  `value` is another ruler, whose tab array holds the one
// tab being talked about.
void
TXAdvancedRuler::UpdateAttribute(TXAttrTag tag, const void* value, long how)
{
	if (tag != kTXAttrTabs || how == 0)
	{
		TXAttrObject::UpdateAttribute(tag, value, how);
		return;
	}

	const TXTabUpdate* update = (const TXTabUpdate*) value;
	// the tabs of the ruler the change came from are taken first, so
	// what follows works on a copy of them
	SetTabs(update->fRuler->fTabs);

	long index;
	if (how == kTXTabRemove)
	{
		if (!fTabs->SearchTab(update->fOld.fPosition, &index))
			return;
		fTabs->RemoveTab(index);
		// the last tab gone is no tabs at all
		if (fTabs->GetCount() == 0)
		{
			delete fTabs;
			fTabs = nil;
		}
		return;
	}
	if (how == kTXTabAdd)
	{
		if (fTabs == nil)
		{
			fTabs = new TXTabsArray;
			if (fTabs == nil)
				return;
		}
		fTabs->InsertTab(update->fNew);
		return;
	}
	if (how == kTXTabMove)
	{
		// dragged from where fOld stands to where fNew does
		if (!fTabs->SearchTab(update->fOld.fPosition, &index))
			return;
		fTabs->RemoveTab(index);
		fTabs->InsertTab(update->fNew);
	}
}


// ROM 0x0022f6e0 GetLineLeftBlanks__15TXAdvancedRulerCFUc
// The margins are kept in whole pixels and answered as Fixed.
long
TXAdvancedRuler::GetLineLeftBlanks(Boolean firstLine) const
{
	return TXPixelsToFixed(firstLine ? fIndent : fLeftMargin);
}


// ROM 0x0022f6f4 GetLineRightBlanks__15TXAdvancedRulerCFv
long
TXAdvancedRuler::GetLineRightBlanks(void) const
{
	return TXPixelsToFixed(fRightMargin);
}


// ROM 0x0022f754 GetTabWidth__15TXAdvancedRulerCFlT1P12TXPendingTab
// The first tab stop past `x`, if there is one before the right edge;
// otherwise the next default stop.  A tab that is not a plain left one
// is marked pending, because how wide it is depends on the text that
// follows it.
long
TXAdvancedRuler::GetTabWidth(long x, long right, TXPendingTab* pending) const
{
	if (fTabs != nil)
	{
		TXTab tab = fTabs->WidthToTab(x);
		if (tab.fPosition <= TXRoundFixed(right))
		{
			pending->fTab = tab;
			pending->fPending = tab.fKind != kTXTabLeft;
			pending->fWidth = TXPixelsToFixed(pending->fTab.fPosition) - x;
			return pending->fWidth;
		}
	}

	short w = TXRoundFixed(x);
	pending->fTab.fPosition = (w / gTXDefaultTabVal + 1) * gTXDefaultTabVal;
	pending->fPending = false;
	pending->fWidth = TXPixelsToFixed(pending->fTab.fPosition) - x;
	return pending->fWidth;
}


// ROM 0x0022f700 CalcPendingTabWidth__15TXAdvancedRulerCFRC12TXPendingTablT2
// A centre tab takes half the text's width off its own; any other kind
// takes all of it.  What is left over cannot be more than the line has
// room for, and never less than nothing.
long
TXAdvancedRuler::CalcPendingTabWidth(const TXPendingTab& pending, long textWidth, long available) const
{
	long width;
	if (pending.fTab.fKind == kTXTabCenter)
	{
		width = pending.fWidth - FixedDivide(textWidth, 0x20000);
		if (width > 0)
		{
			available -= width;
			if (available < 0)
				width += available;
		}
	}
	else
		width = pending.fWidth - textWidth;

	if (width < 0)
		width = 0;
	return width;
}


// ROM 0x0022f808 AdjustLineHeight__15TXAdvancedRulerCFP16TXLineHeightInfo
// Single spacing leaves the line alone; every step past it adds half a
// line.  (The +(x >>> 31) before the shift is the compiler's rounding
// of a signed divide by two towards nought.)
void
TXAdvancedRuler::AdjustLineHeight(TXLineHeightInfo* info) const
{
	if (fLineSpacing == 1)
		return;
	long extra = (fLineSpacing - 1) * info->fAscent;
	extra = extra + (long) (((unsigned long) extra) >> 31);
	info->fHeight += extra >> 1;
}


#pragma mark -
/*--------------------------------------------------------------------
	A ruler as a script sees it.
--------------------------------------------------------------------*/

// ROM 0x0022f83c TabKindSymbolToNum__FRC6RefVar
unsigned char
TabKindSymbolToNum(RefArg symbol)
{
	if (EQRef(symbol, RSSYMright))
		return kTXTabRight;
	if (EQRef(symbol, RSSYMcenter))
		return kTXTabCenter;
	if (EQRef(symbol, RSSYMdecimalpoint))
		return kTXTabDecimalPoint;
	return kTXTabLeft;
}


// ROM 0x0022fcec GetNSObject__15TXAdvancedRulerCFv
Ref
TXAdvancedRuler::GetNSObject(void) const
{
	RefVar frame(TXBasicRuler::GetNSObject());
	SetFrameSlot(frame, RSSYMindent, RefVar(MAKEINT(fIndent)));
	SetFrameSlot(frame, RSSYMleftmargin, RefVar(MAKEINT(fLeftMargin)));
	SetFrameSlot(frame, RSSYMrightmargin, RefVar(MAKEINT(fRightMargin)));
	SetFrameSlot(frame, RSSYMlinespacing, RefVar(MAKEINT(fLineSpacing)));

	if (fTabs != nil)
	{
		long count = fTabs->GetCount();
		RefVar tabs(AllocateArray(RSSYMarray, count));
		for (long i = 0; i < count; i++)
		{
			TXTab tab = fTabs->GetIndTab(i);
			RefVar entry(Clone(RefVar(Rtxcanonicaltab)));
			SetFrameSlot(entry, RSSYMvalue, RefVar(MAKEINT(tab.fPosition)));
			RefVar kind;
			if (tab.fKind == kTXTabCenter)
				kind = RSSYMcenter;
			else if (tab.fKind == kTXTabDecimalPoint)
				kind = RSSYMdecimalpoint;
			else if (tab.fKind == kTXTabRight)
				kind = RSSYMright;
			else
				kind = RSSYMleft;
			SetFrameSlot(entry, RSSYMkind, kind);
			SetArraySlotRef(tabs, i, entry);
		}
		SetFrameSlot(frame, RSSYMtabs, tabs);
	}
	return frame;
}


// ROM 0x0022f8c4 FromObject__FRC6RefVarPP11TXTabsArray
// An array of tab frames, each `{value: <pixels>, kind: 'left}`, made
// into a TXTabsArray.  A frame with no value is passed over, and an
// array that held none at all answers nil rather than an empty array.
void
FromObject(RefArg tabs, TXTabsArray** array)
{
	TXTabsArray* made = new TXTabsArray;
	if (made != nil)
	{
		long count = Length(tabs);
		for (long i = 0; i < count; i++)
		{
			RefVar entry(GetArraySlotRef(tabs, i));
			RefVar value(GetFrameSlotRef(entry, RSSYMvalue));
			if (NOTNIL(value))
			{
				RefVar kind(GetFrameSlotRef(entry, RSSYMkind));
				unsigned char which = TabKindSymbolToNum(kind);
				TXTab tab;
				tab.Set((int) RINT(value), (char) which, 0);
				made->InsertTab(tab);
			}
		}
		if (made->GetCount() == 0)
		{
			delete made;
			made = nil;
		}
	}
	*array = made;
}


// ROM 0x0022ff58 SetNSObject__15TXAdvancedRulerFRC6RefVar
// Only the slots the frame actually has are taken; everything else is
// left as it was, which is what lets a slip change one thing.
void
TXAdvancedRuler::SetNSObject(RefArg obj)
{
	TXBasicRuler::SetNSObject(obj);

	RefVar slot(GetFrameSlotRef(obj, RSSYMindent));
	if (ISINT(slot))
		fIndent = RINT(slot);
	slot = GetFrameSlotRef(obj, RSSYMleftmargin);
	if (ISINT(slot))
		fLeftMargin = RINT(slot);
	slot = GetFrameSlotRef(obj, RSSYMrightmargin);
	if (ISINT(slot))
		fRightMargin = RINT(slot);
	slot = GetFrameSlotRef(obj, RSSYMlinespacing);
	if (ISINT(slot))
		fLineSpacing = (unsigned char) RINT(slot);

	slot = GetFrameSlotRef(obj, RSSYMtabs);
	if (IsArray(slot))
	{
		if (fTabs != nil)
			delete fTabs;
		FromObject(slot, &fTabs);
	}
}


// ROM 0x0022fa14 TXGetRulerAttrValues__FRC6RefVar
// The frame turned straight into an attribute list, without a ruler in
// between: only the slots it has go in, so what comes back says what to
// change about a range's rulers and nothing else.
TXAttrValues*
TXGetRulerAttrValues(RefArg ruler)
{
	TXAttrValues* values = new TXAttrValues;
	if (values == nil)
		OutOfMemory();

	RefVar slot(GetFrameSlotRef(ruler, RSSYMjustification));
	if (IsSymbol(slot))
	{
		char justification = TXJustSymbolToNum(slot);
		values->Add(kTXAttrJustification, &justification, sizeof(justification), false);
	}

	long number;
	slot = GetFrameSlotRef(ruler, RSSYMindent);
	if (ISINT(slot))
	{
		number = RINT(slot);
		values->Add(kTXAttrIndent, &number, sizeof(number), false);
	}
	slot = GetFrameSlotRef(ruler, RSSYMleftmargin);
	if (ISINT(slot))
	{
		number = RINT(slot);
		values->Add(kTXAttrLeftMargin, &number, sizeof(number), false);
	}
	slot = GetFrameSlotRef(ruler, RSSYMrightmargin);
	if (ISINT(slot))
	{
		number = RINT(slot);
		values->Add(kTXAttrRightMargin, &number, sizeof(number), false);
	}
	slot = GetFrameSlotRef(ruler, RSSYMlinespacing);
	if (ISINT(slot))
	{
		unsigned char spacing = (unsigned char) RINT(slot);
		values->Add(kTXAttrLineSpacing, &spacing, sizeof(spacing), false);
	}

	slot = GetFrameSlotRef(ruler, RSSYMtabs);
	if (IsArray(slot))
	{
		TXTabsArray* tabs;
		FromObject(slot, &tabs);
		// the list owns this one: whoever takes the list apart frees it
		if (tabs != nil)
			values->Add(kTXAttrTabs, &tabs, sizeof(tabs), true);
	}
	return values;
}
