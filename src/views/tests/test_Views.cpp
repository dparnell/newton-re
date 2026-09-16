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
	EXPECT(ShapeBounds(RefVar(Eval("pict")), &box) && box.right == 8 && box.bottom == 4);
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
	// what the boot makes: vars.fonts, the ROM's font families by symbol
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
	{
		RefVar family(GetArraySlotRef(list, i));
		SetFrameSlot(fonts, RefVar(GetFrameSlotRef(family, Intern((char*) "screenSym"))), family);
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
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	if (failures == 0)
		printf("test_Views: all passed\n");
	else
		printf("test_Views: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
