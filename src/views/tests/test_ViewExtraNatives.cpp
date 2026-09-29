// The view natives outside ViewNatives.cpp (views/ViewExtraNatives.cpp,
// FontNatives.cpp's range extractors and ComputeParagraphHeight,
// PickView.cpp's scroller natives): each called from NewtonScript over a
// root view on an offscreen one-bit map, with the ROM's objects imported
// for the fonts.
#include "View.h"
#include "RootView.h"
#include "ViewFlags.h"
#include "ParagraphView.h"
#include "PickView.h"
#include "Pictures.h"
#include "Fonts.h"
#include "Text.h"
#include "Rects.h"
#include "Ports.h"
#include "Draw.h"
#include "Screen.h"
#include "ByteOrder.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtWorld.h"
#include "REPTranslators.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kWidth = 160;
const long kHeight = 100;
static unsigned char gBits[kWidth * kHeight / 8];
static PixelMap gMap;
static GrafPort gPort;


static Ref
Eval(const char* source)
{
	if (getenv("EVAL_TRACE")) fprintf(stderr, "eval: %s\n", source);
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static long Pixel(long x, long y) { return GetPixel(&gMap, x, y); }


// a bitmap frame of one byte a row
static Ref
MakeBitmap(const unsigned char* rows, long width, long height)
{
	long rowBytes = ((width + 31) / 32) * 4;
	RefVar bits(AllocateBinary(RefVar(Intern((char*) "bits")), kFramBitmapHeaderSize + rowBytes * height));
	unsigned char* data = (unsigned char*) BinaryData(bits);
	memset(data, 0, kFramBitmapHeaderSize + rowBytes * height);
	PutBigEndianHalf(data + 4, (unsigned short) rowBytes);
	PutBigEndianHalf(data + 12, (unsigned short) height);
	PutBigEndianHalf(data + 14, (unsigned short) width);
	for (long y = 0; y < height; y++)
		data[kFramBitmapHeaderSize + y * rowBytes] = rows[y];
	RefVar frame(AllocateFrame());
	Rect bounds;
	SetRect(&bounds, 0, 0, width, height);
	SetFrameSlot(frame, RSSYMbounds, RefVar(ToObject(bounds)));
	SetFrameSlot(frame, RSSYMbits, bits);
	return frame;
}


static void
Refresh()
{
	Eval("RefreshViews()");
}


static void
TestParagraphNatives()
{
	Eval("font := {family: 'espy, face: 0, size: 9}");
	// ComputeParagraphHeight: never under 50; a long text in a narrow box more
	EXPECT(RINT(RefVar(Eval("ComputeParagraphHeight({text: \"Hi\", viewFont: font}, 0, 100)"))) == 50);
	long tall = RINT(RefVar(Eval("ComputeParagraphHeight({text: \"one two three four five six seven eight nine ten eleven twelve thirteen\", viewFont: font}, 0, 30)")));
	EXPECT(tall > 50);

	// ExtractRichStringFromParaSlots: the range cut out; plain with no ink
	EXPECT(NOTNIL(RefVar(Eval("StrEqual(ExtractRichStringFromParaSlots(\"abcdef\", nil, 1, 3), \"bcd\")"))));
	EXPECT(NOTNIL(RefVar(Eval("StrEqual(ExtractRichStringFromParaSlots(\"abcdef\", [6, font], 4, 10), \"ef\")"))));		// clamped to the text
	EXPECT(NOTNIL(RefVar(Eval("StrEqual(ExtractRichStringFromParaSlots(\"abc\", nil, 9, 2), \"\")"))));				// past the end: nothing

	// ExtractRangeAsRichString over an open paragraph
	Eval("ctxPara := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 150, bottom: 40}, "
		 "text: \"hello world\", styles: [11, font]})");
	EXPECT(NOTNIL(RefVar(Eval("StrEqual(ctxPara:ExtractRangeAsRichString(6, 5), \"world\")"))));
	EXPECT(NOTNIL(RefVar(Eval("StrEqual(ctxPara:ExtractRangeAsRichString(0, 5), \"hello\")"))));
	// not the key view: no keyboard input
	EXPECT(ISNIL(RefVar(Eval("ctxPara:KeyboardInput()"))));
	Eval("ctxPara:Close()");
}


static void
TestFormatVertical()
{
	Eval("ctxStack := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 100, bottom: 100}, "
		 "stepChildren: [{viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 10, bottom: 10}}, "
		 "{viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 10, bottom: 30}}], "
		 "fv: func(r, s) FormatVertical(r, s)})");	// (a global that takes its view from self)
	// stacked from the top with no gaps: 5-15, 15-45; the bottom answered
	long bottom = RINT(RefVar(Eval("ctxStack:fv({left: 0, top: 5, right: 50, bottom: 105}, nil)")));
	EXPECT(bottom == 45);
	// spread: the gap is the height (100) less the children's total over
	// ChildrenHeight's count, which is one more than there are (40 / 3 =
	// 13) - the ROM's arithmetic - so 92-102, then 189-219
	bottom = RINT(RefVar(Eval("ctxStack:fv({left: 0, top: 5, right: 50, bottom: 105}, true)")));
	if (bottom != 306) fprintf(stderr, "  spread bottom %ld\n", bottom);
	EXPECT(bottom == 306);
	TView* stack = GetView(RefVar(Eval("ctxStack")));
	EXPECT(stack != nil);
	if (stack != nil)
	{
		TView* first = (TView*) stack->fChildren->At(0);
		if (!(first->viewBounds.top == 92)) fprintf(stderr, "  first child %d-%d\n", first->viewBounds.top, first->viewBounds.bottom);
		EXPECT(first->viewBounds.top == 92 && first->viewBounds.bottom == 102);
	}
	// not a rectangle: thrown (FromObject refuses a non-frame first)
	EXPECT(ISNIL(RefVar(Eval("try ctxStack:fv(3, nil) onexception |evt.ex| do nil"))));
	Eval("ctxStack:Close()");
}


static void
TestGrayShrink()
{
	// a hollow 4 x 4 box in an 8 x 4 bitmap
	static const unsigned char kRows[4] = { 0xf0, 0x90, 0x90, 0xf0 };
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "box")), RefVar(MakeBitmap(kRows, 8, 4)));
	Eval("ctxShrink := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 60, bottom: 30}})");
	Refresh();
	memset(gBits, 0, sizeof(gBits));
	// a transform's second rectangle, offset by the view's top-left: the
	// bitmap drawn at its own size at (20, 10)
	Eval("ctxShrink:GrayShrink(box, {transform: [{}, {left: 0, top: 0, right: 8, bottom: 4}]})");
	EXPECT(Pixel(20, 10) == 1 && Pixel(23, 10) == 1 && Pixel(20, 13) == 1 && Pixel(21, 11) == 0 && Pixel(24, 10) == 0);
	// no transform: the view's bounds, offset by the view's top-left once
	// more (the ROM's doing) - (40, 20)-(80, 40), five times the size
	memset(gBits, 0, sizeof(gBits));
	Eval("ctxShrink:GrayShrink(box, {})");
	EXPECT(Pixel(40, 20) == 1 && Pixel(59, 24) == 1 && Pixel(45, 25) == 0 && Pixel(20, 10) == 0 && Pixel(60, 20) == 0);
	// an integer where the transform's first rectangle goes is refused
	EXPECT(ISNIL(RefVar(Eval("try ctxShrink:GrayShrink(box, {transform: [1, 2]}) onexception |evt.ex.graf| do nil"))));
	Eval("ctxShrink:Close()");
}


static void
TestSyncScroll()
{
	// a roll 50 high over three items 30 high each
	Eval("rollItem := {viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 20, bottom: 30}, height: 30}");
	Eval("rollItems := [Clone(rollItem), Clone(rollItem), Clone(rollItem)]");
	Eval("ctxRoll := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 40, bottom: 50}, index: 0})");
	// down: the first item fits, so on to the second; the second and third
	// are what show, and the first item's height is how far it slides
	RefVar showing(Eval("ctxRoll:SyncScroll(rollItems, 0, 1)"));
	EXPECT(IsArray(showing) && Length(showing) == 2);
	EXPECT(RINT(RefVar(Eval("ctxRoll.index"))) == 1);
	TView* roll = GetView(RefVar(Eval("ctxRoll")));
	EXPECT(roll != nil && roll->fChildren->Count() == 2);
	// down again from the second: on to the third, which shows alone
	showing = Eval("ctxRoll:SyncScroll(rollItems, 1, 1)");
	EXPECT(IsArray(showing) && Length(showing) == 1);
	EXPECT(RINT(RefVar(Eval("ctxRoll.index"))) == 2);
	// past the last: nothing to do
	EXPECT(ISNIL(RefVar(Eval("ctxRoll:SyncScroll(rollItems, 2, 1)"))));
	// up from the third: back to the second, the second and third showing,
	// the new child put in front
	showing = Eval("ctxRoll:SyncScroll(rollItems, 2, -1)");
	EXPECT(IsArray(showing) && Length(showing) == 2);
	EXPECT(RINT(RefVar(Eval("ctxRoll.index"))) == 1);
	EXPECT(roll != nil && roll->fChildren->Count() == 2);
	// up at the top: nothing to do
	Eval("ctxRoll:SyncScroll(rollItems, 1, -1)");
	EXPECT(ISNIL(RefVar(Eval("ctxRoll:SyncScroll(rollItems, 0, -1)"))));
	// (a cursor for the items goes through SyncScrollSoup: host.NewtonSoupScroll)
	Eval("ctxRoll:Close()");
}


static void
TestReflow()
{
	// the format the paper roll's print layout hands ReFlow
	Eval("font := {family: 'espy, face: 0, size: 9}");
	Eval("reflowFormat := {reflowFont: font, unistyle: 'font, textGutter: 16, graphicsGutter: -16, "
		 "viewLineSpacing: 28, pageBounds: {left: 0, top: 0, right: 100, bottom: 200}}");
	Eval("reflowBox := {left: 0, top: 0, right: 100, bottom: 200}");
	// a paragraph, and under it two shapes, the second starting within
	// the (negative) graphics gutter of the first's bottom
	Eval("reflowItems := ["
		 "{viewClass: 81, viewFlags: 1, text: \"Hello world\", viewFont: {family: 'espy, face: 0, size: 12}, "
		 " viewJustify: 0x32, viewBounds: {left: 10, top: 10, right: 150, bottom: 30}},"
		 "{viewClass: 76, viewFlags: 1, viewBounds: {left: 20, top: 40, right: 60, bottom: 70}},"
		 "{viewClass: 76, viewFlags: 1, viewBounds: {left: 70, top: 45, right: 90, bottom: 60}}]");
	Eval("reflowPages := ReFlow(reflowItems, reflowFormat, reflowBox, reflowBox)");
	EXPECT(RINT(RefVar(Eval("Length(reflowPages)"))) == 2);
	// the paragraph on its own, poured through the width: a first group
	// (the ROM's own template, 80 high) with a copy of it, 100 wide,
	// horizontal justification only, and its font at the reflow size
	EXPECT(RINT(RefVar(Eval("reflowPages[0].viewBounds.top"))) == 0);
	EXPECT(RINT(RefVar(Eval("Length(reflowPages[0].viewChildren)"))) == 1);
	EXPECT(RINT(RefVar(Eval("reflowPages[0].viewChildren[0].viewBounds.right"))) == 100);
	EXPECT(RINT(RefVar(Eval("reflowPages[0].viewChildren[0].viewBounds.bottom"))) == 20);
	EXPECT(RINT(RefVar(Eval("reflowPages[0].viewChildren[0].viewJustify"))) == 2);
	EXPECT(RINT(RefVar(Eval("GetFontSize(reflowPages[0].viewChildren[0].viewFont)"))) == 9);
	EXPECT(NOTNIL(RefVar(Eval("StrEqual(reflowPages[0].viewChildren[0].text, \"Hello world\")"))));
	// the shapes a group of two (canonicalGroup, 20 down), each moved to
	// the group's top left
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewBounds.top"))) == 20);
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewJustify"))) == 0xA010);
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewLineSpacing"))) == 28);
	EXPECT(RINT(RefVar(Eval("Length(reflowPages[1].viewChildren)"))) == 2);
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewChildren[0].viewBounds.left"))) == 0);
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewChildren[0].viewBounds.bottom"))) == 30);
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewChildren[1].viewBounds.left"))) == 50);
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewChildren[1].viewBounds.top"))) == 5);
	// the originals are left alone
	EXPECT(RINT(RefVar(Eval("reflowItems[1].viewBounds.left"))) == 20);

	// a paragraph longer than its own height at the new width is cut where
	// its lines stop and carried on in pieces, each a first group, which
	// put back together are the text again
	Eval("reflowLong := {viewClass: 81, viewFlags: 1, viewFont: font, viewBounds: {left: 0, top: 0, right: 100, bottom: 24}, "
		 "text: \"one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen\"}");
	Eval("reflowPages := ReFlow([reflowLong], reflowFormat, reflowBox, {left: 0, top: 0, right: 60, bottom: 200})");
	long pieces = RINT(RefVar(Eval("Length(reflowPages)")));
	EXPECT(pieces > 2);
	EXPECT(NOTNIL(RefVar(Eval("begin local s := \"\"; foreach g in reflowPages do s := s & g.viewChildren[0].text; StrEqual(s, reflowLong.text) end"))));
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewBounds.top"))) == 0);
	EXPECT(RINT(RefVar(Eval("reflowPages[1].viewChildren[0].viewBounds.right"))) == 60);

	// nothing to do: nil
	EXPECT(ISNIL(RefVar(Eval("ReFlow(nil, reflowFormat, reflowBox, reflowBox)"))));
	EXPECT(ISNIL(RefVar(Eval("ReFlow(reflowItems, reflowFormat, {left: 0, top: 0}, reflowBox)"))));

	// the fonts a page uses, each once
	Eval("preflight := ReflowPreflight([{viewFont: font}, {styles: [3, {family: 'espy, face: 1, size: 12}, 2, font]}, {}])");
	EXPECT(RINT(RefVar(Eval("Length(preflight.family)"))) == 1);
	EXPECT(Eval("preflight.family[0]") == RSSYMespy);
	EXPECT(RINT(RefVar(Eval("Length(preflight.size)"))) == 2);
	EXPECT(RINT(RefVar(Eval("Length(preflight.face)"))) == 2);
	EXPECT(RINT(RefVar(Eval("Length(preflight.styles)"))) == 2);
	// a copy each time: the ROM's frame is not added to
	EXPECT(RINT(RefVar(Eval("Length(ReflowPreflight(nil).family)"))) == 0);
}


static void
TestDebugging()
{
	extern Boolean gOutlineViews;
	extern long gSlowMotion;
	Eval("ViewAutopsy(3)");
	EXPECT(gSlowMotion == 3);
	Eval("ViewAutopsy(0)");
	EXPECT(gSlowMotion == 0);
	Boolean was = gOutlineViews;
	Eval("ViewAutopsy(nil)");
	EXPECT(gOutlineViews == !was);
	Eval("ViewAutopsy(nil)");
	EXPECT(gOutlineViews == was);
	// a view that is not there: said so, nothing else
	EXPECT(ISNIL(RefVar(Eval("DV('noSuchView)"))));
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_ViewExtraNatives: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();
	InitGraf();
	InitFonts();
	RegisterTextNatives();
	RegisterViewNatives();
	RegisterViewExtraNatives();
	RegisterPortNatives();
	InstallHostNatives();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(list, i)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfonts, fonts);
	{
		RefVar config(AllocateFrame());
		SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, config);
		RefVar intl(AllocateFrame());
		SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(AllocateFrame()));
		SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	}
	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = kWidth / 8;
	SetRect(&gMap.bounds, 0, 0, kWidth, kHeight);
	gMap.pixMapFlags = kPixMapPtr | 1;
	gMap.deviceRes.v = kDefaultDPI;
	gMap.deviceRes.h = kDefaultDPI;
	gMap.grayTable = nil;
	OpenPort(&gPort);
	SetPortBits(&gMap);
	gPort.portRect = gMap.bounds;
	RectRgn(gPort.visRgn, &gMap.bounds);
	memset(gBits, 0, sizeof(gBits));
	newton_try
	{
		InitViewSystem();
		gNewtIsAliveAndWell = true;
		// the root the host builds without a template has a short list of
		// methods (MakeViewMethods); on the machine its proto is the ROM's
		// viewroot, whose native methods bind to what is registered - so the
		// ones under test are copied from there
		{
			RefVar proto(GetFrameSlotRef(RefVar(gRootView->fContext), RSSYM_proto));
			static const char* kMethods[] = { "KeyboardInput", "ExtractRangeAsRichString", "GrayShrink",
											  "SyncScroll", nil };
			for (long i = 0; kMethods[i] != nil; i++)
			{
				RefVar name(Intern((char*) kMethods[i]));
				SetFrameSlot(proto, name, RefVar(GetFrameSlotRef(RefVar(Rviewroot), name)));
			}
		}
		Refresh();
		TestParagraphNatives();
		TestFormatVertical();
		TestGrayShrink();
		TestSyncScroll();
		TestReflow();
		TestDebugging();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: exception %s\n", CurrentException()->name);
	}
	end_try;
	if (failures == 0)
		printf("test_ViewExtraNatives: all passed\n");
	return failures != 0;
}
