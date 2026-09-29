// The text engine's rulers: the tab stops and the array that keeps them
// sorted, the two rulers' attributes and how they compare, the blanks
// and tab widths a line is laid out with, the line spacing, and the
// rulers as a script sees them (which wants the ROM's canonical frames,
// so the image is imported).
#include "TXRuler.h"
#include "Frames.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "ObjectHeap.h"
#include "OSErrors.h"
#include "FixedMath.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// (qd/Ports.h has the real one; the text engine does not link QuickDraw)
static long
ToFixed(long n)
{
	return (long) (int) ((unsigned int) n << 16);
}


static void
TestTabs()
{
	// a decimal-point tab with no fill character asked for gets a full
	// stop; nothing else does
	TXTab tab;
	tab.Set(100, kTXTabDecimalPoint, 0);
	EXPECT(tab.fPosition == 100 && tab.fKind == kTXTabDecimalPoint && tab.fFillChar == '.');
	tab.Set(100, kTXTabDecimalPoint, '-');
	EXPECT(tab.fFillChar == '-');
	tab.Set(100, kTXTabLeft, 0);
	EXPECT(tab.fFillChar == 0);

	// put in out of order, they come out sorted
	TXTabsArray tabs;
	EXPECT(tabs.GetCount() == 0);
	tab.Set(200, kTXTabRight, 0);		EXPECT(tabs.InsertTab(tab) == noErr);
	tab.Set(50, kTXTabLeft, 0);			EXPECT(tabs.InsertTab(tab) == noErr);
	tab.Set(120, kTXTabCenter, 0);		EXPECT(tabs.InsertTab(tab) == noErr);
	EXPECT(tabs.GetCount() == 3);
	EXPECT(tabs.GetIndTab(0).fPosition == 50);
	EXPECT(tabs.GetIndTab(1).fPosition == 120 && tabs.GetIndTab(1).fKind == kTXTabCenter);
	EXPECT(tabs.GetIndTab(2).fPosition == 200 && tabs.GetIndTab(2).fKind == kTXTabRight);

	long index = -1;
	EXPECT(tabs.SearchTab(120, &index) && index == 1);
	// not there: the index is where it would go
	EXPECT(!tabs.SearchTab(60, &index) && index == 1);
	EXPECT(!tabs.SearchTab(500, &index) && index == 3);

	// the first tab past a width (a Fixed, rounded to whole pixels)
	EXPECT(tabs.WidthToTab(ToFixed(0)).fPosition == 50);
	EXPECT(tabs.WidthToTab(ToFixed(50)).fPosition == 120);
	EXPECT(tabs.WidthToTab(ToFixed(119)).fPosition == 120);
	// past the last of them, the default stops go on for ever
	EXPECT(tabs.WidthToTab(ToFixed(200)).fPosition == 210);
	EXPECT(tabs.WidthToTab(ToFixed(215)).fPosition == 240);
	EXPECT(tabs.WidthToTab(ToFixed(200)).fKind == kTXTabLeft);

	// two arrays are equal when every tab matches
	TXTabsArray other;
	tab.Set(50, kTXTabLeft, 0);			other.InsertTab(tab);
	tab.Set(120, kTXTabCenter, 0);		other.InsertTab(tab);
	tab.Set(200, kTXTabRight, 0);		other.InsertTab(tab);
	EXPECT(tabs.IsEqual(&other) && other.IsEqual(&tabs));
	other.RemoveTab(1);
	EXPECT(other.GetCount() == 2 && !tabs.IsEqual(&other));
	tab.Set(120, kTXTabLeft, 0);		other.InsertTab(tab);
	EXPECT(other.GetCount() == 3 && !tabs.IsEqual(&other));		// the kind differs

	// BUG (the ROM's): the same position twice gives two entries
	tab.Set(50, kTXTabLeft, 0);
	EXPECT(other.InsertTab(tab) == noErr);
	EXPECT(other.GetCount() == 4 && other.GetIndTab(0).fPosition == 50
		&& other.GetIndTab(1).fPosition == 50);
}


static void
TestBasicRuler()
{
	TXBasicRuler ruler;
	EXPECT(ruler.fJustification == kTXJustifyLeft);
	EXPECT(ruler.GetClassId() == 0x62726c72);		// 'brlr'
	EXPECT(ruler.GetLineLeftBlanks(true) == 0 && ruler.GetLineRightBlanks() == 0);

	char justification = kTXJustifyFull;
	ruler.SetAttributeValue(kTXAttrJustification, &justification);
	justification = 0;
	EXPECT(ruler.GetAttributeValue(kTXAttrJustification, &justification));
	EXPECT(justification == kTXJustifyFull);
	EXPECT(ruler.GetCommonAttrValue(kTXAttrJustification, &justification));

	TXBasicRuler other;
	EXPECT(!ruler.IsEqual(&other));
	other.Assign(&ruler);
	EXPECT(ruler.IsEqual(&other) && other.fJustification == kTXJustifyFull);

	// every attribute it has is one
	TXAttrValues values;
	ruler.GetAttributesValues(&values);
	EXPECT(values.GetCount() == 1);
	TXAttrTag tag = 0;
	int length = 0;
	char value = 0;
	values.GetIndAttrData(0, &tag, &value, &length);
	EXPECT(tag == kTXAttrJustification && value == kTXJustifyFull);

	// the default tab stops, and nothing else
	TXPendingTab pending;
	memset(&pending, 0, sizeof(pending));
	long width = ruler.GetTabWidth(ToFixed(10), ToFixed(300), &pending);
	EXPECT(pending.fTab.fPosition == 30 && !pending.fPending);
	EXPECT(width == ToFixed(20));
	EXPECT(ruler.CalcPendingTabWidth(pending, ToFixed(5), ToFixed(100)) == 0);

	// and a line's height is whatever it was
	TXLineHeightInfo info;
	info.fHeight = 14;
	info.fAscent = 14;
	ruler.AdjustLineHeight(&info);
	EXPECT(info.fHeight == 14);
}


static void
TestAdvancedRuler()
{
	TXAdvancedRuler ruler;
	EXPECT(ruler.GetClassId() == 0x72756c72);		// 'rulr'
	EXPECT(ruler.fTabs == nil && ruler.fLineSpacing == 1);

	long margin = 20;
	ruler.SetAttributeValue(kTXAttrLeftMargin, &margin);
	margin = 8;
	ruler.SetAttributeValue(kTXAttrIndent, &margin);
	margin = 300;
	ruler.SetAttributeValue(kTXAttrRightMargin, &margin);
	// the first line is indented, the rest are at the margin
	EXPECT(ruler.GetLineLeftBlanks(true) == ToFixed(8));
	EXPECT(ruler.GetLineLeftBlanks(false) == ToFixed(20));
	EXPECT(ruler.GetLineRightBlanks() == ToFixed(300));

	// the line spacing is held between one and twenty
	unsigned char spacing = 0;
	ruler.SetAttributeValue(kTXAttrLineSpacing, &spacing);
	EXPECT(ruler.fLineSpacing == 1);
	spacing = 99;
	ruler.SetAttributeValue(kTXAttrLineSpacing, &spacing);
	EXPECT(ruler.fLineSpacing == 0x14);
	spacing = 2;
	ruler.SetAttributeValue(kTXAttrLineSpacing, &spacing);
	EXPECT(ruler.fLineSpacing == 2);

	// one and a half spacing adds half a line
	TXLineHeightInfo info;
	info.fHeight = 14;
	info.fAscent = 14;
	ruler.AdjustLineHeight(&info);
	EXPECT(info.fHeight == 21);
	spacing = 3;
	ruler.SetAttributeValue(kTXAttrLineSpacing, &spacing);
	info.fHeight = 14;
	ruler.AdjustLineHeight(&info);
	EXPECT(info.fHeight == 28);

	// the tabs are copied, not taken over
	TXTabsArray tabs;
	TXTab tab;
	tab.Set(60, kTXTabLeft, 0);			tabs.InsertTab(tab);
	tab.Set(150, kTXTabCenter, 0);		tabs.InsertTab(tab);
	ruler.SetTabs(&tabs);
	EXPECT(ruler.fTabs != nil && ruler.fTabs != &tabs && ruler.fTabs->GetCount() == 2);
	EXPECT(ruler.EqualTabs(&tabs));
	// an empty array is the same as none at all
	TXTabsArray none;
	ruler.SetTabs(&none);
	EXPECT(ruler.fTabs == nil);
	ruler.SetTabs(&tabs);

	// a centre tab is pending: how wide it is waits on the text
	TXPendingTab pending;
	memset(&pending, 0, sizeof(pending));
	long width = ruler.GetTabWidth(ToFixed(10), ToFixed(400), &pending);
	EXPECT(pending.fTab.fPosition == 60 && !pending.fPending && width == ToFixed(50));
	width = ruler.GetTabWidth(ToFixed(60), ToFixed(400), &pending);
	EXPECT(pending.fTab.fPosition == 150 && pending.fPending);
	EXPECT(width == ToFixed(90));
	// forty pixels of text under a centre tab takes twenty off its width
	EXPECT(ruler.CalcPendingTabWidth(pending, ToFixed(40), ToFixed(400)) == ToFixed(70));
	// but never more than the line has left
	EXPECT(ruler.CalcPendingTabWidth(pending, ToFixed(40), ToFixed(50)) == ToFixed(50));
	// and never less than nothing
	EXPECT(ruler.CalcPendingTabWidth(pending, ToFixed(400), ToFixed(400)) == 0);
	// past the last tab, the default stops again
	width = ruler.GetTabWidth(ToFixed(200), ToFixed(400), &pending);
	EXPECT(pending.fTab.fPosition == 210 && !pending.fPending);
	// unless the right edge is nearer than the next tab
	width = ruler.GetTabWidth(ToFixed(10), ToFixed(40), &pending);
	EXPECT(pending.fTab.fPosition == 30);

	// what it is made of, as attributes: its five and the base's one
	TXAttrValues values;
	ruler.GetAttributesValues(&values);
	EXPECT(values.GetCount() == 6);

	// a copy is equal to it
	TXAdvancedRuler copy;
	EXPECT(!ruler.IsEqual(&copy));
	copy.Assign(&ruler);
	EXPECT(ruler.IsEqual(&copy) && copy.fTabs != ruler.fTabs);
	copy.fLeftMargin = 21;
	EXPECT(!ruler.IsEqual(&copy));

	// and Reference answers a copy rather than another reference,
	// because the tab array belongs to one ruler alone
	TXAttrObject* shared = ruler.Reference();
	EXPECT(shared != &ruler && shared->IsEqual(&ruler));
	shared->Free();			// which is what frees its tab array
	ruler.SetTabs(nil);
	EXPECT(ruler.Reference() == &ruler && ruler.GetCountReferences() == 2);

	// the attribute flags say what belongs to a whole paragraph and
	// what changes how tall its lines are
	EXPECT(ruler.GetAttributeFlags(kTXAttrLeftMargin) == 1);
	EXPECT(ruler.GetAttributeFlags(kTXAttrTabs) == 1);
	EXPECT(ruler.GetAttributeFlags(kTXAttrLineSpacing) == 2);
	EXPECT(ruler.GetAttributeFlags(kTXAttrJustification) == 0);
}


static void
TestTabUpdate()
{
	// one change to a ruler's tabs, as the ruler slip makes it
	TXAdvancedRuler from;
	TXTabsArray tabs;
	TXTab tab;
	tab.Set(60, kTXTabLeft, 0);			tabs.InsertTab(tab);
	tab.Set(150, kTXTabCenter, 0);		tabs.InsertTab(tab);
	from.SetTabs(&tabs);

	TXTabUpdate update;
	memset(&update, 0, sizeof(update));
	update.fRuler = &from;

	TXAdvancedRuler ruler;
	// added
	update.fNew.Set(90, kTXTabRight, 0);
	ruler.UpdateAttribute(kTXAttrTabs, &update, kTXTabAdd);
	EXPECT(ruler.fTabs != nil && ruler.fTabs->GetCount() == 3);
	EXPECT(ruler.fTabs->GetIndTab(1).fPosition == 90);

	// moved
	update.fOld.Set(60, kTXTabLeft, 0);
	update.fNew.Set(200, kTXTabLeft, 0);
	ruler.UpdateAttribute(kTXAttrTabs, &update, kTXTabMove);
	EXPECT(ruler.fTabs->GetCount() == 2);
	EXPECT(ruler.fTabs->GetIndTab(0).fPosition == 150);
	EXPECT(ruler.fTabs->GetIndTab(1).fPosition == 200);

	// and taken away; the last one gone leaves no array at all
	update.fOld.Set(60, kTXTabLeft, 0);
	ruler.UpdateAttribute(kTXAttrTabs, &update, kTXTabRemove);
	EXPECT(ruler.fTabs->GetCount() == 1);
	TXTabsArray one;
	tab.Set(150, kTXTabCenter, 0);
	one.InsertTab(tab);
	from.SetTabs(&one);
	update.fOld.Set(150, kTXTabCenter, 0);
	ruler.UpdateAttribute(kTXAttrTabs, &update, kTXTabRemove);
	EXPECT(ruler.fTabs == nil);
}


static void
TestScriptObject()
{
	// the two rulers as a script sees them
	TXAdvancedRuler ruler;
	ruler.fJustification = kTXJustifyCenter;
	ruler.fIndent = 8;
	ruler.fLeftMargin = 20;
	ruler.fRightMargin = 300;
	ruler.fLineSpacing = 2;
	TXTabsArray tabs;
	TXTab tab;
	tab.Set(60, kTXTabDecimalPoint, 0);		tabs.InsertTab(tab);
	tab.Set(150, kTXTabRight, 0);			tabs.InsertTab(tab);
	ruler.SetTabs(&tabs);

	RefVar frame(ruler.GetNSObject());
	EXPECT(IsFrame(frame));
	EXPECT(EQRef(GetFrameSlotRef(frame, RSSYMjustification), RSSYMcenter));
	EXPECT(RINT(GetFrameSlotRef(frame, RSSYMindent)) == 8);
	EXPECT(RINT(GetFrameSlotRef(frame, RSSYMleftmargin)) == 20);
	EXPECT(RINT(GetFrameSlotRef(frame, RSSYMrightmargin)) == 300);
	EXPECT(RINT(GetFrameSlotRef(frame, RSSYMlinespacing)) == 2);
	RefVar array(GetFrameSlotRef(frame, RSSYMtabs));
	EXPECT(IsArray(array) && Length(array) == 2);
	RefVar entry(GetArraySlotRef(array, 0));
	EXPECT(RINT(GetFrameSlotRef(entry, RSSYMvalue)) == 60);
	EXPECT(EQRef(GetFrameSlotRef(entry, RSSYMkind), RSSYMdecimalpoint));
	entry = GetArraySlotRef(array, 1);
	EXPECT(EQRef(GetFrameSlotRef(entry, RSSYMkind), RSSYMright));

	// and back again
	TXAdvancedRuler back;
	back.SetNSObject(frame);
	EXPECT(back.IsEqual(&ruler));

	// a frame with one slot changes only that one
	RefVar one(AllocateFrame());
	SetFrameSlot(one, RSSYMleftmargin, RefVar(MAKEINT(44)));
	back.SetNSObject(one);
	EXPECT(back.fLeftMargin == 44 && back.fIndent == 8 && back.fLineSpacing == 2);

	// the same frame straight to an attribute list, with no ruler made
	TXAttrValues* values = TXGetRulerAttrValues(one);
	EXPECT(values->GetCount() == 1);
	TXAttrTag tag = 0;
	int length = 0;
	long margin = 0;
	values->GetIndAttrData(0, &tag, &margin, &length);
	EXPECT(tag == kTXAttrLeftMargin && margin == 44);
	delete values;

	// the whole frame gives all six, the tabs among them - and that one
	// the list owns, so taking the list apart frees the array
	values = TXGetRulerAttrValues(frame);
	EXPECT(values->GetCount() == 6);
	TXTabsArray* made = nil;
	EXPECT(values->GetValue(kTXAttrTabs, &made) && made != nil && made->GetCount() == 2);
	delete values;
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_TXRuler: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();

	// what Textension::TextensionStart does before anything asks a
	// ruler for a tab
	gTXDefaultTabVal = 30;

	TestTabs();
	TestBasicRuler();
	TestAdvancedRuler();
	TestTabUpdate();
	TestScriptObject();
	printf("test_TXRuler: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
