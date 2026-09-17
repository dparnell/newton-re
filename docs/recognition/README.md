# The recognition system

Reverse-engineering notes on Newton's recognition system - the C++
objects between the tablet and the views' `viewClickScript`,
`viewStrokeScript`, `viewGestureScript` and `viewWordScript` - and its
reconstruction in `src/recognition/`.  How each fact was established is
stated with it; the reconstruction cites the ROM function each of its
functions comes from.

## What the ROM has

The recogniser is a set of classes over its own object base
(`TRecObject`, 8 bytes: a vtable and a flag word; `TArray`/`TDArray`,
0x20 bytes: a growable array of fixed-size entries in a handle, with a
use count for `Clone`/`Release`).  The chain from pen to view:

- `StrokeCentral` (`gStrokeWorld`, 0x0c1019b8) collects the tablet's
  points into `TStroke`s and makes a `TClickUnit` of every pen-down
  (`StrokeCentral::IdleStrokes` 0x0014640c: `StrokeGet()`,
  `TClickUnit::Make(gRootDomain, 1, stroke, nil)`, flag 0x4000000 set,
  `TController::NewClassification`); when a stroke has a *click event*
  noted in it (its word at +0x48: 2 a tap, 3 a double tap, 4 a hilite
  click, 5) it makes a `TClickEventUnit` over the click unit and
  `ClearEvent`s it (the stroke's event set to 1, processed).
- `TController` (`gController`, 0x0c101968) runs the *domains*
  (`TDomain`, 0x24 bytes: the piece types it groups, its type and name,
  the delay its units wait) over the units, and `TArbiter` (`gArbiter`,
  0x0c10196c) arbitrates between the units of the same strokes.
- The *areas* (`TRecArea`, 0x30 bytes) are what a view's recognition
  flags and `recConfig` become: the unit types accepted with their
  recognisers (a `TTypeAssoc`), the domains, three dictionary chains,
  the view's id at +0x2c; `gAreaCache` (0x0c1008a0) keeps the areas of
  the views hit.  A unit lies in one area (kept at +0x18) or several (a
  `TAreaList`, flag 0x20000).
- `TRecognizer` (0x20 bytes) is what the application side knows of a
  recogniser: the domain (+4), the unit type as its id (+8), the command
  the view system dispatches for it (+0xc: 0xb aeClick for 'CLIK', 0xc
  aeStroke for 'STRK', 0x31 aeTap for 'CEVT', 0xd for the gestures, 0x12
  aeWord), flags (+0x10, a byte: 8 the unit's bounds are its stroke's,
  2 arbitrated), the arbitrate time (+0x14), the services it can and
  does provide as viewFlags bits (+0x18, +0x1c).  `HandleUnit(TUnitPublic*)`
  answers the command to send (0 for none).  The recognisers are
  installed by `TRecognitionManager::InitRecognizers` 0x0019f6f4 into
  the list at `gRecognition+0x14` (`TRecognizerList`, a `TArray` of
  pointers): the gesture recogniser (a `TEdgeListDomain`'s type, command
  0xd, flags 2, arbitrate 1, service 0x800), event ('CEVT', 0x31, flags
  10, 0, 0x800), stroke (`TStrokeDomain`'s type, 0xc, 10, 0, 0x400),
  click ('CLIK', 0xb, 10, 2, 0x200 vClickable); at level 2 the shape,
  word and `TWRecognizer` (0x12, flags 1, 1, services 0x17ef000)
  recognisers too (`Install...Recognizer` 0x001454c4-0x00145be8).  `gRootDomain`
  (0x0c101970) is a plain 'ROOT' `TDomain`.
- `TUnitPublic` (0x3c bytes) is the face of a unit the views and the
  unit natives see; `HandleUnitList` hands each unit to its
  recogniser's `HandleUnit` and posts the command to the view found.

## The units

`TUnit` 0x30 bytes (`TUnit::IUnit` 0x0022bb5c): +8 the type (a
four-character code), +0xc a `Rect` box in pixels (`SetBBox` rounds a
Fixed `FRect`), +0x14 the domain, +0x18 the areas, +0x1c the start time
(`GetTicks()` = `Ticks()`, sixtieths), +0x20/+0x22 halfwords (the
duration; the ticks from the start to the last sub added), +0x24 the
kind byte the maker passed (1 for the stroke world's clicks), +0x25 the
use count (`Clone`/`Release`: 0 means one user; `Dispose` frees the unit
when `Release` finds none left), +0x26 the priority, +0x27 the delay
(`SetDelay`: at most 255 ticks; flag 0x10000000 while not 0), +0x28
0xffff until the first sub, +0x2a/+0x2c the first and last of the
controller's strokes the unit covers.  Flags: 0x40000000 claimed
(`ClaimUnit` = `MarkUnit(list, 0x40000000)`: the unit added to the list
and flagged), 0x8000000 invalidated, 0x400000 invalid, 0x80000 passed
up from the subs.  The vtable (0x1f708; `analysis/vtable.py`): Dispose,
Dump, SizeInBytes, CopyInto, IDispose, Clone, Release, SubCount,
InterpretationCount, GetBestInterpretation, DumpName, ClaimUnit,
MarkUnit, Invalidate, DoneUsingUnit, CountStrokes, GetStroke,
GetAllStrokes, OwnsStroke, ContextID, SetContextID.

`TSIUnit` 0x3c (sub-units and interpretations): +0x30 how the subs are
kept (0 none, 1 the one sub at +0x34, 2 a `TDArray` of them at +0x34),
+0x31 whether +0x38 is the interpretations' `TDArray` (else it holds
their element size for when the list is made).  `AddSub` 0x0021a380
merges the sub's box, start time and duration, stroke range and the
passed-up flag into the unit, notes the elapsed ticks, re-delays the
unit as the domain says, and lowers the controller's next-event time
(`gController+0x20`) to the unit's expiry; `DeleteSub` disposes the sub
and folds a one-entry list back; `EndSubs` drops the delay.  A
`UnitInterpretation` is 16 bytes: label (-1 none), score (10000 worst;
`GetBestInterpretation` takes the lowest labelled), angle (Fixed), and a
parameter object (a `TArray` made by `InitInterpretation` when asked,
disposed with the interpretation).  The vtable adds AddSub, GetSub,
DeleteSub, EndSubs, AddInterpretation, GetInterpretation,
CheckInterpretationIndex, DeleteInterpretation, InsertInterpretation,
Lock/UnlockInterpretations, CompactInterpretations,
InterpretationReuse, GetSubsCopy, GetLabel/Score/Angle/Param,
SetLabel/Score/Angle, EndUnit.

`TStrokeUnit` 0x44 (`IStrokeUnit` 0x0021f708: 'STRK', the stroke's box,
down time to up time, +0x3c a context id, +0x40 the stroke - disposed
with the unit while its type is still 'STRK'); `TClickUnit` 0x34
(`IClickUnit` 0x0021ce70: 'CLIK', the stroke's box so far, its down
time, no duration, +0x30 the stroke - let out of the inker's buffer
(flag 0x10000000, `UnbufferStroke`) when marked or disposed);
`TClickEventUnit` 0x40 ('CEVT' over a click unit; +0x3c the event read
from the click's stroke, -1 until asked).  `CountStrokes(TUnit*)`
0x0022bc14 counts the distinct strokes under a unit by marking its
stroke range through the subs.

## The public face

`TUnitPublic` (`__ct` 0x0022a688): +0 the unit, +4 a `TWordList`,
+0x14/+0x18 the clean and rough shape polygons, +0x1c the stroke's
`TStrokePublic` (`Stroke()`: made once, not owning), +0x28 the word base
`Rect` (top -0x8000 until set), +0x30 a RefHandle for the word info
frame, +0x34/+0x38 the view found and the flags it was found with.
`Bounds` 0x0022b4c8: for a recogniser flagged 8 the stroke's box, else
the unit's, a pixel wider and taller (top and bottom -0x8000 when the
type has no recogniser).  `IsTap`: under 6 pixels each way.
`FindView(flags)` 0x0022b1e0: the view under the bounds' centre
(`TView::FindView(pt, flags, nil)`), then the closest within a (10, 10)
slop; while the arbiter's byte +0x21 is set, the view under the root's
centre with mask 0x1fffe00; the result is kept for the same flags.
`RequiredMask`: the recogniser's enabled services, strokes (0x400)
taking 0x17ef000 along and gestures (0x800) the clicks (0x200).
`InputMask`: the found view's `viewFlags & 0x1ffff00`.  `Invalidate`
0x0022b59c gives the root view the bounds (let out for the ink,
`AdjustForInk`) and every stroke's rect: drawn strokes' rects are
invalidated, undrawn ones' are marked screen-dirty and the strokes
flagged to draw no ink (0x20000000).  `CaretType`/`GestureAngle` read
the first interpretation (the gesture units': labels 2, 3, 5, 6 are the
carets; the angle snapped to 0/90/-90/180/135 within 20 degrees, 30 for
label 5).

## The click recognisers

`TClickRecognizer::HandleUnit` 0x0014578c finds the view with the
enabled services, saves it as the click view (`gRecognition+0x30`, the
previous at +0x2c), and answers aeClick unless another view's area is
in use (`OtherViewInUse`: an area of the cache other than the view's
has users) or clicks are being ignored (`gRecognition+0x18`, the tick
`IgnoreClicks` set); a click swallowed on a clicks-only area sets
`gRecognition+0x38`.  `TEventRecognizer::HandleUnit` 0x00145674 answers,
when the click's stroke is the last written (`OnlyStrokeWritten`), 0x31
aeTap for a tap, 0x32 aeDoubleTap and 0x37 for events 3 and 5 only when
the two clicks were on the same view, 0x34 for a hilite click.

## Reconstruction (`src/recognition/`)

`RecObject.h` (TRecObject, TArray, TDArray, the recogniser's handle
glue), `Stroke.h` (TStroke, TStrokePublic), `Unit.h` (TUnit, TUnitList,
TTypeList, TSIUnit, TStrokeUnit, TClickUnit, TClickEventUnit),
`UnitPublic.h`, `Areas.h` (TRecArea's layout and use counts, TAreaList),
`Domain.h` (TDomain), `Recognizer.h` (TRecognizer, TRecognizerList, the
click and event recognisers, TRecognitionManager: `gRecognition.Init(1)`
installs the two click recognisers and the root domain).  Tests:
`test_RecObject`, `test_Stroke`, `test_Unit`.

NOT YET: StrokeCentral and the tablet, TController and the arbiter, the
domains (stroke, edge-list gestures, shapes, words), TTypeAssoc and the
area cache (`InitAreas`, `GetAreasHit`, `BuildRecConfig`), the inker,
the word list and dictionaries, `HandleUnitList` and the unit natives.
