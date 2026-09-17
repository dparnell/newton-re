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
the ROM.  `Show`/`Hide` 0x00263f48/0x0026404c run the `viewShowScript`/
`viewHideScript` (the ROM's animation effects and stroke blocking are
NOT YET); `ReorderView` 0x0025eda0 (`MoveBehind`, `BringToFront`)
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
light gray.  NOT YET: tracking the pen (`TrackSetValue` 0x0018b344 on
aeClick).

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
top.  NOT YET: the pen tracking on aeClick (`TrackStroke`), the key
commands and type-select, ink items, the pickable test inside a masked
grid picture, the item flash's waits.  The ROM's protoPicker has
viewFlags without vVisible: it is opened with `:Open()`.

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
aeDropChild), idlers (SetupIdle, the idle script re-timing and stopping
its idler, removal with the view), pickers from the ROM's protoPicker
(the rows, the placement below and above, the separator, marks, an
icon, a cut item, the item under a point, a pick closing the picker).

## Not yet

Hilites and selection (`THilite`, `HiliteLoop`), the caret and key views,
drag and drop, the key events (`HandleKeyEvent`), the animation
effects (`TAnimate`), `SyncScroll`, the clipboards, the popup
and modal dialog machinery, the other subclasses (`TListView`,
`TEditView`, ...), editing in `TParagraphView`, the picker's pen
tracking and keys, the
recogniser's units behind the click and gesture commands, the event
loop (`TNotebook::Run`) and the idle timer.
