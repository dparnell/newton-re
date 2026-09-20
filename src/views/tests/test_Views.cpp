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
#include "Hilites.h"
#include "ContainerView.h"
#include "EditView.h"
#include "GaugeView.h"
#include "PickView.h"
#include "DrawShape.h"
#include "Commands.h"
#include "Keyboard.h"
#include "REPTranslators.h"
#include "Bits.h"
#include "Application.h"
#include "UnitPublic.h"
#include "Recognizer.h"
#include "StrokeCentral.h"
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
	// PostCommandParam with a frame parameter: aeAddHilite appends it to the
	// view's hilites as it stands (a real hilite is a THilite pointer Ref, so
	// the slot is emptied again rather than left holding a frame)
	Eval("PostCommandParam(ctxC, 0x47, {hilite: 'h1})");
	EXPECT(RINT(Eval("Length(ctxC.hilites)")) == 1 && EQRef(Eval("ctxC.hilites[0].hilite"), Intern((char*) "h1")));
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

	// and RemoveAllHilites empties the array (the objects disposed of with it)
	v->RemoveAllHilites();
	EXPECT(!v->Hilited() && RINT(Eval("Length(ctxDH.hilites)")) == 0 && ISNIL(v->FirstHilite()));
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

	// (a complete hilite draws as the container's bounds filled, but nothing
	// in the reconstruction calls a view's DrawHilites yet except the
	// paragraph, which draws its own: the generic draw path is NOT YET)
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


static void
TestClicks()
{
	gRecognition.Init(1);
	gStrokeWorld.Init();
	HostTabletInit();
	RegisterUnitNatives();
	// the tablet's calibration, which the Setup assistant asks for: the
	// host has no inker to ask, so nothing needs calibrating, there is
	// nothing to read back, and calibrating answers that it worked
	EXPECT(ISNIL(Eval("IsTabletCalibrationNeeded()")));
	EXPECT(ISNIL(Eval("GetCalibration()")));
	EXPECT(ISNIL(Eval("SetCalibration(nil)")));
	gInkerCalibrated = 0;
	EXPECT(ISNIL(Eval("CalibrateTablet()")) && gInkerCalibrated == 1);
	Eval("userConfiguration.userPenSize := 1");		// (the ink is let out by the pen size)
	TView* v = ViewOf("ctxC := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200 + 0x800, viewBounds: {left: 60, top: 40, right: 120, bottom: 80}, viewFormat: 1, clicks: [], gestures: [], "
		"viewClickScript: func(unit) begin AddArraySlot(clicks, [GetPoint(0, unit), GetPoint(1, unit), StrokeDone(unit), StrokeBounds(unit), GetUnitDownTime(unit), CountUnitStrokes(unit)]); nil end, "
		"viewGestureScript: func(unit, kind) begin AddArraySlot(gestures, [kind, GetPoint(4, unit), GetPoint(5, unit), GetUnitUpTime(unit), GetPoint(8, unit)]); true end})");
	Eval("ctxC:Dirty()");
	Refresh();
	// a tap: the click at the pen-down (the stroke already done here), and - the click script not having taken it - the tap at the pen-up
	HostTabletPenDown(90, 60, 1000);
	HostTabletPenUp(1004);
	IdleStrokes();
	EXPECT(RINT(Eval("Length(ctxC.clicks)")) == 1 && RINT(Eval("ctxC.clicks[0][0]")) == 90 && RINT(Eval("ctxC.clicks[0][1]")) == 60 && NOTNIL(Eval("ctxC.clicks[0][2]")));
	EXPECT(RINT(Eval("ctxC.clicks[0][3].left")) == 90 && RINT(Eval("ctxC.clicks[0][3].bottom")) == 61 && RINT(Eval("ctxC.clicks[0][4]")) == 1000 && RINT(Eval("ctxC.clicks[0][5]")) == 1);
	EXPECT(RINT(Eval("Length(ctxC.gestures)")) == 1 && RINT(Eval("ctxC.gestures[0][0]")) == aeTap && RINT(Eval("ctxC.gestures[0][1]")) == 90 && RINT(Eval("ctxC.gestures[0][3]")) == 1004 && RINT(Eval("ctxC.gestures[0][4].y")) == 60);
	EXPECT(gRecognition.fClickView == v && gStrokeWorld.CurrentStroke() == nil);
	// a tap where no view takes clicks: nothing, and no click view
	HostTabletPenDown(10, 90, 2000);
	HostTabletPenUp(2003);
	IdleStrokes();
	EXPECT(RINT(Eval("Length(ctxC.clicks)")) == 1 && RINT(Eval("Length(ctxC.gestures)")) == 1 && gRecognition.fClickView == nil);
	// a press and drag, fed a record a tick as the click script's TrackHilite waits: hilited inside, not outside, ended inside; the click taken (true), so no gesture follows
	Eval("ctxC.viewClickScript := func(unit) begin AddArraySlot(clicks, [StrokeDone(unit), :TrackHilite(unit), GetPointsArrayXY(unit), StrokeDone(unit)]); true end");
	HostTabletQueuePenDown(90, 60, 3000);
	HostTabletQueuePenMove(95, 62);
	HostTabletQueuePenMove(100, 65);
	HostTabletQueuePenMove(150, 65);
	HostTabletQueuePenMove(110, 65);
	HostTabletQueuePenUp(3040);
	HostTabletPump();
	IdleStrokes();
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
	IdleStrokes();
	HostTabletPenDown(91, 61, 5010);
	HostTabletPenUp(5013);
	IdleStrokes();
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
	IdleStrokes();
	EXPECT(RINT(Eval("ctxS.viewValue")) == 70 && RINT(Eval("Length(ctxS.changes)")) == 1 && RINT(Eval("ctxS.changes[0][0]")) == 30 && RINT(Eval("ctxS.changes[0][1]")) == 70);
	EXPECT(HostTabletQueued() == 0 && gStrokeWorld.CurrentStroke() == nil && slider->fFlags & vVisible);
	// a read-only gauge ignores the pen
	Eval("SetValue(ctxS, 'viewFlags, 3 + 0x200)");
	HostTabletPenDown(40, 55, 7000);
	HostTabletPenUp(7003);
	IdleStrokes();
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
	IdleStrokes();
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
	IdleStrokes();
	EXPECT(NOTNIL(Eval("cancelled")) && ISNIL(Eval("picked")) && gRootView->fChildren->Count() == 0);		// (the ROM: the cancel script, then the action script with nil)
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
	IdleStrokes();
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
	IdleStrokes();
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

	// drag and drop: a press-drag from a source view carries a 'text item
	// to a target with a viewDropScript that accepts 'text
	Eval("dropped := nil");
	TView* src = ViewOf("ctxDS := AddView(GetRoot(), {viewClass: 74, viewFlags: 1 + 0x200, viewBounds: {left: 20, top: 40, right: 60, bottom: 70}, viewFormat: 1, "
		"viewClickScript: func(unit) begin :DragAndDrop(unit, :GlobalBox(), nil, nil, [{types: ['text], dragRef: \"hi there\"}]); true end, viewGetDropDataScript: func(dropType, dragRef) dragRef})");
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
	HostTabletQueuePenUp(0);
	HostTabletPump();
	IdleStrokes();
	EXPECT(NOTNIL(Eval("StrEqual(dropped, \"hi there\")")));		// the target's drop script got the data
	Eval("RemoveView(GetRoot(), ctxDS); RemoveView(GetRoot(), ctxDT)");
	Refresh();
	EXPECT(MapIs(ExpWhite, "drag and drop closed"));
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
	IdleStrokes();
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
	IdleStrokes();
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
	IdleStrokes();
	EXPECT(d->viewBounds.right == 160 && d->viewBounds.left == 120 && d->viewBounds.top == 50);
	Refresh();
	EXPECT(InkIn(120, 50, 160, 80) == 40 * 30 && InkIn(0, 0, 120, 100) == 0);
	// a press without a move: nothing moves (:Drag answers true all the same)
	Eval("ctxD.dragged := 'untouched");
	HostTabletQueuePenDown(140, 65, 14000);
	HostTabletQueuePenMove(141, 65);
	HostTabletQueuePenUp(14010);
	HostTabletPump();
	IdleStrokes();
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
	p->MakeHilite(0, 5, false);
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
	// RemoveHilite drops the one hilite (the array emptied)
	p->RemoveHilite(RefVar(p->FirstHilite()));
	EXPECT(ISNIL(p->FirstHilite()) && RINT(Eval("Length(ctxS.hilites)")) == 0);
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
	EXPECT(RINT(Eval("ctxS.styles[3].face")) == 1);				// the selection's run is bold
	p->RemoveAllHilites();
	Eval("ctxS.text := \"Hello World\"; ctxS.styles := nil; ctxS:SyncView()");	// back to plain for the checks below
	// typing over a selection replaces it in one edit
	gKeyboardConnected = true;
	Eval("ClearUndoStacks()");
	gApplication->Idle();
	p->RemoveAllHilites();
	p->MakeHilite(0, 5, false);		// select "Hello"
	EXPECT(NOTNIL(p->FirstHilite()));
	TypeKey(16);	// y: replaces the selection
	EXPECT(NOTNIL(Eval("StrEqual(ctxS.text, \"y World\")")) && ISNIL(p->FirstHilite()) && p->fCaretOffset == 1);
	// backspace over a selection deletes it
	p->MakeHilite(0, 2, false);		// select "y "
	TypeKey(0x33);	// backspace
	EXPECT(NOTNIL(Eval("StrEqual(ctxS.text, \"World\")")) && ISNIL(p->FirstHilite()));
	// an arrow collapses a selection to its edge
	p->MakeHilite(1, 4, false);
	TypeKey(0x7b);	// left arrow: caret to the selection start
	EXPECT(ISNIL(p->FirstHilite()) && p->fCaretOffset == 1);
	gKeyboardConnected = false;
	Eval("ctxS.text := \"Hello World\"; ctxS:SyncView()");		// back to the plain text for the check below
	Refresh();
	// removing the hilites restores the plain text
	p->RemoveAllHilites();
	Refresh();
	// (the hilites array is emptied, not cleared: RemoveHilite takes them out one by one)
	EXPECT(ISNIL(p->FirstHilite()) && RINT(Eval("Length(ctxS.hilites)")) == 0
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
	// a tap between the 3rd and 4th character places the caret there
	Rect box3;
	p->OffsetToBounds(3, &box3);
	Point tap;
	tap.h = (short) (box3.left + 1);
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
		TestPickView();
		TestLayoutTable();
		TestTimeDownAView();
		TestHiliteOffsets();
		TestFontQueries();
		TestExtractData();
		TestClicks();
		TestEffects();
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
