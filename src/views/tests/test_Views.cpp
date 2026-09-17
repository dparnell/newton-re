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
#include "RootView.h"
#include "TextView.h"
#include "ParagraphView.h"
#include "GaugeView.h"
#include "PickView.h"
#include "DrawShape.h"
#include "Commands.h"
#include "Keyboard.h"
#include "REPTranslators.h"
#include "Bits.h"
#include "Application.h"
#include "StyleRuns.h"
#include "Rects.h"
#include "Ports.h"
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
	EXPECT(gRootView->fChildren->Count() == 1 && ISNIL(Eval("ctxU.viewCObject")));	// the view's own context protos to ctxU
	TView* u = gRootView->fChildren->At(0);
	EXPECT(u->ProtoedFrom(RefVar(Eval("ctxU"))) && (u->fFlags & vVisible));
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
	EXPECT(p->LineCount() == 3);
	if (p->LineCount() == 3)
	{
		EXPECT(p->Line(0).fStart == 0 && p->Line(0).fEnd == 6 && p->Line(1).fStart == 6 && p->Line(1).fEnd == 12 && p->Line(2).fStart == 12 && p->Line(2).fEnd == 17);	// a line keeps the space that ends it (LineInfo's endsWithSpace)
		EXPECT(p->Line(0).fEndsWithSpace && !p->Line(2).fEndsWithSpace);
		EXPECT(p->Line(0).fHeight == p->fLineHeight && p->Line(1).fBounds.top == 10 + p->Line(0).fHeight && p->Line(0).fBounds.left == 20);
		EXPECT(p->TextBounds().top == 10 && p->TextBounds().bottom == 10 + 3 * p->Line(0).fHeight);
	}
	EXPECT(EQRef(p->GetStyles(), Eval("espy12")));
	Eval("ctxQ:Dirty()");
	Refresh();
	long inkLeft, inkRight;
	long lineHeight = p->Line(0).fHeight;
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

	// style runs: the second word in a bigger font makes its line taller
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "espy18")), RefVar(MAKEINT(PackFont(0, 18, 0))));
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 70, bottom: 90}, viewJustify: 0, viewFont: espy12, text: \"Hello World again\", styles: [6, espy12, 6, espy18, 5, espy12]})");
	EXPECT(p->LineCount() == 3);
	if (p->LineCount() == 3)
	{
		EXPECT(p->Line(1).fHeight > p->Line(0).fHeight && p->Line(2).fHeight == p->Line(0).fHeight);
		EXPECT(p->Line(1).fFirstObj == 1 && p->Line(0).fFirstObj == 0 && p->Line(2).fFirstObj == 2);
	}
	EXPECT(IsArray(p->GetStyles()));
	Eval("ctxQ:Dirty()");
	Refresh();
	InkExtent(10 + p->Line(0).fHeight, 10 + p->Line(0).fHeight + p->Line(1).fHeight, &inkLeft, &inkRight);
	EXPECT(inkLeft == 20 && inkRight > 46);								// "World" in espy 18 is wider than "Hello" in 12
	Eval("ctxQ:Close()");

	// viewLineSpacing: the lines that far apart when the font fits it
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 70, bottom: 90}, viewJustify: 0, viewFont: espy12, viewLineSpacing: 18, text: \"Hello World\"})");
	EXPECT(p->fLineSpacing == 18 && p->GetInterLineSpacing() == 18 && p->LineCount() == 2 && p->Line(1).fBounds.top == 28);
	Eval("ctxQ:Close()");

	// justified: flush right and at the bottom
	p = (TParagraphView*) ViewOf("ctxQ := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 70, bottom: 90}, viewJustify: 9, viewFont: espy12, text: \"Hello\"})");
	EXPECT(p->LineCount() == 1 && p->Line(0).fBounds.bottom == 90);
	Eval("ctxQ:Dirty()");
	Refresh();
	InkExtent(10, 90, &inkLeft, &inkRight);
	EXPECT(inkRight == 69 && inkLeft == 43);								// flush right: the advance's last pixel is blank
	InkExtent(10, 90 - p->Line(0).fHeight, &inkLeft, &inkRight);
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
	EXPECT(p->Line(0).fBounds.left == 60 && p->Line(0).fBounds.top == 50 && p->fCachedBounds.left == 60);
	InkExtent(50, 70, &inkLeft, &inkRight);
	EXPECT(inkLeft == 60);
	InkExtent(10, 30, &inkLeft, &inkRight);
	EXPECT(inkRight == 0);
	Eval("ctxQ:Close()");
	Refresh();
	EXPECT(MapIs(ExpWhite, "paragraph closed"));
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
	// PostCommandParam with a frame parameter: aeAddHilite appends to the view's hilites
	Eval("PostCommandParam(ctxC, 0x47, {hilite: 'h1})");
	EXPECT(RINT(Eval("Length(ctxC.hilites)")) == 1 && EQRef(Eval("ctxC.hilites[0]"), Intern((char*) "h1")));
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
static void
TestKeyboard()
{
	const ULong kGermanyBundle = 0x003c10ed;		// the ROM's locale bundle 'Germany
	RefVar bundle(TranslateROMRef(kGermanyBundle));
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
	// a dead key: the acute accent, then a completed and an uncompleted character
	EXPECT(TranslateKey(0x18, true, 0, &dead) == 0xb4 && dead != 0);
	EXPECT(TranslateKey(0, true, 0, &dead) == 0xe1 && dead == 0);			// a acute
	TranslateKey(0x18, true, 0, &dead);
	EXPECT(TranslateKey(0x12, true, 0, &dead) == '1' && dead == 0);		// no completion for 1: as it is
	TranslateKey(0x18, true, 0, &dead);
	EXPECT(TranslateKey(0x18, true, 0, &dead) == 0xb4 && dead == 0);		// the accent itself again
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
	EXPECT(KeyIn(0x18, true, (TView*) -1) == 0 && gHardKeyDeadState != 0);
	KeyIn(0x18, false, (TView*) -1);
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
	HandleKeyEvent(&down);
	EXPECT(gKeyboardConnected);		// told by the first key
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
	Eval("ctxB:Close()");
	Refresh();
	// a paragraph as the key view: no caret without a keyboard, then the caret at the offset
	TParagraphView* p = (TParagraphView*) ViewOf("ctxC2 := AddView(GetRoot(), {viewClass: 81, viewFlags: 3, viewBounds: {left: 20, top: 10, right: 120, bottom: 40}, viewJustify: 0, viewFont: espy12, text: \"Hello World\"})");
	Eval("ctxC2:Dirty()");
	Refresh();
	EXPECT(gRootView->fCaretView == nil && !gRootView->fCaretShowing);
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
	EXPECT(!EmptyRect(&caret) && caret.right - caret.left == 12 && caret.bottom - caret.top == 11 && caret.left == caretBox.left - 5 && caret.top == caretBox.bottom);
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
	// a point to the caret: the character nearest a tap
	Point pt;
	pt.h = caretBox.left;
	pt.v = 16;
	Rect tapped;
	p->PointToCaret(pt, &tapped, nil);
	EXPECT(tapped.left == caretBox.left && p->PointToOffset(pt) == 5);
	pt.h = 22;
	EXPECT(p->PointToOffset(pt) == 0);
	pt.h = 119;
	EXPECT(p->PointToOffset(pt) == 11);
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
	const ULong kGermanyBundle = 0x003c10ed;
	RefVar bundle(TranslateROMRef(kGermanyBundle));
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
	TypeKey(6);		// y (the German layout: key 16 is z)
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
	TypeKey(16);	// z
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
		RefVar bundle(TranslateROMRef(0x003c10ed));
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
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Views: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();
	InitGraf();
	InitFonts();
	RegisterTextNatives();
	RegisterViewNatives();
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
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, RefVar(AllocateFrame()));
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
		TestParagraphView();
		TestGaugeView();
		TestShapes();
		TestCommands();
		TestHilite();
		TestKeyboard();
		TestCaret();
		TestTyping();
		TestIdlers();
		TestPickView();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
		if (strncmp(_info.exception.name, "evt.ex.fr", 9) == 0)
		{
			HostInitREP(stderr, nil);
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
