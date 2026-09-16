# The view system

Reverse-engineering notes on Newton's view system - the C++ `TView`
hierarchy behind every NewtonScript view frame - and the plan for
reconstructing it in `src/views/`.  Nothing is reconstructed yet; what is
here was established while building QuickDraw (`docs/qd/README.md`),
which the views draw through.  How each fact was established is stated
with it.

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

## Plan

1. `src/views/View.h`: `TView` with the ROM's layout (fields from the
   constructor, the methods in vtable order from `analysis/vtable.py`),
   the slot cache, `BuildContext`/`Constructor`/`SetupForm`/`SetupDone`,
   the child list (`TViewList`, `AddView`/`RemoveView`/`ReorderView`),
   bounds (`viewBounds` with `viewJustify`, `JustifyBounds`/
   `DejustifyBounds` 0x00262224/0x00262b1c, `OuterBounds`, `SetBounds`,
   `Move`), flags (`SetFlags`/`ClearFlags`, `vVisible`, ...), `Show`/
   `Hide`/`Dirty`, `RunScript`/`RunCacheScript` (the view scripts),
   `GetValue`/`SetValue` (`viewValue` slot changes with redraw), `Delete`.
2. Drawing: `Draw`/`Update`/`DrawChildren` 0x00265b54.. with the
   `visRgn` per view (`SetupVisRgn` 0x00265a3c, `Clipper`), `PreDraw`/
   `RealDraw`/`PostDraw` (the `viewFormat` word: frame styles, fill
   patterns, rounded corners, shadows - `vfFillWhite`, `vfFrameBlack`,
   `vfRound...`), the `viewDrawScript`, hilites.
3. `TRootView`: the screen's port, the update region, `RefreshViews`,
   idle, the view with the caret, `gRootView`.
4. The NewtonScript view natives (`src/views/ViewNatives.cpp`) and the
   host test: a root view over an offscreen map, templates built in
   NewtonScript (`{viewClass: clView, viewBounds: {...}, viewFlags: ...,
   viewFormat: ...}`), opened, drawn and checked pixel by pixel.
5. The subclasses as they are needed: `TContainerView`, `TPictureView`,
   `TPolygonView`, `TListView`/`TPickView`, then the text views.
