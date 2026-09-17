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
methods in the vtable's order (`analysis/vtable.py build/MP2100D 0x1f75c`),
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
builds the caches.  NOT YET: editing, hilites, the caret, ink, tabs (drawn
as characters), the text objects, the parents' bounds narrowing the
lines, the empty last line after a final carriage return.

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
0x000ded00 moves one in place, `IsPrimShape` 0x000deac8 tells a single
shape from a list.

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
German mapping's tables, a dead key, the key maps and modifiers through
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
the caret the same way (the caret popup NOT YET).  The unit functions of
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

## Not yet

The hilites of data views (`THilite`, `HiliteLoop`, `TContainerView`),
the rest of the
paragraph's editing (the hilites typed over, the style and clipboard
commands, ink words, the correction info, the caret's line moves), the
the key help, the keyboard tool and the
on-screen keyboards, drag and drop (`DragAndDrop`, `TDragInfo`), the
sounds, `SyncScroll`, the clipboards, the popup and modal dialog
machinery, the other subclasses (`TListView`, `TEditView`, ...), editing
in `TParagraphView`, the strokes and words of the recogniser (its
controller and domains: `docs/recognition/README.md`).
