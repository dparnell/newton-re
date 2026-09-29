# The view system

Reverse-engineering notes on Newton's view system - the C++ `TView`
hierarchy behind every NewtonScript view frame - and its reconstruction
in `src/views/`.  How each fact was established is stated with it; the
reconstruction cites the ROM function each of its functions comes from.

## What the ROM has

The symbols name 22 view classes with 831 methods between them
(`analysis`: the `__NTView...` class prefixes of `symbols.txt`):
`TView` (160 methods, constructor `TView::Constructor` 0x00264430),
`TRootView` (71), `TContainerView` (29), `TParagraphView` (153, the text
editor), `TEditView` (77), `TPictureView`, `TListView`, `TPickView`,
`TDataView`, `TKeyboardView`, `TPolygonView` (ink), `TGaugeView`,
`TSliderView`, `TMonthView`, `TMeetingView`, `TRemoteView`, `TPrintView`,
`TTextView`, `TXView` (the new text engine's view, 101), the math views.
`TView` derives from `TxObject` (its destructor 0x00266bbc calls
`TxObject::~TxObject`).  About three hundred of the unbound NewtonScript
natives are view functions (`src/frames/ROMNatives.cpp`: `SetValue`,
`Dirty`, `RelBounds`, `SetBounds`, `AddView`, `RemoveView`, `RefreshViews`,
`GlobalBox`, `LocalBox`, `GetView`, `Open`, `Close`, `Toggle`, `Show`,
`Hide`, `SyncView`, `SyncChildren`, `LayoutTable`, `SetKeyView`, ...).

## TView's layout (from the constructor and the slot cache)

`TView::Constructor(RefArg context, TView* parent)` 0x00264430 sets
+0x20 `fChildren` to `TView::gEmptyViewList` (0x0c101a1c), +0x1c
`fParent`, +0x24 `fContext` (a RefStruct: the view's *context frame*),
+0x18 to -1 and +0x2c to 0xffff (the slot cache masks, below), +0x04
`fId` from a counter at 0x0c102050; puts the parent's context into the
context's `_parent` and the view's address (`AddressToRef`) into its
`viewCObject`; builds the contexts of the `stepAllocateContext` (slot
cache 28) and `allocateContext` (23) pairs (`[slot, template, ...]`:
each template's context built with `BuildContext` and stored in the
view's context under the slot); sets flag 0x10000000 and calls virtual
slot 13 (`SetupForm`?).  `gRootView` is at 0x0c101a20 (`BuildContext`
puts `gRootView->fContext` into a new context's `_parent`).

The *slot cache*: `Rslotcachetable` (0x5c94c5) lists 34 slot symbols -
0 `viewQuitScript`, 1 `styles`, 2 `tabs`, 3 `realData`, 4 `text`, 5
`viewTransferMode`, 6 `viewFont`, 7 `viewOriginY`, 8 `viewOriginX`, 9
`viewJustify`, 10 `viewFlags`, 11 `viewDrawScript`, 12 `viewIdleScript`,
13 `viewSetupChildrenScript`, 14 `viewChildren`, 15 `buttonClickScript`,
16 `viewClickScript`, 17 `viewFormat`, 18 `viewBounds`, 19
`viewShowScript`, 20 `viewStrokeScript`, 21 `viewGestureScript`, 22
`viewHiliteScript`, 23 `allocateContext`, 24 `keyPressScript`, 25
`viewKeyUpScript`, 26 `viewKeyDownScript`, 27 `viewKeyRepeatScript`, 28
`stepAllocateContext`, 29 `viewChangedScript`, 30 `viewRawInkScript`, 31
`viewInkWordScript`, 32 `viewKeyStringScript`, 33
`viewCaretActivateScript` - whose Refs the boot interns into
`slotCacheRefs` (0x0c10204c).  `TView::GetCacheProto(index)` 0x0025d474
and `GetCacheVariable` 0x0025d3e4 look a slot up through the context's
proto/parent chains only while the view's mask bit for it (+0x18 for
indices 0-31, +0x2c for 32-33) is set, clearing the bit when the lookup
finds nil (`InvalidateSlotCache` 0x00261d94 sets bits again).

## Building a context (`TView::BuildContext` 0x0025c634, `FBuildContext` 0x0025c9e4)

A view *template* (the NewtonScript frame, usually protoed) becomes a
*context* frame the view runs in: the template's `viewClass` (or its
`viewStationery`, looked up in `vars.stdForms` for the class; an `ink`
slot makes it `'poly`) says the C++ class - bit 0x10000 of the class
means the template is *data* (`realData`: the context protos to the
stationery's form with the data as `realData`), otherwise the template
itself is the context's `_proto`; a template whose `viewFlags` lacks bit
1 (`vVisible`) is skipped unless forced.  The context is a clone of
`Rcanonicalcontext` (`{_parent, _proto, viewCObject}`) or
`Rcanonicaldatacontext`, with `_parent` the root view's context until
the constructor sets the real parent.  Errors: `evt.ex` -8502 (no view
class), -8503 (no such stationery), -8504 (no viewFlags).

## Making a view (`BuildView` 0x0025ca18)

`TView::AddView(template)` 0x0025d274 builds the context (unless the
template has a `preallocatedContext`) and calls `BuildView(parent,
context)`, the factory: a switch on the context's `viewClass` number
allocates the C++ object (`TxObject::operator new`, the size below),
sets its vtable (every view's first vtable slot is `ClassID`: `TView`
derives from `TResponder`, which derives from `TxObject`), allocates the
RefStructs among its fields (the context at +0x24 for all; more for the
subclasses) and calls `Constructor`.  The classes, their numbers and
sizes as the switch has them:

| viewClass | class | size |
|---|---|---|
| 74 (0x4a) | `TView` | 0x30 |
| 75, 76 | `TPictureView` | 0x30 |
| 77 | `TEditView` | 0x50 |
| 78, 79 | `TKeyboardView` (on `TView`) | 0x94 |
| 80 | `TMonthView` (on `TView`) | 0x94 |
| 81 | `TParagraphView` (on `TDataView`) | 0xd0 |
| 82 | `TPolygonView` | 0x30 |
| 83, 84 | `TMathExpView` (on `TContainerView`, 0x44, RefStruct at +0x38) | 0x44 |
| 85 | `TMathOpView` | 0x34 |
| 86 | `TMathLineView` | 0x38 |
| 87, 88 | `TRemoteView` | 0x58 |
| 89-91 | `TPickView` (on `TView`; RefStructs at +0x34, +0x54, +0xa4, +0xac) | 0xbc |
| 92 | `TGaugeView` | 0x38 |
| 93, 94 | `TPrintView` | 0x48 |
| 95 | `TMeetingView` | 0x3c |
| 96 | `TSliderView` | 0x4c |
| 97, 98 | `TTextView` | 0x34 |
| 99 | `TListView` | 0x5c |
| 100, 101 | `TClipboard` (on `TView`; RefStructs at +0x30, +0x34, +0x40) | 0x44 |
| 102-105 | `TOutline` (on `TView`; RefStructs at +0x30, +0x60, +0x64) | 0x68 |
| 106, 107 | `THelpOutline` (on `TView`) | 0x68 |
| 108 | `TXView` (on `TView`) | 0x60 |

(`TContainerView` itself, 0x44 bytes, is what `clContainerView`'s
number maps to among 78/83; the NTK constants `clView` 74,
`clPictureView` 76, `clEditView` 77, `clKeyboardView` 79,
`clMonthView` 80, `clParagraphView` 81, `clPolygonView` 82,
`clRemoteView` 88, `clPickView` 91, `clGaugeView` 92, `clOutline` 105
agree with the switch.)

## The reconstruction (`src/views/`)

`View.h` declares `TView` with the ROM's fields at their offsets and its
methods in the vtable's order (`analysis/vtable.py build/MP2x00US 0x1f750`),
over `TResponder` and `TxObject` (`TxObject::operator new` clears the
memory - NewPtrClear - so a fresh view's fields are zero).  `ViewFlags.h`
has the constants: the viewFlags bits (the names TView::Dump 0x0025e33c
prints; `vIsInSetupForm` 0x10000000, `vHasIdlerHint` 0x20000000,
`vIsMarked` 0x40000000 and `vIsInSetup2` 0x80000000 are the private ones
above the 28 bits the context's viewFlags slot holds), the viewJustify
bits as JustifyBounds uses them (below), the viewFormat fields as
PreDraw/PostDraw draw them, the class numbers (each class's `ClassID`),
the slot cache indices and the -85xx errors.

**Making a view.**  `TView::BuildContext(template, force)` 0x0025c634 and
`BuildView(parent, context)` 0x0025ca18 are as described above; the
host's `BuildView` makes a `TView` for every class number (the
subclasses are NOT YET; an unknown number is -8501).  `Constructor`
0x00264430: the context linked (`_parent`, `viewCObject`), the
`allocateContext`/`stepAllocateContext` pairs built, `vIsInSetupForm`
set around `SetupForm` (the `viewSetupFormScript`; a script that closes
the view marks it `vIsBeingDeleted` and the Constructor throws -8501),
the flags and format read, a child of the root view given a `TClipper`
(kept in the context's `viewclipper` slot as an address), the parent's
`AddView(TView*)` 0x0025d994 (in front of the floaters), then, under a
handler that takes the view out again on a Throw: `SetBounds` from
`viewBounds` (-8505 without one; an application hanging below the screen
moved up), `declareSelf`, `AddViews(false)` 0x00260cd4 (`viewChildren` and
`stepChildren`, after the `viewSetupChildrenScript`; a `vjReflow` view
stops at the first child below its bottom), the `vjChildrenLasso` sizing
(the union of the children at the view's origin, written back through
`DejustifyBounds`), `SetupDone`.  `AddView(RefArg)` 0x0025d274 is
`BuildContext` + `BuildView` (a template naming a `preallocatedContext`
takes that variable as its context).  Nothing draws a newly added view:
the caller dirties or shows it (the NewtonScript `AddView` says so too).
`Delete` 0x0026564c runs the `viewQuitScript` (answering `'postQuit`
asks for the `viewPostQuitScript` after the children are gone), removes
the children, frees the clipper, tells the root view (`ForgetAboutView`)
and deletes the object; `RemoveView`/`RemoveChildView` 0x0025da28/34
hide first and free the list when empty.

**The context.**  `GetProto` 0x00269074 is `GetProtoVariable` on the
context, `GetVar` 0x00269080 `GetVariable` (the proto chain, then the
parent chain); `SetContextSlot` sets the context's own slot, `SetDataSlot`
the data frame's (`realData` for a data view, else the context).  The
slot cache: `GetCacheProto/GetCacheVariable(index)` 0x0025d474/0x0025d3e4
answer nil at once when the view's mask bit is clear, and clear it when
a lookup finds nil; `InvalidateSlotCache` sets it again (`Sync` does for
viewJustify, `SetOrigin` for the origins).  `RunScript(tag, args,
lookupVars)` 0x00261dc8 sends the script to the context with
`DoProtoMessage` (or `DoMessage` when the parent chain counts) unless
`vNoScripts`; `RunCacheScript(index, ...)` 0x00261c84 the same for a cached
slot.  `SetFlags`/`ClearFlags` 0x0025d360/0x00268c78 write `viewFlags` back
to the context when `vVisible` or `vSelected` change.  `SetValue`
0x00268ab4 sets the slot, keeps `fFlags`/`fViewFormat` for viewFlags and
viewFormat, syncs for viewBounds/viewFormat/viewJustify/viewFont and sends
`Changed` 0x00268d08 (the `viewTie` pairs told, the `viewChangedScript`
run - even for a `vNoScripts` view - and the view dirtied); `GetValue`
0x002688d0 converts to a `'string` (SPrintObject) or `'int` (a char or
boolean) when asked.  `Sync` 0x0025d730 runs `SetupForm` again for a view
that is set up, re-reads viewBounds and viewJustify, and moves the view
(`Offset`) when its size is unchanged, else dirties, re-sets and dirties
again.

**Justification** (`JustifyBounds` 0x00262224, read from the assembly;
`DejustifyBounds` 0x00262b1c its inverse).  The template's viewBounds are
offset by a *base*: the parent's contents origin (its top left less
`viewOriginX/Y`; the top left itself for `vjParentClip` 0x100) - the
application area of `vars.displayParams` for a child of the root view.
The sibling bits place the view against the previous sibling instead
(H: 0x200 centred, 0x400 after its right, 0x600 full - the bounds added
to the sibling's, 0x800 at its left; V: 0x1000 centred, 0x2000 below,
0x3000 full, 0x4000 at its top); the ratio bits (`vjLeftRatio` 0x4000000,
`vjRightRatio` 0x8000000, `vjTopRatio` 0x10000000, `vjBottomRatio`
0x20000000) scale the bounds by a hundredth of the sibling's (else the
parent's) size; the parent bits (H: 0x10 centred - the bounds an offset
from the centred place - 0x20 from the right edge, 0x30 full; V: 0x40,
0x80, 0xc0) apply where no sibling bit does.  (The NPG's `vjParentCenterH`
= 16, `vjParentRightH` = 32 agree with the code; `vjParentClip`,
`vjChildrenLasso` 0x8000 and `vjReflow` 0x10000 are named from memory of
the NTK's constants.)  The ROM keeps two private bits above the
justification: 0x40000000 marks the modal view (`SetModalView`
0x002e8b18).

**Drawing** (`ViewDraw.cpp`).  The port is the current one; a view draws
where the port's `visRgn` lets it.  `Draw(rgn, force)` 0x00265b90 (from
the assembly: the decompiler stops at its virtual calls): nothing for an
invisible view unless forced, or when the outer bounds miss the region
(or the clipper's visible region does); `SetupVisRgn` 0x00265a3c narrows
the port's visRgn for a child of the root view (each ancestor's clipper
region, the bounds of a `vClipping` view, less the front mask
0x00263b4c: the filled or windowed siblings in front); then `PreDraw`
0x00266370 (the fill as a round rectangle in its pattern, the lines
`viewLineSpacing` apart in `patOr` - both columns and rows for a
`'squareGrid` viewGrid), `RealDraw` (nothing for `TView`), the
`viewDrawScript` (its `evt.ex` errors dropped), the children (a
`vClipping` view cuts the visRgn to its bounds for them), `PostDraw`
0x002666c0 (the frame in its pattern and pen *outside* the bounds - the
outer bounds grow by pen + inset - round when the corners are; the
hilite/drag-shadow frames a gray frame with a black one inside; the drop
shadow; the default button's marks).  `SetPattern` 0x000e4aa0 maps a
format's pattern index (1 white ... 5 black, 14 the custom
`viewFillPattern`/`viewLinePattern`/`viewFramePattern` through
`GetPattern` 0x0019a378).  `Dirty(rect)` 0x00268ee8 cuts the outer bounds
to each `vClipping` ancestor and the window's clipper region and gives
it to the root view's update regions with the first filled ancestor as
the *filler*; `TRootView::Invalidate(rgn, filler)` 0x001b4524 keeps up to
three regions, merging under a common parent (the filler paints the
background: `TView::Update` 0x00266050 erases only when it has no fill);
`TRootView::Update` 0x001b4914 redraws them (the part outside the port's
visRgn stays pending).  The clipper of a child of the root view
(`TClipper` 0x00066bf8: the full region from the outer bounds, rounded
when the format is; the visible region less what is in front,
`RecalcVisible`) is made by `SetBounds` and recomputed by
`ViewVisibleChanged` 0x00263d48 (Show, Hide, Constructor, a reorder), so a
viewFormat changed on an open window does not grow its region - as in
the ROM.  `Show`/`Hide` 0x00263f48/0x0026404c block the strokes, set up
the view's effect (`TAnimate`: below), make the change, run the effect
with the `showSound`/`hideSound` and the `viewShowScript`/
`viewHideScript`; `ReorderView` 0x0025eda0 (`MoveBehind`, `BringToFront`)
invalidates what the siblings between the two places overlap of the
view.  `TRegionVar` (`qd/RegionVars.h`) registers an exception cleanup
like the ROM's, so a Throw through a drawing scope gives the region back.

**NewtonScript** (`ViewNatives.cpp`).  The globals are bound by ROM
symbol (`FAddView__FRC6RefVarN21`, ...: `AddView`, `AddStepView`,
`RemoveView`, `RemoveStepView`, `SetValue`, `GetDynamicValue` (the ROM's
name for `FGetValue`), `RelBounds`, `SetBounds`, `RefreshViews`, `GetView`,
`GetViewFlags`, `BuildContext`, `GetRoot`, and `Visible` as source); the
methods a view inherits are slots of the ROM's root template `Rviewroot`
(`Dirty`, `show`, `Hide`, `_Open`, `close`, `_Toggle`, `Parent`,
`ChildViewFrames`, `SyncView`, `SyncChildren`, `RedoChildren`,
`MoveBehind`, `GlobalBox`, `LocalBox`, `GlobalOuterBox`, `VisibleBox`,
`GetDrawBox`, `SetOrigin`, and the scripts `Open`, `Toggle`) - the host's
`MakeViewMethods` builds that frame and `InitViewSystem` makes it the
root template's `_proto`.  `Show`/`Hide`/`Open`/`Close` dispatch
`aeShow`/`aeHide`/`aeAddChild`/`aeDropChild` through the application to
the views (Commands and the application, below).  `GetView(context)` 0x0025f4c4 finds `viewCObject`
through the proto *and parent* chains, so a template whose `_parent` is
the root context resolves to the root view (`RealOpenX` then does
nothing): the ROM's own applications name a `preallocatedContext`, and a
context from `BuildContext` has its own nil `viewCObject`.

`SyncChildren` and `RedoChildren` (`FSyncChildrenX` 0x001eae8c,
`FRedoChildrenX` 0x001ead80) are the two ways a view is told its children
are out of date.  `SyncChildren` brings them up to date with the
template's `viewChildren`, adding and removing as needed
(`TView::AddViews(true)`).  `RedoChildren` does not: it throws every
child away (`RemoveAllViews`), builds the whole lot again
(`AddViews(false)`) and marks the view dirty, which is what a view whose
children come out of something that has *changed* needs - the Setup
assistant's list of countries, for one, whose `PickLetterScript` moves
the soup cursor and then calls `:RedoChildren()` to draw the page the
cursor now points at.  Both refuse while the view is still in its setup
(`vIsInSetupForm`): they complain (`BadWickedNaughtyNoot`) and put the
work off with `AddDelayedAction(view, 'RedoChildren, nil, nil)`, which
runs once the setup is done.  A Throw out of either clears the deletion
marks (`vIsBeingDeleted`, 0x90000000) before letting the exception go on,
so a part-built view is not left looking as though it were being deleted.
Both answer `true`.

**TMonthView** (`MonthView.h`, clMonthView 80): the calendar - a month
laid out as seven columns of days under a row of weekday letters.  It is
the grid in the Dates app, the one the Setup assistant asks today's date
on, and the one behind every date picker.

`RealDraw` works the month out afresh each time: the bounds give a label
rectangle (the top nine pixels) and a grid under it; the month comes from
`selectedDates` - the minutes of each selected day - and when the first
and last of them fall in different months the context's own `month` says
which of the two is on show, the other end being folded into it by a
month's worth of days.  The month and year are then written *back* onto
the context, which is what the pickers around the calendar read: the
Setup assistant's year picker does `yearPicker:SetYear(monthView.year)`
as soon as anything changes, so a calendar that never drew left it nil.
The cells are what is left divided seven ways across and five or six down,
depending on whether the month fits in five weeks.

`firstDayOfWeek` (the view's, the user's preference, or the locale's)
decides which column a day falls in, and every other piece of geometry
follows from it: `DateRect` answers a day's cell, `PointToDate` the day a
point is over (pinned to the grid, so a pen dragged off the calendar still
picks something), and `UpdateRangeRect` the one or two rounded rectangles
a range of days covers - `InvertSelection` turns them over.  `HandleClick`
follows the pen: in the grid it picks a day and, without `singleDay`,
drags a range out (kept inside one week, the pen wrapping round rather
than running on); on the labels it picks a weekday column down the whole
month.  What the pen settled on becomes `selectedDates` again
(`UpdateFrame`) and the `monthChangedScript` is run.

NOT YET: `DrawMonthOverView` (0x00122174), the Dates app's month
overview, which draws a bar across each day that has meetings in it - it
needs the meeting and repeat soups.  Until it is there an overview draws
its dates like any other month.

**TTextView** (`TextView.h`, clTextView 98: protoTitle, protoTextButton
and the like): its text slot in its viewFont - a single line
(viewJustify's `oneLineOnly` 0x800000) laid across the bounds' width by
the horizontal text bits, the baseline the ascent below the top (or
viewLineSpacing below it), centred, or at the bottom, one pixel lower
than the room leaves (`RealDraw` 0x0025090c); else wrapped into the
bounds by `TextBox`.  The transfer mode is viewTransferMode (srcOr when
none).

**TPictureView** (`PictureView.h`, clPictureView 76): its `icon` slot (a
bitmap frame, `GetValue` through the chains) drawn into the bounds by
the viewJustify bits - centred both ways for a template without one - in
the viewTransferMode (`DrawUsingRect` 0x0018bd00 through `DrawPicture`).

**TParagraphView** (`ParagraphView.h`, clParagraphView 81, on
`TDataView` 83 - `DataView.h`, only the class identity yet):
protoStaticText and every editable paragraph, display only.  The `text`
slot (`Text` 0x00183034 = `GetValue(text, string)`) is wrapped into the
bounds a line at a time (`FillAllCaches` 0x0016dc68: the ROM's
`LineLoop` breaks the lines and makes a text object per run of each; the
host measures each line with `DoTextOnce` over the style runs and cuts it
back at a word boundary as `DrawSimpleLine` does), each line the height
its fonts need or `GetInterLineSpacing` 0x0016b490 (viewLineSpacing when
a single style's font fits it: the font's height between 0.8 x the
spacing and the spacing + 3), a line whose midline falls below the bottom
dropped (`TestLineOverlap` 0x000a41fc) unless the view has
vCalculateBounds (the ROM then grows the view; the host keeps every line
and the bounds as they are), the lines moved down by the vertical text
bits when the text is shorter than the view; the lines are cached as
`LineInfo` records (the ROM's 0x24-byte ones: start and end offsets, the
first and last text object, whether the line ends in white space - a line
keeps the space that ends it - and its box), moved along when the view
moves (`OffsetCachedBounds` 0x0016b94c) and rebuilt when it is resized.
`RealDraw` 0x0016b14c draws the lines and an ellipsis (U+2026, the ROM's
Mac Roman 0xc9) after the last when the text goes on past it and the
view does not calculate its bounds.  The `styles` slot is the style runs
(`StyleRuns.h`: `[length, style, ...]`; `CorrectAnyBadStyleRuns`
0x0017c92c stretches or cuts them to the text's length through
`RunsInsert`/`RunsDelete` 0x0012aa28/0x0012a938; `GetStyles` 0x00183134
answers a single run's style itself, or `GetDefaultViewStyle` 0x0017a9ec
- viewFont from the protos, a read-only view's from the parents too, else
the userFont preference - when there are none).  `SetupDone` 0x00181608
reads viewTransferMode, viewLineSpacing, the text flags
(`GetInputViewTextFlags` 0x0025fdf4), the locale's break tables, and
builds the caches.  A final carriage return leaves an empty line behind
it - the line the caret goes to when the return is typed, and the line
that makes a view which sizes itself to its text grow by one.  NOT YET:
editing, hilites, the caret, ink, tabs (drawn as characters), the text
objects, the parents' bounds narrowing the lines.

**TGaugeView** (`GaugeView.h`, clGaugeView 92: protoGauge, protoSlider):
a bar filled black from the left in proportion to `viewValue` between
`minValue` and `maxValue` (0 and 100 without them; `Constructor`
0x0018ade0, `SetValue` 0x0018aed4 keeps the limits) - `RealDraw`
0x0018af84: the bounds made an odd height, the filled part inset two
from the top and bottom; an editable gauge (vReadOnly clear) keeps a
knob's width (the height) out of the range and draws a hollow diamond
(a region from four lines, painted, inset a pixel and erased) centred on
the filled part's end; `gaugeDrawLimits` paints the rest of the bar
light gray.  A click on an editable gauge (`RealDoCommand` 0x0018b2d0:
aeClick, vReadOnly clear) tracks the pen (`TrackSetValue` 0x0018b344):
the ink off, each turn the value under the stroke's last point - its
distance from the left, plus half a value's width in pixels, as a
fraction of the width in the range, clamped - set when it changed (the
`_sound` played, the root view updated) and a tick waited when not,
until the stroke is done; then `viewFinalChangeScript([old, new])` when
the value changed.  (Tested with the host tablet: `test_Views`'s
slider.)  NOT YET: the sound.

**Shapes** (`DrawShape.h`): the NewtonScript shapes and `DrawShape`.  A
shape is a `'rectangle`, `'oval` or `'line` binary (8 bytes: the rect,
a line's two ends - `MakeRect` 0x000ddb34, `MakeOval` 0x000decb0,
`MakeLine` 0x000dfb00 through `MakeRectShape` 0x000e260c), a
`'roundRectangle` or `'wedge` binary (12 bytes: the rect and the
diameter, or the angles), or a frame: `'polygon` (`MakePolygon`
0x000e4d30: `data` a Polygon of the `[x, y, ...]` points), `'region`
(`MakeRegion` 0x000e446c: the shape drawn into an open region),
`'text`/`'TextBox` (`MakeText`/`MakeTextBox` 0x000de268/0x000de334:
`bounds` a `'boundsRect` binary, `data` the string as `'textData` or
`'textBox`), `'bitmap`, `'picture`, `'ink`.  `ShapeBounds` 0x000e21cc
gives a shape's or a list's bounds (a line's ends put in order and at
least a pixel wide, a polygon's box a pixel wider), `OffsetShape`
0x000ded00 moves one in place, `ScaleShape` 0x000ddd84 stretches one in
place - as though the rectangle `src` had been pulled into `dst` and the
shape had come with it, a nil `src` meaning the shape's own bounds, so
that the shape is simply fitted into `dst` - and `IsPrimShape`
0x000deac8 tells a single shape from a list.

Each kind is scaled the way QuickDraw maps it: a region and a polygon
through their own data (`MapRgn`, `MapPoly`), a line through its two
points, and everything else through the rectangle it keeps - `bounds` for
a bitmap, picture, text or ink, and the binary itself otherwise. A list
is scaled member by member out of the list's own bounds, so the members
keep their places within it, and the style frames in it are left alone. A
mapped region is not the size it was, so its data is resized to the
region that came back.

`DrawShape` 0x000e0a68 (`view:DrawShape(shape, style)` - `FDrawShape`
0x000ddae4 is a slot of the ROM's root template, drawing from the
view's top left) draws a shape or an array of them with a style frame
through `TStyleSave` (0x0019a86c: the state a style sets - `SetStyle`
0x0019aab8 reads `fillPattern`, `penPattern` (0, vfNone, draws no
outline; the pen is on when the slot is nil or absent - the ROM's own
style frames say `penPattern: 0`), `penSize` (an integer or `[h, v]`),
`transferMode` (8 draws as srcOr), `font`, `justification` (`'center`,
`'right`), `textPattern`, `clipping` (a shape the port's clip is
narrowed to, undone when the list ends), `selection`; `transform`
scaling is NOT YET).  `DrawShapeList` 0x000e0d5c: a style frame in a
list applies to the shapes after it; a nested list draws in its own
level and the style before it is put back; `DrawOneShape` 0x000e0fa0
paints (fill) then frames (pen) a rectangle, oval, round rectangle or
wedge, draws a line, offsets and paints/frames a polygon's or region's
data, draws a bitmap (`DrawBitmap`, the mask first for patCopy), a text
shape as a line from its bounds' top plus the ascent aligned by the
justification, a TextBox wrapped and clipped into its bounds.  NOT YET:
`'picture` and `'ink` shapes, `MakeShape`/`MakePict`/`ScaleShape`/
`MungeShape`, the hit testing (`HitShape`, `FindShape`,
`PointInShape`), `GetShapeInfo`, `WedgeBox`.  The bounds binaries hold
the host's Rect (DEVIATION: the ROM's are big-endian shorts).

**Commands and the application** (`Commands.h`, `Application.h`).  A
command is a frame cloned from the ROM's `protoCommand` `{id, result,
parameter, receiver, frameParameter, params, undo}` (`MakeCommand`
0x00070dc4; the accessors 0x00070e88-0x0007130c): the receiver a view's
context or `'application`, the parameter an integer of the ROM's word
size (a recognition unit's pointer, a view id, a delta; 0x8000000 for
none), `params` an array by index.  `TApplication` (class 67,
0x00033b58-0x000345c0; `gApplication`, the ROM's is the `TNotebook`
subclass whose `Run` is the event loop) dispatches a command to its
receiver's `DoCommand` (`DispatchCommand` 0x00034128; no receiver:
`ErrorNotify` -8003, `root:Notify(3, -8003, nil)`) and answers the
result; keeps the undo stacks (`PostUndoCommand` 0x00034450: a command
marked `undo` on the stack, the first after an `Idle` starting a batch
- the stack so far kept as the previous batch without the `undoRedo`
user preference, dropped with it; `Undo` 0x00034178 dispatches the
stack newest first while a fresh stack collects the inverses the
commands post: without the preference the previous batch then becomes
the undo stack (Undo, Undo undoes two actions), with it the inverses do
(Undo, Undo redoes - `GetUndoState` answers `'undoRedo`); `ClearUndo`)
and the delayed actions (`AddDelayedAction` 0x00033ba0: [receiver,
message or function, args, due time] quadruples - the time a `'time`
binary of the global time the delay milliseconds on, nil for the next
idle; `RunNextDelayedAction` 0x00033d48 runs the due ones - a function
`DoBlock`, a symbol `DoMessage`, a function on a receiver `DoScript`;
the idle timer's re-arming NOT YET, the host's `RunDelayedActions()`
global runs them).  `TApplication::DoCommand` 0x00034744 answers
aeAppIdle, aeRunScript ([script, args, context] run on the context's
view; an `'undo` array from `MakeUndoCommand` sends the message or
calls the function) and aeUndo.

`TView::RealDoCommand` 0x00266e00 is the views' side (the ids as
`Commands.h` names them - from what each does, the NTK's names not
being in the ROM): the scripts (aeClick 0x0b: `viewClickScript(unit)`
on a vClickable view, `'skip` leaving the result 0; aeStroke 0x0c,
the gestures 0x0d/0x0f/0x10/0x2f/0x31/0x32 `viewGestureScript(unit,
kind)`, aeWord 0x12, aeRawInk 0x15 and aeInkWord 0x18 with the stroke
bundle, aeScrollUp/Down 0x2d/0x2e, aeOverview 0x33 - `vars.lastTextChanged`
cleared after the text ones), the key events 0x1f-0x23 (`HandleKeyEvent`
NOT YET), the structure (aeAddChild 0x29 adds the frame parameter's
view and dispatches aeShow to it, aeDropChild 0x2a hides and removes the
parameter's view, aeHide 0x2b, aeShow 0x2c - under a modal dialog
`ModalSafeShow`, NOT YET), the data (aeAddData 0x3d `AddToSoup`, posting
aeRemoveData 0x3f as its undo, which `RemoveFromSoup`s the child of the
id and posts aeAddData with its data; aeMoveData 0x40 `Move` by
params[0], [1], posting aeMoveChild 0x4c to the parent with the id and
the reverse delta; aeScaleData 0x42), the hilites (aeAddHilite 0x47
appends the frame parameter - or a frame's `hilite` slot - to `hilites`,
aeRemoveHilite 0x48, aeRemoveAllHilites 0x30) and the relays
(aeToChildren 0x49 to every child, aeToHilitedChildren 0x4b).  The
NewtonScript side: `PostCommand(receiver, id)`, `PostCommandParam`
(an integer or a frame parameter), `PostAndDo(cmd)`,
`AddDelayedAction/Call/Send`, `AddDeferredAction/Call/Send`,
`AddUndoAction` (a view method too)/`AddUndoCall`/`AddUndoSend`,
`ClearUndoStacks`, `GetUndoState`, and the host's `Undo()` and
`RunDelayedActions()` (the ROM's undo is the Undo button's aeUndo, its
delayed actions the event loop's idle - DEVIATION: globals for the
tests).  The command parameter is `Long` (pointer-sized) on the host,
so a view pointer fits it as on the ARM.

**Idlers** (`RootView.h`): `view:SetupIdle(ms)` (`FSetupIdleX`
0x001ee9e8) gives the root view an idler for the view (`AddIdler`
0x001b4f8c: an `IdlerRecord` {view, arg, due time} in the root's array
at +0x40, an existing one for the view and arg re-timed; the view gets
vHasIdlerHint, the application's next idle time is brought forward; 0
removes it - `RemoveIdler` 0x001b5124 answers the time it had left,
`RemoveAllIdlers` 0x001b5238 clears a view's when it goes,
`ForgetAboutView`).  `IdleViews` 0x001b4bf4 (the event loop's idle; the
host's `IdleViews()` global runs it) runs the idlers due within 10 ms:
the view's `Idle(arg)` (`viewIdleScript`, 0x00266c04) answers the next
delay in milliseconds - the idler re-timed from when it was due (from
now when that is past), 0 removing it; a view whose Idle is running is
on the `IdlingView` list linked through the stack (+0x4c), so an idler
removed (or a view deleted) during its own Idle is not touched after;
==> the earliest due time (zero for none).  The children and idler
arrays are re-made (`MoveLow` 0x001b4b60) when they shrank below their
high-water marks.

**TPickView** (`PickView.h`, clPickView 89-91: protoPicker, the popup
menus).  `SetupForm` 0x00187350 lays the `pickItems` out: each a row -
a string `pickTextItemHeight` high and its measured width (a text wider
than `pickMaxWidth` is cut with an ellipsis, `StyledStrTruncate`
0x001ecf64, the length kept negative in the item's flags), plus its
`icon`'s width (or the frame item's `indent`) and height; the symbols
`'pickSeparator`/`'pickSolidSeparator` 6 high; a bitmap or picture
frame its bounds plus `pickTopMargin`/`pickBottomMargin` (with a
`width`/`height` slot a grid of cells, `GetGridInfo` 0x00185690); an
item's `fixedHeight` for it and the rest.  The bottoms accumulate in a
handle at +0x38, the flags (the `mark` character, the pickable bit, the
length) in one at +0x3c, the grids at +0x40.  The width is
`pickLeftMargin` (+ `pickMarkWidth` when any item has a mark) + the
widest + `pickRightMargin` (+19 with the `scrollers` child shown, when
the list is taller than the application area).  The view is then placed
from the template's `bounds`: below them (above, with the viewEffect
0x182000, when it would run off the bottom from the lower half) at
their left, or - when the bounds carry an `info` view - beside that
view's window by `AdjustPopupInRect` 0x00186e84; shifted into the area,
written as viewBounds relative to the area's origin.  `RealDraw`
0x001885e0 draws the rows from the child origin (the text column, the
icon centred in its row, the mark in its column, the separators as a
gray or a two-pixel black line three down), the picked item inverted.
`Item`/`PickableItem` 0x001895b8/0x0018966c find the row (and grid
cell) under a point, an unpickable row sending the search up; `PickItem`
0x00189a7c runs `pickActionScript(index + topItem)` (a grid cell as a
protoGridItem `{index, x, y}`) on the `callbackContext` or the view,
hiding an autoclose picker first; `Hide` 0x00188fa4 of an unpicked
autoclose picker runs `pickCancelledScript`; `RealDoCommand` 0x001890f4
answers the pick command 0x36 (the PickStuff as a binary, or the
parameter as the index) and drops an autoclose picker from its parent.
`Scroll` 0x0018718c moves the child origin a view's height to an item's
top.  A click (`RealDoCommand`: aeClick, after `FClicker`) tracks the
pen over the items (`TrackStroke` 0x00189948: the ink off, the root view
updated, the pickable item under the stroke's first point inverted, then
each turn the one under the last point - un-inverting the old and
inverting the new when it changed, cell and all, a tick waited when
not - until the stroke is done) and dispatches the pick command with the
item the pen ended on (the PickStuff as a 'string binary frame
parameter), which `PickItem`s it: an item flashed (`FlashItem`: inverted
three times, five ticks apart), the autoclose picker hidden and
`pickActionScript(index)` run; no item (-1) runs `pickCancelledScript`
from the hide, and then the action script with nil.  NOT YET: the key
commands, ink items, the pickable test inside a masked grid picture, the
clicker.  The ROM's protoPicker has
viewFlags without vVisible: it is opened with `:Open()`.

### Hiliting a view (`TView::Hilite` 0x0026418c, `Select` 0x00264c34)

Buttons are hilited by inverting them.  `Select(on, unique)` keeps the
`vSelected` flag (0x02000000) in the view's flags and calls the virtual
`Hilite` (vtable +0x58) when the flag changes; `unique` first has the
parent's `SelectNone` 0x002643c4 un-hilite its first selected child
(the child's flag is left set - the ROM does the same).  `Hilite(on)`
does nothing for a view that is not `VisibleDeep`; otherwise, with the
port's visRgn narrowed to the view's (`SetupVisRgn`, put back after,
even on a throw), it runs the `viewHiliteScript` (slot cache 22) with
`[on]` - a non-nil answer means the script did the hiliting - and else
inverts the view's bounds let out by the format's inset, as a round
rectangle of twice the format's radius, less `(pen - 1) * 2`, when
there is a radius (`InvertRect`/`InvertRoundRect`).  The caret is
hidden while the view's outer bounds overlap its rectangle (NOT YET:
the caret).  The slot cache's bit for a script is cleared once a lookup
finds none, so a `viewHiliteScript` added to a context after its first
hilite is not seen.

`:TrackHilite(unit)` 0x001ecaa8 tracks the pen: while the stroke
(`StrokeFromRef`, its ink off) goes on, its final point's distance from
the view (`TView::Distance`) toggles the selection as it enters and
leaves, `Wait(1)` between turns; each turn inside runs the
`buttonPressedScript` (`DoMessageIfDefined`), whose non-nil answer ends
the tracking when the `newt_feature` proto variable is set.  Before
that the busy box is shown (`BusyBoxSend(0x35)`) when there is no
`buttonPressedScript`, and the `_sound` proto variable is played
(`FPlaySound`; `FClicker` when there is none); `BusyBoxSend(0x36)`
takes the busy box down at the end.  ==> whether the pen ended inside.
Without a stroke (a nil unit) the ROM's loop presses at the view's
centre for two turns, which is what the host does (strokes, the sounds
and the busy box are NOT YET).  `:TrackButton(unit)` 0x001ecd9c is
`TrackHilite` then `:buttonClickScript()` when it answered non-nil,
and `Select(false, false)` after, even when a script throws.
`:Hilite(on)` 0x001ece78 and `:HiliteUnique(on)` 0x001eceb4 are
`Select(on, false)` and `Select(on, true)`.  All four are methods of
`Rviewroot`.

### The keyboard (`Keyboard.h`)

Key events come from the keyboard tool (`TKeyboardTool::SendKeyEvent`
0x000fbc54) as `KeyboardEvent`s (0x2c bytes: a `TNewtEvent` 'newt/'idle
with the type 'keyb at +8, then +0xc the id - aeKeyUp 0x1f, aeKeyDown
0x20, aeKeyboardConnected 0x21, aeKeyString 0x22 (several key events at
once: +0x18 their count, +0x1c their bytes, a key code | 0x80 for a
down), aeKeyRepeat 0x23 -, +0x10 1, +0x14 the key code).  `HandleKeyEvent`
0x002e5e0c sends a key string to `HandleKeyEvents` 0x002e5b80 and the
rest to `DoKeyEvent` 0x002e5918 for the posting view (`GetPostingView`
0x002eb114: the root's visible popup unless it lets keys through
(`allowKeysThrough`), else `GetView(nil, 'viewFrontKey)` - or
`'viewFrontCommandKey` for the command key, a command key held or a
command key code).  `DoKeyEvent` runs the key through `KeyIn`, dispatches
aeKeyboardConnected to the root when no keyboard was known (the root's
`RealDoCommand` 0x001b56c8 sets `gKeyboardConnected` 0x0c101a24, clears
the hard key map when connected, syncs the popup and dirties itself),
then dispatches the event's command to the receiver with the parameter
`(modifiers << 25) | (key code << 16) | character`, and updates the root
view; the command key repeating opens the key help (`_keyHelpOpenScript`
up the key view chain; NOT YET: the help itself).

`KeyIn` 0x002e6a64 keeps the key maps (`gHardKeyMap`/`gSoftKeyMap`, a bit
per key code; `Modifiers` 0x002e69bc reads bits 0x37-0x3b of them:
command, shift, caps lock, option, control), folds the right shift and
option keys (0x3c, 0x3d) onto the left ones with `gTrueModifiers`
remembering which are really down, toggles caps lock on each press (its
release becomes key 0), tells the on-screen keyboards of a modifier
(`TRootView::HandleKeyIn` 0x001b6e04 dirties the first registered
keyboard showing them) and translates the key: `TranslateKey` 0x002e5610
reads the locale's `'kchr` binary (`vars.international.keyboard.mapping`,
`GetKeyTransMapping` 0x002ea13c; the Macintosh KCHR: +2 a table index per
modifier combination, +0x102 the table count, +0x104 the 128-byte
tables, then the dead keys - a count and records {table, key code,
completion count, (completion, result) pairs, (0, the accent)}), keeps a
pending dead key's record offset in the dead state (`KeyIn` answers 0
until it completes), converts the Mac Roman character to Unicode and maps
the function keys' 0x10 to U+F721-U+F72F.  `IsCommandKeyCode` 0x002eaf0c
(escape 0x35 and the function keys), `IsCommandKeystroke` 0x002eaf64
(command held, a function key or escape), `KeyIsPrintable` 0x002eb01c
(return and tab only in a paragraph), `KeyCanBeHandled` 0x002eb0e4.

`TView::HandleKeyEvent` 0x00267d00 answers the key commands: aeKeyString
runs `viewKeyStringScript(string)`; the others make the arguments `[char,
key]` - the key an int of the key code's unmodified character
(`TranslateKey` without modifiers; a function key's character and
escape kept) with the key code and modifiers above it - and run
`viewKeyRepeatScript` (a repeat; `viewKeyDownScript` when there is
none), `viewKeyDownScript` or `viewKeyUpScript`.  A key down nobody took
looks for a key command (`FindKeyCommand` 0x002e9f9c: the `_keyCommands`
arrays of frames `{char, modifiers, keyMessage}` up the key view chain -
the `_nextKeyView` proto variable, `'none` ending it, else the parent -
an exact match of the modifiers (`KeyCommandModifiers` 0x002e9de0: the
parameter's bits 25-29) winning at once, else the one asking for the
most of the modifiers held) unless the view takes its own keys
(`textFlags` 0x1000) and it is not a command keystroke, and sends its
`keyMessage` with `SendKeyMessage` 0x002ea238 (run by the first view up
the chain that has it, with the context as the argument) - a repeat only
when the command's `modifiers` has bit 2 (`gInRepeatedKeyCommand` set
while it runs); caps lock nobody took clicks and tells the
`_infoButtons` `:SetCapsLock(on)`.  `HandleKeyEvents` sends a key view
that wants its keys one by one (`textFlags` 0x400) each event as
`DoKeyEvent`, else gathers the keys down into a string for
`PostKeyString` 0x002eb1ec (one aeKeyString when every character is
printable for the view; a view wanting keys gets a key down and up per
character; else the printable runs as strings and each other character
as a down and up), switching to `DoKeyEvent` for good at a command
keystroke.  The natives `KeyIn`, `TranslateKey`, `IsKeyDown`,
`GetTrueModifiers`, `IsCommandKeystroke`, `PostKeyString`,
`HandleKeyEvents`, `SendKeyMessage`, `ClearHardKeymap`.  NOT YET: the key
help (`MatchKeyMessage`, `GatherKeyCommands`), the caret's key view and
its chain (`SetKeyView`, `NextKeyView`, `BuildKeyChildList`), the
on-screen keyboards' registry (`RegisterKeyboard`), the keyboard tool.
The root view's `+0x60` is the registered keyboards' `[context, flags]`
pairs (flags: 1 shows the modifiers, 2 hears `viewCaretChangedScript`, 4
active), `+0x64` a keyboard passed through a soft one
(`ConnectPassthruKeyboard` 0x001b6df4), `+0x7c` the selection stack.

### The key view and the caret (`RootView.h`, `Bits.h`)

The root view keeps the key view - the view typed into - at `+0x68`
with the caret's character offset (`+0x6c`) and the selection's length
(`+0x70`).  `SetKeyView(view, offset, length, flushWord)` 0x001b608c
pushes a usable old key view's selection (`GetSelection`, a caret info
frame) on the selection stack (`+0x7c`: `[context, info]` pairs;
`PushSelection` 0x001b69a8 cleans it of gone views and trims it to
twenty entries; `PopSelection` 0x001b6868, `FindRestorableKeyView`
0x001b66d4 and `RestoreKeyView` 0x001b678c bring one back - the ROM
leaves a restored entry in place), asks the new view to fix the offset
and length (`SetCaretOffset`; a paragraph's 0x00181008 clamps them to
its text) and goes on to `CommonSetKeyView` 0x001b6174; `SetKeyViewSelection`
0x001b5fbc does the same from a caret info frame through the view's
`SetSelection` (a paragraph's 0x001811a8 reads `offset` and `length`).
`CommonSetKeyView` stores the three, tells an old view
`ActivateSelection(false)` and the new one `ActivateSelection(true)`
(`TView`'s 0x0026876c runs the `viewCaretActivateScript`; not between
two paragraphs of the same hilite view), makes the hiliter the view for
a selection, sets the on-screen keyboards' shift for the view when no
keyboard is connected (`DoAutoShift` 0x001b5f24 after white space), and
tells the registered keyboards' `viewCaretChangedScript`.  When the key
view goes (`ForgetAboutView` → `CaretViewGone` 0x001b420c) the newest
stacked selection takes over.

The caret is the ROM's two caret bitmaps (`Rcaretbitsoutside`,
`Rcaretbitsinside`: a 12 x 11 triangle) drawn under the insertion point:
`GetCaretPoint` 0x001b72a0 asks the key view's `OffsetToCaret` (a
paragraph's 0x00173b04: the character's left edge, a pixel in, kept
inside the view; host: the line found and the text measured up to the
offset - `OffsetToBounds` 0x00179f50 - where the ROM asks its text
objects) and takes the rect's left and bottom (the baseline);
`CaretPointToRect` 0x001b7210 puts the 12 x 11 rect from 5 left of the
point and down from it.  `DrawCaret` 0x001b745c saves the screen under
the rect in a `TBits` (`+0x84`), narrows the port's visible region to
the key view's less what obscures it up to its clip view
(`GetCaretClipView` 0x001b73dc: the paragraph's window), draws the bits
(`DrawCaretBits` 0x001b7344) and remembers the caret as showing
(`+0x88`) at the point (`+0x8c`) for the view (`+0x90`);
`RestoreBitsUnderCaret` 0x001b7698 puts the saved bits back.
`CaretEnabled` 0x001b707c wants a key view without a selection and a way
to type (remote writing, a keyboard, an active on-screen keyboard);
`CaretValid` 0x001b70cc says whether what shows is right (always while
`HideCaret` 0x001b7adc's count (`+0x94`) holds - `ShowCaret` 0x001b7b0c
lets it go; `DirtyCaret` 0x001b7b6c invalidates its rect).
`TRootView::Update` 0x001b4914 takes an invalid caret (or one under a
dirty region) off the screen before painting and draws it again after;
`NeedsUpdate` 0x001b4870 counts an invalid caret and a changed default
button or caret slip (`FindDefaultButtonAndCaretSlip` 0x001b6bac: the
`_defaultButton` variable and the first hilite- or drag-shadow-framed
ancestor; `UpdateDefaultButtonAndCaretSlip` 0x001b6c60 dirties the one
that changed).  `PointToCaret` (a paragraph's 0x001736f8, over
`PointToOffset` 0x00179550: the nearest character on the line under the
point) places the caret for a tap.  `TBits` (`Bits.h`, 0x00042b7c-
0x00045bcc) is a `PixelMap` over a handle of the screen's depth for a
rectangle (`InitBitMap` 0x00042e84: 32-bit rows) with `CopyFromScreen`,
`Draw`, `CopyIntoBitmap` and `Fill`; the drag image and animation
sprites use it too (NOT YET: `TBitsPort`, `BeginDrawing`).

The natives: `SetKeyView(view, offsetOrInfo)`, `GetKeyView`,
`GetCaretBox` (the rect with `view` and `offset`), `GetCaretInfo`,
`RestoreKeyView`, `GetSelectionStack`, `ViewContainsCaretView`,
`RegisterOpenKeyboard`/`UnregisterOpenKeyboard` (the keyboards array:
`RegisterKeyboard` 0x001b6a3c), `KeyboardConnected`,
`CommandKeyboardConnected`, `SetRemoteWriting`/`GetRemoteWriting` (the
`remoteWriting` preference).  NOT YET: the caret's tap (`DoCaretClick`),
`SetCaretInfo`/`PositionCaret`, `HoldPendingKeyView`'s users, the hilites
a selection means, typing into the paragraph.

### The key view chain (`NextKeyView`, `BuildKeyChildList`)

`TView::NextKeyView(focus, direction, kind)` 0x002683a0 answers the view
that follows (direction 1) or precedes (-1) the focus in the tab order.
An explicit order comes first: from the view up to the one that holds a
`_tabChildren` array (a plain view with no `_tabParent` starts one level
up), whose entries are frame paths from that view's context to its key
views; the focus's path is found in it (`GetFramePath`) and the entry
`direction` further on (wrapping with a modulo) is the answer - a
`_tabChildren` that does not name the focus throws `kViewErrNoKeyView`
(-8500).  Failing that, the automatic order: from the view up to the
container (a `vApplication` view, a `protoContainerView`, or one with a
`_tabParent`), whose `BuildKeyChildList(list, direction, kind)`
0x00268290 gathers the key views front to back - each visible child asked
to add its own (recursing), then the child itself when it is not
read-only and, for the plain order (kind 0), its `textFlags` slot has bit
0x8000 or it is a `protoInputLine`, or, for the command-key order (kind
1), it is a paragraph that is not `protoStaticText`.  The view after the
focus is the next (wrapping to the first), the one before it the previous
(the last when the focus is the first).  Tab in a paragraph
(`RealDoCommand`, `ch == 9`, unless it calculates its bounds) moves the
caret to the next key view (backward with the shift modifier);
DEVIATION: the ROM selects the whole target when it is a paragraph
(`MakeHilite`, the data hilites NOT YET) - the reconstruction puts the
caret at its end.  `NextKeyView(view, direction, kind)` is the
NewtonScript native.  (Tested by `test_Views`'s `TestKeyChain`: tab
cycles a slip's fields, skipping a read-only one and a plain box.)

### Typing into a paragraph (`ParagraphView.h`, `StyleRuns.h`)

`TParagraphView::RealDoCommand` 0x0016e688 takes the key commands: a
key down or repeat first runs the key scripts and key commands
(`HandleKeyEvent`); a key nobody took, when the paragraph can be
written (not vReadOnly/vWriteProtected, and not a command keystroke),
goes into the text - return (or enter, 3) with a default button
(viewJustify 0x1800000) sends `_doDefaultButton` up the key chain; tab
moves to the next key view (NOT YET) unless the view calculates its
bounds; the left and right arrows (0x1c, 0x1d) move the caret
(`SetKeyView`; up and down, NOT YET: to the line's start/end); white
space flushes the word at the caret (`FlushWordAtCaret`, NOT YET: the
recogniser's dictionaries); backspace (8) removes the character before
the caret, another character `KeyCanBeHandled` goes in at it - both
through `AddKeyToCurrUndo` 0x00179248 first: when the last undo entry
undoes typing here (an aeReplaceText for this view's id, typed,
inserting nothing) that ends at the offset and covers fewer than ten
characters, the entry grows (shrinks for a backspace, goes when empty)
and the key is carried out by a replace command posting no undo -
which only happens for a vCalculateBounds paragraph, whose commands
carry its id (a plain paragraph's carry kNoParameter, so every key gets
its own entry).  A key string (aeKeyString) is inserted at the caret
(or over the hilite, NOT YET) and the hilites removed.

`InsertStyledText` 0x0017aa6c (offset, text, length, styles,
correctInfo, styleOffset, removeLength, typed) makes an aeReplaceText
(0x46) command through `MakeAndDoReplaceCommand` 0x0017ad8c - index
parameters `[offset, removeLength, length, styleOffset, postUndo (1),
caretAfter (1), typed]`, the frame parameter the styles (a runs array,
or a canonical correctInfo frame {styles, correctInfo}), the command's
`text` slot the string - and dispatches it; a deletion that inserts
nothing then drops the white space left at the end of the text.
`RemoveText` 0x0017abc8 widens a range to a neighbouring space.
`HandleReplaceText` 0x00170f30 carries the command out: the styles for
the insertion (none: one run of `GetStyleForInsertion` 0x0017a778 -
`vars.nextStyle`, else the style of the last character before the
offset, else for an empty vCalculateBounds paragraph `defaultFontSpec`
or the `userFont` preference, else the default view style; an ink word
before the offset gets the style over the whole range), the offset and
count kept within the text, the inverse command (the removed text with
its styles - `GetStylesOfRange` - and tabs, in a `SaveStylesAndTabStopsArrays`
frame) posted to the application's undo, the text munged (`Munger`
0x0012b5d0: a read-only string is cloned) into the data frame, the
style runs adjusted (`AdjustStyles` 0x0017af04: `RunsDelete`/`RunsInsert`
for an unstyled change, else the run before the range grown or shrunk
by the difference and the new runs laid over the insertion with
`SetStyleOfRange` 0x0017bb10, then `CompactStyleRuns` 0x0017cb94 - a
single run that is the view's font drops the styles slot, a
vCalculateBounds paragraph takes it as its viewFont), the hilites moved
(NOT YET), the tabs slot dropped when no tab is left, the caret moved
(to the insertion's end, or past the change when it lay after it) when
this is the key view, `RangeChanged` 0x00182c08 (the lines laid out
again - `FixupBBox` 0x001835e8, which also sizes a vCalculateBounds
paragraph to its text - and, once set up, `Changed('text)` after
`ProcessStyles` 0x00182d14 has looked for ink to recognise - NOT YET),
the view dirtied (its parent for an undo, and the old bounds when they
shrank).  `GetStyleAtOffset` 0x0017f8dc, `GetStylesOfRange` 0x0017fa94,
`CountStylesForLength` 0x0017fd68, `SetStyleOfRange`,
`CompactStyleRuns`, `ExtractStylesArray`/`ExtractTabStopsArray`/
`ExtractCorrectInfo` (0x0017ca88-) are `StyleRuns.h`'s;
`GetWriteableTextStylesArray` 0x0017b278 makes the styles slot a runs
array when it was a single spec.

### Things put into a paragraph (`HandleInsertItems` 0x001700a0)

Typing is not the only way text gets into a paragraph.  A word the
recogniser read, an ink word the pen left, a clipping dragged from
somewhere else, an ink word split off another - all of them arrive the
same way, as command **0x4d** with a frame saying what and where, and
the paragraph gathers the lot into one `aeReplaceText`, so that the
whole insert is one thing to undo.

`DoInsertItems` 0x00170f7c builds the spec for a named view: a clone of
`Rstarterinsertspec` with `insertItems` (one item or an array of them),
`addSpace`, `undoable`, `insertOffset`, `replaceChars`, `moveCaret` and
`defaultFontSpec`, sent on with the view's `DoCommand`.
`InsertItemsAtCaret` 0x00171168 sends the same spec to whatever view
the caret is in, and beeps (the root view's `SysBeep` script) when
nothing takes it.  Both go through the same little sender at
0x00170e90, which clones `protoCommand` and fills in `id`, `receiver`
and `frameParameter`.

`TParagraphView::HandleInsertItems` 0x001700a0 does the work.  It reads
the spec's slots (the caret's offset for a missing `insertOffset`, the
selection's length for a missing `replaceChars`), takes the selection
away unless `TRootView::GetPreserveHilites` says to keep it, and works
out the style anything that brings none is written in
(`GetStyleForInsertion` at the insertion point).  Then it appends each
item in turn into a text binary and a styles array it grows as it goes,
and hands them to the same `HandleReplaceText` that typing goes
through.  Afterwards it writes back into the spec what actually went
in, so the caller can see it: `insertOffset` where, `replaceChars` how
many characters.

The item kinds:

| item | what goes in |
| --- | --- |
| a string | the string |
| an ink word (`'inkWord`) | one 0xF701 character, the word as its style |
| a frame with `text` | its text, with its `styles` |
| a frame with `words` (the recogniser's word info) | the first reading's `word` - or, when the frame is flagged `kWordInfoIsInk`, its `ink` (or its `strokes`, packed with `CompressStrokes`) as one 0xF701 character |
| anything else | nothing |

A word-info frame also leaves a note in the **correction information**:
its `start` and `stop` are set to where it landed and its `flags` to
what it was (2 a word the corrector knows, +8 ink, +1 it carries
training data), and the frame is added to a `NewCorrectInfo`
0x0007623c - a clone of `protoCorrectInfo` with an empty `info` array -
which rides along in the replace command's frame parameter beside the
styles.  That is how the corrector can later offer alternatives for a
word that is already on the page.

#### The delimiter between two items

The appender at 0x0016fd7c is the piece that makes two things dropped
together read like two words rather than one.  Before each item it asks
`GetAppendDelimiter` 0x000edc24 what belongs between what came before
and what is about to go in, and the answer is a space or nothing:
nothing when either side is empty, when there is white space at the
join already, when the left ends in `-` or `(`, when the right starts
with `-`, when the right is a single punctuation mark that is not `(`,
or when the left ends in a punctuation mark standing on its own.  So
"one" and "two" come out `one two`, but "one" and "," come out `one,`.

What "what came before" means changes after the first item: the first
is measured against the paragraph's own text up to the insertion point,
and every one after it against the text built so far.  A delimiter is
also put *after* the last item when the paragraph goes on past the
insertion and the character there is not white space already.

> The ROM tests the character at `insertOffset + replaceChars` - the
> first character that survives the replacement - but measures the
> delimiter from `insertOffset`, so a replacement is measured against
> the characters it is about to take out.  Kept as it is.

Style runs are appended beside the text, merged with the run before
them when `EqualStyles` 0x0016fa08 says they match - two font frames
match when their `size`, `face` and `family` do, anything else only
when it is the same object.  An item that brings its own runs has them
copied in; one that brings a single style, or none, gets one run over
the whole of it, taking the style of the last run that is not an ink
word (0x0016fcc8) and failing that the default.  The two binaries grow
in steps - forty characters (0x0016f954), ten style slots (0x0016f9b0)
- so a long insert does not resize once per item.

`test_Views`'s `TestInsertItems` drops strings, an ink word, a
recogniser's word info of each kind and something the paragraph has no
use for, and undoes the lot.

A *rich* string item - a string with writing in it - comes apart first.
A rich string and a paragraph keep the same thing two different ways: a
rich string is one object, the characters with 0xf700 standing for each
word of writing and a region after them holding the writing itself; a
paragraph is two, a plain string with 0xf701 for each word and a styles
array whose run for that character *is* the `'inkWord` binary.
`TRichString::MakeParagraphTextSlot` (0x001abf6c) and
`MakeParagraphStylesSlot` (0x001ac038) make the second form out of the
first, which is what lets a note dropped from somewhere else keep its
writing.

### A word written on the page (`TParagraphView::HandleWord` 0x00172760)

Every paragraph on a page is asked, before a word is put down, how well
it would take that word - and the best answer gets it
(`TEditView::HandleWord`).  The same question with a single letter 'A'
and no unit is how `TextContainingPoint` finds what text a point is in,
which is what places the caret; the two share one implementation, so a
tap and a written word always agree about where they land.

The score a paragraph answers:

| score | why |
| --- | --- |
| 0 | not at all |
| 1 | the word's box overlaps the paragraph |
| 2 | it is on the line below the paragraph's text |
| 3 | the last word to go in went into this view |
| 4 | it is over the paragraph's last line |
| 5 | the paragraph covers half the word's box or more (`CoveredBy`) |
| 6 | it replaces a character of the text exactly (`ReplaceCharacter`) |

Below four is only an opinion; four and above are taken as certain, and
the paragraph goes on to place the word even when it was only asked.
`TEditView::HandleWord` stops asking as soon as one answers 6.

The room a word may fall in is the view's bounds with the margins added
(`AddMarginsToBounds`: ten pixels to the left, thirty to the right,
because writing runs on past the right edge far more often than it
starts before the left one) and one more line's worth of slack on the
right, guessed from how wide the word's own letters are - six times the
box's width divided by its letter count, or a flat hundred for an ink
word.

**Two words written one after the other belong together**, even when the
second falls outside the paragraph the first one made.  The globals
`gLastAddedWord*` carry the tie: the view a word last went into, the box
it was written in, the middle of its base line, and when its ink ended
and when it went in (`SaveAddedUnitBounds`, the vtable's +0x150).  When
the last word went into this view and this one was written beside it
(`AdjacentBoxes`, a thousand pixels of slack) or on the line under it
(`BoxAboveBox`), the word is placed *as though it had been written just
after the last one in the text* - five pixels past the end of the last
character, on that line.  More than a second between them, or a word
written above the paragraph or to the left of it, and the tie is
forgotten.

> A slip kept as it is: the adjusted base point's h is *added* to the
> old one rather than replacing it.  Nothing reads that h again - only
> its v, which `AdjacentBoxes` compares - so it never showed.

#### A letter written over a letter (`ReplaceCharacter` 0x00174e14)

The strongest claim a paragraph can make on a piece of writing, and the
only one that scores 6: the writer has written one letter over another
letter of a word, meaning to correct it.

`FindWordInRun` asks it first, before anything else it might do with the
writing.  It has to fall on the line horizontally, and then either -
with a unit - cover no more than three characters and not be over a run
of spaces (`WordOverSpaces`, 0x0017bc84: as many spaces as the writing
is wide, never fewer than three, counted forward and then backwards),
or - without one, which is the edit view's probe for what text a point
is in - be no more than two character gaps wide.  The character it lands
on is the one under the middle of its box, stepped back over any tabs
and returns and back one more when it is the character the line ends at.

Whether that character is *replaced* or the writing goes beside it is
then worked out from the two boxes: writing that covers the character
replaces it, writing clear of it on one side goes on that side, and
writing that overlaps it is decided by which half of the character its
middle is in.  A space is treated more carefully - writing over the last
space of a line, with another space before it, is not a correction at
all.

`DoReplaceSym` (0x0017b1b8) does the rest, and the thing worth knowing
about it is that **the word is replaced, not the letter**.  The word
around the hit is found (`FindWordBreaks` over the paragraph's own word
break table) together with the correction entry covering it - made on
the spot when there is none, and widened to take in the neighbours when
the "word" turns out to be nothing but white space.  The word's
characters are copied into a buffer; then every reading of the unit that
is a *single character* is tried in the place the writing landed, and
each one makes a whole candidate word which goes into a frame of
readings.  That frame is handed to `HandleInsertItems` with the word's
offsets, so what goes onto the page is the new word with a new set of
alternatives, and the correction information ends up describing it.

The unit's own score is only used as a floor when the writing was more
than a letter or two, or the character being replaced is a space, so
that a single deliberate letter always wins.  The last replacement is
remembered (`gLastReplacedIndex`, `gLastReplacedWord`): a writer
correcting the same letter of the same word again is understood to be
choosing between its readings, so the try string is kept rather than
cleared, and `TWordList::Reorder` moves the guesses they have been
picking to the front.

NOT YET: the branch for a view read a word at a time rather than a
letter at a time (`!UsesLetters`).  There the ROM asks the engine to
read the writing *again* as one character of a known height -
`ReclassifyCharacter` 0x000348e4 over `MakeCharArea` and
`TController::ClassifyInArea` - with the unit's readings saved and put
back around it.  An engine that reads nothing has nothing to say to it,
so the readings the unit already has are used either way.

`test_Views`'s `TestReplaceCharacter` writes an "o" over the middle
letter of "cat" and finds "cot" on the page with a correction entry
covering it.

#### Where in the text it goes (the `Finder`)

A `Finder` carries the word in - its box, its base point, its text and
the unit it came from - and comes back with the view, the offset, how
many characters the word replaces, and whether it starts a new line.

`FindWordInRun` (0x00173668) answers for a word written *over* the
text.  The line is the one nearest its box (`FindLineForWord` tries the
middle, the top and the bottom), and then: written out in the left
margin it goes at the line's start; past the right end of the line it
goes at the line's end; over the text it goes before or after the word
it was written over, whichever edge it was written nearer - unless it
was written over a run of spaces, in which case it takes their place.

`FindWordInParagraph` (0x0017348c) is the whole question: over the text,
or carrying on from the last word (beside the last one this view took,
or beside the end of its last line when it has taken none) in which case
it goes at the end of the text, or on a line of its own below the
paragraph (`SetFinderBelowParagraph`, 0x001735e4).

`AddWord` (0x00172eb4) acts on the Finder.  The word is copied into the
middle of a buffer with seventeen characters of room in front of it, and
what has to go before it is built up backwards into that room: the tabs
it was written at, the carriage return that starts its line, and the
space that keeps it off the word before (`GetAppendDelimiter`, the same
rule dropped items go through).  A space after it goes on the end.  The
whole lot is one `InsertStyledText`, and how many characters the run-up
came to is the `styleOffset` argument - which is what makes the word's
own styles land on the word rather than on the space in front of it.

Tabs are dead code in this ROM: `FindTab` (0x00173ea0) answers "no tab"
out of hand, so `AddTabStop` and the tab characters `AddWord` would put
in are never reached.  That is why writing in columns on a Newton gives
you spaces.  `PreviousLineNeedsCR` (0x00173268) is another that answers
no and nothing else.  `MinWidthToIntuitTab` (0x001733e4) survives
because `FindWordInParagraph` still uses its number to ask whether a gap
is small enough to be a space: four times the average width of one of
the word's letters (the narrow ones - i, l, I - counting half), never
less than 22 pixels.

When the writer has asked for remote writing and the caret is here, the
word is not placed at all: its word-info frame goes to
`InsertItemsAtCaret` instead, with `addSpace` off when
`IsMidWordLetterInsertion` (0x00172584) says a single letter is going
into the middle of a word at the caret.

`test_Views`'s `TestWordGeometry` checks the predicates and
`TestWordIntoParagraph` writes a word past the end of a paragraph's line
and finds it in that paragraph's text.

### Tapping a paragraph (`HandleTap` 0x001772f4, the double tap)

A tap on a paragraph (aeTap) is deferred by the double-tap interval so a
second tap can be a double tap instead: `RealDoCommand`'s aeTap case
stores the point and arms an idler (`AddIdler(this, gDoubleTapInterval *
16 + 80 ms, 2)`); when it fires, `Idle(2)` 0x00180994 runs `HandleTap`
0x001772f4 - the selection removed and the caret placed at the character
nearest the point (`PointToOffset`; before the first line the start, past
the text the end).  A double tap (aeDoubleTap) cancels the pending tap and
selects the word under it: the character found, the word scanned around it
(`ScanWordStart`/`ScanWordEnd` 0x001a37d0/0x001a36b4 - back and forward
over characters of the same kind, ink or not, that are not white space)
and `MakeHilite`d.  NOT YET: the ink-word double tap
(`HitsHilitedInkWord`), `OpenKeypadFor`, the tap sound (`FClicker`).
(Tested by `test_Views`: `TestParagraphTap` taps and double-taps
directly, and `TestClicks` taps a paragraph through the recognition and
lets the idler place the caret.)

### Selecting text (`TParagraphView::MakeHilite` 0x0016c4cc)

A range of a paragraph's text is selected by `MakeHilite(start, end,
caretOnEmpty)`: the offsets clamped to the text and unioned with the
existing selection (removed first, so a drag extends it); an empty range
with `caretOnEmpty` just moves the caret; else a hilite is added
(`aeAddHilite`: `TView::RealDoCommand` appends it to the `hilites` slot)
and the key view set to the range - the caret is off for a selection
(`CaretEnabled` fails when the caret length is not zero) and the
selection drawn.  DEVIATION: the ROM's hilite is a C++ `TParagraphHilite`
(0x1c bytes: a `THilite`, the start and end offsets, the selected text, a
region for its area) referenced from the slot through `AddressToRef`;
the reconstruction stores a `{start, end}` frame instead.
`DrawHilites` 0x0016cefc inverts each hilite's region over the text -
the ROM fills the regions into offscreen `TBits` and XORs them onto the
view in `PostDraw` 0x0016cc84, the host inverts the region directly (the
same on one bit); `SelectionRegion` builds the region as the union, over
the lines the selection touches, of the box from the first selected
character to the last (`OffsetToBounds`).  Tab into a paragraph selects
it whole (`RealDoCommand`, `ch == 9`); a content key typed over a
selection replaces it in one edit, backspace deletes it, an arrow
collapses it to an edge, and `GetSelection` answers its range.  `gDontDrawHilites` suppresses
the drawing during an effect.  (Tested by `test_Views`'s `TestSelection`:
a range inverted, extended, removed, and a tab selecting a field.)  NOT
When
the key view moves away, `CommonSetKeyView` deactivates the old view -
`TParagraphView::ActivateSelection(false)` 0x00181308 removes its (or its
hilite view's) hilites, so a selection clears when its field loses the
caret.  `RemoveHilite` 0x0025ff60 drops one hilite from the array and
invalidates (the ROM disposes the C++ hilite and invalidates its area;
the host dirties the view).  A selection survives the key view moving:
`SetKeyView` pushes the old view's `GetSelection` onto the selection stack
and `RestoreKeyView` restores it within a view, `SetSelection` re-hiliting
the range through `MakeHilite`.  NOT YET: `AdjustHilites` (moving a
selection past an edit), `ActivateSelection`'s soft-keyboard shift, the
container and edit views' hilites.

`DoPopup(pickItems, x, y, callbackContext)` (`FDoPopup` 0x001f2a3c) opens
a popup menu over the items at a point (or a bounds frame local to the
receiver view): a picker is built from the template, parented to the root,
placed and shown; picking an item runs the `callbackContext`'s
`pickActionScript` and closes the autoclose popup.  DEVIATION: the ROM
clones `canonicalPopup` (a scrolling-popup wrapper); the reconstruction
uses a plain `protoPicker` (the scrolling popup, which the ROM opens
with `FilterDialog`, is NOT YET).  (Tested by `test_Views`: `DoPopup`
opens a three-item menu and a pick runs the callback.)


### Restyling a range (`ChangeStylesOfRange` 0x00179464)

`ChangeStyleOfSelection` 0x00179a68 restyles the selected text: the first
hilite's range goes to `ChangeStylesOfRange`, which is also
`view:ChangeStylesOfRange(start, length, style, redraw)` - the verb the
Styles slip sends.

It is done as a **replacement of the range by itself**.  The styles of
the range are taken (cloned), each run's spec is merged with the one
asked for, and the text and the new styles go through the ordinary
`aeReplaceText` command - so the change lands in the undo stack with
every other edit, and the caret, the hilites and the correction
information follow it.  A paragraph that changed size has its parent
dirtied when it is inside a view that lays its children out
(`vCalculateBounds`).

`style` may be nil (the `userFont` preference), a packed font integer
(opened out with `IntFontToFontParms`), a frame with a `fontParms` slot,
or any other frame taken as a set of font parameters.  The `fontParms`
form is how the slip asks for a *change* rather than a setting: its
`command` slot is 1 to add the face bits, 2 to take them away and 3 to
toggle - and a toggle makes its mind up on the **first run** of the
range, so a selection that is only partly bold comes out bold all
through.

`GetRangeText` 0x00180248 is the other side of it: the characters of a
range (`ExtractTextRange` 0x001726a4) alone when the paragraph has no
style runs, and text and styles put together into a rich string when it
has - which is how a range that holds writing keeps it.

(Tested by `TestSelection`: bolding "World" splits the styles into a
bold run, the toggle goes back and forth, and an undo puts the old
styles back.)
### The key commands a script sees (`views/Keyboard.cpp`)

A view's `_keyCommands` is an array of command frames - a `char`, its
`modifiers` and the `keyMessage` to send - and four functions let a
script work with them.  All four walk the key-view chain the way
`FindKeyCommand` does: `_nextKeyView` when the view names one, the
parent otherwise, stopping at the root or at `'none`.

`GatherKeyCommands(view)` 0x0030fbac collects every command in force at
the view, from it outwards, skipping a key that is already spoken for
nearer the caret (`AlreadyInCommandArray` 0x0030fa70 compares the
character and the modifiers) - so what comes back is what would actually
happen rather than everything that exists.

`MatchKeyMessage(view, message, what)` 0x0030f7e0 goes the other way,
finding the commands that send a given message: `what` is 0 for the
first there is, 1 for the first that could be *shown* in a menu, and 2
for all of them.  What "could be shown" means is `GetDisplayCmdChar`
0x0030f700 - the command's `showChar`, else its `char` - passed through
`UserVisibleChar` 0x0030f6c8, which refuses the special key characters
0xf721 to 0xf72f (the arrows and the function keys), anything below a
space, and delete.

`view:AddKeyCommands(commands)` 0x0030b2a4 adds to the view's own array;
a view that has none takes the array given, and one that has takes a
copy first, because the ROM's own templates are read-only.
`view:BlockKeyCommand(message)` 0x0030b3ec stops a message the chain
above would have answered, by giving the view a command of its own for
the same key with *no* message - which the search finds first, and which
therefore does nothing.

`CategorizeKeyCommands` 0x0030fe38 sorts a gathered array into the
groups a keyboard help slip shows.  A command with no `category` is
given the "other" one, the array is sorted by category so the ones alike
fall together, and each run becomes a `canonicalKeyCommandCategory`
frame with its `keyCommands`; only commands with a `name` and a
character anyone could read are shown at all.  The groups are then
sorted by name and the "other" one moved to the end - once, which is
what the `moved` flag is for, since moving it to the end would otherwise
find it there again.

### The caret from a script (`views/ViewNatives.cpp`)

`SetCaretInfo(view, info)` 0x001eef6c puts the caret where a view says,
and what `info` holds depends on the view: a paragraph takes an
`offset` and a `length` (no length puts the caret there, a length
selects that many characters - and an edit view around it has its other
hilites cleared first), an edit view takes an `x` and a `y` in its own
contents which it turns into a caret of its own (`PositionCaret`), and
anything else is simply made the key view, with the frame kept in the
root's context as `_caretInfo` for whoever wants it.  A nil view takes
the caret away altogether.  `GetCaretInfo()` 0x001eee94 is the other
way: `{view, info}` for the key view, its `info` being whatever that
kind of view says its selection is.

`ShowCaret()` 0x001ef368 and `HideCaret()` 0x001ef38c are the counted
pair the root view keeps (`fCaretHidden`).

The four word scanners a script walks text with are the same ones the
paragraph's own editing uses: `ScanWordStart` 0x001a1250 and
`ScanWordEnd` 0x001a1134 for the word around an offset (ink and
characters counting as different kinds), `ScanNextWord` 0x001a1398 for
the start of the next one and `ScanPrevWordEnd` 0x001a1484 for the end
of the one before.  The natives answer nil rather than the limit when
there is no word that way.

### The picker's keys (`TPickView::HandleKeyDown` 0x0018a4b0)

The ROM's protoPicker's `viewKeyDownScript` is the native
`PickViewKeyDown(char, key)` 0x0018585c: the arrows move the pick -
left and right within a grid item's row (or to the first pickable
item's last/first cell when nothing is picked), up and down (and tab)
to the previous/next pickable item (`KeyToPrevItem` 0x0018a320,
`KeyToNextItem` 0x0018a17c: the separators skipped; with nothing picked
yet the first item that is shown); return or enter picks the picked
item (the pick command with the PickStuff, keys allowed through the
picker); a key command of the items (`keyCommands`,
`FindKeyCommandInArray`) picks that item outright; command-., command-w
and escape close the picker (aeDropChild to its parent); any other
character type-selects - the characters typed within the
`typeSelectTimeout` preference (up to 21) pick the first item, from the
picked one on, whose text begins with them.  The picked item is
scrolled into view and the picker redrawn.  The host's demo types "d"
into its paragraph and shows the picker last (a popup takes the keys).

`test_Views` runs with the ROM's objects imported (for the text views'
fonts; the canonical context, rect and slot cache frames come from the
ROM, or from `InitViewPrototypes` without it), over a 160 x 100 one-bit
map: the structure, every justification, the round trip through
`DejustifyBounds`, the formats pixel by pixel, overlapping windows and
their clippers, scripts, ties, the errors, a title and a button's text,
a picture view's icon, paragraphs (the wrapped lines and their offsets,
the ellipsis, vCalculateBounds, style runs, viewLineSpacing, justified
and moved paragraphs, the style runs' corrections), gauges (the bar, the
value through SetValue, the limits, the knob and the gray rest), shapes
(the objects, their bounds, every kind drawn from a viewDrawScript with
fills, pens, styles in lists, nested lists, text, clipping), commands
(the frames, show/hide/click through the application, the undo stacks
both ways, AddUndoAction/Call/Send, the delayed actions, aeAddChild and
aeDropChild), hiliting (a framed round button inverted inside its
frame, TrackHilite and TrackButton without a stroke, the click script,
a throwing script, the pressed script's answer with newt_feature,
HiliteUnique, the viewHiliteScript, a hidden view), the keyboard (the
U.S. mapping's tables, a dead key (option-e), the key maps and modifiers through
KeyIn, key events to a key view's scripts, a repeat, key commands with
the command key, one found up at the root, PostKeyString and
HandleKeyEvents, the natives), the caret (TBits, a paragraph made the
key view showing the caret under its text at the offset, hidden and
shown, moved, a tap's offset, the selection stack pushed and restored,
the natives), typing (keys into a paragraph, backspace, the undo
entries and Undo, a key string, styled runs around an insertion, the
arrows, a read-only paragraph, RemoveText, the style-run helpers),
idlers (SetupIdle,
the idle script re-timing and stopping its idler, removal with the
view), pickers from the ROM's protoPicker
(the rows, the placement below and above, the separator, marks, an
icon, a cut item, the item under a point, the keys - the pick moved,
type-select, a key event through the ROM's viewKeyDownScript -, a pick
closing the picker).

### Clicks and taps (`docs/recognition/README.md`)

A pen-down reaches a view as aeClick with the recogniser's unit as the
command parameter (`TView::RealDoCommand` runs `viewClickScript(unit)`;
the script's `true` claims the click, `'skip` passes it on), the
pen-up's tap, double tap or tap-and-drag as aeTap/aeDoubleTap/aeTapDrag
(`viewGestureScript(unit, kind)`) - unless the click was claimed.  The
path is the recognition system's: the tablet buffer, the stroke queue's
click-event watcher, the stroke world's click units, the unit handler's
`PostAndDoCommand` to the view under the pen (`TUnitPublic::FindView`:
the deepest view with the recogniser's viewFlags bits - vClickable for
clicks, vGesturesAllowed for the events).  `TrackHilite`/`TrackButton`
(`FTrackHiliteX` 0x001ecaa8) follow the stroke a tick at a time (`Wait(1)`)
until `StrokeDone`; `TRootView::DoCaretClick` 0x001b7774 takes a click on
the caret the same way (its _caretPopup opened via DoPopupMenu).  The unit functions of
NewtonScript (`GetPoint`, `GetPointsArray`, `StrokeDone`, `StrokeBounds`,
`InkOff`, ...) are `recognition/UnitNatives.cpp`.  On the host the pen is
`hal/host/HostTablet.h`, fed at once or a record a tick; `test_Views`'s
`TestClicks` taps, drags and double-taps a view.

### The view effects (`Animate.h`)

`TAnimate` (0xbc bytes: a `TBits` sprite, a `TSaveScreenBits` at +0x34,
the mask region +0x60, the effect's bounds +0x64, its start bounds
+0x6c, the saved area +0x74, the part of the sprite drawn from the view
+0x7c with its origin +0x84, the view +0x88, the kind +0x8c, the slide
offsets +0x90/+0x94, the cell limit +0x98, the effect word +0x9c, the
context +0xa0, the reverse and has-bits flags +0xa4/+0xa5, the enabled
kinds +0xa8 - all but the `noFX` preference's bits - and a cleanup
+0xac) animates a view: an effect is set up before the view changes and
run after.  `SetupPlainEffect(view, showing, effect)` 0x00043460 (Show,
Hide, `:Effect()`; the effect word the view's `viewEffect` when 0, none
means no effect) takes the view's outer bounds cut to the port, then
`PreSetup` 0x00043b24 and `PostSetup(bounds, from, to)` 0x00043c3c: the
screen under the rows of bounds outside from is what will be saved
(`TrimRect` 0x00043b60); the sprite's bits are allocated for to; the mask
is the front mask of the view and its ancestors, complemented within the
screen and then everywhere (so: what is in front, plus off-screen); a
show (from = bounds) draws the whole image from the view later, a hide
copies the screen into the sprite (the caret's bits put back first) and
draws the view into it where other views cover it.  `SetupSlideEffect
(view, bounds, distance, direction)` 0x00043600 (`SyncScroll`,
`:SlideEffect()`, `:RevealEffect()`: the contents slid, new ones coming
in from the far edge when direction > 0, the old ones going out for 0
and < 0, the effect word fxMoveV with fxVStartPhase when the distance
and direction disagree), `SetupTrashEffect` 0x000438e0 (`:Delete()`: the
saved area reaching from at most 72 above the view's bottom to the
screen's bottom right), `SetupPoofEffect(view, bounds)` 0x000439fc (a
scrub: a cloud of at least 89 x 54), `SetupDragEffect` 0x000459e8 (the
drag's sprite: a hide's setup, every kind enabled).

`DoEffect(sound)` 0x00043fac: a disabled kind invalidates the view's
bounds and updates; no sprite or no memory for the screen bits plays
the sound only; else, drawing, when there is an area to save the view's
bounds are validated when the image starts somewhere (not to be drawn
by the update), the root view updated (the screen without the view),
the caret's bits put back and the screen saved; the caret is dirtied
when it lies in the area; then `MultiEffect` 0x000441b4 (plain and
slide), `CrumpleEffect` 0x00045298 or `PoofEffect` 0x000458cc.
`MultiEffect` reads the effect word - bits 0-4 the columns less one,
5-9 the rows less one (NTK's fxColumnsMask/fxRowsMask), 10/11 the
horizontal/vertical start phase (the cells drawn from their right/bottom
edge), 12/13 the phase alternating along a row, 14/15 from row to row,
16/17 the cells moving (fxMoveH/fxMoveV: a slide rather than a wipe),
18 a line along the moving edge the frame's pen wide (fxRevealLine), 19
the effect the other way, 21-24 the steps (0: 3, else n + 1), 25-28 the
ticks a step takes (0: 3) - completes the image from the view, clips to
the view's region (the clipper's when not a plain rectangle, else the
bounds) less the mask, plays the sound, and each step puts back the
cells drawn last from the saved screen and draws every cell (a part
shrinking when hiding or growing when showing towards the slide offset,
no taller than the limit; the last step of a plain hide draws nothing -
the screen is right already), releasing the screen for the step's ticks
between steps (`SleepTillTicks` 0x002531e8); the clip and pen are put
back even on a throw, and the mask within the sprite is validated in
the root view.  `CrumpleSprite` 0x00044d58 crumples the image in six
passes, eight ticks each (eight vertical strips squeezed to the middle,
losing rows top and bottom; the sprite drawn clipped to a region with
jittered edges - `CrumpleRect` 0x00044b2c, `CrumplePt` 0x00044ad4 with
`Rand` 0x0025a67c over QuickDraw's `Random` 0x00313540 - and framed
two wide); `CrumpleEffect` then flies the ball (the `crumpleBitmaps`
cycled) along a path that rises 16, 9, 4, 1 and falls with gathering
speed into the `trashBitmap` at the application area's bottom right
(`vars.displayParams`), every other point a frame four ticks apart, the
clip cut so nothing draws over the trash, a plunk, half a second, the
screen put back.  `PoofEffect` draws the three `cloud` bitmaps (178 x
109, filled into the area at half size) two, two and one tick apart and
dirties the saved area.  The sounds go through `PlaySound` 0x000432bc
(a symbol looked up in the context; NOT YET: the sound system itself).

The NewtonScript face: `:Effect(effect, offScreen, sound, message,
args)` (`FEffectX` 0x001ee2cc), `:SlideEffect(distance, direction, ...)`
0x001ee3b0, `:RevealEffect(distance, bounds, ...)` 0x001ee4a4,
`:Delete(message, args)` 0x001ee22c, `DoScrubEffect(view, unit)`
0x001ee620 - and `:Drag(unit, bounds)` (`FDragX` 0x001ecef0, `TView::Drag`
0x00264cbc): the view dragged with the pen within the bounds (nil: the
application area) - the caret hidden, the view's image taken as a sprite
and the port's pixels saved, then until the stroke ends the view's outer
bounds follow the pen's travel once it has moved more than four pixels
(the view hidden the first time and the screen redrawn without it), the
saved pixels put back and the sprite drawn; when it moved, the view is
offset to the new place (its viewBounds slot written and Changed), the
pixels put back where views in front cover it, the port's area
validated.  `test_Views`'s `TestEffects` shows and hides a view with
effects, slides, reveals, trashes and poofs, and drags a view with the
host tablet (the waits are the tablet's hook: no real time passes).  The
`newton` program's demo slip has a checkerboard effect (the Slip button
hides and shows it) and is dragged by a press on it.

### Drag and drop (`DragDrop.h`, `TView::DragAndDrop` 0x0009d194)

A view drags its data onto another.  `TDragInfo` (`DragDrop.h`, from
0x000a0b78) is the payload: an array of item frames (the ROM's
canonicalDragItem), each with the drag `types` it offers, the `dragRef`
(the data or a key to it), a `label` and the source `view` - with
accessors and `CheckTypes` (do the items overlap a set of accepted
types).

`TView::DragAndDrop(stroke, bounds, pinBounds, clipBounds, copy, info,
limitBounds)` is the whole drag; a script reaches it through
`:DragAndDrop` and `:DragAndDropLtd` (`FDragAndDrop` 0x001f0b5c,
`FDragAndDropLtd` 0x001f0c28), whose `pinBounds`, `limitBounds` and
`clipBounds` are the three rectangles.  Data whose `copyProtection` has
bit 0 is not dragged at all.  `TView::Drag` 0x0009d6f4 follows the pen:

- the pen is kept to where `pinBounds` (the point it went down at, when
  there is none) stays inside `limitBounds` (the application area) -
  that is `dragPt`; the pen itself is `dropPt`;
- nothing moves until the pen has gone further than the items' smallest
  `minDragDistance` (four at most);
- the image is `DragBits` (`Bits.h`, 0x0004266c): the data drawn by the
  source's `DrawDragData` (`viewDrawDragDataScript`, else its selected
  data), and the screen under it as it would be without the data when
  the drag is a move (`DrawDragBackground`/`viewDrawDragBackgroundScript`,
  else the root drawn without the selection and the data exclusive-ored
  out).  Each move puts the saved screen back, takes the screen where
  the image goes, cuts a hole the image's shape through a one-bit mask
  (`InitBitMap` 0x000414e0) and draws the image into it - at most every
  three ticks.  With no memory for the bits a gray outline is dragged;
- each time the image moves over a target (`TargetDrop`: the deepest
  view with the drop flags that accepts the drag, walked up by
  `FindDropViewDeep`, then its `FindDropView` and
  `viewFindTargetScript`), the target is asked to show where the data
  would go (`DragFeedback`/`viewDragFeedbackScript`; a paragraph inverts
  a caret) and the offset is snapped to its grid (`AlignDragPtToGrid`);
- a pen let go within the minimum distance of where it started is no
  drag; one let go within five pixels of the application area's edge
  (`PointOnClipboard`, the button bar's edge not counting) is on the
  clipboard, its drop point taken to just outside that edge.

`DragAndDrop` then, if the source approves (`DropApprove`/
`viewDropApproveScript`): delivers the data to the target (`EndDrag`
0x0009cdb4 - each item moved on its own view by `DropMove`, told the
distance; otherwise its type matched to what the target takes, the data
fetched from the source (`GetDropData`), its `viewBounds` moved by the
distance and into the target's coordinates, the target told to `Drop`
it and, unless it was a copy, the source to `DropRemove` it; the
target's `DropDone` last); or makes a clipping of it on the clipboard's
edge (`TClipboard::NewClipboard`, the items taken from the source unless
a copy); or, for a clipping, moves it (`MoveIcon`).  It answers 0 for no
drag, 1 for a drag that went nowhere, 2 for a drop.

The page (`TEditView`) and the paragraph take part as well:

- **the page** offers the drag items of each selected child in reading
  order (`AddDragInfo`, `GetDragInfo` 0x000a8c78), gives a child's data
  moved into the page's coordinates (`GetDropData`), takes text,
  polygons, ink and pictures (`GetSupportedDropTypes`), adding each as a
  child through an undoable `aeAddData` with the stationery that shows
  it (`Drop` 0x000a8f34 - the new child selected and made the page's
  hiliter), moves a wholly selected child dragged about on its own page
  (`DropMove` 0x000a91ec, an undoable `aeMoveData`) and removes one
  dragged off (`DropRemove`); a paragraph under the pen that is not
  wholly selected takes the drop itself (`FindDropView` 0x000a8d7c);
- **the paragraph** offers its selected text as one `'text` item whose
  data is a paragraph frame of its own - the characters, styles, tabs,
  correction information, and `viewBounds` in the paragraph's own
  coordinates (`GetDropData` 0x0017f3f4 over `GetRangeProperties`) -
  takes text dropped on it where it was let go as a word written there
  would be (`Drop` 0x0017fc20, `HandleWord` and failing that below the
  last line; never on its own selection, `PointOverHilitedText`), and
  gives up the dragged text by deleting the selection (`DropRemove`,
  `DeleteHilited`, `ROMDeleteHilited` - a self-sizing paragraph left
  with nothing but white space removed from the page).

### Clicks on a selection (`TEditView::HiliteClick` 0x000aabb0)

The pen pressed on a page's selection (`aeClick`, or `aeTapDrag` for a
copy) goes to `TEditView::HiliteClick`: on the gray border of a
selection that may be resized (`GlobalHiliteBounds` bit 1) it resizes
it, anywhere else on the selection it drags the selected children with
`DragAndDrop`.  A paragraph pressed on its own selection drags the
selected text (`TParagraphView::HiliteClick` 0x0017ede4: a copy after a
tap, or when two presses come within 80 ticks), and a clipping's label
pressed on picks the clipping up (`IconClick` 0x0017efa8).

Resizing is `TEditView::TrackScale` 0x000a7b18: the side of the
selected children's bounds the pen went down nearer to follows the pen
(each way), no closer than 16 pixels to the other and not past the page,
snapped to a square grid; as the pen moves the page is drawn with the
selected children scaled into the new rectangle over the screen as it was
without them (`DrawScaledViews` 0x000a6384, each child's
`DrawScaledData` through `gEditViewTransform`, and the gray border
`DrawResizeBorder` 0x000a3780).  When the pen lifts, children only part
of which is selected are cut in two (`DiceHilited`: the selection made a
paragraph of its own by `AddHilited`), and each selected child is sent
an undoable `aeScaleData` from the old bounds to the new ones - two
words each, a rectangle in two parameters.  A paragraph resized so stops
fitting its width or height to its text (text flags 1 and 4).  A click
on the border that does not move joins the selected paragraphs into the
first (`CleanupData` 0x000aafcc, the paragraph's own 0x0017e83c making
its tabs and returns single spaces).

A press within eight pixels of a corner of a selected shape of straight
sides (a child answering `ClickOptions` bit 4) drags the corner instead
(`TrackDistort` 0x000a9634).  Up to four corners go together, where
selected shapes share one.  Each such shape is first diced - the whole
selection copied into a new view by `TPolygonView::AddHilited` and the
old view removed through `aeRemoveData` - and the corners are found again
on the copies; as the pen moves (onto a square grid, never off the page)
each corner follows it inside the hilite's own copy of the points, and
the page's hiliting is drawn into the drag bits over the screen as it was
without the selection.  When the pen lifts each corner goes to its shape
as command 0x43 (the point's index, and where it went as a page point),
which moves the view's point and the hilite's, turns a rectangle, square
or diamond into a plain closed polygon, and fits the view round its
points again (`TPolygonView::UpdateBounds`).  There is no undo of it.

The polygon selection itself (`TPolygonHilite`, 0x24 bytes) is its own
copy of the points it covers: from point `fFirst`, `fFirstPart` of the
way along the segment after it (16.16), to point `fLast`, `fLastPart`
along the one before it, with its own verb and pen.  `MakeHilite` moves
the two ends along their segments and gives a partial selection an open
line's verb (5, a curve's 7, an arc's 13); `HiliteAll` is the whole
shape, `MakeInkHilite` ink.  `ClickOptions` answers 1, +2 when the whole
shape is selected, +4 for straight sides (so the page's `fClickOptions`
mask of ~2 for a tapped selection leaves 5).  `Encloses` finds the pen
within sixteen pixels of a side (`LineHitRatio`, how far along the
segment's longer axis).  It is drawn in two passes: a thick black line
over what is selected (`DrawHiliteLine` along each side, an eight-pixel
arc over an oval, a round rectangle round ink), then a white dot on each
corner.  NOT YET: `HiliteTraced` 0x0018fa3c (part of a shape selected by
tracing along it, with about 7.5 KB of segment and snapping geometry
under it), so a selection is always a whole shape, and with it the
partial branch of `RemovePoints` and command 0x44 that undoes it.

`TView::LocalOrigin` is where a view is in the coordinates its
`viewBounds` slot is written in - the parent's contents origin taken off
its bounds.  (The host had been taking its own contents origin, which
comes to the scroll origin; a partly selected paragraph diced by
`AddHilited` was placed as if the paragraph were at the page's top left.)

(Tested by `test_Views`: `TestEditViewDrop`, `TestParagraphDrop`,
`TestSelectionClicks`, `TestDistort`; and
`src/host/demo/drag.ns` drags a word written on the Notepad, with
`PacePen(true)` feeding the pen a sample a tick.)

## The data hilites (`views/Hilites.h`)

What is selected *inside* a view, as against `TView::Hilite`, which
inverts a whole one because it is being pressed.  A hilite is a C++
object whose address is kept in the view's `hilites` array as a pointer
Ref (`AddressToRef`, the same "magic" the views themselves are held by),
so the array a script sees is opaque to it.

`THilite` 0x00260bdc is the base: a rectangle in the view's own
coordinates and the questions asked of it - `Area` (the rectangle as a
region), `Overlaps` (each rectangle given a pixel first, so that a hilite
of an empty line still overlaps it), `Encloses`, `Clone`/`CopyFrom`.
`TParagraphHilite` 0x00182e68 is the paragraph's: a range of characters,
whose region only the paragraph can work out - the characters are laid
out in lines - so `TParagraphView::SetupArea` 0x0016c774 computes it once
and keeps it in the hilite, `fBounds` being that region's bounding box.
The lines are laid out in the port's coordinates, so `TParagraphView::Area`
0x0016a92c moves the region into the view's own at the end - a hilite's
area and box are always the view's own, which is why `GlobalHiliteBounds`
and `TEditView::ScrubHilite` convert, and why `DrawHilites` is the one
place that moves the region back to draw it.
NOT YET: the copy of the selected text the ROM's carries for the undo of
a replacement.

`HiliteLoop` 0x00260f58 walks a view's hilites, handing out each one's
Ref and the object behind it.  It reads the array's length once, so a
caller that removes what it is handed steps the loop's index and count
back with it - which is what `TView::RemoveAllHilites` 0x0026002c does.

The base class only *keeps* hilites; a data view draws them
(`TParagraphView::DrawHilites` inverts each one's region).
`HiliteAll` 0x002600d0 makes one over the whole view and adds it through
the `aeAddHilite` command, so it can be undone like anything else;
`RemoveHilite` 0x0025ff60 takes one out, disposes of the object and
invalidates the parent over where it was; `GlobalHiliteBounds` 0x002603a0
unions their bounds into the parent's coordinates; `DeleteHilited`
0x002601cc asks the *parent* to remove what is selected here, the data
belonging to it rather than to the view holding the hilite.

`GlobalHiliteBounds` answers as well as fills in: the click options that go
with the selection.  Bit 1 says it can be resized, and
`TEditView::DrawHiliting` draws a resize border round it when it is set.
A leaf view answers its own `ClickOptions`; an editor ANDs its hilited
children's answers together, except bit 2, which it ORs.

### The pen over a view

`TView::HandleHilite` 0x00260218 is what makes a stroke select a view.  The
unit's box, grown by eight pixels, has to cover more than 60 per cent of
the view (`TRect::CoveredBy` 0x00199d24, `qd/Rects.h`: the percentage of
the first rectangle their intersection covers, each given a pixel where it
is degenerate).  A stroke that is long and thin counts by its long axis
alone - a line drawn across a one-line view covers little of it - so when
the stroke is at least twice as wide as it is tall, and lies within the
view horizontally, the horizontal extent is taken out of both rectangles
and the test made on the vertical one; and the other way round for a tall
stroke.  Only gestures 1 and -1 are looked at, and a false `doIt` asks
whether the stroke would count without acting on it; acting is
`HiliteAll`.

`TView::HandleScrub` 0x002605f0 answers the gesture the view takes (5) when
a scrub covers more than 75 per cent of it; a read-only or write-protected
view takes none.  The base only answers - the caller is what acts.

`TView::PointInHilite` 0x0026051c asks each hilite whether it encloses the
point, moved into the view's own coordinates.

## Container views (`views/ContainerView.h`)

`TContainerView` 0x00073b84 (class 78, over `TDataView`) is the view that
holds others and lets them be selected - the frame a page of a notebook
application is laid out in, and the base of `TEditView`.  What it adds is
a selection made of whole children rather than a range of anything.

A `TContainerHilite` either says "all of me" (`fComplete`, and then
`DrawHilites` fills the container's bounds with the black pattern) or
stands for the children that are hilited themselves, and the container
hands `DrawHilites`, `DrawHilitedData`, `GlobalHiliteBounds` and
`RemoveHilite` on to them - a child being removed takes its own selection
with it.  `GetHiliteView` 0x00074618 answers which view a selection really
belongs to: the container when it is complete, else the first hilited
child.  `MakeHilite(child, view)` 0x00075270 makes either kind, taking the
child's own hilite bounds for the second, and adds it through
`aeAddHilite` so that it can be undone; `HiliteAll` is
`RemoveAllHilites` and then `MakeHilite(0, nil)`.

Bounds are gathered into a rectangle whose top and bottom start at -32768:
that makes it empty, so the first `Union` replaces it rather than
stretching from the top of the world, and `GlobalHiliteResizeBounds` reads
the top as the same marker.  The ROM leaves left and right uninitialised,
which the host cannot, so the reconstruction sets all four.

`GlobalHiliteBounds` answers the same click options as `TView`'s.

**A written word.**  The recogniser's `aeWord` reaches the editor's
`RealDoCommand` 0x000a4360 (the case at 0x000a5614): a page whose script
takes words (text flag 0x2000) is offered it first, the hilites are
cleared for it (`ResetHilitesForNewWord`, or `RemoveAllHilites` with the
corrector up), `RemoveInk` 0x0019dfa4 removes any ink view left on the
page for its strokes (an undoable `aeRemoveData` for each stroke whose
context id names a child), and `HandleWordUnit` 0x000ab9f8 hands the
unit's best reading to `HandleWord` 0x000abaa4 in the box it was written
in.  `HandleWord` asks every data child how well it would take the word
and then, with **remote writing** off, gives it to the best of them or
makes it a paragraph of its own (`AddNewParagraph`).  Remote writing is
the default, and it sends the word to the caret instead (0x000abe58):
into the caret's paragraph when the caret is in one of the page's own
(`InsertItemsAtCaret`, a space in front unless `IsMidWordLetterInsertion`
says it is a letter written into the middle of a word); onto the end of
the text under it, on a line of its own, when the caret is on the page
itself just below a paragraph (`TextContainingPoint` answering 2); and
otherwise a paragraph where it was written.  So a second word written
beside the first joins it: `src/host/demo/write.ns` leaves "ton to".

`HandleShape` puts a recognised shape on the page as a `TPolygonView`
(class 82, `views/PolygonView.h`: the shape's points as a `PolygonShape`,
drawn as a polyline, an oval or arc in its box, or ink).

NOT YET: `PlaybackInk`, `SetSelection`/`GetSelection`,
`GetValue`/`SetValue`.  (Drag and drop, `TrackScale`, `TrackDistort` and
the resize border are described above.)

## How the machine's own views get on screen

The root's fifty-nine children are the system's views - the keyboards,
the slips, the alerts, the applications - and *none* of them is visible:
each is a `preallocatedContext` that waits to be opened.  The asking
starts with the root's own scripts:

- `viewRoot.viewSetupChildrenScript` (ROM object 0x0041a54d), which
  `TView::AddViews` runs before it builds any child, does nothing but
  `AddDeferredSend(self, '_OpenLater, nil)`.
- `viewRoot._OpenLater` (0x0041a481) then does three things: it sends
  `InitButtonBar` to `self.buttons` - the button bar along the bottom,
  which is what puts the *first* child on the root - it reads
  `userConfiguration.blessedapp` (defaulting it to `'paperroll`, the
  Notepad, when unset) and it calls `self:_BlessedOpen` on
  `self.<that symbol>` - the application's own preallocated context -
  under a handler that notifies "the new Backdrop failed to open" and
  puts the old one back.
- `_BlessedOpen` (0x0041a4f5) takes the floating bit off the
  application's viewFlags (`viewFlags := viewFlags band -65`, written
  onto the context), gives it Close/Hide/Toggle slots of its own, locks
  the screen, opens it, and moves it behind
  `GetRoot():ChildViewFrames()[0]` - the button bar.
- `buttons.InitButtonBar` (0x00550ec9) is `if
  displayParams.buttonBarPosition <> 'none then self:Open()`.

### Open does not go through AddView

`Open` is `viewRoot.Open` -> `_Open` -> `FOpenX` 0x001f173c ->
`RealOpenX` 0x001f1638, which, when the context has no `viewCObject`
yet, dispatches `aeAddChild` to the view of the context's `_parent`.
`TView::RealDoCommand` 0x00268d38 answers that with `AddChild`
0x00265e4c - and `AddChild` is four instructions long:

```
00265e60  ldr r0,[r0,#0x20]     ; fChildren
00265e64  bl  Exists(TViewList*, RefArg)
00265e68  teq r0,#0x0
00265e78  beq BuildView(TView*, RefArg)     ; tail call
```

It calls **`BuildView`**, not `AddView`.  That matters, because
`TView::AddView(RefArg)` 0x0025f1ac is the one place with a `vVisible`
test: a template with a `preallocatedContext` is refused unless that
context's `viewFlags` has bit 0, and a template without one goes to
`BuildContext(templ, false)` 0x0025e56c, which returns nil on the same
test.  `AddView` is what `TView::AddViews` 0x00262c0c calls for each of
`viewChildren`/`stepChildren`, so an invisible child in a parent's
viewChildren is quietly skipped - that is what the test is for.  The
open path skips it entirely, which is how a view that was preallocated
*invisible* can be opened at all.

The data only makes sense that way round.  The button bar's template
(magic pointer 692) has `viewFlags` 2560 while its own `_proto` (@511)
has 2561: the template deliberately takes `vVisible` off.  Its
`_cacheContext` (ROM object 0x006306a1), which `GetCacheContext`
0x0025e4d0 clones to make the context, carries `viewFlags` 2560 as a
slot of its own.  Of the 423 frames in the ROM that have a `viewFlags`
slot, 305 *do* have `vVisible`; the sixty in the root's
`allocateContext` are exactly the ones that do not.

The one visible difference `BuildView` makes is whose frame becomes the
view's context: `AddView` would have built a fresh context protoed from
the one it was given, whereas `AddChild` builds the view on that frame
itself, so `Open`ing a context sets that context's own `viewCObject`.
## Drawing from a script (`views/ViewNatives.cpp`)

Three of the root template's methods let a script put pixels on the
screen outside a `viewDrawScript`:

- `:CopyBits(picture, x, y, mode)` 0x0003e85c draws a bitmap or picture
  frame with its top left at that point of the view.  The box it hands
  `DrawPicture` is the point alone, so the picture is drawn at its own
  size.
- `:DrawXBitmap(bounds, picture, index, mode)` 0x0003ead4 draws one image
  out of a strip of them: the picture holds several side by side, all the
  width of the bounds, and `index` says which, counting from 0.  The
  source rectangle is the bounds moved to that cell (`index * (right -
  left)` across, plus the picture's own origin), and the whole thing is
  `CopyBits` between the picture's pixel map and the port's.  It is what
  draws the lettered index tabs down the side of the Setup assistant's
  country list.
- `:DoDrawing(message, args)` 0x001edcbc sends the view one of its own
  messages with the port set to the view's visible region.  The caret
  comes down first when its rectangle overlaps the view's
  (`TRect::Overlaps` 0x001991fc, which gives a rectangle with no width or
  height a pixel of it first so that a caret, which is a line, still
  overlaps what it stands on), and the port's clipping and the caret are
  put back however the message ends.  A view that is not visible all the
  way up is not drawn in at all and the message is not sent.

A point in a view's own coordinates becomes one on the screen through
`ToGlobalCoordinates` 0x000e3490, which adds the view's left edge to the
x's and its top to the y's.

## Laying children out (`views/ViewNatives.cpp`)

Two of the root template's methods build the templates for a view's
children rather than drawing anything, and both stop when the view is
full rather than making children that would be clipped.

`:LayoutColumn(entries, index)` 0x001eb2f4 answers as many of the entries
from `index` on as fit down the view, asking each one its own `height` -
or the view's `collapsedHeight` when the view says `allCollapsed` or the
entry says `collapsed`.

`:LayoutTable(spec, column, row)` 0x001eb4b0 answers the cells of a table
as view templates, and is what the Dates application's month and week
views are drawn from. The spec says how big the table is (`tabAcross`
columns, `tabDown` rows) and what a cell is: `tabProtos` the prototype,
`tabValues` what goes in the slot `tabValueSlot` names, `tabWidths` and
`tabHeights` the sizes, `indentx` and `indenty` where the first cell
starts. Each of those four may be an array, walked round and round for
as long as the cells last - seven widths for the days of a week, one
height for each row of it - or a single value every cell takes. The
walk does not start at the beginning: the proto and value arrays are
entered at `across * row + column`, so a table laid out from the middle
picks up the prototype and the value the cell there should have.

`tabSetup`, when the spec has one, is sent to the spec for every cell as
`:tabSetup(cell, column, row)` - with the column and row counted from
one, since both are the loop counter after it has been stepped on.

The rows overlap by a pixel: a row's top is the last one's bottom less
one, so a grid of framed cells draws each line once rather than twice.
The columns do not overlap - a cell's left is the last one's right.
Laying out stops at the first cell that would cross the view's right
edge and at the first row that would cross the bottom one.

## The Dates views' own drawing (`views/ViewNatives.cpp`)

`:DrawMeetingGrid(step)` 0x001efdf0 draws the rules a day's meetings are
laid between: a line across the view every `step` pixels - one to each
half hour - with the whole hours named down the eighteen-pixel gutter on
the left, right-aligned in it, and "am" or "pm" under the first hour
drawn and under noon. Which half hour the first line is comes from the
view's child origin divided by the step, since the view is taller than
the day it shows and is scrolled through it. The hour rules are drawn
light grey and the half hours grey, which is the way round the ROM has
it. Afterwards it rules down the left edge of every child but the
first, which is what separates the days of a week.

`:DrawDateLabels(bounds, dates)` 0x001f030c writes the dates under the
columns, one to each, centred in `(right - left) / count` pixels and ten
below the bounds. Dates a day apart (the week view) get the day and the
date; anything else takes the view's own `dayStrSpec`, and printing
takes the long form out of the ROM's date-and-time specs.

Both are global natives whose receiver is the view they are called from,
so a script in the view calls `DrawMeetingGrid(20)` without a receiver.

`:DragAndDropLtd(unit, bounds, limits, copy, dragItems)` 0x001f0c28 is
the drag the plain `:DragAndDrop` does with the three rectangles it does
not take: `limits.pinBounds` is where the dragged view itself may go
(`'none`: anywhere; the slot missing: no further than the bounds given),
`limits.limitBounds` the rectangle the drag is kept inside (`'none`: the
whole screen), and `limits.clipBounds` the slop. A `limits` of `'none`,
or a plain rectangle rather than a frame of those three, is a
limitBounds.

## What a note says (`views/ViewNatives.cpp`)

`ExtractData(data, separator, maxLength)` 0x001ed314 is the line the
Notes overview shows for a note - the Notes stationery's `StringExtract`
calls it from `Abstract`, which is why every boot with a store full of
notes asks for it. `data` is the note's children, and the answer is
their words run together with the separator between them, stopping at
`maxLength` characters.

The children are read in the order they sit down the page (sorted by
`viewBounds.top` through `SortArray` with a `pathExpr` key), but only
when there are fewer than forty of them: past that the sort is skipped
and they come out in the order the note stored them, a long note not
being worth sorting for one line of summary. The words come first, in
one pass, and the drawings in a second, so a note that begins with a
sketch still reads as its text first; a child that is the same object as
the one before it is passed over.

A paragraph contributes `ExtractRichStringFromParaSlots` 0x0017d6c0 of
its text - the characters asked for, kept rich when a style covering
them is ink (`MakeRichString` is NOT YET, so the ink is dropped and its
characters stay). A drawing contributes `PolygonDescription` 0x00191800,
which is the ROM's `" -sketch- "` when it carries ink and `" -shape- "`
when it does not. Anything else with a stationery of its own
contributes the word `"data"`. Carriage returns and tabs in the answer
become spaces, so that the one line stays one line.

## A time down a view

The Dates application's day view is a strip of one day: the top of it is
midnight and the bottom is midnight again, so a y in the view is a time
and a time is a y. `PositionToTime(view, y)` 0x001ecc1c and
`TimeToPosition(view, minutes)` 0x001ecc88 convert between them over the
view's own height, the whole day being 1440 minutes; the arithmetic
itself (0x001ca128, 0x001ca16c) the ROM keeps with the meeting views it
is for.

Both snap the time to the nearest quarter of an hour with the same
expression, `t - ((t + 8) mod 15 - 8)`, which tips at *seven* minutes
past the quarter rather than seven and a half: 9:07 lands on 9:15 and
9:06 on 9:00.

`GetHiliteOffsets()` 0x001f0f10 answers where the current selection is -
the `offset` value of the `hilites` of whichever view owns them
(`TRootView::fHiliter`) - and nil when nothing is selected anywhere.

## The on-screen keyboard (`views/KeyboardView.h`)

A keyboard is a `keyDefinitions` array of rows, each row `[pitch, height,
legend, result, info, legend, result, info, ...]` (`Rnumerickeys`,
`Ralphakeys`).  The `info` word is packed - the key's width and height in
eighths of the keyboard's unit cell in bits 8-15 and 0-7, a 3-D depth in
23-24, an inset in 25-27, whether the key hilites when pressed in bit 28,
and bit 29 saying the entry is a gap rather than a key.  A legend is a
character, a string, a number, a bitmap frame, an array indexed by
`keyArrayIndex` (which is how one keyboard has a shifted and an unshifted
face), or a function of the view answering any of those.

`TRawKeyIterator` (0x000fac10) walks the definition; `TVisKeyIterator`
(0x000fd0ac) walks it as it is laid out, keeping a pen at the end of the
last key and answering three rectangles for each - its cell, its face and
its shadow.  `TKeyboardView` (0x000fb220-0x000fcfec) draws them, and the
keyboard's *cell* is worked out from the bounds it is given rather than
the other way about: as wide as the view over the widest row, as tall as
the view over all the rows' pitches, rounded down through a table of
`qdConstants` so that no key's eighths land between pixels.  A keyboard
therefore fills whatever it is put in.

`TrackStroke` (0x000fc958) follows the pen: the key under it is drawn
pressed and redrawn as the pen slides from one to the next, so a finger
can be run along and land on the right one.  It takes the view's visible
region over for the duration so that it can draw without the view system,
and puts it back afterwards.  A key held still for three fifths of a
second repeats every fifth of a second, unless the keyboard says
`_noRepeat`.

`HandleKeyPress` (0x000fc300) is where the shift key turns out to be
*sticky*: a key code goes into the key map as a press and a release, and
every modifier that was held goes out with it, so shift is on for exactly
one key and then off again.  Tapping shift itself toggles it, and the
keyboard redraws so the legends change.  `InsideView` (0x000fc760) is
worth noting too - a keyboard is only where its keys are, so a tap in the
gaps between them falls through to whatever is underneath.

NOT YET: the busy box the ROM holds off while the pen is on the keyboard,
and the `keySound` it plays, both of which live outside the view system
(as they do for `TGaugeView`'s tracking).

## The hilite stroke (`views/View.h`, `views/RootView.h`)

Selecting something on a Newton has no modifier key and no menu. The pen
is held still on it for three quarters of a second — which is what
`recognition/StrokeQueue.cpp` calls a *hilite click*, more than 45
samples within `gHiliteDistance` of where the pen went down — and then
drawn through what is to be selected, or round it. The line the pen
draws is the only feedback, and what is under it when the pen comes up is
what ends up selected.

The click reaches the view under the pen as an `aeHiliteClick` command.
No view answers it, so it walks up the parent chain (`TView::DoCommand`)
to the root, whose `RealDoCommand` calls `TRootView::Hiliter`
(0x001b2e64). That is where the pen is followed:

- the ink the stroke was leaving is taken off (`InkOff`), whatever was
  selected before is dropped (`aeRemoveAllHilites` to the view), and
  `hiliteSound` is played;
- the screen is copied into a `TSaveScreenBits` and an off-screen
  `TBits` the size of the port is made. Each new segment of the stroke is
  drawn into the `TBits`, the saved screen is put back over the segment's
  rectangle and the `TBits` is exclusive-ored on top — so the line grows
  without anything underneath being redrawn. A machine too short of
  memory for either buffer draws straight on to the screen instead and
  leaves the line there until the invalidation at the end repaints it;
- the union of every segment's rectangle is what
  `TRootView::SmartInvalidate` is given at the end.

`DrawHiliteLine` (0x000a37ec) draws one segment, stepping a pixel at a
time along whichever axis it moves further in. Only the first four and
the last four steps are filled ovals; everything between them is a
*single straight line* from the fourth oval to the fifth-from-last. A
long stroke therefore costs two ovals' worth of filling and one `LineTo`
rather than hundreds of `FillOval`s, and looks the same, because the ends
are the only places the roundness shows. The first segment of a stroke
is drawn with a pen four pixels fatter, which is what puts the blob where
the pen was held still.

When the pen comes up the unit is dispatched to the view under it again,
this time as `aeGesture2f`, and *that* is what turns the stroke into a
selection.

### What the stroke selects

`TEditView`'s `aeGesture2f` arm calls `TEditView::AddHiliter`
(0x000a6fb0); `TView::AddHiliter` (0x00262708) is the same thing for a
plain view, and is what the `HiliteViewChildren` method calls. Both
begin by asking what *kind* of stroke it was:

> a stroke whose box is more than 24 by 16, and which - from its fifth
> point on - goes more than twenty pixels from where it started and then
> comes back to within twenty of it, is a **lasso**: it went round
> something rather than through it.

A lasso asks the children for a whole-object hilite (kind 1); anything
else asks them what they would take (-1). The children are then asked
twice: once to find the strongest claim, and once to carry that claim
out, so that *every* child that can take the same kind of hilite takes
it. That is what selects several paragraphs with one stroke.

The kinds, and what answers them:

| kind | what it means | who takes it |
|---|---|---|
| 1 | the whole object | `TView::HandleHilite`, `TParagraphView::HiliteParagraph` |
| 2 | whole lines | `TParagraphView::HiliteLines` |
| 3 | a range of characters | `TParagraphView::HiliteRange` |
| 5 | a whole container | `TContainerView::HandleHilite` |
| 6 | the words run through | `TParagraphView::HiliteWords` — which the ROM's is `mov r0,#0; mov pc,lr`, so nothing ever takes a 6 |

`TView::HandleHilite` (0x00262150) is the whole-view test: the stroke's
box, grown by eight pixels, has to cover more than 60 per cent of the
view — and a stroke that is long and thin counts by its long axis alone,
because a line drawn across a one-line view covers very little of it.

`TContainerView::HandleHilite` (0x00074770) asks its children after
itself, and *promotes* their answer: a child that would take a 1 makes
the container answer **5**. So a lasso round something inside a
container selects the container, not the one child it went round. When
the 5 is carried out the container drops its own hilites, offers a 1 to
each child in turn and stops at the first that takes it, and that child
becomes the container's hilite.

`TParagraphView::HandleHilite` (0x00169b0c) tries its four in order 6, 1,
2, 3:

- **HiliteParagraph** (0x00169c0c) wants the stroke to cover 60 per cent
  of the laid-out text. A stroke at least twice as tall as it is wide,
  drawn between the view's left and right edges, is judged by its
  vertical extent alone — which is how a line down the margin takes the
  whole paragraph.
- **HiliteLines** (0x00169da0) also wants a stroke twice as tall as it is
  wide, and takes the run of lines it covers half of, ending at the first
  line it does not.
- **HiliteRange** (0x00169ec4) is the ordinary case: a line drawn through
  some words. The stroke's rough outline — `TUnitPublic::RoughShape`
  (0x0022d198), which is `AsPolygon` (0x00145e38) of its first stroke,
  the points rounded to pixels and moved to the top left of their box —
  is walked from each end by `FindFirstWordHitByHilite` (0x00169fbc),
  stepping a pixel at a time along any segment that moves more than one
  pixel across and asking `PointToWordBoundary` (with a bias of -50,
  towards the left) at every step. The two boundaries found are the ends
  of the selection; both ends finding the same one means the stroke
  crossed no text.

`TEditView::AddHiliter` then works out the *click options* the selection
offers: a lasso, or a selection that has come to cover more than one
child, may be resized (unless the children's own bounds say otherwise);
anything else may not. A lasso drawn on the Calendar is not a selection
at all — that application answers it by redrawing itself.

### Two things this needed fixing along the way

`TView::DoCommand` (0x00268bcc) did not pass a command it had not taken
on to the parent, so nothing posted to a view could ever reach the root
or the application. It does now — and a `RealDoCommand` that answers
**2** stops the walk while still telling the caller that nobody took the
command, which is how a `viewClickScript` returning `'skip` lets the
click through without it being handled twice. Two commands were also not
marking themselves taken (`aeHide`, `aeDropChild`), which only showed up
once the walk existed.

`TRootView::CommonSetKeyView` asked `GetHiliteView` where the ROM asks
`GetEnclosingEditView` (vtable +0x140). The two agree for a container
but not for a paragraph, which is its own hilite view: with the wrong
one, selecting a second paragraph dropped the first's selection, and a
stroke through several paragraphs left only the last one selected.

## Modal dialogs (`views/ModalDialogs.cpp`, `newt/ModalDialogNatives.cpp`)

A view opened with `:FilterDialog()` or `:ModalDialog()` is *modal*: it
is marked with the private viewJustify bit `vjIsModal` (0x40000000, above
the thirty bits a script can set) and `SetModalView` (0x0030de2c)
confines recognition to its outer bounds
(`TRecognitionManager::EnableModalRecognition`), so a tap anywhere else
does nothing.  `gModalCount` says how many are up; while any is, a view
asked to show waits (`ModalSafeShow`) and a command not marked
`kNoModalCheck` is refused.  The dialog's context's `modalState` is what
tells the two kinds apart:

- `FilterDialog` (0x0030e054) sets it to TRUE and returns at once - the
  caller goes on, and the dialog's own scripts decide what happens
  (`AsyncConfirm` is made of it);
- `ModalDialog` (0x0030de88) sets it to the address of a
  `TPseudoSyncState` (`utility/PseudoSyncState.h`) and *waits* on it: the
  newt world forks, a new task takes over the event loop and runs the
  dialog, and the asking script carries on only once the dialog is
  exited (`ModalConfirm` is made of it).  A view inside a dialog makes
  the root's child that holds it the dialog.  What was being recognised
  when it opened is put aside for the fork
  (`TRecognitionManager::SaveRecognitionState`, with the stroke world's,
  the controller's and the arbiter's) and put back afterwards.

`RealExitModalDialog` (0x0030e14c) undoes it - from `:ExitModalDialog()`,
or from `TRootView::ForgetAboutView` when a modal view goes: the mark and
the recognition bounds come off, the count goes down, the waiting views
are shown when none is left (or recognition moves to the frontmost
dialog left, `GetFrontmostModalView`), a `ModalDialog`'s opener is
unblocked, and the dialog is sent `UnstuffModalCommandKeys`.  ROM
behaviour kept: a dialog exited while still open is itself the frontmost
dialog with a `modalState` (nothing takes a FilterDialog's away), so
recognition stays confined to it until something moves it.
`ForgetAboutView` is now the ROM's whole list - it also unregisters the
view's keyboard and makes the recogniser forget it was clicked.

(Tested by `test_Views`: `TestModalDialogViews`; the fork by `test_Newt`
and `src/host/demo/modal.ns`.)


## The clipboard (`views/ClipboardView.h`)

A *clipping* is what the Newton makes when something is dragged out of a
view and let go at the edge of the screen (within five pixels of the
application area's edge, `PointOnClipboard`).  It is two views put on the root
together: the **clipboard** (`TClipboard`, class 101, 0x0009edfc-
0x000a0b78) which holds the dragged items and draws the picture taken of
them, and its **icon**, a small paragraph of the clipping's label that
sits at the edge of the application area and is what the pen picks the
clipping up by.  The root view keeps them as two parallel arrays of
contexts, front first, so the icon at index *i* belongs to the clipboard
at index *i* (`TRootView` +0x54 and +0x58).

`TClipboard::NewClipboard` (0x0009f188) makes one.  Every item of the
drag is asked of the source view for its data of each of its types
(`GetDropData`), and, when the source answers nothing, of the item's own
view; the data is made internal (it may be pointing into a store), given
an empty `viewBounds` when it is `'text` without one (`CheckViewBounds`
0x0009cb00), and moved out of the source view's coordinates
(`OffsetBoundsRef` 0x0009ca58).  The types go into a `types` array and
the data into a parallel `data` array of arrays, which is what the
clipping's context carries alongside the `bounds` the items came from
and a `bits` picture of them - `TView::GetClipboardDataBits`
(0x0009e528) draws the view into a `'bits` binary through a `TBits`.
A heap too full for the binary is not an error: the clipping is made
without a picture, and `DrawDragData` (0x000a0454) frames its outline in
gray instead of drawing it.

`CreateLabelForm` (0x0009fcf0) makes the icon.  The label is the first
item of the drag that has one, or the `text` of the first `'text` item's
data, or the ROM's own string `"data"` (magic pointer 63); tabs and
returns in it become spaces so that it stays on one line, and it is cut
at 50 pixels with an ellipsis (`TruncateLabel` 0x0009f84c, which measures
the characters one at a time until the fiftieth pixel).
`CalcIconDimensions` (0x000a08a4) then asks how big the paragraph has to
be - the label's advance rounded to a pixel, and the tallest ascent plus
the deepest descent of the style and of every ink word in it - and
`CalcIconBounds` (0x000a0a50) places it, with the point at the middle of
its top edge, clamped to the application area.

The icon's `pin` slot records which edges of the application area it has
come to rest against (1 left, 2 top, 4 right, 8 bottom).  That is what
`FReOrientLabelForm` (0x0009f978) - a C function the form carries as its
`ReOrientToScreen` - uses to put it back against the same edges when the
screen is turned round.  The button bar's own edge is the exception: an
icon pinned to the edge the bar is on is moved to the far side instead,
so that it does not end up underneath it.  The same asymmetry is in
`PointOnClipboard` (0x0009e2b0), which is the question "was this drag let
go on the background?" - a point past the application area's edge counts,
unless that edge is the button bar's, because the bar is drawn on top of
the application area rather than beside it.

Both views go on and come off the root through the ordinary
`aeAddData`/`aeRemoveData` commands (`TRootView::AddClipboard` 0x001b37fc
dispatches one command twice, with each template in turn as its frame
parameter), so each half is separately undoable.  `TRootView::
RealDoCommand` is what maintains the arrays: the new context is inserted
at the front of the icons or of the clipboards by which class the view
turned out to be, the `clipboardDepth` preference (one by default) says
how many clippings are kept and the last is removed when there is one too
many, and the clipping that has just been covered is dimmed - an icon by
its `viewFillPattern`, a clipboard by giving up the picture it drew.
Adding a clipboard plays `addSound`; removing either half removes its
partner, because `TClipboard::EndDrag` dispatches an `aeRemoveData` for
both.

Picking a clipping up again is `DragFromClipboard` (0x000a0380): the
clipping's own items are made into a `TDragInfo` whose dragRefs are their
indices (`GetClipboardDataInfo` 0x000a02d8; `GetDropData` looks the index
up again and deep-clones the answer, so the clipping keeps its own copy
whatever the taker does with it) and dragged from where the picture is
drawn (`CalcDataBitsBounds` 0x0009f6dc, which lays the items' rectangle at
the icon's top left and pushes it back inside the application area when
it hangs off the right or the bottom).  A second stroke within 80 ticks
of the last is a *copy* rather than a move, which is how a clipping is
left behind by tapping it twice.

The drag that makes one is `TView::DragAndDrop` (above), which also
moves a clipping dragged by its label (`MoveIcon` 0x0009f568).

## Questions asked of shapes (`views/ShapeVerbs.cpp`)

`FindShape(shapes, x, y, style)` is the hit test a drawing application
selects with: which shape of a list is nearest the pen, and which corner
of it.  `DoFindShape` (0x000e1be0) walks the list (a style frame in it put
in force for the shapes after it, as drawing does) and tests each shape
within the style's `selection` distance, six when there is none: a
rectangle's or round rectangle's outline is the band between its box grown
and shrunk by that distance (filled, anywhere inside), a line and each side
of a polygon by `DistanceFromLine` - the cross product over the cheap
length, so only roughly the perpendicular distance - an oval's and a
wedge's outline as a distance from the middle between half the shorter and
half the longer side, a region by its own bytes, ink by every expanded
point within the distance and the pen's width, and a bitmap, text or
picture by its box.  With a selection distance a point in one of the box's
four corners (0 top left, clockwise) finds the shape whatever its outline
says - that is where a selected shape's handles are.  A find nearer than
the one held starts the path again as [distance, corner] and every list
level adds its index on the way out; `FFindShape` turns it round into
{vertex, path} (path true for a single shape).  Two ROM bugs are kept: the
comparison with the find already held is against its Ref, four times the
distance, and a filled oval or wedge asks `PointInShape` and then takes the
shape whatever it answered.

`GetShapeInfo` answers `canonicalShapeInfo` with the bounds (a bitmap's is
`GetBitmapInfo`'s frame), plus a text's string, a line's two ends as
{x, y} frames and a wedge's `bitsBounds` - `WedgeBox` (0x000e148c), which
answers only the quarter of the box the start angle falls in (the arc is
never looked at).  `MakeInk` wraps an ink binary in `canonicalInkShape`
with `bounds` and `originalBounds` (two 'boundsRect binaries).
`StrokeInPicture` asks `PtInPicture` whether a stroke ended on a picture.
`AnimateSimpleStroke` plays a recorded drawing back as though written: the
binary is the rectangle it was drawn in, then strokes of a count, a start
point and nibble-pair moves, mapped into the destination; with a pen the
stylus picture (`gtPens[2]`) is drawn at each point a tick at a time over
a `TBits` copy of what was under it.  No ROM script calls it, so its bytes
are read big-endian as the ROM reads them.

`MungeShape(shape, operation, style)` turns ('rotateLeft, 'rotateRight)
or flips ('flipHorizontal, 'flipVertical) a shape about the middle of its
box (`views/ShapeVerbs.cpp`).  The point and rectangle turners
(0x000de6d8-0x000dea9c, the centre (cx, cy)): `RotatePointR` makes (h, v)
into (cx + cy - v, cy + h - cx), `RotatePointL` into (cx + v - cy,
cy + cx - h), the flips mirror about cx or cy, and the rectangle turners
do the same to the corners (`RotateRectR`: top = left - cx + cy, left =
cx + cy - bottom, bottom = right - cx + cy, right = cx + cy - top).
`DoMungeShape` applies them to a geometric shape's data (a list member by
member, a style frame in it applying to what follows), turns or scales an
ink shape's strokes one by one (`TStroke::Rotate`/`Scale` over
`toolbox/Matrix.h`, the 3x3 16.16 matrices) and packs them again, and
turns a bitmap with `MungeBitmap` (`qd/MungeBitmap.cpp`) - a region,
picture or text first drawn into a bitmap of its own, which is what
comes back (ROM quirk: the shape drawn is left moved to the origin).
`MungeBitmap` turns the bits themselves: a quarter turn takes 32 columns
by 8 rows at a time and makes each column of eight a byte of a new
'pixels object (the bounds and resolution turned too), a flip reverses
bytes through the ROM's `bitFlip` table (left to right, after moving the
row right by its padding) or swaps rows, and a half turn reverses the
whole buffer in eight pieces, telling an options `callback` the percent
done - with three quirks kept: the rows' padding ends up at their start,
a few bytes in the middle are left when the size is not a multiple of
sixteen, and a bitmap under sixteen bytes is not turned at all.
`test_Views` turns and flips a 10 x 3 bitmap pixel by pixel.  NOT YET: a
quarter turn of a fax page - `Tilable`'s four sizes are 216-byte rows
(1728 pixels, a G3 fax line) by 1146, 2292, 1152 or 2304 rows, not the
screen as was once written here - is `RotTiledBitmap` 0x00040b54 over
`TTile`, which makes the turned copy on the same store and with the same
compander as the page's own large binary and fills it a tile at a time.
It is not reconstructed because nothing in the host reaches it: large
binaries are NOT YET (`IsLargeBinary` answers false, and
`MakePixelsObject`'s store arm throws), and the fax receiver that makes
the pages belongs to the comms stack, which is NOT YET too.  A script's
own heap bitmap of exactly a fax page's size is the only way in, and it
is left unturned.

## The outline list (`views/ListView.h`)

`TListView` (class 99, over `TEditView`; ROM 0x0010eec4-0x00112f74) is
what the To Do list and the Notepad's checklist and outline stationery
are built on.  Its context holds `topics`, an array of topic frames -
each its `text` and `styles`, its `viewBounds` in the list, its `level`
(1 at the left) and a `hideCount` (not nil and not nought: it is inside
a collapsed topic) - and the list's own C++ side is mostly arithmetic
over them, reached from NewtonScript through thirteen natives
(`SetupVisibleChildren`, `CollapseTopic`, `ExpandTopic`, `IsCollapsed`,
`ListBottom`, `FamilyBottom`, `TopicBottom`, `MarkerBounds`,
`MakeDragRef`, `ChildTemplateFromTopic`, `AdjustParagraph`,
`TopicIndexToView`, `VisibleTopicIndex`).

**Laying the topics out.**  `SetupVisibleChildren` is a list's
`viewSetupChildrenScript`'s work: for each visible topic from
`firstTopic` on it clones the context's `canonicalParaTopic` with the
topic's box, text and styles (`ChildTemplateFromTopic`), *adds it as a
real paragraph child* so that it can be measured, and places it with
`AdjustParagraph` - its first baseline the next one after the last
paragraph's (`TParagraphView::GetNextBaseline`), at least sixteen
pixels lower, at the list's top margin for the first - indented 20
pixels a level past the marker gutter (`leftMarkGap` +
`rightMarkGap`).  The topic's `viewBounds` is set to where the
paragraph came out, and when the loop is done the measuring children
are all thrown away again (`RemoveAllViews`, the call at vtable +0x7c):
what the function answers is the array of templates, which becomes
the list's `viewChildren`.  With `minimalChildren` set it stops at the
first topic below the part of the list that shows and sets `lastTopic`.

ROM QUIRK, kept: `AdjustParagraph` adds the right edge it is given to
the list's *top*, not its left - it loads the wrong half of the
origin's word (an unaligned `ldr [sp,#0x12]` rotates the top into the
low half).  The right edge `SetupVisibleChildren` passes is already
global (the list's right plus `rightIndent`), so the paragraph comes
out the list's top too far right - and then the paragraph's own
`SetBounds` brings an edge past the parent's back to it, so it does
not show.

**The baselines** (`TParagraphView::GetFirstBaseline`,
`GetLastBaseline`, `GetNextBaseline`, `AdjustBoundsForFirstBaseline`)
are new with the list.  The ROM keeps four halfwords at +0xa0 as it
lays the lines out (the first line's baseline and ascent, the last's
baseline and descent); the host's line cache has no such block, so
they are read off the first and last `LineInfo` (a DEVIATION).  An
empty paragraph measures by the style an insertion would take.

**`TParagraphView::SetBounds`** turned out to be much more than the
`TView` one it had been standing in for: it keeps the box within the
parent's width (the right edge brought in to the parent's when it goes
past it, or always for a paragraph whose input flags have
`vWidthIsParentWidth`; the left out to the parent's), *writes the box
to the data frame's `viewBounds`* - which is how `SetupVisibleChildren`
reads it back - and lays the lines out again (`FixupBBox`, which is
where `vWidthGrowsWithText` brings a paragraph in to its text's
width).  All of it is skipped while the paragraph's `fTextFlags` is
still -1, before `SetupDone`: the check is made through an unnamed
accessor at vtable +0x20 that answers the field itself.

**Collapsing** (`CollapseTopic`/`ExpandTopic`) adds or takes one off
the `hideCount` of every topic under the one given and rebuilds the
children when asked.  Both read `topics` from the context itself,
without inheritance, so a list's setup must put it there.  ROM QUIRK,
kept: a topic with no `hideCount` counts as one hidden already, so
collapsing and expanding its parent leaves it at 1, still hidden; the
ROM's own stationery gives every topic a `hideCount` of 0.

**The gutter.**  `RealDraw` draws each visible topic's marker from the
context's `topicMarkers` strip (0 expanded, 1 collapsed, 2 hilited), or
its priority out of `priorityItems` when `listViewFlags` has 4 or 8,
and its check box out of `checkBitmaps` when it has 2 - all through
`DrawXBitmap`.  `listViewFlags` 1 moves them right past a gutter.  The
pen (`HandlePenDown`, the list's own virtual at +0x12c): a tap in a
marker toggles the topic (`toggleTopic`, `curTopic`), a drag from it
carries the topic and everything under it as a 'topic drag
(`MakeDragRef`: the indexes, the level, copies of the topics moved to
the top and the total height) whose drop caret `PointToCaret` puts
between two topics at the level the pen's x says (`IndexFromY`,
`LevelFromX`) - or nowhere, inside the dragged family or where it would
strand the next topic - and a tap in the check box checks it
(`TrackCheck`, `handleCheck`).  A scrub asks the context's
`handleScrub` first.

`test_Views`'s `TestListView` lays out four topics, collapses and
expands them, checks the markers' boxes and the drag frame, and pins
the `hideCount` quirk.  "remind me to call Daniel" in
`src/host/demo/assist-tasks.ns` now opens the To Do list with the task
in it, check box and priority drawn.

## A meeting in the day view (`views/MeetingView.h`)

`TMeetingView` (class 95, over `TContainerView`; ROM 0x001ca1a8-
0x001cbd54) is one meeting in the Dates day view: a container of a
slider (child 0) and the meeting's text (child 1, a paragraph).  Its icon - the
context's `iconShape`, drawn by the day view's `viewDrawScript` to the
right of the slider - is what the pen picks it up by (`HandleClick`: a
tap runs the viewClickScript, a drag carries it away as a 'meeting or
its text as 'text, with a whole-container hilite on the context for the
drag's length).  Everything else - a word written on it, a scrub, a
hilite, a drop - it hands to the text, answering the arbitration's
questions itself: a scrub covering half the icon takes the whole meeting
(5), a word mostly on the meeting is taken (5), one with 120 pixels to
spare to the right or written within a second of the last is added (3).

`LayoutMeeting` (the native) is where a meeting goes down the day: its
start and end, the minutes into the day, as a top and bottom
(`TimeToPosition`), and across either the whole width, the right half
(a viewBounds of TRUE) or its own box.  `GetMeetingSlot` reads a
repeating meeting's slots through its `repeatTemplate`, and
`GetMeetingTypeInfo`/`GetMeetingIcon` answer the kind of meeting out of
the Dates application's `meetingTypeRegistry` - by its meetingType, its
mtgIconType, else its stationery.

`TSliderView` (class 96, `views/SliderView.h`; ROM 0x001c97b8 and
0x001cbd54) is the bar down a meeting's left that says how long it is:
the view two pixels in from its sides painted black as a polygon whose
ends slant by its width (`TRectToSliderPoly`).  The pen dragged down it
moves the end of the meeting - the screen below it saved
(`TSaveScreenBits`), the bar redrawn longer as the pen goes down and the
screen put back under it as it comes up, never shorter than twelve
pixels - and the meeting's `SetMeetingBounds(new, old)` is told the
result; a scrub over the bar deletes the meeting (an `aeRemoveData` to
the page, with a poof); `aeScaleData` is the drag's undo.  ROM BUG,
kept: the polygon the press inverts is never killed.

Getting the day view to draw a meeting turned up two host bugs outside
the views.  A pattern made from rows (`MakeSimplePattern`, and so every
standard pattern at `InitGraf`) was allocated at the ROM's size, a
0x1c-byte PixelMap and eight rows, where the host's PixelMap is bigger:
every one wrote past its handle, and the heap broke later somewhere
else (DEVIATION: sized from the host's struct).  And the ROM's own
shapes keep their rectangles as big-endian `'boundsRect` binaries, which
the host read in its own order - so the meeting's 13 by 17 icon came
out 4352 by 3328 and was stretched over the side of the day.  The
object area import now turns the shapes' halfwords round as it already
did strings and reals (`frames/ObjectAreaImport.cpp`, `analysis/
nsfunctions.py --binary-classes` saying which classes there are).

## The rest of the view natives (`views/ViewExtraNatives.cpp`, `SplashScreen.h`)

- **A roll scrolled** (`TView::SyncScroll` 0x002635f4, protoRoll's
  viewScrollUpScript/viewScrollDownScript - the Preferences roll): the
  roll's `viewOriginY` is how far into the first item showing it is
  scrolled and `index` that item.  Down goes further into an item taller
  than the roll, else on to the next; up comes back up it, else back to
  the item before (taller than the roll: to `h - h % height`).  The items
  from the lower of the two indexes on are walked while they fill the
  roll (a collapsed one, or every one under `allCollapsed`, counting as
  `collapsedHeight`): those from the new index on become the children
  (made where missing, put in front when going up, the rest removed), the
  ones scrolled past add their height to the slide, and the slide is
  animated with the scroll sound over the roll less its bottom 5 pixels.
  NOT YET: `TView::SyncScrollSoup` 0x00263034, the same over a soup
  cursor (by `overlapScrollAmount`, or twice the line spacing) - a roll
  over a cursor answers nil.
- **GrayShrink** 0x0003ec94: a bitmap shrunk into the view's bounds or the
  second rectangle of the style's `transform`, with the style's
  `grayLevels` in the user's preferences while the bits are copied.  ROM
  bugs kept: the destination is offset by the view's top-left even when it
  is the (already global) bounds, and a `grayLevels` that is not an array
  sets the preference to nil on the way out.
- **FormatVertical** 0x001f0aa4 (a global taking its view from self): the
  children stacked from a rectangle's top, spaced (ROM bug kept) by the
  height less the children's total over `ChildrenHeight`'s count, which is
  one more than there are.
- **ComputeParagraphHeight** 0x001ecfd0: a paragraph frame's text fitted
  to a width (`TextBounds`), never under 50 - its box built on the stack
  as `{top, 0, top, width}` through an unaligned load (read from the
  assembly).  **ExtractRangeAsRichString** and
  **ExtractRichStringFromParaSlots**: a range as a rich string (the second
  only when an ink word falls in it).
- The picker's **GetScrollerValues** (`TPickView::GetOverflows`, a tail
  call) and **Scroll**; **KeyboardInput** (the key view, the caret showing,
  a keyboard active); the Inspector's **DV** (the view dumped and flashed
  eight times) and **ViewAutopsy** (an integer sets `gSlowMotion`, anything
  else toggles `gOutlineViews`).
- **DrawGraphic** 0x0014708c: the maker's splash picture - a
  `TSplashScreenInfo` implementation named `TMainSplashScreenInfo`
  (`SplashScreen.h`) - drawn centred in a box.  The MP2x00 registers none,
  so it answers nil and the script draws its default picture.

`test_ViewExtraNatives` calls each from NewtonScript over an offscreen root
(the root the host builds without a template has only `MakeViewMethods`'
list, so the test copies the ROM's own `viewroot` methods it needs; on the
machine the root's proto is `viewroot` itself).

NOT YET, measured: the **key-help slip**'s `viewSetupFormScript` and
`viewDrawScript` (`FKeyHelpSlipSetup` 0x00183c38, 1.5 KB;
`FKeyHelpSlipDraw` 0x00184244, 2 KB; with `GetCommandCharWidth`,
`GetModifiersWidth`, `DrawModifierIcons` and `GetSlipWidth`
0x001839f8-0x00183c38 - the command keys gathered and categorised
(`GatherKeyCommands`/`CategorizeKeyCommands` are done), laid out in one or
two columns of 100 pixels, the command letters drawn with the modifier
icons before them and the names truncated with `StyledStrTruncate`); and
**ReFlow**/**ReflowPreflight** 0x001a5cd8-0x001a6720 with `ReflowText`,
`SplitStyles`, `MungeStyles`, `MungeAllStyles`, `MungeInkScale`,
`SaveStylee` (about 7 KB) - the print and fax formats' reflow of a page's
paragraphs into `printerPageBounds`.

## Not yet

The rest of the
paragraph's editing (the hilites typed over, the style and clipboard
commands, ink words, the correction info, the caret's line moves), the
key help (above), the keyboard tool and the on-screen keyboards, the sounds, the popup and
modal dialog machinery, the other subclasses, the strokes and words of the recogniser (its controller and
domains: `docs/recognition/README.md`).
