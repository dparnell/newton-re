// View system test: a root view over an offscreen one-bit map; views made
// from NewtonScript templates - their contexts, bounds justified against
// the parent and the previous sibling (every viewJustify alignment, the
// ratios, the round trip through DejustifyBounds), their children - drawn
// through RefreshViews and checked pixel by pixel (fills, frames, pens,
// insets, shadows, rounded corners, lines, clipping to the parent, the
// order of overlapping views); the view scripts (viewSetupFormScript,
// viewSetupDoneScript, viewDrawScript, viewShowScript, viewHideScript,
// viewQuitScript, viewChangedScript, viewTie); SetValue syncing the
// bounds, Show/Hide/Close, MoveBehind, SetOrigin, the update regions.
// Runs over a standalone kernel heap with the ROM's objects imported (for
// the fonts of the text views).
#include "GrayShrink.h"
#include "RootView.h"
#include "Locale.h"
#include "TextView.h"
#include "ParagraphView.h"
#include "Hilites.h"
#include "LargeBinaries.h"
#include "ContainerView.h"
#include "EditView.h"
#include "Ink.h"
#include "Paths.h"
#include "SoundSettings.h"		// FConvertToSoundFrame
#include "StrokeBundle.h"
#include "Polygons.h"
#include "InkFont.h"
#include "InkShapes.h"
#include "RichString.h"
#include "CICCodec.h"
#include "GaugeView.h"
#include "PickView.h"
#include "ClipboardView.h"
#include "DragDrop.h"
#include "DrawShape.h"
#include "ListView.h"
#include "MeetingView.h"
#include "SliderView.h"
#include "PicPlay.h"
#include "Journal.h"
#include "TabletBuffer.h"
#include "Commands.h"
#include "Keyboard.h"
#include "RecConfig.h"
#include "REPTranslators.h"
#include "Bits.h"
#include "Application.h"
#include "UnitPublic.h"
#include "MonthView.h"
#include "Recognizer.h"
#include "StrokeCentral.h"
#include "WRecDomain.h"
#include "WordUnit.h"
#include "WordList.h"
#include "WordInfo.h"
#include "CorrectInfo.h"
#include "Words.h"
#include "Dictionaries.h"
#include "Controller.h"
#include "StrokeQueue.h"
#include "HostTablet.h"
#include "hal/host/Host.h"
#include "StyleRuns.h"
#include "Rects.h"
#include "Ports.h"
#include "Screen.h"
#include "Draw.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Compiler.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "Fonts.h"
#include "Text.h"
#include "Pictures.h"
#include "ByteOrder.h"
#include "PolygonView.h"
#include "DrawShape.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host/RomBugs.h"

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


static TView*
ViewOf(const char* source)
{
	return GetView(RefVar(Eval(source)));
}


static Boolean
BoundsAre(TView* view, long left, long top, long right, long bottom)
{
	Boolean same = view->viewBounds.left == left && view->viewBounds.top == top && view->viewBounds.right == right && view->viewBounds.bottom == bottom;
	if (!same)
		fprintf(stderr, "  bounds [%d,%d,%d,%d], expected [%ld,%ld,%ld,%ld]\n", view->viewBounds.left, view->viewBounds.top, view->viewBounds.right, view->viewBounds.bottom, left, top, right, bottom);
	return same;
}


// every pixel of the map against a predicate; the first difference reported
typedef long (*Expected)(long x, long y);

static Boolean
MapIs(Expected expected, const char* what)
{
	long wrong = 0;
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
		{
			long value = GetPixel(&gMap, x, y);
			long want = expected(x, y);
			if (value != want)
			{
				if (wrong == 0)
					fprintf(stderr, "  %s: first difference at (%ld, %ld): pixel %ld, expected %ld\n", what, x, y, value, want);
				wrong++;
			}
		}
	return wrong == 0;
}

static Boolean In(long x, long y, long l, long t, long r, long b) { return l <= x && x < r && t <= y && y < b; }
static long Pixel(long x, long y) { return GetPixel(&gMap, x, y); }


static void
Refresh()
{
	Eval("RefreshViews()");
}


static void
TestStructure()
{
	// the root view: its context, flags, bounds, the port's size
	TRootView* root = gRootView;
	EXPECT(root != nil && root->ClassID() == clRootView && root->DerivedFrom(clView));
	EXPECT(root->fParent == root && root->fChildren == TView::gEmptyViewList);
	EXPECT(BoundsAre(root, 0, 0, kWidth, kHeight));
	EXPECT((root->fFlags & (vVisible | vApplication)) == (vVisible | vApplication));
	EXPECT(GetView(RefVar(root->fContext)) == root);
	EXPECT(GetView(RefVar(Eval("GetRoot()"))) == root);
	EXPECT(RINT(Eval("GetViewFlags(GetRoot())")) == (long) root->fFlags);
	EXPECT(NOTNIL(Eval("Visible(GetRoot())")));

	// a view from a template: the context, the fields
	Eval("templA := {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewFormat: 5, debug: 'a}");
	TView* a = ViewOf("ctxA := AddView(GetRoot(), templA)");
	EXPECT(a != nil && a->ClassID() == clView);
	EXPECT(a->fParent == root && root->fChildren->Count() == 1 && root->fChildren->At(0) == a);
	EXPECT(EQRef(GetFrameSlotRef(a->fContext, RSSYM_proto), Eval("templA")));
	EXPECT(EQRef(GetFrameSlotRef(a->fContext, RSSYM_parent), root->fContext));
	EXPECT(RefToAddress(GetFrameSlotRef(a->fContext, RSSYMviewcobject)) == a);
	EXPECT((a->fFlags & vVisible) && (a->fFlags & vIsInSetupForm) == 0);
	EXPECT(a->fViewFormat == vfFillBlack);
	EXPECT(BoundsAre(a, 10, 10, 60, 40));
	EXPECT(a->HasVisRgn() && a->Clipper() != nil && !EmptyRgn(a->Clipper()->fFullRgn));
	EXPECT(NOTNIL(Eval("GetRoot().viewChildren")) && RINT(Eval("Length(GetRoot().viewChildren)")) == 1);
	EXPECT(a->fId > root->fId);
	EXPECT(GetView(RefVar(Eval("ctxA"))) == a && root->FindID(a->fId) == a);
	// the slot cache: viewBounds is there, viewDrawScript is not
	EXPECT(NOTNIL(a->GetCacheProto(kIndexViewBounds)));
	EXPECT(ISNIL(a->GetCacheProto(kIndexViewDrawScript)) && (a->fSlotCache & (1UL << kIndexViewDrawScript)) == 0);
	a->InvalidateSlotCache(kIndexViewDrawScript);
	EXPECT((a->fSlotCache & (1UL << kIndexViewDrawScript)) != 0);
	// GetValue/SetValue
	EXPECT(RINT(Eval("GetDynamicValue(ctxA, 'viewFormat, nil)")) == vfFillBlack);
	EXPECT(RINT(Eval("GetDynamicValue(ctxA, 'viewFlags, nil)")) == (long) (a->fFlags & vViewFlagsMask));
	Eval("SetValue(ctxA, 'viewFormat, 3)");
	EXPECT(a->fViewFormat == vfFillGray && RINT(Eval("ctxA.viewFormat")) == vfFillGray);
	Eval("SetValue(ctxA, 'viewFormat, 5)");

	// children of a view: relative to the parent's bounds
	Eval("templA.viewChildren := [{viewClass: 74, viewFlags: 1, viewBounds: {left: 5, top: 5, right: 25, bottom: 15}, debug: 'a1}]");
	Eval("ctxA:SyncChildren()");
	EXPECT(a->fChildren->Count() == 1);
	TView* a1 = a->fChildren->At(0);
	EXPECT(BoundsAre(a1, 15, 15, 35, 25) && a1->fParent == a && !a1->HasVisRgn());
	EXPECT(RINT(Eval("Length(ctxA:ChildViewFrames())")) == 1);
	EXPECT(GetView(RefVar(Eval("ctxA:ChildViewFrames()[0]"))) == a1);
	EXPECT(EQRef(Eval("ctxA:ChildViewFrames()[0]:Parent()"), Eval("ctxA")));
	EXPECT(GetView(RefVar(Eval("GetView('viewFrontMost)"))) == root);	// a is not an application
	// GlobalBox/LocalBox
	EXPECT(RINT(Eval("ctxA:GlobalBox().right")) == 60 && RINT(Eval("ctxA:LocalBox().right")) == 50 && RINT(Eval("ctxA:LocalBox().top")) == 0);
	EXPECT(RINT(Eval("ctxA:GlobalOuterBox().right")) == 60);

	// RemoveView takes the child out and deletes its view
	Eval("RemoveView(ctxA, ctxA:ChildViewFrames()[0])");
	EXPECT(a->fChildren == TView::gEmptyViewList && RINT(Eval("Length(templA.viewChildren)")) == 0);
	// Close removes the view itself
	Eval("ctxA:Close()");
	EXPECT(root->fChildren == TView::gEmptyViewList && ISNIL(Eval("ctxA.viewCObject")));
	EXPECT(RINT(Eval("Length(GetRoot().viewChildren)")) == 1);		// Close leaves the template in viewChildren (RemoveView takes it out)
	Eval("RemoveSlot(GetRoot(), 'viewChildren)");
}


static long ExpA(long x, long y)		{ return In(x, y, 10, 10, 60, 40); }
static long ExpAMoved(long x, long y)	{ return In(x, y, 20, 20, 70, 50); }
static long ExpWhite(long, long)		{ return 0; }
static long ExpFramed(long x, long y)	{ return In(x, y, 8, 8, 62, 42) && !In(x, y, 10, 10, 60, 40); }		// the frame lies outside the bounds, pen wide
static long ExpInset(long x, long y)	{ return In(x, y, 7, 7, 63, 43) && !In(x, y, 8, 8, 62, 42); }
static long ExpShadow(long x, long y)	{ return (In(x, y, 9, 9, 61, 41) && !In(x, y, 10, 10, 60, 40)) || In(x, y, 61, 11, 63, 43) || In(x, y, 11, 41, 63, 43); }
static long ExpLines(long x, long y)	{ return In(x, y, 10, 10, 60, 40) && ((y - 10) % 10) == 9; }
static long ExpChild(long x, long y)	{ return In(x, y, 10, 10, 60, 40) != In(x, y, 40, 30, 60, 40); }


static void
TestDrawing()
{
	// a black filled view on the white root
	Eval("templB := {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewFormat: 5}");
	TView* b = ViewOf("ctxB := AddView(GetRoot(), templB)");
	EXPECT(b != nil);		// AddView draws nothing: the view must be dirtied (or shown)
	Eval("ctxB:Dirty()");
	EXPECT(gRootView->NeedsUpdate());
	Refresh();
	EXPECT(!gRootView->NeedsUpdate());
	EXPECT(MapIs(ExpA, "filled"));

	// SetValue of the bounds syncs the view: the old place erased, the new drawn
	Eval("SetValue(ctxB, 'viewBounds, {left: 20, top: 20, right: 70, bottom: 50})");
	EXPECT(BoundsAre(b, 20, 20, 70, 50));
	Refresh();
	EXPECT(MapIs(ExpAMoved, "moved"));
	Eval("SetValue(ctxB, 'viewBounds, {left: 10, top: 10, right: 60, bottom: 40})");
	Refresh();
	EXPECT(MapIs(ExpA, "moved back"));

	// Hide erases, Show draws again
	Eval("ctxB:Hide()");
	EXPECT((b->fFlags & vVisible) == 0 && (RINT(Eval("ctxB.viewFlags")) & vVisible) == 0);
	Refresh();
	EXPECT(MapIs(ExpWhite, "hidden"));
	Eval("ctxB:Show()");
	EXPECT((b->fFlags & vVisible) != 0);
	Refresh();
	EXPECT(MapIs(ExpA, "shown"));
	EXPECT(NOTNIL(Eval("Visible(ctxB)")));

	// the formats: a fresh view each (a window's clipper regions are made
	// from its outer bounds when it is built - a format changed after
	// does not grow them, as in the ROM)
	Eval("ctxB:Close()");
	// the frame: pen 2, black, no fill - outside the bounds
	b = ViewOf("ctxB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewFormat: 0x50 + (2 << 8)})");
	Eval("ctxB:Dirty()");
	Refresh();
	EXPECT(MapIs(ExpFramed, "framed"));
	Eval("ctxB:Close()");
	// an inset frame: pen 1, two pixels further out
	b = ViewOf("ctxB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewFormat: 0x50 + (1 << 8) + (2 << 16)})");
	Eval("ctxB:Dirty()");
	Rect outer;
	b->OuterBounds(&outer);
	EXPECT(outer.left == 7 && outer.top == 7 && outer.right == 63 && outer.bottom == 43);
	Refresh();
	EXPECT(MapIs(ExpInset, "inset"));
	Eval("ctxB:Close()");
	// a drop shadow of 2 with a pen 1 frame
	b = ViewOf("ctxB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewFormat: 0x50 + (1 << 8) + (2 << 18)})");
	Eval("ctxB:Dirty()");
	b->OuterBounds(&outer);
	EXPECT(outer.left == 9 && outer.top == 9 && outer.right == 63 && outer.bottom == 43);
	Refresh();
	EXPECT(MapIs(ExpShadow, "shadow"));
	Eval("ctxB:Close()");
	// rounded corners: the fill's corners are white
	b = ViewOf("ctxB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewFormat: 5 + (4 << 24)})");
	Eval("ctxB:Dirty()");
	Refresh();
	EXPECT(Pixel(10, 10) == 0 && Pixel(59, 39) == 0 && Pixel(35, 10) == 1 && Pixel(10, 25) == 1 && Pixel(14, 14) == 1);
	Eval("ctxB:Close()");
	// lines, viewLineSpacing apart
	b = ViewOf("ctxB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewFormat: 5 << 12, viewLineSpacing: 10})");
	Eval("ctxB:Dirty()");
	Refresh();
	EXPECT(MapIs(ExpLines, "lines"));
	Eval("ctxB:Close()");
	b = ViewOf("ctxB := AddView(GetRoot(), templB)");
	Eval("ctxB:Dirty()");
	Refresh();
	EXPECT(MapIs(ExpA, "filled again"));

	// a second view in front, white with a black frame, overlapping
	Eval("templC := {viewClass: 74, viewFlags: 1, viewBounds: {left: 30, top: 20, right: 80, bottom: 60}, viewFormat: 1 + 0x50 + (1 << 8)}");
	TView* c = ViewOf("ctxC := AddView(GetRoot(), templC)");
	EXPECT(c != nil && gRootView->fChildren->At(1) == c);
	Eval("ctxC:Dirty()");
	Refresh();
	EXPECT(Pixel(40, 30) == 0 && Pixel(29, 19) == 1 && Pixel(80, 60) == 1 && Pixel(20, 15) == 1 && Pixel(70, 55) == 0);
	// the one behind is obscured: its clipper says so
	EXPECT(b->Clipper()->fIsObscured && !c->Clipper()->fIsObscured);
	// MoveBehind puts b in front of c: c's obscured now
	Eval("ctxB:MoveBehind(nil)");
	EXPECT(gRootView->fChildren->At(1) == b);
	Refresh();
	EXPECT(Pixel(40, 30) == 1 && Pixel(70, 55) == 0 && Pixel(80, 60) == 1);
	EXPECT(!b->Clipper()->fIsObscured && c->Clipper()->fIsObscured);
	Eval("ctxB:MoveBehind(ctxC)");
	EXPECT(gRootView->fChildren->At(0) == b);
	Refresh();
	EXPECT(Pixel(40, 30) == 0);
	// dirtying the view behind repaints only what shows of it
	Eval("ctxB:Dirty()");
	Refresh();
	EXPECT(Pixel(40, 30) == 0 && Pixel(20, 15) == 1);
	Eval("ctxC:Close()");
	Refresh();
	EXPECT(MapIs(ExpA, "c closed"));

	// a child drawn over its parent, clipped to it (vClipping)
	Eval("templB.viewFlags := 1 + 32");
	Eval("templB.viewChildren := [{viewClass: 74, viewFlags: 1, viewBounds: {left: 30, top: 20, right: 70, bottom: 50}, viewFormat: 1}]");
	Eval("ctxB:RedoChildren()");
	Eval("ctxB:SyncChildren()");
	EXPECT(b->fChildren->Count() == 1 && BoundsAre(b->fChildren->At(0), 40, 30, 80, 60));
	Eval("ctxB:Dirty()");
	Refresh();
	EXPECT(MapIs(ExpChild, "clipped child"));
	Eval("templB.viewChildren := nil");
	Eval("ctxB:RedoChildren()");
	Eval("ctxB:SyncChildren()");
	EXPECT(b->fChildren == TView::gEmptyViewList);
	Eval("ctxB:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "all closed"));
}


static void
TestJustify()
{
	// a parent of 100 x 60 at (20, 10), children justified against it
	Eval("templP := {viewClass: 74, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 120, bottom: 70}, viewFormat: 0}");
	TView* p = ViewOf("ctxP := AddView(GetRoot(), templP)");
	EXPECT(BoundsAre(p, 20, 10, 120, 70));
	struct { long justify; long left, top, right, bottom; long gl, gt, gr, gb; } kCases[] = {
		{ 0, 5, 5, 25, 15, 25, 15, 45, 25 },														// parent left, top
		{ vjParentRightH, -30, 5, -10, 15, 90, 15, 110, 25 },										// from the right edge
		{ vjParentCenterH, 0, 5, 20, 15, 60, 15, 80, 25 },											// centred (the bounds offset from the centred place)
		{ vjParentFullH, 5, 5, -5, 15, 25, 15, 115, 25 },											// both edges
		{ vjParentBottomV, 5, -20, 25, -10, 25, 50, 45, 60 },
		{ vjParentCenterV, 5, 0, 25, 10, 25, 35, 45, 45 },
		{ vjParentFullV, 5, 5, 25, -5, 25, 15, 45, 65 },
		{ vjParentCenterH | vjParentCenterV, 0, 0, 20, 10, 60, 35, 80, 45 },
		{ vjLeftRatio | vjRightRatio, 10, 5, 50, 15, 30, 15, 70, 25 },								// percentages of the width
		{ vjTopRatio | vjBottomRatio, 5, 50, 25, 100, 25, 40, 45, 70 },
		{ vjParentClip, 5, 5, 25, 15, 25, 15, 45, 25 },
	};
	for (unsigned i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
	{
		char source[256];
		snprintf(source, sizeof(source), "ctxJ := AddView(ctxP, {viewClass: 74, viewFlags: 1, viewJustify: %ld, viewBounds: {left: %ld, top: %ld, right: %ld, bottom: %ld}})",
			kCases[i].justify, kCases[i].left, kCases[i].top, kCases[i].right, kCases[i].bottom);
		TView* j = ViewOf(source);
		EXPECT(j != nil);
		if (j == nil)
			continue;
		if (!BoundsAre(j, kCases[i].gl, kCases[i].gt, kCases[i].gr, kCases[i].gb))
			fprintf(stderr, "  (case %u, viewJustify 0x%lx)\n", i, kCases[i].justify);
		// the round trip through DejustifyBounds
		Rect relative;
		j->DejustifyBounds(&relative);
		EXPECT(relative.left == kCases[i].left && relative.top == kCases[i].top && relative.right == kCases[i].right && relative.bottom == kCases[i].bottom);
		if (!(relative.left == kCases[i].left && relative.top == kCases[i].top && relative.right == kCases[i].right && relative.bottom == kCases[i].bottom))
			fprintf(stderr, "  dejustified [%d,%d,%d,%d] (case %u)\n", relative.left, relative.top, relative.right, relative.bottom, i);
		Eval("RemoveView(ctxP, ctxJ)");
	}

	// siblings: the second placed against the first
	Eval("ctxS1 := AddView(ctxP, {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 40, bottom: 30}})");
	TView* s1 = ViewOf("ctxS1");
	EXPECT(BoundsAre(s1, 30, 20, 60, 40));
	TView* s2 = ViewOf("ctxS2 := AddView(ctxP, {viewClass: 74, viewFlags: 1, viewJustify: vjSiblingRightH, viewBounds: {left: 5, top: 0, right: 25, bottom: 10}})");
	EXPECT(BoundsAre(s2, 65, 10, 85, 20));		// left from the sibling's right; top from the parent's top
	Rect relative;
	s2->DejustifyBounds(&relative);
	EXPECT(relative.left == 5 && relative.top == 0 && relative.right == 25 && relative.bottom == 10);
	Eval("RemoveView(ctxP, ctxS2)");
	s2 = ViewOf("ctxS2 := AddView(ctxP, {viewClass: 74, viewFlags: 1, viewJustify: vjSiblingBottomV + vjSiblingCenterH, viewBounds: {left: 0, top: 2, right: 20, bottom: 12}})");
	EXPECT(BoundsAre(s2, 35, 42, 55, 52));		// centred under the sibling, 2 below it
	s2->DejustifyBounds(&relative);
	EXPECT(relative.left == 0 && relative.top == 2 && relative.right == 20 && relative.bottom == 12);
	Eval("RemoveView(ctxP, ctxS2)");
	s2 = ViewOf("ctxS2 := AddView(ctxP, {viewClass: 74, viewFlags: 1, viewJustify: vjSiblingTopV + vjSiblingLeftH + vjTopRatio + vjBottomRatio, viewBounds: {left: 0, top: 50, right: 10, bottom: 100}})");
	EXPECT(BoundsAre(s2, 30, 30, 40, 40));		// percentages of the sibling's height, from its top
	s2->DejustifyBounds(&relative);
	EXPECT(relative.top == 50 && relative.bottom == 100);
	Eval("RemoveView(ctxP, ctxS2)");
	Eval("RemoveView(ctxP, ctxS1)");

	// a scrolled parent: the children follow the origin unless vjParentClip
	TView* k = ViewOf("ctxK := AddView(ctxP, {viewClass: 74, viewFlags: 1, viewBounds: {left: 5, top: 5, right: 25, bottom: 15}})");
	TView* kc = ViewOf("ctxKC := AddView(ctxP, {viewClass: 74, viewFlags: 1, viewJustify: vjParentClip, viewBounds: {left: 5, top: 5, right: 25, bottom: 15}})");
	Eval("ctxP:SetOrigin(10, 20)");
	EXPECT(BoundsAre(k, 15, -5, 35, 5) && BoundsAre(kc, 25, 15, 45, 25));
	Eval("ctxP:SyncChildren()");
	EXPECT(BoundsAre(k, 15, -5, 35, 5) && BoundsAre(kc, 25, 15, 45, 25));
	Point origin = p->ContentsOrigin();
	EXPECT(origin.h == 10 && origin.v == -10);
	Eval("ctxP:SetOrigin(0, 0)");
	EXPECT(BoundsAre(k, 25, 15, 45, 25));
	// a view moved: the template's viewBounds written
	k->Move(MakePoint(3, 4));
	EXPECT(BoundsAre(k, 28, 19, 48, 29) && RINT(Eval("ctxKC:Parent():ChildViewFrames()[0].viewBounds.left")) == 8);
	// a lassoing parent sized to its children
	TView* l = ViewOf("ctxL := AddView(ctxP, {viewClass: 74, viewFlags: 1, viewJustify: vjChildrenLasso, viewBounds: {left: 0, top: 0, right: 0, bottom: 0}, viewChildren: [{viewClass: 74, viewFlags: 1, viewBounds: {left: 3, top: 4, right: 13, bottom: 14}}, {viewClass: 74, viewFlags: 1, viewBounds: {left: 20, top: 8, right: 30, bottom: 28}}]})");
	EXPECT(BoundsAre(l, 20, 10, 47, 34));		// the children's union (3,4)-(30,28) at the view's own origin
	EXPECT(RINT(Eval("ctxL.viewBounds.right")) == 27 && RINT(Eval("ctxL.viewBounds.bottom")) == 24);
	// :RedoChildren(): every child thrown away and the whole lot built
	// again out of viewChildren as it now stands, the view left dirty.  It
	// is how a view whose children come out of something that has changed
	// (a list of countries scrolled to another letter) is made to look
	// again - RemoveAllViews alone would leave it empty.
	Eval("ctxR := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 60, bottom: 60},"
		" viewChildren: [{viewClass: 74, viewFlags: 1, viewBounds: {left: 1, top: 1, right: 11, bottom: 11}, debug: 'r1}]})");
	EXPECT(RINT(Eval("Length(ctxR:ChildViewFrames())")) == 1);
	Eval("ctxR.viewChildren := [{viewClass: 74, viewFlags: 1, viewBounds: {left: 1, top: 1, right: 11, bottom: 11}, debug: 'r2},"
		" {viewClass: 74, viewFlags: 1, viewBounds: {left: 20, top: 20, right: 30, bottom: 30}, debug: 'r3}]");
	EXPECT(NOTNIL(Eval("ctxR:RedoChildren()")));
	EXPECT(RINT(Eval("Length(ctxR:ChildViewFrames())")) == 2);
	EXPECT(EQRef(Eval("ctxR:ChildViewFrames()[0].debug"), Eval("'r2")));
	Eval("ctxR:Close()");
	Eval("ctxP:Close()");
	Refresh();
}


static void
TestScripts()
{
	Eval("log := []");
	Eval("templS := {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewFormat: 0,"
		"  viewSetupFormScript: func() begin AddArraySlot(log, 'setupForm); self.viewBounds := {left: 15, top: 15, right: 65, bottom: 45} end,"
		"  viewSetupDoneScript: func() AddArraySlot(log, 'setupDone),"
		"  viewShowScript: func() AddArraySlot(log, 'show),"
		"  viewHideScript: func() AddArraySlot(log, 'hide),"
		"  viewQuitScript: func() begin AddArraySlot(log, 'quit); 'postQuit end,"
		"  viewPostQuitScript: func() AddArraySlot(log, 'postQuit),"
		"  viewChangedScript: func(slot, view) AddArraySlot(log, slot),"
		"  viewDrawScript: func() AddArraySlot(log, 'draw),"
		"  viewSetupChildrenScript: func() AddArraySlot(log, 'setupChildren),"
		"  viewIdleScript: func() begin AddArraySlot(log, 'idle); 250 end }");
	TView* s = ViewOf("ctxS := AddView(GetRoot(), templS)");
	// the setup form script's bounds are the ones used
	EXPECT(BoundsAre(s, 15, 15, 65, 45));
	EXPECT(NOTNIL(Eval("log = ['setupForm, 'setupChildren, 'setupDone] or log[0] = 'setupForm")));
	EXPECT(RINT(Eval("Length(log)")) == 3 && EQRef(Eval("log[2]"), Eval("'setupDone")));
	Eval("log := []");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(RINT(Eval("Length(log)")) == 1 && EQRef(Eval("log[0]"), Eval("'draw")));
	Eval("log := []");
	Eval("SetValue(ctxS, 'viewFormat, 5)");		// syncs: the setup form script runs again, then the changed script
	EXPECT(RINT(Eval("Length(log)")) == 2 && EQRef(Eval("log[0]"), Eval("'setupForm")) && EQRef(Eval("log[1]"), Eval("'viewFormat")));
	EXPECT(s->Idle(0) == 250);
	Eval("log := []");
	Eval("ctxS:Hide()");
	Eval("ctxS:Show()");
	EXPECT(EQRef(Eval("log[0]"), Eval("'hide")) && EQRef(Eval("log[1]"), Eval("'show")));
	// vNoScripts silences them
	Eval("log := []");
	s->SetFlags(vNoScripts);
	Refresh();
	EXPECT(RINT(Eval("Length(log)")) == 0);
	s->ClearFlags(vNoScripts);
	// a tied view is told of changes
	Eval("tied := {Changed: func(view, slot) AddArraySlot(log, slot)}");
	Eval("ctxS.viewTie := [tied, 'Changed]");
	Eval("log := []");
	Eval("SetValue(ctxS, 'viewFormat, 0)");
	EXPECT(RINT(Eval("Length(log)")) == 3 && EQRef(Eval("log[1]"), Eval("'viewFormat")) && EQRef(Eval("log[2]"), Eval("'viewFormat")));
	// the quit scripts on Close
	Eval("log := []");
	Eval("ctxS:Close()");
	EXPECT(NOTNIL(Eval("log[0] = 'hide and log[1] = 'quit and log[2] = 'postQuit")));
	Refresh();
	EXPECT(MapIs(ExpWhite, "closed"));

	// a setup form script that closes the view: the view is not made
	Eval("templT := {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, viewSetupFormScript: func() :Close()}");
	volatile Boolean threw = false;
	newton_try
	{
		Eval("ctxT := AddView(GetRoot(), templT)");
	}
	newton_catch(exRootException)
	{
		threw = (long) (Long) _info.exception.data == kViewErrCouldNotCreate;
	}
	end_try;
	EXPECT(threw && gRootView->fChildren == TView::gEmptyViewList);
	// a template without bounds
	threw = false;
	newton_try
	{
		Eval("AddView(GetRoot(), {viewClass: 74, viewFlags: 1})");
	}
	newton_catch(exRootException)
	{
		threw = (long) (Long) _info.exception.data == kViewErrNoViewBounds;
		if (!threw) fprintf(stderr, "  threw %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	EXPECT(threw && gRootView->fChildren->Count() == 0);		// (the list stays, emptied: the ROM's Constructor removes the view with CList::Remove)
	// a template parented to the root resolves (GetView walks the _parent
	// chain) to the root view itself, as in the ROM: the ROM's built-in
	// applications name a preallocatedContext instead - a context made
	// with BuildContext has its own viewCObject slot (nil) and opens
	Eval("templU := {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 60, bottom: 40}, _parent: GetRoot()}");
	EXPECT(GetView(RefVar(Eval("templU"))) == gRootView);
	Eval("ctxU := BuildContext(templU)");
	EXPECT(GetView(RefVar(Eval("ctxU"))) == nil && ISNIL(Eval("ctxU.viewCObject")));
	Eval("ctxU:Open()");
	// aeAddChild goes to AddChild, which builds the view on the context it
	// was given rather than on a context of its own, so ctxU is the view's
	// context and takes the viewCObject slot
	EXPECT(gRootView->fChildren->Count() == 1 && NOTNIL(Eval("ctxU.viewCObject")));
	TView* u = gRootView->fChildren->At(0);
	EXPECT(GetView(RefVar(Eval("ctxU"))) == u && (u->fFlags & vVisible));
	Eval("ctxU:Open()");		// open already: nothing
	EXPECT(gRootView->fChildren->Count() == 1);
	// Toggle on an open view's context closes it
	Eval("GetRoot():ChildViewFrames()[0]:Toggle()");
	EXPECT(gRootView->fChildren == TView::gEmptyViewList);
	// a hidden view's context: Open shows it again
	Eval("ctxU:Open()");
	Eval("GetRoot():ChildViewFrames()[0]:Hide()");
	EXPECT((gRootView->fChildren->At(0)->fFlags & vVisible) == 0);
	Eval("GetRoot():ChildViewFrames()[0]:Open()");
	EXPECT((gRootView->fChildren->At(0)->fFlags & vVisible) != 0);
	Eval("GetRoot():ChildViewFrames()[0]:Close()");
	EXPECT(gRootView->fChildren == TView::gEmptyViewList);
	Refresh();
}


// the extent of the set pixels in a row band of the map
static void
InkExtent(long top, long bottom, long* left, long* right)
{
	*left = kWidth;
	*right = 0;
	for (long y = top; y < bottom; y++)
		for (long x = 0; x < kWidth; x++)
			if (Pixel(x, y))
			{
				if (x < *left) *left = x;
				if (x + 1 > *right) *right = x + 1;
			}
}


static void
TestTextView()
{
	// a title: one line, centred (protoTitle's viewJustify: vjCenterH + vjCenterV + oneLineOnly)
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "espy12")), RefVar(MAKEINT(PackFont(0, 12, 0))));
	TView* t = ViewOf("ctxT := AddView(GetRoot(), {viewClass: 98, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 120, bottom: 30}, viewJustify: 0x800006, viewFont: espy12, text: \"Hello\"})");
	EXPECT(t != nil && t->ClassID() == clTextView && t->DerivedFrom(clView) && ((TTextView*) t)->fTransferMode == srcOr);
	Eval("ctxT:Dirty()");
	Refresh();
	long inkLeft, inkRight;
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkLeft == 57 && inkRight == 83);		// "Hello" advances 27 in espy 12 (26 of ink): centred at 20 + 36.5, the half rounding up
	InkExtent(0, 10, &inkLeft, &inkRight);
	EXPECT(inkRight == 0);
	// the baseline: ascent 12, descent 4 in a 20 high view - centred one lower than the room leaves
	long inkTop = kHeight;
	for (long y = 10; y < 30; y++)
		for (long x = 20; x < 120; x++)
			if (Pixel(x, y) && y < inkTop)
				inkTop = y;
	EXPECT(inkTop == 15);		// the baseline at top + ascent - 1 + (20 - 16) / 2 + 1 = 24, the H 9 high above it
	// a button: text flush right, at the top with a line spacing
	Eval("SetValue(ctxT, 'viewJustify, 0x800001)");
	Eval("ctxT.viewLineSpacing := 14");
	Eval("ctxT:Dirty()");
	Refresh();
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkRight == 119 && inkLeft == 93);		// flush right: the advance's last pixel is blank
	// wrapped: two lines in a narrow view
	Eval("ctxT:Close()");
	t = ViewOf("ctxT := AddView(GetRoot(), {viewClass: 98, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 60, bottom: 50}, viewJustify: 0, viewFont: espy12, text: \"Hello World\"})");
	Eval("ctxT:Dirty()");
	Refresh();
	InkExtent(10, 26, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight == 46);
	InkExtent(26, 42, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight > 20);
	Eval("ctxT:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "text closed"));
}


// a bitmap frame: an 8 x 4 one-bit picture with the given rows (a 'bits
// binary is a persistent format: its halfwords big-endian)
static Ref
MakeBitmap(const unsigned char* rows, long width, long height)
{
	long rowBytes = ((width + 31) / 32) * 4;
	RefVar bits(AllocateBinary(RefVar(Intern((char*) "bits")), kFramBitmapHeaderSize + rowBytes * height));
	unsigned char* data = (unsigned char*) BinaryData(bits);
	memset(data, 0, kFramBitmapHeaderSize + rowBytes * height);
	PutBigEndianHalf(data + 4, (unsigned short) rowBytes);
	PutBigEndianHalf(data + 8, 0);						// top
	PutBigEndianHalf(data + 10, 0);						// left
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
TestPictureView()
{
	// a picture view: the icon centred in the bounds
	static const unsigned char kRows[4] = { 0xf0, 0x90, 0x90, 0xf0 };		// a hollow 4 x 4 box in an 8 wide row
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "pict")), RefVar(MakeBitmap(kRows, 8, 4)));
	TView* p = ViewOf("ctxP := AddView(GetRoot(), {viewClass: 76, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 60, bottom: 30}, icon: pict})");
	EXPECT(p != nil && p->ClassID() == clPictureView && p->DerivedFrom(clView));
	Eval("ctxP:Dirty()");
	Refresh();
	// centred: the 8 x 4 picture at (36, 18)
	EXPECT(Pixel(36, 18) == 1 && Pixel(39, 18) == 1 && Pixel(36, 21) == 1 && Pixel(37, 19) == 0 && Pixel(40, 18) == 0 && Pixel(35, 18) == 0);
	EXPECT(Pixel(36, 17) == 0 && Pixel(36, 22) == 0);
	// at the top left with a viewJustify of 0
	Eval("ctxP:Close()");
	p = ViewOf("ctxP := AddView(GetRoot(), {viewClass: 76, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 60, bottom: 30}, viewJustify: 0, icon: pict})");
	Eval("ctxP:Dirty()");
	Refresh();
	EXPECT(Pixel(20, 10) == 1 && Pixel(23, 13) == 1 && Pixel(36, 18) == 0);
	// at the bottom right
	Eval("ctxP:Close()");
	p = ViewOf("ctxP := AddView(GetRoot(), {viewClass: 76, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 60, bottom: 30}, viewJustify: 9, icon: pict})");
	Eval("ctxP:Dirty()");
	Refresh();
	EXPECT(Pixel(52, 26) == 1 && Pixel(55, 29) == 1 && Pixel(20, 10) == 0);
	// the picture drawn straight: DrawPicture with the box the picture's size
	Eval("ctxP:Close()");
	Refresh();
	Rect box;
	SetRect(&box, 100, 50, 108, 54);
	DrawPicture(RefVar(Eval("pict")), box, 0, srcCopy);
	EXPECT(Pixel(100, 50) == 1 && Pixel(107, 50) == 0 && Pixel(103, 53) == 1);
	// the bounds of the picture; a box of no size takes them
	// its bounds as a shape's (a bitmap frame with the class)
	Eval("pict.class := 'bitmap");
	ShapeBounds(RefVar(Eval("pict")), &box);
	EXPECT(box.right == 8 && box.bottom == 4);
	SetRect(&box, 3, 3, 3, 3);
	Justify(&box, box, 0);
	EXPECT(box.left == 3 && box.right == 3);
	Rect pictBox;
	SetRect(&pictBox, 0, 0, 8, 4);
	SetRect(&box, 10, 10, 10, 10);
	Justify(&pictBox, box, 0);
	EXPECT(pictBox.left == 10 && pictBox.top == 10 && pictBox.right == 18 && pictBox.bottom == 14);
	Eval("GetRoot():Dirty()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "pictures gone"));
}


// A polygon view: a triangle joined up with lines and an oval drawn in
// its box, both out of 'polygonShape binaries as MakePolygonForm makes
// them (the points relative to the view's top left).
static void
TestPolygonView()
{
	Point tri[4];
	tri[0] = MakePoint(0, 0);
	tri[1] = MakePoint(30, 0);
	tri[2] = MakePoint(0, 20);
	tri[3] = MakePoint(0, 0);
	Rect box;
	SetRect(&box, 10, 60, 41, 81);
	RefVar form(MakePolygonForm(tri, 4, 5, box, 1));
	SetFrameSlot(form, RSSYMviewclass, RefVar(MAKEINT(clPolygonView)));
	SetFrameSlot(form, RSSYMviewflags, RefVar(MAKEINT(1)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "polyForm")), form);
	TView* p = ViewOf("ctxG := AddView(GetRoot(), polyForm)");
	EXPECT(p != nil && p->ClassID() == clPolygonView && p->DerivedFrom(clDataView));
	EXPECT(((TPolygonView*) p)->GetPenSize() == 1);
	Eval("ctxG:Dirty()");
	Refresh();
	// the top edge, the left edge and the diagonal, nothing inside
	EXPECT(Pixel(10, 60) == 1 && Pixel(25, 60) == 1 && Pixel(40, 60) == 1);
	EXPECT(Pixel(10, 70) == 1 && Pixel(10, 80) == 1);
	EXPECT(Pixel(25, 70) == 1 || Pixel(24, 70) == 1 || Pixel(26, 70) == 1);
	EXPECT(Pixel(15, 65) == 0 && Pixel(41, 60) == 0);
	Eval("ctxG:Close()");

	// an oval: verb 0 fills its box with a frame, the points unused
	SetRect(&box, 100, 60, 141, 81);
	form = MakePolygonForm(tri, 2, kPolyOval, box, 1);
	SetFrameSlot(form, RSSYMviewclass, RefVar(MAKEINT(clPolygonView)));
	SetFrameSlot(form, RSSYMviewflags, RefVar(MAKEINT(1)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "polyForm")), form);
	p = ViewOf("ctxG := AddView(GetRoot(), polyForm)");
	Eval("ctxG:Dirty()");
	Refresh();
	EXPECT(Pixel(100, 70) == 1 && Pixel(140, 70) == 1 && Pixel(120, 60) == 1 && Pixel(120, 80) == 1);
	EXPECT(Pixel(120, 70) == 0 && Pixel(100, 60) == 0);
	Eval("ctxG:Close()");
	Eval("GetRoot():Dirty()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "polygons gone"));

	// the arithmetic under the arc: the angle of a point about a box's
	// middle, clockwise from twelve o'clock with the aspect taken out
	SetRect(&box, 0, 0, 40, 20);
	long angle = -1;
	PtToAngle(&box, MakePoint(20, 0), &angle);
	EXPECT(angle == 0);
	PtToAngle(&box, MakePoint(40, 10), &angle);
	EXPECT(angle == 90);
	PtToAngle(&box, MakePoint(20, 20), &angle);
	EXPECT(angle == 180);
	PtToAngle(&box, MakePoint(0, 10), &angle);
	EXPECT(angle == 270);
	PtToAngle(&box, MakePoint(40, 0), &angle);		// the corner: 45 degrees, not 63
	EXPECT(angle == 45);
	long start = 0, arc = 0;
	CalcArcAngles(box, MakePoint(20, 0), MakePoint(40, 10), &start, &arc);
	EXPECT(start == 0 && arc == -270);			// the long way round, anticlockwise
}


static void
TestParagraphView()
{
	// the style runs: made to cover the text
	Eval("runs := [2, espy12]");
	CorrectAnyBadStyleRuns(RefVar(Eval("runs")), 5);
	EXPECT(RINT(Eval("runs[0]")) == 5 && TotalRunLength(RefVar(Eval("runs"))) == 5);
	Eval("runs := [3, espy12, 4, espy12]");
	CorrectAnyBadStyleRuns(RefVar(Eval("runs")), 5);
	EXPECT(RINT(Eval("runs[2]")) == 2 && RINT(Eval("Length(runs)")) == 4);
	CorrectAnyBadStyleRuns(RefVar(Eval("runs")), 3);
	EXPECT(RINT(Eval("Length(runs)")) == 2 && RINT(Eval("runs[0]")) == 3);
	RunsInsert(RefVar(Eval("runs")), 1, 4);
	EXPECT(RINT(Eval("runs[0]")) == 7);
	RunsDelete(RefVar(Eval("runs")), 0, 7);
	EXPECT(RINT(Eval("Length(runs)")) == 0);

	// a static text: three words wrapped into a narrow view, one below the other
	TParagraphView* p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 70, bottom: 70}, viewJustify: 0, viewFont: espy12, text: \"Hello World again\"})");
	EXPECT(p != nil && p->ClassID() == clParagraphView && p->DerivedFrom(clDataView) && p->DerivedFrom(clView));
	EXPECT(p->fTransferMode == srcOr && p->fTextFlags != -1 && !p->fCalculateBounds);
	// a paragraph that does not calculate its bounds lays out nothing in
	// SetupDone: its lines are made when they are first wanted (drawn)
	EXPECT(p->LineCount() == 0 && p->fLineCache == nil);
	Eval("ctxQ:Dirty()");
	Refresh();
	EXPECT(p->LineCount() == 3);
	if (p->LineCount() == 3)
	{
		EXPECT(p->Line(0).fStart == 0 && p->Line(0).fEnd == 6 && p->Line(1).fStart == 6 && p->Line(1).fEnd == 12 && p->Line(2).fStart == 12 && p->Line(2).fEnd == 17);	// a line keeps the space that ends it (LineInfo's endsWithSpace)
		EXPECT(p->Line(0).fEndsWithSpace && !p->Line(2).fEndsWithSpace);
		EXPECT((p->Line(0).fAscent + p->Line(0).fHeight) == p->fLineHeight && p->Line(1).fBounds.top == 10 + (p->Line(0).fAscent + p->Line(0).fHeight) && p->Line(0).fBounds.left == 20);
		EXPECT(p->TextBounds().top == 10 && p->TextBounds().bottom == 70);	// as tall as the view, which does not calculate its bounds (FillAllCaches)
	}
	EXPECT(EQRef(p->GetStyles(), Eval("espy12")));
	Eval("ctxQ:Dirty()");
	Refresh();
	long inkLeft, inkRight;
	long lineHeight = p->Line(0).fAscent + p->Line(0).fHeight;
	InkExtent(10, 10 + lineHeight, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight == 46);								// "Hello" as the text view drew it
	InkExtent(10 + lineHeight, 10 + 2 * lineHeight, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight > 40);								// "World"
	InkExtent(10 + 2 * lineHeight, 10 + 3 * lineHeight, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight > 40);								// "again"
	InkExtent(10 + 3 * lineHeight, 70, &inkLeft, &inkRight);
	EXPECT(inkRight == 0);
	Eval("ctxQ:Close()");

	// a paragraph too short for its text: one line and the ellipsis after it
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 100, bottom: 30}, viewJustify: 0, viewFont: espy12, text: \"Hello World again\"})");
	Eval("ctxQ:Dirty()");
	Refresh();
	EXPECT(p->LineCount() == 1 && p->Line(0).fEnd == 12);
	Eval("ctxQ:Dirty()");
	Refresh();
	InkExtent(10, 30, &inkLeft, &inkRight);
	long textRight = p->Line(0).fBounds.right;
	EXPECT(inkLeft == 20 && inkRight > textRight && inkRight <= 100);		// the ellipsis after "Hello World"
	Eval("ctxQ:Close()");

	// the same with vCalculateBounds: every line is kept, no ellipsis
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 11, viewBounds: {left: 20, top: 10, right: 100, bottom: 30}, viewJustify: 0, viewFont: espy12, text: \"Hello World again\"})");
	EXPECT(p->fCalculateBounds && p->LineCount() == 2 && p->Line(1).fEnd == 17);
	Eval("ctxQ:Close()");

	// style runs: the second word in a bigger font makes its line taller (the
	// view 55 wide: espy 18 is the 16-point strike stretched, and a width to
	// fit is measured with the stretched advances, as the ROM measures it)
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "espy18")), RefVar(MAKEINT(PackFont(0, 18, 0))));
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 75, bottom: 90}, viewJustify: 0, viewFont: espy12, text: \"Hello World again\", styles: [6, espy12, 6, espy18, 5, espy12]})");
	Eval("ctxQ:Dirty()");
	Refresh();
	EXPECT(p->LineCount() == 3);
	if (p->LineCount() == 3)
	{
		EXPECT(p->Line(1).fHeight > p->Line(0).fHeight && p->Line(2).fHeight == p->Line(0).fHeight);
		EXPECT(p->Line(1).fFirstObj == 1 && p->Line(0).fFirstObj == 0 && p->Line(2).fFirstObj == 2);
	}
	EXPECT(IsArray(p->GetStyles()));
	Eval("ctxQ:Dirty()");
	Refresh();
	InkExtent(10 + p->Line(1).fBounds.top, 10 + p->Line(1).fBounds.bottom, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight > 46);								// "World" in espy 18 is wider than "Hello" in 12
	Eval("ctxQ:Close()");

	// viewLineSpacing: the lines that far apart when the font fits it, the
	// first baseline three above the first ruled line (LineLoop)
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 70, bottom: 90}, viewJustify: 0, viewFont: espy12, viewLineSpacing: 18, text: \"Hello World\"})");
	Eval("ctxQ:Dirty()");
	Refresh();
	EXPECT(p->fLineSpacing == 18 && p->GetInterLineSpacing() == 18 && p->LineCount() == 2);
	EXPECT(p->Line(0).fBounds.bottom - p->Line(0).fHeight == 10 + 18 - 3 && p->Line(1).fBounds.bottom - p->Line(1).fHeight == 10 + 18 - 3 + 18);	// the baselines
	Eval("ctxQ:Close()");
	// ... whatever the font: an input line's text on its line (a Find slip's)
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 120, bottom: 65}, viewJustify: 0x800000, viewFont: espy12, viewLineSpacing: 45, text: \"tester\"})");
	Eval("ctxQ:Dirty()");
	Refresh();
	EXPECT(p->GetInterLineSpacing() == 45 && p->LineCount() == 1 && p->Line(0).fBounds.bottom - p->Line(0).fHeight == 10 + 45 - 4);
	Eval("ctxQ:Close()");

	// CheckStyles: an italic run is a face that leans out of its box (italic, outline or shadow: 0x1a); no ink words here
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3 + 8, viewBounds: {left: 20, top: 10, right: 120, bottom: 10}, viewJustify: 0, viewFont: espy12, text: \"one two\", styles: [4, espy12, 3, 0x203000]})");
	EXPECT(!p->CheckStyles() && p->fHasHeavyFaces && !p->fHasInkWords);
	Eval("ctxQ:Close()");
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3 + 8, viewBounds: {left: 20, top: 10, right: 120, bottom: 10}, viewJustify: 0, viewFont: espy12, text: \"one\"})");
	EXPECT(!p->CheckStyles() && !p->fHasHeavyFaces);
	Eval("ctxQ:Close()");

	// a paragraph that calculates its bounds lays its lines out in
	// SetupDone - and drops them again when it lies outside what its
	// parents show; one that does not calculate them waits to be drawn
	Eval("ctxQF := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 150, bottom: 60}})");
	p = (TParagraphView*) ViewOf("ctxQ := AddView(ctxQF, {viewClass: 81, viewFlags: 1 + 8, viewBounds: {left: 10, top: 10, right: 140, bottom: 10}, viewJustify: 0, viewFont: espy12, text: \"shown\"})");
	EXPECT(p->LineCount() == 1);
	p = (TParagraphView*) ViewOf("ctxQ := AddView(ctxQF, {viewClass: 81, viewFlags: 1 + 8, viewBounds: {left: 10, top: 80, right: 140, bottom: 80}, viewJustify: 0, viewFont: espy12, text: \"below\"})");
	EXPECT(p->fLineCache != nil && p->LineCount() == 0);
	p = (TParagraphView*) ViewOf("ctxQ := AddView(ctxQF, {viewClass: 81, viewFlags: 1, viewBounds: {left: 10, top: 20, right: 140, bottom: 40}, viewJustify: 0, viewFont: espy12, text: \"later\"})");
	EXPECT(p->fLineCache == nil && p->LineCount() == 0);
	Eval("RemoveView(GetRoot(), ctxQF)");

	// text flag 0x20 on a page: the paragraph moved so its first baseline
	// is on the page's lines, and the flag cleared in the view and its
	// textFlags slot
	{
		TEditView* page = (TEditView*) ViewOf("ctxQE := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, viewLineSpacing: 20, viewBounds: {left: 0, top: 0, right: 200, bottom: 150}})");
		p = (TParagraphView*) ViewOf("ctxQ := AddView(ctxQE, {viewClass: 81, viewFlags: 1, textFlags: 0x20, viewBounds: {left: 10, top: 7, right: 150, bottom: 27}, viewJustify: 0, viewFont: espy12, text: \"ruled\"})");
		StyleRecord record;
		CreateTextStyleRecord(RefVar(MAKEINT(PackFont(0, 12, 0))), &record);
		FontInfo info;
		GetStyleFontInfo(&record, &info);
		DisposeStyleRecord(&record);
		Rect expected;
		SetRect(&expected, 10, 7, 150, 27);
		page->AlignToLineSpacing(&expected, info.ascent + expected.top, info.ascent);
		EXPECT(p->viewBounds.top == expected.top && p->viewBounds.bottom == expected.bottom && expected.top != 7);
		EXPECT((p->fTextFlags & 0x20) == 0 && (RINT(Eval("ctxQ.textFlags")) & 0x20) == 0);
		Eval("RemoveView(GetRoot(), ctxQE)");
	}

	// a view that calculates its bounds is made as tall as its lines when it
	// is built (CreateAllCaches) - a note made by MakeTextNote has no height
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3 + 8, viewBounds: {left: 20, top: 10, right: 120, bottom: 10}, viewJustify: 0, viewFont: espy12, text: \"Hello\"})");
	EXPECT(p->LineCount() == 1 && p->viewBounds.top == 10 && p->viewBounds.bottom == p->Line(0).fBounds.bottom && p->viewBounds.bottom > 10);
	Eval("ctxQ:Close()");

	// a paragraph's vertical justification is only for one line
	// (vjOneLineOnly); another's lines start at its top
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 70, bottom: 90}, viewJustify: 9, viewFont: espy12, text: \"Hello\"})");
	Eval("ctxQ:Dirty()");
	Refresh();
	EXPECT(p->LineCount() == 1 && p->Line(0).fBounds.top == 10);
	Eval("ctxQ:Close()");

	// justified: flush right and at the bottom
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 70, bottom: 90}, viewJustify: 9 + 0x800000, viewFont: espy12, text: \"Hello\"})");
	Eval("ctxQ:Dirty()");
	Refresh();
	EXPECT(p->LineCount() == 1 && p->Line(0).fBounds.bottom == 90);
	Eval("ctxQ:Dirty()");
	Refresh();
	InkExtent(10, 90, &inkLeft, &inkRight);
	EXPECT(inkRight == 69 && inkLeft == 43);								// flush right: the advance's last pixel is blank
	InkExtent(10, 90 - (p->Line(0).fBounds.bottom - p->Line(0).fBounds.top), &inkLeft, &inkRight);
	EXPECT(inkRight == 0);
	// moved: the cached lines move along
	Eval("ctxQ:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "paragraphs closed"));
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 70, bottom: 30}, viewJustify: 0, viewFont: espy12, text: \"Hi\"})");
	Eval("ctxQ:Dirty()");
	Refresh();
	Eval("SetValue(ctxQ, 'viewBounds, {left: 60, top: 50, right: 110, bottom: 70})");
	Refresh();
	EXPECT(p->Line(0).fBounds.left == 60 && p->Line(0).fBounds.top == 50 && p->fTextBounds.left == 60);
	InkExtent(50, 70, &inkLeft, &inkRight);
	EXPECT(inkLeft == 60);
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkRight == 0);
	Eval("ctxQ:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "paragraph closed"));
}


// A paragraph whose right edge is to the left of its left edge: the width
// its lines are measured against comes out negative.  The ARM shifts that
// into the text options' fixed-point width and thinks nothing of it, and
// so must the host - one of the ROM's own slips builds a paragraph this
// way before its parent has sized it.  A line then fits no characters, so
// the line breaking puts one on each.
static void
TestNegativeWidthParagraph()
{
	TView* p = ViewOf("ctxN := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, "
					  "viewBounds: {left: 40, top: 10, right: 37, bottom: 44}, "
					  "viewFont: 0x3000, viewLineSpacing: 10, text: \"abc\"})");
	EXPECT(p != nil && p->ClassID() == clParagraphView);
	Eval("ctxN:Dirty()");
	Refresh();
	EXPECT(RINT(Eval("StrLen(ctxN.text)")) == 3);
	// and the view is still a view: it answers where its caret would go
	EXPECT(NOTNIL(Eval("ctxN:LocalBox()")));
	Eval("ctxN:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "the negative-width paragraph closed"));
}


static void
TestGaugeView()
{
	// a read-only gauge (protoGauge): the bar filled to the value
	TGaugeView* g = (TGaugeView*) ViewOf("ctxG := AddView(GetRoot(), {viewClass: 92, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 120, bottom: 20}, viewValue: 25})");
	EXPECT(g != nil && g->ClassID() == clGaugeView && g->DerivedFrom(clView) && g->fMaxValue == 100 && g->fMinValue == 0);
	Eval("ctxG:Dirty()");
	Refresh();
	// height 10 made 9 (bottom 19), inset 2: rows 12-16; the range 100, 25 of it: 20-45
	long inkLeft, inkRight;
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight == 45);
	EXPECT(Pixel(20, 12) == 1 && Pixel(44, 16) == 1 && Pixel(20, 11) == 0 && Pixel(20, 17) == 0 && Pixel(45, 14) == 0);
	// the value changed through SetValue: the view is dirtied and redrawn
	Eval("SetValue(ctxG, 'viewValue, 50)");
	Refresh();
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight == 70);
	// pinned to the limits; the limits from the slots
	Eval("SetValue(ctxG, 'viewValue, 200)");
	Refresh();
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkRight == 120);
	Eval("SetValue(ctxG, 'maxValue, 400)");
	EXPECT(g->fMaxValue == 400);
	Refresh();
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkRight == 70);
	Eval("ctxG:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "gauge closed"));

	// an editable gauge (protoSlider) with gaugeDrawLimits: the knob and the gray rest
	g = (TGaugeView*) ViewOf("ctxG := AddView(GetRoot(), {viewClass: 92, viewFlags: 1, viewBounds: {left: 20, top: 30, right: 120, bottom: 40}, viewValue: 0, gaugeDrawLimits: true})");
	Eval("ctxG:Dirty()");
	Refresh();
	// the bar 9 high (rows 30-38), inset 2: rows 32-36; the knob 9 wide keeps 9 out of the range, so the
	// filled part ends at 20 + 4 = 24 and the knob's box is 20..29 x 30..39: a hollow diamond centred at (24, 34)
	EXPECT(Pixel(24, 30) == 1 && Pixel(24, 38) == 1 && Pixel(28, 34) == 1 && Pixel(20, 34) == 1 && Pixel(24, 34) == 0 && Pixel(24, 29) == 0 && Pixel(29, 34) == 0 && Pixel(24, 39) == 0);
	// the rest of the bar light gray (one pixel in four): rows 33-35 from the filled part's end to the right
	long grayCount = 0;
	for (long x = 40; x < 120; x++)
		for (long y = 32; y < 37; y++)
			grayCount += Pixel(x, y);
	EXPECT(grayCount == 60 && Pixel(119, 32) == 0 && Pixel(119, 36) == 0);
	Eval("ctxG:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "slider closed"));
}


// the ink in a box of the map
// the map printed as text (for looking at a failure)
static void
DumpMap(const char* what)
{
	fprintf(stderr, "--- %s\n", what);
	for (long y = 0; y < kHeight; y += 2)
	{
		for (long x = 0; x < kWidth; x++)
			fputc(Pixel(x, y) ? '#' : (Pixel(x, y + 1) ? '+' : '.'), stderr);
		fputc('\n', stderr);
	}
}


static long
InkIn(long left, long top, long right, long bottom)
{
	long count = 0;
	for (long y = top; y < bottom; y++)
		for (long x = left; x < right; x++)
			count += Pixel(x, y);
	return count;
}


static void
TestShapes()
{
	// MakeShape of whatever it is given
	EXPECT(EQRef(ClassOf(Eval("MakeShape({left: 1, top: 2, right: 3, bottom: 4})")), RSSYMrectangle)
		&& RINT(Eval("ShapeBounds(MakeShape({left: 1, top: 2, right: 3, bottom: 4})).right")) == 3);
	EXPECT(EQRef(ClassOf(Eval("MakeShape(MakeOval(0, 0, 8, 8))")), RSSYMoval));
	EXPECT(IsArray(RefVar(Eval("MakeShape([MakeRect(0, 0, 5, 5)])"))));
	EXPECT(EQRef(ClassOf(Eval("MakeShape({bounds: {left: 0, top: 0, right: 8, bottom: 8}, bits: \"x\"})")), RSSYMbitmap)
		&& RINT(Eval("ShapeBounds(MakeShape({bounds: {left: 0, top: 0, right: 8, bottom: 8}, bits: \"x\"})).bottom")) == 8);
	// the recogniser's 'polygonShape binary: a verb, a count and the points
	{
		RefVar poly(AllocateBinary(RSSYMpolygonshape, 4 + 3 * sizeof(Point)));
		short* w = (short*) BinaryData(poly);
		w[0] = 1;						// a polygon
		w[1] = 3;
		Point* pts = (Point*) (w + 2);
		pts[0] = MakePoint(10, 20);
		pts[1] = MakePoint(30, 5);
		pts[2] = MakePoint(0, 40);
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "poly")), poly);
		Eval("Print(ShapeBounds(MakeShape(poly)))");
		// the points' bounds, which ShapeBounds gives a pixel more of
		EXPECT(EQRef(ClassOf(Eval("MakeShape(poly)")), RSSYMpolygon)
			&& RINT(Eval("ShapeBounds(MakeShape(poly)).left")) == 0
			&& RINT(Eval("ShapeBounds(MakeShape(poly)).right")) == 31
			&& RINT(Eval("ShapeBounds(MakeShape(poly)).top")) == 5
			&& RINT(Eval("ShapeBounds(MakeShape(poly)).bottom")) == 41);
		// verb 10 or 11 is a rectangle of the same points, verb 0 an oval
		w = (short*) BinaryData(RefVar(Eval("poly")));
		w[0] = 10;
		EXPECT(EQRef(ClassOf(Eval("MakeShape(poly)")), RSSYMrectangle)
			&& RINT(Eval("ShapeBounds(MakeShape(poly)).right")) == 30
			&& RINT(Eval("ShapeBounds(MakeShape(poly)).bottom")) == 40);
		w = (short*) BinaryData(RefVar(Eval("poly")));
		w[0] = 0;
		EXPECT(EQRef(ClassOf(Eval("MakeShape(poly)")), RSSYMoval));
	}
	// LayoutColumn: as many entries as fit down the view, the one that
	// crosses the bottom edge included
	Eval("ctxL := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 50, bottom: 30}})");
	Eval("entries := [{height: 12}, {height: 12}, {height: 12}, {height: 12}]");
	EXPECT(RINT(Eval("Length(ctxL:LayoutColumn(entries, 0))")) == 3);
	EXPECT(RINT(Eval("Length(ctxL:LayoutColumn(entries, 2))")) == 2);
	// a collapsed entry, or a view that says allCollapsed, takes the view's
	// collapsedHeight instead of the entry's own height
	Eval("ctxL.collapsedHeight := 5");
	Eval("entries := [{height: 12, collapsed: true}, {height: 12, collapsed: true}, {height: 12}, {height: 12}]");
	EXPECT(RINT(Eval("Length(ctxL:LayoutColumn(entries, 0))")) == 4);
	Eval("ctxL.allCollapsed := true");
	EXPECT(RINT(Eval("Length(ctxL:LayoutColumn(entries, 0))")) == 4);
	Eval("ctxL:Close()");
	// the shape objects
	EXPECT(EQRef(ClassOf(Eval("MakeRect(10, 10, 30, 20)")), RSSYMrectangle) && RINT(Eval("Length(MakeRect(10, 10, 30, 20))")) == 8);
	EXPECT(RINT(Eval("ShapeBounds(MakeRect(10, 10, 30, 20)).right")) == 30 && RINT(Eval("ShapeBounds(MakeRect(10, 10, 30, 20)).top")) == 10);
	EXPECT(RINT(Eval("ShapeBounds(MakeLine(30, 20, 10, 12)).left")) == 10 && RINT(Eval("ShapeBounds(MakeLine(30, 20, 10, 12)).bottom")) == 20);
	EXPECT(RINT(Eval("ShapeBounds(MakeLine(5, 7, 20, 7)).bottom")) == 8);
	EXPECT(EQRef(ClassOf(Eval("MakeOval(0, 0, 8, 8)")), RSSYMoval) && RINT(Eval("Length(MakeRoundRect(0, 0, 8, 8, 4))")) == 12);
	EXPECT(EQRef(ClassOf(Eval("MakePolygon([0, 0, 10, 0, 5, 8])")), RSSYMpolygon) && RINT(Eval("ShapeBounds(MakePolygon([0, 0, 10, 0, 5, 8])).right")) == 11);
	EXPECT(EQRef(ClassOf(Eval("MakeText(\"Hi\", 0, 0, 40, 20)")), RSSYMtext) && EQRef(ClassOf(Eval("MakeText(\"Hi\", 0, 0, 40, 20).data")), RSSYMtextdata));
	EXPECT(EQRef(ClassOf(Eval("MakeTextBox(\"Hi there\", 0, 0, 40, 40).data")), Intern((char*) "TextBox")));
	EXPECT(NOTNIL(Eval("IsPrimShape(MakeRect(0, 0, 1, 1))")) && ISNIL(Eval("IsPrimShape([MakeRect(0, 0, 1, 1)])")) && ISNIL(Eval("IsPrimShape({})")));
	EXPECT(RINT(Eval("ShapeBounds([MakeRect(10, 10, 30, 20), {fillPattern: 5}, MakeOval(20, 15, 50, 40)]).right")) == 50);
	EXPECT(RINT(Eval("ShapeBounds(OffsetShape(MakeRect(10, 10, 30, 20), 5, -3)).left")) == 15 && RINT(Eval("ShapeBounds(OffsetShape([MakeRect(10, 10, 30, 20)], 5, -3)[0]).top")) == 7);
	EXPECT(RINT(Eval("ShapeBounds(OffsetShape(MakePolygon([0, 0, 10, 0, 5, 8]), 4, 4)).left")) == 4);
	EXPECT(EQRef(ClassOf(Eval("MakeRegion(MakeRect(2, 2, 6, 6))")), RSSYMregion) && RINT(Eval("ShapeBounds(MakeRegion(MakeRect(2, 2, 6, 6))).right")) == 6);
	// FindShape (views/ShapeVerbs.cpp): a hollow rectangle is found near its
	// outline (a single shape answers a frame whose path is true) and not in
	// its middle; in a list the path names the member; with a selection
	// distance a point in a corner of the box names the corner
	EXPECT(ISNIL(Eval("FindShape(MakeRect(10, 10, 60, 60), 35, 35, nil)")));
	EXPECT(Eval("FindShape(MakeRect(10, 10, 60, 60), 11, 35, nil).path") == TRUEREF);
	EXPECT(ISNIL(Eval("FindShape(MakeRect(10, 10, 60, 60), 11, 35, nil).vertex")));
	EXPECT(ISNIL(Eval("FindShape(MakeRect(10, 10, 60, 60), 100, 35, nil)")));
	EXPECT(RINT(Eval("FindShape([MakeRect(0, 0, 10, 10), MakeRect(100, 100, 120, 120)], 101, 110, nil).path[0]")) == 1);
	EXPECT(RINT(Eval("Length(FindShape([MakeRect(0, 0, 10, 10), MakeRect(100, 100, 120, 120)], 101, 110, nil).path)")) == 1);
	EXPECT(RINT(Eval("FindShape(MakeRect(10, 10, 60, 60), 8, 8, {selection: 3}).vertex")) == 0);
	EXPECT(RINT(Eval("FindShape(MakeRect(10, 10, 60, 60), 61, 61, {selection: 3}).vertex")) == 2);
	EXPECT(NOTNIL(Eval("FindShape(MakeLine(0, 0, 50, 50), 26, 24, nil)")) && ISNIL(Eval("FindShape(MakeLine(0, 0, 50, 50), 40, 10, nil)")));
	EXPECT(NOTNIL(Eval("FindShape(MakeOval(0, 0, 40, 40), 20, 1, nil)")) && ISNIL(Eval("FindShape(MakeOval(0, 0, 40, 40), 20, 20, nil)")));
	EXPECT(NOTNIL(Eval("FindShape(MakePolygon([0, 0, 40, 0, 20, 30]), 20, 1, nil)")) && ISNIL(Eval("FindShape(MakePolygon([0, 0, 40, 0, 20, 30]), 20, 12, nil)")));
	// GetShapeInfo: the bounds, a line's ends, a text's string, a wedge's quarter
	EXPECT(RINT(Eval("GetShapeInfo(MakeRect(10, 10, 30, 20)).bounds.right")) == 30);
	EXPECT(RINT(Eval("GetShapeInfo(MakeLine(1, 2, 3, 4)).start.x")) == 1 && RINT(Eval("GetShapeInfo(MakeLine(1, 2, 3, 4)).stop.y")) == 4);
	EXPECT(NOTNIL(Eval("StrEqual(GetShapeInfo(MakeText(\"Hi\", 0, 0, 40, 20)).text, \"Hi\")")));
	// MakeTextLines: each line a text shape as tall as the font's ascent - its
	// bottom the baseline - one line height below the last (a Names card's
	// name line only fits its view so)
	EXPECT(RINT(Eval("Length(MakeTextLines(\"Ann Tester\", {left: 0, top: 0, right: 240, bottom: 24}, FontHeight(espy12), espy12))")) == 1);
	EXPECT(RINT(Eval("ShapeBounds(MakeTextLines(\"Ann Tester\", {left: 0, top: 0, right: 240, bottom: 24}, FontHeight(espy12), espy12)).bottom")) == RINT(Eval("FontAscent(espy12)")));
	EXPECT(RINT(Eval("ShapeBounds(MakeTextLines(\"one two three\", {left: 0, top: 0, right: 30, bottom: 100}, 20, espy12)[1]).top")) == 20);
	EXPECT(RINT(Eval("GetShapeInfo(MakeWedge(0, 0, 40, 40, 100, 30)).bitsBounds.left")) == 20);
	EXPECT(RINT(Eval("GetShapeInfo(MakeWedge(0, 0, 40, 40, 100, 30)).bitsBounds.top")) == 20);
	EXPECT(RINT(Eval("GetShapeInfo(MakeWedge(0, 0, 40, 40, 300, 30)).bitsBounds.right")) == 20);
	// MakeInk: the ink shape, drawn where it was made
	// (both rectangles 'boundsRect binaries, as MakeRectShape makes them)
	EXPECT(EQRef(ClassOf(Eval("MakeInk(SetClass(\"x\", 'ink), 5, 6, 25, 16).bounds")), Intern((char*) "boundsRect")));
	// AnimateSimpleStroke: a stroke of three moves one to the right from
	// (10, 50), drawn at twice the size into (0, 0, 200, 200) - a line from
	// (20, 100) to (26, 100) - and at its own size, from (10, 50) to (13, 50)
	{
		static const UByte kStroke[] = { 0, 0, 0, 0, 0, 100, 0, 100,	// drawn in 0, 0, 100, 100
										 0, 0, 0, 3, 0, 50, 0, 10,		// three moves from v 50, h 10
										 0x01, 0x01, 0x01, 0 };			// right, right, right, and the pad
		RefVar stroke(AllocateBinary(RSSYMbinary, sizeof(kStroke)));
		memmove(BinaryData(stroke), kStroke, sizeof(kStroke));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "simpleStroke")), stroke);
		EXPECT(ISNIL(Eval("AnimateSimpleStroke(simpleStroke, {left: 0, top: 0, right: 200, bottom: 200}, nil)")));
		long before = Pixel(12, 50);
		EXPECT(ISNIL(Eval("AnimateSimpleStroke(simpleStroke, {left: 0, top: 0, right: 100, bottom: 100}, nil)")));
		EXPECT(Pixel(12, 50) != before && Pixel(12, 52) == before && Pixel(16, 50) == before);
		Rect drawn;
		SetRect(&drawn, 0, 40, 40, 60);
		EraseRect(&drawn);						// (the later tests want the map as it was)
	}
	Eval("inkShape := MakeInk(SetClass(\"x\", 'ink), 5, 6, 25, 16)");
	EXPECT(EQRef(ClassOf(Eval("inkShape.originalBounds")), Intern((char*) "boundsRect")) && !EQRef(Eval("inkShape.originalBounds"), Eval("inkShape.bounds")));
	{
		RefVar box(Eval("inkShape.originalBounds"));
		const Rect* r = (const Rect*) BinaryData(box);
		EXPECT(r->left == 5 && r->top == 6 && r->right == 25 && r->bottom == 16);
	}

	// MungeShape: a rectangle turned a quarter right about its middle,
	// a line flipped left to right (views/ShapeVerbs.cpp)
	Eval("mungeRect := MakeRect(10, 10, 30, 20)");
	Eval("MungeShape(mungeRect, 'rotateRight, nil)");
	EXPECT(RINT(Eval("GetShapeInfo(mungeRect).bounds.left")) == 15 && RINT(Eval("GetShapeInfo(mungeRect).bounds.top")) == 5);
	EXPECT(RINT(Eval("GetShapeInfo(mungeRect).bounds.right")) == 25 && RINT(Eval("GetShapeInfo(mungeRect).bounds.bottom")) == 25);
	Eval("mungeLine := MakeLine(1, 2, 3, 4)");
	Eval("MungeShape(mungeLine, 'flipHorizontal, nil)");
	EXPECT(RINT(Eval("GetShapeInfo(mungeLine).start.x")) == 3 && RINT(Eval("GetShapeInfo(mungeLine).stop.x")) == 1);
	EXPECT(RINT(Eval("GetShapeInfo(mungeLine).start.y")) == 2);
	// MungeBitmap: a 10 x 3 bitmap with its top left pixel set, turned
	// and flipped (qd/MungeBitmap.cpp); a half turn moves the row's
	// padding to its start, which puts the pixel outside the 10 columns
	// (a ROM quirk kept)
	{
		// (a bitmap under sixteen bytes is not turned half round at all -
		// another quirk - so the half turn is also tried on eight rows)
		struct { const char* op; long rows; long x, y; long width, height; } cases[] = {
			{ "rotateRight", 3, 2, 0, 3, 10 },
			{ "rotateLeft", 3, 0, 9, 3, 10 },
			{ "flipHorizontal", 3, 9, 0, 10, 3 },
			{ "flipVertical", 3, 0, 2, 10, 3 },
			{ "rotate180", 3, 0, 0, 10, 3 },
			{ "rotate180", 8, 31, 7, 10, 8 } };
		for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
		{
			char make[40];
			snprintf(make, sizeof(make), "MakeBitmap(10, %ld, nil)", cases[c].rows);
			RefVar bitmap(Eval(make));
			{
				TPixelObj obj;
				obj.Init(bitmap);
				((UByte*) GetPixelMapBits(obj.Pixels()))[0] = 0x80;
			}
			SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "mungeBitmap")), bitmap);
			char source[80];
			snprintf(source, sizeof(source), "MungeBitmap(mungeBitmap, '%s, nil)", cases[c].op);
			Eval(source);
			TPixelObj obj;
			obj.Init(bitmap);
			PixelMap* pm = obj.Pixels();
			EXPECT(pm->bounds.right - pm->bounds.left == cases[c].width && pm->bounds.bottom - pm->bounds.top == cases[c].height);
			long set = 0;
			for (long y = 0; y < cases[c].height; y++)
				for (long x = 0; x < pm->rowBytes * 8; x++)
					if (((UByte*) GetPixelMapBits(pm))[y * pm->rowBytes + x / 8] & (0x80 >> (x & 7)))
					{
						set++;
						if (x != cases[c].x || y != cases[c].y)
							printf("munge %s: pixel at %ld, %ld\n", cases[c].op, x, y);
						EXPECT(x == cases[c].x && y == cases[c].y);
					}
			EXPECT(set == 1);
		}
	}

	// MakeBitmap makes an offscreen bitmap and DrawIntoBitmap draws into
	// it (qd/Pictures.cpp, views/DrawShape.cpp)
	Eval("bm := MakeBitmap(40, 20, nil)");
	// the `bounds` slot is a 'boundsRect binary, as the ROM makes it -
	// the pixel map inside the shape carries the same rectangle
	EXPECT(EQRef(ClassOf(Eval("bm.bounds")), Intern((char*) "boundsRect")));
	{
		RefVar data(Eval("bm.data"));
		LockRef(data);
		// the binary is the ROM's: a 0x1c-byte big-endian header, then the rows
		const UByte* raw = (const UByte*) BinaryData(data);
		EXPECT(Length(data) == 0x1c + 8 * 20);
		EXPECT(GetBigEndianWord(raw) == 0x1c && GetBigEndianHalf(raw + 4) == 8);
		EXPECT(GetBigEndianHalf(raw + 12) == 20 && GetBigEndianHalf(raw + 14) == 40);
		EXPECT(GetBigEndianWord(raw + 0x10) == 0x80001001);		// kPixMapOffset | kPixMapVersion2 | one bit deep
		EXPECT(GetBigEndianHalf(raw + 0x14) == 72 && GetBigEndianHalf(raw + 0x16) == 72);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		EXPECT(pm->bounds.right == 40 && pm->bounds.bottom == 20);
		EXPECT(pm->rowBytes == 8);				// forty bits rounded up to a whole word
		EXPECT(PixelMapDepth(pm) == 1);
		UnlockRef(data);
	}
	EXPECT(EQRef(ClassOf(Eval("bm.data")), Intern((char*) "pixels")));
	// the options frame: the row bytes are rounded up to a whole word,
	// and every slot that is not one of the known ones is copied on
	Eval("bm2 := MakeBitmap(33, 4, {rowBytes: 8, mine: 'kept})");
	EXPECT(EQRef(Eval("bm2.mine"), Intern((char*) "kept")));
	EXPECT(ISNIL(Eval("bm2.rowBytes")));		// the options the bitmap used are not copied
	// a depth that is not a power of two, and a width of nought, are refused
	EXPECT(NOTNIL(Eval("call func() begin try MakeBitmap(8, 8, {depth: 3}) onexception |evt.ex.graf| do 'threw end with ()")));
	EXPECT(NOTNIL(Eval("call func() begin try MakeBitmap(0, 8, nil) onexception |evt.ex.graf| do 'threw end with ()")));
	// nothing is drawn in it yet, and a filled rectangle turns pixels on
	{
		RefVar data(Eval("bm.data"));
		LockRef(data);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		long lit = 0;
		for (long y = 0; y < 20; y++)
			for (long x = 0; x < 40; x++)
				if (GetPixel(pm, x, y) != 0)
					lit++;
		EXPECT(lit == 0);
		UnlockRef(data);
	}
	// a rectangle framed, then the same one filled black
	Eval("DrawIntoBitmap(MakeRect(2, 3, 12, 9), nil, bm)");
	{
		RefVar data(Eval("bm.data"));
		LockRef(data);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		long lit = 0;
		for (long y = 0; y < 20; y++)
			for (long x = 0; x < 40; x++)
				if (GetPixel(pm, x, y) != 0)
					lit++;
		EXPECT(lit == 28);					// the outline of a ten-by-six rectangle
		EXPECT(GetPixel(pm, 2, 3) != 0 && GetPixel(pm, 11, 8) != 0);
		EXPECT(GetPixel(pm, 5, 5) == 0);	// not filled
		EXPECT(GetPixel(pm, 1, 3) == 0 && GetPixel(pm, 12, 8) == 0);
		UnlockRef(data);
	}
	// a bitmap at 144 dpi is drawn into at twice the size (DrawShapeScaled)
	Eval("bm144 := MakeBitmap(40, 20, {resolution: 144})");
	Eval("DrawIntoBitmap(MakeRect(2, 3, 12, 9), nil, bm144)");
	{
		RefVar data(Eval("bm144.data"));
		LockRef(data);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		long lit = 0;
		long left = 99, top = 99, right = -1, bottom = -1;
		for (long y = 0; y < 20; y++)
			for (long x = 0; x < 40; x++)
				if (GetPixel(pm, x, y) != 0)
				{
					lit++;
					if (x < left) left = x;
					if (y < top) top = y;
					if (x > right) right = x;
					if (y > bottom) bottom = y;
				}
		EXPECT(left == 4 && top == 6 && right == 23 && bottom == 17);
		EXPECT(lit == 112);					// the frame and its pen doubled too
		UnlockRef(data);
	}
	// text in a font whose color is a gray: the slab knocked out in a
	// checkerboard on a one-bit map (MakeGrayText), so the H's stems
	// alternate
	Eval("bmText := MakeBitmap(40, 20, nil)");
	// (a text shape's baseline is its bounds' bottom: this H sits on row 14)
	Eval("DrawIntoBitmap(MakeText(\"H\", 2, 2, 30, 14), {font: {family: 'espy, face: 0, size: 12, color: 3}}, bmText)");
	{
		RefVar data(Eval("bmText.data"));
		LockRef(data);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		EXPECT(GetPixel(pm, 2, 6) != 0 && GetPixel(pm, 2, 7) == 0 && GetPixel(pm, 2, 8) != 0 && GetPixel(pm, 8, 12) != 0);
		EXPECT(GetPixel(pm, 3, 9) != 0 && GetPixel(pm, 4, 9) == 0 && GetPixel(pm, 5, 9) != 0 && GetPixel(pm, 2, 9) == 0);
		UnlockRef(data);
	}
	// a text shape in a box taller than its line is drawn on the box's
	// bottom, as the ROM's DrawOneShape does (the In Box's item header puts
	// its second line in a box as tall as the header, below the first)
	Eval("bmTall := MakeBitmap(40, 30, nil)");
	Eval("DrawIntoBitmap(MakeText(\"H\", 2, 0, 30, 25), {font: {family: 'espy, face: 0, size: 12}}, bmTall)");
	{
		RefVar data(Eval("bmTall.data"));
		LockRef(data);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		long top = -1, bottom = -1;
		for (long y = 0; y < 30; y++)
			for (long x = 0; x < 40; x++)
				if (GetPixel(pm, x, y) != 0)
				{
					if (top < 0)
						top = y;
					bottom = y;
				}
		EXPECT(bottom == 24 && top == 16);			// the H's nine rows, on the baseline at 25
		UnlockRef(data);
	}
	Eval("DrawIntoBitmap(MakeRect(2, 3, 12, 9), {fillPattern: 5}, bm)");
	{
		RefVar data(Eval("bm.data"));
		LockRef(data);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		long lit = 0;
		for (long y = 0; y < 20; y++)
			for (long x = 0; x < 40; x++)
				if (GetPixel(pm, x, y) != 0)
					lit++;
		EXPECT(lit == 60);					// ten across by six down, filled
		EXPECT(GetPixel(pm, 5, 5) != 0);
		UnlockRef(data);
	}
	Eval("bm := nil; bm2 := nil");

	// CopyBits puts a bitmap at a point of the view and DoDrawing lets a
	// script draw outside a viewDrawScript, both in the view's coordinates
	{
		static const unsigned char kBox[4] = { 0xf0, 0x90, 0x90, 0xf0 };		// a hollow 4 x 4 box
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "pict")), RefVar(MakeBitmap(kBox, 8, 4)));
		TView* d = ViewOf("ctxD := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 120, bottom: 90}, PaintIt: func() begin :CopyBits(pict, 2, 2, nil); :DrawShape(MakeRect(20, 20, 30, 26), {fillPattern: 5}) end})");
		EXPECT(d != nil);
		Refresh();
		EXPECT(InkIn(0, 0, kWidth, kHeight) == 0);
		Eval("ctxD:DoDrawing('PaintIt, nil)");
		EXPECT(Pixel(22, 12) == 1 && Pixel(25, 12) == 1 && Pixel(23, 13) == 0);	// the box's outline
		EXPECT(InkIn(40, 30, 50, 36) == 60);									// the filled rectangle
		// a view that is not visible is not drawn in at all
		Eval("ctxD:Hide()");
		Refresh();
		EXPECT(InkIn(0, 0, kWidth, kHeight) == 0);
		Eval("ctxD:DoDrawing('PaintIt, nil)");
		EXPECT(InkIn(0, 0, kWidth, kHeight) == 0);
		Eval("ctxD:Close()");
		Refresh();
	}

	// DrawXBitmap: one image out of a strip of them, all the width of the
	// bounds.  The picture is an 8 x 4 row holding a hollow 4 x 4 box in
	// its left half and nothing in its right, so cell 0 is the box and
	// cell 1 is blank.
	{
		static const unsigned char kStrip[4] = { 0xf0, 0x90, 0x90, 0xf0 };
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "strip")), RefVar(MakeBitmap(kStrip, 8, 4)));
		TView* x = ViewOf("ctxX := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 120, bottom: 90}, "
			"PaintIt: func() :DrawXBitmap({left: 0, top: 0, right: 4, bottom: 4}, strip, cell, 0)})");
		EXPECT(x != nil);
		Refresh();
		Eval("cell := 0");
		Eval("ctxX:DoDrawing('PaintIt, nil)");
		EXPECT(Pixel(20, 10) == 1 && Pixel(23, 10) == 1 && Pixel(21, 11) == 0 && Pixel(24, 10) == 0);
		Eval("ctxX:Dirty()");
		Refresh();
		EXPECT(InkIn(0, 0, kWidth, kHeight) == 0);
		Eval("cell := 1");
		Eval("ctxX:DoDrawing('PaintIt, nil)");
		EXPECT(InkIn(0, 0, kWidth, kHeight) == 0);		// the strip's right half is empty
		Eval("ctxX:Close()");
		Refresh();
	}

	// drawn from a view's viewDrawScript: the origin is the view's top left
	Eval("shapes := nil; shapeStyle := nil");
	TView* v = ViewOf("ctxS := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 120, bottom: 90}, viewDrawScript: func() :DrawShape(shapes, shapeStyle)})");
	EXPECT(v != nil);
	// a filled rectangle: fillPattern 5 is black; the frame (the pen) lies inside it
	Eval("shapes := MakeRect(2, 2, 12, 8); shapeStyle := {fillPattern: 5}");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(InkIn(22, 12, 32, 18) == 60 && InkIn(0, 0, kWidth, kHeight) == 60);
	// no pen: the outline is not drawn; a gray fill
	Eval("shapeStyle := {penPattern: 0, fillPattern: 3}");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(InkIn(22, 12, 32, 18) == 30);
	// the pen alone: the outline, a pixel wide
	Eval("shapeStyle := nil");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(InkIn(22, 12, 32, 18) == 28 && Pixel(22, 12) == 1 && Pixel(31, 17) == 1 && Pixel(25, 14) == 0);
	// a wider pen
	Eval("shapeStyle := {penSize: 2}");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(InkIn(22, 12, 32, 18) == 48 && Pixel(25, 14) == 0 && Pixel(23, 13) == 1);
	// a line
	Eval("shapes := MakeLine(0, 0, 9, 9); shapeStyle := nil");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(Pixel(20, 10) == 1 && Pixel(29, 19) == 1 && Pixel(25, 15) == 1 && Pixel(25, 16) == 0 && InkIn(0, 0, kWidth, kHeight) == 10);
	// an oval and a polygon, filled
	Eval("shapes := [MakeOval(0, 0, 8, 8), MakePolygon([20, 0, 30, 0, 25, 10])]; shapeStyle := {fillPattern: 5}");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(InkIn(20, 10, 28, 18) > 40 && Pixel(20, 10) == 0 && Pixel(23, 13) == 1);
	EXPECT(Pixel(45, 10) == 1 && Pixel(45, 18) == 1 && Pixel(40, 19) == 0 && InkIn(40, 10, 51, 21) > 40);
	// a style in a list applies to what follows; a nested list keeps its style to itself
	// (penPattern 0 - vfNone - is no outline, as the ROM's own style frames have it; a nil slot is not looked at)
	Eval("shapes := [MakeRect(0, 0, 10, 10), {penPattern: 0, fillPattern: 5}, MakeRect(20, 0, 30, 10), [{penPattern: 0, fillPattern: 1}, MakeRect(40, 0, 50, 10)], MakeRect(60, 0, 70, 10)]; shapeStyle := nil");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(InkIn(20, 10, 30, 20) == 36);		// the outline
	EXPECT(InkIn(40, 10, 50, 20) == 100);		// filled
	EXPECT(InkIn(60, 10, 70, 20) == 0);			// white, no pen
	EXPECT(InkIn(80, 10, 90, 20) == 100);		// filled again: the nested list's style is gone
	// text: MakeText draws a line in the style's font from the bounds' top; MakeTextBox wraps
	Eval("shapes := MakeText(\"Hello\", 0, 0, 60, 20); shapeStyle := {font: espy12}");
	Eval("ctxS:Dirty()");
	Refresh();
	long inkLeft, inkRight;
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight == 46);	// "Hello" as the text view drew it, its baseline the ascent below the top
	Eval("shapeStyle := {font: espy12, justification: 'right}");
	Eval("ctxS:Dirty()");
	Refresh();
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkRight == 79 && inkLeft == 53);
	Eval("shapes := MakeTextBox(\"Hello World\", 0, 0, 40, 40); shapeStyle := {font: espy12}");
	Eval("ctxS:Dirty()");
	Refresh();
	InkExtent(10, 26, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight == 46);
	InkExtent(26, 42, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight > 20);
	// clipping: the style's clipping shape limits what is drawn
	Eval("shapes := MakeRect(0, 0, 20, 20); shapeStyle := {penPattern: 0, fillPattern: 5, clipping: MakeRect(5, 5, 10, 10)}");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(InkIn(0, 0, kWidth, kHeight) == 25 && Pixel(25, 15) == 1 && Pixel(24, 15) == 0);
	// a region shape drawn
	Eval("shapes := MakeRegion(MakeOval(0, 0, 8, 8)); shapeStyle := {penPattern: 0, fillPattern: 5}");
	Eval("ctxS:Dirty()");
	Refresh();
	EXPECT(InkIn(20, 10, 28, 18) > 40 && Pixel(20, 10) == 0 && Pixel(23, 13) == 1);
	Eval("ctxS:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "shapes closed"));
	// ScaleShape(shape, src, dst): the shape stretched in place, as though
	// src had been pulled into dst and the shape had come with it
	EXPECT(RINT(Eval("local r := MakeRect(0, 0, 10, 10); "
					 "ScaleShape(r, {left: 0, top: 0, right: 10, bottom: 10}, {left: 0, top: 0, right: 20, bottom: 40}); "
					 "ShapeBounds(r).bottom")) == 40);
	// a nil source is the shape's own bounds, so it is simply fitted
	EXPECT(RINT(Eval("local o := MakeOval(0, 0, 8, 8); "
					 "ScaleShape(o, nil, {left: 10, top: 10, right: 30, bottom: 50}); ShapeBounds(o).left")) == 10);
	EXPECT(RINT(Eval("local o := MakeOval(0, 0, 8, 8); "
					 "ScaleShape(o, nil, {left: 10, top: 10, right: 30, bottom: 50}); ShapeBounds(o).bottom")) == 50);
	// a line moves by its two ends
	EXPECT(RINT(Eval("local l := MakeLine(0, 0, 10, 10); "
					 "ScaleShape(l, nil, {left: 0, top: 0, right: 20, bottom: 20}); ShapeBounds(l).right")) == 20);
	// a polygon through its own points
	EXPECT(RINT(Eval("local p := MakePolygon([0, 0, 10, 0, 10, 10]); "
					 "ScaleShape(p, nil, {left: 0, top: 0, right: 30, bottom: 30}); ShapeBounds(p).right")) >= 30);
	// a region: its data is mapped and written back at the size the
	// mapped region came out
	EXPECT(RINT(Eval("local g := MakeRegion([MakeRect(0, 0, 10, 10), MakeRect(20, 20, 30, 30)]); "
					 "ScaleShape(g, nil, {left: 0, top: 0, right: 60, bottom: 60}); ShapeBounds(g).right")) == 60);
	// a list is mapped member by member out of the list's own bounds, so
	// the members keep their places in it
	EXPECT(RINT(Eval("local a := [MakeRect(0, 0, 5, 5), MakeRect(5, 5, 10, 10)]; "
					 "ScaleShape(a, nil, {left: 0, top: 0, right: 20, bottom: 20}); ShapeBounds(a[0]).right")) == 10);
	EXPECT(RINT(Eval("local a := [MakeRect(0, 0, 5, 5), MakeRect(5, 5, 10, 10)]; "
					 "ScaleShape(a, nil, {left: 0, top: 0, right: 20, bottom: 20}); ShapeBounds(a[1]).left")) == 10);
}


static void
TestCommands()
{
	// the command frames
	EXPECT(gApplication != nil && gApplication->ClassID() == clApplication);
	TView* root = gRootView;
	RefVar cmd(MakeCommand(aeShow, root, 42));
	EXPECT(CommandID(cmd) == aeShow && CommandParameter(cmd) == 42 && CommandReceiver(cmd) == root && CommandResult(cmd) == 0);
	EXPECT(EQRef(GetFrameSlotRef(cmd, RSSYMreceiver), root->fContext));
	CommandSetIndexParameter(cmd, 2, 7);
	EXPECT(CommandIndexParameter(cmd, 2) == 7 && CommandIndexParameter(cmd, 0) == 0 && CommandIndexParameter(cmd, 5) == 0 && RINT(Eval("Length(GetRoot().viewChildren)")) >= 0);
	EXPECT(!IsUndoCommand(cmd));
	MarkUndoCommand(cmd);
	EXPECT(IsUndoCommand(cmd));
	cmd = MakeCommand(aeUndo, gApplication, kNoParameter);
	EXPECT(CommandReceiver(cmd) == gApplication && EQRef(GetFrameSlotRef(cmd, RSSYMreceiver), RSSYMapplication));

	// aeShow/aeHide through the application to a view; the result answers
	TView* v = ViewOf("ctxC := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 10, top: 10, right: 30, bottom: 30}, viewFormat: 5, clicks: 0, viewClickScript: func(unit) begin clicks := clicks + 1; if clicks = 2 then 'skip else true end})");
	Eval("ctxC:Dirty()");
	Refresh();
	EXPECT(Pixel(15, 15) == 1);
	Eval("PostCommand(ctxC, 0x2b)");			// aeHide
	Refresh();
	EXPECT(Pixel(15, 15) == 0 && (v->fFlags & vVisible) == 0);
	Eval("PostCommand(ctxC, 0x2c)");			// aeShow
	Refresh();
	EXPECT(Pixel(15, 15) == 1 && (v->fFlags & vVisible) != 0);
	// aeClick runs viewClickScript on a clickable view; 'skip leaves the result 0
	cmd = MakeCommand(aeClick, v, 0);
	EXPECT(gApplication->DispatchCommand(cmd) == 1 && RINT(Eval("ctxC.clicks")) == 1);
	cmd = MakeCommand(aeClick, v, 0);
	EXPECT(gApplication->DispatchCommand(cmd) == 0 && RINT(Eval("ctxC.clicks")) == 2);
	// a command nobody takes: the result 0, the root told (Notify defined here)
	Eval("GetRoot().notified := nil; GetRoot().Notify := func(kind, err, x) notified := [kind, err]");
	cmd = MakeCommand(0x66, v, 0);
	EXPECT(gApplication->DispatchCommand(cmd) == 0 && ISNIL(Eval("GetRoot().notified")));
	// PostCommandParam with a frame parameter: aeAddHilite takes the hilite
	// out of the frame's 'hilite slot and appends that, as the ROM does (a
	// real hilite is a THilite pointer Ref, so the slot is emptied again
	// rather than left holding a symbol)
	Eval("PostCommandParam(ctxC, 0x47, {hilite: 'h1})");
	EXPECT(RINT(Eval("Length(ctxC.hilites)")) == 1 && EQRef(Eval("ctxC.hilites[0]"), Intern((char*) "h1")));
	Eval("ctxC.hilites := nil");
	// aeMoveData moves the view and posts its undo; Undo moves it back
	Eval("ClearUndoStacks()");
	gApplication->Idle();
	cmd = MakeCommand(aeMoveData, v, kNoParameter);
	CommandSetIndexParameter(cmd, 0, 20);
	CommandSetIndexParameter(cmd, 1, 5);
	gApplication->DispatchCommand(cmd);
	Refresh();
	EXPECT(BoundsAre(v, 30, 15, 50, 35) && Pixel(35, 20) == 1 && Pixel(15, 15) == 0);
	EXPECT(Length(gApplication->GetUndoStack(0)) == 1 && EQRef(gApplication->GetUndoState(), RSSYMundo));
	Eval("Undo()");
	Refresh();
	EXPECT(BoundsAre(v, 10, 10, 30, 30) && Pixel(15, 15) == 1 && Pixel(35, 20) == 0);
	// without the undoRedo preference the undo stack is now the batch before (none): a second Undo does nothing
	EXPECT(Length(gApplication->GetUndoStack(0)) == 0);
	Eval("Undo()");
	Refresh();
	EXPECT(BoundsAre(v, 10, 10, 30, 30));
	// with the preference the undone command's inverse stays: Undo again redoes the move
	Eval("userConfiguration.undoRedo := true");
	gApplication->Idle();
	cmd = MakeCommand(aeMoveData, v, kNoParameter);
	CommandSetIndexParameter(cmd, 0, 20);
	CommandSetIndexParameter(cmd, 1, 5);
	gApplication->DispatchCommand(cmd);
	Eval("Undo()");
	Refresh();
	EXPECT(BoundsAre(v, 10, 10, 30, 30) && Length(gApplication->GetUndoStack(0)) == 1 && EQRef(gApplication->GetUndoState(), RSSYMundoredo));
	Eval("Undo()");
	Refresh();
	EXPECT(BoundsAre(v, 30, 15, 50, 35) && EQRef(gApplication->GetUndoState(), RSSYMundo));
	Eval("userConfiguration.undoRedo := nil");
	// AddUndoAction: a script run on the view by Undo
	Eval("ClearUndoStacks()");
	gApplication->Idle();
	Eval("ctxC.undone := nil; ctxC:AddUndoAction('SetUndone, [3])");
	Eval("ctxC.SetUndone := func(n) undone := n");
	Eval("Undo()");
	EXPECT(RINT(Eval("ctxC.undone")) == 3);
	// AddUndoCall and AddUndoSend
	gApplication->Idle();
	Eval("called := nil; AddUndoCall(func(a) called := a, [5]); AddUndoSend(ctxC, 'SetUndone, [6])");
	EXPECT(Length(gApplication->GetUndoStack(0)) == 2);
	Eval("Undo()");
	EXPECT(RINT(Eval("called")) == 5 && RINT(Eval("ctxC.undone")) == 6);
	// delayed actions: deferred ones run at the next RunDelayedActions, delayed ones when due
	Eval("ran := []; AddDeferredCall(func(a) AddArraySlot(ran, a), ['x]); AddDeferredSend(ctxC, 'SetUndone, [9]); AddDeferredAction(func(a) AddArraySlot(ran, a), ['y])");
	EXPECT(RINT(Eval("Length(ran)")) == 0);
	Eval("RunDelayedActions()");
	EXPECT(RINT(Eval("Length(ran)")) == 2 && EQRef(Eval("ran[0]"), Intern((char*) "x")) && EQRef(Eval("ran[1]"), Intern((char*) "y")) && RINT(Eval("ctxC.undone")) == 9);
	Eval("AddDelayedCall(func() AddArraySlot(ran, 'later), [], 100000)");
	Eval("RunDelayedActions()");
	EXPECT(RINT(Eval("Length(ran)")) == 2);
	EXPECT(NOTNIL(gApplication->fDelayedActions) && Length(gApplication->fDelayedActions) == 4);
	gApplication->fDelayedActions = NILREF;
	// aeAddChild/aeDropChild through the application
	Eval("PostCommandParam(ctxC, 0x29, {viewClass: 74, viewFlags: 1, viewBounds: {left: 2, top: 2, right: 8, bottom: 8}, viewFormat: 5})");
	EXPECT(v->fChildren->Count() == 1);
	TView* child = v->fChildren->At(0);
	EXPECT(BoundsAre(child, 32, 17, 38, 23));
	cmd = MakeCommand(aeDropChild, v, (Long) child);
	gApplication->DispatchCommand(cmd);
	EXPECT(v->fChildren->Count() == 0);
	Refresh();
	Eval("ctxC:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "commands closed"));
}


// Hilite/Select: a view inverted while selected, TrackHilite/TrackButton
// without a stroke (a press at the centre), HiliteUnique, the
// viewHiliteScript taking over.
static void
TestHilite()
{
	TView* v = ViewOf("ctxH := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 10, top: 10, right: 30, bottom: 30}, viewFormat: 1 + 0x50 + 0x100 + 0x10000 + (2 << 24), pressed: 0, clicked: nil, buttonPressedScript: func() begin pressed := pressed + 1; nil end, buttonClickScript: func() clicked := true})");
	Eval("ctxH:Dirty()");
	Refresh();
	// a white, round-cornered box framed a pixel outside its bounds (viewFormat: fill white, frame black, pen 1, inset 1, round 2)
	EXPECT(Pixel(15, 15) == 0 && Pixel(8, 15) == 1 && Pixel(31, 15) == 1 && Pixel(9, 15) == 0 && Pixel(9, 9) == 0);
	long framed = InkIn(7, 7, 33, 33);
	// Hilite(true): the bounds let out by the inset (9,9,31,31) inverted as a round rectangle of radius 4 - the corners stay white, the frame stays
	Eval("ctxH:Hilite(true)");
	EXPECT((v->fFlags & vSelected) != 0 && Pixel(15, 15) == 1 && Pixel(9, 15) == 1 && Pixel(30, 15) == 1 && Pixel(9, 9) == 0 && Pixel(30, 30) == 0 && Pixel(8, 15) == 1);
	EXPECT(InkIn(9, 9, 31, 31) > 470 && InkIn(9, 9, 31, 31) < 22 * 22);
	Eval("ctxH:Hilite(true)");		// already selected: nothing changes
	EXPECT(Pixel(15, 15) == 1);
	Eval("ctxH:Hilite(nil)");
	EXPECT((v->fFlags & vSelected) == 0 && Pixel(15, 15) == 0 && Pixel(9, 15) == 0 && InkIn(7, 7, 33, 33) == framed);
	// TrackHilite with no stroke: pressed at the centre for two turns, the button left hilited
	RefVar result(Eval("ctxH:TrackHilite(nil)"));
	EXPECT(NOTNIL(result) && RINT(Eval("ctxH.pressed")) == 2 && (v->fFlags & vSelected) != 0 && Pixel(15, 15) == 1);
	Eval("ctxH:Hilite(nil)");
	// TrackButton: the click script run, the view un-hilited after
	Eval("ctxH.pressed := 0");
	result = Eval("ctxH:TrackButton(nil)");
	EXPECT(NOTNIL(result) && NOTNIL(Eval("ctxH.clicked")) && RINT(Eval("ctxH.pressed")) == 2 && (v->fFlags & vSelected) == 0 && Pixel(15, 15) == 0);
	// a throwing click script still leaves the button un-hilited
	Eval("ctxH.buttonClickScript := func() Throw('|evt.ex.msg|, \"boom\")");
	Eval("caught := nil; try ctxH:TrackButton(nil) onexception |evt.ex.msg| do caught := true");
	EXPECT(NOTNIL(Eval("caught")) && (v->fFlags & vSelected) == 0 && Pixel(15, 15) == 0);
	// a buttonPressedScript's answer ends the tracking only with newt_feature set
	Eval("ctxH.buttonPressedScript := func() begin pressed := pressed + 1; 'stop end; ctxH.pressed := 0");
	result = Eval("ctxH:TrackHilite(nil)");
	EXPECT(EQRef(result, TRUEREF) && RINT(Eval("ctxH.pressed")) == 2);
	Eval("ctxH:Hilite(nil); ctxH.newt_feature := true; ctxH.pressed := 0");
	result = Eval("ctxH:TrackHilite(nil)");
	EXPECT(EQRef(result, Intern((char*) "stop")) && RINT(Eval("ctxH.pressed")) == 1);
	Eval("ctxH:Hilite(nil)");
	// HiliteUnique: the sibling selected before is un-hilited (its flag stays, as the ROM leaves it)
	TView* w = ViewOf("ctxH2 := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 40, top: 10, right: 60, bottom: 30}, viewFormat: 1})");
	Eval("ctxH2:Dirty()");
	Refresh();
	Eval("ctxH:Hilite(true)");
	EXPECT(Pixel(15, 15) == 1 && Pixel(45, 15) == 0);
	Eval("ctxH2:HiliteUnique(true)");
	EXPECT(Pixel(15, 15) == 0 && Pixel(45, 15) == 1 && (w->fFlags & vSelected) != 0 && (v->fFlags & vSelected) != 0);
	v->ClearFlags(vSelected);
	Eval("ctxH2:Hilite(nil)");
	EXPECT(Pixel(45, 15) == 0);
	// the viewHiliteScript: a non-nil answer means it did the hiliting itself (a slot the view was
	// made with: the slot cache's bit is cleared once a lookup finds nothing)
	TView* h = ViewOf("ctxH3 := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 40, top: 40, right: 60, bottom: 60}, viewFormat: 1, hilited: [], viewHiliteScript: func(on) begin AddArraySlot(hilited, on); true end})");
	Eval("ctxH3:Dirty()");
	Refresh();
	Eval("ctxH3:Hilite(true)");
	EXPECT(Pixel(45, 45) == 0 && (h->fFlags & vSelected) != 0 && RINT(Eval("Length(ctxH3.hilited)")) == 1 && EQRef(Eval("ctxH3.hilited[0]"), TRUEREF));
	Eval("ctxH3:Hilite(nil)");
	EXPECT(RINT(Eval("Length(ctxH3.hilited)")) == 2 && ISNIL(Eval("ctxH3.hilited[1]")));
	Eval("ctxH3:Close()");
	// a hidden view is not hilited on screen, though selected
	Eval("ctxH:Hide()");
	Refresh();
	Eval("ctxH:Hilite(true)");
	EXPECT((v->fFlags & vSelected) != 0 && Pixel(15, 15) == 0);
	Eval("ctxH:Hilite(nil)");
	Eval("ctxH:Close(); ctxH2:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "hilite closed"));
}


// The keyboard: the German 'kchr mapping of the ROM's locale bundle, the
// key maps and modifiers, a dead key, key events to a key view and its
// scripts, a key command, PostKeyString and HandleKeyEvents.
// The data hilites: what is selected *inside* a view (views/Hilites.h),
// as against TView::Hilite, which inverts a whole one.  The base class only
// keeps them - each a THilite whose address is a pointer Ref in the view's
// hilites array - so what is checked here is the keeping: HiliteAll makes
// one over the whole view, GlobalHiliteBounds unions their bounds into the
// parent's coordinates, and RemoveAllHilites takes them out again.
static void
TestDataHilites()
{
	TView* v = ViewOf("ctxDH := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 20, top: 40, right: 60, bottom: 70}})");
	EXPECT(!v->Hilited() && ISNIL(v->FirstHilite()));
	EXPECT(v->IsCompletelyHilited(RefVar(NILREF)));	// a view with no items of its own

	// HiliteAll selects the whole view, in the view's own coordinates
	v->HiliteAll();
	EXPECT(v->Hilited() && RINT(Eval("Length(ctxDH.hilites)")) == 1);
	RefVar first(v->FirstHilite());
	THilite* hilite = (THilite*) RefToAddress(first);
	EXPECT(hilite != nil);
	EXPECT(hilite->fBounds.left == 0 && hilite->fBounds.top == 0
		&& hilite->fBounds.right == 40 && hilite->fBounds.bottom == 30);

	// the hilite answers about itself in those coordinates
	Point inside;  inside.h = 10;  inside.v = 10;
	Point outside; outside.h = 50; outside.v = 10;
	EXPECT(hilite->Encloses(inside) && !hilite->Encloses(outside));
	Rect over;  SetRect(&over, 5, 5, 15, 15);
	Rect clear; SetRect(&clear, 100, 100, 110, 110);
	EXPECT(hilite->Overlaps(over) && !hilite->Overlaps(clear));
	TRegionVar area;
	hilite->Area(area);
	EXPECT(!EmptyRgn(area) && EqualRect(&(*(RgnHandle) area)->rgnBBox, &hilite->fBounds));

	// GlobalHiliteBounds offsets them into the parent's coordinates and
	// unions them into whatever the caller had
	Rect bounds;
	SetEmptyRect(&bounds);
	v->GlobalHiliteBounds(&bounds);
	EXPECT(bounds.left == 20 && bounds.top == 40 && bounds.right == 60 && bounds.bottom == 70);

	// a loop hands out each hilite and the object behind it
	long seen = 0;
	{
		HiliteLoop loop(v);
		while (loop.Next())
		{
			EXPECT(loop.fCurrent == hilite && EQRef(loop.fHilite, first));
			seen++;
		}
	}
	EXPECT(seen == 1);

	// HiliteAll again replaces the one that was there
	v->HiliteAll();
	EXPECT(RINT(Eval("Length(ctxDH.hilites)")) == 1);

	// and RemoveAllHilites takes them all out (the objects disposed of with
	// them) - the last one out leaves the slot nil, as the ROM's
	// ArrayRemove(frame, slot, value) does
	v->RemoveAllHilites();
	EXPECT(!v->Hilited() && ISNIL(Eval("ctxDH.hilites")) && ISNIL(v->FirstHilite()));
	SetEmptyRect(&bounds);
	v->GlobalHiliteBounds(&bounds);
	EXPECT(EmptyRect(&bounds));		// nothing selected, nothing added


	// PointInHilite asks the hilites, in the view's own coordinates
	v->HiliteAll();
	Point on;  on.h = 30;  on.v = 50;		// inside (20,40,60,70)
	Point off; off.h = 70; off.v = 50;
	EXPECT(v->PointInHilite(on) && !v->PointInHilite(off));
	v->RemoveAllHilites();
	EXPECT(!v->PointInHilite(on));

	// a scrub takes the view when it covers more than three quarters of it
	Rect all;    SetRect(&all, 20, 40, 60, 70);
	Rect most;   SetRect(&most, 20, 40, 60, 66);	// 26 of 30 rows: 86%
	Rect little; SetRect(&little, 20, 40, 60, 55);	// half
	EXPECT(v->HandleScrub(all, 5, nil, true));
	EXPECT(v->HandleScrub(most, 5, nil, true));
	EXPECT(!v->HandleScrub(little, 5, nil, true));
	EXPECT(!v->HandleScrub(all, 2, nil, true));		// not a scrub gesture

	// and a read-only view takes none
	v->fFlags |= vReadOnly;
	EXPECT(!v->HandleScrub(all, 5, nil, true));
	v->fFlags &= ~vReadOnly;

	// CoveredBy is what both of those ask (qd/Rects.h): the percentage of the
	// first rectangle the intersection covers, a degenerate one given a pixel
	Rect half;  SetRect(&half, 0, 0, 10, 10);
	Rect qtr;   SetRect(&qtr, 0, 0, 5, 10);
	Rect away;  SetRect(&away, 100, 100, 110, 110);
	Rect line;  SetRect(&line, 0, 5, 10, 5);		// no height
	EXPECT(CoveredBy(&half, &half) == 100);
	EXPECT(CoveredBy(&half, &qtr) == 50);
	EXPECT(CoveredBy(&qtr, &half) == 100);
	EXPECT(CoveredBy(&half, &away) == 0);
	EXPECT(CoveredBy(&half, &line) == 10);		// the line given a row of its own
	// Intersects and Overlaps, which DoDrawing asks of the caret's rectangle
	EXPECT(Intersects(&half, &qtr) && !Intersects(&half, &away));
	EXPECT(!Intersects(&half, &line));		// a rectangle with no height meets nothing
	EXPECT(Overlaps(&half, &line));			// but Overlaps gives it a row first
	EXPECT(Overlaps(&line, &half) && !Overlaps(&line, &away));

	Eval("ctxDH:Close()");
	Refresh();
}


// TContainerView (views/ContainerView.h): a view whose selection is made of
// whole children rather than a range of anything.  Its hilite either says
// "all of me" - drawn as its bounds filled - or stands for the children that
// are hilited themselves, and the container then hands the drawing, the
// bounds and the removing on to them.
static void
TestContainerView()
{
	TView* c = ViewOf("ctxCV := AddView(GetRoot(), {viewClass: 78, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 90, bottom: 60}})");
	EXPECT(c->ClassID() == clContainerView && c->DerivedFrom(clContainerView) && c->DerivedFrom(clView));
	TContainerView* container = (TContainerView*) c;
	EXPECT(container->ClickOptions() == 1);
	EXPECT(container->GetHiliteView() == nil);	// nothing selected at all

	// two children of its own
	TView* a = ViewOf("ctxCVa := AddView(ctxCV, {viewClass: 74, viewFlags: 1, viewBounds: {left: 5, top: 5, right: 35, bottom: 25}})");
	TView* b = ViewOf("ctxCVb := AddView(ctxCV, {viewClass: 74, viewFlags: 1, viewBounds: {left: 45, top: 5, right: 75, bottom: 25}})");
	Refresh();

	// HiliteAll selects the whole container, in its own coordinates
	container->HiliteAll();
	RefVar first(container->FirstHilite());
	EXPECT(NOTNIL(first) && container->IsCompletelyHilited(first));
	TContainerHilite* hilite = (TContainerHilite*) RefToAddress(first);
	EXPECT(hilite->fComplete && hilite->fView == container);
	EXPECT(hilite->fBounds.left == 0 && hilite->fBounds.top == 0
		&& hilite->fBounds.right == 80 && hilite->fBounds.bottom == 50);
	EXPECT(container->GetHiliteView() == container);	// all of it: the container itself

	// and its bounds come back in the parent's coordinates
	Rect bounds;
	SetEmptyRect(&bounds);
	container->GlobalHiliteBounds(&bounds);
	EXPECT(bounds.left == 10 && bounds.top == 10 && bounds.right == 90 && bounds.bottom == 60);

	// (a complete hilite draws as the container's bounds filled - the
	// page's DrawHiliting draws it; not looked at here)
	container->RemoveAllHilites();
	EXPECT(!container->Hilited());

	// a hilite of one child takes that child's hilite bounds, and the
	// container then leaves the drawing and the bounds to the children
	a->HiliteAll();			// the child selects itself
	container->MakeHilite(1, a);
	first = container->FirstHilite();
	hilite = (TContainerHilite*) RefToAddress(first);
	EXPECT(!hilite->fComplete && !container->IsCompletelyHilited(first));
	EXPECT(hilite->fBounds.left == 5 && hilite->fBounds.top == 5
		&& hilite->fBounds.right == 35 && hilite->fBounds.bottom == 25);
	EXPECT(container->GetHiliteView() == a);	// the first hilited child

	SetEmptyRect(&bounds);
	container->GlobalHiliteBounds(&bounds);
	EXPECT(bounds.left == 15 && bounds.top == 15 && bounds.right == 45 && bounds.bottom == 35);

	// removing the container's hilite takes the children's with it
	EXPECT(a->Hilited() && !b->Hilited());
	container->RemoveAllHilites();
	EXPECT(!container->Hilited() && !a->Hilited());
	// (and with nothing hilited there are no offsets to answer)
	EXPECT(ISNIL(container->GetValue(RefVar(RSSYMhilites), RefVar(RSSYMoffset))));

	// a child grown wider pushes the children to the right of it that share
	// its rows along, keeping five pixels between (fGap), and the container
	// grows with them: a's right from 45 to 60 brings it within 5 of b's
	// left (55), so b moves 10 and the container widens by 10
	{
		Rect old = a->viewBounds;
		Rect wider = old;
		wider.right = (short) (wider.right + 15);
		a->viewBounds = wider;
		container->ChildBoundsChanged(a, old);
		EXPECT(b->viewBounds.left == 65 && b->viewBounds.right == 95);
		EXPECT(container->viewBounds.right == 100);
		// narrower, nothing moves
		Rect now = a->viewBounds;
		a->viewBounds = old;
		container->ChildBoundsChanged(a, now);
		EXPECT(b->viewBounds.left == 65 && container->viewBounds.right == 100);
	}

	// a child's data taken out (aeRemoveData) and put back by its undo
	// (aeAddData, the id kept)
	{
		long bId = b->fId;
		gApplication->DispatchCommand(RefVar(MakeCommand(aeRemoveData, container, bId)));
		EXPECT(container->fChildren->GetArraySize() == 1 && container->FindID(bId) == nil);
		gApplication->Undo();
		EXPECT(container->fChildren->GetArraySize() == 2 && container->FindID(bId) != nil);
	}

	Eval("ctxCV:Close()");
	Refresh();
}


// TEditView (views/EditView.h): an editor over its children.  It keeps no
// hilite of its own - every question about the selection is put to the
// children that are hilited themselves - and its answer to
// GlobalHiliteBounds is the click options they have in common.
static void
TestEditView()
{
	TView* e = ViewOf("ctxEV := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 110, bottom: 90}, viewLineSpacing: 22})");
	EXPECT(e->ClassID() == clEditView && e->DerivedFrom(clEditView) && e->DerivedFrom(clView));
	EXPECT(!e->DerivedFrom(clContainerView));	// the ROM derives it from TView
	TEditView* editor = (TEditView*) e;
	EXPECT(editor->fLineSpacing == 22);		// SetupDone read viewLineSpacing
	EXPECT(editor->fClickOptions == ~2);		// nothing resizable yet
	EXPECT(editor->fCaretRect.top == -32768);	// and no caret

	// two children, of which one gets selected
	TView* a = ViewOf("ctxEVa := AddView(ctxEV, {viewClass: 74, viewFlags: 1, viewBounds: {left: 5, top: 5, right: 45, bottom: 25}})");
	TView* b = ViewOf("ctxEVb := AddView(ctxEV, {viewClass: 74, viewFlags: 1, viewBounds: {left: 5, top: 35, right: 45, bottom: 55}})");
	Refresh();
	EXPECT(editor->CountHilites() == 0 && !editor->HasHilitedChildren(1, nil));

	a->HiliteAll();
	TView* found = nil;
	EXPECT(editor->CountHilites() == 1);
	EXPECT(editor->HasHilitedChildren(1, &found) && found == a);
	EXPECT(!editor->HasHilitedChildren(2, nil));

	// the bounds of the selection are the child's, in the editor's parent
	Rect bounds;
	long options = editor->GlobalHiliteBounds(&bounds);
	EXPECT(bounds.left == 15 && bounds.top == 15 && bounds.right == 55 && bounds.bottom == 35);
	EXPECT((options & 2) == 0);		// masked off by fClickOptions

	// GlobalSelectedBounds is the hilited children themselves
	b->HiliteAll();
	editor->GlobalSelectedBounds(&bounds);
	EXPECT(bounds.left == 15 && bounds.top == 15 && bounds.right == 55 && bounds.bottom == 65);
	EXPECT(editor->CountHilites() == 2);

	// a point on either child's selection is on the editor's
	Point on;  on.h = 20; on.v = 20;
	Point off; off.h = 90; off.v = 20;
	EXPECT(editor->PointInHilite(on) && !editor->PointInHilite(off));

	// HiliteAll selects every child; RemoveAllHilites clears them all
	editor->RemoveAllHilites();
	EXPECT(editor->CountHilites() == 0 && !a->Hilited() && !b->Hilited());
	options = editor->GlobalHiliteBounds(&bounds);
	EXPECT(options == 0 && bounds.top == -32768);	// nothing gathered

	editor->HiliteAll();
	EXPECT(editor->CountHilites() == 2 && a->Hilited() && b->Hilited());
	editor->RemoveAllHilites();

	// the caret rectangle, kept in the editor's own coordinates
	Rect caret;
	SetRect(&caret, 4, 6, 5, 20);
	editor->SetCaretRectLocal(caret);
	Point topLeft = editor->GetCaretLocalTopLeft();
	EXPECT(topLeft.h == 4 && topLeft.v == 6);
	EXPECT(editor->fCaretRect.bottom == 20);
	// given in the scrolled coordinates it comes back to the same place
	Point origin = editor->ContentsOrigin();
	Rect global = caret;
	OffsetRect(&global, origin.h, origin.v);
	editor->SetCaretRectGlobal(global);
	EXPECT(editor->fCaretRect.left == 4 && editor->fCaretRect.top == 6);
	Point back = editor->GetCaretGlobalTopLeft();
	EXPECT(back.h == global.left && back.v == global.top);

	// the selected children come back in reading order: down the page, and
	// within twelve pixels of the same top, left to right
	TView* p1 = ViewOf("ctxEVp1 := AddView(ctxEV, {viewClass: 81, viewFlags: 1, viewBounds: {left: 50, top: 60, right: 90, bottom: 75}, text: \"one\"})");
	TView* p2 = ViewOf("ctxEVp2 := AddView(ctxEV, {viewClass: 81, viewFlags: 1, viewBounds: {left: 5, top: 62, right: 45, bottom: 77}, text: \"two\"})");
	Refresh();
	a->HiliteAll();
	p1->HiliteAll();
	p2->HiliteAll();
	EXPECT(editor->CountHilites() == 3);
	{
		TView** sorted = editor->GetHilitedViewsSorted();
		EXPECT(sorted != nil);
		if (sorted != nil)
		{
			// a is at the top; p2 and p1 are within two pixels of each other,
			// so the left one comes first
			EXPECT(sorted[0] == a && sorted[1] == p2 && sorted[2] == p1);
			delete[] sorted;
		}
	}
	editor->RemoveAllHilites();
	EXPECT(editor->GetHilitedViewsSorted() == nil);

	// the up and down arrows leave one paragraph for the nearest next
	long top1 = p1->viewBounds.top;		// the bounds are global: the editor is at 10, 10
	long top2 = p2->viewBounds.top;
	EXPECT(top1 < top2);
	EXPECT(editor->MoveBetweenParagraphs(top1 - 1, 1) == p1);	// the nearest below
	EXPECT(editor->MoveBetweenParagraphs(top1, 1) == p2);
	EXPECT(editor->MoveBetweenParagraphs(top2, -1) == p1);	// the nearest above
	EXPECT(editor->MoveBetweenParagraphs(top2 + 1, -1) == p2);
	EXPECT(editor->MoveBetweenParagraphs(top1, -1) == nil);
	EXPECT(editor->MoveBetweenParagraphs(top2, 1) == nil);

	// a new paragraph is put on the ruled lines: its baseline onto the
	// nearest, with the text sitting three pixels above it
	{
		Rect box;
		SetRect(&box, 7, 30, 60, 48);
		editor->AlignToLineSpacing(&box, 30, 12);	// a baseline at 30 + 12
		// viewLineSpacing is 22, so the line is (30 + 44/3) / 22 = 2, the
		// baseline goes to 22 * 2 - 4 = 40 (four, the spacing being over 20)
		// and the box moves up by the two the baseline moved
		EXPECT(box.top == 28 && box.bottom == 46);
		EXPECT(box.left == 7 && box.right == 60);	// no square grid on this view
		// a clipboard is left where it is
		editor->fFlags |= vClipboard;
		SetRect(&box, 7, 30, 60, 48);
		editor->AlignToLineSpacing(&box, 30, 12);
		EXPECT(box.top == 30);
		editor->fFlags &= ~vClipboard;
	}

	// OffsetToCaret answers the caret rectangle where the view is scrolled to
	{
		Rect where;
		editor->OffsetToCaret(0, &where);
		Point origin2 = editor->ContentsOrigin();
		EXPECT(where.left == editor->fCaretRect.left + origin2.h);
		EXPECT(where.top == editor->fCaretRect.top + origin2.v);
		// and the marker when there is no caret
		editor->fCaretRect.top = -32768;
		editor->OffsetToCaret(0, &where);
		EXPECT(where.top == -32768);
	}

	Eval("ctxEV:Close()");
	Refresh();
}


static void
TestKeyboard()
{
	const ULong kUSABundle = 0x004a4d09;		// the ROM's locale bundle 'USA
	RefVar bundle(TranslateROMRef(kUSABundle));
	RefVar intl(AllocateFrame());
	RefVar keyboard(AllocateFrame());
	SetFrameSlot(keyboard, RSSYMmapping, RefVar(GetFrameSlotRef(bundle, RefVar(Intern((char*) "keycodeMapping")))));
	SetFrameSlot(intl, RSSYMkeyboard, keyboard);
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	EXPECT(NOTNIL(GetKeyTransMapping()));
	// the translation: the tables by the modifiers, the function keys
	ULong dead = 0;
	EXPECT(TranslateKey(0, true, 0, &dead) == 'a' && TranslateKey(0, true, kShiftModifier, &dead) == 'A' && TranslateKey(0, true, kCapsLockModifier, &dead) == 'A');
	EXPECT(TranslateKey(0x12, true, 0, &dead) == '1' && TranslateKey(0x12, true, kShiftModifier, &dead) == '!' && TranslateKey(0x12, true, kOptionModifier, &dead) == 0xa1);	// option-1: inverted ! (Mac Roman 0xc1)
	EXPECT(TranslateKey(0x7a, true, 0, &dead) == 0xf721 && TranslateKey(0x60, true, 0, &dead) == 0xf725 && dead == 0);
	// a dead key: option-e is the acute accent on the U.S. keyboard, then a
	// completed and an uncompleted character
	EXPECT(TranslateKey(0x0e, true, kOptionModifier, &dead) == 0xb4 && dead != 0);
	EXPECT(TranslateKey(0, true, 0, &dead) == 0xe1 && dead == 0);			// a acute
	TranslateKey(0x0e, true, kOptionModifier, &dead);
	EXPECT(TranslateKey(0x12, true, 0, &dead) == '1' && dead == 0);		// no completion for 1: as it is
	TranslateKey(0x0e, true, kOptionModifier, &dead);
	EXPECT(TranslateKey(0x0e, true, kOptionModifier, &dead) == 0xb4 && dead == 0);	// the accent itself again
	// the hard key map: modifiers and caps lock through KeyIn
	ClearHardKeymap();
	EXPECT(KeyIn(0, true, (TView*) -1) == 'a' && KeyDown(0, true) && !KeyDown(1, true));
	EXPECT(KeyIn(0, false, (TView*) -1) == 'a' && !KeyDown(0, true));
	EXPECT(KeyIn(kShiftKey, true, (TView*) -1) == 0 && Modifiers(true) == kShiftModifier && gTrueModifiers == 1);
	EXPECT(KeyIn(0, true, (TView*) -1) == 'A');
	KeyIn(0, false, (TView*) -1);
	EXPECT(KeyIn(kRightShiftKey, true, (TView*) -1) == 0 && gTrueModifiers == 3 && Modifiers(true) == kShiftModifier);
	EXPECT(KeyIn(kShiftKey, false, (TView*) -1) == 0 && Modifiers(true) == kShiftModifier);		// the right one still holds it
	EXPECT(KeyIn(kRightShiftKey, false, (TView*) -1) == 0 && Modifiers(true) == 0 && gTrueModifiers == 0);
	KeyIn(kCapsLockKey, true, (TView*) -1);
	EXPECT(gHardCapsLock && Modifiers(true) == kCapsLockModifier && KeyIn(0, true, (TView*) -1) == 'A');
	KeyIn(0, false, (TView*) -1);
	KeyIn(kCapsLockKey, false, (TView*) -1);
	KeyIn(kCapsLockKey, true, (TView*) -1);
	EXPECT(!gHardCapsLock && Modifiers(true) == 0);
	KeyIn(kCapsLockKey, false, (TView*) -1);
	// a dead key through KeyIn: nothing until it completes
	KeyIn(kOptionKey, true, (TView*) -1);
	EXPECT(KeyIn(0x0e, true, (TView*) -1) == 0 && gHardKeyDeadState != 0);
	KeyIn(0x0e, false, (TView*) -1);
	KeyIn(kOptionKey, false, (TView*) -1);
	EXPECT(KeyIn(0, true, (TView*) -1) == 0xe1 && gHardKeyDeadState == 0);
	KeyIn(0, false, (TView*) -1);
	EXPECT(IsCommandKeyCode(0x35) && IsCommandKeyCode(0x7a) && !IsCommandKeyCode(0x66) && !IsCommandKeyCode(0));
	EXPECT(IsCommandKeystroke('s', kCommandModifier << 25) && IsCommandKeystroke(0xf721, 0) && IsCommandKeystroke(0x1b, 0) && !IsCommandKeystroke('s', 0));
	EXPECT(KeyIsPrintable('a', gRootView) && !KeyIsPrintable(0x0d, gRootView) && !KeyIsPrintable(0xf721, gRootView) && !KeyIsPrintable('a', nil));
	// key events to a key view: the scripts' arguments
	TView* v = ViewOf("ctxK := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 30, bottom: 30}, viewFormat: 1, keys: [], viewKeyDownScript: func(char, key) begin AddArraySlot(keys, ['down, char, key]); nil end, viewKeyUpScript: func(char, key) begin AddArraySlot(keys, ['up, char, key]); nil end, viewKeyStringScript: func(str) begin AddArraySlot(keys, ['string, Clone(str)]); true end, viewKeyRepeatScript: func(char, key) begin AddArraySlot(keys, ['again, char, key]); if char = $x then true else nil end, _keyCommands: [{char: $s, modifiers: 1 << 25, keyMessage: 'DoSave}, {char: $r, modifiers: (1 << 25) + 4, keyMessage: 'DoRepeat}], saved: 0, DoSave: func(ctx) saved := saved + 1, DoRepeat: func(ctx) saved := saved + 10})");
	gRootView->fCaretView = v;
	EXPECT(GetPostingView(false) == v);
	gKeyboardConnected = false;
	KeyboardEvent down(aeKeyDown, 0);
	TTime never;
	never.time.hi = -1;
	never.time.lo = 0;
	gTickleTime = never;
	HandleKeyEvent(&down);
	EXPECT(gKeyboardConnected);		// told by the first key
	EXPECT(CompCompare(&gTickleTime.time, &never.time) != 0);		// a key is the user being active (the time now)
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 1 && EQRef(Eval("ctxK.keys[0][0]"), Intern((char*) "down")) && EQRef(Eval("ctxK.keys[0][1]"), MAKECHAR('a')) && RINT(Eval("ctxK.keys[0][2]")) == 'a');
	KeyboardEvent up(aeKeyUp, 0);
	HandleKeyEvent(&up);
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 2 && EQRef(Eval("ctxK.keys[1][0]"), Intern((char*) "up")));
	// shift held: the char is '!', the key argument the plain key's '1' with the key code and modifiers above
	KeyboardEvent shiftDown(aeKeyDown, kShiftKey);
	HandleKeyEvent(&shiftDown);
	KeyboardEvent oneDown(aeKeyDown, 0x12);
	HandleKeyEvent(&oneDown);
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 4 && EQRef(Eval("ctxK.keys[3][1]"), MAKECHAR('!')) && RINT(Eval("ctxK.keys[3][2]")) == (long) MakeKeyEventParameter(kShiftModifier, 0x12, '1'));
	KeyboardEvent oneUp(aeKeyUp, 0x12);
	HandleKeyEvent(&oneUp);
	KeyboardEvent shiftUp(aeKeyUp, kShiftKey);
	HandleKeyEvent(&shiftUp);
	EXPECT(Modifiers(true) == 0);
	// a repeat: the repeat script, and the down script when it is not there
	Eval("ctxK.keys := []");
	KeyboardEvent repeat(aeKeyRepeat, 0);
	HandleKeyEvent(&repeat);
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 1 && EQRef(Eval("ctxK.keys[0][0]"), Intern((char*) "again")));
	HandleKeyEvent(&up);
	// a key command: command-s runs DoSave through SendKeyMessage; command-r repeats
	Eval("ctxK.keys := []");
	KeyboardEvent cmdDown(aeKeyDown, kCommandKey);
	HandleKeyEvent(&cmdDown);
	EXPECT(IsCommandKeyDown() && Modifiers(true) == kCommandModifier);
	KeyboardEvent sDown(aeKeyDown, 1);
	HandleKeyEvent(&sDown);
	EXPECT(RINT(Eval("ctxK.saved")) == 1 && RINT(Eval("Length(ctxK.keys)")) == 2 && EQRef(Eval("ctxK.keys[1][1]"), MAKECHAR('s')));		// the command key's down and the s down ran the script first (answering nil)
	KeyboardEvent sRepeat(aeKeyRepeat, 1);
	HandleKeyEvent(&sRepeat);
	EXPECT(RINT(Eval("ctxK.saved")) == 1);		// not repeatable: taken, not sent
	KeyboardEvent sUp(aeKeyUp, 1);
	HandleKeyEvent(&sUp);
	KeyboardEvent rDown(aeKeyDown, 0x0f);
	HandleKeyEvent(&rDown);
	KeyboardEvent rRepeat(aeKeyRepeat, 0x0f);
	HandleKeyEvent(&rRepeat);
	EXPECT(RINT(Eval("ctxK.saved")) == 21);
	KeyboardEvent rUp(aeKeyUp, 0x0f);
	HandleKeyEvent(&rUp);
	KeyboardEvent cmdUp(aeKeyUp, kCommandKey);
	HandleKeyEvent(&cmdUp);
	EXPECT(!IsCommandKeyDown());
	// a command the view does not have goes up to the root
	Eval("GetRoot()._keyCommands := [{char: $q, modifiers: 1 << 25, keyMessage: 'DoQuit}]; GetRoot().quit := nil; GetRoot().DoQuit := func(ctx) quit := ctx");
	HandleKeyEvent(&cmdDown);
	KeyboardEvent qDown(aeKeyDown, 0x0c);
	HandleKeyEvent(&qDown);
	EXPECT(EQRef(Eval("GetRoot().quit"), v->fContext));
	KeyboardEvent qUp(aeKeyUp, 0x0c);
	HandleKeyEvent(&qUp);
	HandleKeyEvent(&cmdUp);
	Eval("RemoveSlot(GetRoot(), '_keyCommands)");
	// FindKeyCommand: the best partial match when there is no exact one
	RefVar found(FindKeyCommand(v, 's', (kCommandModifier | kShiftModifier) << 25));
	EXPECT(NOTNIL(found) && EQRef(GetFrameSlotRef(found, RSSYMkeymessage), Intern((char*) "DoSave")));
	EXPECT(ISNIL(FindKeyCommand(v, 's', 0)));
	// ... and the same from a script, with the commands gathered,
	// matched by their message, added to and blocked (views/Keyboard.cpp)
	EXPECT(NOTNIL(Eval("FindKeyCommand(ctxK, $s, 1 << 25).keyMessage")));
	EXPECT(EQRef(Eval("FindKeyCommand(ctxK, $s, 1 << 25).keyMessage"), Intern((char*) "DoSave")));
	EXPECT(ISNIL(Eval("FindKeyCommand(ctxK, $z, 1 << 25)")));
	// GatherKeyCommands walks outwards from the view, and a key already
	// spoken for nearer the caret is not gathered twice
	Eval("GetRoot()._keyCommands := [{char: $s, modifiers: 1 << 25, keyMessage: 'RootSave}, {char: $q, modifiers: 1 << 25, keyMessage: 'DoQuit}]");
	EXPECT(RINT(Eval("Length(GatherKeyCommands(ctxK))")) == 3);		// the view's two, and the root's $q
	EXPECT(EQRef(Eval("GatherKeyCommands(ctxK)[0].keyMessage"), Intern((char*) "DoSave")));
	EXPECT(EQRef(Eval("GatherKeyCommands(ctxK)[2].keyMessage"), Intern((char*) "DoQuit")));
	// MatchKeyMessage finds the command a message would come from
	EXPECT(EQRef(Eval("MatchKeyMessage(ctxK, 'DoQuit).char"), MAKECHAR('q')));
	EXPECT(ISNIL(Eval("MatchKeyMessage(ctxK, 'NoSuchMessage)")));
	// AddKeyCommands puts more on the view itself
	Eval("ctxK:AddKeyCommands([{char: $n, modifiers: 1 << 25, keyMessage: 'DoNew}])");
	EXPECT(RINT(Eval("Length(ctxK._keyCommands)")) == 3);
	EXPECT(EQRef(Eval("MatchKeyMessage(ctxK, 'DoNew).char"), MAKECHAR('n')));
	// BlockKeyCommand stops one the root would have answered: a command
	// of its own with no message, which the search finds first
	EXPECT(NOTNIL(Eval("FindKeyCommand(ctxK, $q, 1 << 25).keyMessage")));
	Eval("ctxK:BlockKeyCommand('DoQuit)");
	EXPECT(ISNIL(Eval("FindKeyCommand(ctxK, $q, 1 << 25).keyMessage")));
	Eval("RemoveSlot(GetRoot(), '_keyCommands)");
	Eval("ctxK._keyCommands := [{char: $s, modifiers: 1 << 25, keyMessage: 'DoSave}, {char: $r, modifiers: (1 << 25) + 4, keyMessage: 'DoRepeat}]");
	// CategorizeKeyCommands sorts a gathered array into the groups a
	// keyboard help slip shows: one frame per category, its commands
	// sorted by name, and the ones nobody named last
	Eval("ctxK._keyCommands := [{char: $s, modifiers: 1 << 25, keyMessage: 'DoSave, name: \"Save\", category: \"File\"}, "
		 "{char: $b, modifiers: 1 << 25, keyMessage: 'DoBold, name: \"Bold\", category: \"Style\"}, "
		 "{char: $a, modifiers: 1 << 25, keyMessage: 'DoAll, name: \"All\", category: \"File\"}, "
		 "{char: $z, modifiers: 1 << 25, keyMessage: 'DoOdd, name: \"Odd\"}]");
	Eval("cats := CategorizeKeyCommands(GatherKeyCommands(ctxK))");
	EXPECT(RINT(Eval("Length(cats)")) == 3);
	EXPECT(NOTNIL(Eval("StrEqual(cats[0].category, \"File\")")) && RINT(Eval("Length(cats[0].keyCommands)")) == 2);
	EXPECT(NOTNIL(Eval("StrEqual(cats[0].keyCommands[0].name, \"All\")")));		// sorted by name within the group
	EXPECT(NOTNIL(Eval("StrEqual(cats[1].category, \"Style\")")));
	EXPECT(NOTNIL(Eval("StrEqual(cats[2].keyCommands[0].name, \"Odd\")")));		// the unnamed category, moved to the end
	Eval("cats := nil");
	Eval("ctxK._keyCommands := [{char: $s, modifiers: 1 << 25, keyMessage: 'DoSave}, {char: $r, modifiers: (1 << 25) + 4, keyMessage: 'DoRepeat}]");
	// PostKeyString: printable text as one aeKeyString; other characters as key down/up
	Eval("ctxK.keys := []");
	PostKeyString(v, RefVar(MakeString("hi")));
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 1 && EQRef(Eval("ctxK.keys[0][0]"), Intern((char*) "string")) && NOTNIL(Eval("StrEqual(ctxK.keys[0][1], \"hi\")")));
	Eval("ctxK.keys := []");
	PostKeyString(v, RefVar(MakeString("a\tb")));
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 6);		// "a", tab down, tab up, "b", the terminator's down and up (as the ROM posts)
	EXPECT(NOTNIL(Eval("StrEqual(ctxK.keys[0][1], \"a\")")) && EQRef(Eval("ctxK.keys[1][0]"), Intern((char*) "down")) && EQRef(Eval("ctxK.keys[1][1]"), MAKECHAR(9)) && EQRef(Eval("ctxK.keys[2][0]"), Intern((char*) "up")) && NOTNIL(Eval("StrEqual(ctxK.keys[3][1], \"b\")")));
	// HandleKeyEvents: key events in an array, the characters gathered into a string
	Eval("ctxK.keys := []");
	RefVar events(Eval("[0x80 + 4, 4, 0x80 + 0x22, 0x22]"));		// h, i
	HandleKeyEvents(events, 4);
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 1 && NOTNIL(Eval("StrEqual(ctxK.keys[0][1], \"hi\")")));
	Eval("ctxK.keys := []");
	Eval("HandleKeyEvents([0x80 + 4, 4, 0x80 + 0x37, 0x80 + 1, 1, 0x37])");		// h, then command-s
	// "h", the command key down, s down and up, the command key up (the string posted is the gathering
	// buffer, reused for the rest as the ROM does - hence the script's Clone)
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 5 && NOTNIL(Eval("StrEqual(ctxK.keys[0][1], \"h\")")) && EQRef(Eval("ctxK.keys[2][1]"), MAKECHAR('s')) && RINT(Eval("ctxK.saved")) == 22);
	// the natives
	EXPECT(RINT(Eval("KeyIn(0, true)")) == 'a' && RINT(Eval("KeyIn(0, nil)")) == 'a' && NOTNIL(Eval("IsCommandKeystroke($s, 1 << 25)")) && RINT(Eval("GetTrueModifiers()")) == 0);
	EXPECT(RINT(Eval("TranslateKey(0, 2 << 25, 0)")) == 'A');
	Eval("ctxK.keys := []");
	Eval("PostKeyString(ctxK, \"yo\")");
	EXPECT(RINT(Eval("Length(ctxK.keys)")) == 1);
	gRootView->fCaretView = nil;
	EXPECT(GetPostingView(false) == gRootView);
	Eval("ctxK:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "keyboard closed"));
	ClearHardKeymap();
	gKeyboardConnected = false;
	Eval("RemoveSlot(vars, 'international)");
}


// The key view and the caret: a paragraph made the key view shows the
// caret below its baseline at the offset; HideCaret/ShowCaret, the caret
// moved, the selection stack, the natives, TBits.
static void
TestCaret()
{
	// TBits: the screen under a rectangle saved and put back
	Rect box;
	SetRect(&box, 10, 10, 22, 21);
	Eval("ctxB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 12, top: 12, right: 20, bottom: 18}, viewFormat: 0x151})");
	Eval("ctxB:Dirty()");
	Refresh();
	long inked = InkIn(10, 10, 22, 21);
	EXPECT(inked > 0);
	TBits bits;
	Rect dst;
	SetRect(&dst, 0, 0, 12, 11);		// the bits' own coordinates (as the caret's are made)
	EXPECT(bits.Constructor(dst) && bits.rowBytes == 4 && bits.bounds.right == 12);
	bits.CopyFromScreen(box, dst, 0, nil);
	EXPECT(GetPixel(&bits, 1, 1) == 1 && GetPixel(&bits, 5, 5) == 0);		// the frame, a pixel outside the bounds
	Rect all;
	SetRect(&all, 0, 0, kWidth, kHeight);
	EraseRect(&all);
	EXPECT(InkIn(10, 10, 22, 21) == 0);
	bits.Draw(dst, box, 0, nil);
	EXPECT(InkIn(10, 10, 22, 21) == inked);
	// in slow motion (ViewAutopsy) drawing into bits is drawn on the screen,
	// and copied into the bits when it is over
	{
		extern long gSlowMotion;
		EraseRect(&all);
		TBits slow;
		EXPECT(slow.Constructor(dst));
		gSlowMotion = 1;
		Point origin;
		origin.h = 0;
		origin.v = 0;
		slow.BeginDrawing(origin);
		Rect mark;
		SetRect(&mark, 2, 2, 6, 6);
		PaintRect(&mark);
		slow.EndDrawing();
		gSlowMotion = 0;
		EXPECT(InkIn(2, 2, 6, 6) == 16);
		EXPECT(GetPixel(&slow, 3, 3) == 1 && GetPixel(&slow, 8, 8) == 0);
		EraseRect(&all);
	}
	Eval("ctxB:Close()");
	Refresh();
	// a paragraph as the key view: no caret without a keyboard, then the caret at the offset
	TParagraphView* p = (TParagraphView*) ViewOf("ctxC2 := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 120, bottom: 40}, viewJustify: 0, viewFont: espy12, text: \"Hello World\"})");
	Eval("ctxC2:Dirty()");
	Refresh();
	EXPECT(gRootView->fCaretView == nil && !gRootView->fCaretShowing);
	// the caret's clip view: a paragraph on the root is its own window;
	// one in a page, the page (its enclosing edit view)
	EXPECT(GetCaretClipView(p) == p);
	{
		TView* page = ViewOf("ctxCP := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, viewBounds: {left: 150, top: 10, right: 230, bottom: 60}})");
		TView* inPage = ViewOf("AddView(ctxCP, {viewClass: 81, viewFlags: 1, viewBounds: {left: 5, top: 5, right: 60, bottom: 20}, viewFont: espy12, text: \"x\"})");
		EXPECT(page != nil && inPage != nil && GetCaretClipView(inPage) == page);
		Eval("RemoveView(GetRoot(), ctxCP)");
	}
	gKeyboardConnected = false;
	Eval("SetKeyView(ctxC2, 5)");
	EXPECT(gRootView->fCaretView == p && gRootView->fCaretOffset == 5 && p->fCaretOffset == 5 && !gRootView->CaretEnabled());
	Refresh();
	EXPECT(!gRootView->fCaretShowing && ISNIL(Eval("GetCaretBox()")));
	gKeyboardConnected = true;
	EXPECT(gRootView->CaretEnabled() && !gRootView->CaretValid(nil) && gRootView->NeedsUpdate());
	long textInk = InkIn(20, 10, 120, 22);
	Refresh();
	EXPECT(gRootView->fCaretShowing && gRootView->fCaretDrawnView == p && gRootView->CaretValid(nil));
	Rect caret;
	gRootView->GetCaretRect(&caret);
	Rect caretBox;
	p->OffsetToCaret(5, &caretBox);
	EXPECT(!EmptyRect(&caret) && caret.right - caret.left == 11 && caret.bottom - caret.top == 12 && caret.left == caretBox.left - 5 && caret.top == caretBox.bottom + 1);		// (the caret bitmaps' 11x12, a pixel under the baseline)
	EXPECT(caretBox.left > 40 && caretBox.left < 60 && caretBox.bottom > 18 && caretBox.bottom < 24);		// after "Hello" in espy 12, on the baseline
	EXPECT(InkIn(caret.left, caret.top, caret.right, caret.bottom) > 10);		// the caret's triangle
	EXPECT(InkIn(20, 10, 120, 22) == textInk);									// the text untouched
	RefVar caretFrame(Eval("GetCaretBox()"));
	EXPECT(NOTNIL(caretFrame) && RINT(GetFrameSlotRef(caretFrame, RSSYMleft)) == caret.left && RINT(GetFrameSlotRef(caretFrame, RSSYMoffset)) == 5 && EQRef(GetFrameSlotRef(caretFrame, RSSYMview), p->fContext));
	EXPECT(EQRef(Eval("GetKeyView()"), p->fContext) && RINT(Eval("GetCaretInfo().info.offset")) == 5 && EQRef(Eval("GetCaretInfo().view"), p->fContext));
	// hidden: the bits under it back; shown again by the next update
	long caretInk = InkIn(caret.left, caret.top, caret.right, caret.bottom);
	gRootView->HideCaret();
	EXPECT(!gRootView->fCaretShowing && InkIn(caret.left, caret.top, caret.right, caret.bottom) == 0 && gRootView->CaretValid(nil));
	Refresh();
	EXPECT(!gRootView->fCaretShowing);
	gRootView->ShowCaret();
	EXPECT(!gRootView->CaretValid(nil));
	Refresh();
	EXPECT(gRootView->fCaretShowing && InkIn(caret.left, caret.top, caret.right, caret.bottom) == caretInk);
	// moved to the start: the old place restored, the new one drawn
	Eval("SetKeyView(ctxC2, 0)");
	EXPECT(gRootView->fCaretOffset == 0 && !gRootView->CaretValid(nil));
	Refresh();
	Rect caret0;
	gRootView->GetCaretRect(&caret0);
	EXPECT(caret0.left == 20 - 1 - 5 + 0 || caret0.left == 20 - 5);		// the caret's left a pixel in from the text's left, kept inside the view
	EXPECT(caret0.left < caret.left && InkIn(caret.left + 6, caret.top, caret.right, caret.bottom) == 0);
	// a view hilited over the caret hides it meanwhile and shows it again:
	// the caret is drawn over the inverted view, not inverted with it
	{
		TView* over = ViewOf("ctxC4 := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 5, right: 60, bottom: 40}, viewFormat: 0})");
		Eval("ctxC4:Dirty()");
		Refresh();
		long hiddenBefore = gRootView->fCaretHidden;
		EXPECT(gRootView->fCaretShowing);
		over->Hilite(true);
		// (taken off - its bits put back - and not yet drawn again: that
		// is the next update's)
		EXPECT(gRootView->fCaretHidden == hiddenBefore && !gRootView->fCaretShowing);
		Refresh();
		Rect shown;
		gRootView->GetCaretRect(&shown);
		EXPECT(gRootView->fCaretShowing && shown.left == caret0.left);
		over->Hilite(false);
		EXPECT(gRootView->fCaretHidden == hiddenBefore && !gRootView->fCaretShowing);
		Refresh();
		EXPECT(gRootView->fCaretShowing);
		Eval("ctxC4:Close()");
		Refresh();
	}
	// a point to the caret: the character nearest a tap
	Point pt;
	pt.h = caretBox.left;
	pt.v = 16;
	Rect tapped;
	p->PointToCaret(pt, &tapped, nil);
	EXPECT(tapped.left == caretBox.left && p->PointToOffset(pt, 2, false, nil, nil, nil, nil) == 5);
	pt.h = 22;
	EXPECT(p->PointToOffset(pt, 2, false, nil, nil, nil, nil) == 0);
	pt.h = 119;
	EXPECT(p->PointToOffset(pt, 2, false, nil, nil, nil, nil) == 11);
	// the selection stack: the old key view pushed when another takes over, and restored
	TParagraphView* q = (TParagraphView*) ViewOf("ctxC3 := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 50, right: 120, bottom: 70}, viewJustify: 0, viewFont: espy12, text: \"Second\", activated: [], viewCaretActivateScript: func(on) AddArraySlot(activated, on)})");
	Eval("ctxC3:Dirty()");
	Eval("SetKeyView(ctxC3, 3)");
	EXPECT(gRootView->fCaretView == q && RINT(Eval("Length(GetSelectionStack())")) == 2 && EQRef(Eval("GetSelectionStack()[0]"), p->fContext) && RINT(Eval("GetSelectionStack()[1].offset")) == 0);
	EXPECT(RINT(Eval("Length(ctxC3.activated)")) == 1 && NOTNIL(Eval("ctxC3.activated[0]")));
	Refresh();
	gRootView->GetCaretRect(&caret);
	EXPECT(gRootView->fCaretShowing && caret.top > 50);
	Eval("SetKeyView(nil, nil)");
	EXPECT(gRootView->fCaretView == nil && RINT(Eval("Length(GetSelectionStack())")) == 4 && RINT(Eval("Length(ctxC3.activated)")) == 2 && ISNIL(Eval("ctxC3.activated[1]")));
	Refresh();
	EXPECT(!gRootView->fCaretShowing && InkIn(caret.left, caret.top, caret.right, caret.bottom) == 0);
	EXPECT(NOTNIL(Eval("RestoreKeyView(ctxC3)")) && gRootView->fCaretView == q && gRootView->fCaretOffset == 3 && RINT(Eval("Length(GetSelectionStack())")) == 4);		// (the ROM leaves the entry)
	EXPECT(NOTNIL(Eval("ViewContainsCaretView(ctxC3)")) && ISNIL(Eval("ViewContainsCaretView(ctxC2)")));
	// closing the key view forgets it (CaretViewGone: the stacked one comes back)
	Eval("ctxC3:Close()");
	Refresh();
	EXPECT(gRootView->fCaretView == p && gRootView->fCaretOffset == 0);
	Eval("SetKeyView(nil, nil)");
	Eval("ctxC2:Close()");
	Refresh();
	// a plain view as the key view has its caret nowhere (OffsetToCaret: top -32768), so none is
	// drawn - it was drawn at 0,0 when nowhere was an empty rect, and its saved bits put back there
	// later (stale pixels over the In/Out Box's clock)
	Eval("ctxPlain := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 30, top: 30, right: 90, bottom: 60}, viewJustify: 0})");
	Eval("SetKeyView(ctxPlain, 0)");
	Refresh();
	Point nowhere;
	EXPECT(gRootView->CaretValid(&nowhere) && nowhere.v == -32768 && !gRootView->fCaretShowing);
	EXPECT(InkIn(0, 0, 12, 12) == 0);
	Eval("SetKeyView(nil, nil)");
	Eval("ctxPlain:Close()");
	Refresh();
	gKeyboardConnected = false;
	Eval("SetLength(GetSelectionStack(), 0)");
	EXPECT(MapIs(ExpWhite, "caret closed"));
}


static void
TypeKey(ULong keyCode)
{
	KeyboardEvent down(aeKeyDown, keyCode);
	HandleKeyEvent(&down);
	KeyboardEvent up(aeKeyUp, keyCode);
	HandleKeyEvent(&up);
}


// Typing into a paragraph: keys to the key view insert at the caret
// (aeReplaceText through HandleReplaceText), backspace deletes, the undo
// entries merge, a key string is inserted, the style runs follow, the
// text is laid out again and the caret moves.
static void
TestTyping()
{
	const ULong kUSABundle = 0x004a4d09;
	RefVar bundle(TranslateROMRef(kUSABundle));
	RefVar intl(AllocateFrame());
	RefVar keyboard(AllocateFrame());
	SetFrameSlot(keyboard, RSSYMmapping, RefVar(GetFrameSlotRef(bundle, RefVar(Intern((char*) "keycodeMapping")))));
	SetFrameSlot(intl, RSSYMkeyboard, keyboard);
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, bundle);		// (the paragraph reads the locale's break tables)
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	ClearHardKeymap();
	TParagraphView* p = (TParagraphView*) ViewOf("ctxT := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 140, bottom: 40}, viewJustify: 0, viewFont: espy12, text: \"Hello World\", changes: [], viewChangedScript: func(slot, ctx) AddArraySlot(changes, slot)})");
	Eval("ctxT:Dirty()");
	Refresh();
	long helloWidth = p->Line(0).fBounds.right - p->Line(0).fBounds.left;
	gKeyboardConnected = true;
	Eval("ClearUndoStacks()");
	gApplication->Idle();
	Eval("SetKeyView(ctxT, 5)");
	Refresh();
	// x and y typed at the caret
	TypeKey(7);		// x
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"Hellox World\")")) && p->fCaretOffset == 6 && gRootView->fCaretOffset == 6);
	EXPECT(RINT(Eval("Length(ctxT.changes)")) == 1 && EQRef(Eval("ctxT.changes[0]"), RSSYMtext));
	EXPECT(ISNIL(Eval("ctxT.styles")));		// a single run of the view's font: no styles slot
	TypeKey(16);	// y (key 6 is z on the U.S. layout)
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"Helloxy World\")")) && p->fCaretOffset == 7);
	Refresh();
	EXPECT(p->Line(0).fBounds.right - p->Line(0).fBounds.left > helloWidth);
	Rect caret;
	gRootView->GetCaretRect(&caret);
	Rect at7;
	p->OffsetToCaret(7, &at7);
	EXPECT(gRootView->fCaretShowing && caret.left == at7.left - 5);
	// each key posts its inverse (remove 1 at the offset) - keys merge into one entry only in a
	// vCalculateBounds paragraph, whose commands carry its id (AddKeyToCurrUndo)
	EXPECT(Length(gApplication->GetUndoStack(0)) == 2);
	RefVar entry(GetArraySlotRef(gApplication->GetUndoStack(0), 1));
	EXPECT(CommandID(entry) == aeReplaceText && CommandIndexParameter(entry, 0) == 6 && CommandIndexParameter(entry, 1) == 1 && CommandIndexParameter(entry, 2) == 0 && CommandIndexParameter(entry, 6) == 1);
	// backspace takes the y back
	TypeKey(0x33);
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"Hellox World\")")) && p->fCaretOffset == 6 && Length(gApplication->GetUndoStack(0)) == 3);
	entry = GetArraySlotRef(gApplication->GetUndoStack(0), 2);
	EXPECT(CommandIndexParameter(entry, 0) == 6 && CommandIndexParameter(entry, 1) == 0 && CommandIndexParameter(entry, 2) == 1 && ((UniChar*) BinaryData(RefVar(CommandText(entry))))[0] == 'y');
	// undo puts the text back (the batch: all three) and the caret where the change was
	Eval("Undo()");
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"Hello World\")")) && p->fCaretOffset == 5);
	Refresh();
	EXPECT(p->Line(0).fBounds.right - p->Line(0).fBounds.left == helloWidth);
	// a key string goes in at the caret
	gApplication->Idle();
	PostKeyString(p, RefVar(MakeString("abc")));
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"Helloabc World\")")) && p->fCaretOffset == 8 && gRootView->fCaretOffset == 8);
	// styled: a paragraph with style runs keeps them around the insertion
	Eval("ctxT.text := \"ab\"; ctxT.styles := [1, espy12, 1, {family: 'geneva, face: 1, size: 12}]; ctxT:SyncView()");
	Eval("SetKeyView(ctxT, 1)");
	TypeKey(6);		// z
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"azb\")")) && RINT(Eval("Length(ctxT.styles)")) == 4 && RINT(Eval("ctxT.styles[0]")) == 2 && RINT(Eval("ctxT.styles[2]")) == 1);
	// backspace at the start does nothing; a control character is not typed
	Eval("SetKeyView(ctxT, 0)");
	TypeKey(0x33);
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"azb\")")) && p->fCaretOffset == 0);
	// the arrows move the caret
	TypeKey(0x7c);	// right arrow (0x1d)
	EXPECT(p->fCaretOffset == 1);
	TypeKey(0x7b);	// left arrow (0x1c)
	EXPECT(p->fCaretOffset == 0);
	// a read-only paragraph takes no keys
	p->fFlags |= vReadOnly;
	TypeKey(7);
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"azb\")")));
	p->fFlags &= ~vReadOnly;
	// a key over a selection: the selected text taken out first, then the
	// key typed where it was
	Eval("ctxT.text := \"Hello World\"; ctxT.styles := nil; ctxT:SyncView()");
	Eval("ctxT:Dirty()");
	Refresh();
	p->MakeHilite(0, 5, true);
	EXPECT(NOTNIL(p->FirstHilite()));
	TypeKey(7);		// x
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"x World\")")) && p->fCaretOffset == 1 && ISNIL(p->FirstHilite()));
	// a selection follows the text: an insertion before it moves it on,
	// a removal at its start shorter than it shortens it, one that reaches
	// into it otherwise takes it away
	{
		p->MakeHilite(2, 7, true);				// "World"
		{
			// the hilite carries a copy of the text it selected, and a clone copies it
			TParagraphHilite* made = (TParagraphHilite*) RefToAddress(RefVar(p->FirstHilite()));
			UniChar world[] = { 'W', 'o', 'r', 'l', 'd', 0 };
			EXPECT(Ustrcmp(made->fText, world) == 0);
			TParagraphHilite* clone = (TParagraphHilite*) made->Clone();
			EXPECT(Ustrcmp(clone->fText, world) == 0 && clone->fStart == 2 && clone->fEnd == 7);
			delete clone;
		}
		UniChar ab[2] = { 'a', 'b' };
		p->InsertStyledText(0, ab, 2, RefVar(NILREF), RefVar(NILREF), 0, 0, false);
		TParagraphHilite* moved = (TParagraphHilite*) RefToAddress(RefVar(p->FirstHilite()));
		EXPECT(moved->fStart == 4 && moved->fEnd == 9);
		p->InsertStyledText(4, nil, 0, RefVar(NILREF), RefVar(NILREF), 0, 2, false);
		moved = (TParagraphHilite*) RefToAddress(RefVar(p->FirstHilite()));
		EXPECT(moved->fStart == 4 && moved->fEnd == 7);
		p->InsertStyledText(3, nil, 0, RefVar(NILREF), RefVar(NILREF), 0, 2, false);
		EXPECT(ISNIL(p->FirstHilite()));
		Eval("ctxT.text := \"x World\"; ctxT:SyncView()");
	}
	// ... and an arrow collapses a selection to one of its ends (2..3)
	p->MakeHilite(2, 3, true);
	TypeKey(0x7b);	// left arrow: the selection's start
	EXPECT(p->fCaretOffset == 2 && ISNIL(p->FirstHilite()));
	p->MakeHilite(2, 3, true);
	TypeKey(0x7c);	// right arrow: its end
	EXPECT(p->fCaretOffset == 3 && ISNIL(p->FirstHilite()));
	// up and down a line, keeping the caret's place across it
	Eval("ctxT.text := \"one two three four five six\"; SetValue(ctxT, 'viewBounds, {left: 20, top: 10, right: 140, bottom: 90}); ctxT:SyncView()");
	Eval("ctxT:Dirty()");
	Refresh();
	EXPECT(p->LineCount() >= 2);
	Eval("SetKeyView(ctxT, 2)");
	TypeKey(0x7d);	// down arrow (0x1f)
	EXPECT(p->FindLineContainingCharOffset(p->fCaretOffset) == 1 && p->fCaretOffset > p->Line(1).fStart);
	TypeKey(0x7e);	// up arrow (0x1e)
	EXPECT(p->FindLineContainingCharOffset(p->fCaretOffset) == 0 && p->fCaretOffset == 2);
	// backspace over the last character of a paragraph that calculates its
	// bounds, on a page: the paragraph goes when the key comes up
	{
		TEditView* page = (TEditView*) ViewOf("ctxTE := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 160, bottom: 100}})");
		TParagraphView* last = (TParagraphView*) ViewOf("ctxTL := AddView(ctxTE, {viewClass: 81, viewFlags: 1 + 8, viewBounds: {left: 10, top: 10, right: 150, bottom: 10}, viewJustify: 0, viewFont: espy12, text: \"a\"})");
		EXPECT(page != nil && last != nil && page->fChildren->Count() == 1);
		Eval("SetKeyView(ctxTL, 1)");
		TypeKey(0x33);
		EXPECT(page->fChildren->Count() == 0 && !gRemoveEmptyParagraph);
		Eval("RemoveView(GetRoot(), ctxTE)");
	}
	// RemoveText widens to a neighbouring space; the style-run helpers
	Eval("ctxT.text := \"one two three\"; ctxT.styles := nil; ctxT:SyncView()");
	p->RemoveText(4, 3);
	EXPECT(NOTNIL(Eval("StrEqual(ctxT.text, \"one three\")")));
	RefVar runs(Eval("[3, 'a, 2, 'b, 4, 'c]"));
	long run, inRun;
	EXPECT(EQRef(GetStyleAtOffset(runs, 4, &run, &inRun), Intern((char*) "b")) && run == 1 && inRun == 1);
	EXPECT(EQRef(GetStyleAtOffset(runs, 20, &run, &inRun), Intern((char*) "c")) && run == 2);
	EXPECT(CountStylesForLength(runs, 0, 5) == 2 && CountStylesForLength(runs, 1, 1) == 1 && CountStylesForLength(runs, 0, 100) == 3);
	RefVar part(GetStylesOfRange(runs, 2, 4, false));
	EXPECT(Length(part) == 6 && RINT(GetArraySlotRef(part, 0)) == 1 && EQRef(GetArraySlotRef(part, 1), Intern((char*) "a")) && RINT(GetArraySlotRef(part, 2)) == 2 && EQRef(GetArraySlotRef(part, 3), Intern((char*) "b")) && RINT(GetArraySlotRef(part, 4)) == 1 && EQRef(GetArraySlotRef(part, 5), Intern((char*) "c")));
	SetStyleOfRange(runs, RefVar(Intern((char*) "d")), 1, 6);
	EXPECT(Length(runs) == 6 && RINT(GetArraySlotRef(runs, 0)) == 1 && RINT(GetArraySlotRef(runs, 2)) == 5 && EQRef(GetArraySlotRef(runs, 3), Intern((char*) "d")) && RINT(GetArraySlotRef(runs, 4)) == 3 && TotalRunLength(runs) == 9);
	SetStyleOfRange(runs, RefVar(Intern((char*) "d")), 0, 1);
	CompactStyleRuns(runs);
	EXPECT(Length(runs) == 4 && RINT(GetArraySlotRef(runs, 0)) == 6 && TotalRunLength(runs) == 9);
	Eval("SetKeyView(nil, nil)");
	Eval("ctxT:Close()");
	Refresh();
	gKeyboardConnected = false;
	Eval("SetLength(GetSelectionStack(), 0); RemoveSlot(vars, 'international); ClearUndoStacks()");
	EXPECT(MapIs(ExpWhite, "typing closed"));
}


static void
TestIdlers()
{
	// an idler: the viewIdleScript runs when the time comes, its answer re-times it, 0 stops it
	TView* v = ViewOf("ctxI := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 30, bottom: 30}, ticks: 0, viewIdleScript: func() begin ticks := ticks + 1; if ticks < 3 then 1 else 0 end})");
	EXPECT((v->fFlags & vHasIdlerHint) == 0);
	Eval("ctxI:SetupIdle(1)");
	EXPECT((v->fFlags & vHasIdlerHint) != 0 && gRootView->fIdlers->GetArraySize() == 1);
	// not due yet: nothing runs, the wait is what is left
	TTime next = gRootView->IdleViews();
	EXPECT(RINT(Eval("ctxI.ticks")) <= 1);
	// spin until it has run three times and stopped
	long spins = 0;
	while (gRootView->fIdlers->GetArraySize() > 0 && spins < 200000)
	{
		gRootView->IdleViews();
		spins++;
	}
	EXPECT(RINT(Eval("ctxI.ticks")) == 3 && gRootView->fIdlers->GetArraySize() == 0);
	next = gRootView->IdleViews();
	EXPECT(next.time.hi == 0 && next.time.lo == 0 && ISNIL(Eval("IdleViews()")));
	// SetupIdle(0) removes; a view going takes its idlers with it
	Eval("ctxI:SetupIdle(1000)");
	EXPECT(gRootView->fIdlers->GetArraySize() == 1 && RINT(Eval("IdleViews()")) > 0);
	Eval("ctxI:SetupIdle(0)");
	EXPECT(gRootView->fIdlers->GetArraySize() == 0 && gRootView->RemoveIdler(v, 0) == 0);
	Eval("ctxI:SetupIdle(1000)");
	Eval("ctxI:Close()");
	EXPECT(gRootView->fIdlers->GetArraySize() == 0);
	Refresh();
	EXPECT(MapIs(ExpWhite, "idlers closed"));
}


// Clicks: the pen's records go through the host tablet into the tablet
// buffer, the stroke queue makes strokes of them, the stroke world makes
// click units and the unit handler posts aeClick to the view under the
// pen (its viewClickScript, with the unit) at the pen-down and the click
// events (aeTap, aeDoubleTap: viewGestureScript) at the pen-up; a click
// script's TrackHilite follows a stroke fed a tick at a time.
// :LayoutTable(spec, column, row) lays a grid of cell templates out in
// the view - what the Dates application's month and week views are drawn
// from.  Three columns of 10, 20 and 10 across a view 100 wide, two rows
// of 12 down one 50 tall, indented one and two.
static void
TestLayoutTable()
{
	ViewOf("ctxT := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		   "viewBounds: {left: 10, top: 10, right: 110, bottom: 60}})");
	Eval("tabSpec := {tabAcross: 3, tabDown: 2, tabWidths: [10, 20, 10], tabHeights: 12, "
		 "indentx: 1, indenty: 2, tabProtos: [{viewClass: 74}], "
		 "tabValues: [1, 2, 3, 4, 5, 6], tabValueSlot: 'cellValue, "
		 "tabSetup: func(cell, col, row) cell.tag := col * 10 + row}");
	Eval("cells := ctxT:LayoutTable(tabSpec, 0, 0)");
	EXPECT(RINT(Eval("Length(cells)")) == 6);

	// the first row: the three widths from the indent, the height below it
	EXPECT(RINT(Eval("cells[0].viewBounds.left")) == 1 && RINT(Eval("cells[0].viewBounds.right")) == 11);
	EXPECT(RINT(Eval("cells[1].viewBounds.left")) == 11 && RINT(Eval("cells[1].viewBounds.right")) == 31);
	EXPECT(RINT(Eval("cells[2].viewBounds.left")) == 31 && RINT(Eval("cells[2].viewBounds.right")) == 41);
	EXPECT(RINT(Eval("cells[0].viewBounds.top")) == 2 && RINT(Eval("cells[0].viewBounds.bottom")) == 14);

	// the second row starts on the first one's last line, and its columns
	// start again from the indent
	EXPECT(RINT(Eval("cells[3].viewBounds.top")) == 13 && RINT(Eval("cells[3].viewBounds.bottom")) == 25);
	EXPECT(RINT(Eval("cells[3].viewBounds.left")) == 1 && RINT(Eval("cells[5].viewBounds.right")) == 41);

	// the values are walked in order, and tabSetup saw the column and row
	// counted from one
	EXPECT(RINT(Eval("cells[0].cellValue")) == 1 && RINT(Eval("cells[5].cellValue")) == 6);
	EXPECT(RINT(Eval("cells[0].tag")) == 11 && RINT(Eval("cells[2].tag")) == 31);
	EXPECT(RINT(Eval("cells[3].tag")) == 12 && RINT(Eval("cells[5].tag")) == 32);
	EXPECT(NOTNIL(Eval("cells[0]._proto")));

	// a table laid out from the middle enters the value array where that
	// cell is: row 1, column 0 is the fourth value
	Eval("cells := ctxT:LayoutTable(tabSpec, 0, 1)");
	EXPECT(RINT(Eval("Length(cells)")) == 3 && RINT(Eval("cells[0].cellValue")) == 4);

	// cells that would cross the view's edges are left out: four columns
	// of 40 do not fit across 100, and six rows of 12 do not fit down 50
	Eval("wide := {tabAcross: 4, tabDown: 1, tabWidths: 40, tabHeights: 12, tabProtos: {viewClass: 74}}");
	EXPECT(RINT(Eval("Length(ctxT:LayoutTable(wide, 0, 0))")) == 2);
	Eval("tall := {tabAcross: 1, tabDown: 6, tabWidths: 10, tabHeights: 12, tabProtos: {viewClass: 74}}");
	EXPECT(RINT(Eval("Length(ctxT:LayoutTable(tall, 0, 0))")) == 4);

	// a table of no size at all is nil
	EXPECT(ISNIL(Eval("ctxT:LayoutTable({tabAcross: 0, tabDown: 3}, 0, 0)")));
	EXPECT(ISNIL(Eval("ctxT:LayoutTable({tabAcross: 3, tabDown: 0}, 0, 0)")));
	Eval("ctxT:Close()");
	Refresh();
}


// PositionToTime(view, y) and TimeToPosition(view, minutes) map a day
// down a view's height - what the Dates day view is drawn on - snapping
// to the quarter hour as they go.  A view 240 tall is a minute every ten
// pixels less a sixth: 1440 minutes over 240 pixels is six a pixel.
static void
TestTimeDownAView()
{
	ViewOf("ctxD := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		   "viewBounds: {left: 0, top: 0, right: 100, bottom: 240}})");
	// the top is midnight, the bottom the end of the day
	EXPECT(RINT(Eval("PositionToTime(ctxD, 0)")) == 0);
	EXPECT(RINT(Eval("PositionToTime(ctxD, 240)")) == 1440);
	EXPECT(RINT(Eval("PositionToTime(ctxD, 120)")) == 720);		// noon, halfway down
	EXPECT(RINT(Eval("TimeToPosition(ctxD, 0)")) == 0);
	EXPECT(RINT(Eval("TimeToPosition(ctxD, 720)")) == 120);
	EXPECT(RINT(Eval("TimeToPosition(ctxD, 1440)")) == 240);

	// a time is snapped to the quarter hour first, so 9:00 and 9:07 are
	// the same place and 9:06 is the one before
	EXPECT(RINT(Eval("TimeToPosition(ctxD, 9 * 60)")) == 90);
	EXPECT(RINT(Eval("TimeToPosition(ctxD, 9 * 60 + 7)")) == RINT(Eval("TimeToPosition(ctxD, 9 * 60 + 15)")));
	EXPECT(RINT(Eval("TimeToPosition(ctxD, 9 * 60 + 6)")) == 90);

	// and a position answers a whole quarter hour, never anything between
	for (long y = 0; y <= 240; y += 7)
	{
		char source[64];
		snprintf(source, sizeof(source), "PositionToTime(ctxD, %ld)", y);
		EXPECT(RINT(Eval(source)) % 15 == 0);
	}
	Eval("ctxD:Close()");
	Refresh();
}


// GetHiliteOffsets() answers where the selection is, and nil when there
// is none - which is the truth until something is selected.
static void
TestHiliteOffsets()
{
	EXPECT(ISNIL(Eval("GetHiliteOffsets()")));
}


// GetFontSize and GetFontFamilySym read a font spec, packed or a frame.
static void
TestFontQueries()
{
	EXPECT(RINT(Eval("GetFontSize(0x3000)")) == 12);			// espy 12 bold, packed
	EXPECT(Eval("GetFontFamilySym(0x3000)") == Intern((char*) "espy"));
	EXPECT(RINT(Eval("GetFontSize({family: 'newYork, size: 18, face: 0})")) == 18);
	EXPECT(Eval("GetFontFamilySym({family: 'newYork, size: 18, face: 0})") == Intern((char*) "newYork"));
	// a frame with no size anywhere answers nil rather than nought
	EXPECT(ISNIL(Eval("GetFontSize({family: 'espy})")));
	EXPECT(ISNIL(Eval("GetFontFamilySym(\"not a font\")")));
}


// ExtractData(data, separator, maxLength) is the line the Notes overview
// shows for a note: its children's words run together, the text first and
// the drawings after, in the order they sit down the page.
static void
TestExtractData()
{
	Eval("noteData := ["
		 "{viewStationery: 'para, viewBounds: {top: 40, left: 0, bottom: 60, right: 100}, text: \"second\"}, "
		 "{viewStationery: 'para, viewBounds: {top: 10, left: 0, bottom: 30, right: 100}, text: \"first\"}, "
		 "{viewStationery: 'poly, viewBounds: {top: 70, left: 0, bottom: 90, right: 100}}]");
	// sorted down the page, and the shape after the words whatever its place
	EXPECT(Eval("StrEqual(ExtractData(noteData, \"; \", 50), \"first; second;  -shape- \")") == TRUEREF);
	// cut at the length asked for (the separator counts towards it)
	EXPECT(Eval("StrEqual(ExtractData(noteData, \"; \", 8), \"first; s; \")") == TRUEREF);
	// a sketch says so rather than saying nothing
	EXPECT(Eval("StrEqual(ExtractData([{viewStationery: 'poly, ink: MakeBinary(4, 'inkWord)}], \"; \", 50), \" -sketch- \")") == TRUEREF);
	// anything else with a stationery of its own says only "data"
	EXPECT(Eval("StrEqual(ExtractData([{viewStationery: 'weird}], \"; \", 50), \"data\")") == TRUEREF);
	// returns and tabs become spaces, so the one line stays one line
	EXPECT(Eval("StrEqual(ExtractData([{text: \"a\\nb\\tc\"}], \"; \", 50), \"a b c\")") == TRUEREF);
	// nothing at all is the empty string, not nil
	EXPECT(Eval("StrEqual(ExtractData(nil, \"; \", 50), \"\")") == TRUEREF);
	EXPECT(Eval("StrEqual(ExtractData([], \"; \", 50), \"\")") == TRUEREF);
}


// The Dates views' two drawing functions.  Both are global natives whose
// receiver is the view they are called from, so the view is given a
// method that calls them.
static void
TestDatesDrawing()
{
	// what the boot script makes, which the hour labels ask for the time
	// cycle (the tests before this one take it away again)
	const ULong kUSABundle = 0x004a4d09;		// the ROM's locale bundle 'USA
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(kUSABundle)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	EXPECT(InitInternationalUtils() == noErr);

	// a view 120 x 100 with two children: an hour grid every twenty pixels
	ViewOf("ctxG := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		   "viewBounds: {left: 0, top: 0, right: 120, bottom: 100}, "
		   "HourFont: 0x3000, "
		   "viewChildren: [{viewClass: 74, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 70, bottom: 100}}, "
		   "{viewClass: 74, viewFlags: 1, viewBounds: {left: 70, top: 0, right: 120, bottom: 100}}], "
		   "Grid: func() begin DrawMeetingGrid(20); true end})");
	Eval("ctxG:Grid()");
	// a rule at the top of each half hour, across the view from the
	// eighteen-pixel gutter, and nothing between them
	EXPECT(InkIn(18, 19, 120, 20) > 0);
	EXPECT(InkIn(18, 39, 120, 40) > 0);
	EXPECT(InkIn(18, 5, 60, 6) == 0);		// (past 60 is the second child's own rule)
	// the hours named in the gutter (6am and 7am here: the view's origin
	// is nought, so the first line is the first half hour of the day)
	EXPECT(InkIn(0, 0, 18, 100) > 0);
	// and the second child's left edge ruled down the view (the first
	// child's is not)
	EXPECT(InkIn(70, 0, 71, 100) > 50);
	Eval("ctxG:Close()");
	Refresh();

	// the date labels: two dates a day apart, centred under the columns
	ViewOf("ctxL := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		   "viewBounds: {left: 0, top: 0, right: 120, bottom: 40}, "
		   "labelFont: 0x3000, dayStrSpec: 80466, "
		   "Labels: func() begin DrawDateLabels({left: 0, top: 0, right: 120, bottom: 20}, "
		   "[Time(), Time() + 1440]); true end})");
	Eval("ctxL:Labels()");
	// drawn ten pixels below the bounds it was given
	EXPECT(InkIn(0, 20, 120, 40) > 0);
	EXPECT(InkIn(0, 0, 120, 18) == 0);
	// one date to a column: something in each half
	EXPECT(InkIn(0, 20, 60, 40) > 0 && InkIn(60, 20, 120, 40) > 0);
	// a single date draws nothing at all
	Eval("ctxL:Close()");
	Refresh();
	ViewOf("ctxL := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		   "viewBounds: {left: 0, top: 0, right: 120, bottom: 40}, "
		   "labelFont: 0x3000, dayStrSpec: 80466, "
		   "Labels: func() begin DrawDateLabels({left: 0, top: 0, right: 120, bottom: 20}, [Time()]); true end})");
	Eval("ctxL:Labels()");
	EXPECT(MapIs(ExpWhite, "one date draws nothing"));
	Eval("ctxL:Close()");
	Refresh();

	// ---- the calendar (TMonthView) ----
	// a month view 140 x 90 over September 2026, with the 10th selected
	// 2026-09-10 in minutes since 1904, which is what a date is here
	Eval("monthDates := [64530720]");
	TView* month = ViewOf("ctxM := AddView(GetRoot(), {viewClass: 80, viewFlags: 1 + 0x200, "
		   "viewBounds: {left: 0, top: 0, right: 140, bottom: 90}, "
		   "labelFont: 0x3000, datesFont: 0x3000, firstDayOfWeek: 0, "
		   "selectedDates: monthDates, singleDay: true, "
		   "monthChangedScript: func() changed := true})");
	Eval("ctxM:Dirty()");
	Refresh();
	TMonthView* calendar = (TMonthView*) month;
	EXPECT(calendar->ClassID() == clMonthView && calendar->DerivedFrom(clView));
	// the month it settled on is written back for the pickers around it
	EXPECT(RINT(Eval("ctxM.year")) == 2026 && RINT(Eval("ctxM.month")) == 9);
	// seven columns across the grid, and five rows: September 2026 starts
	// on a Tuesday and has thirty days, so it fits in five weeks
	EXPECT(calendar->fCellWidth == (calendar->fGridRect.right - calendar->fGridRect.left + 2) / 7);
	EXPECT(calendar->fCellHeight == (calendar->fGridRect.bottom - calendar->fGridRect.top) / 5);
	EXPECT(calendar->FirstColumn() == 2);		// the 1st is a Tuesday
	// the cell of a day, and the day of a point in it
	Rect cell;
	calendar->DateRect(cell, 10);
	EXPECT(cell.left == calendar->fGridRect.left + calendar->fCellWidth * 4);
	EXPECT(cell.top == calendar->fGridRect.top + calendar->fCellHeight + 1);
	Point middle;
	middle.h = (short) (cell.left + 1);
	middle.v = (short) (cell.top + 1);
	EXPECT(calendar->PointToDate(middle) == 10);
	// a point off the end of the grid comes back to the last cell
	Point outside;
	outside.h = 1000;
	outside.v = 1000;
	// (thirty days from the Tuesday column fit in five rows, so the last
	// row is row four)
	EXPECT(calendar->PointToDate(outside) == 4 * 7 + 6 - 2 + 1);
	// the selection is the tenth's cell, and there is no second rectangle
	EXPECT(EqualRect(&calendar->fRangeRect, &cell) && EmptyRect(&calendar->fRangeRect2));
	// something was drawn in the labels and in the grid
	EXPECT(InkIn(calendar->fLabelRect.left, calendar->fLabelRect.top, calendar->fLabelRect.right, calendar->fLabelRect.bottom) > 0);
	EXPECT(InkIn(calendar->fGridRect.left, calendar->fGridRect.top, calendar->fGridRect.right, calendar->fGridRect.bottom) > 0);
	// a range across a week's end takes two rectangles
	calendar->UpdateRangeRect(5, 9);
	EXPECT(!EmptyRect(&calendar->fRangeRect2));
	// and the days it covers are written back as selectedDates
	calendar->UpdateFrame(10, 10);
	EXPECT(RINT(Eval("Length(ctxM.selectedDates)")) == 1);
	EXPECT(RINT(Eval("ctxM.selectedDates[0]")) == 64530720);
	Eval("ctxM:Close()");
	Refresh();

	Eval("RemoveSlot(vars, 'international)");
}


// The caret gesture: a paragraph handed the corners of a caret drawn
// over it.  HandleCaret is what TEditView::HandleCaret reaches through
// the page; here it is called directly, so no pen is needed.
// The view's text, compared exactly - NewtonScript's StrEqual ignores
// case, which is the only thing the line gesture changes.
static Boolean
TextIs(TParagraphView* view, const char* expect)
{
	RefVar text(view->Text());
	if (ISNIL(text))
		return false;
	const UniChar* chars = (const UniChar*) BinaryData(text);
	for (long i = 0; ; i++)
	{
		if (chars[i] != (UniChar) (unsigned char) expect[i])
			return false;
		if (expect[i] == 0)
			return true;
	}
}

// A string object's characters, compared exactly.
static Boolean
TextIsRef(RefArg text, const char* expect)
{
	if (!IsString(text))
		return false;
	const UniChar* chars = (const UniChar*) BinaryData(text);
	for (long i = 0; ; i++)
	{
		if (chars[i] != (UniChar) (unsigned char) expect[i])
			return false;
		if (expect[i] == 0)
			return true;
	}
}

// The line gesture: a line drawn up through the selected text puts it in
// upper case, one drawn down in lower case, and a line over the first
// letter alone takes that letter only.
static void
TestLineGesture()
{
	TParagraphView* p = (TParagraphView*) ViewOf("ctxLg := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 200, bottom: 40}, viewJustify: 0, viewFont: espy12, text: \"one two three\"})");
	EXPECT(p != nil && p->TextLength() == 13);
	Refresh();

	// "two" selected, and the line's box around it
	Rect box;
	p->OffsetToBounds(4, &box);
	short wordLeft = box.left;
	p->OffsetToBounds(7, &box);
	short wordRight = box.left;
	short above = (short) (p->Line(0).fBounds.top - 2);
	short below = (short) (p->Line(0).fBounds.bottom + 2);
	p->MakeHilite(4, 7, false);
	Refresh();

	// what a script asks of the selection (TParagraphView::GetValue):
	// [[context, start, end]] as 'offset, the text as 'string, and the
	// text itself as viewValue
	{
		RefVar offsets(p->GetValue(RSSYMhilites, RSSYMoffset));
		EXPECT(IsArray(offsets) && Length(offsets) == 1);
		RefVar entry(GetArraySlot(offsets, 0));
		EXPECT(EQRef(GetArraySlot(entry, 0), p->fContext));
		EXPECT(RINT(GetArraySlot(entry, 1)) == 4 && RINT(GetArraySlot(entry, 2)) == 7);
		RefVar texts(p->GetValue(RSSYMhilites, RSSYMstring));
		EXPECT(IsArray(texts) && Length(texts) == 1 && TextIsRef(RefVar(GetArraySlot(texts, 0)), "two"));
		EXPECT(TextIsRef(RefVar(p->GetValue(RSSYMviewvalue, RefVar(NILREF))), "one two three"));
	}

	// a hilite keeps its area and its box in the view's own coordinates,
	// so GlobalHiliteBounds is what puts it back where the word is
	{
		TParagraphHilite* h = (TParagraphHilite*) RefToAddress(RefVar(p->FirstHilite()));
		EXPECT(h != nil && h->fBounds.top == 0);
		Rect global;
		SetEmptyRect(&global);
		p->GlobalHiliteBounds(&global);
		EXPECT(global.left == wordLeft);
		EXPECT(global.top == p->Line(0).fBounds.top);
	}

	// drawn upwards through the middle of the word: the word goes up
	Point from, to;
	from.h = (short) ((wordLeft + wordRight) / 2);	from.v = below;
	to.h = from.h;								to.v = above;
	EXPECT(p->HandleLineGesture(0, from, to) == 1);
	EXPECT(TextIs(p, "one TWO three"));

	// and drawn downwards through it again: back to lower case
	from.h = (short) ((wordLeft + wordRight) / 2);	from.v = above;
	to.h = from.h;								to.v = below;
	EXPECT(p->HandleLineGesture(180, from, to) == 1);
	EXPECT(TextIs(p, "one two three"));

	// over the first letter alone: only the letter goes up
	from.h = (short) (wordLeft + 1);	from.v = below;
	to.h = from.h;						to.v = above;
	EXPECT(p->HandleLineGesture(0, from, to) == 1);
	EXPECT(TextIs(p, "one Two three"));

	// a line that does not span the selection does nothing
	Eval("SetValue(ctxLg, 'text, \"one two three\")");
	Refresh();
	p->MakeHilite(4, 7, false);
	Refresh();
	from.h = (short) ((wordLeft + wordRight) / 2);	from.v = (short) (above + 6);
	to.h = from.h;								to.v = (short) (above + 8);
	EXPECT(p->HandleLineGesture(0, from, to) == 0);
	EXPECT(TextIs(p, "one two three"));

	// and neither does one over a paragraph with nothing selected
	p->RemoveAllHilites();
	Refresh();
	from.h = (short) ((wordLeft + wordRight) / 2);	from.v = below;
	to.h = from.h;								to.v = above;
	EXPECT(p->HandleLineGesture(0, from, to) == 0);

	Eval("RemoveView(GetRoot(), ctxLg)");
	Refresh();
}

static void
TestCaretGesture()
{
	TParagraphView* p = (TParagraphView*) ViewOf("ctxCg := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 200, bottom: 40}, viewJustify: 0, viewFont: espy12, text: \"onetwo\"})");
	EXPECT(p != nil && p->TextLength() == 6);
	Refresh();

	// where the third character starts, and the line's baseline
	Rect box;
	p->OffsetToBounds(3, &box);
	short at = box.left;
	short baseline = (short) (p->Line(0).fBounds.top + p->Line(0).fAscent);

	// a caret pointing up: its two arms below the baseline, its point above
	Point armA, point, armB, tail;
	armA.h = (short) (at - 8);	armA.v = (short) (baseline + 6);
	point.h = at;				point.v = (short) (baseline - 10);
	armB.h = (short) (at + 8);	armB.v = (short) (baseline + 6);
	tail.v = (short) 0x8000;	tail.h = 0;
	EXPECT(p->HandleCaret(2, 0, armA, point, armB, tail) == 1);
	EXPECT(NOTNIL(Eval("StrEqual(ctxCg.text, \"one two\")")));

	// the same caret drawn miles below the text belongs to no line
	Eval("SetValue(ctxCg, 'text, \"onetwo\")");
	Refresh();
	armA.v = (short) (baseline + 200);
	point.v = (short) (baseline + 184);
	armB.v = (short) (baseline + 200);
	EXPECT(p->HandleCaret(2, 0, armA, point, armB, tail) == 0);
	EXPECT(p->TextLength() == 6);

	// a caret with a tail as wide as three spaces puts three in
	armA.v = (short) (baseline + 6);
	point.v = (short) (baseline - 10);
	armB.v = (short) (baseline + 6);
	p->OffsetToBounds(4, &box);
	long character = box.left - at;			// one character of the text
	tail.v = (short) (baseline - 10);
	tail.h = (short) (at + character * 3);
	EXPECT(p->HandleCaret(3, 0, armA, point, armB, tail) == 1);
	// how many depends on how wide a space is in the font, which is
	// narrower than the character box the tail was measured against
	EXPECT(p->TextLength() > 7 && p->TextLength() < 30);

	// and it left an insert area over the spaces it put in: written into
	// (two letters in the middle of it) and then left alone - the pen up
	// a while, and a while since the writing - the paragraph's idler
	// brings each run of white space left in it down to one space
	{
		long spaces = p->TextLength() - 6;
		EXPECT(p->fInsertRunList != nil && p->fInsertRunList->GetArraySize() == 1);
		InsertRun* run = (InsertRun*) p->fInsertRunList->At(0);
		EXPECT(run != nil && run->fStart == 3 && (long) run->fLength == spaces && !run->fChanged);
		EXPECT(!p->fInsertAreasChanged);
		UniChar ab[] = { 'a', 'b' };
		p->InsertStyledText(5, ab, 2, RefVar(NILREF), RefVar(NILREF), 0, 0, false);
		EXPECT(run->fStart == 3 && (long) run->fLength == spaces + 2 && run->fChanged);
		EXPECT(p->fInsertAreasChanged);
		Eval("ClearUndoStacks()");
		ULong now = Ticks();
		p->fInsertAreasTime = now - 100;
		gStrokeWorld.fLastDownTime = now - 200;
		gStrokeWorld.fLastUpTime = now - 150;
		p->Idle(1);
		EXPECT(TextIs(p, "one ab two"));
		EXPECT(p->fInsertRunList->GetArraySize() == 0);
		EXPECT(!p->fInsertAreasChanged);
		// a run of returns and spaces the writer never touched is kept
		SaveInsertArea(p->fInsertRunList, 3, 1);
		p->fInsertAreasChanged = true;
		p->fInsertAreasTime = now - 100;
		EXPECT(p->Idle(1) == 1500);
		EXPECT(p->fInsertRunList->GetArraySize() == 1 && p->fInsertAreasChanged);
		// and one that starts inside another is not saved again
		SaveInsertArea(p->fInsertRunList, 3, 4);
		EXPECT(p->fInsertRunList->GetArraySize() == 1);
		delete (InsertRun*) p->fInsertRunList->At(0);
		p->fInsertRunList->RemoveElementsAt(0, 1);
		p->fInsertAreasChanged = false;

		// a deletion that swallows the whole of an area: the ROM leaves
		// its length "negative" (the unsigned check never empties it);
		// fixed, the area is left empty
		for (int fixed = 0; fixed < 2; fixed++)
		{
			SetRomBugFixed(fixed != 0);
			CList* areas = CList::Make();
			SaveInsertArea(areas, 5, 2);
			p->AdjustInsertAreasAfterDeletion(areas, 3, 6);
			InsertRun* swallowed = (InsertRun*) areas->At(0);
			EXPECT(swallowed->fStart == 3);
			EXPECT(fixed ? swallowed->fLength == 0 : swallowed->fLength > 0x7fffffff);
			delete swallowed;
			areas->RemoveElementsAt(0, 1);
			delete areas;
		}
		SetRomBugFixed(true);
		p->fInsertAreasChanged = false;
	}

	// a caret with neither a width worth a space nor a height worth a
	// line: the ROM inserts its unfilled buffer (the host's is empty);
	// fixed, nothing is inserted at all.  Either way the text is as it was
	for (int fixed = 0; fixed < 2; fixed++)
	{
		SetRomBugFixed(fixed != 0);
		Rect at1;
		p->OffsetToBounds(1, &at1);
		Point on;
		on.h = at1.left;
		on.v = (short) (at1.top + 2);
		long had = p->TextLength();
		EXPECT(p->InsertHorizontalSpace(on, 0, 0, false) == 1);
		EXPECT(p->TextLength() == had);
	}
	SetRomBugFixed(true);

	// the white space a block of insert area comes down to (FindPreviousWhiteSpaceBlock)
	{
		static const UniChar text[] = { 'a', ' ', 'b', ' ', ' ', 'c', 0x0d, 0x09, 'd', 0x0d, 'e', 0 };
		const UniChar* block;
		ULong length;
		// from the end back: the return before 'e' (not followed by a tab)
		EXPECT(FindPreviousWhiteSpaceBlock(text + 10, text, &block, &length) && block == text + 9 && length == 1);
		// from 'd' back: the return before the tab is passed over, the two spaces found
		EXPECT(FindPreviousWhiteSpaceBlock(text + 8, text, &block, &length) && block == text + 3 && length == 2);
		// from 'b' back: a single space is not worth taking out
		EXPECT(!FindPreviousWhiteSpaceBlock(text + 2, text, &block, &length));
		static const UniChar white[] = { ' ', 0x0d, ' ' };
		EXPECT(ContainsOnlyInsertedWhiteSpace(white, 3));
		EXPECT(!ContainsOnlyInsertedWhiteSpace(text, 2));
	}

	// a caret drawn upside down across the line closes up the space its
	// arms straddle (CheckAndDoJoin)
	Eval("SetValue(ctxCg, 'text, \"one two three\")");
	Refresh();
	p->OffsetToBounds(3, &box);
	armA.h = box.left;			armA.v = baseline;
	p->OffsetToBounds(4, &box);
	armB.h = box.left;			armB.v = baseline;
	point.h = (short) ((armA.h + armB.h) / 2);
	point.v = (short) (baseline + 8);
	tail.v = (short) 0x8000;	tail.h = 0;
	EXPECT(p->HandleCaret(2, 180, armA, point, armB, tail) == 1);
	EXPECT(NOTNIL(Eval("StrEqual(ctxCg.text, \"onetwo three\")")));

	// the same caret over the middle of a word has no space to close
	Eval("SetValue(ctxCg, 'text, \"one two three\")");
	Refresh();
	p->OffsetToBounds(9, &box);
	armA.h = box.left;
	p->OffsetToBounds(10, &box);
	armB.h = box.left;
	point.h = (short) ((armA.h + armB.h) / 2);
	EXPECT(p->HandleCaret(2, 180, armA, point, armB, tail) == 0);
	EXPECT(p->TextLength() == 13);


	// a caret pointing right, drawn in the left margin between two lines,
	// opens a line between them (InsertVerticalSpace)
	Eval("RemoveView(GetRoot(), ctxCg)");
	p = (TParagraphView*) ViewOf("ctxCg := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 200, bottom: 90}, viewJustify: 0, viewFont: espy12, text: \"one\\ntwo\"})");
	EXPECT(p != nil && p->TextLength() == 7);
	Refresh();
	EXPECT(p->LineCount() == 2);

	// the point just inside the second line's top, and in the left margin
	armA.h = (short) (p->viewBounds.left + 4);
	armB.h = armA.h;
	point.h = (short) (p->viewBounds.left + 4);
	point.v = (short) (p->Line(1).fBounds.top + 1);
	armA.v = (short) (point.v - 8);
	armB.v = (short) (point.v + 8);
	tail.v = (short) 0x8000;	tail.h = 0;
	EXPECT(p->HandleCaret(2, 90, armA, point, armB, tail) == 1);
	EXPECT(NOTNIL(Eval("StrEqual(ctxCg.text, \"one\\n\\ntwo\")")));

	// and the same caret drawn far below the text belongs to no line
	Eval("SetValue(ctxCg, 'text, \"one\\ntwo\")");
	Refresh();
	point.v = (short) (p->viewBounds.bottom + 100);
	armA.v = (short) (point.v - 8);
	armB.v = (short) (point.v + 8);
	EXPECT(p->HandleCaret(2, 90, armA, point, armB, tail) == 0);
	EXPECT(p->TextLength() == 7);

	Eval("RemoveView(GetRoot(), ctxCg)");
	Refresh();
}


// Scrubbing: a paragraph asked what a rectangle over its text would take
// out, and then to take it.  HandleScrub is what the gesture recogniser
// reaches through the page (TEditView::Scrub); here it is called
// directly, so no pen is needed.
// The empty line a final carriage return leaves behind: the caret goes
// on it, and it is inside the view, so the editor that scrolls until
// the caret is in sight (the ROM's checkCaret) is not left scrolling
// for ever.
static void
TestTrailingReturn()
{
	TParagraphView* p = (TParagraphView*) ViewOf("ctxCR := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 200, bottom: 90}, viewJustify: 0, viewFont: espy12, text: \"one\\ntwo\\n\"})");
	EXPECT(p != nil && p->TextLength() == 8);
	Refresh();

	// the three lines: "one", "two", and the empty one after the last return
	Rect first, second, third;
	// (OffsetToCaret reads the bottom it is handed - see there)
	SetRect(&first, 0, 0, 0, 0);
	SetRect(&second, 0, 0, 0, 0);
	SetRect(&third, 0, 0, 0, 0);
	p->OffsetToCaret(0, &first);
	p->OffsetToCaret(4, &second);
	p->OffsetToCaret(8, &third);
	EXPECT(second.top > first.top);
	EXPECT(third.top > second.top);
	EXPECT(third.top - second.top == second.top - first.top);
	// and the caret on it starts at the line's left edge
	EXPECT(third.left == first.left);

	// an offset at the end of a line is on that line, not at the start
	// of the next one: the line keeps the return that ends it
	Rect endOfFirst, endOfSecond;
	p->OffsetToCaret(3, &endOfFirst);
	p->OffsetToCaret(7, &endOfSecond);
	EXPECT(endOfFirst.top == first.top && endOfFirst.left > first.left);
	EXPECT(endOfSecond.top == second.top && endOfSecond.left > second.left);

	// with the caret there, the view does not think it is out of sight
	gRootView->SetKeyView(p, 8, 0, false);
	EXPECT(p->CaretRelativeToVisibleRect(p->viewBounds) == 1);

	Eval("RemoveView(GetRoot(), ctxCR)");
	Refresh();
}


static void
TestScrubbing()
{
	TParagraphView* p = (TParagraphView*) ViewOf("ctxSc := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 200, bottom: 40}, viewJustify: 0, viewFont: espy12, text: \"one two three\"})");
	EXPECT(p != nil && p->TextLength() == 13);
	Refresh();

	// where the words are: the box of each character's left edge
	Rect box;
	p->OffsetToBounds(4, &box);
	long twoLeft = box.left;
	p->OffsetToBounds(7, &box);
	long twoRight = box.left;
	EXPECT(twoRight > twoLeft);
	const Rect& line = p->viewBounds;

	// a rectangle over the middle word alone: asked without doing it, it
	// answers 2 (words) and the text is untouched
	Rect scrub;
	SetRect(&scrub, (short) (twoLeft - 1), (short) (line.top + 2), (short) (twoRight + 1), (short) (line.top + 18));
	EXPECT(p->HandleScrub(scrub, -1, nil, false) == 2);
	EXPECT(p->TextLength() == 13);
	// and then for real: "two " goes, the trailing space with it
	EXPECT(p->HandleScrub(scrub, -1, nil, true) == 2);
	EXPECT(NOTNIL(Eval("StrEqual(ctxSc.text, \"one three\")")));

	// a rectangle over one letter takes the letter
	p->OffsetToBounds(1, &box);
	long nLeft = box.left;
	// a narrow one, inside the letter, so no gesture is needed to justify it
	SetRect(&scrub, (short) (nLeft + 1), (short) (line.top + 2), (short) (nLeft + 4), (short) (line.top + 18));
	EXPECT(p->HandleScrub(scrub, -1, nil, true) == 2);
	EXPECT(NOTNIL(Eval("StrEqual(ctxSc.text, \"oe three\")")));

	// a rectangle over the whole paragraph empties it (the answer is 5:
	// nothing is left, which is what tells a page to take the view away)
	SetRect(&scrub, (short) (line.left - 2), (short) (line.top - 2), (short) (line.right + 2), (short) (line.bottom + 2));
	EXPECT(p->HandleScrub(scrub, -1, nil, true) == 5);
	EXPECT(p->TextLength() == 0);

	// a rectangle nowhere near it takes nothing
	p->SetValue(RefVar(RSSYMtext), RefVar(Eval("\"one two three\"")));
	Refresh();
	EXPECT(p->TextLength() == 13);
	SetRect(&scrub, 0, (short) (line.bottom + 20), 10, (short) (line.bottom + 30));
	EXPECT(p->HandleScrub(scrub, -1, nil, true) == 0);
	EXPECT(p->TextLength() == 13);

	// a scrub that goes over the selection takes the selection, whatever
	// else it covers.  The scrub arrives in the port's coordinates and a
	// hilite keeps its area in the view's own, so ScrubHilite moves it
	// there first - if the two ever disagree this finds nothing.
	p->MakeHilite(4, 7, false);
	Refresh();
	p->OffsetToBounds(5, &box);
	SetRect(&scrub, (short) (box.left), (short) (line.top + 2), (short) (box.left + 3), (short) (line.top + 18));
	EXPECT(p->ScrubHilite(scrub));
	EXPECT(p->TextLength() < 13);

	Eval("RemoveView(GetRoot(), ctxSc)");
	Refresh();
}


// The word info frame: what a piece of writing looks like to a script.
// A word unit with no reading at all comes out marked as ink and with
// its words taken away; one that was read carries the reading, and the
// strokes are there either way.
static void
TestWordInfo()
{
	gWordID = kWRecDomainType;
	TDomain* domain = TDomain::Make(gController, kWRecDomainType, (char*) "word");
	EXPECT(domain != nil);
	// a unit has no bounds without a recogniser for its type, and the
	// word info frame asks the recogniser what it makes of the unit
	// (InstallWRecRecognizer, which would put the real one here, is NOT
	// YET)
	TRecognizer* recognizer = new TRecognizer;
	recognizer->Init(domain, kWRecDomainType, aeWord, 0, 1);
	recognizer->InitServices(0, 0);
	gRecognition.fRecognizers->AddRecognizer(recognizer);

	TStroke* stroke = TStroke::Make(0);
	TabPt pt;
	pt.z = 3;
	pt.p = 0;
	pt.x = ToFixed(70);
	pt.y = ToFixed(50);
	stroke->AddPoint(&pt);
	pt.x = ToFixed(90);
	pt.y = ToFixed(70);
	stroke->AddPoint(&pt);
	stroke->fDownTime = 100;
	stroke->fUpTime = 110;
	stroke->EndStroke();
	TStrokeUnit* strokeUnit = TStrokeUnit::Make(gRootDomain, 1, stroke, nil);
	TWRecUnit* word = TWRecUnit::Make(domain, 1, nil);
	EXPECT(strokeUnit != nil && word != nil);
	if (strokeUnit == nil || word == nil)
		return;
	word->AddSub(strokeUnit);
	word->EndSubs();

	{
		// nothing read: the frame says ink and carries no words
				TUnitPublic pub(word, 0);
		RefVar info(pub.WordInfo());
				EXPECT(NOTNIL(info));
		EXPECT(RINT(RefVar(GetFrameSlot(info, RSSYMflags))) == kWordInfoIsInk);
		EXPECT(Length(RefVar(GetFrameSlot(info, RSSYMwords))) == 0);
		// the unit id is the type's own four bytes read as two Unicode
		// characters
		RefVar id(GetFrameSlot(info, RSSYMunitid));
		EXPECT(IsString(id) && Length(id) == 3 * (long) sizeof(UniChar));
		const UniChar* text = (const UniChar*) BinaryData(id);
		EXPECT(text[0] == (('W' << 8) | 'R') && text[1] == (('E' << 8) | 'C'));
		// and the strokes are a stroke bundle of what was written
		RefVar strokes(GetFrameSlot(info, RSSYMstrokes));
		EXPECT(NOTNIL(strokes) && CountStrokes(strokes) == 1);
		EXPECT(CountPoints(RefVar(GetStroke(strokes, 0))) == 2);
		RefVar bounds(GetFrameSlot(strokes, RSSYMbounds));
		Rect unitBounds;
		pub.Bounds(&unitBounds);
		EXPECT(RINT(RefVar(GetFrameSlot(bounds, RSSYMleft))) == unitBounds.left);
		EXPECT(RINT(RefVar(GetFrameSlot(bounds, RSSYMtop))) == unitBounds.top);
		EXPECT(unitBounds.left == 70 && unitBounds.top == 50);
		// Strokes() is the same slot
		EXPECT(EQ(pub.Strokes(), strokes));
		// asked again it is the same frame, not another one
		EXPECT(EQ(pub.WordInfo(), info));
	}

	{
		// read as a word: the reading is in the frame and the ink flag
		// is not
		UniChar hello[6] = { 'h', 'e', 'l', 'l', 'o', 0 };
		EXPECT(word->AddWordInterpretation() == 0);
		EXPECT(word->SetWordString(0, hello) != nil);
		word->SetLabel(0, kWordLabelWord);
		word->SetScore(0, 55);

		TUnitPublic pub(word, 0);
		RefVar info(pub.WordInfo());
		EXPECT(RINT(RefVar(GetFrameSlot(info, RSSYMflags))) == 0);
		RefVar words(GetFrameSlot(info, RSSYMwords));
		EXPECT(Length(words) == 1);
		RefVar first(GetArraySlot(words, 0));
		RefVar read(GetFrameSlot(first, RSSYMword));
		EXPECT(Ustrcmp((const UniChar*) BinaryData(read), hello) == 0);
		EXPECT(RINT(RefVar(GetFrameSlot(first, RSSYMscore))) == 55);
		EXPECT(RINT(RefVar(GetFrameSlot(first, RSSYMindex))) == 0);
		EXPECT(RINT(RefVar(GetFrameSlot(first, RSSYMlabel))) == kWordLabelWord);
		// the list is handed over, so asking twice makes a second one
		TWordList* list = pub.Words();
		EXPECT(list != nil && list->Count() == 1);
		delete list;
	}

	// the base line: with nothing measured it is the bottom of the box,
	// from its left edge to its right
	{
		TUnitPublic pub(word, 0);
		pub.SetWordBase();
		FRect box;
		word->GetBBox(&box);
		EXPECT(pub.fWordBase.left == (short) RoundFixed(box.left));
		EXPECT(pub.fWordBase.right == (short) RoundFixed(box.right));
		EXPECT(pub.fWordBase.top == (short) RoundFixed(box.bottom));
		EXPECT(pub.fWordBase.bottom == (short) RoundFixed(box.bottom));
	}

	word->Dispose();
	domain->Dispose();
	gWordID = 0;
}


static void
TestClicks()
{
	// the locale the recognition system asks which language it is
	// reading, which the boot has set long before any of this (an
	// earlier test may have taken it away again)
	{
		RefVar intl(AllocateFrame());
		SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(AllocateFrame()));
		SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	}
	// A machine at this level builds no dictionaries, but the ROM reads
	// the dictionary preferences and expands words at every level (the
	// bug written down in ReadDictPrefs), so they are built here.
	InitDictionaries();
	gRecognition.Init(1);
	gStrokeWorld.Init();
	HostTabletInit();
	RegisterUnitNatives();
	// (the tablet's calibration natives ask the inker world, which needs
	// the OS: ctest host.NewtonAlignPen, recognition/Inker.h)
	Eval("userConfiguration.userPenSize := 1");		// (the ink is let out by the pen size)
	TView* v = ViewOf("ctxC := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200 + 0x800, viewBounds: {left: 60, top: 40, right: 120, bottom: 80}, viewFormat: 1, clicks: [], gestures: [], "
		"viewClickScript: func(unit) begin AddArraySlot(clicks, [GetPoint(0, unit), GetPoint(1, unit), StrokeDone(unit), StrokeBounds(unit), GetUnitDownTime(unit), CountUnitStrokes(unit)]); nil end, "
		"viewGestureScript: func(unit, kind) begin AddArraySlot(gestures, [kind, GetPoint(4, unit), GetPoint(5, unit), GetUnitUpTime(unit), GetPoint(8, unit)]); true end})");
	Eval("ctxC:Dirty()");
	Refresh();
	// a tap: the click at the pen-down (the stroke already done here), and - the click script not having taken it - the tap at the pen-up
	HostTabletPenDown(90, 60, 1000);
	HostTabletPenUp(1004);
	gRecognition.Idle();
	EXPECT(RINT(Eval("Length(ctxC.clicks)")) == 1 && RINT(Eval("ctxC.clicks[0][0]")) == 90 && RINT(Eval("ctxC.clicks[0][1]")) == 60 && NOTNIL(Eval("ctxC.clicks[0][2]")));
	EXPECT(RINT(Eval("ctxC.clicks[0][3].left")) == 90 && RINT(Eval("ctxC.clicks[0][3].bottom")) == 61 && RINT(Eval("ctxC.clicks[0][4]")) == 1000 && RINT(Eval("ctxC.clicks[0][5]")) == 1);
	EXPECT(RINT(Eval("Length(ctxC.gestures)")) == 1 && RINT(Eval("ctxC.gestures[0][0]")) == aeTap && RINT(Eval("ctxC.gestures[0][1]")) == 90 && RINT(Eval("ctxC.gestures[0][3]")) == 1004 && RINT(Eval("ctxC.gestures[0][4].y")) == 60);
	EXPECT(gRecognition.fClickView == v && gStrokeWorld.CurrentStroke() == nil);
	// a tap where no view takes clicks: nothing at all.  The unit gets no
	// area (GetAreasHit answers an input mask of zero), so the controller
	// claims it and it never reaches a recogniser - which also means the
	// click view the tap before left is not touched.
	HostTabletPenDown(10, 90, 2000);
	HostTabletPenUp(2003);
	gRecognition.Idle();
	EXPECT(RINT(Eval("Length(ctxC.clicks)")) == 1 && RINT(Eval("Length(ctxC.gestures)")) == 1 && gRecognition.fClickView == v);
	// a press and drag, fed a record a tick as the click script's TrackHilite waits: hilited inside, not outside, ended inside; the click taken (true), so no gesture follows
	Eval("ctxC.viewClickScript := func(unit) begin AddArraySlot(clicks, [StrokeDone(unit), :TrackHilite(unit), GetPointsArrayXY(unit), StrokeDone(unit)]); true end");
	HostTabletQueuePenDown(90, 60, 3000);
	HostTabletQueuePenMove(95, 62);
	HostTabletQueuePenMove(100, 65);
	HostTabletQueuePenMove(150, 65);
	HostTabletQueuePenMove(110, 65);
	HostTabletQueuePenUp(3040);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(RINT(Eval("Length(ctxC.clicks)")) == 2 && ISNIL(Eval("ctxC.clicks[1][0]")) && NOTNIL(Eval("ctxC.clicks[1][1]")) && NOTNIL(Eval("ctxC.clicks[1][3]")));
	EXPECT(RINT(Eval("Length(ctxC.clicks[1][2])")) == 10 && RINT(Eval("ctxC.clicks[1][2][6]")) == 150 && RINT(Eval("ctxC.clicks[1][2][9]")) == 65);
	EXPECT(RINT(Eval("Length(ctxC.gestures)")) == 1 && (v->fFlags & vSelected) != 0 && HostTabletQueued() == 0 && gStrokeWorld.CurrentStroke() == nil);
	// AlignBounds: a new paragraph lined up with the children it is nearly
	// aligned with already.  An edit view with two paragraphs on it: one at
	// the top left, one below it and slightly indented.
	{
		TEditView* editor = (TEditView*) ViewOf(
			"ctxE := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, "
			"viewBounds: {left: 20, top: 20, right: 300, bottom: 400}, viewChildren: ["

			"{viewClass: 81, viewFlags: 1, viewBounds: {left: 40, top: 40, right: 140, bottom: 60}, text: \"one\"}, "

			"{viewClass: 81, viewFlags: 1, viewBounds: {left: 44, top: 100, right: 144, bottom: 120}, text: \"two\"}]})");
		EXPECT(editor != nil && editor->ClassID() == clEditView);
		// the children are laid out inside the editor, so they sit at
		// 60,60 - 160,80 and 64,120 - 164,140
		Rect result;
		Rect want, measured;
		// written just inside the first child's box: its left edge lines up
		// with the child's, and vertically it is already centred on the child,
		// so the centre-to-centre alignment wins with nothing to do
		want.left = 63; want.top = 62; want.right = 110; want.bottom = 78;
		measured = want;
		editor->AlignBounds(want, measured, &result);
		EXPECT(result.left == 60 && result.top == 62);
		// written just above the first child and overlapping it across: the
		// new paragraph's bottom goes to the child's top, which is the
		// edge-to-the-opposite-edge alignment tucking it onto the line above
		want.left = 43; want.top = 44; want.right = 90; want.bottom = 58;
		measured = want;
		editor->AlignBounds(want, measured, &result);
		EXPECT(result.bottom == 60 && result.top == 46);
		// far from anything: left where it was
		want.left = 200; want.top = 300; want.right = 260; want.bottom = 318;
		measured = want;
		editor->AlignBounds(want, measured, &result);
		EXPECT(result.left == 200 && result.top == 300);
		// off the top left of the page: pulled back inside it
		want.left = 0; want.top = 0; want.right = 60; want.bottom = 18;
		measured = want;
		editor->AlignBounds(want, measured, &result);
		EXPECT(result.left >= 20 && result.top >= 20);
		Eval("ctxE:Close()");
	}

	// RangeDistance: 0 when one range holds the other, 1 when they only
	// overlap, the gap when they are apart
	EXPECT(RangeDistance(10, 20, 12, 18) == 0);		// b inside a
	EXPECT(RangeDistance(12, 18, 10, 20) == 0);		// a inside b
	EXPECT(RangeDistance(10, 20, 10, 20) == 0);		// the same range
	EXPECT(RangeDistance(10, 20, 15, 25) == 1);		// b starts inside a
	EXPECT(RangeDistance(15, 25, 10, 20) == 1);		// a starts inside b
	EXPECT(RangeDistance(10, 20, 20, 30) == 1);		// touching counts as overlapping
	EXPECT(RangeDistance(10, 20, 26, 30) == 6);		// b is six past a
	EXPECT(RangeDistance(26, 30, 10, 20) == 6);		// and the other way round

	// FlushStrokes: the strokes waiting in the queue thrown away.  The pen
	// is still down when the flush starts, and the wait the loop takes
	// between turns is what lets the rest of the stroke through - here the
	// host's wait hook feeds a queued record a tick, where the machine has
	// the inker task.  Without that wait the loop would never see the pen
	// go up, and would hold the processor for good.
	HostTabletPenDown(90, 60, 6000);		// straight in, so there is a stroke to flush
	StrokeTime();
	HostTabletQueuePenMove(95, 62);			// the rest a record a tick, as the flush waits
	HostTabletQueuePenMove(100, 65);
	HostTabletQueuePenUp(6030);
	EXPECT(NOTNIL(Eval("FlushStrokes()")));
	EXPECT(HostTabletQueued() == 0 && gStrokeWorld.CurrentStroke() == nil);
	EXPECT(ISNIL(Eval("FlushStrokes()")));	// and nothing left to throw away
	Eval("ctxC:Hilite(nil)");
	// two taps close together: a tap, then a double tap (both on the same view; the clicks not taken)
	Eval("ctxC.viewClickScript := func(unit) begin AddArraySlot(clicks, GetPoint(6, unit)); nil end");
	HostTabletPenDown(90, 60, 5000);
	HostTabletPenUp(5003);
	gRecognition.Idle();
	HostTabletPenDown(91, 61, 5010);
	HostTabletPenUp(5013);
	gRecognition.Idle();
	EXPECT(RINT(Eval("Length(ctxC.gestures)")) == 3 && RINT(Eval("ctxC.gestures[1][0]")) == aeTap && RINT(Eval("ctxC.gestures[2][0]")) == aeDoubleTap);
	EXPECT(RINT(Eval("Length(ctxC.clicks)")) == 4 && RINT(Eval("ctxC.clicks[3].x")) == 91);
	Eval("ctxC:Hilite(nil)");
	Eval("RemoveView(GetRoot(), ctxC)");
	Refresh();
	// a slider dragged: the value follows the pen along the bar (the aeClick tracked by TGaugeView::TrackSetValue), the viewFinalChangeScript run once at the end with [old, new]
	TView* slider = ViewOf("ctxS := AddView(GetRoot(), {viewClass: 92, viewFlags: 1 + 0x200, viewBounds: {left: 20, top: 50, right: 120, bottom: 60}, viewValue: 30, changes: [], "
		"viewFinalChangeScript: func(old, new) begin AddArraySlot(changes, [old, new]); nil end})");
	Eval("ctxS:Dirty()");
	Refresh();
	HostTabletQueuePenDown(50, 55, 6000);		// (50 - 20 + 0) * 100 / 100 = 30: unchanged
	HostTabletQueuePenMove(70, 55);				// 50
	HostTabletQueuePenMove(70, 55);
	HostTabletQueuePenMove(200, 55);			// past the end: 100
	HostTabletQueuePenMove(90, 55);				// 70
	HostTabletQueuePenUp(6040);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(RINT(Eval("ctxS.viewValue")) == 70 && RINT(Eval("Length(ctxS.changes)")) == 1 && RINT(Eval("ctxS.changes[0][0]")) == 30 && RINT(Eval("ctxS.changes[0][1]")) == 70);
	EXPECT(HostTabletQueued() == 0 && gStrokeWorld.CurrentStroke() == nil && slider->fFlags & vVisible);
	// a read-only gauge ignores the pen
	Eval("SetValue(ctxS, 'viewFlags, 3 + 0x200)");
	HostTabletPenDown(40, 55, 7000);
	HostTabletPenUp(7003);
	gRecognition.Idle();
	EXPECT(RINT(Eval("ctxS.viewValue")) == 70 && RINT(Eval("Length(ctxS.changes)")) == 1);
	Eval("RemoveView(GetRoot(), ctxS)");
	Refresh();
	// a picker tracked with the pen: the item under the pen inverted as it moves, the one it ends on picked (pickActionScript, the autoclose picker closed); a pen ending outside picks nothing (-1) and closes it
	Eval("picked := nil");
	TPickView* p = (TPickView*) ViewOf("ctxK := AddView(GetRoot(), {_proto: protoPicker, pickItems: [\"Alpha\", \"Beta\", 'pickSeparator, \"Gamma\"], bounds: {left: 30, top: 20, right: 80, bottom: 35}, pickActionScript: func(index) picked := index, pickCancelledScript: func() picked := 'cancelled})");
	Eval("ctxK:Open()");
	Refresh();
	long top = p->viewBounds.top, left = p->viewBounds.left;
	HostTabletQueuePenDown(left + 10, top + 5, 8000);		// on Alpha
	HostTabletQueuePenMove(left + 10, top + 5);
	HostTabletQueuePenMove(left + 10, top + 20);			// Beta
	HostTabletQueuePenMove(left + 10, top + 40);			// Gamma
	HostTabletQueuePenUp(8040);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(RINT(Eval("picked")) == 3 && gRootView->fChildren->Count() == 0 && HostTabletQueued() == 0);
	Refresh();
	EXPECT(MapIs(ExpWhite, "picker picked and closed"));
	p = (TPickView*) ViewOf("ctxK := AddView(GetRoot(), {_proto: protoPicker, pickItems: [\"Alpha\", \"Beta\"], bounds: {left: 30, top: 20, right: 80, bottom: 35}, pickActionScript: func(index) picked := index, pickCancelledScript: func() cancelled := true})");
	Eval("picked := 0; cancelled := nil");
	Eval("ctxK:Open()");
	Refresh();
	top = p->viewBounds.top, left = p->viewBounds.left;
	HostTabletQueuePenDown(left + 10, top + 5, 9000);
	HostTabletQueuePenMove(left + 10, top + 60);			// off the picker
	HostTabletQueuePenUp(9020);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(NOTNIL(Eval("cancelled")) && RINT(Eval("picked")) == 0 && gRootView->fChildren->Count() == 0);		// (the cancel script only: PickItem of no item returns at once, 0x187a84)
	Refresh();
	EXPECT(MapIs(ExpWhite, "picker cancelled and closed"));

	// a tap on a paragraph reaches it as aeTap and, after the double-tap
	// interval (the idler), places the caret at the tapped character
	gKeyboardConnected = true;
	TParagraphView* tp = (TParagraphView*) ViewOf("ctxTP := AddView(GetRoot(), {viewClass: 81, viewFlags: 1 + 0x200 + 0x800 + 0x1000, viewBounds: {left: 20, top: 40, right: 160, bottom: 60}, viewJustify: 0, viewFont: espy12, text: \"Hello World\"})");
	Eval("ctxTP:Dirty()");
	Refresh();
	Rect box4;
	tp->OffsetToBounds(4, &box4);
	long tpy = (tp->Line(0).fBounds.top + tp->Line(0).fBounds.bottom) / 2;
	HostAdvanceClock(60 * 60 * 0xf000);		// so the tap is well after any prior click
	HostTabletPenDown(box4.left + 1, tpy, 0);
	HostTabletPenUp(0);
	gRecognition.Idle();
	EXPECT(tp->fTapped && gRootView->fCaretView != tp);		// deferred, not yet placed
	HostAdvanceClock(kSeconds);								// past the double-tap interval
	gRootView->IdleViews();
	EXPECT(!tp->fTapped && gRootView->fCaretView == tp && tp->fCaretOffset == 4);
	// a tap on the caret opens the key view's _caretPopup (DoCaretClick ->
	// DoPopupMenu), a picker over the popup's items
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "canonicalPopup")), RefVar(Rcanonicalpopup));
	Eval("caretPopped := nil");
	Eval("ctxTP._caretPopup := {popup: [\"Undo\", \"Copy\"], pickActionScript: func(i) caretPopped := i}");
	Eval("SetKeyView(ctxTP, 2)");
	Refresh();
	EXPECT(gRootView->fCaretShowing);
	Rect caretRect;
	gRootView->GetCaretRect(&caretRect);
	long caretChildren = gRootView->fChildren->Count();
	HostAdvanceClock(kSeconds);
	HostTabletPenDown((caretRect.left + caretRect.right) / 2, (caretRect.top + caretRect.bottom) / 2, 0);
	HostTabletPenUp(0);
	gRecognition.Idle();
	EXPECT(gRootView->fChildren->Count() == caretChildren + 1);		// the popup opened
	TView* caretPop = gRootView->fChildren->Last();
	EXPECT(caretPop->ClassID() == clPickView);
	{
		RefVar cmd(MakeCommand(aePickItem, caretPop, 0));		// pick "Undo"
		gApplication->DispatchCommand(cmd);
	}
	EXPECT(RINT(Eval("caretPopped")) == 0 && gRootView->fChildren->Count() == caretChildren);
	gKeyboardConnected = false;
	Eval("SetKeyView(nil, nil); RemoveView(GetRoot(), ctxTP)");
	Refresh();
	EXPECT(MapIs(ExpWhite, "paragraph tap in recognition closed"));

	// a tap on a day of a calendar (TMonthView::HandleClick) leaves the
	// port's regions as it found them: the visible region SetupVisRgn
	// narrowed is put back into the visRgn ([port,#0x24] at 0x001209d4),
	// and the clip is not touched
	{
		TMonthView* cal = (TMonthView*) ViewOf("ctxM2 := AddView(GetRoot(), {viewClass: 80, viewFlags: 1 + 0x200, "
			"viewBounds: {left: 0, top: 0, right: 140, bottom: 90}, labelFont: 0x3000, datesFont: 0x3000, "
			"firstDayOfWeek: 0, selectedDates: [64530720], singleDay: true})");
		Eval("ctxM2:Dirty()");
		Refresh();
		GrafPort* port;
		GetPort(&port);
		TRegionVar visBefore, clipBefore;
		CopyRgn(port->visRgn, visBefore);
		CopyRgn(port->clipRgn, clipBefore);
		Rect day12;
		cal->DateRect(day12, 12);
		HostAdvanceClock(kSeconds);
		HostTabletPenDown((day12.left + day12.right) / 2, (day12.top + day12.bottom) / 2, 0);
		HostTabletPenUp(0);
		gRecognition.Idle();
		GetPort(&port);
		EXPECT(RINT(Eval("ctxM2.selectedDates[0]")) != 64530720);		// the 12th was picked
		EXPECT(EqualRgn(port->visRgn, visBefore));
		EXPECT(EqualRgn(port->clipRgn, clipBefore));
		Eval("RemoveView(GetRoot(), ctxM2)");
		Refresh();
	}

	// drag and drop: a press-drag from a source view carries a 'text item
	// to a target with a viewDropScript that accepts 'text
	Eval("dropped := nil");
	// (the drag keeps the pen inside the application area, or the screen)
	Eval("vars.displayParams := {appAreaGlobalLeft: 0, appAreaGlobalTop: 0, appAreaWidth: 160, appAreaHeight: 100}");
	screenWidth = kWidth;
	screenHeight = kHeight;
	TView* src = ViewOf("ctxDS := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 20, top: 40, right: 60, bottom: 70}, viewFormat: 1, "
		"viewClickScript: func(unit) begin :DragAndDrop(unit, :GlobalBox(), nil, nil, [{types: ['text], dragRef: {text: \"hi there\"}}]); true end, viewGetDropDataScript: func(dropType, dragRef) dragRef})");
	Eval("ctxDT := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 100, top: 40, right: 150, bottom: 70}, viewFormat: 1, "
		"viewGetDropTypesScript: func(pt) ['text], "
		"viewDropScript: func(dropType, dropData, pt) begin dropped := dropData; true end})");
	Eval("ctxDS:Dirty(); ctxDT:Dirty()");
	Refresh();
	(void) src;
	// press on the source, drag to the target, release
	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(40, 55, 0);
	HostTabletQueuePenMove(70, 55);
	HostTabletQueuePenMove(100, 55);
	HostTabletQueuePenMove(125, 55);		// over the target
	// (held there a moment: the drag looks for its target only every
	//  three ticks while the pen moves)
	HostTabletQueuePenMove(126, 56);
	HostTabletQueuePenMove(125, 55);
	HostTabletQueuePenUp(0);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(NOTNIL(Eval("StrEqual(dropped.text, \"hi there\")")));		// the target's drop script got the data
	Eval("RemoveView(GetRoot(), ctxDS); RemoveView(GetRoot(), ctxDT)");
	Refresh();
	EXPECT(MapIs(ExpWhite, "drag and drop closed"));

	// the same through :DragAndDropLtd, which takes the three rectangles
	// the plain one does not: the drag kept inside limitBounds, the view
	// free to go anywhere ('none pinBounds)
	Eval("dropped := nil");
	ViewOf("ctxDS := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 20, top: 40, right: 60, bottom: 70}, viewFormat: 1, "
		"viewClickScript: func(unit) begin :DragAndDropLtd(unit, :GlobalBox(), "
		"{pinBounds: 'none, limitBounds: {left: 0, top: 0, right: 160, bottom: 120}}, nil, "
		"[{types: ['text], dragRef: {text: \"and again\"}}]); true end, viewGetDropDataScript: func(dropType, dragRef) dragRef})");
	Eval("ctxDT := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 100, top: 40, right: 150, bottom: 70}, viewFormat: 1, "
		"viewGetDropTypesScript: func(pt) ['text], "
		"viewDropScript: func(dropType, dropData, pt) begin dropped := dropData; true end})");
	Eval("ctxDS:Dirty(); ctxDT:Dirty()");
	Refresh();
	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(40, 55, 0);
	HostTabletQueuePenMove(70, 55);
	HostTabletQueuePenMove(100, 55);
	HostTabletQueuePenMove(125, 55);
	HostTabletQueuePenMove(126, 56);
	HostTabletQueuePenMove(125, 55);
	HostTabletQueuePenUp(0);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(NOTNIL(Eval("StrEqual(dropped.text, \"and again\")")));
	// and the limits in their other shapes: a plain rectangle, 'none, nil
	Eval("dropped := nil");
	Eval("ctxDS.viewClickScript := func(unit) begin :DragAndDropLtd(unit, :GlobalBox(), "
		 "{left: 0, top: 0, right: 160, bottom: 120}, nil, [{types: ['text], dragRef: {text: \"a rectangle\"}}]); true end");
	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(40, 55, 0);
	HostTabletQueuePenMove(100, 55);
	HostTabletQueuePenMove(125, 55);
	HostTabletQueuePenMove(126, 56);
	HostTabletQueuePenMove(125, 55);
	HostTabletQueuePenUp(0);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(NOTNIL(Eval("StrEqual(dropped.text, \"a rectangle\")")));
	Eval("dropped := nil");
	Eval("ctxDS.viewClickScript := func(unit) begin :DragAndDropLtd(unit, :GlobalBox(), 'none, nil, "
		 "[{types: ['text], dragRef: {text: \"anywhere\"}}]); true end");
	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(40, 55, 0);
	HostTabletQueuePenMove(100, 55);
	HostTabletQueuePenMove(125, 55);
	HostTabletQueuePenMove(126, 56);
	HostTabletQueuePenMove(125, 55);
	HostTabletQueuePenUp(0);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(NOTNIL(Eval("StrEqual(dropped.text, \"anywhere\")")));
	Eval("RemoveView(GetRoot(), ctxDS); RemoveView(GetRoot(), ctxDT)");
	Refresh();
	EXPECT(MapIs(ExpWhite, "the limited drag and drop closed"));
}


// TGrayShrink (qd/GrayShrink.h): one bit shrunk into four as grays - each
// 2x2 cell of the source counted, and the count (0-4) looked up in the gray
// table MakeGrayTable makes for a cell of four with no grayLevels
// preference: 0, 7, 8, 11, 15.  A solid block is all 15, a checkerboard
// (two in four set) 8, one pixel in four 7.  (Here, where the object system
// is, because the table asks the preference.)
static void
TestGrayShrink()
{
	static unsigned char srcBits[32 * 4];
	static unsigned char dstBits[16 * 8];
	PixelMap src, dst;
	src.baseAddr = (Ptr) srcBits;
	src.rowBytes = 4;
	SetRect(&src.bounds, 0, 0, 32, 32);
	src.pixMapFlags = kPixMapPtr | 0x1000000 | 1;
	src.deviceRes.v = src.deviceRes.h = kDefaultDPI;
	src.grayTable = nil;
	dst.baseAddr = (Ptr) dstBits;
	dst.rowBytes = 8;
	SetRect(&dst.bounds, 0, 0, 16, 16);
	dst.pixMapFlags = kPixMapPtr | 4;
	dst.deviceRes.v = dst.deviceRes.h = kDefaultDPI;
	dst.grayTable = nil;
	RgnHandle all = NewRgn();
	RectRgn(all, &dst.bounds);
	TGrayShrink shrink;
	Rect from = src.bounds, to = dst.bounds;
	struct { const char* what; long every; long gray; } cases[] = {
		{ "solid", 0, 15 }, { "checkerboard", 1, 8 }, { "one in four", 2, 7 } };
	for (long c = 0; c < 3; c++)
	{
		memset(srcBits, 0, sizeof(srcBits));
		for (long y = 0; y < 32; y++)
			for (long x = 0; x < 32; x++)
			{
				Boolean on = cases[c].every == 0 || (cases[c].every == 1 ? ((x + y) & 1) == 0 : (x % 2 == 0 && y % 2 == 0));
				if (on)
					srcBits[y * 4 + (x >> 3)] |= (unsigned char) (0x80 >> (x & 7));
			}
		memset(dstBits, 0, sizeof(dstBits));
		shrink.GrayShrink(&src, &dst, &from, &to, all, all, all);
		Boolean ok = true;
		for (long y = 0; y < 16; y++)
			for (long x = 0; x < 16; x++)
				if (GetPixel(&dst, x, y) != cases[c].gray)
					ok = false;
		if (!ok)
			printf("gray shrink %s: pixel (0,0) is %ld\n", cases[c].what, (long) GetPixel(&dst, 0, 0));
		EXPECT(ok);
	}
	DisposeRgn(all);
}


// A paragraph that calculates its bounds, emptied by SetValue, asks the
// page it is in to remove it (aeRemoveData) rather than being laid out
// again - unless the page's text flags have 0x80, or it does not
// calculate its bounds.
static void
TestEmptiedParagraph()
{
	Eval("emPage := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 200, bottom: 200}})");
	Eval("emA := AddView(emPage, {viewClass: 81, viewFlags: 1 + 8, viewBounds: {left: 20, top: 20, right: 150, bottom: 40}, viewFont: espy12, text: \"gone\"})");
	Eval("emB := AddView(emPage, {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 50, right: 150, bottom: 70}, viewFont: espy12, text: \"stays\"})");
	TView* page = ViewOf("emPage");
	EXPECT(page != nil && page->fChildren->Count() == 2);
	Eval("SetValue(emB, 'text, \"\")");
	EXPECT(page->fChildren->Count() == 2);			// (not vCalculateBounds: laid out, empty)
	Eval("SetValue(emA, 'text, \"\")");
	EXPECT(page->fChildren->Count() == 1);			// taken away
	Eval("RemoveView(GetRoot(), emPage)");
	// a passthrough keyboard gone takes the caret from a page that takes
	// neither text nor ink words (no keyboard up), but not from a paragraph
	TView* bare = ViewOf("emBare := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 100, bottom: 100}})");
	gRootView->fCaretView = bare;
	gRootView->ConnectPassthruKeyboard(false);
	EXPECT(gRootView->fCaretView == nil);
	Eval("RemoveView(GetRoot(), emBare)");
}


// The views a script names the front key view by (GetView's
// 'viewFrontCommandKey and 'viewFrontKey), with no caret: the front-most
// visible child of the root that takes command keys (textFlags 0x4000),
// and for the key view one that also takes keys (0x8000).
static void
TestFrontKeyViews()
{
	TView* savedCaret = gRootView->fCaretView;
	gRootView->fCaretView = nil;
	TView* a = ViewOf("ctxFA := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, textFlags: 0x4000, viewBounds: {left: 10, top: 10, right: 40, bottom: 30}})");
	TView* b = ViewOf("ctxFB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 50, top: 10, right: 80, bottom: 30}})");
	EXPECT(a != nil && b != nil);
	EXPECT(GetFrontCommandKeyView() == a);								// b is in front, but takes no command keys
	EXPECT(GetView(RefVar(), RSSYMviewfrontcommandkey) == a);
	EXPECT(GetView(RefVar(), RSSYMviewfrontkey) != a);					// a takes no keys (0x8000)
	Eval("ctxFA.textFlags := 0xC000");
	EXPECT(GetView(RefVar(), RSSYMviewfrontkey) == a);
	Eval("ctxFA:Hide()");
	EXPECT(GetFrontCommandKeyView() == nil && GetView(RefVar(), RSSYMviewfrontcommandkey) == gRootView);	// hidden: none
	Eval("RemoveView(GetRoot(), ctxFA); RemoveView(GetRoot(), ctxFB)");
	gRootView->fCaretView = savedCaret;
	Refresh();
}


static void
TestPickView()
{
	// a picker from the ROM's protoPicker: three text items and a separator, popped below a button's bounds
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "protoPicker")), RefVar(Rprotopicker));
	Eval("picked := nil");
	TPickView* p = (TPickView*) ViewOf("ctxK := AddView(GetRoot(), {_proto: protoPicker, pickItems: [\"Alpha\", \"Beta\", 'pickSeparator, \"Gamma\"], bounds: {left: 30, top: 20, right: 80, bottom: 35}, pickActionScript: func(index) picked := index})");
	EXPECT(p != nil && p->ClassID() == clPickView && p->DerivedFrom(clView));
	EXPECT(p->fItemCount == 4 && p->fTextItemHeight == 13 && p->fAutoClose && !p->fHasMarks);
	EXPECT(p->ItemTop(0) == 0 && p->ItemBottom(0) == 13 && p->ItemBottom(1) == 26 && p->ItemBottom(2) == 32 && p->ItemBottom(3) == 45);
	EXPECT(p->IsItemNoPickable(0) && !p->IsItemNoPickable(2) && p->GetItemLength(0) == 5 && p->GetItemLength(3) == 5);	// (the ROM's IsItemNoPickable answers the pickable bit)
	EXPECT(p->fTextLeft == 4 && p->fMarkLeft == 4 && p->fRightMargin == 5);
	// placed below the bounds, at its left; the height the items', the width the widest plus the margins
	EXPECT(p->viewBounds.top == 35 && p->viewBounds.left == 30 && p->viewBounds.bottom == 80);
	long widest = 0;
	for (long i = 0; i < 4; i++)
	{
		RefVar text(p->GetItemNoText(i));
		if (NOTNIL(text))
		{
			long width = RINT(Eval(i == 0 ? "StrFontWidth(\"Alpha\", protoPicker.viewFont)" : i == 1 ? "StrFontWidth(\"Beta\", protoPicker.viewFont)" : "StrFontWidth(\"Gamma\", protoPicker.viewFont)"));
			if (width > widest)
				widest = width;
		}
	}
	EXPECT(p->viewBounds.right == 30 + 4 + widest + 5);
	EXPECT(ISNIL(p->GetItemNoText(2)) && IsString(p->GetItemNoText(1)));
	Eval("ctxK:Open()");
	Refresh();
	// the rows: text in the first two and the last, the gray separator line in the third
	long right = p->viewBounds.right;
	EXPECT(InkIn(30, 37, 34, 48) == 0 && InkIn(34, 35, right, 48) > 0 && InkIn(right - 5, 37, right, 48) == 0);	// the text from the text column, short of the right margin
	EXPECT(InkIn(30, 48, 34, 61) == 0 && InkIn(34, 48, 40, 61) > 0);
	EXPECT(Pixel(31, 64) + Pixel(32, 64) == 1 && Pixel(31, 63) == 0 && Pixel(31, 65) == 0);	// the separator: gray at top + 3
	EXPECT(InkIn(30, 67, 34, 78) == 0 && InkIn(34, 67, 40, 80) > 0);
	// the frame (pen 4, rounded) lies outside the bounds
	EXPECT(Pixel(29, 50) == 1 && Pixel(28, 50) == 1 && Pixel(27, 50) == 0 && Pixel(40, 34) == 1 && Pixel(40, 33) == 1 && Pixel(40, 32) == 0);
	// the item under a point; a separator sends the search up
	PickStuff stuff;
	Point pt = MakePoint(40, 55);
	p->Item(pt, &stuff);
	EXPECT(stuff.fItem == 1 && !stuff.fIsGrid);
	pt = MakePoint(40, 63);
	p->PickableItem(pt, &stuff);
	EXPECT(stuff.fItem == 1);
	pt = MakePoint(40, 70);
	p->PickableItem(pt, &stuff);
	EXPECT(stuff.fItem == 3);
	pt = MakePoint(5, 5);
	p->Item(pt, &stuff);
	EXPECT(stuff.fItem == -1);
	Rect r;
	stuff.fItem = 3;
	p->GetItemRect(&stuff, &r);
	EXPECT(r.top == 67 && r.bottom == 80 && r.left == 30 && r.right == p->viewBounds.right);
	// keys: down moves the pick over the pickable items (the separator skipped), up back; the
	// pick is inverted on screen
	EXPECT(p->HandleKeyDown(0, 0x1f) && p->fPicked.fItem == 0);
	EXPECT(InkIn(34, 35, 60, 48) > (60 - 34) * 6);
	p->HandleKeyDown(0, 0x1f);
	p->HandleKeyDown(0, 0x1f);
	EXPECT(p->fPicked.fItem == 3);
	p->HandleKeyDown(0, 0x1f);
	EXPECT(p->fPicked.fItem == 3);		// the end
	p->HandleKeyDown(0, 0x1e);
	EXPECT(p->fPicked.fItem == 1);
	// type-select: "g" picks Gamma, "ga" too, then "b" (within the timeout, so "gab": nothing)
	p->fTypeSelectTimeout = 100000;
	p->HandleKeyDown('g', 'g');
	EXPECT(p->fPicked.fItem == 3);
	p->HandleKeyDown('a', 'a');
	EXPECT(p->fPicked.fItem == 3);
	p->HandleKeyDown('b', 'b');
	EXPECT(p->fPicked.fItem == 3);
	p->fTypeSelect = NILREF;		// (the timeout passed)
	p->HandleKeyDown('b', 'b');
	EXPECT(p->fPicked.fItem == 1);
	// through the ROM's viewKeyDownScript (PickViewKeyDown) from a key event to the popup
	{
		RefVar bundle(TranslateROMRef(0x004a4d09));
		RefVar intl(AllocateFrame());
		RefVar keyboard(AllocateFrame());
		SetFrameSlot(keyboard, RSSYMmapping, RefVar(GetFrameSlotRef(bundle, RefVar(Intern((char*) "keycodeMapping")))));
		SetFrameSlot(intl, RSSYMkeyboard, keyboard);
		SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	}
	gKeyboardConnected = true;
	Eval("ctxK:SetPopup()");
	KeyboardEvent down(aeKeyDown, 0x7d);		// the down arrow
	HandleKeyEvent(&down);
	EXPECT(p->fPicked.fItem == 3);
	KeyboardEvent up(aeKeyUp, 0x7d);
	HandleKeyEvent(&up);
	gKeyboardConnected = false;
	Eval("RemoveSlot(vars, 'international)");
	// picking: the pick command runs pickActionScript with the index and closes the autoclose picker
	{
		RefVar cmd(MakeCommand(aePickItem, p, 3));
		gApplication->DispatchCommand(cmd);
	}
	EXPECT(RINT(Eval("picked")) == 3 && gRootView->fChildren->Count() == 0);
	Refresh();
	EXPECT(MapIs(ExpWhite, "picker closed"));

	// marks, an icon, a truncated item, and the placement flipping above from the lower half
	static const unsigned char kRows[4] = { 0xf0, 0x90, 0x90, 0xf0 };
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "pict")), RefVar(MakeBitmap(kRows, 8, 4)));
	p = (TPickView*) ViewOf("ctxK := AddView(GetRoot(), {_proto: protoPicker, pickItems: [{item: \"One\", mark: $x}, {item: \"Two\", icon: pict}, \"A very long item that must be cut down to the maximum width of the picker\"], pickMaxWidth: 60, bounds: {left: 30, top: 85, right: 80, bottom: 95}, pickActionScript: func(index) picked := index})");
	EXPECT(p->fHasMarks && p->fTextLeft == 14 && p->fMarkLeft == 4);
	EXPECT(p->GetItemLength(2) < 0 && -p->GetItemLength(2) < 70);
	EXPECT(p->viewBounds.right == 30 + 14 + 60 + 5 && p->viewBounds.bottom == 85 && p->viewBounds.top == 85 - 39);
	EXPECT(RINT(Eval("ctxK.viewEffect")) == 0x182000);
	Eval("ctxK:Open()");
	Refresh();
	long top = p->viewBounds.top;
	EXPECT(InkIn(34, top, 44, top + 13) > 0 && InkIn(44, top, 60, top + 13) > 0);		// the mark in its column, the text after it
	EXPECT(InkIn(34, top + 13, 44, top + 26) == 0);									// no mark on the second
	EXPECT(Pixel(44, top + 17) == 1 && Pixel(47, top + 17) == 1 && Pixel(44, top + 20) == 1 && Pixel(45, top + 18) == 0);	// the icon (8 x 4 hollow box) at the text column's left, centred in the row
	EXPECT(InkIn(30, top + 26, 44, top + 37) == 0 && InkIn(44, top + 26, 44 + 60, top + 39) > 0 && InkIn(44 + 64, top + 26, p->viewBounds.right, top + 37) == 0);	// cut to the width, the ellipsis after
	Eval("ctxK:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "picker closed again"));

	// key commands (a command keyboard connected): an item's keyCommand,
	// or its keyMessage matched among the callback view's ($q
	// DoQuit), drawn at the right - the letter and the command key's
	// icon before it - the picker widened for them; picking one sends the
	// key message instead of running pickActionScript
	gKeyboardConnected = true;
	Eval("ctxC := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 200, top: 10, right: 220, bottom: 20}, _keyCommands: [{char: $q, modifiers: 1 << 25, keyMessage: 'DoQuit}], saved: 0, DoSave: func(ctx) saved := saved + 1})");
	Eval("picked := nil");
	p = (TPickView*) ViewOf("ctxK := AddView(GetRoot(), {_proto: protoPicker, callbackContext: ctxC, pickItems: [{item: \"Save\", keyCommand: {char: $s, modifiers: 1 << 25, keyMessage: 'DoSave}}, \"Plain\", {item: \"Quit\", keyMessage: 'DoQuit}], bounds: {left: 30, top: 20, right: 80, bottom: 35}, pickActionScript: func(index) picked := index})");
	EXPECT(NOTNIL(p->fKeyCommands) && NOTNIL(p->GetKeyCommand(0)) && ISNIL(p->GetKeyCommand(1)));
	EXPECT(NOTNIL(p->GetKeyCommand(2)) && EQRef(GetFrameSlotRef(p->GetKeyCommand(2), RSSYMchar), MAKECHAR('q')));
	long commandWidth = p->fKeyCommandWidth >> 16;
	EXPECT(commandWidth == RINT(Eval("Max(StrFontWidth(\"S\", protoPicker.viewFont), StrFontWidth(\"Q\", protoPicker.viewFont))")));
	{
		// an item with a command is as wide as its text and the command
		// key's icon (11, and 10 more: GetModifiersWidth)
		EXPECT(p->GetKeyCommandModifierWidth(0) == 21 && p->GetKeyCommandModifierWidth(1) == 0);
		long textWidest = RINT(Eval("Max(StrFontWidth(\"Save\", protoPicker.viewFont) + 21, Max(StrFontWidth(\"Plain\", protoPicker.viewFont), StrFontWidth(\"Quit\", protoPicker.viewFont) + 21))"));
		EXPECT(p->viewBounds.right == 30 + 4 + textWidest + commandWidth + 2 + 5);
	}
	Eval("ctxK:Open()");
	Refresh();
	{
		long r0 = p->viewBounds.right, t0 = p->viewBounds.top;
		long letterLeft = r0 - commandWidth - 5;
		EXPECT(InkIn(letterLeft, t0, r0 - 5 + 1, t0 + 13) > 0);						// "S" at the right of the first row
		EXPECT(InkIn(letterLeft - 11, t0, letterLeft - 2, t0 + 13) > 0);				// the command key's icon before it
		EXPECT(InkIn(letterLeft, t0 + 13, r0, t0 + 26) == 0);							// none on "Plain"
		EXPECT(InkIn(letterLeft, t0 + 26, r0 - 5 + 1, t0 + 39) > 0);					// "Q" on "Quit"
	}
	{
		RefVar cmd(MakeCommand(aePickItem, p, 0));
		gApplication->DispatchCommand(cmd);
	}
	EXPECT(RINT(Eval("ctxC.saved")) == 1 && ISNIL(Eval("picked")) && gRootView->fChildren->Count() == 1);
	Eval("ctxC:Close()");
	gKeyboardConnected = false;
	Refresh();
	EXPECT(MapIs(ExpWhite, "key command picker closed"));

	// an ink item (the corrector's, for writing): its strokes drawn from
	// their bounds, brought down to 28 high - a stroke 60 high and 40 wide
	// drawn 28 high and 18 wide at the text column, under the top margin
	Eval("bndl := MakeStrokeBundle([[10, 10, 40, 30, 70, 50]], 1)");
	p = (TPickView*) ViewOf("ctxK := AddView(GetRoot(), {_proto: protoPicker, pickItems: [\"Text\", {strokeList: bndl, bounds: bndl.bounds, pickable: true}], bounds: {left: 30, top: 20, right: 80, bottom: 35}, pickActionScript: func(index) picked := index})");
	EXPECT(RINT(Eval("bndl.bounds.bottom - bndl.bounds.top")) == 60 && RINT(Eval("bndl.bounds.right - bndl.bounds.left")) == 40);
	Eval("ctxK:Open()");
	Refresh();
	{
		long inkTop = p->viewBounds.top + p->ItemTop(1) + p->fTopMargin;
		long inkLeft = p->viewBounds.left + p->fTextLeft;
		EXPECT(p->ItemBottom(1) - p->ItemTop(1) == 28 + p->fTopMargin + p->fBottomMargin);
		EXPECT(InkIn(inkLeft, inkTop, inkLeft + 19, inkTop + 29) > 20);				// the stroke, scaled
		EXPECT(InkIn(inkLeft + 21, inkTop, p->viewBounds.right, inkTop + 29) == 0);		// no wider than 18
		EXPECT(InkIn(inkLeft, inkTop, inkLeft + 4, inkTop + 4) > 0 && InkIn(inkLeft + 14, inkTop + 24, inkLeft + 20, inkTop + 30) > 0);	// top left to bottom right
	}
	Eval("ctxK:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "ink picker closed"));

	// a grid picture with a mask: a point on the mask's blank picks no cell
	{
		static const unsigned char kHalf[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };	// the left 8 of 16
		RefVar grid(MakeBitmap(kHalf, 16, 8));
		SetFrameSlot(grid, RSSYMmask, RefVar(GetFrameSlotRef(RefVar(MakeBitmap(kHalf, 16, 8)), RSSYMbits)));
		SetFrameSlot(grid, RSSYMwidth, RefVar(MAKEINT(2)));
		SetFrameSlot(grid, RSSYMheight, RefVar(MAKEINT(1)));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "gridPict")), grid);
	}
	p = (TPickView*) ViewOf("ctxK := AddView(GetRoot(), {_proto: protoPicker, pickItems: [gridPict], bounds: {left: 30, top: 20, right: 80, bottom: 35}, pickActionScript: func(index) picked := index})");
	Eval("ctxK:Open()");
	Refresh();
	{
		long picLeft = p->viewBounds.left + p->fTextLeft, picTop = p->viewBounds.top + p->fTopMargin;
		Point inMask = MakePoint(picLeft + 4, picTop + 4), offMask = MakePoint(picLeft + 12, picTop + 4);
		p->Item(offMask, &stuff);
		EXPECT(stuff.fItem == 0 && stuff.fIsGrid && stuff.fX == 1);		// a cell of the grid there
		p->PickableItem(inMask, &stuff);
		EXPECT(stuff.fItem == 0 && stuff.fX == 0);
		p->PickableItem(offMask, &stuff);
		EXPECT(stuff.fItem == -1);										// but the mask is blank
	}
	Eval("ctxK:Close()");
	Refresh();

	// DoPopup opens a popup menu (a picker from canonicalPopup) over the
	// items; picking one runs the callback's pickActionScript and closes it
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "canonicalPopup")), RefVar(Rcanonicalpopup));
	Eval("popped := nil; cb := {pickActionScript: func(index) popped := index}");
	RefVar popCtx(Eval("GetRoot():DoPopup([\"Cut\", \"Copy\", \"Paste\"], {left: 30, top: 30, right: 30, bottom: 30}, 0, cb)"));
	TPickView* pop = (TPickView*) GetView(popCtx);
	EXPECT(pop != nil && pop->ClassID() == clPickView && pop->fItemCount == 3 && (pop->fFlags & vVisible));
	Refresh();
	{
		RefVar cmd(MakeCommand(aePickItem, pop, 1));		// pick "Copy"
		gApplication->DispatchCommand(cmd);
	}
	EXPECT(RINT(Eval("popped")) == 1 && gRootView->fChildren->Count() == 0);		// the callback ran, the autoclose popup gone
	Refresh();
	EXPECT(MapIs(ExpWhite, "popup closed"));

	// the three a script uses to find the popup that is up and take it
	// down (views/PickView.cpp)
	EXPECT(ISNIL(Eval("GetPopup()")));
	RefVar again(Eval("GetRoot():DoPopup([\"Cut\", \"Copy\"], {left: 30, top: 30, right: 30, bottom: 30}, 0, cb)"));
	EXPECT(NOTNIL(Eval("GetPopup()")) && EQRef(Eval("GetPopup()"), again));
	// DismissPopup closes every popup that is up
	Eval("DismissPopup()");
	EXPECT(ISNIL(Eval("GetPopup()")) && gRootView->fChildren->Count() == 0);
	// ClearPopup only forgets it - the view is still there, and whoever
	// opened it has to take it away
	RefVar third(Eval("GetRoot():DoPopup([\"Cut\"], {left: 30, top: 30, right: 30, bottom: 30}, 0, cb)"));
	Eval("ClearPopup()");
	EXPECT(ISNIL(Eval("GetPopup()")) && gRootView->fChildren->Count() == 1);
	Eval("RemoveView(GetRoot(), GetRoot():ChildViewFrames()[0])");
	EXPECT(gRootView->fChildren->Count() == 0);
	// the command key held down (its repeat) opens the key help, dismissing
	// any popup first (DoKeyEvent over FDismissPopup)
	{
		gKeyboardConnected = true;
		TView* helped = ViewOf("ctxKH := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 200, top: 10, right: 220, bottom: 20}, "
			"opened: nil, _keyHelpOpenScript: func(ctx) opened := true, _keyHelpCloseScript: func(ctx) opened := nil})");
		Eval("ctxPP := AddView(GetRoot(), {_proto: protoPicker, pickItems: [\"Cut\", \"Copy\"], bounds: {left: 30, top: 20, right: 80, bottom: 35}, pickActionScript: func(index) nil}); ctxPP:Open(); ctxPP:SetPopup()");
		EXPECT(NOTNIL(Eval("GetPopup()")));
		DoKeyEvent(helped, aeKeyRepeat, kCommandKey);
		EXPECT(ISNIL(Eval("GetPopup()")) && NOTNIL(Eval("ctxKH.opened")));
		// (the key up reads the keyboard's translation through
		// vars.international, which the test put back earlier: without it
		// GetKeyTransMapping throws, the ROM's as ours)
		{
			RefVar bundle(TranslateROMRef(0x004a4d09));
			RefVar intl(AllocateFrame());
			RefVar keyboard(AllocateFrame());
			SetFrameSlot(keyboard, RSSYMmapping, RefVar(GetFrameSlotRef(bundle, RefVar(Intern((char*) "keycodeMapping")))));
			SetFrameSlot(intl, RSSYMkeyboard, keyboard);
			SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
		}
		DoKeyEvent(helped, aeKeyUp, kCommandKey);
		EXPECT(ISNIL(Eval("ctxKH.opened")) && !gKeyHelpOpen);
		Eval("RemoveSlot(vars, 'international)");
		Eval("RemoveView(GetRoot(), ctxKH)");
		EXPECT(gRootView->fChildren->Count() == 0);
		gKeyboardConnected = false;
	}
	// nothing is selected, and no key is repeating
	EXPECT(ISNIL(Eval("HiliteOwner()")));
	EXPECT(ISNIL(Eval("InRepeatedKeyCommand()")));
	// what a view allows to be written on it: a plain view with every
	// recognition flag set takes both raw ink and unread words
	{
		RefVar all(Eval("ctxAI := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x1fffe00, viewBounds: {left: 0, top: 0, right: 20, bottom: 20}})"));
		EXPECT(NOTNIL(Eval("ViewAllowsInk(ctxAI)")));
		EXPECT(NOTNIL(Eval("ViewAllowsInkWords(ctxAI)")));
		Eval("RemoveView(GetRoot(), ctxAI)");
	}
	Refresh();
}


// the animation effects and dragging: the view effects run over the
// offscreen map (the waits are the tablet's hook: no time passes), the
// picture right when they are done
static void
TestEffects()
{
	// what InitScreen would set: the effects work in screen coordinates and save the screen's pixels
	screenWidth = kWidth;
	screenHeight = kHeight;
	qdGlobals.fScreenBits = gMap;
	Eval("vars.displayParams := {appAreaGlobalLeft: 0, appAreaGlobalTop: 0, appAreaWidth: 160, appAreaHeight: 100}");
	// a hidden view shown with an effect (eight rows wiped in), hidden with it: the effect's steps drawn, the picture whole after
	TView* e = ViewOf("ctxE := AddView(GetRoot(), {viewClass: 74, viewFlags: 0, viewBounds: {left: 20, top: 20, right: 80, bottom: 60}, viewFormat: 5, viewEffect: 7 << 5})");
	Refresh();
	EXPECT(MapIs(ExpWhite, "effect view hidden"));
	Eval("ctxE:Show()");
	EXPECT((e->fFlags & vVisible) != 0);
	Refresh();
	EXPECT(InkIn(20, 20, 80, 60) == 60 * 40 && InkIn(0, 0, 160, 20) == 0 && InkIn(80, 20, 160, 100) == 0);
	// ViewIntoBitmap draws a whole view into a bitmap rather than onto the
	// screen (views/DrawShape.cpp).  The screen bits are set just above,
	// which is where a fresh port's visible region comes from and what the
	// view's own coordinates are in.
	Eval("vb := MakeBitmap(60, 40, nil)");
	Eval("ctxE:ViewIntoBitmap(nil, nil, vb)");
	{
		RefVar data(Eval("vb.data"));
		LockRef(data);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		long lit = 0;
		for (long y = 0; y < 40; y++)
			for (long x = 0; x < 60; x++)
				if (GetPixel(pm, x, y) != 0)
					lit++;
		EXPECT(lit == 60 * 40);			// the view is filled black and fills the bitmap
		UnlockRef(data);
	}
	// a part of it into a corner of a larger bitmap
	Eval("vb := MakeBitmap(60, 40, nil)");
	Eval("ctxE:ViewIntoBitmap({left: 0, top: 0, right: 20, bottom: 10}, {left: 5, top: 5, right: 25, bottom: 15}, vb)");
	{
		RefVar data(Eval("vb.data"));
		LockRef(data);
		PixelMap pixMap; PixelsToPixMap(BinaryData(data), &pixMap); PixelMap* pm = &pixMap;
		long lit = 0;
		for (long y = 0; y < 40; y++)
			for (long x = 0; x < 60; x++)
				if (GetPixel(pm, x, y) != 0)
					lit++;
		EXPECT(lit == 20 * 10);
		EXPECT(GetPixel(pm, 5, 5) != 0 && GetPixel(pm, 24, 14) != 0);
		EXPECT(GetPixel(pm, 4, 5) == 0 && GetPixel(pm, 25, 15) == 0);
		UnlockRef(data);
	}
	Eval("vb := nil");
	Eval("ctxE:Hide()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "effect view hidden again"));
	// a sliding show (the cells moving down from the top), the slide of the contents, a reveal, an effect run by the script
	Eval("SetValue(ctxE, 'viewEffect, (1 << 17) + (1 << 11) + (2 << 21) + (1 << 25))");
	Eval("ctxE:Show()");
	Refresh();
	EXPECT(InkIn(20, 20, 80, 60) == 60 * 40 && InkIn(0, 60, 160, 100) == 0);
	// the slides draw the contents where they are going and leave the screen so (the callers move the contents): the view slid down ten rows
	Eval("ctxE:SlideEffect(10, 1, nil, nil, nil)");
	EXPECT(InkIn(20, 20, 80, 60) == 60 * 40 && InkIn(20, 60, 80, 70) == 60 * 10 && InkIn(0, 70, 160, 100) == 0);
	Eval("ctxE:SlideEffect(-10, 0, nil, nil, nil)");
	Eval("ctxE:SlideEffect(10, -1, nil, nil, nil)");
	Eval("ctxE:RevealEffect(8, {left: 0, top: 0, right: 60, bottom: 20}, nil, nil, nil)");
	Eval("ctxE:Effect((1 << 16) + (1 << 18) + (3 << 21), nil, nil, nil, nil)");
	Eval("ctxE:Effect(0x1f + (1 << 12) + (1 << 14), true, nil, nil, nil)");
	gRootView->Dirty(nil);
	Refresh();
	EXPECT(InkIn(20, 20, 80, 60) == 60 * 40 && InkIn(0, 60, 160, 100) == 0 && InkIn(80, 0, 160, 100) == 0);
	// the trash: crumpled into the ROM's trash can at the application area's corner, the view closed by the message
	Eval("ctxE.trashed := nil; ctxE.trashIt := func() begin trashed := true; :Close() end");
	Eval("ctxE:Delete('trashIt, nil)");
	EXPECT(NOTNIL(Eval("ctxE.trashed")) && gRootView->fChildren->Count() == 0);
	Refresh();
	Boolean trashedClean = MapIs(ExpWhite, "trashed");
	if (!trashedClean)
		DumpMap("after the trash");
	EXPECT(trashedClean);
	// the poof over a unit's bounds
	Eval("ctxP := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 10, top: 10, right: 150, bottom: 90}, viewFormat: 1, "
		"viewClickScript: func(unit) begin DoScrubEffect(unit); true end})");
	Eval("ctxP:Dirty()");
	Refresh();
	HostTabletPenDown(80, 50, 11000);
	HostTabletPenUp(11003);
	gRecognition.Idle();
	Refresh();
	EXPECT(InkIn(10, 10, 150, 90) == 0);		// (the frame is a pixel: viewFormat 1 has none)
	Eval("RemoveView(GetRoot(), ctxP)");
	Refresh();
	// dragging: a view moved by the pen within the bounds given, its viewBounds slot following; a press that goes nowhere moves nothing
	TView* d = ViewOf("ctxD := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 20, top: 20, right: 60, bottom: 50}, viewFormat: 5, dragged: nil, "
		"viewClickScript: func(unit) begin dragged := :Drag(unit, {left: 0, top: 0, right: 160, bottom: 100}); true end})");
	Eval("ctxD:Dirty()");
	Refresh();
	EXPECT(InkIn(20, 20, 60, 50) == 40 * 30 && InkIn(60, 0, 160, 100) == 0);
	HostTabletQueuePenDown(40, 35, 12000);
	HostTabletQueuePenMove(45, 40);
	HostTabletQueuePenMove(60, 50);
	HostTabletQueuePenMove(80, 60);
	HostTabletQueuePenMove(80, 60);
	HostTabletQueuePenMove(90, 65);
	HostTabletQueuePenUp(12040);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(NOTNIL(Eval("ctxD.dragged")) && d->viewBounds.left == 70 && d->viewBounds.top == 50 && d->viewBounds.right == 110 && d->viewBounds.bottom == 80);
	EXPECT(RINT(Eval("ctxD.viewBounds.left")) == 70 && RINT(Eval("ctxD.viewBounds.top")) == 50 && HostTabletQueued() == 0);
	Refresh();
	EXPECT(InkIn(70, 50, 110, 80) == 40 * 30 && InkIn(0, 0, 160, 50) == 0 && InkIn(0, 50, 70, 100) == 0);
	// dragged past the limit: stopped at the edge
	HostTabletQueuePenDown(90, 65, 13000);
	HostTabletQueuePenMove(150, 65);
	HostTabletQueuePenMove(200, 65);
	HostTabletQueuePenUp(13020);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(d->viewBounds.right == 160 && d->viewBounds.left == 120 && d->viewBounds.top == 50);
	Refresh();
	EXPECT(InkIn(120, 50, 160, 80) == 40 * 30 && InkIn(0, 0, 120, 100) == 0);
	// a press without a move: nothing moves (:Drag answers true all the same)
	Eval("ctxD.dragged := 'untouched");
	HostTabletQueuePenDown(140, 65, 14000);
	HostTabletQueuePenMove(141, 65);
	HostTabletQueuePenUp(14010);
	HostTabletPump();
	gRecognition.Idle();
	EXPECT(NOTNIL(Eval("ctxD.dragged")) && d->viewBounds.left == 120 && d->viewBounds.top == 50);
	Refresh();
	EXPECT(InkIn(120, 50, 160, 80) == 40 * 30 && InkIn(0, 0, 120, 100) == 0);
	Eval("RemoveView(GetRoot(), ctxD)");
	Refresh();
	EXPECT(MapIs(ExpWhite, "drag view removed"));
}


// the key view chain: tab moves the caret along the visible, editable
// views in the tab order (BuildKeyChildList), wrapping around; NextKeyView
// answers the next/previous of a kind
static void
TestKeyChain()
{
	const ULong kUSABundle = 0x004a4d09;
	RefVar bundle(TranslateROMRef(kUSABundle));
	RefVar intl(AllocateFrame());
	RefVar keyboard(AllocateFrame());
	SetFrameSlot(keyboard, RSSYMmapping, RefVar(GetFrameSlotRef(bundle, RefVar(Intern((char*) "keycodeMapping")))));
	SetFrameSlot(intl, RSSYMkeyboard, keyboard);
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, bundle);
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	ClearHardKeymap();
	// a slip with three editable fields (textFlags 0x8000 marks a tab stop),
	// a read-only field between them (skipped) and a plain box (skipped)
	Eval("ctxKC := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 150, bottom: 90}, viewFormat: 1, viewChildren: ["
		"{viewClass: 81, viewFlags: 1, viewBounds: {left: 4, top: 2, right: 130, bottom: 14}, textFlags: 0x8000, viewFont: espy12, text: \"one\", debug: 'f1},"
		"{viewClass: 74, viewFlags: 1, viewBounds: {left: 4, top: 16, right: 20, bottom: 28}, viewFormat: 1, debug: 'box},"
		"{viewClass: 81, viewFlags: 3, viewBounds: {left: 4, top: 30, right: 130, bottom: 42}, textFlags: 0x8000, viewFont: espy12, text: \"ro\", debug: 'ro},"
		"{viewClass: 81, viewFlags: 1, viewBounds: {left: 4, top: 44, right: 130, bottom: 56}, textFlags: 0x8000, viewFont: espy12, text: \"two\", debug: 'f2},"
		"{viewClass: 81, viewFlags: 1, viewBounds: {left: 4, top: 58, right: 130, bottom: 70}, textFlags: 0x8000, viewFont: espy12, text: \"three\", debug: 'f3}]})");
	Eval("ctxKC:Dirty()");
	Refresh();
	TView* f1 = GetView(RefVar(Eval("ctxKC:ChildViewFrames()[0]")));
	TView* f2 = GetView(RefVar(Eval("ctxKC:ChildViewFrames()[3]")));
	TView* f3 = GetView(RefVar(Eval("ctxKC:ChildViewFrames()[4]")));
	// NextKeyView walks the three fields in order, wrapping both ways; the
	// box and the read-only field are not in the chain
	EXPECT(f1->NextKeyView(f1, 1, 0) == f2 && f2->NextKeyView(f2, 1, 0) == f3 && f3->NextKeyView(f3, 1, 0) == f1);
	EXPECT(f1->NextKeyView(f1, -1, 0) == f3 && f3->NextKeyView(f3, -1, 0) == f2 && f2->NextKeyView(f2, -1, 0) == f1);
	// the NewtonScript native answers the context of the next view
	EXPECT(EQRef(Eval("NextKeyView(ctxKC:ChildViewFrames()[0], 1, 0)"), (Ref) f2->fContext));
	EXPECT(EQRef(Eval("NextKeyView(ctxKC:ChildViewFrames()[3], -1, 0)"), (Ref) f1->fContext));
	// tab from the first field moves the caret to the second, shift-tab back
	gKeyboardConnected = true;
	Eval("SetKeyView(ctxKC:ChildViewFrames()[0], 0)");
	EXPECT(gRootView->fCaretView == f1);
	TypeKey(0x30);		// tab
	EXPECT(gRootView->fCaretView == f2);
	TypeKey(0x30);
	EXPECT(gRootView->fCaretView == f3);
	TypeKey(0x30);		// wraps to the first
	EXPECT(gRootView->fCaretView == f1);
	Eval("SetKeyView(nil, nil)");
	gKeyboardConnected = false;
	Eval("RemoveView(GetRoot(), ctxKC); RemoveSlot(vars, 'international)");
	Refresh();
	EXPECT(MapIs(ExpWhite, "key chain closed"));
}


// text selection: a hilited range of a paragraph drawn inverted, made by
// MakeHilite (and by tab into a paragraph, which selects it all)
// The range of a paragraph's first hilite: a THilite pointer Ref in the
// view's hilites array, not a frame (views/Hilites.h).
static long
HiliteRange(TParagraphView* view, Boolean wantStart)
{
	RefVar first(view->FirstHilite());
	if (ISNIL(first))
		return -1;
	TParagraphHilite* hilite = (TParagraphHilite*) RefToAddress(first);
	return wantStart ? hilite->fStart : hilite->fEnd;
}


// The face of the style a paragraph would draw the character at `offset`
// in (views/StyleRuns.h's runs array, [length, spec, length, spec, ...]).
static long
FaceAt(TParagraphView* view, long offset)
{
	RefVar runs(view->GetStylesOfRange(offset, 1, false));
	return GetFontFace(RefVar(GetArraySlotRef(runs, 1)));
}


static void
TestSelection()
{
	const ULong kUSABundle = 0x004a4d09;
	RefVar bundle(TranslateROMRef(kUSABundle));
	RefVar intl(AllocateFrame());
	RefVar keyboard(AllocateFrame());
	SetFrameSlot(keyboard, RSSYMmapping, RefVar(GetFrameSlotRef(bundle, RefVar(Intern((char*) "keycodeMapping")))));
	SetFrameSlot(intl, RSSYMkeyboard, keyboard);
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, bundle);
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	ClearHardKeymap();
	TParagraphView* p = (TParagraphView*) ViewOf("ctxS := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 140, bottom: 30}, viewJustify: 0, viewFont: espy12, text: \"Hello World\"})");
	Eval("ctxS:Dirty()");
	Refresh();
	// select "Hello" (offsets 0..5): its box on the line is inverted
	Rect box0, box5;
	p->OffsetToBounds(0, &box0);
	p->OffsetToBounds(5, &box5);
	long before = InkIn(box0.left, p->Line(0).fBounds.top, box5.left, p->Line(0).fBounds.bottom);
	p->MakeHilite(0, 5, true);
	Refresh();
	EXPECT(RINT(Eval("Length(ctxS.hilites)")) == 1 && HiliteRange(p, true) == 0 && HiliteRange(p, false) == 5);
	long after = InkIn(box0.left, p->Line(0).fBounds.top, box5.left, p->Line(0).fBounds.bottom);
	EXPECT(after != before);		// the region is inverted (mostly-white text becomes mostly-black)
	EXPECT(gRootView->fCaretView == p && gRootView->fCaretLength == 5 && !gRootView->fCaretShowing);	// a selection, no caret
	// the ink outside the selection ("World") is untouched
	Rect box6, box11;
	p->OffsetToBounds(6, &box6);
	p->OffsetToBounds(11, &box11);
	long worldInk = InkIn(box6.left, p->Line(0).fBounds.top, box11.left, p->Line(0).fBounds.bottom);
	EXPECT(worldInk > 0 && worldInk < (box11.left - box6.left) * (p->Line(0).fBounds.bottom - p->Line(0).fBounds.top));	// plain text, not a solid block
	// extending the selection unions the ranges
	p->MakeHilite(5, 8, false);
	EXPECT(HiliteRange(p, true) == 0 && HiliteRange(p, false) == 8);
	// RemoveHilite drops the one hilite (the slot set to nil once empty)
	p->RemoveHilite(RefVar(p->FirstHilite()));
	EXPECT(ISNIL(p->FirstHilite()) && ISNIL(Eval("ctxS.hilites")));
	// PointInHilite tells whether a point is on the selection
	p->MakeHilite(6, 11, false);				// select "World"
	{
		Rect wb; p->OffsetToBounds(8, &wb);
		Point inSel; inSel.h = (short) wb.left; inSel.v = (short) ((p->Line(0).fBounds.top + p->Line(0).fBounds.bottom) / 2);
		Rect hb; p->OffsetToBounds(2, &hb);
		Point outSel; outSel.h = (short) hb.left; outSel.v = inSel.v;
		EXPECT(p->PointInHilite(inSel) && !p->PointInHilite(outSel));
	}
	p->RemoveAllHilites();
	// ChangeStyleOfSelection restyles the selected text: "World" (6..11)
	// given a bold spec becomes a run of its own with that style
	p->MakeHilite(6, 11, false);
	p->ChangeStyleOfSelection(RefVar(Eval("{family: 'espy, face: 1, size: 12}")));
	EXPECT(RINT(Eval("Length(ctxS.styles)")) >= 2);				// now there are style runs
	EXPECT(RINT(Eval("ctxS.styles[0]")) == 6 && RINT(Eval("ctxS.styles[2]")) == 5);	// the first run "Hello ", then the 5-char "World"
	// the run's spec: 'espy has a family number, so the three parts pack
	// into one integer rather than staying a frame (qd/Fonts.h)
	EXPECT(GetFontFace(RefVar(Eval("ctxS.styles[3]"))) == 1);	// the selection's run is bold
	EXPECT(GetFontSize(RefVar(Eval("ctxS.styles[3]"))) == 12);
	p->RemoveAllHilites();
	Eval("ctxS.text := \"Hello World\"; ctxS.styles := nil; ctxS:SyncView()");	// back to plain for the checks below

	// The parts of a font spec, as the Styles slip reads and writes them
	// (views/FontNatives.cpp over qd/Fonts.h).  A family that has a
	// number packs the three into one integer.
	EXPECT(RINT(Eval("GetFontFamilyNum(MakeCompactFont('geneva, 10, 2))")) == 2);
	// the colours a script packs and takes apart, and the gray the port
	// would draw them as (qd/Ports.cpp)
	EXPECT(RINT(Eval("GetRed(PackRGB(0xFFFF, 0x8000, 0))")) == 0xFFFF);
	EXPECT(RINT(Eval("GetGreen(PackRGB(0xFFFF, 0x8000, 0))")) == 0x8080);
	EXPECT(RINT(Eval("GetBlue(PackRGB(0xFFFF, 0x8000, 0))")) == 0);
	EXPECT(NOTNIL(Eval("IsEqualTone(PackRGB(0, 0, 0), PackRGB(0, 0, 0))")));
	EXPECT(ISNIL(Eval("IsEqualTone(PackRGB(0, 0, 0), PackRGB(0xFFFF, 0xFFFF, 0xFFFF))")));
	// the gray itself, at a depth of its own: white is 0 and black all
	// ones, the other way from a colour (qd/Ports.h)
	EXPECT(RGBtoGray(0xffff, 0xffff, 0xffff, 8, 8) == 0);
	EXPECT(RGBtoGray(0, 0, 0, 8, 8) == 0xff);
	EXPECT(RGBtoGray(0x8080, 0x8080, 0x8080, 8, 8) > 0x70 && RGBtoGray(0x8080, 0x8080, 0x8080, 8, 8) < 0x90);
	// the large-binary questions: an ordinary binary is not one
	EXPECT(ISNIL(Eval("IsVBO(\"hello\")")) && ISNIL(Eval("GetVBOStore(\"hello\")")));
	// a polygon shape opened out into an array and put back
	Eval("pts := ArrayToPoints([3, 2, 10, 20, 30, 40])");
	EXPECT(EQRef(ClassOf(Eval("pts")), Intern((char*) "polygonShape")));
	EXPECT(RINT(Eval("Length(PointsToArray(pts))")) == 6);
	EXPECT(RINT(Eval("PointsToArray(pts)[0]")) == 3 && RINT(Eval("PointsToArray(pts)[1]")) == 2);
	EXPECT(RINT(Eval("PointsToArray(pts)[2]")) == 10 && RINT(Eval("PointsToArray(pts)[3]")) == 20);
	EXPECT(RINT(Eval("PointsToArray(pts)[5]")) == 40);
	Eval("pts := nil");
	// the small view and array verbs
	EXPECT(RINT(Eval("UnionPoint(nil, 5, 7).left")) == 5 && RINT(Eval("UnionPoint(nil, 5, 7).bottom")) == 7);
	EXPECT(RINT(Eval("UnionPoint({left: 0, top: 0, right: 2, bottom: 2}, 5, 7).right")) == 5);
	EXPECT(RINT(Eval("UnionPoint({left: 0, top: 0, right: 2, bottom: 2}, 5, 7).bottom")) == 7);
	EXPECT(NOTNIL(Eval("BubbleArraySlot(['a, 'b, 'c, 'd], 0, 2)")));
	EXPECT(EQRef(Eval("BubbleArraySlot(['a, 'b, 'c, 'd], 0, 2)[2]"), Intern((char*) "a")));
	EXPECT(EQRef(Eval("BubbleArraySlot(['a, 'b, 'c, 'd], 0, 2)[0]"), Intern((char*) "b")));
	EXPECT(EQRef(Eval("BubbleArraySlot(['a, 'b, 'c, 'd], 3, 1)[1]"), Intern((char*) "d")));
	EXPECT(NOTNIL(Eval("MatchedChar($A, 65)")) && ISNIL(Eval("MatchedChar($A, 66)")));
	EXPECT(RINT(Eval("GetFontFace(MakeCompactFont('geneva, 10, 2))")) == 2);
	EXPECT(ISNIL(Eval("GetFontFamilyNum({family: 'nosuchfont, size: 9, face: 0})")));
	// a font frame that does not say what face it is answers nil, not 0
	EXPECT(ISNIL(Eval("GetFontFace({family: 'espy, size: 9})")));
	EXPECT(RINT(Eval("GetFontFace({family: 'espy, size: 9, face: 3})")) == 3);
	// one part changed at a time, the others kept
	EXPECT(RINT(Eval("GetFontFace(SetFontFace(MakeCompactFont('espy, 12, 0), 1))")) == 1);
	EXPECT(RINT(Eval("GetFontFamilyNum(SetFontFace(MakeCompactFont('geneva, 12, 0), 1))")) == 2);
	EXPECT(GetFontSize(RefVar(Eval("SetFontSize(MakeCompactFont('espy, 12, 1), 18)"))) == 18);
	EXPECT(GetFontFace(RefVar(Eval("SetFontSize(MakeCompactFont('espy, 12, 1), 18)"))) == 1);
	EXPECT(RINT(Eval("GetFontFamilyNum(SetFontFamily(MakeCompactFont('espy, 12, 1), 'newYork))")) == 1);
	// SetFontParms takes any of the three out of one frame
	EXPECT(RINT(Eval("GetFontFace(SetFontParms(MakeCompactFont('espy, 12, 0), {face: 2}))")) == 2);
	EXPECT(RINT(Eval("GetFontFamilyNum(SetFontParms(MakeCompactFont('espy, 12, 0), {face: 2}))")) == 0);
	// a family with no number keeps the spec a frame
	EXPECT(NOTNIL(Eval("IsFrame(MakeCompactFont('nosuchfont, 12, 1))")));
	EXPECT(RINT(Eval("MakeCompactFont('nosuchfont, 12, 1).size")) == 12);

	// ChangeStylesOfRange: the Styles slip's verb, and the face commands
	// it sends - 1 adds the bits, 2 takes them away, 3 toggles, and the
	// toggle makes its mind up on the first run of the range, so a
	// selection that is part bold ends up bold all through
	Eval("ctxS.text := \"Hello World\"; ctxS.styles := nil; ctxS:SyncView()");
	Eval("ClearUndoStacks()");
	Eval("ctxS:ChangeStylesOfRange(6, 5, {fontParms: {face: 1}, command: 3}, true)");
	EXPECT(FaceAt(p, 6) == 1 && FaceAt(p, 0) == 0);		// "World" bold, "Hello " not
	Eval("ctxS:ChangeStylesOfRange(6, 5, {fontParms: {face: 1}, command: 3}, true)");
	EXPECT(FaceAt(p, 6) == 0);							// toggled off again
	Eval("ctxS:ChangeStylesOfRange(0, 11, {fontParms: {face: 2}, command: 1}, true)");
	EXPECT(FaceAt(p, 0) == 2 && FaceAt(p, 6) == 2);		// the lot italic
	Eval("ctxS:ChangeStylesOfRange(0, 11, {fontParms: {face: 2}, command: 2}, true)");
	EXPECT(FaceAt(p, 0) == 0);
	// a plain spec sets the style outright
	Eval("ctxS:ChangeStylesOfRange(6, 5, MakeCompactFont('espy, 14, 2), true)");
	EXPECT(FaceAt(p, 6) == 2 && FaceAt(p, 0) == 0);
	// and it went through the ordinary replace-text command, so it undoes
	gApplication->Idle();
	Eval("Undo()");
	EXPECT(NOTNIL(Eval("StrEqual(ctxS.text, \"Hello World\")")) && FaceAt(p, 6) == 0);
	Eval("ClearUndoStacks()");
	Eval("ctxS.text := \"Hello World\"; ctxS.styles := nil; ctxS:SyncView()");

	// the rest of the style natives a slip asks for
	EXPECT(NOTNIL(Eval("GetDefaultFont(ctxS)")));
	// a paragraph answers the text flags it took when it was set up (its own
	// accessor at vtable +0x20), not a textFlags slot set since
	EXPECT(RINT(Eval("GetTextFlags(ctxS)")) == (long) p->fTextFlags);
	Eval("ctxS.textFlags := 5");
	EXPECT(RINT(Eval("GetTextFlags(ctxS)")) == (long) p->fTextFlags && p->fTextFlags != 5);
	Eval("RemoveSlot(ctxS, 'textFlags)");
	EXPECT(NOTNIL(Eval("StrEqual(ctxS:ExtractTextRange(0, 5), \"Hello\")")));
	EXPECT(NOTNIL(Eval("GetInsertionStyle()")));
	EXPECT(NOTNIL(Eval("StrEqual(GetRangeText(ctxS, 6, 11), \"World\")")));
	// InsertStyledText puts text into the paragraph at an offset, taking
	// out what it is given first (views/ViewNatives.cpp over
	// TParagraphView::InsertStyledText)
	Eval("InsertStyledText(ctxS, 5, 0, \", cruel\", nil)");
	EXPECT(NOTNIL(Eval("StrEqual(ctxS.text, \"Hello, cruel World\")")));
	Eval("InsertStyledText(ctxS, 5, 7, \"\", nil)");
	EXPECT(NOTNIL(Eval("StrEqual(ctxS.text, \"Hello World\")")));
	// TieViews and OffsetView
	Eval("TieViews(ctxS, 'a, 'b)");
	EXPECT(RINT(Eval("Length(ctxS.viewTie)")) == 2);
	Eval("TieViews(ctxS, 'c, 'd)");
	EXPECT(RINT(Eval("Length(ctxS.viewTie)")) == 4);
	Eval("RemoveSlot(ctxS, 'viewTie)");
	{
		Rect was = p->viewBounds;
		Eval("ctxS:OffsetView(3, 4)");
		EXPECT(p->viewBounds.left == was.left + 3 && p->viewBounds.top == was.top + 4);
		Eval("ctxS:OffsetView(-3, -4)");
		EXPECT(p->viewBounds.left == was.left && p->viewBounds.top == was.top);
	}

	// the word scanners, as a script walks a piece of text
	// ("the cat  sat": 0..2 the, 4..6 cat, 9..11 sat)
	Eval("s := \"the cat  sat\"");
	EXPECT(RINT(Eval("ScanWordStart(s, 5, 0)")) == 4);
	EXPECT(RINT(Eval("ScanWordEnd(s, 5, 12)")) == 7);
	EXPECT(RINT(Eval("ScanNextWord(s, 7, 12)")) == 9);
	EXPECT(ISNIL(Eval("ScanNextWord(s, 7, 8)")));			// nothing but space up to the limit
	EXPECT(RINT(Eval("ScanPrevWordEnd(s, 8, 0)")) == 7);
	EXPECT(ISNIL(Eval("ScanPrevWordEnd(s, 3, 3)")));		// the limit is itself white space
	Eval("RemoveSlot(vars, 's)");

	// the caret: put in the paragraph at an offset, and with a length it
	// selects instead
	Eval("SetCaretInfo(ctxS, {offset: 3})");
	EXPECT(gRootView->fCaretView == p && gRootView->fCaretOffset == 3);
	EXPECT(NOTNIL(Eval("GetCaretInfo().view")) && EQRef(Eval("GetCaretInfo().view"), Eval("ctxS")));
	Eval("SetCaretInfo(ctxS, {offset: 6, length: 5})");
	EXPECT(HiliteRange(p, true) == 6 && HiliteRange(p, false) == 11);
	p->RemoveAllHilites();
	// HideCaret/ShowCaret take it off the screen and put it back
	Eval("HideCaret()");
	EXPECT(gRootView->fCaretHidden == 1);
	Eval("ShowCaret()");
	EXPECT(gRootView->fCaretHidden == 0);
	Eval("SetCaretInfo(nil, nil)");
	EXPECT(gRootView->fCaretView == nil);
	// typing over a selection replaces it in one edit
	gKeyboardConnected = true;
	Eval("ClearUndoStacks()");
	gApplication->Idle();
	p->RemoveAllHilites();
	p->MakeHilite(0, 5, true);		// select "Hello"
	EXPECT(NOTNIL(p->FirstHilite()));
	TypeKey(16);	// y: replaces the selection
	EXPECT(NOTNIL(Eval("StrEqual(ctxS.text, \"y World\")")) && ISNIL(p->FirstHilite()) && p->fCaretOffset == 1);
	// backspace over a selection deletes it
	p->MakeHilite(0, 2, true);		// select "y "
	TypeKey(0x33);	// backspace
	EXPECT(NOTNIL(Eval("StrEqual(ctxS.text, \"World\")")) && ISNIL(p->FirstHilite()));
	// an arrow collapses a selection to its edge
	p->MakeHilite(1, 4, true);
	TypeKey(0x7b);	// left arrow: caret to the selection start
	EXPECT(ISNIL(p->FirstHilite()) && p->fCaretOffset == 1);
	gKeyboardConnected = false;
	Eval("ctxS.text := \"Hello World\"; ctxS:SyncView()");		// back to the plain text for the check below
	Refresh();
	// removing the hilites restores the plain text
	p->RemoveAllHilites();
	Refresh();
	// (RemoveHilite takes them out one by one, and the last leaves the slot nil)
	EXPECT(ISNIL(p->FirstHilite()) && ISNIL(Eval("ctxS.hilites"))
		&& InkIn(box0.left, p->Line(0).fBounds.top, box5.left, p->Line(0).fBounds.bottom) == before);
	Eval("SetKeyView(nil, nil)");
	Eval("ctxS:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "selection closed"));

	// tab into a paragraph selects it whole
	gKeyboardConnected = true;
	Eval("ctxS2 := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, viewBounds: {left: 10, top: 10, right: 150, bottom: 60}, viewFormat: 1, viewChildren: ["
		"{viewClass: 81, viewFlags: 1, viewBounds: {left: 4, top: 2, right: 130, bottom: 14}, textFlags: 0x8000, viewFont: espy12, text: \"aaa\"},"
		"{viewClass: 81, viewFlags: 1, viewBounds: {left: 4, top: 16, right: 130, bottom: 28}, textFlags: 0x8000, viewFont: espy12, text: \"bbbbb\"}]})");
	Eval("ctxS2:Dirty()");
	Refresh();
	TParagraphView* q0 = (TParagraphView*) GetView(RefVar(Eval("ctxS2:ChildViewFrames()[0]")));
	TParagraphView* q = (TParagraphView*) GetView(RefVar(Eval("ctxS2:ChildViewFrames()[1]")));
	Eval("SetKeyView(ctxS2:ChildViewFrames()[0], 0)");
	TypeKey(0x30);		// tab: the second field selected whole
	EXPECT(gRootView->fCaretView == q && HiliteRange(q, false) == 5);
	TypeKey(0x30);		// tab again: back to the first, and the second's selection removed (its ActivateSelection(false))
	EXPECT(gRootView->fCaretView == q0 && NOTNIL(q0->FirstHilite()) && ISNIL(q->FirstHilite()));
	// the selection stack: select in the first field, move the key view
	// away (the selection pushed), then RestoreKeyView brings it back
	q0->MakeHilite(0, 3, false);							// select "aaa" in the first
	EXPECT(HiliteRange(q0, false) == 3);
	gRootView->SetKeyView(nil, 0, 0, false);				// focus away: q0's selection pushed, its hilite deactivated
	EXPECT(gRootView->fCaretView == nil && ISNIL(q0->FirstHilite()));
	EXPECT(gRootView->RestoreKeyView(GetView(RefVar(Eval("ctxS2")))));	// restore within the container
	EXPECT(gRootView->fCaretView == q0 && NOTNIL(q0->FirstHilite()) && HiliteRange(q0, false) == 3);
	Eval("SetKeyView(nil, nil); SetLength(GetSelectionStack(), 0); RemoveView(GetRoot(), ctxS2); RemoveSlot(vars, 'international)");
	gKeyboardConnected = false;
	Refresh();
	EXPECT(MapIs(ExpWhite, "tab selection closed"));
}


// tapping a paragraph places the caret; a double tap selects the word
static void
TestParagraphTap()
{
	const ULong kUSABundle = 0x004a4d09;
	RefVar bundle(TranslateROMRef(kUSABundle));
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, bundle);
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	gKeyboardConnected = true;
	TParagraphView* p = (TParagraphView*) ViewOf("ctxPT := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewBounds: {left: 20, top: 10, right: 160, bottom: 30}, viewJustify: 0, viewFont: espy12, text: \"Hello World\"})");
	Eval("ctxPT:Dirty()");
	Refresh();
	long mid = (p->Line(0).fBounds.top + p->Line(0).fBounds.bottom) / 2;
	// a tap on the boundary between the 3rd and 4th character places the
	// caret there (PointToChar: the boundaries whose characters' middles
	// the point is past - an 'l' is two pixels wide, so a pixel in is its
	// middle and already past it)
	Rect box3;
	p->OffsetToBounds(3, &box3);
	Point tap;
	tap.h = (short) box3.left;
	tap.v = (short) mid;
	p->HandleTap(tap);
	EXPECT(gRootView->fCaretView == p && p->fCaretOffset == 3 && gRootView->fCaretLength == 0);
	// a tap past the end of the text goes to the end
	Point tapEnd;
	tapEnd.h = (short) (p->viewBounds.right - 1);
	tapEnd.v = (short) mid;
	p->HandleTap(tapEnd);
	EXPECT(p->fCaretOffset == 11);
	// a double tap in "World" selects the whole word (offsets 6..11)
	Rect box8;
	p->OffsetToBounds(8, &box8);
	Point tapWord;
	tapWord.h = (short) box8.left;
	tapWord.v = (short) mid;
	EXPECT(p->SelectWordAt(tapWord));
	EXPECT(NOTNIL(p->FirstHilite()) && HiliteRange(p, true) == 6 && HiliteRange(p, false) == 11);
	p->RemoveAllHilites();
	// the deferred single tap: aeTap stores the point and arms the idler,
	// Idle(2) places the caret once the interval passes
	p->fTapped = true;
	p->fTapPoint = tap;			// near offset 3
	gRootView->SetKeyView(nil, 0, 0, false);
	p->Idle(2);
	EXPECT(!p->fTapped && gRootView->fCaretView == p && p->fCaretOffset == 3);
	Eval("SetKeyView(nil, nil); RemoveView(GetRoot(), ctxPT); RemoveSlot(vars, 'international)");
	gKeyboardConnected = false;
	Refresh();
	EXPECT(MapIs(ExpWhite, "paragraph tap closed"));
}


// Ink put on a page: the strokes handed to an edit view are packed up
// as an ink shape and offered to the page as a child of its own.  What
// the page does with it is the application's business - the frame the
// ROM makes is a piece of stationery, not a view template, and it is the
// page's viewAddChildScript that turns it into a view - so the test
// The stroke, ink and try-string functions a script reaches - thin
// natives over the machinery the areas above already have
// (recognition/StrokeBundle.cpp, ink/InkShapes.cpp,
// recognition/WordList.cpp).
static void
TestStrokeAndInkNatives()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();

	// a bundle of one stroke, made and taken apart from a script
	Eval("bndl := MakeStrokeBundle([[20, 10, 30, 20, 40, 30]], 1)");
	EXPECT(RINT(Eval("CountStrokes(bndl)")) == 1);
	Eval("strk := GetStroke(bndl, 0)");
	EXPECT(RINT(Eval("CountPoints(strk)")) == 3);
	EXPECT(RINT(Eval("GetStrokeBounds(strk).left")) == 10);
	EXPECT(RINT(Eval("GetStrokeBounds(strk).top")) == 20);
	// GetStrokePoint fills in the frame it is given and answers it
	EXPECT(RINT(Eval("GetStrokePoint(strk, 0, {x: 0, y: 0}, 1).x")) == 10);
	EXPECT(RINT(Eval("GetStrokePoint(strk, 2, {x: 0, y: 0}, 1).y")) == 40);
	// a stroke is not a bundle and says so
	EXPECT(ISNIL(Eval("call func() begin try CountPoints(bndl) onexception |evt.ex| do nil end with ()")));

	// the writing as a shape, and the two `contains ink' questions
	Eval("shape := CompressStrokes(bndl)");
	EXPECT(NOTNIL(Eval("PolyContainsInk(shape)")));
	EXPECT(ISNIL(Eval("PolyContainsInk({})")));
	// CalcInkBounds works the box out again from the strokes themselves
	EXPECT(RINT(Eval("CalcInkBounds(shape).right")) - RINT(Eval("CalcInkBounds(shape).left")) > 0);
	EXPECT(NOTNIL(Eval("shape.viewBounds")));

	// an ink word, and what it says it measures - it takes its scale and
	// its pen from the writer's preferences as it is made
	Eval("userConfiguration.inkWordScaling := 100; userConfiguration.userPenSize := 3");
	Eval("word := StrokeBundleToInkWord(bndl)");
	Eval("info := GetInkWordInfo(word)");
	EXPECT(RINT(Eval("info.origWidth")) > 0);
	EXPECT(RINT(Eval("info.origAscent")) > 0);
	EXPECT(RINT(Eval("info.scale")) == 100);		// the preference this test sets
	EXPECT(RINT(Eval("info.origPenSize")) == 3);
	EXPECT(RINT(Eval("info.curWidth")) > 0);

	// a styles array with an ink word in it has writing; one without has not
	EXPECT(NOTNIL(Eval("StyleArrayContainsInk([1, word])")));
	EXPECT(ISNIL(Eval("StyleArrayContainsInk([1, 0x3000])")));
	EXPECT(ISNIL(Eval("StyleArrayContainsInk(nil)")));

	// A word of writing stands in a paragraph's text as one character,
	// kInkWordChar, whose style run holds the word itself; a rich string
	// uses kInkChar for the same thing.  So the two questions below are
	// about the *text*, not the styles, and the text has to have that
	// character in it.
	{
		UniChar written[3];
		written[0] = kInkWordChar;
		written[1] = U_CONST_CHAR('b');
		written[2] = 0;
		RefVar para(AllocateFrame());
		SetFrameSlot(para, RSSYMtext, RefVar(MakeString(written, 2)));
		RefVar styles(AllocateArray(RSSYMarray, 4));
		SetArraySlot(styles, 0, RefVar(MAKEINT(1)));
		SetArraySlot(styles, 1, RefVar(Eval("word")));
		SetArraySlot(styles, 2, RefVar(MAKEINT(1)));
		SetArraySlot(styles, 3, RefVar(MAKEINT(0x3000)));
		SetFrameSlot(para, RSSYMstyles, styles);
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "para")), para);

		// a rich string counts the ink words in it
		Eval("rich := MakeRichString(para.text, para.styles)");
		EXPECT(RINT(Eval("NumInkWordsInRange(rich, 0, nil)")) == 1);
		EXPECT(RINT(Eval("NumInkWordsInRange(rich, 1, 1)")) == 0);
		// and it measures as the ink word's own width plus the b's
		// (StrFontWidth measures a rich string, not its text alone)
		{
			InitializeInkFont();		// (the font engine's ink, as Notebook.cpp hands it over at boot)
			long withInk = RINT(Eval("StrFontWidth(rich, espy12)"));
			long letter = RINT(Eval("StrFontWidth(\"b\", espy12)"));
			EXPECT(withInk > letter + 4);
		}
		// and a field hands its own text and styles on as one (GetRichString,
		// a method of the view: its text and styles slots)
		RefVar fromField(FGetRichString(para));
		EXPECT(Length(fromField) == Length(RefVar(Eval("rich"))));
		EXPECT(memcmp(BinaryData(fromField), BinaryData(RefVar(Eval("rich"))), Length(fromField)) == 0);

		// and a paragraph's data frame says whether there is writing in it
		EXPECT(NOTNIL(Eval("ParaContainsInk(para)")));
		EXPECT(ISNIL(Eval("ParaContainsInk({text: \"ab\", styles: [2, 0x3000]})")));
	}

	// the try string: the few characters the writer has lately picked by
	// hand, which the word list reorders its guesses by
	Eval("ClearTryString()");
	EXPECT(RINT(Eval("TryStringLength()")) == 0);
	Eval("AddTryString($O); AddTryString($1)");
	EXPECT(RINT(Eval("TryStringLength()")) == 2);
	EXPECT(NOTNIL(Eval("InTryString($O)")) && ISNIL(Eval("InTryString($z)")));
	Eval("ClearTryString()");
	EXPECT(RINT(Eval("TryStringLength()")) == 0);

	Eval("bndl := nil; strk := nil; shape := nil; word := nil; info := nil; rich := nil; para := nil");
}


// stands in for that script and looks at what it is handed.
static void
TestInkOnThePage()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	TEditView* editor = (TEditView*) ViewOf("ctxIV := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 200, bottom: 150}, viewChildren: [], added: nil, viewAddChildScript: func(t) begin added := t; t end})");

	// a bundle of one stroke - the array is v, h, v, h, in pixels
	RefVar points(Eval("[20, 10, 30, 20, 40, 30]"));
	RefVar arrays(AllocateArray(RSSYMarray, 1));
	SetArraySlot(arrays, 0, points);
	RefVar bundle(MakeStrokeBundle(arrays, 1));
	EXPECT(CountStrokes(bundle) == 1);
	Rect box;
	GetBundleBounds(bundle, &box);
	EXPECT(box.left == 10 && box.top == 20 && box.right == 30 && box.bottom == 40);

	EXPECT(HandleInk(editor, bundle) == 1);
	RefVar added(Eval("ctxIV.added"));
	EXPECT(IsFrame(added));
	EXPECT(IsRawInk(RefVar(GetFrameSlot(added, RSSYMink))));
	Rect where;
	EXPECT(FromObject(RefVar(GetFrameSlot(added, RSSYMviewbounds)), where));
	// the shape sits where the strokes were, let out for the pen
	EXPECT(where.left <= 10 && where.right >= 30 && where.top <= 20 && where.bottom >= 40);

	// the same through the aeRawInk command, which the page takes itself
	// when no child will have it
	Eval("ctxIV.added := nil");
	RefVar cmd(MakeCommand(aeRawInk, editor, 0));
	CommandSetFrameParameter(cmd, bundle);
	gApplication->DispatchCommand(cmd);
	EXPECT(IsFrame(RefVar(Eval("ctxIV.added"))));

	// a view that takes only numbers says so; one that takes letters does not
	TView* numbers = ViewOf("ctxNum := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x2000, viewBounds: {left: 0, top: 0, right: 10, bottom: 10}})");
	TView* words = ViewOf("ctxWord := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x4000, viewBounds: {left: 0, top: 0, right: 10, bottom: 10}})");
	EXPECT(ViewExpectsNumbers(numbers));
	EXPECT(!ViewExpectsNumbers(words));
	Eval("RemoveView(GetRoot(), ctxNum); RemoveView(GetRoot(), ctxWord); RemoveView(GetRoot(), ctxIV)");
}

// A word of writing put on the page as a paragraph of its own: the
// aeInkWord command the word recogniser sends when it could not read
// the writing.  The paragraph holds one character, the ink word, and is
// placed on the line the writing was on.
static void
TestInkWordOnThePage()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();
	TEditView* editor = (TEditView*) ViewOf(
		"ctxIW := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, "
		"viewBounds: {left: 0, top: 0, right: 200, bottom: 150}, viewChildren: [], "
		"added: nil, viewAddChildScript: func(t) begin added := t; t end})");
	EXPECT(editor != nil);

	// a bundle of one stroke - the array is v, h, v, h, in pixels
	RefVar points(Eval("[20, 10, 30, 20, 40, 30]"));
	RefVar arrays(AllocateArray(RSSYMarray, 1));
	SetArraySlot(arrays, 0, points);
	RefVar bundle(MakeStrokeBundle(arrays, 1));
	Rect box;
	GetBundleBounds(bundle, &box);

	RefVar cmd(MakeCommand(aeInkWord, editor, 0));
	CommandSetFrameParameter(cmd, bundle);
	gApplication->DispatchCommand(cmd);

	RefVar added(Eval("ctxIW.added"));
	EXPECT(IsFrame(added));
	if (!IsFrame(added))
		return;
	// one character of text, and its style run is the ink word itself
	RefVar text(GetFrameSlot(added, RSSYMtext));
	EXPECT(IsString(text) && Ustrlen(GetCString(text)) == 1);
	EXPECT(*GetCString(text) == kInkWordChar);
	RefVar styles(GetFrameSlot(added, RSSYMstyles));
	EXPECT(IsArray(styles) && Length(styles) == 2);
	EXPECT(RINT(RefVar(GetArraySlot(styles, 0))) == 1);
	EXPECT(IsInkWord(RefVar(GetArraySlot(styles, 1))));

	// and it sits on the line the writing was on, measured as text is (by
	// TextBounds, through its ink font) from the left of the box it was
	// written in - the room it was given - with nothing on the page to
	// line it up with
	Rect where;
	EXPECT(FromObject(RefVar(GetFrameSlot(added, RSSYMviewbounds)), where));
	EXPECT(where.top == box.top);
	EXPECT(where.left == box.left);
	EXPECT(where.right > where.left && where.bottom > where.top);
	EXPECT(where.bottom - where.top < box.bottom - box.top + 20);

	Eval("RemoveView(GetRoot(), ctxIW)");
}

// A word of writing put in at the caret.  A page whose caret is in one
// of its own paragraphs takes the word into that paragraph rather than
// starting a new one where the writing happens to be - which is what
// lets a word written anywhere carry on the line being written - but
// only when the writer has asked for it.
static void
TestInkWordAtTheCaret()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();
	TEditView* editor = (TEditView*) ViewOf(
		"ctxIC := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, recConfig: rcInkOrTextConfig, "
		"viewBounds: {left: 0, top: 0, right: 200, bottom: 150}, viewChildren: [], "
		"added: nil, viewAddChildScript: func(t) begin added := t; t end})");
	EXPECT(editor != nil);
	TParagraphView* para = (TParagraphView*) ViewOf(
		"ctxICP := AddView(ctxIC, {viewClass: 81, viewFlags: 1, "
		"viewBounds: {left: 10, top: 10, right: 150, bottom: 40}, "
		"viewFont: espy12, text: \"ab\"})");
	EXPECT(para != nil && para->TextLength() == 2);
	// (the page's recognition keeps ink words - rcInkOrText - so the one
	// written into the paragraph stays one: on a page that does not,
	// ProcessStyles reads the ink words as they come - RangeChanged)
	Eval("ctxIC.added := nil");			// (adding the paragraph ran the script)
	Refresh();
	Eval("SetKeyView(ctxICP, 1)");
	EXPECT(gRootView->fCaretView == (TView*) para);

	// the writer has said writing may come from somewhere other than
	// the caret
	Eval("userConfiguration.remoteWriting := true");

	// a bundle of one stroke, written well away from the paragraph
	RefVar points(Eval("[100, 60, 110, 70, 120, 80]"));
	RefVar arrays(AllocateArray(RSSYMarray, 1));
	SetArraySlot(arrays, 0, points);
	RefVar cmd(MakeCommand(aeInkWord, editor, 0));
	CommandSetFrameParameter(cmd, RefVar(MakeStrokeBundle(arrays, 1)));
	gApplication->DispatchCommand(cmd);

	// no new paragraph: the word went into the one the caret was in,
	// where it stands as a single character
	EXPECT(ISNIL(Eval("ctxIC.added")));
	// "a<space><ink><space>b": a word written into the text is spaced
	// off from it like any other word
	EXPECT(para->TextLength() == 5);
	EXPECT(GetCString(RefVar(para->Text()))[2] == kInkWordChar);
	RefVar styles(para->Styles());
	Boolean carried = false;
	for (long i = 1; NOTNIL(styles) && i < Length(styles); i += 2)
		if (IsInkWord(RefVar(GetArraySlot(styles, i))))
			carried = true;
	EXPECT(carried);

	// without the preference, and with nothing hilited at the caret to
	// ask for it, the word starts a paragraph of its own again
	Eval("userConfiguration.remoteWriting := nil");
	cmd = MakeCommand(aeInkWord, editor, 0);
	CommandSetFrameParameter(cmd, RefVar(MakeStrokeBundle(arrays, 1)));
	gApplication->DispatchCommand(cmd);
	EXPECT(IsFrame(RefVar(Eval("ctxIC.added"))));
	EXPECT(para->TextLength() == 5);

	Eval("RemoveView(GetRoot(), ctxIC)");
	Refresh();
}

// A word the recogniser read, put on the page as a paragraph of its
// own.  Unlike an ink word it is measured - the text is laid out to find
// out how wide it is - and then lined up with whatever the page already
// has on it and with its ruled lines.
static void
TestRecognisedWord()
{
	TEditView* editor = (TEditView*) ViewOf(
		"ctxRW := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, "
		"viewBounds: {left: 20, top: 20, right: 300, bottom: 400}, viewChildren: [], "
		"added: nil, viewAddChildScript: func(t) begin added := t; t end})");
	EXPECT(editor != nil);
	if (editor == nil)
		return;

	// the font a page is written in, which the boot sets long before
	// anything is written on one (family 0, 12 point, plain)
	Eval("userConfiguration.userFont := 0 + (12 << 10)");

	TDomain* domain = TDomain::Make(gController, kWRecDomainType, (char*) "word");
	TWRecUnit* word = TWRecUnit::Make(domain, 1, nil);
	EXPECT(domain != nil && word != nil);
	if (word == nil)
		return;

	// the writing stood on a level base line from 60,200 to 120,200
	TUnitPublic pub(word, 0);
	pub.fWordBase.top = 200;
	pub.fWordBase.left = 60;
	pub.fWordBase.bottom = 200;
	pub.fWordBase.right = 120;

	UniChar text[3] = { 'h', 'i', 0 };
	Rect box;
	SetRect(&box, 60, 180, 120, 210);
	Rect room;
	SetRect(&room, 40, 100, 280, 380);
	RefVar info;
	long offset = 0;
	RefVar noInkFont;
	// (the add-child script above takes the template, so nothing is
	//  built from it: a starterParagraph form is built through the
	//  'para stationery, which only a booted Notepad has registered.
	//  What is checked here is the form, which is what the geometry
	//  makes.)
	editor->AddNewParagraph(text, 2, box, room, &pub, info, &offset, noInkFont);

	RefVar added(Eval("ctxRW.added"));
	EXPECT(IsFrame(added));
	if (!IsFrame(added))
		return;
	RefVar wrote(GetFrameSlot(added, RSSYMtext));
	EXPECT(IsString(wrote) && Ustrcmp(GetCString(wrote), text) == 0);
	// the unit's own style covers the whole word, which is one font for
	// the whole paragraph, so MakeParagraphForm writes it down as the
	// viewFont and drops the run
	EXPECT(ISNIL(RefVar(GetFrameSlot(added, RSSYMstyles))));
	EXPECT(RINT(RefVar(GetFrameSlot(added, RSSYMviewfont)))
		   == RINT(RefVar(Eval("userConfiguration.userFont"))));

	Rect where;
	EXPECT(FromObject(RefVar(GetFrameSlot(added, RSSYMviewbounds)), where));
	// it was measured, so it has a width of its own - unlike an ink
	// word, which is as wide as the writing was
	EXPECT(where.right > where.left);
	EXPECT(where.bottom > where.top);
	// and it stands on the line the writing stood on, in the editor's
	// own coordinates (the page starts at 20,20)
	EXPECT(where.top < 200 - 20 && where.bottom > 200 - 20 - 4);
	// it fits the room it was given, which the page's own bounds clip
	EXPECT(where.left >= 0 && where.right <= 280);

	word->Dispose();
	domain->Dispose();
	Eval("RemoveView(GetRoot(), ctxRW)");
}


// A word of writing inside a line of text: the paragraph's style run
// for it is the ink word itself, which the font engine opens as a font
// of one glyph, so the writing draws where the character would.
static void
TestInkWordInText()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	// a word: two strokes, so that it is plainly not a letter
	TStroke* list[3];
	list[0] = TStroke::Make(0);
	list[1] = TStroke::Make(0);
	list[2] = nil;
	for (long i = 0; i <= 10; i++)
	{
		TabPt tab;
		tab.z = 0;
		tab.p = 0;
		tab.x = ToFixed(10 + i * 2);
		tab.y = ToFixed(20 + i);
		list[0]->AddPoint(&tab);
		tab.x = ToFixed(34 + i * 2);
		tab.y = ToFixed(30 - i);
		list[1]->AddPoint(&tab);
	}
	list[0]->EndStroke();
	list[1]->EndStroke();
	Rect made;
	RefVar word(TStrokesToInkWord(list, &made));
	EXPECT(IsInkWord(word));

	// the text is the one character an ink word stands as, and the
	// styles say that character is the word
	RefVar text(AllocateBinary(RSSYMstring, 2 * (long) sizeof(UniChar)));
	UniChar* chars = (UniChar*) BinaryData(text);
	chars[0] = kInkWordChar;
	chars[1] = 0;
	RefVar styles(MakeArray(2));
	SetArraySlot(styles, 0, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 1, word);

	RefVar templ(AllocateFrame());
	SetFrameSlot(templ, RSSYMviewclass, RefVar(MAKEINT(clParagraphView)));
	// (a view whose recognition keeps ink words - rcInkOrText - keeps them:
	// in one that does not, ProcessStyles has them read when it is set up)
	SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(vVisible)));
	SetFrameSlot(templ, RSSYMrecconfig, RefVar(Rrcinkortext));
	Rect where;
	SetRect(&where, 5, 5, 110, 60);
	SetFrameSlot(templ, RSSYMviewbounds, RefVar(ToObject(where)));
	SetFrameSlot(templ, RSSYMtext, text);
	SetFrameSlot(templ, RSSYMstyles, styles);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "inkTempl")), templ);

	memset(gBits, 0, sizeof(gBits));
	TParagraphView* para = (TParagraphView*) ViewOf("ctxIW := AddView(GetRoot(), inkTempl)");
	EXPECT(para != nil && para->ClassID() == clParagraphView);
	Eval("ctxIW:Dirty()");
	Refresh();

	// the run's style is the word rather than a font, and the top bit of
	// its face says the word keeps a pen of its own
	EXPECT(para->LineCount() == 1);
	long lit = 0;
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
			if (Pixel(x, y) != 0)
				lit++;
	EXPECT(lit > 20);			// the writing, not an empty line

	// all of it inside the paragraph
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
			if (Pixel(x, y) != 0)
				EXPECT(x >= where.left && x < where.right && y >= where.top && y < where.bottom);

	// and the line is as tall as the word, not as a letter
	InkWordInfo info;
	GetInkWordInfo(word, &info);
	EXPECT((para->Line(0).fAscent + para->Line(0).fHeight) >= (long) (info.fScaledAscent + info.fScaledDescent));

	Eval("RemoveView(GetRoot(), ctxIW)");
	Refresh();
	list[0]->Dispose();
	list[1]->Dispose();
}


// Two words of writing joined into one by a caret drawn upside down
// across the space between them, which is the same gesture that closes
// up the space between two words of text.
static void
TestJoinInk()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	// two words, each a single stroke
	TStroke* first[2];
	first[0] = TStroke::Make(0);
	first[1] = nil;
	TStroke* second[2];
	second[0] = TStroke::Make(0);
	second[1] = nil;
	for (long i = 0; i <= 10; i++)
	{
		TabPt tab;
		tab.z = 0;
		tab.p = 0;
		tab.x = ToFixed(10 + i * 2);
		tab.y = ToFixed(20 + i);
		first[0]->AddPoint(&tab);
		tab.x = ToFixed(60 + i * 2);
		tab.y = ToFixed(30 - i);
		second[0]->AddPoint(&tab);
	}
	first[0]->EndStroke();
	second[0]->EndStroke();
	Rect made;
	RefVar wordA(TStrokesToInkWord(first, &made));
	RefVar wordB(TStrokesToInkWord(second, &made));
	EXPECT(IsInkWord(wordA) && IsInkWord(wordB));

	// a paragraph reading "<ink> <ink>": two ink word characters with a
	// space between them
	RefVar text(AllocateBinary(RSSYMstring, 4 * (long) sizeof(UniChar)));
	UniChar* chars = (UniChar*) BinaryData(text);
	chars[0] = kInkWordChar;
	chars[1] = ' ';
	chars[2] = kInkWordChar;
	chars[3] = 0;
	RefVar styles(MakeArray(6));
	SetArraySlot(styles, 0, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 1, wordA);
	SetArraySlot(styles, 2, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 3, RefVar(Eval("espy12")));
	SetArraySlot(styles, 4, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 5, wordB);

	RefVar templ(AllocateFrame());
	SetFrameSlot(templ, RSSYMviewclass, RefVar(MAKEINT(clParagraphView)));
	// (a view whose recognition keeps ink words - rcInkOrText - keeps them:
	// in one that does not, ProcessStyles has them read when it is set up)
	SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(vVisible)));
	SetFrameSlot(templ, RSSYMrecconfig, RefVar(Rrcinkortext));
	Rect where;
	SetRect(&where, 5, 5, 200, 60);
	SetFrameSlot(templ, RSSYMviewbounds, RefVar(ToObject(where)));
	SetFrameSlot(templ, RSSYMtext, text);
	SetFrameSlot(templ, RSSYMstyles, styles);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "joinTempl")), templ);

	TParagraphView* p = (TParagraphView*) ViewOf("ctxJI := AddView(GetRoot(), joinTempl)");
	EXPECT(p != nil && p->TextLength() == 3);
	Eval("ctxJI:Dirty()");
	Refresh();

	// the caret drawn upside down with an arm over each word
	Rect box;
	p->OffsetToBounds(0, &box);
	short baseline = (short) (p->Line(0).fBounds.top + p->Line(0).fAscent);
	Point armA, point, armB, tail;
	armA.h = (short) (box.left + 1);
	armA.v = baseline;
	p->OffsetToBounds(2, &box);
	armB.h = (short) (box.left + 1);
	armB.v = baseline;
	point.h = (short) ((armA.h + armB.h) / 2);
	point.v = (short) (baseline + 8);
	tail.v = (short) 0x8000;
	tail.h = 0;
	EXPECT(p->HandleCaret(2, 180, armA, point, armB, tail) == 1);

	// the two words and the space between them are now one word
	EXPECT(p->TextLength() == 1);
	RefVar joinedText(p->Text());
	EXPECT(*(const UniChar*) BinaryData(joinedText) == kInkWordChar);
	RefVar joinedStyles(Eval("ctxJI.styles"));
	EXPECT(IsArray(joinedStyles) && Length(joinedStyles) == 2);
	RefVar joined(GetArraySlot(joinedStyles, 1));
	EXPECT(IsInkWord(joined));
	// and the word that came out is wider than either of the two that
	// went in, being both of them side by side
	InkWordInfo one, all;
	GetInkWordInfo(wordA, &one);
	GetInkWordInfo(joined, &all);
	EXPECT(all.fWidth > one.fWidth);

	Eval("RemoveView(GetRoot(), ctxJI)");
	Refresh();
	first[0]->Dispose();
	second[0]->Dispose();
}


// A rich string with a word of writing in the middle of it: the string
// is cut into runs - text, ink, text - and the ink run's style carries
// the blob's address, which the font engine opens as a font of one
// glyph.
static void
TestInkInRichString()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	// a word of writing, and its bytes
	TStroke* list[2];
	list[0] = TStroke::Make(0);
	list[1] = nil;
	for (long i = 0; i <= 12; i++)
	{
		TabPt tab;
		tab.z = 0;
		tab.p = 0;
		tab.x = ToFixed(10 + i * 2);
		tab.y = ToFixed(20 + (i & 1) * 6);
		list[0]->AddPoint(&tab);
	}
	list[0]->EndStroke();
	Rect made;
	RefVar word(TStrokesToInkWord(list, &made));
	EXPECT(IsInkWord(word));
	long inkLength = Length(word);

	// "a?b" with the word where the ? is, laid out the way
	// SetFormatAndLength reads it: the text, padding to a word, the
	// blob (its length halfword, its bytes, padding), the trailer
	const long kChars = 3;
	long inkOffset = (kChars * (long) sizeof(UniChar) + 5) & ~3;
	long blob = (long) InkBlobSize((ULong) inkLength);
	RefVar str(AllocateBinary(RSSYMstring, inkOffset + blob + 4));
	char* base = (char*) BinaryData(str);
	UniChar* chars = (UniChar*) base;
	chars[0] = 'a';
	chars[1] = kInkChar;
	chars[2] = 'b';
	chars[3] = 0;
	*(UniChar*) (base + inkOffset) = (UniChar) inkLength;
	memcpy(base + inkOffset + sizeof(UniChar), BinaryData(word), (size_t) inkLength);
	ULong trailer = ((ULong) kChars << 4) | 1;
	UniChar* end = (UniChar*) (base + inkOffset + blob + 4);
	end[-2] = (UniChar) (trailer >> 16);
	end[-1] = (UniChar) trailer;

	TRichString rich(str);
	EXPECT(rich.Format() == kRichStringFormatInk);
	EXPECT(rich.Length() == kChars && rich.NumInkWords() == 1);

	// three runs: the letter, the writing, the letter
	EXPECT(rich.NumInkAndTextRunsInRange(0, kChars) == 3);
	short lengths[3];
	void* data[3];
	rich.GetLengthsAndDataInRange(0, kChars, lengths, data);
	EXPECT(lengths[0] == 1 && lengths[1] == 1 && lengths[2] == 1);
	EXPECT(data[0] == nil && data[1] != nil && data[2] == nil);
	// the run's data is the blob, and it reads back as the word
	EXPECT(*(const UniChar*) data[1] == (UniChar) inkLength);
	InkWordInfo blobInfo;
	GetInkWordAddrInfo(RefVar(AddressToRef(data[1])), &blobInfo);
	InkWordInfo wordInfo;
	GetInkWordInfo(word, &wordInfo);
	EXPECT(blobInfo.fWidth == wordInfo.fWidth && blobInfo.fAscent == wordInfo.fAscent);

	// the same characters without the ink region: a plain string
	RefVar plain(AllocateBinary(RSSYMstring, (kChars + 1) * (long) sizeof(UniChar)));
	UniChar* plainChars = (UniChar*) BinaryData(plain);
	plainChars[0] = 'a';
	plainChars[1] = kInkChar;
	plainChars[2] = 'b';
	plainChars[3] = 0;
	TRichString plainRich(plain);
	EXPECT(plainRich.Format() == kRichStringFormatPlain);

	StyleRecord style;
	CreateTextStyleRecord(RefVar(MAKEINT(PackFont(0, 12, 0))), &style);
	FPoint where;
	where.x = ToFixed(4);
	where.y = ToFixed(40);

	memset(gBits, 0, sizeof(gBits));
	DrawRichString(plainRich, 0, plainRich.Length(), &style, where, nil, nil);
	long withoutInk = 0;
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
			if (Pixel(x, y) != 0)
				withoutInk++;

	memset(gBits, 0, sizeof(gBits));
	DrawRichString(rich, 0, rich.Length(), &style, where, nil, nil);
	long withInk = 0;
	for (long y = 0; y < kHeight; y++)
		for (long x = 0; x < kWidth; x++)
			if (Pixel(x, y) != 0)
				withInk++;
	EXPECT(withoutInk > 0);					// the two letters
	EXPECT(withInk > withoutInk + 15);		// and the writing between them

	DisposeStyleRecord(&style);
	memset(gBits, 0, sizeof(gBits));
	list[0]->Dispose();
}


// A Unicode literal, for the delimiter rule below.
static UniChar*
Uni(const char* s)
{
	static UniChar gBuffers[2][32];
	static long gNext = 0;
	UniChar* out = gBuffers[gNext++ & 1];
	long i = 0;
	for (; s[i] != 0 && i < 31; i++)
		out[i] = U_CONST_CHAR((unsigned char) s[i]);
	out[i] = 0;
	return out;
}


// Things put into a paragraph from outside: a dropped clipping, a word
// the recogniser read, an ink word.  They all arrive as command 0x4d
// with a spec saying what and where, and the paragraph gathers them into
// one replacement - with a delimiter worked out between each pair, which
// is why two words dropped together come out spaced apart but a word and
// a comma do not.
static void
TestInsertItems()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	// the delimiter rule on its own
	UniChar delimiter[8];
	GetAppendDelimiter(delimiter, Uni("one"), Uni("two"), 3, 3);
	EXPECT(Ustrlen(delimiter) == 1 && delimiter[0] == U_CONST_CHAR(' '));
	GetAppendDelimiter(delimiter, Uni("one"), Uni(","), 3, 1);
	EXPECT(Ustrlen(delimiter) == 0);		// punctuation joins up
	GetAppendDelimiter(delimiter, Uni("one"), Uni("("), 3, 1);
	EXPECT(Ustrlen(delimiter) == 1);		// ... but an open bracket does not
	GetAppendDelimiter(delimiter, Uni("one-"), Uni("two"), 4, 3);
	EXPECT(Ustrlen(delimiter) == 0);		// a hyphen holds them together
	GetAppendDelimiter(delimiter, Uni("one "), Uni("two"), 4, 3);
	EXPECT(Ustrlen(delimiter) == 0);		// there is a space there already
	GetAppendDelimiter(delimiter, Uni("("), Uni("two"), 1, 3);
	EXPECT(Ustrlen(delimiter) == 0);
	GetAppendDelimiter(delimiter, Uni(""), Uni("two"), 0, 3);
	EXPECT(Ustrlen(delimiter) == 0);

	TParagraphView* p = (TParagraphView*) ViewOf(
		"ctxII := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, recConfig: rcInkOrTextConfig, "
		"viewBounds: {left: 5, top: 5, right: 150, bottom: 90}, "
		"viewFont: espy12, text: \"one four\"})");
	EXPECT(p != nil && p->ClassID() == clParagraphView);
	EXPECT(p->TextLength() == 8);
	Refresh();

	// two strings dropped in the middle: a space between them, and one
	// between them and what was already there
	RefVar items(MakeArray(2));
	SetArraySlot(items, 0, RefVar(MakeString("two")));
	SetArraySlot(items, 1, RefVar(MakeString("three")));
	RefVar spec(DoInsertItems(p, items, true, true, 4, 0, true, RefVar(NILREF)));
	EXPECT(NOTNIL(spec));
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("one two three four")) == 0);
	// and the spec comes back saying what actually went in: "two three "
	EXPECT(RINT(RefVar(GetFrameSlot(spec, RSSYMinsertoffset))) == 4);
	EXPECT(RINT(RefVar(GetFrameSlot(spec, RSSYMreplacechars))) == 10);

	// a comma needs no space in front of it
	Eval("SetValue(ctxII, 'text, \"one two\")");
	Refresh();
	DoInsertItems(p, RefVar(MakeString(",")), true, true, 7, 0, true,
				  RefVar(NILREF));
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("one two,")) == 0);

	// without addSpace nothing goes between them at all
	Eval("SetValue(ctxII, 'text, \"onetwo\")");
	Refresh();
	DoInsertItems(p, RefVar(MakeString("X")), false, true, 3, 0, true,
				  RefVar(NILREF));
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("oneXtwo")) == 0);

	// characters taken out and replaced
	Eval("SetValue(ctxII, 'text, \"one two\")");
	Refresh();
	DoInsertItems(p, RefVar(MakeString("six")), false, true, 4, 3, true,
				  RefVar(NILREF));
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("one six")) == 0);

	// two items in the same style come out as one run rather than two
	Eval("SetValue(ctxII, 'text, \"\")");
	Refresh();
	RefVar pair(MakeArray(2));
	SetArraySlot(pair, 0, RefVar(MakeString("aa")));
	SetArraySlot(pair, 1, RefVar(MakeString("bb")));
	DoInsertItems(p, pair, false, true, 0, 0, true, RefVar(NILREF));
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("aabb")) == 0);
	RefVar styles(p->Styles());
	EXPECT(ISNIL(styles) || (IsArray(styles) && Length(styles) == 2));

	// an ink word goes in as the one character it stands as, with the
	// word itself as the style of that character
	Eval("SetValue(ctxII, 'text, \"ab\")");
	Refresh();
	TStroke* list[2];
	list[0] = TStroke::Make(0);
	list[1] = nil;
	for (long i = 0; i <= 10; i++)
	{
		TabPt tab;
		tab.z = 0;
		tab.p = 0;
		tab.x = ToFixed(10 + i * 2);
		tab.y = ToFixed(20 + i);
		list[0]->AddPoint(&tab);
	}
	list[0]->EndStroke();
	Rect made;
	RefVar word(TStrokesToInkWord(list, &made));
	EXPECT(IsInkWord(word));
	DoInsertItems(p, word, false, true, 1, 0, true, RefVar(NILREF));
	EXPECT(p->TextLength() == 3);
	EXPECT(GetCString(RefVar(p->Text()))[1] == kInkWordChar);
	styles = p->Styles();
	Boolean carried = false;
	for (long i = 1; NOTNIL(styles) && i < Length(styles); i += 2)
		if (IsInkWord(RefVar(GetArraySlot(styles, i))))
			carried = true;
	EXPECT(carried);

	// a word-info frame the recogniser marked as ink: the writing goes
	// in the same way, and the frame is told where it landed so that the
	// corrector can find it again
	Eval("SetValue(ctxII, 'text, \"ab\")");
	Refresh();
	RefVar info(AllocateFrame());
	SetFrameSlot(info, RSSYMwords, RefVar(MakeArray(0)));
	SetFrameSlot(info, RSSYMflags, RefVar(MAKEINT(kWordInfoIsInk)));
	SetFrameSlot(info, RSSYMink, word);
	DoInsertItems(p, info, false, true, 1, 0, true, RefVar(NILREF));
	EXPECT(p->TextLength() == 3);
	EXPECT(GetCString(RefVar(p->Text()))[1] == kInkWordChar);
	// where it landed: HandleInsertItems writes the offsets down
	// relative to the text going in, and HandleReplaceText then moves
	// them to where that text went (the insertion point, 1)
	EXPECT(RINT(RefVar(GetFrameSlot(info, RSSYMstart))) == 1);
	EXPECT(RINT(RefVar(GetFrameSlot(info, RSSYMstop))) == 2);
	// noted as ink (8) and as a word the corrector knows about (2)
	EXPECT(RINT(RefVar(GetFrameSlot(info, RSSYMflags))) == 10);

	// ... and one it read goes in as the word itself
	Eval("SetValue(ctxII, 'text, \"ab\")");
	Refresh();
	RefVar read(AllocateFrame());
	RefVar words(MakeArray(1));
	RefVar reading(AllocateFrame());
	SetFrameSlot(reading, RSSYMword, RefVar(MakeString("hi")));
	SetArraySlot(words, 0, reading);
	SetFrameSlot(read, RSSYMwords, words);
	SetFrameSlot(read, RSSYMflags, RefVar(MAKEINT(0)));
	DoInsertItems(p, read, false, true, 1, 0, true, RefVar(NILREF));
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("ahib")) == 0);
	EXPECT(RINT(RefVar(GetFrameSlot(read, RSSYMstart))) == 1);
	EXPECT(RINT(RefVar(GetFrameSlot(read, RSSYMstop))) == 3);
	EXPECT(RINT(RefVar(GetFrameSlot(read, RSSYMflags))) == 2);

	// an item of a kind the paragraph has no use for is passed over
	Eval("SetValue(ctxII, 'text, \"ab\")");
	Refresh();
	DoInsertItems(p, RefVar(MAKEINT(42)), false, true, 1, 0, true, RefVar(NILREF));
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("ab")) == 0);

	// and the undo the insert made puts the paragraph back
	Eval("SetValue(ctxII, 'text, \"one four\")");
	Refresh();
	Eval("ClearUndoStacks()");
	DoInsertItems(p, RefVar(MakeString("two")), true, true, 4, 0, true,
				  RefVar(NILREF));
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("one two four")) == 0);
	Eval("Undo()");
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("one four")) == 0);

	list[0]->Dispose();
	Eval("RemoveView(GetRoot(), ctxII)");
	Refresh();
}


// A caret drawn over a word of writing cuts it in two rather than
// opening space in the text: the other answer the caret gesture has for
// a paragraph whose characters are ink.
static void
TestSplitInk()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	// a word of two strokes with a clear gap between them
	TStroke* list[3];
	list[0] = TStroke::Make(0);
	list[1] = TStroke::Make(0);
	list[2] = nil;
	for (long i = 0; i <= 10; i++)
	{
		TabPt tab;
		tab.z = 0;
		tab.p = 0;
		tab.x = ToFixed(10 + i);
		tab.y = ToFixed(20 + i);
		list[0]->AddPoint(&tab);
		tab.x = ToFixed(50 + i);
		tab.y = ToFixed(30 - i);
		list[1]->AddPoint(&tab);
	}
	list[0]->EndStroke();
	list[1]->EndStroke();
	Rect made;
	RefVar word(TStrokesToInkWord(list, &made));
	EXPECT(IsInkWord(word));

	// a paragraph holding it between two letters
	RefVar text(AllocateBinary(RSSYMstring, 4 * (long) sizeof(UniChar)));
	UniChar* chars = (UniChar*) BinaryData(text);
	chars[0] = U_CONST_CHAR('a');
	chars[1] = kInkWordChar;
	chars[2] = U_CONST_CHAR('b');
	chars[3] = 0;
	RefVar styles(MakeArray(6));
	SetArraySlot(styles, 0, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 1, RefVar(Eval("espy12")));
	SetArraySlot(styles, 2, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 3, word);
	SetArraySlot(styles, 4, RefVar(MAKEINT(1)));
	SetArraySlot(styles, 5, RefVar(Eval("espy12")));
	RefVar templ(AllocateFrame());
	SetFrameSlot(templ, RSSYMviewclass, RefVar(MAKEINT(clParagraphView)));
	// (a view whose recognition keeps ink words - rcInkOrText - keeps them:
	// in one that does not, ProcessStyles has them read when it is set up)
	SetFrameSlot(templ, RSSYMviewflags, RefVar(MAKEINT(vVisible)));
	SetFrameSlot(templ, RSSYMrecconfig, RefVar(Rrcinkortext));
	Rect where;
	SetRect(&where, 5, 5, 150, 60);
	SetFrameSlot(templ, RSSYMviewbounds, RefVar(ToObject(where)));
	SetFrameSlot(templ, RSSYMtext, text);
	SetFrameSlot(templ, RSSYMstyles, styles);
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "splitTempl")), templ);
	TParagraphView* para = (TParagraphView*) ViewOf("ctxSI := AddView(GetRoot(), splitTempl)");
	EXPECT(para != nil && para->TextLength() == 3);
	Eval("ctxSI:Dirty()");
	Refresh();

	// where the word sits on the page
	Rect box;
	EXPECT(IsInkWord(RefVar(para->GetInkRefAndBounds(1, &box))));
	Rect caret;
	para->OffsetToBounds(1, &caret);

	// a caret to the left of the offset means the character before it,
	// which is a letter: nothing happens
	Point pt;
	pt.v = (short) ((box.top + box.bottom) / 2);
	pt.h = (short) (caret.left - 2);
	EXPECT(para->CheckAndDoSplitInk(pt, 1) == 0);
	EXPECT(para->TextLength() == 3);

	// a caret through the middle of the word cuts it in two
	pt.h = (short) ((box.left + box.right) / 2);
	EXPECT(para->CheckAndDoSplitInk(pt, 1) == 1);
	// "a<space><ink><space><ink><space>b": the two halves go in as two
	// items, and DoInsertItems spaces them apart like any other pair
	EXPECT(para->TextLength() == 7);
	RefVar nowText(para->Text());
	const UniChar* now = GetCString(nowText);
	EXPECT(now[0] == U_CONST_CHAR('a') && now[2] == kInkWordChar
		   && now[4] == kInkWordChar && now[6] == U_CONST_CHAR('b'));
	EXPECT(now[1] == U_CONST_CHAR(' ') && now[3] == U_CONST_CHAR(' ')
		   && now[5] == U_CONST_CHAR(' '));
	// both halves are ink words of their own, and neither is the one
	// that went in
	RefVar after(para->Styles());
	long words = 0;
	for (long i = 1; NOTNIL(after) && i < Length(after); i += 2)
		if (IsInkWord(RefVar(GetArraySlot(after, i))))
		{
			words++;
			EXPECT(!EQRef(RefVar(GetArraySlot(after, i)), word));
		}
	EXPECT(words == 2);

	Eval("RemoveView(GetRoot(), ctxSI)");
	Refresh();
	list[0]->Dispose();
	list[1]->Dispose();
}


// Where a word written on the page falls in relation to a paragraph,
// which is the arithmetic that decides whether the word belongs to it.
static void
TestWordGeometry()
{
	// the margins: ten pixels to the left, thirty to the right, because
	// writing runs on past the right edge far more often than it starts
	// before the left one
	Rect room;
	SetRect(&room, 20, 10, 120, 50);
	AddMarginsToBounds(&room);
	EXPECT(room.left == 10 && room.right == 150
		   && room.top == 10 && room.bottom == 50);

	// side by side on the same line
	Rect first, second;
	Point base, nextBase;
	SetRect(&first, 20, 10, 60, 30);			// l, t, r, b
	base.h = 40;
	base.v = 28;
	SetRect(&second, 62, 10, 100, 30);
	nextBase.h = 80;
	nextBase.v = 28;
	EXPECT(AdjacentBoxes(first, second, base, nextBase, 100));
	// too far to the right
	SetRect(&second, 200, 10, 240, 30);
	EXPECT(!AdjacentBoxes(first, second, base, nextBase, 100));
	EXPECT(AdjacentBoxes(first, second, base, nextBase, 1000));
	// far enough left to be inside the first
	SetRect(&second, 50, 10, 90, 30);
	EXPECT(!AdjacentBoxes(first, second, base, nextBase, 100));
	// on the same line but with the base lines too far apart
	SetRect(&second, 62, 10, 100, 30);
	nextBase.v = 60;
	EXPECT(!AdjacentBoxes(first, second, base, nextBase, 100));
	nextBase.v = 28;
	// the empty box nothing is beside
	Rect empty;
	SetRect(&empty, -0x8000, -0x8000, -0x8000, -0x8000);
	EXPECT(!AdjacentBoxes(empty, second, base, nextBase, 1000));
	EXPECT(!AdjacentBoxes(first, empty, base, nextBase, 1000));

	// one box on the line above another
	Rect below;
	SetRect(&below, 20, 32, 60, 52);
	EXPECT(BoxAboveBox(first, below));
	// half a page down
	SetRect(&below, 20, 200, 60, 220);
	EXPECT(!BoxAboveBox(first, below));
	// on the same line
	EXPECT(!BoxAboveBox(first, second));
	EXPECT(!BoxAboveBox(empty, below));

	// a paragraph, and a word written over its last line
	TParagraphView* p = (TParagraphView*) ViewOf(
		"ctxWG := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, "
		"viewBounds: {left: 20, top: 10, right: 120, bottom: 40}, "
		"viewFont: espy12, text: \"one two\"})");
	EXPECT(p != nil);
	Eval("ctxWG:Dirty()");
	Refresh();
	Rect word;
	SetRect(&word, 30, p->viewBounds.bottom - 5, 70, p->viewBounds.bottom + 5);
	EXPECT(p->WordOnLastLine(word));
	SetRect(&word, 30, p->viewBounds.top - 40, 70, p->viewBounds.top - 30);
	EXPECT(!p->WordOnLastLine(word));

	// the last line's box is the line's, not the view's
	Rect last;
	p->BoundsOfLastLine(&last);
	EXPECT(p->LineCount() > 0);
	EXPECT(last.top == p->Line(p->LineCount() - 1).fBounds.top);

	// a word on the line after the text belongs to the paragraph; one
	// well below it does not
	// (the strip is measured from the bounds the lines were laid out in,
	//  which is the view's own, not from the text's)
	gLastAddedWordView = nil;
	long under = p->viewBounds.bottom;
	Point wordBase;
	wordBase.h = 40;
	wordBase.v = (short) (under + 8);
	SetRect(&word, 30, under + 2, 70, under + 12);
	EXPECT(p->WordOnLineBelowParagraph(word, wordBase));
	SetRect(&word, 30, under + 200, 70, under + 210);
	wordBase.v = (short) (under + 206);
	EXPECT(!p->WordOnLineBelowParagraph(word, wordBase));

	// ... unless it carries on from the word that last went in here
	Rect there;
	SetRect(&there, 30, under + 180, 70, under + 200);
	Point thereBase;
	thereBase.h = 50;
	thereBase.v = (short) (under + 196);
	p->SaveAddedUnitBounds(there, thereBase, 0);
	EXPECT(gLastAddedWordView == (TView*) p);
	EXPECT(GetLastAddedWordBox()->top == there.top);
	EXPECT(GetLastAddedWordBase()->v == thereBase.v);
	EXPECT(p->WordOnLineBelowParagraph(word, wordBase));
	gLastAddedWordView = nil;

	Eval("RemoveView(GetRoot(), ctxWG)");
	Refresh();
}


// A word the recogniser read, written over a paragraph that is already
// on the page: it goes into that paragraph's text rather than starting a
// new paragraph of its own, and the paragraph says how well it would
// take it before it is handed over.
static void
TestWordIntoParagraph()
{
	TEditView* editor = (TEditView*) ViewOf(
		"ctxWP := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, "
		"viewBounds: {left: 0, top: 0, right: 300, bottom: 200}, viewChildren: [], "
		"added: nil, viewAddChildScript: func(t) begin added := t; t end})");
	EXPECT(editor != nil);
	TParagraphView* para = (TParagraphView*) ViewOf(
		"ctxWPP := AddView(ctxWP, {viewClass: 81, viewFlags: 1, "
		"viewBounds: {left: 10, top: 10, right: 200, bottom: 40}, "
		"viewFont: espy12, text: \"one two\"})");
	EXPECT(para != nil && para->TextLength() == 7);
	Eval("ctxWP.added := nil");			// (adding the paragraph ran the script)
	Eval("ctxWPP:Dirty()");
	Refresh();
	gLastAddedWordView = nil;

	long right = para->TextBounds().right;
	long base = para->viewBounds.top + para->Line(0).fAscent;

	// asked on its own, the paragraph says how well it would take a word
	// written just past the end of its line
	Rect box;
	SetRect(&box, right + 6, base - 10, right + 30, base + 3);
	Point pt;
	pt.h = (short) (right + 18);
	pt.v = (short) base;
	UniChar text[4];
	text[0] = U_CONST_CHAR('t');
	text[1] = U_CONST_CHAR('e');
	text[2] = U_CONST_CHAR('n');
	text[3] = 0;
	RefVar none;
	long wants = para->HandleWord(text, 3, box, pt, 0, 0, none, false, nil, nil);
	EXPECT(wants > 0);
	// and nothing at all about a word written half a page below it
	Rect far;
	SetRect(&far, right + 6, base + 140, right + 30, base + 153);
	Point farPt;
	farPt.h = (short) (right + 18);
	farPt.v = (short) (base + 150);
	EXPECT(para->HandleWord(text, 3, far, farPt, 0, 0, none, false, nil, nil) == 0);

	// the same word offered to the page goes into the paragraph
	TDomain* domain = TDomain::Make(gController, kWRecDomainType, (char*) "word");
	TWRecUnit* unit = TWRecUnit::Make(domain, 1, nil);
	EXPECT(domain != nil && unit != nil);
	if (unit == nil)
		return;
	// the reading the engine came back with, so the unit has a word
	long interp = unit->AddWordInterpretation();
	EXPECT(interp == 0);
	UniChar reading[4];
	reading[0] = U_CONST_CHAR('t');
	reading[1] = U_CONST_CHAR('e');
	reading[2] = U_CONST_CHAR('n');
	reading[3] = 0;
	EXPECT(unit->SetWordString(0, reading) != nil);
	TUnitPublic pub(unit, 0);
	pub.fWordBase.top = (short) base;
	pub.fWordBase.bottom = (short) base;
	pub.fWordBase.left = (short) (right + 6);
	pub.fWordBase.right = (short) (right + 30);

	// the word info the engine would have left on the unit: the
	// ink-only engine here reads nothing, so it is filled in by hand as
	// a word that *was* read
	RefVar wordInfo(pub.WordInfo());
	SetWordList(wordInfo, RefVar(Eval("[\"ten\"]")));
	ClearWordInfoFlags(wordInfo, kWordInfoIsInk);

	Rect room = editor->viewBounds;
	RefVar info;
	long offset = -1;
	TView* into = editor->HandleWord(text, 3, box, room, &pub, info, &offset);
	EXPECT(into == (TView*) para);
	EXPECT(ISNIL(Eval("ctxWP.added")));		// no new paragraph
	EXPECT(NOTNIL(Eval("StrEqual(ctxWPP.text, \"one two ten\")")));
	// the word itself landed after the space that was put in front of it
	EXPECT(offset == 8);
	// and the paragraph remembers where the word went, so the next one
	// can carry on from it
	EXPECT(gLastAddedWordView == (TView*) para);
	EXPECT(gLastAddedWordEndOffset == 11);
	// and the word is registered with the machine, so the corrector can
	// still be asked about it: the entry covers where it landed
	RefVar registered(FindWordInfo(para, 8));
	EXPECT(IsFrame(registered));
	EXPECT(RINT(RefVar(GetFrameSlotRef(registered, RSSYMstart))) == 8);
	EXPECT(RINT(RefVar(GetFrameSlotRef(registered, RSSYMstop))) == 11);
	EXPECT(Ustrcmp(GetCString(RefVar(GetNthWord(registered, 0))), Uni("ten")) == 0);
	EXPECT(TestWordInfoFlags(registered, kWordInfoKnown));
	RemoveCorrectionInfo(para);

	// with remote writing on and the caret on the page just under the
	// paragraph, a word written far below goes onto the end of the
	// paragraph's text.  The ROM answers the word's text pointer as the
	// view (the ROM bug in HandleWord); fixed, the paragraph it went into
	Eval("userConfiguration.remoteWriting := true");
	for (int fixed = 0; fixed < 2; fixed++)
	{
		SetRomBugFixed(fixed != 0);
		Point under;
		under.h = (short) 15;
		under.v = (short) (para->viewBounds.bottom + 2);
		editor->PositionCaret(under, false);
		Rect caretRect;
		SetRect(&caretRect, 15, para->viewBounds.bottom + 2, 17, para->viewBounds.bottom + 14);
		editor->SetCaretRectGlobal(caretRect);
		EXPECT(gRootView->fCaretView == (TView*) editor);
		long before = para->TextLength();
		offset = -1;
		into = editor->HandleWord(text, 3, far, room, &pub, info, &offset);
		EXPECT(para->TextLength() > before);
		EXPECT(into == (fixed ? (TView*) para : (TView*) text));
	}
	SetRomBugFixed(true);
	Eval("userConfiguration.remoteWriting := nil");
	gRootView->SetKeyView(nil, 0, 0, false);
	RemoveCorrectionInfo(para);

	unit->Dispose();
	domain->Dispose();
	gLastAddedWordView = nil;
	Eval("RemoveView(GetRoot(), ctxWP)");
	Refresh();
}


// The caret on the page itself, just under a paragraph, with a word
// written somewhere else: the page starts a new line in that paragraph -
// a carriage return written into it at its bottom corner, which is what
// moves the caret in - and the word goes in at the caret.
static void
TestInkWordAtPageCaret()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();
	TEditView* editor = (TEditView*) ViewOf(
		"ctxPC := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, recConfig: rcInkOrTextConfig, "
		"viewBounds: {left: 0, top: 0, right: 300, bottom: 200}, viewChildren: [], "
		"added: nil, viewAddChildScript: func(t) begin added := t; t end})");
	EXPECT(editor != nil);
	TParagraphView* para = (TParagraphView*) ViewOf(
		"ctxPCP := AddView(ctxPC, {viewClass: 81, viewFlags: 1, "
		"viewBounds: {left: 10, top: 10, right: 120, bottom: 30}, "
		"viewFont: espy12, text: \"one\"})");
	EXPECT(para != nil && para->TextLength() == 3);
	Eval("ctxPC.added := nil");
	Refresh();
	gLastAddedWordView = nil;
	gLastAddedWordEndOffset = 0;

	// the caret put just under the paragraph: the page keeps it, because
	// the point is only just in the paragraph's text (score 2)
	Point pt;
	pt.h = (short) 15;
	pt.v = (short) (para->viewBounds.bottom + 2);
	editor->PositionCaret(pt, false);
	EXPECT(gRootView->fCaretView == (TView*) editor);
	// ... and moved down to where the page's ruling would put it, on
	// the line under the paragraph (which is what the grid path of
	// PositionCaret does on a ruled page)
	Rect caret;
	SetRect(&caret, 15, para->viewBounds.bottom + 2, 17, para->viewBounds.bottom + 14);
	editor->SetCaretRectGlobal(caret);

	Eval("userConfiguration.remoteWriting := true");
	RefVar points(Eval("[100, 60, 110, 70, 120, 80]"));
	RefVar arrays(AllocateArray(RSSYMarray, 1));
	SetArraySlot(arrays, 0, points);
	RefVar cmd(MakeCommand(aeInkWord, editor, 0));
	CommandSetFrameParameter(cmd, RefVar(MakeStrokeBundle(arrays, 1)));
	gApplication->DispatchCommand(cmd);
	Eval("userConfiguration.remoteWriting := nil");

	// no new paragraph: the return and the word both went into the one
	// that was there, and the word stands as a single character
	EXPECT(ISNIL(Eval("ctxPC.added")));
	EXPECT(para->TextLength() == 5);
	const UniChar* now = GetCString(RefVar(para->Text()));
	EXPECT(now[3] == 0x0d && now[4] == kInkWordChar);

	gLastAddedWordView = nil;
	Eval("RemoveView(GetRoot(), ctxPC)");
	Refresh();
}


// A polygon whose points' box is not its bounds (ValidatePoly): the ROM
// never sets the flag that says so (the ROM bug), so a closed curve keeps
// its verb; fixed, it becomes the plain closed polygon.  Bounds that
// match leave it alone either way.
static void
TestValidatePolyBounds()
{
	static const short xy[6][2] = { {0, 0}, {10, 0}, {10, 10}, {5, 12}, {0, 10}, {0, 0} };
	for (int fixed = 0; fixed < 2; fixed++)
	{
		SetRomBugFixed(fixed != 0);
		for (int matching = 0; matching < 2; matching++)
		{
			RefVar poly(AllocateBinary(RSSYMpolygonshape, 4 + 6 * sizeof(Point)));
			PolygonShape* shape = (PolygonShape*) BinaryData(poly);
			shape->fVerb = 6;
			shape->fCount = 6;
			for (int i = 0; i < 6; i++)
			{
				shape->fPoints[i].h = xy[i][0];
				shape->fPoints[i].v = xy[i][1];
			}
			Rect bounds;
			SetRect(&bounds, 0, 0, matching ? 10 : 50, matching ? 12 : 50);
			ValidatePoly(poly, bounds);
			shape = (PolygonShape*) BinaryData(poly);
			EXPECT(shape->fVerb == ((fixed && !matching) ? 4 : 6));
		}
	}
	SetRomBugFixed(true);
}


// Remote writing turned off while the corrector is up.  A word that
// arrives while the corrector's slip is on screen has to go where it was
// written, not to whatever caret the slip happens to hold.
static void
TestRemoteForCorrector()
{
	// nothing up and nothing on: nothing to save and nothing to put back
	Eval("userConfiguration.remoteWriting := nil");
	EXPECT(!CorrectorUp());
	EXPECT(SetRemoteForCorrector() == 0);
	EXPECT(ISNIL(Eval("userConfiguration.remoteWriting")));

	// remote writing on with the corrector down: it is left alone
	Eval("userConfiguration.remoteWriting := true");
	EXPECT(SetRemoteForCorrector() == 2);
	EXPECT(NOTNIL(Eval("userConfiguration.remoteWriting")));

	// the corrector up: remote writing is taken away for the duration
	// and put back afterwards
	Eval("GetRoot().correct := {viewCObject: 1}");
	EXPECT(CorrectorUp());
	ULong state = SetRemoteForCorrector();
	EXPECT(state == 3);
	EXPECT(ISNIL(Eval("userConfiguration.remoteWriting")));
	RestoreRemoteForCorrector(state);
	EXPECT(NOTNIL(Eval("userConfiguration.remoteWriting")));

	// the ROM's bug (NEWTON_ROM_BUGS=1): with the corrector up and remote
	// writing already off, nothing was taken away - but the restore tests
	// for either bit rather than both, so it switches remote writing ON
	SetRomBugFixed(false);
	Eval("userConfiguration.remoteWriting := nil");
	state = SetRemoteForCorrector();
	EXPECT(state == 1);
	EXPECT(ISNIL(Eval("userConfiguration.remoteWriting")));
	RestoreRemoteForCorrector(state);
	EXPECT(NOTNIL(Eval("userConfiguration.remoteWriting")));
	SetRomBugFixed(true);

	// fixed: nothing was taken away, so nothing is put back
	Eval("userConfiguration.remoteWriting := nil");
	state = SetRemoteForCorrector();
	EXPECT(state == 1);
	RestoreRemoteForCorrector(state);
	EXPECT(ISNIL(Eval("userConfiguration.remoteWriting")));
	// nor with the corrector down and remote writing on (state 2)
	Eval("userConfiguration.remoteWriting := true");
	RestoreRemoteForCorrector(2);
	EXPECT(NOTNIL(Eval("userConfiguration.remoteWriting")));

	Eval("userConfiguration.remoteWriting := nil");
	Eval("GetRoot().correct := nil");
	EXPECT(!CorrectorUp());
}


// A rich string - a string with writing in it - dropped into a
// paragraph: it comes apart into the two halves a paragraph keeps, so
// the writing survives the move instead of being dropped.
static void
TestRichStringIntoParagraph()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	// a word of writing, and a rich string "a?b" holding it
	TStroke* list[2];
	list[0] = TStroke::Make(0);
	list[1] = nil;
	for (long i = 0; i <= 12; i++)
	{
		TabPt tab;
		tab.z = 0;
		tab.p = 0;
		tab.x = ToFixed(10 + i * 2);
		tab.y = ToFixed(20 + (i & 1) * 6);
		list[0]->AddPoint(&tab);
	}
	list[0]->EndStroke();
	Rect made;
	RefVar word(TStrokesToInkWord(list, &made));
	EXPECT(IsInkWord(word));
	long inkLength = Length(word);

	const long kChars = 3;
	long inkOffset = (kChars * (long) sizeof(UniChar) + 5) & ~3;
	long blob = (long) InkBlobSize((ULong) inkLength);
	RefVar str(AllocateBinary(RSSYMstring, inkOffset + blob + 4));
	char* base = (char*) BinaryData(str);
	UniChar* chars = (UniChar*) base;
	chars[0] = U_CONST_CHAR('a');
	chars[1] = kInkChar;
	chars[2] = U_CONST_CHAR('b');
	chars[3] = 0;
	*(UniChar*) (base + inkOffset) = (UniChar) inkLength;
	memcpy(base + inkOffset + sizeof(UniChar), BinaryData(word), (size_t) inkLength);
	ULong trailer = ((ULong) kChars << 4) | 1;
	UniChar* end = (UniChar*) (base + inkOffset + blob + 4);
	end[-2] = (UniChar) (trailer >> 16);
	end[-1] = (UniChar) trailer;
	EXPECT(NOTNIL(str) && IsRichString(str));

	// spoken, it is a codec frame whose samples are its letters without the
	// ink (sound/SoundSettings.h's ConvertToSoundFrame over FStripInk), the
	// string itself left alone
	{
		RefVar frame(FConvertToSoundFrame(RefVar(), str));
		RefVar samples(GetFrameSlotRef(frame, RSSYMsamples));
		TRichString spoken(samples);
		EXPECT(spoken.Length() == 2 && spoken.GetChar(0) == U_CONST_CHAR('a') && spoken.GetChar(1) == U_CONST_CHAR('b'));
		EXPECT(TRichString(str).GetChar(1) == kInkChar);
	}

	// the two halves on their own
	{
		TRichString rich(str);
		RefVar text(rich.MakeParagraphTextSlot());
		EXPECT(IsString(text) && Ustrlen(GetCString(text)) == 3);
		const UniChar* got = GetCString(text);
		EXPECT(got[0] == U_CONST_CHAR('a') && got[1] == kInkWordChar
			   && got[2] == U_CONST_CHAR('b'));
		RefVar plain(MAKEINT(0x1234));
		RefVar styles(rich.MakeParagraphStylesSlot(plain));
		// three runs: the letter, the writing, the letter
		EXPECT(IsArray(styles) && Length(styles) == 6);
		EXPECT(RINT(RefVar(GetArraySlot(styles, 0))) == 1);
		EXPECT(EQRef(RefVar(GetArraySlot(styles, 1)), plain));
		EXPECT(RINT(RefVar(GetArraySlot(styles, 2))) == 1);
		EXPECT(IsInkWord(RefVar(GetArraySlot(styles, 3))));
		EXPECT(Length(RefVar(GetArraySlot(styles, 3))) == inkLength);
		EXPECT(RINT(RefVar(GetArraySlot(styles, 4))) == 1);
		EXPECT(EQRef(RefVar(GetArraySlot(styles, 5)), plain));
	}
	// a plain string is one run over the whole of it
	{
		TRichString flat(RefVar(MakeString("hello")));
		RefVar plain(MAKEINT(0x1234));
		RefVar styles(flat.MakeParagraphStylesSlot(plain));
		EXPECT(IsArray(styles) && Length(styles) == 2);
		EXPECT(RINT(RefVar(GetArraySlot(styles, 0))) == 5);
	}

	// and dropped into a paragraph
	TParagraphView* p = (TParagraphView*) ViewOf(
		"ctxRS := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, recConfig: rcInkOrTextConfig, "
		"viewBounds: {left: 5, top: 5, right: 150, bottom: 60}, "
		"viewFont: espy12, text: \"\"})");
	EXPECT(p != nil);
	Refresh();
	DoInsertItems(p, str, false, true, 0, 0, true, RefVar(NILREF));
	EXPECT(p->TextLength() == 3);
	const UniChar* now = GetCString(RefVar(p->Text()));
	EXPECT(now[0] == U_CONST_CHAR('a') && now[1] == kInkWordChar
		   && now[2] == U_CONST_CHAR('b'));
	RefVar kept(p->Styles());
	Boolean carried = false;
	for (long i = 1; NOTNIL(kept) && i < Length(kept); i += 2)
		if (IsInkWord(RefVar(GetArraySlot(kept, i))))
			carried = true;
	EXPECT(carried);
	// a piece of it taken out again comes back rich, the ink word with its
	// character (ExtractRichStringFromParaSlots over MakeRichString)
	{
		RefVar piece(ExtractRichStringFromParaSlots(RefVar(p->Text()), kept, 1, 2));
		EXPECT(IsRichString(piece));
		TRichString richPiece(piece);
		EXPECT(richPiece.Length() == 2 && richPiece.GetChar(1) == U_CONST_CHAR('b'));
		RefVar plainPiece(ExtractRichStringFromParaSlots(RefVar(p->Text()), kept, 2, 1));
		EXPECT(!IsRichString(plainPiece) && GetCString(plainPiece)[0] == U_CONST_CHAR('b'));
	}

	Eval("RemoveView(GetRoot(), ctxRS)");
	Refresh();
	list[0]->Dispose();
}


// The correction information: what the machine remembers about the
// words already on a page, so that one can still be corrected long
// after it was written.
static void
TestCorrectInfo()
{
	InitializeInkCodecs();
	InitializeParagraphCompression();
	InitializeInkFont();

	// the list the boot makes, emptied
	Eval("correctInfo := {}");
	InitCorrection();
	RefVar list(CorrectInfo());
	EXPECT(IsFrame(list));
	EXPECT(IsArray(RefVar(GetFrameSlotRef(list, RSSYMinfo))));
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 0);
	EXPECT(RINT(RefVar(GetFrameSlotRef(list, RSSYMmax))) == 0x28);

	// one reading, and a word info around it
	RefVar interp(MakeWordInterp(RefVar(MakeString("hello")), 7, 1, kWordLabelWord));
	EXPECT(IsFrame(interp));
	EXPECT(RINT(RefVar(GetFrameSlotRef(interp, RSSYMscore))) == 7);
	EXPECT(RINT(RefVar(GetFrameSlotRef(interp, RSSYMindex))) == 1);
	EXPECT(RINT(RefVar(GetFrameSlotRef(interp, RSSYMlabel))) == kWordLabelWord);

	RefVar info(MakeWordInfo(RefVar(MakeString("hello"))));
	EXPECT(IsFrame(info));
	EXPECT(Length(RefVar(GetFrameSlotRef(info, RSSYMwords))) == 1);
	RefVar first(GetNthWord(info, 0));
	EXPECT(IsString(first) && Ustrcmp(GetCString(first), Uni("hello")) == 0);
	EXPECT(ISNIL(RefVar(GetNthWord(info, 3))));
	// and one made of writing instead carries the strokes and no reading
	RefVar bundleArrays(AllocateArray(RSSYMarray, 1));
	SetArraySlot(bundleArrays, 0, RefVar(Eval("[20, 10, 30, 20]")));
	RefVar bundle(MakeStrokeBundle(bundleArrays, 1));
	RefVar written(MakeWordInfo(bundle));
	EXPECT(Length(RefVar(GetFrameSlotRef(written, RSSYMwords))) == 0);
	EXPECT(NOTNIL(RefVar(GetFrameSlotRef(written, RSSYMstrokes))));

	// the flags
	SetFrameSlot(info, RSSYMflags, RefVar(MAKEINT(0)));
	SetWordInfoFlags(info, kWordInfoKnown | kWordInfoAutoAdded);
	EXPECT(TestWordInfoFlags(info, kWordInfoKnown));
	EXPECT(TestWordInfoFlags(info, kWordInfoKnown | kWordInfoAutoAdded));
	EXPECT(!TestWordInfoFlags(info, kWordInfoIsInk));
	// AutoRemove takes the "added to the dictionary" flag off again
	AutoRemove(info);
	EXPECT(!TestWordInfoFlags(info, kWordInfoAutoAdded));
	EXPECT(TestWordInfoFlags(info, kWordInfoKnown));

	// a paragraph, and a word info that says where in it the word went
	TParagraphView* p = (TParagraphView*) ViewOf(
		"ctxCI := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, viewId: 4242, "
		"viewBounds: {left: 5, top: 5, right: 150, bottom: 60}, "
		"viewFont: espy12, text: \"one two three\"})");
	EXPECT(p != nil);
	Refresh();
	SetOffsetInfo(info, p, 4, 7, kWordInfoKnown);
	EXPECT(RINT(RefVar(GetFrameSlotRef(info, RSSYMid))) == p->fId);
	EXPECT(RINT(RefVar(GetFrameSlotRef(info, RSSYMstart))) == 4);
	EXPECT(RINT(RefVar(GetFrameSlotRef(info, RSSYMstop))) == 7);

	// it goes on the list, and can be found again from any offset it covers
	AddWordInfo(list, info);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);
	EXPECT(FindWordInfoIndex(list, p, 4) == 0);
	EXPECT(FindWordInfoIndex(list, p, 6) == 0);
	EXPECT(FindWordInfoIndex(list, p, 7) == -1);		// stop is past the end
	EXPECT(FindWordInfoIndex(list, p, 3) == -1);
	EXPECT(EQRef(RefVar(FindWordInfo(list, p, 5)), info));
	EXPECT(EQRef(RefVar(FindWordInfo(p, 5)), info));	// the machine's own list
	EXPECT(ISNIL(RefVar(FindWordInfo(p, 0))));
	// MoveCorrectionInfo(from, offset, to): the offset goes to FindWordInfo
	// as its Ref, four times over (a ROM bug kept), so offset 1 finds the
	// word at 4; the new offset is never passed (the native table's count
	// is one short) and the host's stand-in, nil, puts it at the Ref of nil
	Eval("ctxCI.moveIt := func() MoveCorrectionInfo(ctxCI, 1, ctxCI)");
	Eval("ctxCI:moveIt()");
	EXPECT(RINT(RefVar(GetFrameSlotRef(info, RSSYMstart))) == NILREF);
	EXPECT(RINT(RefVar(GetFrameSlotRef(info, RSSYMstop))) == NILREF + MAKEINT(3));
	SetOffsetInfo(info, p, 4, 7, kWordInfoKnown);

	// a reading that is not a single word is not kept: the corrector has
	// nothing to offer for it
	RefVar two(MakeWordInfo(RefVar(MakeString("one two"))));
	SetOffsetInfo(two, p, 0, 7, kWordInfoKnown);
	AddWordInfo(list, two);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);

	// a word info for a stretch of text nobody wrote: its one reading is
	// the characters themselves
	RefVar typed(MakeWordInfo(p, 8, 5));
	EXPECT(IsFrame(typed));
	RefVar read(GetNthWord(typed, 0));
	EXPECT(IsString(read) && Ustrcmp(GetCString(read), Uni("three")) == 0);

	// the readings replaced wholesale
	SetWordList(info, RefVar(Eval("[\"hello\", \"hallo\"]")));
	EXPECT(Length(RefVar(GetFrameSlotRef(info, RSSYMwords))) == 2);
	EXPECT(Ustrcmp(GetCString(RefVar(GetNthWord(info, 1))), Uni("hallo")) == 0);
	SetWordList(info, RefVar(NILREF));
	EXPECT(ISNIL(RefVar(GetFrameSlotRef(info, RSSYMwords))));

	InitCorrection();
	EXPECT(Length(RefVar(GetFrameSlotRef(RefVar(CorrectInfo()), RSSYMinfo))) == 0);
	Eval("RemoveView(GetRoot(), ctxCI)");
	Refresh();
}


// The correction information keeping up with the text.  Every edit of a
// paragraph has to be answered, or a word's alternatives would be
// offered for whatever now happens to sit at its offsets.
static void
TestCorrectInfoEditing()
{
	Eval("correctInfo := {}");
	InitCorrection();
	RefVar list(CorrectInfo());
	TParagraphView* p = (TParagraphView*) ViewOf(
		"ctxCE := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, "
		"viewBounds: {left: 5, top: 5, right: 200, bottom: 60}, "
		"viewFont: espy12, text: \"one two three\"})");
	EXPECT(p != nil && p->TextLength() == 13);
	Refresh();

	// an entry over "two" (4..7) and one over "three" (8..13)
	RefVar two(MakeWordInfo(RefVar(MakeString("two"))));
	SetOffsetInfo(two, p, 4, 7, kWordInfoKnown);
	AddWordInfo(list, two);
	RefVar three(MakeWordInfo(RefVar(MakeString("three"))));
	SetOffsetInfo(three, p, 8, 13, kWordInfoKnown);
	AddWordInfo(list, three);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 2);

	// two characters put in at the front: both move along, neither is
	// dropped
	OffsetCorrectionInfo(list, p, 0, 0, 2);
	EXPECT(RINT(RefVar(GetFrameSlotRef(two, RSSYMstart))) == 6);
	EXPECT(RINT(RefVar(GetFrameSlotRef(two, RSSYMstop))) == 9);
	EXPECT(RINT(RefVar(GetFrameSlotRef(three, RSSYMstart))) == 10);
	// and the word can still be found at its new place
	EXPECT(EQRef(RefVar(FindWordInfo(list, p, 7)), two));
	EXPECT(ISNIL(RefVar(FindWordInfo(list, p, 4))));

	// an edit that straddles the first entry takes it away; the one
	// after it moves
	OffsetCorrectionInfo(list, p, 7, 1, 1);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);
	EXPECT(EQRef(RefVar(GetArraySlot(RefVar(GetFrameSlotRef(list, RSSYMinfo)), 0)), three));

	// an entry belonging to another view is left alone
	RefVar elsewhere(MakeWordInfo(RefVar(MakeString("x"))));
	SetOffsetInfo(elsewhere, gRootView, 0, 1, kWordInfoKnown);
	AddWordInfo(list, elsewhere);
	OffsetCorrectionInfo(list, p, 0, 0, 5);
	EXPECT(RINT(RefVar(GetFrameSlotRef(elsewhere, RSSYMstart))) == 0);
	EXPECT(RINT(RefVar(GetFrameSlotRef(elsewhere, RSSYMstop))) == 1);

	// a backspace that closes the gap between two words joins their
	// entries back into one
	InitCorrection();
	list = CorrectInfo();
	RefVar left(MakeWordInfo(RefVar(MakeString("one"))));
	SetOffsetInfo(left, p, 0, 3, kWordInfoKnown);
	AddWordInfo(list, left);
	RefVar right(MakeWordInfo(RefVar(MakeString("two"))));
	SetOffsetInfo(right, p, 4, 7, kWordInfoKnown);
	AddWordInfo(list, right);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 2);
	OffsetCorrectionInfo(list, p, 3, 1, 0);		// the space at 3 removed
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);
	// neither carries writing, so the second is simply dropped and the
	// first keeps its own range: the ROM only joins the ranges and the
	// words when both entries have strokes to join
	EXPECT(RINT(RefVar(GetFrameSlotRef(left, RSSYMstart))) == 0);
	EXPECT(RINT(RefVar(GetFrameSlotRef(left, RSSYMstop))) == 3);

	// with writing on both, the ranges and the words do join
	InitCorrection();
	list = CorrectInfo();
	RefVar arrays(AllocateArray(RSSYMarray, 1));
	SetArraySlot(arrays, 0, RefVar(Eval("[20, 10, 30, 20]")));
	RefVar wrote(MakeWordInfo(RefVar(MakeStrokeBundle(arrays, 1))));
	SetWordList(wrote, RefVar(Eval("[\"on\"]")));
	SetOffsetInfo(wrote, p, 0, 2, kWordInfoKnown);
	AddWordInfo(list, wrote);
	RefVar arrays2(AllocateArray(RSSYMarray, 1));
	SetArraySlot(arrays2, 0, RefVar(Eval("[40, 10, 50, 20]")));
	RefVar wrote2(MakeWordInfo(RefVar(MakeStrokeBundle(arrays2, 1))));
	SetWordList(wrote2, RefVar(Eval("[\"e\"]")));
	SetOffsetInfo(wrote2, p, 3, 4, kWordInfoKnown);
	AddWordInfo(list, wrote2);
	OffsetCorrectionInfo(list, p, 2, 1, 0);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);
	EXPECT(RINT(RefVar(GetFrameSlotRef(wrote, RSSYMstop))) == 3);
	EXPECT(Ustrcmp(GetCString(RefVar(GetNthWord(wrote, 0))), Uni("one")) == 0);
	// the two lots of writing are one bundle now
	EXPECT(Length(RefVar(GetFrameSlotRef(RefVar(GetFrameSlotRef(wrote, RSSYMstrokes)), RSSYMstrokes))) == 2);
	// and the merged entry is marked as one the corrector knows
	EXPECT(TestWordInfoFlags(wrote, kWordInfoKnown));

	// a range taken away and put back, which is what an undo does
	InitCorrection();
	list = CorrectInfo();
	RefVar word(MakeWordInfo(RefVar(MakeString("two"))));
	SetOffsetInfo(word, p, 4, 7, kWordInfoKnown);
	AddWordInfo(list, word);
	RefVar taken(ExtractRange(list, p, 4, 7));
	EXPECT(NOTNIL(taken));
	EXPECT(Length(RefVar(GetFrameSlotRef(taken, RSSYMinfo))) == 1);
	EXPECT(ISNIL(RefVar(ExtractRange(list, p, 20, 30))));
	// rebased to the start of the range, as HandleReplaceText rebases it
	OffsetCorrectionInfo(taken, p, 0, 4, 0);
	RefVar copy(GetArraySlot(RefVar(GetFrameSlotRef(taken, RSSYMinfo)), 0));
	EXPECT(RINT(RefVar(GetFrameSlotRef(copy, RSSYMstart))) == 0);
	EXPECT(RINT(RefVar(GetFrameSlotRef(copy, RSSYMstop))) == 3);
	// and back, at a different place
	InitCorrection();
	list = CorrectInfo();
	OffsetCorrectionInfo(taken, nil, 0, 0, 9);
	InsertRange(list, taken, p);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);
	EXPECT(RINT(RefVar(GetFrameSlotRef(RefVar(FindWordInfo(list, p, 9)), RSSYMstop))) == 12);

	// entries the recogniser never proposed anything for are cleared out
	InitCorrection();
	list = CorrectInfo();
	RefVar madeUp(Clone(RefVar(Rprotowordinfo)));
	RefVar only(MakeArray(1));
	SetArraySlot(only, 0, RefVar(MakeWordInterp(RefVar(MakeString("aa")), 0, -1, 0)));
	SetFrameSlot(madeUp, RSSYMwords, only);
	SetOffsetInfo(madeUp, p, 0, 2, 0);
	AddWordInfo(list, madeUp);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);
	ClearEmptyEntries(list);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 0);

	// GetWordInfo finds the entry for a range, or makes one
	InitCorrection();
	list = CorrectInfo();
	RefVar made(GetWordInfo(list, p, 4, 3));
	EXPECT(IsFrame(made));
	EXPECT(Ustrcmp(GetCString(RefVar(GetNthWord(made, 0))), Uni("two")) == 0);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);
	EXPECT(EQRef(RefVar(GetWordInfo(list, p, 4, 3)), made));	// found, not made again
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 1);

	// and the whole view's entries go when it does
	RemoveCorrectionInfo(list, p);
	EXPECT(Length(RefVar(GetFrameSlotRef(list, RSSYMinfo))) == 0);

	InitCorrection();
	Eval("RemoveView(GetRoot(), ctxCE)");
	Refresh();
}


// A letter written over a letter of a word replaces it - the strongest
// claim a paragraph can make on a piece of writing, and the one that
// stops the edit view asking anybody else.
static void
TestReplaceCharacter()
{
	Eval("correctInfo := {}");
	InitCorrection();
	Eval("userConfiguration.remoteWriting := nil");
	TParagraphView* p = (TParagraphView*) ViewOf(
		"ctxRC := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, "
		"viewBounds: {left: 5, top: 5, right: 200, bottom: 60}, "
		"viewFont: espy12, text: \"cat\"})");
	EXPECT(p != nil && p->TextLength() == 3);
	Refresh();
	gLastAddedWordView = nil;
	gLastReplacedIndex = 100;

	// where the middle letter is
	Rect middle;
	p->OffsetToBounds(1, &middle);
	EXPECT(middle.right > middle.left);

	// a unit that read "o", written over that letter
	TDomain* domain = TDomain::Make(gController, kWRecDomainType, (char*) "word");
	TWRecUnit* unit = TWRecUnit::Make(domain, 1, nil);
	EXPECT(domain != nil && unit != nil);
	if (unit == nil)
		return;
	EXPECT(unit->AddWordInterpretation() == 0);
	UniChar reading[2];
	reading[0] = U_CONST_CHAR('o');
	reading[1] = 0;
	EXPECT(unit->SetWordString(0, reading) != nil);
	// how sure the engine is: an interpretation starts at the worst
	// score there is, and a replacement only happens for a better one
	unit->GetInterpretation(0)->score = 100;
	unit->GetInterpretation(0)->label = kWordLabelWord;
	// nothing has been written since (the controller still holds the
	// pieces earlier tests made): a correction is only a correction
	// while the writer has not moved on
	unit->fMaxStroke = 0xfffe;
	TUnitPublic pub(unit, 0);
	// the word recogniser's own unit type, which the readings are only
	// gathered for (SetWordRecognizer sets it on a real machine)
	ULong wasWordID = gWordID;
	gWordID = kWRecDomainType;

	Finder finder;
	memset(&finder, 0, sizeof(finder));
	SetRect(&finder.fBox, middle.left + 1, p->viewBounds.top + 1,
			middle.right - 1, p->viewBounds.top + 14);
	finder.fBase.h = (short) ((middle.left + middle.right) / 2);
	finder.fBase.v = (short) (p->viewBounds.top + 12);
	finder.fText = reading;
	finder.fLength = 1;
	finder.fUnit = &pub;
	finder.fReallyDoIt = true;

	EXPECT(p->FindWordInRun(&finder));
	// the finder came back with the strongest claim
	EXPECT(finder.fExact);
	EXPECT(finder.fView == p);
	EXPECT(finder.fOffset == 1);
	EXPECT(finder.fReplaceLength == 1);
	// and the word was replaced rather than the letter: "cat" -> "cot"
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("cot")) == 0);
	// the correction entry covers the new word and offers it
	RefVar entry(FindWordInfo(p, 1));
	EXPECT(IsFrame(entry));
	if (IsFrame(entry))
	{
		EXPECT(RINT(RefVar(GetFrameSlotRef(entry, RSSYMstart))) == 0);
		EXPECT(RINT(RefVar(GetFrameSlotRef(entry, RSSYMstop))) == 3);
		EXPECT(Ustrcmp(GetCString(RefVar(GetNthWord(entry, 0))), Uni("cot")) == 0);
	}
	// the replacement is remembered, so writing over the same letter
	// again goes on choosing between the readings rather than starting
	// the choice over
	EXPECT(gLastReplacedIndex == 1);
	EXPECT(gLastReplacedWord != nil && Ustrcmp(gLastReplacedWord, Uni("cot")) == 0);

	// writing that covers more than three characters is not a
	// replacement at all
	Eval("SetValue(ctxRC, 'text, \"cat\")");
	Refresh();
	Finder wide;
	memset(&wide, 0, sizeof(wide));
	Rect all;
	p->OffsetToBounds(0, &all);
	Rect last;
	p->OffsetToBounds(2, &last);
	SetRect(&wide.fBox, all.left, p->viewBounds.top + 1, last.right + 40,
			p->viewBounds.top + 14);
	wide.fBase.h = (short) all.left;
	wide.fBase.v = (short) (p->viewBounds.top + 12);
	wide.fText = reading;
	wide.fLength = 1;
	wide.fUnit = &pub;
	wide.fReallyDoIt = true;
	EXPECT(!p->ReplaceCharacter(&p->Line(0), p->fTextObjects[p->Line(0).fFirstObj], &wide));
	EXPECT(!wide.fExact);
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), Uni("cat")) == 0);

	gWordID = wasWordID;
	unit->Dispose();
	domain->Dispose();
	InitCorrection();
	gLastAddedWordView = nil;
	Eval("RemoveView(GetRoot(), ctxRC)");
	Refresh();
}


// The clipboard: a clipping made by hand from a script, the two views
// it comes to on the root, the icon's geometry, what a drag off it is
// offered, and the clipping thrown away again.
static void
TestClipboard()
{
	Eval("vars.displayParams := {appAreaGlobalLeft: 0, appAreaGlobalTop: 0, appAreaWidth: 160, appAreaHeight: 100}");
	Eval("userConfiguration.userFont := espy12");
	Eval("userConfiguration.clipboardDepth := 1");

	// where CalcIconBounds puts an icon: the point at the middle of its
	// top edge, clamped to the application area
	Rect box;
	Point where;
	where.h = 80;
	where.v = 40;
	TClipboard::CalcIconBounds(40, 12, where, &box);
	EXPECT(box.left == 60 && box.right == 100 && box.top == 40 && box.bottom == 52);
	where.h = 10;						// too near the left edge: against it
	TClipboard::CalcIconBounds(40, 12, where, &box);
	EXPECT(box.left == 0 && box.right == 40);
	where.h = 155;						// and too near the right
	TClipboard::CalcIconBounds(40, 12, where, &box);
	EXPECT(box.left == 120 && box.right == 160);
	where.h = 80;
	where.v = 95;						// too far down: against the bottom
	TClipboard::CalcIconBounds(40, 12, where, &box);
	EXPECT(box.top == 88 && box.bottom == 100);

	// the question that says a drag was let go on the background
	Rect area;
	SetRect(&area, 0, 0, 100, 160);		// top, left, bottom, right
	Point inside;
	inside.h = 80;
	inside.v = 50;
	EXPECT(!PointOnClipboard(inside, area, RefVar()));
	Point past;
	past.h = 200;
	past.v = 50;
	EXPECT(PointOnClipboard(past, area, RefVar()));
	// ... but not over the button bar's own edge, which is drawn on top
	EXPECT(!PointOnClipboard(past, area, RefVar(RSSYMright)));

	// nothing on the clipboard to begin with
	EXPECT(gRootView->GetClipboard() == nil && gRootView->GetClipboardIcon() == nil);
	EXPECT(ISNIL(Eval("GetClipboard()")) && ISNIL(Eval("GetClipboardIcon()")));

	// a clipping put there by hand, as a script does
	Eval("SetClipboard({label: \"a note\", types: [['text]], "
		 "data: [[{text: \"hello\", bounds: {left: 0, top: 0, right: 50, bottom: 20}}]], "
		 "bounds: {left: 10, top: 10, right: 60, bottom: 30}, xy: {x: 80, y: 40}})");
	Refresh();
	TView* clipboard = gRootView->GetClipboard();
	TView* icon = gRootView->GetClipboardIcon();
	EXPECT(clipboard != nil && icon != nil);
	if (clipboard == nil || icon == nil)
		return;
	EXPECT(clipboard->DerivedFrom(clClipboard) && clipboard->ClassID() == clClipboard);
	EXPECT(!icon->DerivedFrom(clClipboard) && (icon->fFlags & vClipboard) != 0);
	// the two arrays line up, so each finds the other
	EXPECT(gRootView->GetClipboard(icon) == clipboard);
	EXPECT(gRootView->GetClipboardIcon((TClipboard*) clipboard) == icon);
	EXPECT(RINT(Eval("Length(GetClipboardIcon())")) == 1);
	// the clipping kept the rectangle its items came from
	EXPECT(((TClipboard*) clipboard)->fBounds.right - ((TClipboard*) clipboard)->fBounds.left == 50);
	// and the icon is a paragraph of the label inside the application area
	EXPECT(icon->viewBounds.left >= 0 && icon->viewBounds.right <= 160);
	EXPECT(icon->viewBounds.right > icon->viewBounds.left && icon->viewBounds.bottom > icon->viewBounds.top);
	EXPECT(Ustrcmp(GetCString(RefVar(GetProtoVariable(icon->fContext, RSSYMtext, nil))), Uni("a note")) == 0);
	// and it is on the screen: the label is drawn somewhere in it
	{
		long ink = 0;
		for (long y = icon->viewBounds.top; y < icon->viewBounds.bottom; y++)
			for (long x = icon->viewBounds.left; x < icon->viewBounds.right; x++)
				if (Pixel(x, y) != 0)
					ink++;
		EXPECT(ink > 0);
	}

	// what a script reads back
	RefVar back(Eval("GetClipboard()"));
	EXPECT(IsFrame(back));
	EXPECT(Ustrcmp(GetCString(RefVar(GetFrameSlotRef(back, RSSYMlabel))), Uni("a note")) == 0);
	EXPECT(RINT(Eval("Length(GetClipboard().types)")) == 1);
	EXPECT(NOTNIL(Eval("GetClipboard().types[0][0] = 'text")));
	EXPECT(RINT(Eval("GetClipboard().bounds.right")) == 60);

	// what a drag off it would be given: the items by index, each with
	// the types it was made with, and the data of the type that is asked
	{
		TDragInfo info(0);
		((TClipboard*) clipboard)->GetClipboardDataInfo(&info);
		EXPECT(info.Count() == 1);
		EXPECT(RINT(RefVar(info.GetItemDragRef(0))) == 0);
		EXPECT(EQRef(info.GetItemIndType(0, 0), RSSYMtext));
		RefVar data(clipboard->GetDropData(RefVar(RSSYMtext), RefVar(MAKEINT(0))));
		EXPECT(IsFrame(data) && Ustrcmp(GetCString(RefVar(GetFrameSlotRef(data, RSSYMtext))), Uni("hello")) == 0);
		// a type the clipping does not carry has no data
		EXPECT(ISNIL(clipboard->GetDropData(RefVar(RSSYMstyles), RefVar(MAKEINT(0)))));
	}

	// a second clipping pushes the first off, because the depth is one
	Eval("SetClipboard({label: \"another\", types: [['text]], data: [[{text: \"bye\"}]], "
		 "bounds: {left: 0, top: 0, right: 20, bottom: 20}, xy: {x: 20, y: 60}})");
	Refresh();
	EXPECT(RINT(Eval("Length(GetClipboardIcon())")) == 1);
	EXPECT(Ustrcmp(GetCString(RefVar(GetFrameSlotRef(RefVar(Eval("GetClipboard()")), RSSYMlabel))), Uni("another")) == 0);
	EXPECT(gRootView->GetClipboard() != clipboard);

	// the icon put back against the edges of a screen turned round
	{
		TView* second = gRootView->GetClipboardIcon();
		SetFrameSlot(second->fContext, RSSYMpin, RefVar(MAKEINT(4)));		// against the right
		Eval("vars.displayParams.buttonBarPosition := nil");
		EXPECT(NOTNIL(FReOrientLabelForm(second->fContext)));
		EXPECT(second->viewBounds.right == 160);
	}

	// and thrown away
	Eval("SetClipboard(nil)");
	Refresh();
	EXPECT(gRootView->GetClipboard() == nil && gRootView->GetClipboardIcon() == nil);
	EXPECT(ISNIL(Eval("GetClipboard()")) && ISNIL(Eval("GetClipboardIcon()")));

	// a drag let go on the edge of the screen becomes a clipping: the
	// source's drop data is what the clipping keeps, and - the drag not
	// being a copy - the source is asked to give the item up
	Eval("carriedOff := nil");
	ViewOf("ctxCB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, "
		   "viewBounds: {left: 20, top: 40, right: 60, bottom: 70}, viewFormat: 1, "
		   "viewClickScript: func(unit) begin :DragAndDrop(unit, :GlobalBox(), nil, nil, "
		   "[{types: ['text], dragRef: \"carried off\", label: \"a clipping\"}]); true end, "
		   "viewGetDropDataScript: func(dropType, dragRef) {text: dragRef}, "
		   "viewDropRemoveScript: func(dragRef) begin carriedOff := dragRef; true end})");
	Refresh();
	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(40, 55, 0);
	HostTabletQueuePenMove(80, 60);
	HostTabletQueuePenMove(120, 70);
	HostTabletQueuePenMove(158, 70);			// within five pixels of the right edge
	HostTabletQueuePenUp(0);
	HostTabletPump();
	gRecognition.Idle();
	TView* dropped = gRootView->GetClipboard();
	EXPECT(dropped != nil && gRootView->GetClipboardIcon() != nil);
	EXPECT(NOTNIL(Eval("StrEqual(carriedOff, \"carried off\")")));
	if (dropped != nil)
	{
		// the item's data went in, and the picture of the view with it
		RefVar data(dropped->GetDropData(RefVar(RSSYMtext), RefVar(MAKEINT(0))));
		EXPECT(IsFrame(data) && Ustrcmp(GetCString(RefVar(GetFrameSlotRef(data, RSSYMtext))), Uni("carried off")) == 0);
		EXPECT(NOTNIL(GetProtoVariable(dropped->fContext, RSSYMbits, nil)));
		// the drag's own label named the icon
		EXPECT(Ustrcmp(GetCString(RefVar(GetProtoVariable(gRootView->GetClipboardIcon()->fContext, RSSYMtext, nil))), Uni("a clipping")) == 0);
	}
	Eval("SetClipboard(nil); RemoveView(GetRoot(), ctxCB)");
	Refresh();
	EXPECT(gRootView->GetClipboard() == nil && gRootView->GetClipboardIcon() == nil);
	Eval("userConfiguration.clipboardDepth := nil");
}


// The hilite stroke: the pen held still on a page and then drawn across
// it, which is how the Newton is told what to select.
static void
TestHiliteStroke()
{
	// a page with a paragraph on it
	TView* page = ViewOf("ctxHP := AddView(GetRoot(), {viewClass: 77, viewFlags: 1 + 0x200 + 0x800, "
		"viewBounds: {left: 0, top: 0, right: 160, bottom: 60}})");
	TParagraphView* para = (TParagraphView*) ViewOf(
		"ctxHT := AddView(ctxHP, {viewClass: 81, viewFlags: 1 + 0x200 + 0x800, "
		"viewBounds: {left: 4, top: 4, right: 156, bottom: 40}, viewFont: espy12, "
		"text: \"the quick brown fox\"})");
	EXPECT(page != nil && para != nil && para->DerivedFrom(clParagraphView));
	Refresh();
	if (para == nil)
		return;
	EXPECT(!para->Hilited());

	// where the words are: draw from the "q" of quick to the "f" of fox
	Rect from;
	Rect to;
	para->OffsetToBounds(4, &from);				// the q
	para->OffsetToBounds(16, &to);				// the f
	EXPECT(to.left > from.left);
	// along the middle of the line (the stroke's points are looked for on
	// the lines' own boxes, margin 0)
	long y = (long) ((para->Line(0).fBounds.top + para->Line(0).fBounds.bottom) / 2);

	// the pen held still for more than 45 samples is a hilite click; the
	// stationary samples are fed first, so that the click is delivered
	// while the rest of the stroke is still to come - the root view's
	// Hiliter draws the line by reading the stroke as the pen moves
	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(from.left, y, 0);
	for (long i = 0; i < 60; i++)
		HostTabletQueuePenMove(from.left, y);
	for (long x = from.left; x <= to.left; x += 3)
		HostTabletQueuePenMove(x, y);
	HostTabletQueuePenUp(0);
	for (long i = 0; i < 61; i++)
		HostTabletPump();
	gRecognition.Idle();

	// the words the line went through are selected, and the page knows
	// which of its children owns the selection
	EXPECT(para->Hilited());
	EXPECT(RINT(Eval("Length(ctxHT.hilites)")) == 1);
	TParagraphHilite* hilite = (TParagraphHilite*) RefToAddress(RefVar(para->FirstHilite()));
	EXPECT(hilite != nil);
	if (hilite != nil)
	{
		// "quick brown " - from the start of the word the line began on
		// to the boundary before the word it ended on, because
		// PointToWordBoundary is asked with a bias towards the left
		EXPECT(hilite->fStart == 4);
		EXPECT(hilite->fEnd == 15);
	}

	para->RemoveAllHilites();
	EXPECT(!para->Hilited());

	Eval("RemoveView(GetRoot(), ctxHP)");
	gRootView->Dirty(nil);				// (the line was drawn on the screen, not into a view)
	Refresh();
	EXPECT(MapIs(ExpWhite, "the hilited page closed"));

	// a lasso - a stroke that goes round something and comes back to
	// where it started - selects the whole of what it went round.  Here
	// the view answers the gesture itself and asks for the hilite with
	// HiliteViewChildren, which is what a script does when it wants the
	// selection without the view system deciding.
	TView* box = ViewOf("ctxHB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200 + 0x800, "
		"viewBounds: {left: 0, top: 0, right: 160, bottom: 60}, asked: nil, "
		"viewGestureScript: func(unit, kind) begin asked := kind; :HiliteViewChildren(unit) end})");
	TParagraphView* other = (TParagraphView*) ViewOf(
		"ctxHT2 := AddView(ctxHB, {viewClass: 81, viewFlags: 1 + 0x200, "
		"viewBounds: {left: 20, top: 15, right: 140, bottom: 35}, viewFont: espy12, "
		"text: \"lasso me\"})");
	EXPECT(box != nil && other != nil);
	Refresh();
	if (other == nil)
		return;

	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(6, 6, 0);
	for (long i = 0; i < 60; i++)
		HostTabletQueuePenMove(6, 6);
	for (long x = 6; x <= 150; x += 6)			// right along the top
		HostTabletQueuePenMove(x, 6);
	for (long y = 6; y <= 50; y += 6)			// down the right
		HostTabletQueuePenMove(150, y);
	for (long x = 150; x >= 6; x -= 6)			// back along the bottom
		HostTabletQueuePenMove(x, 50);
	for (long y = 50; y >= 8; y -= 6)			// and up to where it began
		HostTabletQueuePenMove(6, y);
	HostTabletQueuePenUp(0);
	for (long i = 0; i < 61; i++)
		HostTabletPump();
	gRecognition.Idle();

	EXPECT(RINT(Eval("ctxHB.asked")) == aeGesture2f);
	EXPECT(other->Hilited());
	TParagraphHilite* all = (TParagraphHilite*) RefToAddress(RefVar(other->FirstHilite()));
	EXPECT(all != nil && all->fStart == 0 && all->fEnd == other->TextLength());

	Eval("RemoveView(GetRoot(), ctxHB)");
	gRootView->Dirty(nil);
	Refresh();
	EXPECT(MapIs(ExpWhite, "the lassoed page closed"));
}


// A word sent to a field on its own - a paragraph with no page around it,
// which is where the recogniser sends a word written on it: a field that
// holds one word (viewJustify oneWordOnly) has its text replaced and keeps
// the readings as alternateWords.
static void
TestWordIntoField()
{
	gWordID = kWRecDomainType;
	TDomain* domain = TDomain::Make(gController, kWRecDomainType, (char*) "word");
	if (gRecognition.fRecognizers->FindRecognizer(kWRecDomainType) == nil)
	{
		TRecognizer* recognizer = new TRecognizer;
		recognizer->Init(domain, kWRecDomainType, aeWord, 0, 1);
		recognizer->InitServices(0, 0);
		gRecognition.fRecognizers->AddRecognizer(recognizer);
	}
	TStroke* stroke = TStroke::Make(0);
	TabPt pt;
	pt.z = 3;
	pt.p = 0;
	pt.x = ToFixed(30);
	pt.y = ToFixed(15);
	stroke->AddPoint(&pt);
	pt.x = ToFixed(60);
	pt.y = ToFixed(25);
	stroke->AddPoint(&pt);
	stroke->fDownTime = 100;
	stroke->fUpTime = 110;
	stroke->EndStroke();
	TStrokeUnit* strokeUnit = TStrokeUnit::Make(gRootDomain, 1, stroke, nil);
	TWRecUnit* word = TWRecUnit::Make(domain, 1, nil);
	EXPECT(strokeUnit != nil && word != nil);
	if (strokeUnit == nil || word == nil)
		return;
	word->AddSub(strokeUnit);
	word->EndSubs();
	UniChar hello[6] = { 'h', 'e', 'l', 'l', 'o', 0 };
	EXPECT(word->AddWordInterpretation() == 0);
	EXPECT(word->SetWordString(0, hello) != nil);
	word->SetLabel(0, kWordLabelWord);
	word->SetScore(0, 55);

	TParagraphView* field = (TParagraphView*) ViewOf("ctxWF := AddView(GetRoot(), {viewClass: 81, viewFlags: 1, "
		"viewBounds: {left: 20, top: 10, right: 200, bottom: 30}, viewJustify: 0x1000000, viewFont: espy12, "
		"text: \"old\"})");
	EXPECT(field != nil);
	if (field == nil)
		return;
	TUnitPublic pub(word, 0);
	RefVar cmd(MakeCommand(aeWord, field, (Long) &pub));
	EXPECT(gApplication->DispatchCommand(cmd) == 1);
	RefVar text(field->Text());
	EXPECT(Ustrcmp(GetCString(text), hello) == 0);
	RefVar alternates(Eval("ctxWF.alternateWords"));
	EXPECT(IsArray(alternates) && Length(alternates) == 1
		   && Ustrcmp((const UniChar*) BinaryData(RefVar(GetArraySlot(alternates, 0))), hello) == 0);
	Eval("RemoveView(GetRoot(), ctxWF)");
	Refresh();
}


// A page as the target of a drag: the kinds of data it takes, text
// dropped on it made a paragraph (its textFlags dropped, a paragraph with
// no width given the page's right edge, one above the page moved down
// onto it, and the new paragraph selected), a selected child dragged
// about on its own page simply moved, and a child dragged off it taken
// away.
static void
TestEditViewDrop()
{
	TEditView* page = (TEditView*) ViewOf(
		"ctxED := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, "
		"viewBounds: {left: 0, top: 20, right: 200, bottom: 150}, viewChildren: []})");
	EXPECT(page != nil);
	if (page == nil)
		return;
	Point pt;
	pt.h = 30;
	pt.v = 40;
	RefVar types(page->GetSupportedDropTypes(pt));
	EXPECT(IsArray(types) && Length(types) == 4
		   && EQ(RefVar(GetArraySlot(types, 0)), RSSYMtext) && EQ(RefVar(GetArraySlot(types, 3)), RSSYMpicture));

	long before = page->fChildren->Count();
	RefVar data(Eval("{viewBounds: {left: 20, top: -5, right: 20, bottom: 15}, text: \"dropped\", textFlags: 3, "
					 "viewFont: espy12}"));
	EXPECT(page->Drop(RefVar(RSSYMtext), data, &pt));
	EXPECT(page->fChildren->Count() == before + 1);
	TView* para = page->fChildren->Last();
	EXPECT(para != nil && para->ClassID() == clParagraphView);
	if (para == nil)
		return;
	EXPECT(ISNIL(GetFrameSlot(data, RSSYMtextflags)));
	EXPECT(EQ(RefVar(GetFrameSlot(data, RSSYMviewstationery)), RSSYMpara));
	// laid out to the page's right edge, and then - a paragraph's input
	// flags having vWidthGrowsWithText (4) - brought in to the text's
	// width when SetBounds lays it out again (FixupBBox)
	EXPECT(para->viewBounds.right < 200 && para->viewBounds.right > para->viewBounds.left + 5);
	EXPECT(para->viewBounds.top == 20);				// moved down onto the page
	EXPECT(para->Hilited() && gRootView->fHiliter == page);

	// dragged about on its own page, wholly selected: moved
	Point delta;
	delta.h = 5;
	delta.v = 10;
	Rect was = para->viewBounds;
	EXPECT(page->DropMove(RefVar(para->fContext), delta, pt, false));
	EXPECT(para->viewBounds.top == was.top + 10 && para->viewBounds.left == was.left + 5);
	// but never above the page
	delta.h = 0;
	delta.v = -100;
	EXPECT(page->DropMove(RefVar(para->fContext), delta, pt, false));
	EXPECT(para->viewBounds.top == 20);

	// and dragged off it: the whole child taken away
	EXPECT(page->DropRemove(RefVar(para->fContext)));
	EXPECT(page->fChildren->Count() == before);
	Eval("RemoveView(GetRoot(), ctxED)");
	Refresh();
}


// A paragraph as a drag's source and target: what a click on its
// selection may do, the selected text as a paragraph frame of its own, a
// drop refused on the selection itself and taken elsewhere, and the
// dragged text taken out afterwards.
static void
TestParagraphDrop()
{
	TParagraphView* p = (TParagraphView*) ViewOf("ctxPD := AddView(GetRoot(), {viewClass: 81, viewFlags: 1 + 8, "
		"viewBounds: {left: 20, top: 10, right: 160, bottom: 30}, viewJustify: 0, viewFont: espy12, text: \"Hello World\"})");
	EXPECT(p != nil);
	if (p == nil)
		return;
	Eval("ctxPD:Dirty()");
	Refresh();
	// "World" selected: draggable only, not the whole paragraph
	p->MakeHilite(6, 11, true);
	RefVar hilite(p->FirstHilite());
	EXPECT(NOTNIL(hilite) && !p->IsCompletelyHilited(hilite));
	EXPECT(p->ClickOptions() == 1);

	// the drag item and its data: the selected characters, in a frame
	// whose viewBounds are the selection's, in the paragraph's coordinates
	TDragInfo info(0L);
	EXPECT(p->AddDragInfo(&info));
	EXPECT(info.Count() == 1 && EQ(RefVar(info.GetItemIndType(0, 0)), RSSYMtext));
	RefVar data(p->GetDropData(RefVar(RSSYMtext), RefVar(p->fContext)));
	EXPECT(IsFrame(data));
	EXPECT(Ustrcmp(GetCString(RefVar(GetFrameSlot(data, RSSYMtext))), (const UniChar*) u"World") == 0);
	EXPECT(ISNIL(GetFrameSlot(data, RSSYMviewjustify)));
	Rect r;
	EXPECT(FromObject(RefVar(GetFrameSlot(data, RSSYMviewbounds)), r));
	Rect w6;
	p->OffsetToBounds(6, &w6);
	EXPECT(r.left == w6.left - p->viewBounds.left && r.top >= 0 && r.top < 20);

	// a drop on the selection itself is refused (the selection runs to the
	// end of the text: 2)
	Point over;
	over.h = (short) (w6.left + 3);
	over.v = (short) ((w6.top + w6.bottom) / 2);
	EXPECT(p->PointOverHilitedText(over) == 2);
	RefVar dropped(Eval("{text: \"big\"}"));
	Point at = over;
	EXPECT(!p->Drop(RefVar(RSSYMtext), dropped, &at));

	// the selected text taken out once dragged away
	EXPECT(p->DropRemove(RefVar(p->fContext)));
	// (with the space before it, as deleting a word takes it)
	EXPECT(Ustrcmp(GetCString(RefVar(p->Text())), (const UniChar*) u"Hello") == 0);

	// and all of it selected, in a paragraph that sizes itself: resizable
	p->MakeHilite(0, 5, true);
	EXPECT(p->IsCompletelyHilited(RefVar(p->FirstHilite())));
	EXPECT(p->ClickOptions() == 3);
	p->RemoveAllHilites();
	Eval("RemoveView(GetRoot(), ctxPD)");
	Refresh();
}


// The pen pressed on a selection.  A paragraph selected whole is dragged
// along the page with the pen (a quick press and drag, not a hilite
// stroke); a resize (aeScaleData from the old bounds to new ones) scales
// the paragraph and stops it fitting itself to its text.
static void
TestSelectionClicks()
{
	Eval("vars.displayParams := {appAreaGlobalLeft: 0, appAreaGlobalTop: 0, appAreaWidth: 160, appAreaHeight: 100}");
	screenWidth = kWidth;
	screenHeight = kHeight;
	TView* page = ViewOf("ctxSP := AddView(GetRoot(), {viewClass: 77, viewFlags: 1 + 0x200 + 0x800, "
		"viewBounds: {left: 0, top: 0, right: 160, bottom: 100}})");
	TParagraphView* para = (TParagraphView*) ViewOf(
		"ctxST := AddView(ctxSP, {viewClass: 81, viewFlags: 1 + 0x200 + 0x800 + 8, "
		"viewBounds: {left: 4, top: 4, right: 80, bottom: 24}, viewFont: espy12, "
		"text: \"moved\"})");
	EXPECT(page != nil && para != nil);
	if (page == nil || para == nil)
		return;
	Refresh();
	para->MakeHilite(0, 5, true);
	gRootView->fHiliter = page;
	EXPECT(para->Hilited() && para->IsCompletelyHilited(RefVar(para->FirstHilite())));
	Rect was = para->viewBounds;

	// pressed on the text and dragged 30 pixels down: the text arrives
	// 30 pixels further down the page
	Rect w0;
	para->OffsetToBounds(1, &w0);
	long x = w0.left + 2;
	long y = (w0.top + w0.bottom) / 2;
	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(x, y, 0);
	for (long step = 3; step <= 30; step += 3)
		HostTabletQueuePenMove(x, y + step);
	HostTabletQueuePenMove(x + 1, y + 30);
	HostTabletQueuePenMove(x, y + 30);
	HostTabletQueuePenMove(x + 1, y + 31);
	HostTabletQueuePenMove(x, y + 30);
	HostTabletQueuePenUp(0);
	HostTabletPump();
	gRecognition.Idle();
	TView* moved = nil;
	TListLoop loop(page->fChildren);
	TView* child;
	while ((child = (TView*) loop.Next()) != nil)
		if (child->DerivedFrom(clParagraphView)
			&& Ustrcmp(GetCString(RefVar(((TParagraphView*) child)->Text())), (const UniChar*) u"moved") == 0)
			moved = child;
	EXPECT(moved != nil && page->fChildren->Count() == 1);
	if (moved != nil)
		EXPECT(moved->viewBounds.top >= was.top + 25 && moved->viewBounds.top <= was.top + 35);
	Eval("RemoveView(GetRoot(), ctxSP)");
	Refresh();

	// a resize: twice as wide, as TrackScale would send it (a paragraph that
	// does not calculate its bounds: one that does is as wide as its parent
	// from the start, its input flags having bit 0 - CreateAllCaches)
	TView* page2 = ViewOf("ctxSP := AddView(GetRoot(), {viewClass: 77, viewFlags: 1, "
		"viewBounds: {left: 0, top: 0, right: 160, bottom: 100}})");
	TParagraphView* p2 = (TParagraphView*) ViewOf(
		"ctxST := AddView(ctxSP, {viewClass: 81, viewFlags: 1, textFlags: 5, "
		"viewBounds: {left: 10, top: 10, right: 50, bottom: 30}, viewFont: espy12, text: \"wide\"})");
	EXPECT(page2 != nil && p2 != nil);
	if (p2 == nil)
		return;
	Rect src = p2->viewBounds;
	Rect dst = src;
	dst.right = (short) (src.left + 2 * (src.right - src.left));
	RefVar cmd(MakeCommand(aeScaleData, p2, kNoParameter));
	CommandSetIndexRect(cmd, 0, src);
	CommandSetIndexRect(cmd, 2, dst);
	gApplication->DispatchCommand(cmd);
	EXPECT(p2->viewBounds.left == src.left && p2->viewBounds.right == dst.right);
	EXPECT((p2->TextFlags() & 5) == 0);
	EXPECT(RINT(Eval("ctxST.textFlags")) == 0);
	Eval("RemoveView(GetRoot(), ctxSP)");
	Refresh();
}


// A shape's selection (TPolygonHilite) and a corner of it dragged with the
// pen (TEditView::TrackDistort, reached through HiliteClick): the shape is
// diced into a copy of itself, the corner follows the pen, and the copy's
// point is moved by command 0x43 when the pen is lifted.
static void
TestDistort()
{
	// the arithmetic first: how far along a segment the pen is
	Point a = MakePoint(0, 0), b = MakePoint(30, 0);
	EXPECT(LineHitRatio(MakePoint(15, 2), a, b, 16) == 0x8000);
	EXPECT(LineHitRatio(MakePoint(15, 20), a, b, 16) == (Fixed) 0x80000000);
	EXPECT(LineHitRatio(MakePoint(-10, 0), a, b, 16) == 0x5555);		// (before the start: measured from it all the same)
	EXPECT(LineHitRatio(MakePoint(2, 15), a, MakePoint(0, 30), 16) == 0x8000);
	EXPECT(LessOrEq(0, 0, 3, 0x10000) && LessOrEq(0, 0x8000, 1, 0x10000) && !LessOrEq(2, 0, 1, 0x10000));

	Eval("vars.displayParams := {appAreaGlobalLeft: 0, appAreaGlobalTop: 0, appAreaWidth: 160, appAreaHeight: 100}");
	screenWidth = kWidth;
	screenHeight = kHeight;
	TView* page = ViewOf("ctxDP := AddView(GetRoot(), {viewClass: 77, viewFlags: 1 + 0x200 + 0x800, "
		"viewBounds: {left: 0, top: 0, right: 160, bottom: 100}})");
	EXPECT(page != nil);
	if (page == nil)
		return;
	Point tri[4];
	tri[0] = MakePoint(0, 0);
	tri[1] = MakePoint(30, 0);
	tri[2] = MakePoint(0, 20);
	tri[3] = MakePoint(0, 0);
	Rect box;
	SetRect(&box, 40, 20, 71, 41);
	RefVar form(MakePolygonForm(tri, 4, 5, box, 1));
	SetFrameSlot(form, RSSYMviewclass, RefVar(MAKEINT(clPolygonView)));
	SetFrameSlot(form, RSSYMviewflags, RefVar(MAKEINT(1 + 0x200 + 0x800 + 8)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "distortForm")), form);
	TPolygonView* poly = (TPolygonView*) ViewOf("ctxDG := AddView(ctxDP, distortForm)");
	EXPECT(poly != nil && poly->ClassID() == clPolygonView);
	if (poly == nil)
		return;
	Refresh();
	EXPECT(Pixel(55, 18) == 0);

	// part of the top side selected: from half way along it to its end
	poly->MakeHilite(0, 0x8000, 1, 0x10000);
	RefVar hiliteRef(poly->FirstHilite());
	EXPECT(NOTNIL(hiliteRef));
	TPolygonHilite* hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
	EXPECT(hilite->fShape->fCount == 2 && hilite->fShape->fVerb == 5);
	EXPECT(hilite->fShape->fPoints[0].h == 15 && hilite->fShape->fPoints[0].v == 0);
	EXPECT(hilite->fShape->fPoints[1].h == 30 && hilite->fShape->fPoints[1].v == 0);
	EXPECT(!poly->IsCompletelyHilited(hiliteRef) && poly->ClickOptions() == 5);
	EXPECT(hilite->Encloses(MakePoint(20, 3)) && !hilite->Encloses(MakePoint(20, 19)));

	// the whole of it: the points as they are, the box round them grown by
	// four (and four more at the bottom right); corners may be dragged
	poly->HiliteAll();
	gRootView->fHiliter = page;
	hiliteRef = poly->FirstHilite();
	hilite = (TPolygonHilite*) RefToAddress(hiliteRef);
	EXPECT(Length(RefVar(poly->Hilites())) == 1);
	EXPECT(hilite->fShape->fCount == 4 && hilite->fShape->fVerb == 5);
	EXPECT(hilite->fFirst == 0 && hilite->fLast == 3 && hilite->fFirstPart == 0 && hilite->fLastPart == 0x10000);
	EXPECT(poly->IsCompletelyHilited(hiliteRef) && poly->ClickOptions() == 7);
	EXPECT(hilite->fBounds.left == -4 && hilite->fBounds.top == -4);
	Rect sel;
	SetRect(&sel, 0, 0, 0, 0);
	sel.top = -32768;
	sel.bottom = -32768;
	EXPECT(page->GlobalHiliteBounds(&sel) == 5);		// (a tapped selection is not resized: fClickOptions)
	EXPECT(sel.left == 36 && sel.top == 16);
	// the hiliting: a thick black line along the sides
	Refresh();
	EXPECT(Pixel(55, 18) != 0);

	// the corner at the top right dragged ten pixels right and down
	long x = 70, y = 20;
	HostAdvanceClock(kSeconds);
	HostTabletQueuePenDown(x, y, 0);
	for (long step = 2; step <= 10; step += 2)
		HostTabletQueuePenMove(x + step, y + step);
	HostTabletQueuePenMove(x + 10, y + 10);
	HostTabletQueuePenMove(x + 10, y + 10);
	HostTabletQueuePenUp(0);
	HostTabletPump();
	gRecognition.Idle();

	// the shape was diced into a copy (the old view gone) and the copy's
	// corner moved
	EXPECT(page->fChildren->Count() == 1);
	TView* child = page->fChildren->Count() > 0 ? (TView*) page->fChildren->At(0) : nil;
	EXPECT(child != nil && child->ClassID() == clPolygonView);
	if (child == nil || child->ClassID() != clPolygonView)
		return;
	TPolygonView* moved = (TPolygonView*) child;
	RefVar points(moved->Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	EXPECT(shape->fCount == 4);
	EXPECT(moved->viewBounds.left == 40 && moved->viewBounds.top == 20);
	EXPECT(shape->fPoints[1].h == 40 && shape->fPoints[1].v == 10);
	EXPECT(shape->fPoints[0].h == 0 && shape->fPoints[2].v == 20);
	EXPECT(moved->Hilited());

	// drawn without the selection: the new side from (40, 20) to (80, 30)
	moved->RemoveAllHilites();
	gRootView->fHiliter = nil;
	Eval("GetRoot():Dirty()");
	Refresh();
	EXPECT(Pixel(60, 25) != 0 || Pixel(60, 24) != 0 || Pixel(60, 26) != 0);
	EXPECT(Pixel(80, 30) != 0);
	EXPECT(Pixel(65, 20) == 0);				// the old top side gone
	Eval("RemoveView(GetRoot(), ctxDP)");
	Refresh();
}


// A shape edited other than by its corners: a scrub over one side of a box
// takes that side out (HitSegment finds the run of sides the scrub's box
// covers more than half of; the closed box becomes one open shape of the
// other three), a resized selection maps the points (Scale), the dragged
// data is a shape view's form of the selection (GetDropData), and a double
// tap on a shape of ink reads it again (aeDoubleTap) where one on points
// is not taken.
static void
TestPolygonEditing()
{
	Eval("vars.displayParams := {appAreaGlobalLeft: 0, appAreaGlobalTop: 0, appAreaWidth: 160, appAreaHeight: 100}");
	screenWidth = kWidth;
	screenHeight = kHeight;
	TView* page = ViewOf("ctxPE := AddView(GetRoot(), {viewClass: 77, viewFlags: 1 + 0x200 + 0x800, "
		"viewBounds: {left: 0, top: 0, right: 160, bottom: 100}})");
	EXPECT(page != nil);
	if (page == nil)
		return;
	Point box[5];
	box[0] = MakePoint(0, 0);
	box[1] = MakePoint(30, 0);
	box[2] = MakePoint(30, 20);
	box[3] = MakePoint(0, 20);
	box[4] = MakePoint(0, 0);
	Rect where;
	SetRect(&where, 40, 20, 71, 41);
	RefVar form(MakePolygonForm(box, 5, 4, where, 1));
	SetFrameSlot(form, RSSYMviewclass, RefVar(MAKEINT(clPolygonView)));
	SetFrameSlot(form, RSSYMviewflags, RefVar(MAKEINT(1 + 0x200 + 0x800 + 8)));
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "editForm")), form);
	TPolygonView* poly = (TPolygonView*) ViewOf("ctxPB := AddView(ctxPE, editForm)");
	EXPECT(poly != nil && poly->ClassID() == clPolygonView);
	if (poly == nil)
		return;
	Eval("GetRoot():Dirty()");
	Refresh();
	EXPECT(Pixel(70, 30) != 0 && Pixel(55, 20) != 0);		// (black)

	// the scrub's box over the right side only
	Rect scrub;
	SetRect(&scrub, 60, 15, 80, 45);
	long first = -1, last = -1;
	EXPECT(poly->HitSegment(scrub, &first, &last) && first == 1 && last == 2);
	Rect everything;
	SetRect(&everything, 30, 10, 90, 50);
	EXPECT(poly->HandleScrub(everything, 5, nil, false) == 5);		// most of it: the view
	EXPECT(poly->HandleScrub(scrub, 5, nil, false) == 0);
	EXPECT(poly->HandleScrub(scrub, 3, nil, true) == 3);
	EXPECT(page->fChildren->Count() == 1);
	TView* child = page->fChildren->Count() > 0 ? (TView*) page->fChildren->At(0) : nil;
	EXPECT(child != nil && child != poly && child->ClassID() == clPolygonView);
	if (child == nil || child->ClassID() != clPolygonView)
		return;
	TPolygonView* rest = (TPolygonView*) child;
	RefVar points(rest->Points());
	PolygonShape* shape = (PolygonShape*) BinaryData(points);
	EXPECT(shape->fCount == 4 && shape->fVerb == 5);
	EXPECT(shape->fPoints[0].h == 30 && shape->fPoints[0].v == 20);
	EXPECT(shape->fPoints[1].h == 0 && shape->fPoints[1].v == 20);
	EXPECT(shape->fPoints[2].h == 0 && shape->fPoints[2].v == 0);
	EXPECT(shape->fPoints[3].h == 30 && shape->fPoints[3].v == 0);
	Eval("GetRoot():Dirty()");
	Refresh();
	EXPECT(Pixel(70, 30) == 0);				// the right side gone
	EXPECT(Pixel(55, 20) != 0 && Pixel(40, 30) != 0 && Pixel(55, 40) != 0);

	// resized to twice the size from its top left
	Rect from = rest->viewBounds, to = rest->viewBounds;
	to.right = (short) (to.left + (from.right - from.left) * 2);
	to.bottom = (short) (to.top + (from.bottom - from.top) * 2);
	rest->Scale(from, to);
	points = rest->Points();
	shape = (PolygonShape*) BinaryData(points);
	EXPECT(shape->fPoints[0].h == 60 && shape->fPoints[0].v == 40);
	EXPECT(shape->fPoints[3].h == 60 && shape->fPoints[3].v == 0);
	EXPECT(rest->viewBounds.left == 40 && rest->viewBounds.top == 20 && rest->viewBounds.right == 100 && rest->viewBounds.bottom == 60);

	// the dragged data: the whole shape at nought; half of the bottom side
	// brought to nought with its box as the bounds
	RefVar data(rest->GetDropData(RefVar(RSSYMpolygon), RefVar(NILREF)));
	Rect bounds;
	EXPECT(FromObject(RefVar(GetFrameSlot(data, RSSYMviewbounds)), bounds) && bounds.left == 0 && bounds.top == 0 && bounds.right == 60);
	rest->MakeHilite(0, 0x8000, 1, 0x10000);
	data = rest->GetDropData(RefVar(RSSYMpolygon), RefVar(NILREF));
	PolygonShape* dragged = (PolygonShape*) BinaryData(RefVar(GetFrameSlot(data, RSSYMpoints)));
	EXPECT(dragged->fCount == 2 && dragged->fPoints[0].h == 30 && dragged->fPoints[0].v == 0 && dragged->fPoints[1].h == 0);
	EXPECT(FromObject(RefVar(GetFrameSlot(data, RSSYMviewbounds)), bounds) && bounds.left == 0 && bounds.top == 40 && bounds.right == 30);
	rest->RemoveAllHilites();

	// a double tap: not taken by points
	RefVar tap(MakeCommand(aeDoubleTap, rest, 0));
	EXPECT(!rest->RealDoCommand(tap));
	// ... and taken by ink, which is read again (the remote-writing
	// preference put back afterwards)
	RefVar points2(Eval("[60, 110, 70, 120, 80, 130]"));
	RefVar arrays(AllocateArray(RSSYMarray, 1));
	SetArraySlot(arrays, 0, points2);
	RefVar bundle(MakeStrokeBundle(arrays, 1));
	long before = page->fChildren->Count();
	EXPECT(HandleInk((TEditView*) page, bundle) == 1);
	EXPECT(page->fChildren->Count() == before + 1);
	TView* inky = page->fChildren->Count() > before ? (TView*) page->fChildren->At(before) : nil;
	EXPECT(inky != nil && inky->ClassID() == clPolygonView && NOTNIL(inky->GetProto(RSSYMink)));
	if (inky != nil && inky->ClassID() == clPolygonView)
	{
		// on a printer's port the ink is made into outlined paths and
		// framed (InkMakePaths, FramePaths): one polyline contour per
		// stroke, the stroke's three points, drawn where the screen's is
		{
			RefVar ink(inky->GetProto(RSSYMink));
			pathsHandle* list = InkMakePaths(ink, 0, 0);
			EXPECT(list != nil && list[0] != nil && list[1] == nil);
			if (list != nil && list[0] != nil)
			{
				EXPECT((*list[0])->contours == 1 && (*list[0])->contour[0].vectors >= 2 && (*list[0])->contour[0].controlBits[0] == 0);
				for (long i = 0; list[i] != nil; i++)
					DisposePaths(list[i]);
			}
			if (list != nil)
				DisposePtr((Ptr) list);
			Refresh();
			Rect r = inky->viewBounds;
			long onScreen = InkIn(r.left, r.top, r.right + 2, r.bottom + 2);
			EXPECT(onScreen > 0);
			Rect wider = r;
			wider.right += 2;
			wider.bottom += 2;
			EraseRect(&wider);
			EXPECT(InkIn(r.left, r.top, r.right + 2, r.bottom + 2) == 0);
			ULong flags = gPort.portBits.pixMapFlags;
			gPort.portBits.pixMapFlags |= kPixMapDevDotPrint;
			inky->RealDraw(r);
			gPort.portBits.pixMapFlags = flags;
			long printed = InkIn(r.left, r.top, r.right + 2, r.bottom + 2);
			EXPECT(printed > 0 && printed >= onScreen - 4 && printed <= onScreen + 4);
			inky->Dirty(nil);
			Refresh();
		}
		RefVar remote(GetPreference(RSSYMremotewriting));
		tap = MakeCommand(aeDoubleTap, inky, 0);
		EXPECT(inky->RealDoCommand(tap) && CommandResult(tap) == 1);
		EXPECT(EQRef(RefVar(GetPreference(RSSYMremotewriting)), remote));
	}
	Eval("RemoveView(GetRoot(), ctxPE)");
	Refresh();
}


// A view put on the root while a modal dialog is up waits to be shown
// until the dialog goes (ModalSafeShow), unless it is marked safe to show
// over one; a view inside another is shown at once.
static void
TestModalSafeShow()
{
	TView* waits = ViewOf("ctxMS := AddView(GetRoot(), {viewClass: 74, viewFlags: 0, "
		"viewBounds: {left: 10, top: 10, right: 50, bottom: 30}, viewFormat: 1})");
	TView* safe = ViewOf("ctxMF := AddView(GetRoot(), {viewClass: 74, viewFlags: 0, "
		"viewBounds: {left: 60, top: 10, right: 90, bottom: 30}, viewFormat: 1})");
	EXPECT(waits != nil && safe != nil);
	if (waits == nil || safe == nil)
		return;
	safe->fViewJustify |= 0x40000000;			// (a private bit: above a script's thirty)
	EXPECT((waits->fFlags & vVisible) == 0 && (safe->fFlags & vVisible) == 0);
	gModalCount = 1;
	gApplication->DispatchCommand(RefVar(MakeCommand(aeShow, waits, 0)));
	gApplication->DispatchCommand(RefVar(MakeCommand(aeShow, safe, 0)));
	EXPECT((waits->fFlags & vVisible) == 0);		// waiting
	EXPECT((safe->fFlags & vVisible) != 0);			// safe over a modal dialog
	gModalCount = 0;
	ModalSafeShowRelease();
	EXPECT((waits->fFlags & vVisible) != 0);		// shown once the dialog went
	// a view waiting to be shown that a script hides meanwhile waits no
	// longer (FHideX over RemoveModalSafeView): the dialog going leaves it
	// hidden; :Show() and :Hide() answer true
	Eval("ctxMS:Hide()");
	EXPECT((waits->fFlags & vVisible) == 0);
	gModalCount = 1;
	EXPECT(NOTNIL(Eval("ctxMS:Show()")));
	EXPECT((waits->fFlags & vVisible) == 0);		// waiting again
	EXPECT(NOTNIL(Eval("ctxMS:Hide()")));
	gModalCount = 0;
	ModalSafeShowRelease();
	EXPECT((waits->fFlags & vVisible) == 0);		// taken off the list: still hidden
	Eval("RemoveView(GetRoot(), ctxMS); RemoveView(GetRoot(), ctxMF)");
	Refresh();
}


// The view side of a modal dialog: SetModalView marks the view and
// confines recognition to its bounds; RealExitModalDialog - here for a
// FilterDialog, whose modalState is TRUE - takes the mark and the bounds
// off, counts the dialog down, tells the dialog to unstuff its command
// keys and lets the next dialog (the frontmost left) have recognition;
// a modal view that simply goes (RemoveView) is exited the same way.
static void
TestModalDialogViews()
{
	TView* back = ViewOf("ctxMB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		"viewBounds: {left: 10, top: 10, right: 100, bottom: 60}, modalState: true, "
		"UnstuffModalCommandKeys: func() unstuffedMB := true})");
	TView* front = ViewOf("ctxMD := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		"viewBounds: {left: 20, top: 30, right: 80, bottom: 90}, modalState: true, "
		"UnstuffModalCommandKeys: func() unstuffedMD := true})");
	EXPECT(back != nil && front != nil);
	if (back == nil || front == nil)
		return;
	Eval("unstuffedMB := nil; unstuffedMD := nil");
	gModalCount = 1;
	SetModalView(back);
	gModalCount = 2;
	gRecognition.DisableModalRecognition();
	SetModalView(front);
	EXPECT((back->fViewJustify & vjIsModal) != 0 && (front->fViewJustify & vjIsModal) != 0);
	EXPECT(gRecognition.fModalBounds != nil && gRecognition.fModalBounds->top == 30 && gRecognition.fModalBounds->left == 20);
	EXPECT(gRootView->GetFrontmostModalView() == front);

	// the front one exited while still open: recognition goes to the
	// frontmost visible view with a modalState - which is still the one
	// being exited (ROM behaviour: nothing takes a FilterDialog's
	// modalState away, so it stays confined there until something else
	// moves it)
	RealExitModalDialog(front);
	EXPECT(gModalCount == 1 && (front->fViewJustify & vjIsModal) == 0);
	EXPECT(NOTNIL(Eval("unstuffedMD")) && ISNIL(Eval("unstuffedMB")));
	EXPECT(gRecognition.fModalBounds != nil && gRecognition.fModalBounds->top == 30 && gRecognition.fModalBounds->left == 20);
	// ... and exiting it again does nothing
	RealExitModalDialog(front);
	EXPECT(gModalCount == 1);

	// the back one closed: no dialog is left and recognition is free
	Eval("RemoveView(GetRoot(), ctxMB)");
	EXPECT(gModalCount == 0 && gRecognition.fModalBounds == nil);
	EXPECT(NOTNIL(Eval("unstuffedMB")));
	Eval("RemoveView(GetRoot(), ctxMD)");
	Refresh();
}


// The editing commands on a page's selection: copy makes a clipping of
// it, clear takes it off the page, and paste puts the clipping back on
// the page (keeping the clipping when asked to).
static void
TestEditCommands()
{
	Eval("vars.displayParams := {appAreaGlobalLeft: 0, appAreaGlobalTop: 0, appAreaWidth: 160, appAreaHeight: 100}");
	screenWidth = kWidth;
	screenHeight = kHeight;
	TView* page = ViewOf("ctxEC := AddView(GetRoot(), {viewClass: 77, viewFlags: 1 + 0x200 + 0x800, "
		"viewBounds: {left: 0, top: 0, right: 160, bottom: 100}, viewLineSpacing: 20})");
	TParagraphView* para = (TParagraphView*) ViewOf(
		"ctxET := AddView(ctxEC, {viewClass: 81, viewFlags: 1 + 0x200 + 0x800 + 8, "
		"viewBounds: {left: 10, top: 10, right: 80, bottom: 30}, viewFont: espy12, text: \"clip\"})");
	EXPECT(page != nil && para != nil);
	if (page == nil || para == nil)
		return;
	Refresh();
	para->MakeHilite(0, 4, true);
	gRootView->fHiliter = page;
	EXPECT(gRootView->GetClipboard() == nil);

	// copy: a clipping, the page left as it was
	EXPECT(page->DoEditCommand(1));
	EXPECT(gRootView->GetClipboard() != nil && gRootView->GetClipboardIcon() != nil);
	EXPECT(page->fChildren->Count() == 1);

	// clear: the selected paragraph taken off the page
	EXPECT(page->DoEditCommand(4));
	EXPECT(page->fChildren->Count() == 0);

	// paste, keeping the clipping: the text back on the page
	EXPECT(page->DoEditCommand(3));
	EXPECT(page->fChildren->Count() == 1);
	TView* back = page->fChildren->Count() > 0 ? (TView*) page->fChildren->At(0) : nil;
	EXPECT(back != nil && back->DerivedFrom(clParagraphView)
		   && Ustrcmp(GetCString(RefVar(((TParagraphView*) back)->Text())), (const UniChar*) u"clip") == 0);
	EXPECT(gRootView->GetClipboard() != nil);
	Eval("SetClipboard(nil); RemoveView(GetRoot(), ctxEC)");
	Refresh();
}


// A stroke as a polygon (AsPolygon: its points moved to its box's top
// left), and :DrawPolygons drawing polygons of GetPolygons' kind - points
// relative to the view - back as lines with the view's pen.
static void
TestPolygons()
{
	RefVar bundle(Eval("MakeStrokeBundle([[20, 10, 30, 20, 40, 30]], 1)"));
	RefVar stroke(GetStroke(bundle, 0));
	Handle h = AsPolygon(stroke);
	EXPECT(h != nil);
	if (h == nil)
		return;
	Polygon* poly = (Polygon*) *h;
	EXPECT(PolyPointCount(poly) == 3 && poly->polySize == 24);
	EXPECT(poly->polyBBox.left == 10 && poly->polyBBox.top == 20 && poly->polyBBox.right == 30 && poly->polyBBox.bottom == 40);
	EXPECT(poly->polyPoints[0].h == 0 && poly->polyPoints[0].v == 0);
	EXPECT(poly->polyPoints[2].h == 20 && poly->polyPoints[2].v == 20);
	RefVar binary(AllocateBinary(RSSYMpolygon, GetHandleSize(h)));
	memcpy(BinaryData(binary), *h, GetHandleSize(h));
	KillPoly((PolyHandle) h);

	TView* view = ViewOf("ctxPG := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		"viewBounds: {left: 40, top: 30, right: 100, bottom: 90}, drawPenSizeX: 2})");
	EXPECT(view != nil);
	if (view == nil)
		return;
	Refresh();
	EXPECT(Pixel(50, 40) == 0);
	RefVar polygons(MakeArray(1));
	SetArraySlot(polygons, 0, binary);
	SetFrameSlot(RefVar(gVarFrame), RefVar(MakeSymbol("polysPG")), polygons);
	Eval("ctxPG:DrawPolygons(polysPG)");
	// the diagonal from the view's top left, two pixels wide
	EXPECT(Pixel(40, 30) != 0 && Pixel(50, 40) != 0 && Pixel(51, 40) != 0 && Pixel(60, 50) != 0);
	EXPECT(Pixel(50, 45) == 0 && Pixel(61, 51) == 0);
	Eval("RemoveView(GetRoot(), ctxPG)");
	Refresh();
}


// The outline list (TListView): its paragraphs laid out one below the
// other at their topics' levels, collapsing and expanding, and the
// arithmetic the pen is placed by.
static void
TestListView()
{
	TView* view = ViewOf("ctxLV := AddView(GetRoot(), {viewClass: 99, viewFlags: 1, "
		"viewBounds: {left: 0, top: 0, right: 200, bottom: 200}, "
		"listViewFlags: 1, leftMarkGap: 0, rightMarkGap: 10, topMargin: 4, rightIndent: 0, "
		"firstTopic: 0, lastTopic: nil, maxLevel: 4, "
		"canonicalParaTopic: {viewClass: 81, viewFlags: 1, viewJustify: 0}, "
		"topics: ["
		"{text: \"one\", styles: [3, 12291], level: 1, hideCount: 0, viewBounds: {left: 0, top: 0, right: 200, bottom: 20}}, "
		"{text: \"two\", styles: [3, 12291], level: 2, hideCount: 0, viewBounds: {left: 0, top: 0, right: 200, bottom: 20}}, "
		"{text: \"three\", styles: [5, 12291], level: 1, hideCount: 0, viewBounds: {left: 0, top: 0, right: 200, bottom: 20}}, "
		"{text: \"four\", styles: [4, 12291], level: 2, viewBounds: {left: 0, top: 0, right: 200, bottom: 20}}]})");
	EXPECT(view != nil);
	if (view == nil)
		return;
	TListView* list = (TListView*) view;
	EXPECT(list->ClassID() == clListView && list->DerivedFrom(clEditView) && !list->DerivedFrom(clParagraphView));
	EXPECT(list->fDragTopic == -2);
	// the list's own topics (CollapseTopic and ExpandTopic read the slot
	// without inheritance, as a list's setup script leaves it)
	Eval("ctxLV.topics := ctxLV.topics");
	RefVar ctx(Eval("ctxLV"));
	EXPECT(list->NTopics() == 4 && list->GadgetWidth() == 10);

	// a paragraph for each visible topic, the children made to measure them
	// taken away again
	RefVar kids(FSetupVisibleChildren(ctx, MAKEINT(0), NILREF, NILREF));
	EXPECT(Length(kids) == 4);
	EXPECT(list->fChildren == nil || list->fChildren->Count() == 0);
	Rect box[4];
	for (long i = 0; i < 4 && i < Length(kids); i++)
	{
		RefVar kid(GetArraySlotRef(kids, i));
		EXPECT(RINT(GetFrameSlotRef(kid, RSSYMindex)) == i);
		EXPECT(RINT(GetFrameSlotRef(kid, RSSYMviewclass)) == 81);
		// the topic's box is where its paragraph came out (the template
		// keeps the box it was made with)
		char source[32];
		snprintf(source, sizeof(source), "ctxLV.topics[%ld].viewBounds", i);
		FromObject(RefVar(Eval(source)), box[i]);
	}
	// indented a level at a time (20 pixels) past the gutter's 10
	EXPECT(box[0].left == 10 && box[1].left == 30 && box[2].left == 10 && box[3].left == 30);
	// each below the one before, sixteen pixels at the least
	EXPECT(box[1].top - box[0].top >= 16 && box[2].top - box[1].top >= 16 && box[3].top - box[2].top >= 16);
	EXPECT(RINT(FVisibleTopicIndex(ctx, MAKEINT(3))) == 3);

	// the list's arithmetic
	EXPECT(RINT(FTopicBottom(ctx, RefVar(list->Topic(0)))) == (box[0].bottom > box[0].top + 16 ? box[0].bottom : box[0].top + 16));
	EXPECT(RINT(FFamilyBottom(ctx, MAKEINT(0))) == RINT(FTopicBottom(ctx, RefVar(list->Topic(1)))));
	EXPECT(RINT(FListBottom(ctx)) == RINT(FTopicBottom(ctx, RefVar(list->Topic(3)))));
	EXPECT(list->FamilySize(0) == 2 && list->FamilySize(1) == 1 && list->FamilySize(2) == 2);
	EXPECT(list->LevelFromX(5, -2) == 1);
	EXPECT(list->LevelFromX(45, -2) == 2);			// (45 - 10) / 20 + 1
	EXPECT(list->LevelFromX(200, -2) == 4);			// no deeper than maxLevel
	EXPECT(list->LevelFromX(200, 0) == 2);			// nor a level below the topic before
	Rect marker;
	list->MarkerBounds(1, &marker);
	EXPECT(marker.top == box[1].top && marker.bottom == box[1].top + 16);
	EXPECT(marker.right == box[1].left - 10 && marker.left == marker.right - 20);
	RefVar markerRef(FMarkerBounds(ctx, RefVar(list->Topic(1))));
	Rect marker2;
	FromObject(markerRef, marker2);
	EXPECT(EqualRect(&marker, &marker2));
	// the y of the middle of a topic's line falls above it: after the one before
	EXPECT(list->IndexFromY((box[2].top + box[2].top + 16) / 2 - 1) == 1);
	EXPECT(list->IndexFromY(10000) == 3);

	// a topic dragged: it and those under it, moved to the top
	RefVar dragRef(FMakeDragRef(ctx, MAKEINT(0)));
	RefVar indexes(GetFrameSlotRef(dragRef, RSSYMindex));
	EXPECT(Length(indexes) == 2 && RINT(GetArraySlotRef(indexes, 1)) == 1);
	EXPECT(RINT(GetFrameSlotRef(dragRef, RSSYMlevel)) == 1);
	RefVar dragged(GetFrameSlotRef(dragRef, RSSYMtopics));
	EXPECT(TopicTop(RefVar(GetArraySlotRef(dragged, 0))) == 0);
	EXPECT(TopicTop(RefVar(GetArraySlotRef(dragged, 1))) == TopicHeight(RefVar(list->Topic(0))));
	EXPECT(RINT(GetFrameSlotRef(dragRef, RSSYMheight)) == TopicHeight(RefVar(list->Topic(0))) + TopicHeight(RefVar(list->Topic(1))));

	// collapsed: the topic under it hidden, and made one fewer child
	EXPECT(ISNIL(FIsCollapsed(ctx, MAKEINT(0))));
	EXPECT(NOTNIL(FCollapseTopic(ctx, MAKEINT(0), NILREF)));
	EXPECT(RINT(Eval("ctxLV.topics[1].hideCount")) == 1);
	EXPECT(NOTNIL(FIsCollapsed(ctx, MAKEINT(0))));
	EXPECT(RINT(FVisibleTopicIndex(ctx, MAKEINT(2))) == 1);
	EXPECT(RINT(FFamilyBottom(ctx, MAKEINT(0))) == RINT(FTopicBottom(ctx, RefVar(list->Topic(0)))));
	kids = FSetupVisibleChildren(ctx, MAKEINT(0), NILREF, NILREF);
	EXPECT(Length(kids) == 3);
	// a hidden topic cannot itself be collapsed
	EXPECT(ISNIL(FCollapseTopic(ctx, MAKEINT(1), NILREF)));
	EXPECT(NOTNIL(FExpandTopic(ctx, MAKEINT(0), NILREF)));
	EXPECT(RINT(Eval("ctxLV.topics[1].hideCount")) == 0 && ISNIL(FIsCollapsed(ctx, MAKEINT(0))));
	// ROM QUIRK: a topic with no hideCount counts as one hidden already, so
	// collapsing and expanding its parent leaves it hidden
	FCollapseTopic(ctx, MAKEINT(2), NILREF);
	EXPECT(RINT(Eval("ctxLV.topics[3].hideCount")) == 2);
	FExpandTopic(ctx, MAKEINT(2), NILREF);
	EXPECT(RINT(Eval("ctxLV.topics[3].hideCount")) == 1);
	EXPECT(!TopicVisible(RefVar(list->Topic(3))));

	Eval("RemoveView(GetRoot(), ctxLV)");
	Refresh();
}


// A meeting in the day view (TMeetingView): where LayoutMeeting puts a
// meeting, its two children, and what it says to a scrub and a word.
static void
TestMeetingView()
{
	// 9:00 to 10:00 on some day, down a 240-pixel day 200 wide
	Eval("mtgA := {mtgStartDate: 1440 * 5 + 9 * 60, mtgDuration: 60, viewBounds: nil}");
	Rect box;
	FromObject(RefVar(LayoutMeeting(RefVar(), RefVar(Eval("mtgA")), RefVar(MAKEINT(240)), RefVar(MAKEINT(200)), RefVar())), box);
	EXPECT(box.top == 90 && box.bottom == 100 && box.left == 0 && box.right == 200);
	Eval("mtgA.viewBounds := true");
	FromObject(RefVar(LayoutMeeting(RefVar(), RefVar(Eval("mtgA")), RefVar(MAKEINT(240)), RefVar(MAKEINT(200)), RefVar())), box);
	EXPECT(box.left == 100 && box.right == 200);
	// a box of its own: kept, unless the day is narrower than its right
	Eval("mtgA.viewBounds := {left: 20, top: 0, right: 150, bottom: 5}");
	FromObject(RefVar(LayoutMeeting(RefVar(), RefVar(Eval("mtgA")), RefVar(MAKEINT(240)), RefVar(MAKEINT(200)), RefVar())), box);
	EXPECT(box.left == 20 && box.right == 200 && box.top == 90);
	Eval("mtgA.viewBounds := {left: 20, top: 0, right: 300, bottom: 5}");
	FromObject(RefVar(LayoutMeeting(RefVar(), RefVar(Eval("mtgA")), RefVar(MAKEINT(240)), RefVar(MAKEINT(200)), RefVar())), box);
	EXPECT(box.left == 100 && box.right == 300);
	// a repeating meeting's instance takes what it lacks from its template
	RefVar instance(Eval("{repeatTemplate: {mtgDuration: 30, mtgText: \"standup\"}, mtgStartDate: 0}"));
	EXPECT(RINT(GetMeetingSlot(instance, RSSYMmtgduration)) == 30);
	EXPECT(RINT(GetMeetingSlot(instance, RSSYMmtgstartdate)) == 0);
	EXPECT(ISNIL(GetMeetingSlot(instance, RSSYMviewbounds)));
	EXPECT(IsString(GetMeetingText(instance)));

	TView* view = ViewOf("ctxMV := AddView(GetRoot(), {viewClass: 95, viewFlags: 1, "
		"viewBounds: {left: 20, top: 40, right: 220, bottom: 72}, "
		"viewChildren: [{viewClass: 96, viewFlags: 1, viewBounds: {left: 0, top: 0, right: 12, bottom: 32}}, "
		"{viewClass: 81, viewFlags: 1, viewFont: 0x3000, text: \"lunch\", viewBounds: {left: 38, top: 0, right: 200, bottom: 32}}]})");
	EXPECT(view != nil);
	if (view == nil)
		return;
	TMeetingView* meeting = (TMeetingView*) view;
	EXPECT(meeting->ClassID() == clMeetingView && meeting->DerivedFrom(clContainerView));
	EXPECT(meeting->GetSliderView() != nil && meeting->GetSliderView()->ClassID() == clSliderView);
	EXPECT(meeting->GetTextView() != nil && meeting->GetTextView()->ClassID() == clParagraphView);
	// the duration bar: black down the slider, two pixels in from its sides
	Eval("ctxMV:Dirty()");
	Refresh();
	EXPECT(Pixel(26, 56) != 0 && Pixel(21, 56) == 0 && Pixel(31, 56) == 0);
	// its outline slants its ends by its width
	Rect barBox;
	SetRect(&barBox, 0, 0, 10, 100);
	PolyHandle bar = TRectToSliderPoly(barBox);
	EXPECT(bar != nil);
	if (bar != nil)
	{
		const Rect& b = (*bar)->polyBBox;
		EXPECT(b.left == 0 && b.top == 0 && b.right == 10 && b.bottom == 99);
		KillPoly(bar);
	}
	// a scrub that covers the icon (24 by 16 past the slider) takes the
	// whole meeting; one that misses the meeting is nothing to it
	Rect scrub;
	SetRect(&scrub, 30, 38, 60, 60);
	EXPECT(meeting->HandleScrub(scrub, 0, nil, false) == 5);
	SetRect(&scrub, 0, 300, 10, 310);
	EXPECT(meeting->HandleScrub(scrub, 0, nil, false) == 0);
	// a word written mostly on the meeting is taken; one far off is not
	Point pt;
	pt.h = 100;
	pt.v = 50;
	UniChar word[] = { 'x', 0 };
	Rect writing;
	SetRect(&writing, 60, 42, 200, 70);
	EXPECT(meeting->HandleWord(word, 1, writing, pt, 0, 0, RefVar(), false, nil, nil) == 5);
	SetRect(&writing, 60, 200, 100, 220);
	EXPECT(meeting->HandleWord(word, 1, writing, pt, 0, 0, RefVar(), false, nil, nil) == 0);
	Eval("RemoveView(GetRoot(), ctxMV)");
	Refresh();
}


// A QuickDraw picture of the ROM's own: the World Clock's map
// (Rworldmapbitmap, a version 1 picture of one PackBitsRect), its frame
// read big-endian by MakeShape and the map played into a box through
// DrawShape and qd/PicPlay.h.
static void
TestPicture()
{
	RefVar map(Rworldmapbitmap);
	EXPECT(IsBinary(map) && EQ(RefVar(ClassOf(map)), RSSYMpicture));
	SetFrameSlot(RefVar(gVarFrame), RefVar(MakeSymbol("worldMapPic")), map);
	Rect frame;
	FromObject(RefVar(Eval("ShapeBounds(MakeShape(worldMapPic))")), frame);
	EXPECT(frame.left == 0 && frame.top == 0 && frame.right == 360 && frame.bottom == 179);
	// drawn at a third of its size: the land black, the sea white
	Rect box;
	SetRect(&box, 0, 0, 120, 60);
	EraseRect(&box);
	LockRef(map);
	Ptr data = (Ptr) BinaryData(map);
	DrawPicture((PicHandle) &data, &box, false);
	UnlockRef(map);
	long land = InkIn(0, 0, 120, 60);
	EXPECT(land > 120 * 60 / 8 && land < 120 * 60 / 2);
	gRootView->Dirty(nil);
	Refresh();
}


// The journal: a stroke written while recording comes to the recorder as
// a big-endian JournalStroke of sample words; played back moved 110 pixels
// right, it reaches a view there as a click (its first point moved), the
// tablet bypassed meanwhile so the window's pen is ignored; a line made up
// by JournalReplayALine reaches a view under it too.
static void
TestJournal()
{
	TView* v = ViewOf("ctxJ := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200 + 0x800, viewFormat: 1, viewBounds: {left: 125, top: 10, right: 160, bottom: 95}, clicks: [], viewGestureScript: func(unit, kind) true, "
		"viewClickScript: func(unit) begin AddArraySlot(clicks, [GetPoint(0, unit), GetPoint(1, unit)]); nil end})");
	EXPECT(v != nil);
	Refresh();
	HostTabletWait(2);								// (whatever an earlier test left in the tablet buffer read first)
	gRecognition.Idle();
	EXPECT(RINT(Eval("recorder := {strokes: [], Record: func(s, size) AddArraySlot(strokes, [s, size]), Start: func() JournalStartRecord('Record, 1)}; recorder:Start()")) == 0);
	EXPECT(gJournallingState == 1);
	HostTabletPenDown(20, 30, 0);
	HostTabletPenMove(24, 32);
	HostTabletPenMove(28, 34);
	HostTabletPenMove(30, 35);
	HostTabletPenUp(0);
	gRecognition.Idle();
	EXPECT(RINT(Eval("Length(recorder.strokes)")) == 1);
	if (RINT(Eval("Length(recorder.strokes)")) != 1)
		return;
	RefVar recorded(Eval("recorder.strokes[0][0]"));
	const unsigned char* data = (const unsigned char*) BinaryData(recorded);
	ULong count = GetBigEndianWord(data + 4);
	EXPECT(count >= 4);
	EXPECT(GetBigEndianWord(data) == 4 * (count - 1) + 0x1c && (ULong) Length(recorded) == GetBigEndianWord(data)
		&& RINT(Eval("recorder.strokes[0][1]")) == (long) GetBigEndianWord(data));
	EXPECT(GetBigEndianWord(data + 8) == 0 && (long) GetBigEndianWord(data + 12) >= 0);
	EXPECT(GetBigEndianWord(data + 0x10) == HostTabletSample(20, 30, 3));
	EXPECT(RINT(Eval("JournalStopRecord()")) == 0 && gJournallingState == 0);

	// played back 100 pixels to the right: into the view
	SetFrameSlot(RefVar(gVarFrame), RefVar(MakeSymbol("journalStroke")), recorded);
	EXPECT(RINT(Eval("JournalReplayAStroke(journalStroke, 110, 0, 1, 100, 20)")) == 0 && gJournallingState == 2);
	EXPECT(NOTNIL(Eval("JournalReplayBusy()")));
	HostTabletWait(40);
	gRecognition.Idle();
	EXPECT(HostTabletBypassed());				// (the bypass outlives the replay, as on the machine)
	EXPECT(gJournallingState == 0 && ISNIL(Eval("JournalReplayBusy()")));
	EXPECT(RINT(Eval("Length(ctxJ.clicks)")) == 1 && RINT(Eval("ctxJ.clicks[0][0]")) == 130 && RINT(Eval("ctxJ.clicks[0][1]")) == 30);
	JournalStopReplay();
	EXPECT(!HostTabletBypassed());

	// a line made up on the spot
	EXPECT(RINT(Eval("JournalReplayALine(130, 80, 150, 80, nil, 60)")) == 0);
	HostTabletWait(60);
	gRecognition.Idle();
	EXPECT(gJournallingState == 0);
	EXPECT(RINT(Eval("Length(ctxJ.clicks)")) == 2 && RINT(Eval("ctxJ.clicks[1][0]")) == 130 && RINT(Eval("ctxJ.clicks[1][1]")) == 80);
	JournalStopReplay();
	Eval("RemoveView(GetRoot(), ctxJ)");
	Refresh();
}


// A picture turned into shapes (PictToShape over DrawPicture's toShapes):
// a hand-made version 1 picture - a rectangle painted and then framed, an
// oval painted, two lines end to end and a framed triangle - comes back
// as [style, rectangle, style, oval, style, polygon (the lines), polygon]
// - the painted-and-framed rectangle one shape whose style has both a fill
// and a pen, and no style again before the triangle, whose style frame is
// equal to the lines' (StylesEqual: all integers); drawn with DrawShape the shapes put down the same
// pixels as the picture drawn straight.  The ROM's world map (one packed
// bitmap) comes back as a style and a bitmap that draws the same too.
static void
TestPictToShape()
{
	unsigned char pic[128];
	long n = 2;
	auto byte = [&](long b) { pic[n++] = (unsigned char) b; };
	auto word = [&](long w) { PutBigEndianHalf(pic + n, (unsigned short) w); n += 2; };
	auto rect = [&](long t, long l, long b, long r) { word(t); word(l); word(b); word(r); };
	rect(0, 0, 40, 40);								// the frame
	byte(0x11); byte(0x01);							// version 1
	byte(0x01); word(10); rect(0, 0, 40, 40);		// ClipRgn
	byte(0x31); rect(2, 2, 10, 20);					// PaintRect
	byte(0x38);										// FrameSameRect
	byte(0x51); rect(12, 2, 20, 12);				// PaintOval
	byte(0x20); word(25); word(2); word(25); word(20);	// Line (v, h pairs): down the left
	byte(0x21); word(35); word(20);					// LineFrom: along the bottom
	byte(0x70); word(22); rect(22, 24, 38, 38);		// FramePoly: a triangle
	word(22); word(24); word(38); word(24); word(22); word(38);
	byte(0xff);
	PutBigEndianHalf(pic, (unsigned short) n);
	RefVar picture(AllocateBinary(RSSYMpicture, n));
	memmove(BinaryData(picture), pic, n);
	SetFrameSlot(RefVar(gVarFrame), RefVar(MakeSymbol("testPic")), picture);
	RefVar shapes(Eval("PictToShape(testPic, {left: 50, top: 0, right: 90, bottom: 40})"));
	EXPECT(IsArray(shapes) && Length(shapes) == 7);
	if (!IsArray(shapes) || Length(shapes) != 7)
	{
		fprintf(stderr, "PictToShape: %s, %ld\n", IsArray(shapes) ? "array" : "not an array", IsArray(shapes) ? Length(shapes) : (long) (Ref) shapes);
		for (long i = 0; IsArray(shapes) && i < Length(shapes); i++)
		{
			RefVar item(GetArraySlotRef(shapes, i));
			RefVar cls(ClassOf(item));
			fprintf(stderr, "  %ld: %s\n", i, IsSymbol(cls) ? SymbolName(cls) : "?");
		}
		return;
	}
	EXPECT(IsFrame(RefVar(GetArraySlotRef(shapes, 0))));
	EXPECT(EQ(RefVar(ClassOf(RefVar(GetArraySlotRef(shapes, 1)))), RSSYMrectangle));
	RefVar style(GetArraySlotRef(shapes, 0));
	EXPECT(RINT(RefVar(GetFrameSlotRef(style, RSSYMfillpattern))) == 5
		&& RINT(RefVar(GetFrameSlotRef(style, RSSYMpenpattern))) == 5
		&& RINT(RefVar(GetFrameSlotRef(style, RSSYMpensize))) == 1
		&& RINT(RefVar(GetFrameSlotRef(style, RSSYMtransfermode))) == 0);
	EXPECT(EQ(RefVar(ClassOf(RefVar(GetArraySlotRef(shapes, 3)))), RSSYMoval));
	Rect bounds;
	ShapeBounds(RefVar(GetArraySlotRef(shapes, 3)), &bounds);
	EXPECT(bounds.left == 52 && bounds.top == 12 && bounds.right == 62 && bounds.bottom == 20);
	RefVar lines(GetArraySlotRef(shapes, 5));
	RefVar triangle(GetArraySlotRef(shapes, 6));
	EXPECT(EQ(RefVar(ClassOf(lines)), RSSYMpolygon) && EQ(RefVar(ClassOf(triangle)), RSSYMpolygon));
	ShapeBounds(lines, &bounds);
	// (a polygon's bounds take in the pen below and to the right)
	EXPECT(bounds.left == 52 && bounds.top == 25 && bounds.right == 71 && bounds.bottom == 36);
	EXPECT(ISINT(RefVar(GetFrameSlotRef(RefVar(GetArraySlotRef(shapes, 4)), RSSYMpenpattern))));
	// drawn: the picture straight at 0..40, the shapes at 50..90
	Rect box;
	SetRect(&box, 0, 0, 100, 40);
	EraseRect(&box);
	LockRef(picture);
	Ptr data = (Ptr) BinaryData(picture);
	SetRect(&box, 0, 0, 40, 40);
	DrawPicture((PicHandle) &data, &box, false);
	UnlockRef(picture);
	DrawShape(shapes, RefVar(), MakePoint(0, 0));
	long differ = 0, ink = 0;
	for (long y = 0; y < 40; y++)
		for (long x = 0; x < 40; x++)
		{
			ink += Pixel(x, y);
			differ += Pixel(x, y) != Pixel(x + 50, y);
		}
	EXPECT(ink > 100);
	EXPECT(differ == 0);
	if (differ != 0)
		fprintf(stderr, "PictToShape: %ld pixels differ\n", differ);

	// the world map: a style and a bitmap, drawn as the picture is
	shapes = Eval("PictToShape(worldMapPic, {left: 0, top: 40, right: 60, bottom: 70})");
	EXPECT(IsArray(shapes) && Length(shapes) == 2);
	if (IsArray(shapes) && Length(shapes) == 2)
	{
		EXPECT(EQ(RefVar(ClassOf(RefVar(GetArraySlotRef(shapes, 1)))), RSSYMbitmap)
			|| IsFrame(RefVar(GetArraySlotRef(shapes, 1))));
		RefVar map(Rworldmapbitmap);
		SetRect(&box, 0, 0, 60, 70);
		EraseRect(&box);
		LockRef(map);
		data = (Ptr) BinaryData(map);
		SetRect(&box, 0, 0, 60, 30);
		DrawPicture((PicHandle) &data, &box, false);
		UnlockRef(map);
		DrawShape(shapes, RefVar(), MakePoint(0, 0));
		differ = 0;
		ink = 0;
		for (long y = 0; y < 30; y++)
			for (long x = 0; x < 60; x++)
			{
				ink += Pixel(x, y);
				differ += Pixel(x, y) != Pixel(x, y + 40);
			}
		EXPECT(ink > 60 * 30 / 8);
		EXPECT(differ == 0);
		if (differ != 0)
			fprintf(stderr, "PictToShape: %ld pixels of the world map differ\n", differ);
	}
	gRootView->Dirty(nil);
	Refresh();
}


// A default button that is no longer the key view's is dirtied once and
// forgotten, not dirtied on every update (which kept the host repainting
// the screen for ever under a popup, and looked like a hang).
static void
TestDefaultButtonChange()
{
	TView* button = ViewOf("ctxDB := AddView(GetRoot(), {viewClass: 74, viewFlags: 1, "
		"viewBounds: {left: 10, top: 10, right: 60, bottom: 30}})");
	EXPECT(button != nil);
	if (button == nil)
		return;
	TView* savedCaretView = gRootView->fCaretView;
	gRootView->fCaretView = nil;					// no key view: no default button
	gRootView->fDefaultButton = button;			// ... but one left over
	gRootView->UpdateDefaultButtonAndCaretSlip();
	EXPECT(gRootView->fDefaultButton == nil);		// dirtied once and forgotten
	gRootView->UpdateDefaultButtonAndCaretSlip();
	EXPECT(gRootView->fDefaultButton == nil);
	gRootView->fCaretView = savedCaretView;
	Eval("RemoveView(GetRoot(), ctxDB)");
	Refresh();
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_Views: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();
	InitGraf();
	InitFonts();
	RegisterTextNatives();
	RegisterViewNatives();
	RegisterPortNatives();
	RegisterLargeBinaryNatives();
	RegisterShapeNatives();
	RegisterJournalNatives();
	RegisterCorrectInfoNatives();
	RegisterBitmapNatives();
	RegisterStrokeBundleNatives();
	RegisterInkNatives();
	RegisterPickNatives();
	RegisterClipboardNatives();
	RegisterKeyboardNatives();
	RegisterRecConfigNatives();
	RegisterWordListNatives();
	InstallHostNatives();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	// what the boot makes: vars.fonts, the ROM's font families by their
	// family symbols ('espy, 'newYork, 'geneva, 'handwriting: the ROM's
	// globals template has fonts: {_proto: {espy: @80, ...}})
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
	{
		RefVar family(GetArraySlotRef(list, i));
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), family);
	}
	SetFrameSlot(RefVar(gVarFrame), RSSYMfonts, fonts);
	// ... and vars.stdForms, the stationery a template without a
	// viewClass names (the clipping's icon is a 'para)
	SetFrameSlot(RefVar(gVarFrame), RSSYMstdforms, RefVar(Rstdforms));
	// a recognition configuration that keeps ink words, for the views that
	// are given ink words to keep: in any other a paragraph's ProcessStyles
	// has them read as they come
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "rcInkOrTextConfig")), RefVar(Rrcinkortext));
	// an ink word asks the user's preferences for its scale and its pen;
	// on a Newton the boot has set them long before anything makes one
	{
		RefVar config(AllocateFrame());
		SetFrameSlot(config, RefVar(RSSYMinkwordscaling), RefVar(MAKEINT(100)));
		SetFrameSlot(config, RefVar(RSSYMuserpensize), RefVar(MAKEINT(2)));
		SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, config);
	}
	// ... and the recognition system asks the locale which language it
	// is reading, which the boot has also set (the tests that want a
	// real bundle put one here themselves)
	{
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
		gNewtIsAliveAndWell = true;		// (the boot is over: the root draws no splash)
		// the viewJustify constants for the templates
		static const struct { const char* fName; long fValue; } kConstants[] = {
			{ "vjParentClip", vjParentClip }, { "vjSiblingRightH", vjSiblingRightH }, { "vjSiblingBottomV", vjSiblingBottomV },
			{ "vjSiblingCenterH", vjSiblingCenterH }, { "vjSiblingTopV", vjSiblingTopV }, { "vjSiblingLeftH", vjSiblingLeftH },
			{ "vjTopRatio", vjTopRatio }, { "vjBottomRatio", vjBottomRatio }, { "vjChildrenLasso", vjChildrenLasso } };
		for (unsigned i = 0; i < sizeof(kConstants) / sizeof(kConstants[0]); i++)
			SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) kConstants[i].fName)), RefVar(MAKEINT(kConstants[i].fValue)));
		Refresh();
		EXPECT(MapIs(ExpWhite, "the root"));
		TestStructure();
		TestDrawing();
		TestJustify();
		TestScripts();
		TestTextView();
		TestPictureView();
		TestPolygonView();
		TestParagraphView();
		TestNegativeWidthParagraph();
		TestGaugeView();
		TestShapes();
		TestCommands();
		TestHilite();
		TestDataHilites();
		TestContainerView();
		TestEditView();
		TestKeyboard();
		TestCaret();
		TestTyping();
		TestKeyChain();
		TestSelection();
		TestParagraphTap();
		TestIdlers();
		TestFrontKeyViews();
		TestEmptiedParagraph();
		TestGrayShrink();
		TestPickView();
		TestLayoutTable();
		TestTimeDownAView();
		TestHiliteOffsets();
		TestFontQueries();
		TestExtractData();
		TestDatesDrawing();
		TestClicks();
		TestTrailingReturn();
		TestScrubbing();
		TestCaretGesture();
		TestLineGesture();
		TestEffects();
		TestInkOnThePage();
		TestStrokeAndInkNatives();
		TestInkWordOnThePage();
		TestInkWordAtTheCaret();
		TestRecognisedWord();
		TestInkWordInText();
		TestJoinInk();
		TestSplitInk();
		TestWordGeometry();
		TestWordIntoParagraph();
		TestInkWordAtPageCaret();
		TestRemoteForCorrector();
		TestValidatePolyBounds();
		TestRichStringIntoParagraph();
		TestCorrectInfo();
		TestCorrectInfoEditing();
		TestReplaceCharacter();
		TestInkInRichString();
		TestWordInfo();
		TestWordIntoField();
		TestEditViewDrop();
		TestParagraphDrop();
		TestSelectionClicks();
		TestDistort();
		TestPolygonEditing();
		TestModalSafeShow();
		TestEditCommands();
		TestPolygons();
		TestDefaultButtonChange();
		TestModalDialogViews();
		TestInsertItems();
		TestClipboard();
		TestHiliteStroke();
		TestListView();
		TestMeetingView();
		TestPicture();
		TestPictToShape();
		TestJournal();						// (last: its replays move the clock on)
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
		if (strncmp(_info.exception.name, "evt.ex.fr", 9) == 0)
		{
			PrintObject(*(RefStruct*) _info.exception.data, 0);
			fprintf(stderr, "\n");
		}
	}
	end_try;
	ClosePort(&gPort);
	if (failures == 0)
		printf("test_Views: all passed\n");
	else
		printf("test_Views: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
