# The recognition system

Reverse-engineering notes on Newton's recognition system - the C++
objects between the tablet and the views' `viewClickScript`,
`viewStrokeScript`, `viewGestureScript` and `viewWordScript` - and its
reconstruction in `src/recognition/`.  How each fact was established is
stated with it; the reconstruction cites the ROM function each of its
functions comes from.

## Status: complete for the built-in fields (2026-09-28; revised 2026-09-29)

Everything the machine does between the pen and the views is
reconstructed and running on the host: the tablet buffer and the stroke
world, the controller, areas and arbiter, the click, gesture, shape and
word domains, both word recognisers - Rosetta for printing (the
classifier, the segmenter and the Viterbi search) and ParaGraph's
cursive reader (the low level, the xr reader, the post-processing, the
digit reader and the orthographic learning) - the dictionaries and the
Airus engine, the correction information, deferred recognition, the
corrector and ink grouping.  The demos write, correct and learn on the
Notepad (`write.ns`, `cursive.ns`, `numbers.ns`, `shapes.ns`,
`correct.ns`, `recognize.ns`, ...), with ctests over each.

What is left, and why:

* **Reachable - done 2026-09-29** (the gaps the NOT YET sweep found):
  all 125 of recognition's natives are answered (`GetAlternatives`,
  `Extract`, `insert`, `LookupCompletions`, `HandleUnit`, `HandleRawInk`
  and `VoteOnWordUnit` the last - `src/host/demo/alternatives.ns`, ctest
  `host.NewtonAlternatives`); `ValidateWord` asks the dictionaries
  (`LookupWordOrVariant`) and the recogniser in use
  (`WRecVerifyWordSymbols`); `FindBaseline` reads the word's base line
  with ParaGraph's `low_level` in its base-line-only mode (the heights at
  rc +0xec/+0xea, which the decompiler names after the halfwords that
  follow them - an unaligned load takes the one *before*), falling back
  on the box only when that base comes to nought; the arbiter weighs a
  shape against a word (`ArbitrateGraphicsWords`, `ArbitrateByRules`,
  `GetGraphicBiasedScore`, and the shape half of `ArbitrateEarly`); and
  `TWRecognizer::EndInkStrokeGroup` hands its run of ink on.  The engine
  stays in the ordinary heap rather than a VM heap of its own (decided:
  nothing on the host depends on it).  (`SafeExceptionNotify` prints to
  stderr where the ROM puts a notify slip up - a DEVIATION the owner
  chose, 2026-09-29, to keep script errors easy to see.)
* **Unreachable from the U.S. ROM**: `CheckDiacriticsDirections`
  (0x0007c9a0) and `AnalyseDiacriticsDirection` - about 3.2 KB asked only
  for a French or German letter set - and the sixteen-bit dictionary walks
  (`AE16_Verify`, `AE16_NextSet9`, `AL16_NextSet`/`AL16_NextSet9`),
  which only a UniChar dictionary would use; the machine's are all eight-bit.
* **Below the recogniser, in hardware**: the inker task (`TInker`,
  `InkerOff`, `TBCWakeUpInker`) and the tablet driver's calibration
  (`CheckTabletHWCalibration`); the host's `hal/host/HostTablet.h` and
  its inker stand-in feed the same tablet buffer.
* **Waiting on other areas**: the journal's replayed units
  (`HandleReplayUnit`), large binaries on a store (`FLBAlloc`, where
  training data would go), `CreateVMHeap` (the engine runs in the
  ordinary heap - DEVIATION), `WRecVerifyWordSymbols`,
  `TController::NextIdleTime` and `SearchAllocateReturnCache`.

The sections below are in the order the work was done, and many say
NOT YET of something a later section finished; the same holds for the
older comments in `src/recognition/`.  What the code cites
(`analysis/coverage.py`) is the record of what is done.

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

## From the pen to the stroke world

The tablet driver (a `TTabletDriver` protocol, `TResistiveTablet` in the
ROM extension: NOT YET) puts records into the *tablet buffer*
(`TBCInsertTabletSample` 0x0024e4e8; `InsertTabletSample` 0x0024e834
wakes the inker after): a ring of 250 words at 0x0c104464 with a write
index (`gTabData`, 0x0c104458), the inker's read index (0x0c10445c) and
the stroker's (0x0c104460).  A sample is one word - x in bits 18-31 and y
in bits 4-17 in eighths of a pixel, the pressure 0-7 in the low nibble;
a pen-down is two words (0xd, the time in ticks), a pen-up four (0xe,
the time, a dummy sample 0x14000000, the time).  `TBCPollTablet` and
`SetTabletPolling` are the calibration screen's one-sample mode.  The
inker (`TInker::LCDEntry`) draws the samples and calls `RealStrokeTime`
0x001fce38, whose reader `xGetTabPt` 0x000381f8 (through the recogniser's
glue `GetTabPt`/`LastTabPt`/`GetDownTime`/`GetUpTime` 0x0011d350-0x0011d360)
runs a pen state machine over the buffer (the `collect` block at
0x0c1008a8: +0x10 the state, 3 idle, 6 just down, 4 down, 1 just up;
+0x18/+0x1c the last down and up times; established from the code and
the ROM's initialised data, which starts the state at 3).

The *stroke queue* (`gStrokeQ` 0x0c101988 -> `sQ` 0x0c103e4c: a head and
tail halfword and 64 `TSStroke*`; a `TSStroke` 0x00220804 is a `TStroke`
of 0x58 bytes with its first point and the point the inker draws from
next) is filled by `RealStrokeTime`: `StrokeNext` 0x001fcd2c starts a
locked, queue-flagged (0x10000000) stroke after the head; the first point
takes the pen-down time (`gTickOff` added) and the pen tip
(`gLastPenTip` in flag bits 8-15) and starts the click-event watcher;
each point re-estimates the up time as down + count * `gSamplesToTicks`;
the pen-up sets the up time, judges the stroke, and `EndStroke`s it.
`StrokeGet` 0x001fcc04 hands the stroke world the tail's stroke once it
has points, flagged taken (0x80000000), inkless when `gDefaultInk` is
off.  The click-event watcher (`StrokeHiliteState`, 0x1c bytes, `oldHilite`
0x0c103fa0 and `newHilite` 0x0c103fbc; `InitHiliteState` 0x001fd094,
`CheckHiliteState` 0x001fd184) decides three things about a stroke -
whether it moved past twice `gHiliteDistance`, whether it is a tap (down
under 10 ticks and within `gMaxTapSize`), whether it can be the second
of a double tap (the last was a tap within `gDoubleTapInterval` = 27
ticks) - and notes the event in the stroke's word at +0x48: 4 a hilite
click (held still 45 ticks, or 90 within twice the distance), 5 a
tap-and-drag (a press within `gDoubleTapDistance` of the last tap while
a double tap was still possible), and at the pen-up 3 a double tap or 2
a tap; 1 once nothing is left to decide.  The distances are 4, 6 and 6
points scaled to the screen's resolution (`SetupDistances` 0x001fc7f8:
the gestalt's dpi; 6, 8, 8 pixels at 100 dpi; the halfwords at
0x0c101d28, 0x0c101d2c, 0x0c101d30 - `CheckHiliteState` reads them with
rotated unaligned `ldr`s).  `NukeEgregiousStrokes` disposes strokes
nobody took for ten seconds; `UnbufferStroke` 0x001fd474 takes a stroke
out of the queue when its unit disposes it.

`StrokeCentral` (`gStrokeWorld`, 0x44 bytes: +0 whether a stroke is
current, +4 the stroke, +8 its click unit, +0xc/+0x10 the last down and
up times, +0x14/+0x18 the block count and idles, +0x1c the last flush
time, +0x20 the deferred strokes, +0x28 the expired strokes, +0x2c the
next compress time, +0x34 the compress group) is idled from the
application (`IdleStrokes` 0x001463cc, not re-entered).
`StrokeCentral::IdleStrokes` 0x0014640c: while the controller is not
busy and no stroke is current, `StrokeGet`; the stroke made current
(`StartNewStroke` notes the previous stroke's times in it at +0x3c/+0x40)
and a `TClickUnit` made of it in `gRootDomain`, flagged 0x4000000, given
to `TController::NewClassification` - and handed to `HandleUnit` at once
when `IsExternallyArbitrated`; then `IdleCurrentStroke` keeps the
click's box at the stroke's, a click event in the stroke (unless the
click was claimed) becomes a `TClickEventUnit` over the click, cleared
from the stroke (`ClearEvent`) and classified; a done stroke is
journalled, `DoneCurrentStroke`d and `TriggerRecognition` called.
`FlushStrokes` 0x0014664c throws the queued strokes away (each a click
whose ink is taken off), `BlockStrokes` holds them for ten idles,
`BeforeLastFlush` tells whether a time precedes the last flush.

## The unit handler

`HandleUnit(TArray*)` 0x0019f964 runs `HandleUnitList` 0x0019f9e8 under
an exception handler.  For each unit: the recogniser's command; dropped
when the recogniser is arbitrated (flag 2), the last unit went to a
flag-1 (word) recogniser (`gRecognition+0x1c`) and the click came within
30 ticks of the previous stroke's pen-up (a tap after writing) - a click
on a clicks-only area noting `gRecognition+0x38`; `SetNextClick` with
the unit's start; dropped too outside the modal bounds
(`ModalRecognitionOK`) or when `TRootView::DoCaretClick` 0x001b7774 takes
a click on the caret.  Unless the unit started before the last flush,
the recogniser's `HandleUnit` answers the command and `PostAndDoCommand`
0x0019ff88 dispatches it: the view under the unit (`FindView` with the
recogniser's mask; nothing for none); a click outside the popup and its
parents closes the popup instead (result 1); else `MakeCommand(command,
view, unitPublic)` - aeRawInk and aeInkWord carry the strokes bundle -
through `TApplication::DispatchCommand`; the application's next undo
batch begins.  Afterwards (when the unit handled is still the one): a
command nobody handled, other than aeTap, with nothing noted leaves the
unit; otherwise `gRecognition+0x1c` becomes the recogniser's flag 1, and
- except for a click whose stroke is already over - the unit's ink is
taken off (`Cleanup`: `InkOff`, the stroke world's current stroke
forgotten), invalidated, and its strokes claimed
(`TController::MarkUnits`, 0x40000000; so a click a view takes gets no
tap gesture after it).  `HandleExpiredStroke` 0x0019fd8c passes a stroke
no recogniser took to the stroke world's expired strokes (ink: NOT YET).

The click and stroke tracking loops - `FTrackHiliteX` 0x001ecaa8
(`:TrackHilite(unit)`: the view hilited while the stroke's last point is
inside it, `Wait(1)` between looks until `StrokeDone`), `DoCaretClick` -
run in the application task while the inker task fills the stroke.

## The unit natives

`UnitFromRef` 0x001ec718 turns a script's unit argument (an address ref
of a `TUnitPublic`) back, `StrokeFromRef` 0x001ec750 gives its stroke
face.  `GetPoint(which, unit)` 0x001a5d60: 0/1 the first point's x/y, 4/5
the last's, 6/8 the first/last as a `{x, y}` frame (a clone of
`canonicalPoint`); `GetPointsArray` 0x001a21d0 answers `[y0, x0, y1, x1,
...]` (the tablet's order - `GetPointsArrayXY` 0x001a25d0 gives x first);
`StrokeDone`, `StrokeBounds` (the unit's bounds as a bounds frame),
`CountUnitStrokes`, `GetUnitStartTime`/`EndTime` (the unit's),
`GetUnitDownTime`/`UpTime` (the stroke's), `InkOn`, `InkOff` (the ink taken
off, invalidating), `InkOffUnHobbled`, `GestureType` (`CaretType`).

## Reconstruction (`src/recognition/`)

`RecObject.h` (TRecObject, TArray, TDArray, the recogniser's handle
glue), `Stroke.h` (TStroke, TStrokePublic, the Fixed rectangle helpers),
`Unit.h` (TUnit, TUnitList, TTypeList, TSIUnit, TStrokeUnit, TClickUnit,
TClickEventUnit), `UnitPublic.h` (TUnitPublic; UnitFromRef,
StrokeFromRef, `RegisterUnitNatives` for UnitNatives.cpp), `Areas.h`
(TRecArea's layout and use counts, TAreaList), `Domain.h` (TDomain),
`Recognizer.h` (TRecognizer, TRecognizerList, the click and event
recognisers, TRecognitionManager: `gRecognition.Init(1)` installs the
two click recognisers and the root domain; the unit handler's
HandleUnit/HandleUnitList/PostAndDoCommand), `TabletBuffer.h` (the ring
and its reader), `StrokeQueue.h` (the queue, TSStroke, the click-event
watcher, the tablet glue), `StrokeCentral.h` (the stroke world:
`gStrokeWorld.Init()`, `IdleStrokes()`).  The host's tablet is
`hal/host/HostTablet.h`: pen records into the buffer at once or queued
a record per tick of a `Wait` (its wait hook stands in for the inker
task, reading the buffer into the stroke queue in `StrokeTime`).
Tests: `test_RecObject`, `test_Stroke`, `test_Unit` (the units, the
buffer and the queue's click events), `test_Views` `TestClicks` (a tap's
aeClick and aeTap to a view's scripts, a tracked drag, a double tap).

DEVIATIONS on the host: `StrokeTime` (nothing in the ROM: the inker task
reads the tablet) reads the tablet buffer into the stroke queue, and the
inker's read index simply follows the writer (no ink is drawn); with no
controller, `StrokeCentral::IdleStrokes` hands every click and
click-event unit straight to `HandleUnit` and disposes the units itself;
`TUnitPublic::EndTime` uses the unit's own end (the controller's stroke
unit is NOT YET); `Wait` with no task running runs the host wait hook
(os600/user/UserBoot.h).

An area's two association lists are `TTypeAssoc` (ROM 0x00229f30-
0x0022a1e0, `recognition/Areas.h`): a `TDArray` of `Assoc` records - a
unit type, the domain that handles it, and the parameter block to handle it
with - sorted by type, so that merging two areas' lists keeps the order.
`AddAssoc` is therefore a search and an insert rather than an append, and
an association that is already there answers its own index rather than
being added twice; two entries count as the same when the type, the domain
and the two words beside them all match.  `IDispose` frees the parameter
blocks that belong to the entries - telling the domain first
(`DomainParameter` with selector 3) - and leaves alone any marked as
someone else's.  (`test_Areas`.)

## The ink

`TStroke::Draw` (0x00222af8) is what puts ink on the screen: it walks the
stroke's samples and draws the segment between each pair with `InkerLine`
(0x002f7c64, `qd/Draw.h`), which writes straight into the screen's pixel
map - no port, no pen, no clipping region, because the inker runs on its
own task at the tablet's rate.  The nib is a square whose size is the
second byte of the stroke's flags, and whose top left follows the line, so
a segment is the parallelogram the nib sweeps out.  Segments whose two
samples round to the same pixel are skipped, which is most of them while
the pen is still.

`kStrokeDrawn` and `kStrokeDrawnWhenDone` say what has been drawn;
`StrokeUpdate` puts back the queued strokes that something has drawn over.
A stroke marked `kStrokeNoInk` - a tap, or a view that does not want ink -
is marked and left.

On the machine the inker task inks each tablet sample as it converts it
(`TInker::LCDEntry` 0x0021781c, `DrawInk` 0x0021765c).  DEVIATION: the
host has no `TInker`, so `StrokeTime` (`recognition/StrokeQueue.h`) draws
the stroke that is being written once the points have been read into it.
The ink is ORed in, so a segment drawn twice is the segment, and what
lands on the screen is the same.

NOT YET: `InkerOff` (0x00140dcc), which is what takes the ink off again
when the recogniser has turned the strokes into something - so a stroke
drawn on the host's Notepad stays where it was drawn.

## The recognition configuration (`recognition/RecConfig.h`)

Between a view's `viewFlags` and the recognisers stands a configuration
frame.  `BuildRecConfig(view, flags)` (ROM 0x00034b70) builds it, and
everything that wants to know what may be written where goes through it -
`TextOrInkWordsEnabled` (which is what lets a tap on a blank page open a
caret), `ViewAllowsInk`, and the areas the controller makes.

Three things go into it:

- **the view's recognition bits.**  `vAnythingAllowed` (0x01fffe00) is
  every one of them; a view that has them all is a page, and a view that
  has only some is a field - `vDateField`, `vNameField`, `vNumbersAllowed`
  and the rest.
- **`vars.userConfiguration`,** which is what the Handwriting Recognition
  preferences were set to.
- **the view's own,** a `recConfig` in its context or a `_recogSettings`
  it carries.

The starting point depends on which kind of view it is.  A page is read
against the user's preferences (`Rrcprefsconfig`), and its input mask is
*built* from them by `BuildInputMask` (0x0019d1f8) - words, letters,
numbers, punctuation and shapes, each a bit, and punctuation only when one
of the others is on.  A field starts from `Rrcnorecog` and keeps the mask
its own flags make, so a date field reads dates whatever the preferences
say.  `vars.userConfiguration.testConfig`, which the handwriting test
screens set and which is nil in ordinary use, replaces the preferences for
a page when it is there.

Nothing is copied.  `PrepRecConfig` (0x00035298) clones `protoRecConfig`
and hangs the configuration off it as `_proto` with the preferences as
`_parent`, so the chain is read through rather than flattened and a
preference changed afterwards is seen straight away.  A view with
`_recogSettings` of its own has them expanded by the NewtonScript
`ExpandSettings` and put in between.

`CountCustomDictionaries` (0x0013f9c0) is worth a look for the shape of
its test: a view that allows everything has no custom dictionaries
whatever its `dictionaries` slot says, because `vAnythingAllowed` is
checked before the `vCustomDictionaries` bit is.

Reconstructed in `src/recognition/RecConfig.h`; `test_RecConfig` builds
the chains against the ROM's own prototypes with the preferences made by
hand.

## The tablet's calibration

The four points the tablet's coordinates are mapped through belong to
the inker task, and a script reaches them by sending it a 'newt/'inkr
RPC: 0x16 reads them (`GetCalibration` 0x0013fda4, a 0x14-byte
'calibration binary), 0x17 writes them back (`SetCalibration`
0x0013fcb8, which ignores a binary of any other length), and 5 runs the
calibration itself - the Setup assistant's "tap the targets" page
(`CalibrateInker` 0x00141098, which gives the RPC the sleep time, at
most ten minutes, as its timeout so the machine does not fall asleep in
the middle).  When it worked, `gInkerCalibrated` is set and the ROM's
`savecalibration` block puts the new calibration in the system soup
under "Calibration"; `CalibrateTablet` 0x001411dc then dirties the whole
root view and answers nil, or the error as an integer.
`IsTabletCalibrationNeeded` 0x0014132c asks the tablet hardware
(`CheckTabletHWCalibration` 0x0014121c) and answers nil when there is no
inker port to ask.

DEVIATION: `TInker` is NOT YET, so the host has no inker port and no
tablet of its own - its pen is already in the display's coordinates
(`hal/host/HostTablet.h`).  Reading and setting the calibration answer
as a machine whose inker kept quiet would, and calibrating answers that
it worked; `savecalibration` then finds no calibration to read and saves
nothing.  That is what lets the Setup assistant go past its calibration
page.

## Is this a word? (`recognition/Words.h`)

Before the system keeps a word - one the recogniser read, or one typed
into a field - it asks `ValidateWord` 0x0008ed50 about it.  That cuts it
down to 63 characters, strips the punctuation off both ends
(`StripRecognitionWord` 0x0008eb8c, which takes the diacriticals off
first unless the word recogniser wrote it), notes how it is capitalised
(`CheckCapAttributes` 0x0008ec34: 0x80 its first letter is a capital,
0x40 the whole word is), looks it up in the dictionaries
(`LookupWordOrVariant`) and answers a word of bits.

The bit the callers want is 0x80.  It is set when the dictionaries
already have the word, when they have it under another capitalisation,
and when it is not a word at all: fewer than two characters, with spaces
in it (`HasSpaces` 0x00256460), without a Roman letter anywhere
(`HasChars` 0x002564d4 - it tests the two ASCII ranges, so a word of
nothing but accented letters has none), or with a character the word
recogniser does not write (`WRecVerifyWordSymbols` 0x001444c8, NOT YET).
`LookupWord` 0x0008ef38 is that question with the answer thrown away: a
copy of the word, stripped, when 0x80 is clear, and nil when it is set -
"is this a new word?", which is what the Setup assistant asks of each
part of the name that is typed into it.

The punctuation set is `!"'(),.:;?` and the four curly quotes, in that
order, and the search gives up once it has gone past.  A closing bracket
or a curly right single quote after an `s` is not punctuation, so that a
possessive the recogniser wrote keeps its quote and `(s)` keeps its
bracket; the plain apostrophe is punctuation wherever it stands, and
survives in the middle of a word only because the stripping works from
the ends inwards.

DEVIATION: with no dictionaries nothing is ever found, so every
well-formed word comes back as one they do not have - which is what a
machine whose user dictionary is empty says about a person's name
anyway.

## The dictionaries (`recognition/Words.h`)

The word sources are frames in `vars.dictionaries`, each with a `dictID`
of its own; `GetDictionary(id)` (`FindDictionaryFrame` 0x0013e558) walks
the list and answers the one that matches.  `InitDictionaries`
0x0013de2c builds the list out of the ROM's own descriptors
(`Rdictionarylist`, 27 of them): each is wrapped in a clone of
`canonicalDictRAMFrame` whose `_proto` is the descriptor, and its `dict`
slot gets the `TDictionary` the ROM built for it - out of the ROM's word
data for most, empty (`NewDictionary`) for the three a user writes into:
31 the user dictionary, 35 the expand dictionary, 36.

The three a user writes into - 31 the user dictionary, 35 the expand
dictionary and 36 - start empty (`NewDictionary`); the rest are built
out of the ROM's own word data.

### The ROM's word data (`recognition/ROMDictionaryData.h`)

The ROM carries 129 lexicons - the letters, the word lists (general,
locale, shorthand, US), the days and months, the phone, date, money,
number, postcode and symbol lexicons, and a great many empty ones -
each beginning with a big-endian size word and then an Airus trie.  The
`gLex8*` ones are AL dictionaries, the `gEnum8*` ones are the
enumerated kind; nothing else is needed to read them.

They are reached through `gROMDictionaryData` (ROM 0x0c106840), a table
of 129 pointers that lives in RAM and is written by
`InitROMDictionaryData` (ROM 0x0019b0d4).  A dictionary descriptor's
`romDictID` is the slot of that table its data is in;
`GetROMDictionaryData` 0x0019b078 reads the size word, steps over it and
answers the trie, which is what `BuildDictionaryFromPtr` opens a
dictionary over - the data is never copied, on the machine or here.

The table is not in the ROM to be read: `InitROMDictionaryData` is a
straight line of `ldr`/`str` pairs, one per lexicon, so it has to be
recovered from the code that writes it.  That is
`tools/newton-rom/analysis/romdicts.py`, and
`recognition/ROMDictionaryTable.cpp` is what it wrote.

DEVIATION: the addresses the ROM stores are the machine's own, where the
ROM is simply there to be read.  The host takes them as offsets into the
image the frames were imported from (`frames/ROMImport.h`), so a host
that has imported no image gets no dictionaries at all - which is the
same as a machine whose lexicons could not be built.

`InitDictionaries` 0x0013de2c then puts it all together: the ROM's list
of 27 descriptors is cloned, each wrapped in a clone of
`canonicalDictRAMFrame`, and a dictionary opened for it - empty for the
three a user writes into, and over the ROM's word data for the rest, but
only for the kinds a lookup walks (`dictType` under 2, or 4).
`gDictList` is built alongside, one entry per frame.  NOT YET: `gTrie`,
which would be dictionary 32 if that descriptor had no `romDictID` (it
has one, so this ROM never takes that path), and the four lexicons the
locale carries as binaries, which come in through `ReadRefDictionary`.

## The chains a lookup walks (`recognition/Dictionaries.h`)

`vars.dictionaries` is the list as a script sees it; `gDictList` (ROM
0x0c10162c) runs alongside it, one `dictListEntry` of eight bytes per
frame, holding the opened Airus dictionary itself, which frame of the
list it belongs to, whether anything was opened for it, and whether it
has been switched off.

`FindDictionaryEntry` 0x0013d4ac is how an id becomes one of those
entries, and it does two things worth knowing.  A dozen ids stand for
others - 13 and 0x29 are the names dictionary; 1, 7, 9, 0x14 and 0x2a
the general lexicon; 2 is 3; 10 is 0x2d; 0x2b and 0x2c are 0x1a - and
the substitution is written out as a `switch` in the code rather than
kept in the frames, so a lexicon that was folded into another one still
answers under its old number.  Then an id that names nothing at all
falls back on whatever the list calls 6, the general lexicon: asking
for a dictionary that is not there gets you the ordinary words rather
than nothing.

A lookup does not walk that list.  It walks a **chain** - a
`TDictChain` (ROM 0x0020cab0-0x0020cc40), a `TDArray` of dictionary
Handles with one thing of its own, how far along it the walk has got -
and there are three of them, picked by the frame's `dictType`: 0 the
ordinary lexicons, 1 the ones consulted only for particular fields, and
4 the exceptions, which is chain 2.  A frame whose `dictType` is
anything else is in no chain at all.

`BuildChains` 0x0013d808 fills the three in from a recognition
configuration: the dictionaries it names by hand first, so that a
field's own dictionary is asked before the general ones; then every
dictionary of the list whose `domainType` overlaps the configuration's
`inputmask` (in the bits 0x1fff000); and then the symbols dictionary
(0x28) unless `inhibitSymbolsDictionary` says to leave it out.  Each of
them goes in through `AddToChain` 0x0013d628, which then follows the
frame's `linkedDictID` to the next dictionary and the next, for up to a
hundred links or until the ring closes back on the one it started from
- which is how asking for the general lexicon gets you the names
dictionary with it.  A dictionary already in the chain is not added
twice.  `BuildChains` 0x0013d9dc is the same for whatever is being
written on now: the recognition configuration of the view the caret is
in, or - when there is none, or it takes ink rather than words - a
clone of the ROM's own `rcbuildchains`, whose mask is 0x1000.  A chain
is built for one lookup and thrown away again (`DoneChains`).

`LookupWordInChain` 0x0013f430 asks each dictionary of a chain in turn
and stops at the first that says anything better than "no"; "the
beginning of other words" is not an answer, because what is being asked
is whether the word is a word.  `LookupWord` 0x0013f4f4 is what
everything else calls: it builds the chains, brings the word down to
eight-bit characters, and asks the ordinary chain and then the
exceptions.  Both answer the `dictID` the word was found in, or -1, and
leave behind the attribute that was stored beside it.

`LookupWordOrVariant` 0x0013f570 tries the word's capitalisations as
well, walking `BuildCaseVariant` 0x0013f2fc up from nought until it
says there are no more (sixteen is as far as it goes).  Each variant is
built on top of the one before it rather than on the original, so the
sequence for "HELLO" is "HELLO", "hello", "Hello", "hEllo" - the last
of which is not a capitalisation anyone would write, and is looked up
like any other.

DEVIATION: `AddToChain` copies the seven bytes of a list entry that
matter, because the `FindDictionaryEntry` that follows may move the
list out from under it; here a Handle is pointer-sized, so the whole
entry is copied instead.

### What the locale has to say

The list of dictionaries is the ROM's whatever language the machine is
set to; what changes is the data behind them.  A descriptor's
`localDictSlot` names a slot of the locale bundle, and what is in that
slot is either an integer - a slot of the ROM's own word data - or a
binary of words the locale brought with it.  `ReplaceLocalDictionary`
0x0013e384 works out which, and the two `ReplaceDictionary`s (0x0013ec74
over a binary, 0x0013f14c over the ROM's data) dispose of the dictionary
that was open and put the new one in its place, in the list entry and in
the frame's `dict` slot.  A dictionary already open on those very bytes
is left alone.  `ReadDictPrefs` 0x0013e4a4 does that for every
dictionary of the list, and `ReadCursiveOptions` calls it, so a change
of locale changes the words the machine reads.

`ReadRefDictionary` 0x0002d6a0 is what opens a dictionary over a binary
object - a locale's words, a store's, a package's - and
`DisposDictionary` 0x0002d6f4 gives one back.  `GetScriptDictRef` uses
the first, which is how a dictionary frame whose `dict` slot holds the
words themselves rather than an address is reached.

The locale also carries four lexicons that are in no list at all - the
time, date, phone and number ones - which `InitDictionaries` opens into
`gTimeLexDictionary` and its three neighbours for the lexical analysis
to use.

ROM BUG (fixed): `ReadDictPrefs` does not check that there is a list.
`TRecognitionManager::Init` builds one only above level 1 but calls
`InitRecognizers` - and so `ReadDomainOptions`, and so this - at every
level, so a machine started at level 1 throws on a `vars.dictionaries`
that was never made.  The MP2x00 always starts at level 2, so nobody
ever saw it; the tests that do start at level 1 put an empty list there
(which `ExpandWord` still wants).  Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour): with no list
it reads nothing.

## What the machine keeps (`recognition/Learning.h`)

Three of the dictionaries are the writer's own and start empty: 31 the
user dictionary, 35 the expand dictionary and 36 the auto-add one.  A
word goes into one of them through `AddWordWithCount` 0x001aab74, which
keeps the dictionary frame's `count` in step and refuses to go past its
`limit` (a hundred) - `airusResult` comes back -15, which is what the
Prefs slip turns into "the dictionary is full".

The expand dictionary is a dictionary and an array together: what the
Airus trie stores beside a word is not the expansion but the *index* of
the expansion in the frame's `list`.  `ExpandWord` 0x001aa930 puts the
pieces back: `CollectPunctSymbols` 0x001aa680 takes the punctuation off
both ends and hands the two runs back, `GetExpandIndex` 0x001aa600 asks
the dictionary, and what comes out is the leading punctuation, the
expansion and the trailing punctuation - with a capital carried over
when the abbreviation had one and the expansion does not.  Only the
first letter is lowered before the lookup, so "Asap" expands and "ASAP"
does not.

`Capitalized` 0x001aa8ec is how the capital is noticed, and it is worth
a look: the ROM reads the first *two* characters as one word, lowers the
first, compares the top halfword of what it read with the top halfword
of what is there now - the first character, the machine being
big-endian - and puts the two bytes back.  Written out, it is "was the
first letter a capital, and leave the word alone".

`LastWordSame` 0x001aae08 is the one-word memory the auto-add dictionary
keeps in its frame's `last` slot: the same word written twice running is
added only once, and a different word in between lets it be offered
again.

### Adding a word on the writer's behalf

`AddAutoAdd` 0x001aaee4 is what the corrector calls when a word the
writer has settled on goes onto the page (`AutoAdd` in
`recognition/CorrectInfo.h`).  It is only done when the writer has asked
for it (`doAutoAdd`), only for a word the handwriting recogniser read
(`gWordID` is `'WREC'`), only for a word the auto-add dictionary was not
offered last time, and only for a well-formed word the dictionaries do
not already have.  The word then goes into two dictionaries: the
auto-add one plain, which is the machine's record of what it did, and
the user dictionary encoded, which is what the recogniser reads against.
Every twentieth word the `autoAdd` view is told, which is what puts up
the slip offering to show the writer what has been learnt.

ROM BUG (fixed): the answer is set to true before the second add is
checked, so a word whose user-dictionary entry failed - and which is
therefore taken straight out of the auto-add dictionary again - is still
reported as added.  Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour): the answer is
whether the second add worked.

`RemoveAutoAdd` 0x001ab0f8 takes it back out of both, which is what
happens when the entry the word was learnt from goes; both go through
`DeleteWordWithCount` 0x001aacdc, the counting other half of
`AddWordWithCount`.  `DoIndexedLearning` 0x001a0cc4 is the other side of
learning: what the writer settled on handed to the recogniser of that
unit type, so that it reads the same writing better next time.


`test_Dictionaries` builds a list of two AL dictionaries by hand and
checks the id substitutions and the fallback, the three chains, the link
from one dictionary to another, the custom dictionaries a configuration
names, and the words found in them; then it runs `InitDictionaries` and
`ReadDictPrefs` for real against the U.S. locale bundle, looks real
words up in what comes out, and puts an expansion in and takes it out
again.

## The Airus engine (`recognition/Airus.h`)

A dictionary is a trie in a byte stream, kept in a Handle, with an
`AirusAParmBlock` of 0x58 bytes in front of it: where the data is, how
big it is, how far it has been filled, the word being looked at, and
where the last walk got to.  The block lives behind a Handle of its own
and is what everything outside calls a dictionary; the engine works on
one at a time, through the global `AE_Parms`.

The data begins with two bytes saying what it is: `'a'`, then a byte
whose low three bits are the kind, bit 3 "lock the Handle while a walker
runs", and high four the size of the attribute each word carries.  The
kind picks the family of walkers: `AL` and `AL16` for the lexicons built
into the ROM, `AEnum` for the ones the machine writes, which is what
the user's words go in.  Everything goes through `CallAirusA`
0x0002d41c with one of ten selectors - StartA, ExitA, Verify, AddWord,
DeleteWord, FirstLast, NextPrevious, ChangeAttribute, NextSet,
NextSet9.

### A node

```
character | flags | sibling offset | attribute | children... | sibling
```

The flags byte carries the size class of the sibling offset in bits 7-6,
"no children" in bit 5, "an attribute follows" in bit 4, and the top
nibble of the offset in bits 3-0.  The offset is counted from the end of
the attribute and reaches over every child, which is what makes it the
way to the next node of the same row.  Two generated tables decode it
(`AirusTables.cpp`, from the initialised read-write data): the masks
`{0xffffffff, 0xf, 0xfff, 0xffffffff}` and the node sizes `{2, 2, 3,
5}`.  The offset itself is counted in nibbles - one, three or seven -
the first of them in the flag byte.

### Looking up

`VerifyStart` 0x0002c760 puts every dictionary of a chain back to its
beginning; `VerifyString` 0x0002cd20 then follows a whole word down,
through `AE8_Verify` 0x0002b048, and leaves `airusResult` saying what it
is: 1 the beginning of other words and not one itself, 2 both, 3 a word
with nothing going on from it, -6 nothing begins that way.  It hands
back pointers to the word's attribute and to the character that would
come next when only one would.  The walk can also be taken one character
at a time, which is what the recogniser does as it reads.

### Writing

`AddWord` 0x0002c48c puts a word in.  `FindInsertionPoint` 0x00029630
follows it down for as long as it is already there and says where the
rest goes - before a node that sorts after it, as the first child of a
word that ended there until now, or past everything the last row leads
to.  `PutWord` 0x00029360 writes what is left as a row of single nodes,
the last carrying the attribute.  Making room moves everything after it,
so `FixupPointers` 0x000294a8 then makes every sibling offset that
reached over that place reach over it still - each may need more bytes,
which moves things again, so what has grown is carried along the stack
of nodes `FindInsertionPoint` put aside on the way down.

### Deleting

`DeleteWord` 0x0002c56c takes a word out, through `AEnum_DeleteWord`
0x00029e3c.  A word that other words go on from keeps its nodes and
loses only its attribute - it stops being a word without stopping being
a path.  A word nothing goes on from has its nodes taken out as well:
`FindDeletionPoint` (0x00029c88, which the ROM keeps no symbol for)
follows the word down leaving its ancestors on the same stack
`FindInsertionPoint` uses, and the walk then goes back up that stack as
far as the first ancestor that is a word itself or has another child.
The bytes between are slid away, `FixupPointers` with the other
operation *shrinks* every sibling offset that reached over them (a node
with no sibling is left alone: what went was under it, not after it),
and the Handle is given a growth unit back whenever the data has shrunk
enough to spare one.

ROM BUG (fixed): a word whose last node carries no attribute - a path that is
not a word - returns without setting the block's result, which
`DeleteWord` had just set to 0, so deleting a word that was never there
is reported as success.  Deleting a word that begins nothing at all is
reported properly, as "not there".  Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour): a path that is not a word is "not there" too.

`test_Airus` builds a six-word dictionary, takes the words out one at a
time in the three shapes above and puts one back, checking after each
that the others are still found with the attributes they went in with.

### Reading a dictionary the other way

`Verify` answers "is this a word".  The other question is "what
characters may follow what I have so far", which is one row of the
trie: `AEnum_NextSet` 0x0002afd0 over `AE8_NextSet9` 0x0002a9f4 walks
the children of a node and hands each one to a callback, and the
callback `AEnum_NextSet` uses (`AE8_NextSetCB` 0x0002af38) simply
writes the characters out into the block's word buffer.  They come out
sorted, because the row is.

ROM BUG (fixed): `AE8_NextSet9` assembles the attribute it hands the callback
from its bytes low one first, where `PutAttr` writes it and `GetAttr`
reads it high one first.  A one-byte attribute - which is what every
dictionary the machine writes has - is the same either way, so nobody
ever saw it; a two- or four-byte one comes out of that call
byte-reversed.  Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour).

Walking a whole dictionary is the two questions in a recursion.
`WalkDictionary` 0x0002e0f0 sets up a `DictWalkBlock` - the dictionary,
a count, the callback and the word the walk is standing on - and
`A8_PrefixCompletions` 0x0002d73c looks that word up: a prefix of other
words is followed on down, a word is reported, a word that is also a
prefix is both, and nothing at all stops that branch.  Following down is
`A8_WalkNextChars` 0x0002d890, which takes the one character that may
follow when `fSymbol` says there is only one, and otherwise asks for the
whole set and tries each in turn.  The callback is given the word, its
attribute, the one character that could follow it and how many words
have come so far, and answers whether to go on; a nil callback walks it
all the same and only counts, which is how `DeleteWordWithCount` puts a
count right that has drifted to nothing.

`DeletePrefix` 0x0002c60c is the same idea on the writing side: it is
`DeleteWord` with the block's result set to 1 rather than 0 going in,
which is the flag `AEnum_DeleteWord` reads as "take the whole row out"
rather than "take this word out" - so the mode is carried *in* the field
that carries the answer back out.

A script reaches all of this through the dictionary frame: `Walk(prefix,
fn)` (`FAirusWalkDictionary` 0x0008f44c, which writes the four things
into one array and calls the function with it each time - so a script
that wants to keep a word has to copy it), `PrivateDeleteWord(word)` and
`DeletePrefix(word)`.

`test_Airus` walks the six-word dictionary it built, in whole and under
a prefix, stops the walk from the callback, counts without one, and
takes a prefix out.

### What a script sees

A dictionary frame's `dict` slot holds the engine's dictionary by
address (`GetScriptDictRef` 0x0008ea78).  `FAirusNew` 0x0008ee98 makes
one, `FAirusLookupWord` 0x0008fb28 looks a word up and fills in the
`attribute` and `terminalClass` of the frame it is handed, and
`FAirusAddWord` 0x0008fc3c adds one.  That is the chain the Setup
assistant's Continue button runs down when a name has been typed in, and
with it the assistant goes on to its next page.

`ChangeAttribute(word, attribute)` writes a word's attribute over where
it lies (`AEnum_ChangeAttribute` 0x0002a7cc): the word is walked from the
root a character at a time — across a row's siblings to find the
character, then down to that node's children for the next — and the new
bytes replace the old ones in place, so nothing moves and the dictionary
does not grow. It writes the bytes out by hand and has arms for the
sizes 1, 2 and 4 only, so a dictionary with a *three*-byte attribute is
left exactly as it was and the call still says it worked.
`AttributeSize()` is `AttributeLength` (0x0002d37c): the size the
dictionary declares, or — for the oldest writable kind, which had no
place to declare one — 1.

`Register()` and `Unregister()` (0x0013eddc, 0x0013ef2c) put a frame's
dictionary into `vars.dictionaries` and `gDictList` beside it with an id
of its own (they are handed out from 200 up, well clear of the ROM's
own), and take it out again — every entry after it having its index
moved down one. Both end in `DictionariesChanged` (0x0013f084), which
tells the Assistant's line to look for the custom dictionaries again and
throws the recognition areas' cache away, because an area remembers the
chain of dictionaries it was built with. `Dispose()` gives the
dictionary back and takes the frame's `dict` slot away.

### The cursor (`recognition/AirusIterator.h`)

`WalkDictionary` runs through a dictionary from end to end and calls
back for each word. A **cursor** does the opposite: it *stands* on one
word and is asked for the next or the previous, so a script can stop,
look and carry on. That is harder than it sounds, because a dictionary
is a trie — there is no "next word" to step to, only a shape to walk,
and the walk has to be kept somewhere between calls.

`TAirusIterator` (0x0002e260) keeps it in a stack of `charState`s, one
per character of the word it stands on. Each state holds

* the trie nodes reached at that character — up to **four** of them,
  because a lookup may be running down a chain of dictionaries at once,
  and because two characters that sort the same (a letter and its
  capital) are followed as one; and
* once it is asked for, the sorted set of characters that may come next
  (`GetNextChars` 0x0002def0, one `AEnum_NextSet` per position,
  `InsertNewNextChar` merging them in `SortOrder`).

`SortOrder` (0x0002de9c) is the Unicode collation with case ignored, so
the set is in the order the words come out in.

Stepping forward (`VerifyNextChar` 0x0002e594) takes the next of those
characters, pushes a state for it, writes the character into the running
word and asks the engine to verify it; every character that sorts the
same goes into that one state as another position. Stepping back
(`VerifyPrevChar`) is the mirror. Running off either end pops the state
(`PopState`) and carries on in the one below; running off the bottom is
the end of the dictionary, and the word is cleared to say so.

A word is found when one of the positions answers **1** (a prefix that
carries an attribute) or **2** (a leaf). `ConstructResult` (0x0002dbe4)
then copies the running word out and walks the stack back down writing
the characters of the path that actually answered over it — because the
running word holds whichever branch was taken last, and the answer may
lie along another of the parallel ones — and verifies the whole word
once more to read its attribute and the character that could follow.

`Reset` (0x0002e38c) starts the walk: `BuildStateAtPrefix` puts one
state at the end of a prefix already verified, and `BuildStateUpToPrefix`
walks from the root to where the prefix would be, stopping at the first
character that is not actually there — which leaves the cursor just
before it, so the next step answers the first word from there on.

A script reaches all of this through a `protoDictionaryCursor` frame:
`AllocateCursor()` (`FAirusIteratorMake` 0x0008f4e8) makes one and
remembers it in the dictionary's `cursors` array, `PrivateReset(word,
exact, which)` puts it somewhere, `PrivateEntry(frame)` fills in the
`word`, `attribute` and `terminalClass`, `Next()`/`prev()` step it and
`PrivateDispose()` gives it back. `PrivateClone` is a muddle — see
`docs/curiosities.md`.

`AddDictionary(frame, custom)` is `Register()` for a frame that is not
the receiver, and `GetDictionaryData(id)` / `SetDictionaryData(id,
binary)` take a dictionary's bytes out as a `'dictdata` binary and put
them back — which is how one travels to a soup or to the desktop. Only
a dictionary in RAM may be asked: a ROM one is read where it lies and
its Handle holds no bytes of its own, which is what the kind byte's
"lock the Handle" bit distinguishes.

## What is left of the engine

Three things, and each of them is wanted by something that is itself not
reconstructed:

* **the sixteen-bit walkers** — `AE16_Verify` (0x0002b2cc),
  `AE16_NextSet9`, `AE16_NextSetCB`, the two-byte-character mirrors of
  the AE8 ones. Reading the kind byte of all 129 lexicons built into
  this ROM gives kinds 1 and 7 only, so **no dictionary in the MP2x00 US
  ROM is sixteen-bit**; they are there for a localisation that needs
  them. `AEnum_Verify` answers "no match" for a sixteen-bit dictionary
  until they are written.
* **the completions walk** — `AEnum_FirstLast` (0x0002a1f4) and
  `AEnum_NextPrevious` (0x0002a244), selectors 5 and 6, which step
  through a dictionary a word at a time *without* a cursor object; the
  block's `fResult` carries the mode in rather than the answer out.
  Their only callers are `FirstCompletion` (0x0002cf0c) and
  `NextCompletion` (0x0002d224), and those in turn are used only by
  `DynaCompress` (the Assistant's dynamic dictionary) and by
  `ConvertDictionaryData`.
* **the random word generator** — `RandomCommonWord` (0x0013e640) over
  `GetDistributedWord`, `InitLetterPairs` and the `charWeights` table,
  which walks the trie choosing a weighted character at each step until
  it lands on a word. `GetRandomDictionaryWord` is its only native.

Both dictionary natives that waited on these are answered now.
`ConvertDictionaryData` (0x0008f06c, `Dictionaries.cpp`) brings an old
dictionary's data up to date in place: it walks the words of one copy with
the completions walk and, for every word whose attribute still carries the
old capitals flags (0x40 the whole word, 0x80 its first letter), deletes it
from a second copy and adds it back in capitals with the flags off
(`DecodeRecognitionWord`, 0x0008ecdc/0x0008ecf8: `UppercaseText` over the
word turned into Unicode and back), then copies the second copy's bytes
into the binary.

## The controller (`recognition/Controller.h`)

Between the stroke world and the recognisers stands `TController`
(`gController`, ROM 0x00209e84-0x0020c7a0).  It holds two lists and one
queue and works them in four passes.

A **piece** is something the domains may build on; a **unit** is what a
domain has built.  The stroke world offers each click it makes to
`NewClassification`, which puts it on the *piece* list, asks the
hit-test routine which areas it lies in, and - out of the area's
`fDomains` associations - queues one **group entry** per domain that
takes pieces of that type.  Out of the area's `fTypes` associations it
also enters the piece with the arbiter, unless the type's arbitrate time
is 2: that is `IsExternallyArbitrated`, the mark that says "handle this
at once", and it is what lets a click reach its view while the pen is
still down.

- **Group** (`DoGroup`) offers every queued entry to its domain.
  `TStrokeDomain::Group` keeps the click's box and duration up to date
  while the pen writes and answers 0, so the entry stays queued and is
  offered again next time; once the stroke is finished it makes a
  `'STRK'` unit with the click as its only sub, gives it to `NewGroup`,
  and answers 1 so the entry is dropped.  An entry whose piece has gone
  (`CleanGroupQ` empties it rather than removing it) or has been claimed
  is dropped too.
- **Classify** (`DoClassify`) hands every unit to the domain that made
  it.  A domain's `Classify` is `NewClassification` again, so the unit
  becomes a piece in its turn and the tree grows: a stroke is a word's
  piece and a word a sentence's.  A unit that is still delayed is left
  where it is and the pass asked for again at the time its delay runs
  out.  Units that have been handed on are taken off the unit list at
  the end of the pass.
- **Arbitrate** (`DoArbitration`) is the arbiter's (below).
- **Clean up** (`CleanUp`) compacts the three.

`Idle` runs whichever passes are due and answers how many milliseconds
to wait before the next (`NextIdleTime` asks without running anything;
`TriggerRecognition` makes all four due at once).  With no pass due and
a click still being written the answer is "nothing to wait for" - the
pen itself will wake the recogniser.  With no pass due, no click and
pieces still on the list, something has gone wrong: the controller
signals a memory error, which throws everything away
(`CleanupAfterError`) and puts the click in hand back as a new piece.

`NoEventsWithinDelay` is what decides whether a delayed unit's time is
really up.  Anything written in the same view inside the delay is an
event and the unit waits; a click that is still being written is not an
event by itself, but once its stroke has more than fifty points the
domain is asked (`PreGroup`) whether it would take it, and if it would,
the unit waits for it properly.

`Initialize` numbers the domains by how far their type is from the
strokes - the stroke domain is 2, what takes strokes is 3, and so on -
by walking outwards over the piece types.

NOT YET: `RecognizeInArea` (re-recognising the strokes of an area),
`UpdateInk`, `BuildGTypes`, and the debugging state
(`SaveRecognitionState`, `RestoreRecognitionState`).

## The areas a piece is written in (`recognition/Areas.h`)

An **area** is what a view looks like to the recogniser.  The chain from
one to the other is:

    view + viewFlags -> BuildRecConfig -> a configuration frame with an
    inputMask -> SetUpArea -> the types the recognisers take ->
    BuildGTypes -> the domains those types need -> ConfigureArea

`SetUpArea` asks every installed recogniser, in turn, whether it wants
anything written here: `TRecognizer::EnableArea` (0x00143818) looks at
the configuration's `inputMask`, and if any of the services the
recogniser provides is in it, adds its unit type to the area's `fTypes`
with `TRecArea::AddAType` (0x0021c74c) - together with the routine the
winning units are answered through (`gRecognition.fUnitHandler`, which
is `HandleUnit`) and the arbitrate time the recogniser was installed
with.  A type added with arbitrate time 1 is counted in `fArbitrateNow`;
arbitrate time 2 means the type is not arbitrated at all, which is how a
click reaches its view while the pen is still down.

`TController::BuildGTypes` (0x0021c7cc) then turns those types into the
`fDomains` list - the domains the area must actually *run*.  A type is
made by the domain of that type, and that domain needs its piece types,
which need the domains that make *those*, and so on: each round looks up
the domains of the types found last time and writes their piece types
(paired with the domain that wants them) into `fDomains`, until a round
finds nothing new.  The area's `fMaxLevel` is the furthest any of those
domains stood from the strokes (`TController::Initialize`), which is how
many rounds of arbitration it will take.

Areas are cached, because the next stroke in the same field would
otherwise build the same one again: `gAreaCache` holds an area, the
input mask it was built for and when it was last used, and
`FindMatchingArea` (0x00035674) answers the cached one for a view and
mask or builds a new one with `MakeArea`.  Every look also ages the
cache - a line untouched for ten seconds is let go, which is what makes
a handwriting preference changed while nothing is being written take
effect - and `PurgeAreaCache` throws the lot away when a script has
changed something the areas were built from.

`GetAreasHit` (0x00036bc8) is the routine the controller hit-tests with.
It finds the view under the piece that takes what it is
(`TUnitPublic::FindView` over the recogniser's required mask) and that
view's area for its input mask.  A view that takes no writing at all
answers an input mask of zero and gets no area, so the piece is nobody's
and the controller claims it; a click there also closes any popup that
was open, which is how tapping outside a menu dismisses it.  The whole
thing runs under an exception handler, so an `evt.ex` out of a view's
scripts is reported rather than thrown at the recogniser.

`TRecognitionManager::Init` (0x0019e124) is where all of this is put
together: the stroke world, the area cache, the controller and the
arbiter are made, the controller is told to hit-test with `GetAreasHit`
and to hand an unwanted stroke to `HandleExpiredStroke`, the recognisers
are installed (the click-event, stroke and click ones at level 1) and
the domains are numbered.

The ROM starts it at **level 2** - clicks and strokes, and the shapes and
words above them - and that is what the host does too, because level 2 is
also what builds the dictionaries (`InitDictionaries`).  The Setup
assistant asks for one as soon as a name has been typed: its Continue
button runs `AddWordsToDict` -> `AddWord` -> `SetUpDictionary` ->
`GetDictionary(31)`, which takes the `Length` of `vars.dictionaries` and
throws if the list was never built.  The shape recogniser of level 2 is
installed before the word one, as the ROM does.

## The arbiter (`recognition/Arbiter.h`)

`TArbiter` (`gArbiter`) decides between the units the domains have built
over the same strokes.  A piece whose area arbitrates its type is entered
on the arbiter's *pending* list when it is classified - one `BestMatch`
per type the area takes it as, carrying the unit and the area's
association for it (the recogniser, its parameters, the handler and the
arbitrate time).

`DoArbitration` takes each pending entry in turn:

- a type the area arbitrates **at once** - or the only such type the area
  has - simply wins and goes straight to the area's handler.  An invalid
  unit is thrown away instead.
- anything else is **held**.  It may still decide early (`ArbitrateEarly`:
  a scrub over a single stroke that nothing else was written with), and
  otherwise it joins the *active* list and waits.
- `WaitingForOtherUnits` is the wait: the first active unit that has
  reached the area's own level (`fMaxLevel`, what `BuildGTypes` worked
  out) is the one a gather is run from, and if that gather does not cover
  every stroke, something is still to come.
- `GatherUnits` is the covering test, and the neat part of the design.
  The strokes under the unit are put in a sorted set with a count beside
  each - how many more units must still cover it, one for each ring of
  domains between the stroke and the top.  Every active entry whose
  stroke range meets the set is taken in, its own strokes folded into the
  set (`UnionStrokes`, which may widen it - and does when a word covers
  strokes the first unit did not), and the rounds go on until every count
  has run down to zero or nothing new is taken in.
- `ArbitrateUnits` then picks.  The rule comes from
  `GetRecognitionCase(area)` - the number of scrub types the area takes,
  plus 2 for shapes and 4 for words: with a scrub among them nothing is
  chosen between (`ArbitrateWithScrubs`: everything that is not a scrub
  wins), with shapes and words together the two are weighed against each
  other (`ArbitrateGraphicsWords`, NOT YET), and otherwise the lowest
  score wins (`GetBestInterpretation`).

The winners go to the area's handler - `HandleUnit`, which posts the
command to the view under the unit - with the controller marked busy, so
that nothing the view does in answer is taken for writing.  Then the
winners that were arbitrated at once, and are not scrubs, are marked
claimed and invalid, and `CleanUp` takes them out: a claimed unit that
was not invalidated has its subs offered to the domains again (the
strokes of a word that lost may still make something else), and a claimed
stroke piece that was marked invalid goes to the expire routine, which is
what leaves it on the screen as ink.  A click the pen is still writing is
never touched.

One ROM bug is kept: the last loop of `DoArbitration` walks the gather and
tests each entry's flags, but marks the unit in hand rather than the
entry's own - the register holding it is never reloaded.  Marking the same
unit twice does no harm, and the entries that should have been marked are
left for the round after.  Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour): each entry's own unit is marked.

## What happens when the pen goes down

With all of that in place the path from the tablet to a view's script is
the ROM's own:

1. the inker's strokes reach `StrokeCentral::IdleStrokes`, which makes a
   `TClickUnit` of each and gives it to `TController::NewClassification`;
2. the hit test builds (or finds) the area of the view under it, and the
   area says which domains run over a click and which types are
   arbitrated how;
3. `'CLIK'` is arbitrated externally, so the click goes to `HandleUnit`
   at once, while the pen is still down - that is what lets a button
   hilite under the finger and a slider follow it;
4. a tap or double tap noted in the stroke becomes a `TClickEventUnit`,
   which is *not* externally arbitrated: it waits for the arbitration,
   which is run from `TRecognitionManager::Idle` out of the application's
   idle loop, and reaches the view as `aeTap`;
5. when the stroke finishes, `TStrokeDomain::Group` makes the `'STRK'`
   unit, `DoClassify` offers it upwards, and (in a view that asks for raw
   strokes) the stroke recogniser answers it with `aeStroke`.

A view that takes no writing at all gets an input mask of zero and so no
area: the piece is nobody's, the controller claims it, and it never
reaches a recogniser - which also means the click view of the tap before
is left as it was.

## The gesture domain

`TEdgeListDomain` (`'SCRB'`, `src/recognition/EdgeList.h`) is the first
domain above the strokes and the only one every level of recognition
has.  It groups each `'STRK'` piece on its own into a `TEdgeListUnit`
and then classifies it by shape, in three steps:

1. **the corners.**  `FindCorners` runs the stroke's sample points
   through a recursive splitter (0x0020e3f8, no debug symbol).  It takes
   the chord from the first point to the last, makes two axes from it -
   one along, one across, each scaled to a twelfth of the chord - and
   measures every point in between against both, keeping how far each
   run has got and where it turned back.  When a point goes further back
   than the last turn did, the axis is *flipped* and the search starts
   again the other way, which is what lets a stroke that doubles back be
   split at its own extremes rather than only at its furthest point from
   the chord.  Whichever axis swings widest, if it swings wider than
   four pixels, gives the split, and the two halves are done again.

2. **the tidying.**  `Collapse2` drops corners within seven units of one
   another (keeping whichever of the two turns more sharply) and then
   corners whose two edges differ by less than about nine degrees.

3. **the tests**, tried in order and stopping at the first that
   recognises anything:

   | test | corners | what it wants | label |
   | --- | --- | --- | --- |
   | `TestLine` | 2 | nothing else | 4 |
   | `TestCarets` | 3 or 4 | the arms within a factor of two of each other and meeting at 100 degrees or less (or one of two looser cases) | 2, 3, 5, 6 |
   | `TestScrub` | 5 to 39 | turns that alternate, at least three of them sharper than 110 degrees, and all pointing within half a turn of each other | 1 |

`TScrubRecognizer::HandleUnit` turns the label into the command:
`aeScrub` for 1, `aeCaret` for 2, 3, 5 and 6, `aeLine` for 4.  Before
it does, it drops the gesture altogether if the stroke came within half
a second of the one before it while the last thing handled went to the
word recogniser (somebody is writing, not gesturing), waits out the
fifth of a second after the pen came up in which another stroke could
still arrive, and answers `aeTap` instead when the gesture's bounds turn
out to be tiny.  Each of those commands runs the view's
`viewGestureScript(unit, kind)`.

One ROM bug is kept, in `TestCarets`: when one arm is more than twice
the length of the other it splits the longer one so that the two match,
and passes `Interpolate` the *ratio* of the two lengths where
`Interpolate` wants a distance along the line.  The new corner therefore
lands all but on top of the joint instead of an arm's length down the
second arm.  It is fixed by default now (the first arm's length is
passed); `NEWTON_ROM_BUGS=1` brings the ROM's behaviour back.

A scrub now reaches the text.  The command goes to the view the stroke
was written in, which on a page is the `TEditView`, not the paragraph:
`TEditView::Scrub` asks the selection first (`ScrubHilite`), and
otherwise asks every child what it would take out
(`TParagraphView::HandleScrub` with -1 and nothing done), takes the
largest answer and asks the children that answered to do that one thing.
A child left with nothing (answer 5) is removed from the page as an
`aeRemoveData` command.  Then the scrub's own ink comes off and the hole
puffs away.  `TParagraphView::HandleScrub` empties the paragraph when the
scrub covers more than seventy per cent of it, and otherwise deletes the
lines it covers (`ScrubLines`: more than sixty per cent of a line's box).

`TParagraphView::ScrubWords` is what takes out less than a whole line.
It finds the line the scrub is on, takes the scrub's left and right edges
at that line's middle and asks each for the word boundary it is nearest
(`PointToWordBoundary`, biased towards the word's start on the left and
its end on the right), and what lies between them is the candidate.
Whether it goes depends on how much of it the scrub covers:

| the candidate | what the scrub must do |
| --- | --- |
| two or more characters on one line | span more than half of it |
| two ends on different lines | nothing: it goes |
| nothing (the ends met) | contain, or be contained by, one character (`ScrubCharacter`) - and a scrub wider than five pixels may only claim a character if it was drawn with seven corners or more |
| a word whose box it only overlaps | cover sixty per cent of the word |
| white space | cover ninety per cent of it - or, for a single space, the same five-pixels-or-seven-corners rule |

The corner count is `CountGesturePoints`, which is simply how many
corners the gesture domain's polyline came down to: a real to-and-fro
has several, a flick across a letter has two or three.  It is what stops
a stray stroke taking a character with it.

Taking out the last of the text answers 5 rather than 2, which is what
tells the page to remove the view; `RemoveText` widens the range it is
given to take a neighbouring space, so scrubbing a word in a sentence
takes the space after it too.

(host: the ROM does all of this over its text objects - `PointToWord`
asks the object under the point for its own text and runs `FindWordBreaks`
over it, `OffsetInRunToBounds` and `CharBounds` measure characters within
a run, and `ScrubCharacter` goes through `ReplaceCharacter` with an empty
replacement string just to find the character a rectangle picks out.  The
text objects are NOT YET, so the line cache and the paragraph's own
measuring stand in for them; the decisions above are the ROM's.)

## The correction information (`recognition/CorrectInfo.h`)

A word info frame says what the recogniser made of one piece of writing.
The *correction information* is the list of them: `vars.correctInfo`, a
frame whose `info` array holds one word info per word that has gone onto
a page, each carrying where it went - the view's id, and the `start` and
`stop` character offsets.  That is what lets a word be corrected long
after it was written: the corrector asks `FindWordInfo` for the word at
a character offset and gets back the readings the recogniser had to
choose between.

It is a list of forty (`InitCorrection`, 0x00076bf0, which sets `max`),
and a frame only goes on it when its first reading is a *single* word -
`AddWordInfo` (0x000770cc) scans the reading with `ScanWordEnd` and
drops anything that ends before the string does, because the corrector
has nothing to offer for "one two".

A word info in the list is not always the recogniser's.
`MakeWordInfo(view, offset, length)` (0x0007824c) makes one for a
stretch of text nobody wrote - typed, or pasted: the writing at the
offset when there is any (`GetStrokesAt`, 0x00078960, which opens an ink
word back out into a stroke bundle), and otherwise the characters
themselves as its single reading.  So the corrector can be asked about
typed text too.

The rest is small and mechanical: `MakeWordInterp` (0x00078548,
0x00078598) for one reading, `SetWordList` (0x00078340) to replace them
all with plain words, `SetOffsetInfo` (0x00078438) for where the word
went, `GetNthEntry`/`GetNthWord` (0x00077c64, 0x00077ce0) to read them
back, `UnitID` (0x00077be4) for the four characters of the unit's type,
and `TestWordInfoFlags`/`ClearWordInfoFlags` (0x00077d34, 0x00077e30).
`AutoRemove` (0x0007959c) takes a word the machine added to the
dictionary on an entry's account back out again when the entry goes,
through `RemoveAutoAdd` (`recognition/Learning.h`); the flag is cleared
whether or not there was a word to take out, as the ROM clears it.

### Keeping up with the text

The offsets are into a paragraph's text, so every edit has to be
answered here or the corrector would offer a word's alternatives for
whatever now sits at those offsets.  `TParagraphView::HandleReplaceText`
is where it happens, and it does three things.

`OffsetCorrectionInfo` (0x000762c0) answers the change itself.  An entry
that straddles the edited range goes - its word no longer means
anything - one entirely after it has both its offsets moved along, and
one before it is left alone.  The one case that *gains* is a backspace
(one character out, nothing in), which may have closed the gap between
two words: the entry ending where the character was and the one
beginning just after it are merged back into one (`MergeWordInfo`,
0x00076dc0), their words run together (`MergeWords`) and their writing
joined (`MergeStrokes`).  That last only happens when *both* entries
carry writing; two typed words joined by a backspace simply lose the
second entry.

`ExtractRange` (0x00077190) and `InsertRange` (0x00077378) are the undo.
Before a deletion the entries covering the range are copied out and
rebased to the start of it, and they ride in the undo command's frame
parameter as its `correctInfo`; carrying the undo out puts them back
(moved to wherever the text is going) with `InsertRange`.  So undoing a
deletion restores not just the words but their alternatives.

The rest: `ClearCorrectionRange` (0x000765bc) takes every entry
overlapping a range off without moving what is left, for when the range
is about to be something else; `RemoveCorrectionInfo` (0x00076830)
takes a whole view's entries, which `TParagraphView::SetValue` does when
the text is replaced wholesale; `DeletedCorrectionInfo` (0x00076758)
gives back the dictionary words a view's entries were responsible for;
`ClearEmptyEntries` (0x00078d04) drops entries that hold nothing the
recogniser actually proposed (fewer than three readings, all of them
index -1 or -2 - the word as written and the same word capitalised);
and `GetWordInfo` (0x00076f58) finds the entry for a range or makes one.

`TRootView::Constructor` calls `InitCorrection` to start the list, as
the ROM does.  (DEVIATION: the machine's starter globals come with a
`correctInfo` frame; a host that has not run the ROM's boot block has
none, so `InitCorrection` makes one.)

### Learning from the list

The list is not only a record.  It is forty deep, and a word falling off
the end of it is the machine's last chance to learn from what the writer
did with it: `DoOverflowLearning` (0x000793d0) drops the empty entries
first, then hands the oldest to `DoEntryLearning` (0x00077ea0) and takes
it off.  An entry only has something to teach when it kept the engine's
training data (flag 1, `unitData`) *and* the reading that went onto the
page is one the recogniser actually proposed - a word the machine made
up out of the letters (index -1 or -2) tells it nothing.  Afterwards the
training data goes, because it has been used.

`AddWordInfo(view, start, stop, unit)` (0x00079790) is what the edit
view calls at the end of `TEditView::HandleWord` once a recognised word
has gone onto a page: room made, the unit's own frame put on the list
saying where it landed, and the word offered to the dictionary
(`AutoAdd`, 0x000794ec, unless the view says `_noAutoAdd`).  That is
what makes the list live - before it, nothing ever went on.

The dictionary's own side of all of it - `AddAutoAdd`,
`RemoveAutoAdd` and `DoIndexedLearning` - is `recognition/Learning.h`
and `recognition/Recognizer.h`.

## The spelling checker (`recognition/Spelling.h`)

A thing of its own sitting on the dictionaries.  It knows nothing about
the recogniser, and the recogniser reaches it only through the
NewtonScript functions the ROM gives it - `SpellDocBegin`, `SpellCheck`,
`SpellCorrect`, `SpellSkip`, `SpellDocEnd` and the learn/unlearn pair -
so that seam is where a modern checker would go in, and it is where the
file boundary is drawn.

### A session

`SpellDocBegin` 0x001f6360 makes a `spell_state` and answers the frame a
script holds it by (`MakeSpellFrame` 0x001f62ac; `GetSpeller` 0x001f6314
goes back the other way).  It builds two sets of chains out of the
machine's dictionaries as they stand: `InitSpellChains` 0x001f1b74 takes
every dictionary whose `domainType` says it holds words (0x1000, less
the ones whose bottom bit marks them as not to be offered), and
`InitNumberChains` 0x001f40a4 takes the ones that hold numbers, dates,
times and money (0x1c2000).  The second set is empty until `ReadDictPrefs`
has run: the date, time, phone and money dictionaries have no words of
their own until the locale gives them some.  A session also holds an
empty dictionary for the words it is told to skip, given the *user*
dictionary's id so that a skipped word looks to the rest of the checker
like a word the writer added.  `SpellDocEnd` 0x001f63f8 gives it all
back and writes the user dictionary out when anything was learnt.

The words are eight-bit throughout: the checker works in Mac Roman, and
the curly right single quote a paragraph writes is turned into a plain
apostrophe on the way in (`FixQuotes` 0x001f5d50) and back on the way out
(`RestoreQuotes` 0x001f5da0), because the dictionaries hold the plain
one.

### Is it spelled right?

`SpellCheck` 0x001f41bc takes the word apart - the punctuation off both
ends (`CollectPunctSymbols`), a possessive off the end
(`CollectContractions` 0x001aa810), the capitalisation noted and taken
off - and looks it up three ways: as it stands, with a capital first
letter, and in capitals.  The first that answers decides, and what comes
back says how what was written differs from what was found: nil when
nothing does, true when no spelling of it is a word, 128 when the
dictionaries hold it only with a capital, 192 when only in capitals.  A
word of one character, a word with a digit in it that the number
dictionaries know (`CheckNumbers` 0x001f54e4), and a word with anything
but letters and apostrophes in it (`CheckSymbols` 0x001f4cdc) are all
left alone.

Under it, `ValidateWord` 0x001f4dc8 asks one dictionary, `ValidateWord2`
0x001f4e48 asks it again with the first letter's case turned over, and
`ValidateWordInChain` 0x001f4bcc walks the session's chain - asking the
words the session was told to skip first.

### What might it have been?

`SpellCorrect` 0x001f44c8 answers up to seven spellings, nearest first.
`CorrectWordInChain` 0x001f49ac asks each dictionary of the chain in
turn, and `CheckWord` 0x001f4aac puts the word through five kinds of
change against it:

- **transpositions** (0x001f4f90): each neighbouring pair swapped.  Two
  pairs at once (0x001f5068) is only tried when nothing else was found.
- **deletions** (0x001f5170): each character dropped.
- **insertions** (0x001f5248): a character put in at each place.
- **splits** (0x001f52f0): the word cut in two, when both halves are
  words.
- **substitutions** (0x001f546c): each character read as another.

The first, second and fourth make a candidate and look it up.  The other
two do not try one letter at a time: they put a `?` where the change
goes and hand the word to `DoWord` 0x001f55ac, which walks the
dictionary and a *map* together.

A map is a sorted list of (what is written, what it might stand for)
pairs.  `wc_map` says what a character may be read as and what `?` may
stand for - any letter, at a cost of seven, in frequency order;
`substitution_map` holds 181 letter groups that are written for one
another, which is the phonetic heart of the thing: `a` may stand for
`ai`, `ay`, `eigh`, `ough` and two dozen more, each at its own cost.
Both live in the initialised RAM area as tables of pointers, so
`tools/newton-rom/analysis/spellmaps.py` follows them and writes
`SpellMaps.cpp`.

`DoWord` brackets the word with `$` and `@` (so a map entry can say what
may stand at the beginning and the end) and then walks a stack of
`word_state`s, one per piece of the word matched so far.  Each state
finds the map entries whose pattern the word goes on with
(`FindList`/`GetNextList` 0x001f5a1c, 0x001f5994 - the map is sorted, so
a pattern that sorts past the word ends the search), and for each of them
every spelling it allows (`GetCurrentElement` 0x001f5b14).  A spelling
whose first character the trie cannot take is passed over, which is what
keeps the walk from trying everything everywhere: the set of characters
available at a node comes from `AEnum_NextSet`
(`GetNextCharacters` 0x001f5570).  A state that reaches the end of the
word on a node that is a word reports what it built; six edits is as far
as it will stray, and 48 states as deep as it will go.

The guesses are kept in order of what they cost (`InsertGuess`
0x001f5f54 over `FindGuess`/`DeleteGuess`), and what a generated
candidate costs is `MeasureDistance` 0x001f612c plus a charge for the
kind of change - two for a transposition, three for a deletion, ten for
a double transposition.  `MeasureDistance` is not an edit distance: it
counts the characters each spelling has that the other has none of,
takes the greater, adds the difference in length, and adds five more
when the two are not the same word.  `ScoreGuess` 0x001f6250 is the
other kind of cost - what the map charged at each state of a walk.

`RestorePunctSymbols` 0x001f4828 puts back on each guess what was taken
off the word: the two runs round it and the capitalisation it was
written with.

`test_Spelling` runs it against the ROM's own lexicons: "wrod" gives
"word", "helllo" gives "hello", "notebok" gives "notebook", "recieve"
gives "receive" and "seperate" gives "separate", and what was written
round the word comes back round the guesses.

NOT YET: the learn/unlearn pair, which are the ROM's own scripts rather
than natives.  (`SpellSkip` 0x001f651c is done: a word the session is to
stop complaining about goes into the session's own dictionary with the
capitalisation it was written in, and goes when the session does.)

## Deferred recognition (`views/Rerecognize.h`)

Writing that is already somewhere - an ink word in a paragraph, an ink
shape on a page, a stroke bundle or an ink word a script holds - can be
read again, now, rather than as the pen writes it.  Everything goes
through one controller call, `TController::RecognizeInArea` (0x0020a1d8):

- the strokes are made into stroke units (`MakeStrokeUnit`) as if written
  one after another in the last second - from just after the last lot
  it read (`gLastWordEndTime`), or a second ago if that was longer ago -
  and classified as the pen's are;
- the controller's hit test is swapped for `SpecialGetAreasHit`, so every
  piece is in the one area the caller built, and its expired-stroke
  routine for `SpecialExpireStroke`; every type of the area that has no
  handler is given `SpecialHandler`, which counts the winner's strokes
  done and hands the unit to the caller's own handler with the caller's
  argument;
- with the controller flagged internal every pass runs whenever it is
  idled, and it is idled until as many strokes are done (read, or
  expired) as were given.

The area is `MakeRerecognizeArea` (0x00035bc4): built from the
configuration given, or `rcRerecognizeConfig`, with the recognition
manager's unit handler cleared first so that its types come out with
none - which is what lets RecognizeInArea give them `SpecialHandler`.
A view with no configuration of its own is read with
`BuildRecConfigForDeferred` (0x00034cec): its own `recConfig` (or the one
its flags give it) asking for text and only text.

The three callers and their handlers:

| caller | handler | what a word read becomes |
|---|---|---|
| a paragraph's command 0x19 (`RerecognizeWord` over the paragraph) | `ParagraphViewWordHandler` | put in place of the ink word through the insert-items path (`DoInsertItems`); the command's `stop` answers the length that went in.  A word nobody could read goes back as its word info where the paragraph keeps ink words, and as nothing where it does not |
| an ink shape's command 0x19 (`RerecognizeWord` over the polygon view) | `PolygonWordHandler` | a word on the page (aeWord17) and the shape removed (aeRemoveData) |
| `Recognize(strokes, config, together)` (`RecognizeStrokes`) | `BulkUnitHandler` | a word info added to a fresh correct info frame, which is the answer; a stroke nobody read goes to a stroke world of its own (`gBulkStrokes`) to be grouped into ink |

A paragraph's command 0x1a (`RecognizePara`) finds every ink word in the
range first - with the box it is drawn in when the line cache covers it,
for the arrow `DrawCheckmark` draws over what is being read - then sends
each a command 0x19 of its own, moving the later offsets by how much
longer each replacement was; the command's `stop` answers where the range
ends now, and the paragraph is told the range changed once, at the end.
A double tap on an ink word inside the selection sends 0x1a for the
selection; a double tap on an ink word the corrector has no readings for
sends 0x19 for that word.  `RecognizeInkWord` wraps one ink word in an
ink shape as wide and tall as the word, expands it to a stroke bundle
and reads it (answering the first word info's words - or, a quirk kept,
the empty info array when nothing came back as a word);
`RecognizeTextInStyles` replaces every ink word run of a text-and-styles
frame by its first reading, in the font the runs before it last named.

`src/host/demo/recognize.ns` writes "ton", then reads the same strokes
through all four (ctest `host.NewtonRecognize`): `Recognize` and
`RecognizeInkWord` read "ton", `RecognizeTextInStyles` turns "an" and the
ink word into "an ton", and an ink word put after "ton" in the paragraph
is read by `RecognizePara` into "ton ton".

Strokes that are not words are grouped into ink (next section) and
handed to `HandleBulkStrokes`, which puts them in the correct info as a
word info with no words - and `AddWordInfo` keeps only word infos with a
word, so they are dropped and `Recognize` answers nothing for them.  That
is the ROM's own behaviour (the disassembly of both is plain), kept as a
quirk; the demo checks it.  NOT YET RECONSTRUCTED: the polygon view's
0x32 (the double tap's reading of its ink) and 0x44 (`TPolygonView::RealDoCommand`
answers 0x19, and 0x43 and 0x4b for its selection - `docs/views/README.md`); the paragraph's
`ProcessStyles` and `FixupDropData`, the other callers of
`RecognizePara`/`RecognizeTextInStyles`.  A ROM bug kept (latent):
`RecognizeTextInStyles` reads the text through a pointer taken before the
ink words are read, which allocates.

## Strokes nobody read, grouped into ink (`recognition/InkGroups.h`, `WordSegment.h`)

A stroke no recogniser claims is *expired*: `HandleExpiredStroke` hands it
to the stroke world (`StrokeCentral::AddExpiredStroke`, unless the arbiter
is waiting on units not yet made, when its ink is simply taken off), which
holds it and calls `IGGroupAndCompressStrokes`.  That keeps a *group* of
the expired strokes (`GroupDataStruct`: the units, and a 256-bit list of
those still to be placed) and gives each new one, as a *trace*, to
**ParaGraph's word segmenter** - the same library as the cursive
recogniser, which uses it with word descriptors of its own; the ink
grouping calls it without.

The segmenter (`WordStrokes`) keeps the line being written in a
0x18cc-byte state block.  Its heart is a histogram of the line along x,
a byte per four trace units (half a pixel): each stroke is laid in slanted
by the writing's slope (`WS_HistTheStroke` walks the pen's path in steps
of an eighth of the line height, a steep step near the line adding more,
and the bottom of each downstroke adding a peak), the columns of its
*core* are marked, and `WS_CalcGaps` makes a *gap* of each run of
unmarked columns.  `WS_SegmentWords` asks of each gap whether it is the
space between two words, and the one asked is a **little net**
(`NeuroNetWS`/`Rget_answer`): eleven measurements of the gap and the line,
turned through an 11x11 matrix for each of two classes and scored against
120 trained Gaussian cells each, the two likelihoods' difference times
five being how sure it is (the tables, `rom_matrix`, `rom_cell`,
`rom_ncells`, `EXP_TABL`, are `WordSegmentTables.cpp`, generated by
`romtable.py`).  The line height, letter width and pitch, word distance
and slope run from stroke to stroke and are averaged into the next line
(`InitForNewLine`) and, over the last four lines, learnt (`WS_FlyLearn`);
`WS_NewLine` decides when a stroke has started a new line, which closes
the old one.  The slope is learnt from the steep steps, **a step down
counting eight times a step up** (`WS_GetStrokeBoxAndSlope`).

`IGCompressStrokes` takes each word the segmenter has settled - its line
closed, more words waiting than the `lineAtATime` preference asks for, or
the end - out of the group and hands its strokes (forty at most) to
`StrokeCentral::IGCompressGroup`; `CompressGroup` takes their ink off and
`ExpireGroup` hands them on: as an aeInkWord or aeRawInk command to the
view under them (`ExpireUsingCommand`, which also warns the writer, once a
day, when the recogniser ran out of memory), or to the stroke world's
`fExpireProc` as a stroke bundle (`Recognize`'s `HandleBulkStrokes`).
`ExpireAll`, half a second after the last stroke (`IdleCompress`),
settles whatever is waiting.  ROM quirks kept: `IGCompressStrokes`
counts the waiting words down once per stroke rather than per word, and
never hands over the strokes gathered for a word whose last stroke was
already placed; `WordLineStrokes` replaces an emptied word with the one
after it alone.  `recognize.ns` writes "ton" twice on a view that reads
nothing, and its `viewRawInkScript` is given two pieces of ink of four
strokes each (ctest `host.NewtonRecognize`).  The word descriptors the
cursive recogniser calls the same layer with are in "The way writing
reaches the cursive reader" below.

## The corrector (`recognition/CorrectInfo.h`, `views/ParagraphView.h`)

A second tap on a word asks for it to be corrected.  The command travels
a little way before it gets to the word:

1. The click-event recogniser makes a `kDoubleTapClick` and turns it
   into `aeDoubleTap`, but only when both taps were on the same view
   (`TEventRecognizer::HandleUnit`).
2. `PostAndDoCommand` sends it to the view the *area* belongs to, which
   for a page of the Notepad is the edit view, not the paragraph.
3. `TEditView::RealDoCommand` walks its children backwards - topmost
   first - and offers the command to the first whose bounds hold the
   point.  That is how the paragraph gets it.
4. `TParagraphView::RealDoCommand` finds the word under the point
   (`FindWordOffset` 0x00177cbc over `PointToWord`, then
   `ScanWordStart`/`ScanWordEnd` to take in the whole of it), widens it
   to whatever the corrector already remembers about it
   (`FindWordInfo`), works out the box it occupies
   (`OffsetToBounds` at both ends) and calls `Correct` 0x0007929c.  A
   tap with no word under it puts the caret there and offers the
   numeric keypad (`OpenKeypadFor` 0x000791f8) instead.

`Correct` is the seam: everything above it is C++, and everything below
is the ROM's own NewtonScript.  It hands the view's context, the word's
offset and length and its bounds to the global `DoCorrection`, which
makes the corrector view out of magic pointer 30, asks
`correctInfo:FindNew` for what the machine remembers about that word and
`wordInfo:GetWords` for the readings, and opens it - unless the view's
`viewCorrectionPopupScript` says not to.

The natives that script uses are the other half of this:

- `FFindNewInfo` 0x00079a44 (`FindNew`) answers the entry for the word,
  but only when it covers exactly that range: a word that has been
  edited since is no longer the word the corrector was told about, so
  the range is cleared and a fresh entry made from what is there now.
- `FGetWordList` 0x00079c94 (`GetWords`) is the readings as plain words.
- `AddCapitalizedEntry` 0x00077988 (`AddCapitalized`) puts the first
  reading's other capitalisation in front of it and then the reading
  itself back in front of that, so the corrector offers the word as it
  stands and then the same word capitalised the other way.
  `GetToggledWord` 0x000790f0 is what "the other way" means: a word in
  capitals comes back in lower case, and anything else has its first
  letter turned over.
- `RemoveToggledEntries` 0x000778b8 (`RemoveAddedEntries`) takes those
  back out again.  A reading the recogniser proposed carries the index
  it had in the recogniser's own list; one the corrector added carries
  -2, and those are the ones that go.

The list operations they work with are the ROM's own
(`InsertArrayElement` 0x00078ea4, `RemoveArrayElement` 0x00078f38,
`MoveArrayElement` 0x00078fc4, which swaps two neighbours and otherwise
takes the element out and puts it back).

The spelling checker `DoCorrection` asks for the alternatives is the
section above; with it in place a double tap on a word of a Notepad page
opens the corrector, and picking one of its alternatives puts that word
on the page - which is what `src/host/demo/correct.ns` photographs.

Picking one goes back through the natives too: `MoveFirst` brings the
reading the writer chose to the front of the entry, `Learn` hands it to
the recogniser that read it, `AutoRemove` takes back out of the
dictionary whatever was added on the strength of the reading that was
there before, `GetStyleAtOffset` asks the paragraph what style the old
word had, and `HandleInsertItems` sends the paragraph the command it
answers for everything put into it from outside.  The rest of the
cluster - `AutoAdd`, the flag pair, `GetCorrectionWordInfo`,
`GetViewID`, `MergeStrokes`, `offset`, `FindWordInfo`,
`MergeWordInfo`, `SetWordList` and the two range natives - are a line
each over what is above them.

NOT YET of the natives: `FAddUnitInfo`, `FAddWordInfo`, `FExtractRange`,
`FInsertRange` and `FMoveCorrectionInfo`.

NOT YET: the two arms of the double tap that ask for a word of *writing*
to be read again rather than corrected, which want the re-recognition
path; `HitsHilitedInkWord` 0x00171344 is reconstructed but nothing takes
the branch that uses it yet.

## The caret gesture

A caret (`aeCaret`) goes to the page the same way a scrub does, and
`TEditView::HandleCaret` offers it to each visible child that holds data
with the gesture's kind (`TUnitPublic::CaretType`), the angle it points
at (`GestureAngle`, snapped to 0, 90, -90, 180 or 135) and the corners of
its polyline (`GesturePoint`): the first arm, the caret's own point, the
second arm, and a fourth for the kinds that have a tail.

`TParagraphView::HandleCaret` takes it when the point lies in the view
grown vertically by the height of the arms - a caret drawn just under a
line still belongs to it - and, for the ones pointing right, only when
the point is in the left margin (within ten pixels outside the view's
left edge or twenty inside it).  What it then does:

| kind | angle | what goes in |
| --- | --- | --- |
| 2, the plain caret | up | one space |
| 2 | right | a line break |
| 2 | down | the space between two words closed up |
| 3, with a tail | up | as many spaces as the tail is wide |
| 3 | right | as many line breaks as it is tall |
| 5, the open one | up | line breaks, unless the view is one line only |
| 6, the flat one | 135 | one space, in a one-line view, when both arms are short |

`InsertHorizontalSpace` is what puts the spaces in.  A width of -1 is
the single space; a real width is divided by the width of a space in the
style the text would be inserted in, so a wide tail gives a wide gap.
Line breaks step over the white space already at the point first, so the
break lands after it.

`InsertVerticalSpace` (0x001764c4) is the vertical half, and it works by
lines rather than by characters.  The line it opens is the first whose
midline is below the caret's point, so a point anywhere in a line's top
half picks that line; the point must also be no more than a quarter of a
line above the line before's baseline, which is what keeps a caret drawn
well clear of the text from splitting anything.  The carriage returns go
in at that line's start - the height rounded to lines, at least one, and
one more unless there is a return at the insertion point or just before
it, because a break in the middle of a line costs one return to make and
one to keep the line that was there.  The caret then follows the first
of them.

One ROM bug is kept and commented: a caret whose tail is narrower than a
space gives neither a space nor a break, and the ROM then inserts the
buffer it never filled - whatever was on the stack.  The host cannot
reproduce which bytes those are and will not put arbitrary text into a
note, so the buffer starts empty there and nothing goes in.

`CheckAndDoJoin` (0x00175964) is the join: a caret drawn upside down
across a line closes up the white space its two arms straddle.  The arms
have to be within fifteen pixels of each other vertically and within half
an ascent of the line's baseline - which is what keeps a caret drawn
between two lines from joining either - and both are taken to the
baseline before the characters under them are asked for, so a badly drawn
caret still picks the characters its arms cross.  From those two the
gesture works outwards (an arm on white space steps back one, an arm on
the last character steps forward one) and the first run of white space
between them goes, however long it is: a join over "one   two" takes all
three spaces, and one over a word takes nothing.  Two *ink* words - both
characters 0xf701 - are joined instead, all the way down to the strokes
and back (`MergeInk`).

`CheckAndDoSplitInk` (0x00176208) is the other thing a caret can do to
ink, and `InsertHorizontalSpace` tries it whenever the caret would open
no space at all.  One of the two characters the caret's offset lies
between has to be an ink word, and the caret's own x says which: to the
left of where that offset draws its caret the word before it is meant,
to the right the word after, and a caret exactly on the boundary picks
neither.  The word is cut at the caret's x (`SplitInkAt` with eight
pixels of slop), each half is brought back to the x-height the view
writes in, and the two go in where the one was through `DoInsertItems` -
so the cut is one thing to undo, and the halves come out spaced apart
like any other pair of items.  The white space after the word goes with
it when there is any, because the insert puts its own back.

## The line gesture

A line (`aeLine`, two corners and nothing between them) carries the slope
of those two corners, and a line gesture's angle is measured *from the
vertical*: `PtsToAngle` divides dx by dy, so a line drawn straight up is
0 and one drawn straight down is 180.  `ValidLineGesture` (0x000ab6dc)
takes only the four square ones - 0, 180, 90 and -90 - and
`TEditView::HandleLineGesture` (0x000ab674) offers the line, with its two
corners, to each visible child that holds data.

In a paragraph (`TParagraphView::HandleLineGesture` 0x00176bd4) it is the
case-change gesture, and only the two vertical angles mean anything: a
line drawn **up** through the selected text puts it in upper case, one
drawn **down** in lower case.  The paragraph must already have a
selection.

A line drawn upwards is turned round first, so either way the first
corner is the end at the top and the second the end at the bottom.  The
box of the two has to touch the view (grown six pixels to the left), and
then each selection in turn is asked whether the line spans it: the line
must begin at or above the selection's top and end at or below its
bottom, and be no taller than three selections plus the slack - fifty
pixels at the least, and twelve more each way when the whole paragraph is
selected, since a line drawn over everything need not be neat.

What changes case is either the whole selection or just its first
character: the first character alone when the line's middle is within six
pixels of that character's box.  So a line through the first letter of a
selected word capitalises the letter, and one through the middle of the
word capitalises the word.  The text goes back in through the same
aeReplaceText command any other edit uses, with the styles
`GetStylesOfRange` answers, and the range is selected again afterwards
(`HiliteText` 0x0016a490, three instructions that fall into `MakeHilite`
and throw away the flag they were given).

NOT YET: `ArbitrateGraphicsWords`, the inker and ink
(`StrokeUpdate`, the expired strokes' grouping and compression, the
stroke bundles), the word list and dictionaries, the tablet driver, the
journal, the caret popup.

## What a script is told about writing (`recognition/WordInfo.h`)

Everything NewtonScript learns about a piece of writing arrives in one
frame, the *word info* frame, cloned from the ROM's `protoWordInfo`
(0x00077fd8 `MakeWordInfo`) and hung off the unit's public face so that
it is made once:

  - `unitID` - the unit's four-character type as a string.
    `EncodeUnitID` (0x00077bb0) makes it by reading those four bytes as
    *two Unicode characters* rather than four ASCII ones, so 'STRK'
    becomes the two characters 0x5354 0x524B.  It is meaningless as
    text, unique per type, and costs nothing to make; the script side
    only ever compares one with another.
  - `words` - an array of `protoWordInterp` frames, one per reading,
    each with its `word`, `score`, `index` and `label`
    (`MakeWordList`, 0x000786b4, over the unit's `TWordList`).
  - `strokes` - a stroke bundle of the writing itself (`ExpandUnit`,
    0x001a2554: every stroke of every sub of the unit, in the order they
    were written, with the unit's own bounds).
  - `unitData` - what the recogniser would learn from this, but only
    when the writer has asked for the learning to be kept
    (`TUnitPublic::TrainingData`, `gSaveWordTrainingData`).
  - `flags` - what the system makes of it.  The one that matters is 8,
    "this is ink": the unit was never read at all, or the recogniser
    answers `kWRecInk` for it.  `MakeWordInfo` sets the flag *and*
    empties the `words` array, so a frame never offers a reading and ink
    at the same time.
  - `ink` - filled in later, by the view that keeps the writing.

Under it, `TUnitPublic::MakeWordList` (0x0022d268) turns the unit's
interpretations into the `TWordList`.  Only the unit type the word
recogniser in use makes (`gWordID`) has readings to gather; anything
else answers nothing.  The list is built in two passes, and that is what
orders it: the first takes every reading *except* an ordinary word the
dictionaries have never heard of, and the second takes exactly those -
so what the machine knows comes before what it is guessing at.  A
two-character reading ending in '.' or ')' is left alone, being an
abbreviation or a list marker rather than a word, and so is anything
that does not start with a letter.  Five readings are kept, and a word
that expands (an abbreviation) goes in ahead of the word itself.

A list that ends up with nothing at all gets one empty reading scoring
1000 - which is what a unit nobody could read comes to, and which is
what makes it ink.

`TUnitPublic::SetWordBase` (0x0022d764) remembers where the writing
stands, as a rectangle that is really a line: the "top" is the height of
the left end of the base line and the "bottom" the height of the right,
so writing running uphill can be laid out along its own slope.  A single
'?' or '!' is the exception - it has a descender the recogniser measures
the base from, so the base it answers is too low, and the bottom of the
unit's bounds is used for both ends instead.

`LookupWord` (0x0013f4f4) and `ExpandWord` (0x001aa930) are the
dictionaries' (`recognition/Dictionaries.h`, `recognition/Learning.h`):
a reading the dictionaries know is put ahead of one they do not, and a
reading that is an abbreviation has its expansion put in ahead of it.

## The shape domain (`recognition/ShapeDomain.h`, `ShapeGeometry.h`)

The shape domain ('GSHP', `TGeneralShapeDomain`) is what turns strokes
drawn on a page set to shapes into clean lines, boxes, triangles,
circles and ellipses.  Its units are `TGeneralShapeUnit`s, whose one
interpretation's label is the shape's *type*:

| type | shape | | type | shape |
|---|---|---|---|---|
| 0 | circle (params: centre, radius) | | 8 | line (angle: its direction) |
| 1 | ellipse (centre, two radii, angle) | | 9 | triangle |
| 2 | curve | | 10 | square (or rhombus) |
| 3 | nothing to make a shape of | | 11 | rectangle (or parallelogram) |
| 4 | closed polygon | | 12 | four sides, nothing to solve |
| 5 | open polyline | | 13 | arc |
| 6 | closed, with curves in it | | 15 | the classifier gave up |
| 7 | open, with curves in it (and a unit still being grouped) | | | |

A direction here is measured **from the vertical**: an angle near 0 or
180 is upright, near 90 level.

**Grouping** (`PreGroup`, `Group`, 0x00215fd4) joins strokes end to end
into one shape, and measures where its two loose ends touch the shapes
already on the page - the *context units*, which the domain asks the
view under the writing for with command 0x14 (`aeGetContextUnits`,
answered by `TEditView`) through `SetContextUnitRoutine`.  A
`ShapeEnd` records what each end met: a side, a corner, an open end, a
vertex of a closed shape, or a circle, and how far away it was.

**Classify** (0x002113f0) is three stages:

1. `FindKeyPoints` (`ShapeKeyPoints.cpp`): the outline cut into the
   corners and the curved pieces between them - a recursive line
   splitter (`RLineOut`), runs of close samples collapsed
   (`RSmallDists`, `Collapser`), cubic Hermite pieces fitted where the
   outline bends smoothly (`FindCubic1` and the `TV*` tangents), turned
   into conic control points (`DoConic`), and the pieces joined
   (`Connect`, `MeetEnds`).  The result is the shape's outline as
   `GeneralPt`s, control points marked.
2. A shape of straight sides has its **equations** found
   (`ShapeEquations.cpp`): every side's length and direction clustered
   (`TTrend`, `ShapeTrends.cpp`), the directions made into at most
   eight `AngCluster`s whose members are the lengths, the axes put in,
   perpendicular pairs made exact and pairs mirrored in a third made
   symmetrical (`RelateAngs`); then equations over the edge vectors -
   parallel sides at their lengths, lengths in a ratio of 1 or 2 made
   exact, mirrored sides at equal angles, the shape closing
   (`FamilyRotEqs`, `FamilyReflEqs`, `AlignRotEqs`, `DirSumEqs`).
   They are **minimised**, not solved (`ShapeSolver.cpp`): each squared
   into one quadratic form and the edge vectors moved by conjugate
   gradients - Numerical Recipes' `frprmn`, `linmin`, `mnbrak` and
   `dbrent` in 16.16 fixed point - then scaled to the box the shape
   covered (`MapSolutionToBounds`) and put back (`PlugNewVals`).  A
   shape with nothing to solve, or an open one, is squared up
   directly, each side laid along its cluster.
3. A closed curvy shape is tried as a **circle** and then an
   **ellipse** (`FindEllipses`, `ShapeEllipses.cpp`): a least-squares
   conic through the points, solved by `Decomp`/`Solve`.

Then a shape that touched others is **snapped** onto them
(`ShapeSnapping.cpp`): `SnapPtToLC` moves its ends onto the corners,
sides and circles they met (`SnapPtToLine`, `SnapPtToCircle`, and
`CircleTan`, which makes a line a tangent - to two circles at once when
they are one size), and an upright square or circle that touched nothing
is lined up with the squares and circles on the page and given their
size (`GlobalTrends`).

`TShapeRecognizer::HandleUnit` sends the view the `aeShape` command and
`TEditView::HandleShape` puts the shape on the page as a polygon view
(`views/PolygonView.h`).  The Notepad does shapes when its paper roll's
`_recogSettings` has 128 in it (the default 864 is text, words and
numbers); `src/host/demo/shapes.ns` draws a line, a box, a triangle and
a circle and `src/host/demo/snapping.ns` two circles and a line off a
box's corner.  `NEWTON_TRACE_SHAPES=1` prints each classification.

ROM bugs kept (each commented where it is):

* `NewCoeffs` allows 42 equations in a system with room for 41.
* `FindEquations` keeps a shape's lengths and angles as two arrays of
  15 one after the other, so a shape of more sides has its lengths run
  into its angles; its side map has room for 15 sides where up to 17
  get through (DEVIATION: the host block has room for 18).
* `TTrend::Merge` never sets the merged cluster's value, so a value
  looked up in it is answered with stack rubbish (DEVIATION: the host
  answers the merged mean).
* `GlobalTrends` skips a turned square without moving on, so every
  shape after it is that square again.
* The solver adds the quadratic's gradient only while the function is
  above 2 in 16.16, and `Minimize1D` leaves `xmin` unset when it runs
  out of steps (DEVIATION: nought on the host).
* `RSmallDists` always marks its runs untrustworthy, and `TVSplSpl` sets
  an end tangent to a point.

`NewCoeffs`' 42nd equation, `TTrend::Merge`'s value (the merged mean),
`GlobalTrends`' turned square, `RLineOut2`'s path limit that overflows
for a long chord and `TVStrTail`'s read past the last corner are fixed by
default now (`NEWTON_ROM_BUGS=1` for the ROM's behaviour).

## The word domain (`recognition/WRecDomain.h`)

`TWRecDomain` ('WREC') is the domain a handwriting engine is driven
from.  It takes strokes as its pieces ('STRK') and does almost nothing
itself: every question the controller asks goes straight to a
`TWRecognizer`, the protocol an engine plugs into.  What the domain adds
is a heap and an exception handler.  The ROM makes the engine a 222 KB
virtual-memory heap of its own (`NewVMHeap`, 0x37400 bytes), makes that
heap current around every call into it and puts the task's own back
afterwards, and treats anything thrown out of the engine as its having
run out of memory: `SignalMemoryError` tells the controller, counts the
failure in `gRecMemErrCount` - the number the word recogniser warns the
user about once a day - and the engine is put to sleep rather than asked
anything else.  DEVIATION: the host has one heap, so only the handler is
left, and where the ROM gives the engine back by destroying the heap it
lived in, this deletes it.

`IWRecDomain` finds the engine by name in the protocol registry
(`NewByName("TWRecognizer")`), hands it the domain it hangs off - one
store, at +0x10 of the protocol instance, which is how everything the
engine calls back reaches the controller - and calls its `Initialize`.
The delay is 0x78, a hundred and twenty ticks: a word waits that long
before the arbiter looks at it, which is the room the writer has to add
another stroke to the same word.

The traffic goes both ways.  Downward, `Group` offers a stroke,
`Classify` asks for a reading, `Reclassify` asks for another, and the
domain's own `VerifyWordSymbols`, `UnitConfidence`, `Sleep` and `WakeUp`
are the questions the recogniser above it puts.  A unit the engine made
nothing of is marked `kInvalidUnit` and closed; one still worth
something goes back to the controller as a piece for the domains above.

Upward, the engine calls the protocol's *own* methods - not dispatched,
had by being a `TWRecognizer` - and that is where the grouping is kept:

  - `GetPartialGroup` answers the word still being built, which is the
    last of the domain's units the controller is holding back (its delay
    list), and says whether there was one;
  - `MakeNewGroupFromStroke` starts one - a unit of the domain's type
    over the stroke's own areas, the stroke as its first sub, and the
    controller told with `NewGroup`.  A word that cannot be made is
    thrown rather than answered, because the engine has nowhere to put
    the stroke;
  - `AddSub` adds another stroke to it and `EndSubs` closes it, which
    drops the delay so the arbiter can have it;
  - `AddWordInterpretation`, `SetWordString`/`SetCharWordString`,
    `SetLabel` and `SetScore` are how a reading is put on the unit, and
    `NewClassification` offers the read word to the controller;
  - `StrokeUnitStroke`, `StrokeSize`, `GetSamplePtAddress`,
    `StrokeSampleX`/`Y`, `GetStartTime` and `GetEndTime` are how the
    engine reads the writing itself;
  - `UnitInfoGetPtr`/`SetPtr` keep the engine's own working store on the
    unit, and it goes back through the domain (`UnitInfoFreePtr`) when
    the unit does, because only the domain knows which heap it came out
    of.

Under the domain are the units.  `TStdWordUnit` is a `TSIUnit` whose
interpretations carry a word: each one's parameter is a handle holding
the string that was read, which is why `DeleteInterpretation` is
overridden - the handle has to go back as a handle rather than as the
recogniser object a `TSIUnit`'s parameter would be, and the label is put
to -1 first so nothing picks the interpretation as the best one on the
way out.  `InsertWordInterpretation` makes the handle before inserting
anything, so a failure leaves the unit as it was.  With nothing measured
the word stands on the bottom edge of its box (`GetWordBase`), and a
recogniser that has measured the writing answers better.  `TRecUnit`
adds the engine's working store, `TWRecUnit` is what the domain makes.

Every place that is written in - a field, a page - is a recognition
area, and an engine may want its own block of parameters for each one:
which dictionaries to read against, whether the field takes letters or
numbers, how much of the writing to keep.  The block belongs to the area
(`TRecArea` makes and frees it) and everything that happens to it is
asked of the engine through the domain: `DomainParameter` for how big
one is, for filling a new one in with the engine's defaults and for
letting go of whatever the engine hangs off one; `ConfigureArea` for
filling it in from the area's recognition configuration; and
`SetParameters` for handing over the block in force.  The handle is
locked around each, because the engine is given a pointer into it and
the heap compacts handles.  `TWRecRecognizer::ConfigureArea` is what
drives that from above, along with the area's three dictionary chains
(`BuildChains`, `recognition/Dictionaries.h`).

`TWRecDomain::SetParameters` carries two ROM quirks, kept: it does not
call the base, so `fParameters` is never written down and the controller
hands the block over before every unit rather than only when it changes;
and its answer is the wrong way round, saying "the parameters changed"
exactly when the engine has just run out of memory.  Now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour): it calls the base and answers whether the block changed.

NOT YET: `EndInkStrokeGroup` (the CIC library's
`WRecEndInkStrokeGroup`).

## The word recogniser and where ink comes from

`TWRecRecognizer` (installed by `InstallWRecRecognizer`, 0x00144094) is
the application-side face of the word domain.  Almost all of it hands
questions straight to the domain - `UnitConfidence`, `Sleep`, `WakeUp` -
and what it adds is `HandleUnit`, which is where a piece of writing
finds out what it is going to be.

Installing it is in three parts.  First the engine registers itself,
unless the `inhibitBaseRomWRecRegistration` preference says to leave the
ROM's own alone and let something else supply one; then, if no engine
answers to `TWRecognizer` at all, nothing is installed.  Then the domain
is made and the recogniser put on the list, with the word command
(aeWord), the "reads writing" flag, an arbitration time of one tick and
the word services (0x017EF000).  It is installed **asleep** and offering
none of its services.

Nothing wakes it until `SetWordRecognizer` (0x001442a0) puts it in use.
Only one word recogniser may be in use at a time - the MP2x00 has two,
this one and the Airus/Rosetta one - so putting one in turns the other's
services off and puts it to sleep, and `gWordID` holds the unit type of
whichever it is.  That one global is how the rest of the system knows
which units carry readings: `TUnitPublic::MakeWordList` gathers
interpretations only for units of that type.  The script side reaches it
through `UseWRec("XRWR")` (`FUseWRec`, 0x001443f4, over `GetIDFromRef`,
which turns a four-character string into a four-character type) and
`WRecIsBeingUsed`.  The area cache is purged on every change, because
the services a recogniser offers are what areas are built from.

`WordRecognizerHandleUnit` (0x00143f00) is the decision itself, and both
word recognisers share it.  The base line is worked out first
(`SetWordBase`), because everything that lays the writing out wants it.
Then:

  - a unit small enough to be a tap is a tap, whatever was written in
    it;
  - a unit the engine could not read (`UnitConfidence` answering
    `kWRecInk`) is ink: its word info frame is marked as ink, and
    `GetInkCommand` (0x00143dec) decides which ink command the view
    gets;
  - anything else is a word, and the recogniser's own aeWord stands.

`GetInkCommand` asks the view under the middle of the writing what it
wants.  A view whose recognition configuration has
`doInkWordRecognition` takes it as an ink *word* - something that sits
in a line of text and may be read later - and gets `aeInkWord`.  Any
other view gets `aeRawInk`, and the writing is drawn where it was
written.  With no view under it there is no command at all and the
writing is dropped.

Two warnings hang off the same function, because this is the one place
that knows both that something has been left as ink and that the
recogniser has been running out of memory (`gRecMemErrCount`, which
`TWRecDomain::SignalMemoryError` counts).  Each is shown at most once a
day, `RealClock()` being in minutes so that dividing by 1440 gives the
day.  Both are switched on by `gRecInkNotifyFlags`, which starts at zero
and which nothing in the ROM ever writes - it is there for a patch or a
diagnostic build.

The ROM's own engine registers itself through `RegisterRosettaWRec`
(0x001b6b7c), which the host now calls (`TNotebook::InitToolbox`,
`HostBootNewtWorld`) in place of the ink-only engine it used to
install, so writing is read (see "The Rosetta engine" below).
`TWRecRecognizer::ConfigureArea` (0x00144178), which hands the engine
the parameters an area is to be read with, needs the area-information
side of `TWRecDomain`.  `ReadDomainOptions` (0x0019cfd8) is what reads
the writer's recognition preferences at boot and calls
`SetWordRecognizer`.

## The cursive recogniser and the letter styles (`recognition/ParaGraph.h`, `XrDomains.h`, `WordRecognizer.h`, `LetterShapes.h`)

The MP2x00 has **two** word recognisers, and which one reads the writer's
hand is decided by the Handwriting Style slip's letter set
(`userConfiguration.letterSetSelection`, `gLetterSetSelection`):
`ReadCursiveOptions` calls `SetUpRosetta` and `SetUpParaGraph`
(`Recognizer.cpp`), and set 2 - printed - puts **Rosetta** ('WREC') in use,
any other set **ParaGraph's cursive recogniser** ('XRWR').  The
Handwriting Recognition preference shows it too: its panel frame keeps
`rosForms` and `paraForms`, and the Letter Shapes slip is one of the
`paraForms`.  (Earlier pages called Rosetta "ParaGraph's Calligrapher":
that was wrong.  Rosetta is Apple's own printed-writing recogniser - the
neural-net classifier and the easter egg's names are Apple's; the `xr`
engine with its `low_type`/`EXTR` feature extraction, which nothing
reachable from `RosettaClassify` calls, is ParaGraph's cursive engine.)

**What is reconstructed** is everything of the cursive recogniser that is
not its reading:

- `InstallWordRecognizer` - in `TRecognitionManager::InitRecognizers`, at
  level 2, between the shape and the WRec recognisers - makes the
  strokes-to-xrs domain 'STXR' (`TStrXrDomain`) and the xrs-to-words one
  'XRWR' (`TXrWordDomain`), a `TWordRecognizer` over the second, registers
  Gestalt 0x02000008, and wakes it and puts it straight back to sleep:
  `WakeUp` runs `loadLetterWeights` (the System soup's "LetterWeights2.0"
  entry into `SetLetterWeights`) and `Sleep` `saveLetterWeights`, both
  with the letter set set to nought for the length of the call.
- The word domain's `DomainParameter` - every selector (XrDomains.h lists
  them) - over the parameter block, an `XRWORDPARAM`: the engine's
  `rc_type` (0x10c bytes) and the domain's own fields.  Its numbers are
  big-endian halfwords written a byte at a time, which the host keeps in
  byte arrays at the ROM's offsets (`RCByte`), so a recognition
  configuration's `xrwCommands` (`SetXrWordRC`: an operation, a field and
  an operand in one word, or a raw byte offset) change the same field.
  Selector 1 is `InitializeParamStruct`: the engine's defaults, the letter
  table loaded and the letter set's learning info.
- The engine's data (`ParaGraph.h`): its allocator (`HWRMemory*`: a handle
  whose first word is the handle, DEVIATION eight bytes on the host), its
  character classes, and the letter table - the `DTEHeader`, `DTEMain`,
  `PPDMain` and `DTETrigrams` binaries of `charsetInfoResources`, read
  into a header (`ReadDteResource`) whose symbol descriptors say how many
  ways each letter may be written, their default weights (*vexes*), their
  groups and which letter sets use them.  Each letter set has a
  *learning info* (0x924 bytes: a byte per variant - vex and use counter -
  and the capitals) made from the defaults the first time it is used
  (`AllocLearnInfo`), changed by picking shapes by hand
  (`SetVariantState`, selector 0x20016) and by learning on the fly
  (`FlyLearn`: the variants a corrected word was read as have their
  counters set back, the others counted up and their vexes moved), and
  kept in the System soup as a 'letterWeights binary for each set that is
  not at its defaults (`PGGetLetterSetInfo`).  `SetRamParaData`/
  `GetRamParaData` replace a table from RAM (`vars.|RamParaGraphData:PARA|`).
- The natives: `GetLetterWeights`, `SetLetterWeights`,
  `ResetLetterDefaults`, `UseTrainingDataForRecognition`,
  `GetLearningData`/`SetLearningData` (the orthographic database, with
  big learning on), `ConvertFromMP`/`ConvertForMP` (the MessagePad 100's
  layout), `DoCursiveTraining`, `RosettaExtension` (nil), and the Letter
  Shapes slip's `DrawLetterScript`, `ClickLetterScript`,
  `CountLetters`, `getLetterIndex`, `getIndexChar`,
  `GetHiliteIndex`/`SetHiliteIndex`, `GetLetterHilite`/`SetLetterHilite`
  and `DrawStringShapes` over the ROM's `letterimages` (LetterShapes.h has
  the format): a group of variants to a 35-pixel cell, a letter pair to a
  page, the tapped group hilited and its weight changed, never leaving a
  letter with no group in use.
- The boot's choice: `SetUpRosetta` also sets `FragmentLigatures` from
  the `doFragmentation` preference - 'default meaning "if the processor
  runs faster than 90 MHz", which is why the kernel now answers the
  MP2x00's 162 MHz StrongARM in Gestalt (`gMainCPUType`,
  `gMainCPUClockSpeed`, `hal/System.h`) - and the host no longer calls
  `SetWordRecognizer` itself.

`src/host/demo/letterstyles.ns` (ctest `host.NewtonLetterStyles`) asks
the natives from NewtonScript: 320 pictures, the letter weights at their
defaults before and after `ResetLetterDefaults`, a word drawn in picked
shapes.

### The way writing reaches the cursive reader (`recognition/StrXrDomain.cpp`, `WordDescriptors.h`, `CursiveReader.h`)

With a cursive letter set the strokes-to-xrs domain ('STXR',
`TStrXrDomain`) takes every stroke.  While the pen is still writing, a
stroke is simply *listed* on the unit collecting the writing
(`TStrXrUnit`: a bit per stroke in `fStrokes`, the stroke added as a
sub) - `PreGroup`/`Group` -> `GCPregroupAndGroup` ->
`CallGroupAndClassify`.  When the unit is classified (or the stroke is
the last complete one) the listed strokes become a trace
(`GCAllocRecTrace`) and go, with the unit's **word descriptors**, to
ParaGraph's word segmenter (`GroupAndClassifyStrokes` -> `GCGroupStrokes`
-> `WordStrokes`, the same segmenter the ink grouping uses).

The word descriptors (`WordDescriptors.h`) are eight 0x50-byte slots in
one handle (`GCNewRecSegment`), a doubly linked list threaded through
them by halfword indices (0xffff none; a slot whose two links are both
nought is free).  Each names its word's strokes as a run (`fFirst` to
`fLast`) and up to eight more, keeps what the segmenter said of its line
(`GetWSBorder`) and of the gaps inside it it was least sure of
(`SetStrokeSureValuesWS`), and flags how far it has got: 2 settled, 4 to
be read now, 0x50/0x28 read, 0x80 ends in a dash, 0x100 not contiguous,
0x200 the low level failed, 0x400 the xr reader failed, 0x800 no memory.
`GCWriteNewGroupResults` turns the segmenter's words into descriptors -
keeping one whose strokes have not changed, throwing away one whose
strokes moved to another word, and **joining a word written after a dash
at the end of a line to the word before it** (`GCMergeWordDesc`: the
second part's strokes appended, where the two lines meet made relative,
`fMerged` the first part's stroke count).  `GCRecSegmentSetGroupFlags`
marks the words to read now: all at the end of the writing, otherwise
the settled ones but the last `lineAtATime`.  `GCClassifyStrokes` reads
those (and, when there are none, reads the settled ones ahead), each
told the base line of the last word read before it.

`GCTryToRecognize` (`CursiveReader.h`) reads one word: its trace
(`GCWDGetTrace` - a copy with the extra strokes appended, and for a
joined word the second line moved up to the end of the first and the
dash taken out, `GCMergeLinesAndRemoveDash`), the base line handed to
the engine (`GCFillBaseLineParameters` -> `SetRCB`: the ink box, and a
height and middle for the letters with how sure of each - from the
segmenter's line, the word before's, or a fixed base line the field
gives), the recogniser's data locked (`GCLockRecognitionData`), then the
reading itself in three layers - the digit and number reader over
"chunks", `low_level` (the trace cut into xrs) and `xrw_algs` (the xrs
matched into words) - and what came of it written into the descriptor
(`GCWDWriteRecResults`).  Back in the domain, `GCReleaseRecResults` makes
each word read (or gone wrong) a unit of its own (`WriteRecResults`: its
strokes, the dash left out, as subs; where it lies; its readings,
`GCWriteRW`, cut into parts when the reader split the word) handed to the
controller as a new piece; a word that went wrong is flagged invalid
(0x400000) and its strokes are given up as ink.  What was not read goes
on in a new unit with the segmenter's state.

ROM bugs kept: the extra strokes of a word are copied (and taken off the
list) while their *index* is less than the stroke number rather than
while the stroke is not nought; the dash's removal from a joined word's
info moves an entry down but renumbers the one left behind (both now fixed
by default, `NEWTON_ROM_BUGS=1` for the ROM's behaviour); when no
stroke reaches right of nought `GCMergeLinesAndRemoveDash` answers the
caller's r8.  `NEWTON_TRACE_CURSIVE=1` prints each word the reader is
given and the answer; `src/host/demo/cursive.ns` makes the letter set
cursive and writes "ton" and "to": two words, each read and each failing
at the low level (-8).  `test_WordDescriptors` covers the list, the
joining, the traces, the base line and two words through the segmenter
into the reader.

### The low level (`recognition/LowLevel.h`, done)

`low_level` (0x0034ea74) is what cuts a word's trace into xrs.  It works
in a `low_type` - a 0x9c-byte block on its stack holding the trace as
parallel x and y arrays (a y of -1 a pen-up, one at each end and one
between strokes), four working buffers of 0xce7 shorts that the filters
write into (0 and 1 x and y, 2 and 3 a map from each point back to the
point of the original trace it came from), a group per stroke
(`POINTS_GROUP`: its extent and box), and the list of *special elements*
(`SPEC_TYPE`, 0x14 bytes: a kind, a code, an attr, the points it covers
and two more, linked both ways) that the later passes turn into xrs.  All
but the element list and the stroke descriptions are one allocation
(`LowAlloc`).  The order of work is `PrepareLowData`, the trace copied in,
then `BaselineAndScale` - `Errorprov` (a doubled pen-up taken out),
`Filt` (the trace resampled: a point too near the last one kept dropped,
a gap too wide filled at a step of the root of the distance asked for),
`InitGroupsBorder`, `Extr` (each stroke's extrema) and `transfrmN` (the
base line) - then `AnalyzeLowData` and `exchange` (the xrs written).

The extrema (`Extr`, `BigExtr`, `DirectExtr`) are found along a
direction (a, b): a point's value is its x and y weighed by the
direction over |a|+|b|; a run of points within `eps` of a point's value,
bounded by lower values on both sides (or the stroke's end on one),
makes a maximum, and likewise a minimum, the element covering the run
with ipoint0 the middle of its flat top.  The two kinds alternate - one
is not recorded straight after one of its own kind - and the stroke is
bracketed by a 0x10 and a 0x20.  The directions: up and down (kinds 3 and
1 - y grows downwards, so 3 is a letter's bottom), left and right (0x13,
0x11), the two diagonals (0x23/0x21 and 0x33/0x31, the second found one
point at a time by `DirectExtr`, with x weighed twice when once finds
nothing).  A quirk: `BigExtr` adds each coordinate with the sign of its
weight rather than multiplied by it, where `DirectExtr` multiplies - the
same for the 0/1 weights `BigExtr` is given.

The base-line finder `transfrmN` (0x001baaf8, 6.4 KB, and 38 KB below it)
works over two arrays of `EXTR`s - copies of the bottoms and the tops
with a *suspicion code* each (0 on the line, 0x65/0x66 sticking out below
or above it, 0x67 near it, 0x6e put back, 0x0d a tail struck off, the
tens the kinds of gap and glitch) - and smooths a lower and an upper line
under every point (`smooth_d_bord`/`smooth_u_bord` over
`point_of_smooth_bord`: the area under the extrema's polyline over a
window, divided by its width).  Its pieces so far are `LowBaseline.cpp`.

Done (2026-09-28): the state and its memory, the strokes, the trace
utilities, the engine's integer roots (`HWRMathISqrt`/`HWRMathILSqrt`
over `SQRTa`/`SQRTb`/`sqrtab`, generated into `LowTables.cpp`), the
filters with `PSProc`/`NewIndex` (the elements moved to the filtered
trace), the extremum finders, the element list operations, and about
thirty of the base-line finder's pieces.  ROM bugs kept:
`InitGroupsBorder` writes the next stroke's start one group past the
array when it is full (onto the index that follows it in the block);
`GetGroupNumber` answers its own argument's address for a point in no
stroke; `spec_neibour_extr`/`neibour_susp_extr` read an unset register
for a kind other than 1 or 3 (all four fixed by default now;
`NEWTON_ROM_BUGS=1` for the ROM's behaviour).  `test_LowLevel` checks the roots against
the true roots, the strokes of a hand-made trace, the filters on a line,
a zigzag's extrema, and the pieces one by one.  `low_level` itself is not
yet called: `GCTryToRecognize` still answers -8 until the whole layer is
there.

**The base line is whole** (2026-09-28): `BaselineAndScale` (`LowLevel.cpp`)
filters the trace to a step of a sixteenth of its box's height, finds each
stroke's extrema up and down, and hands over to `transfrmN`
(`LowBorders.cpp`), which is the finder proper:

- the strokes are classified first (`classify_strokes`, `LowClassify.cpp`:
  every extremum and stroke end given an attr - an i's dot, a t's stem
  crossed by a bar, a horizontal bar, an umlaut, punctuation leading and
  trailing, the ends of entry and exit strokes, loops - over the tests in
  `LowPunct.cpp` and the geometry in `LowGeometry.cpp`); a word of figures
  (rc +0x94 = 0x20) has `classify_num_strokes` instead, which knows an
  upright, a level stroke, a plus, a four's two strokes and a bracket;
- the bottoms and the tops are copied into two lines of `EXTR`s
  (`extract_all_extr`, each stroke's x closed up by the gaps before it) and
  each line cleaned by `bord_correction` (`LowLine.cpp`): the *gaps* (an
  extremum the line steps up or down to, steeper and higher than the
  ROM's tables `TG1`/`H1` allow) and *glitches* (a run of one to three
  that the line steps into and out of, `TG2`/`H2`) are found and made
  descenders (0x65), ascenders (0x66) or things inside the letters
  (0x67), which are taken out of the line and their elements coded so;
- the lower border is smoothed under the bottoms and the upper over the
  tops (a y for every point of the trace), their medians and the letters'
  height taken (`calc_med_heights`), and `line_pos_mist` scores how badly
  they fit - every extremum looked at again against them, and the counts
  of extrema above and below against the caller's own line - which
  decides whether to go round again: a second pass on a second pair of
  arrays with the first pass's findings, the better of the two kept;
- a word that proves to be figures (`numbers_in_text`) goes round again
  as figures; one that will not do (no extrema, a penalty of a hundred,
  the borders crossing, a height under twelve or unlike the caller's)
  gets level borders from `SpecBord`;
- then the borders are brought together for a short word, pushed apart
  when under twelve apart, and **the trace itself is rescaled**: y runs
  0x2796 at the upper border to 0x27e6 at the lower (80 units the small
  letters' height; above and below them continuing at the same scale),
  and x is 80 units to that height from the box's left, +0x50.  The
  engine's parameters are told the height (rc +0xea), the lower border's
  median (+0xec), how sure the finder is of each (+0xee, +0xf0: 45 to 90
  by how many extrema it had and how little it had to put right) and the
  borders at ten places across the word (+0x98, `FillRCNB`).

`test_LowLevel`'s `TestBaseline` writes eight synthetic arches 40 high on
y = 200 and gets back a height of 40 and a lower border of 200, sure
90/90, with the letters' feet at 0x27e6 and their tops at 0x2796; with an
ascender and a descender among them those two are coded 0x66/0x65 and the
lines do not move.  All of it was read from the disassembly: the
decompiler lost the conditions of nearly every one of these functions.
ROM behaviour kept: `extract_all_extr`'s walk back does not skip the
extrema it did not copy, so a stroke with one writes its shift one EXTR
too early; `numbers_in_text`'s test of the stroke before a figure measures
the figure itself, so it never passes (these two are fixed by default
now, `NEWTON_ROM_BUGS=1` for the ROM's behaviour); `FindCrossPoint` rounds a negative
step the wrong way ((5, 5) comes out (5, 6)); `curve_com_or_brkt`,
`all_susp_extr`, `glitch_to_inside` and `bord_correction` leave values
unset for kinds other than 1 and 3, which they are never given.

AnalyzeLowData's passes have begun (`LowAnalyze.cpp`): `DefLineThresholds`
(the heights the passes compare with, `low->fThresh`), `OperateSpeclArray`,
`Sort_specl`, `Clear_specl`, `Surgeon`, `measure_slope` (the slant) and
`look_like_circle`.

**Pict** (`LowPict.cpp`, 0x003298d8 and 59 functions, about 34 KB; done
2026-09-28) is AnalyzeLowData's first element finder.  It looks at the
strokes one by one against eleven heights (`BildHigh`: the word's top and
bottom, clamped to the normal line's, the rescaled line's five fixed
heights 0x2796..0x27e6 and the heights between; `RelHigh` says which band,
9 highest to 0 lowest, a stroke's top and bottom are in - its `code` and
`attr`).  Each stroke is *described* (`StrElements`): a `_SDS_TYPE` head
(0x10), one description per piece between the stroke's ends and its
corners (`RareAngle` finds the corners), and a tail (0x20).  A piece's
description (`iMostFarDoubleSide`) is its box, its chord's length and
slope (hundredths of dy/dx, 0x7fff upright), the furthest point on each
side of the chord, the bend in hundredths of the chord (`crook`), and the
length along the trace; a head reuses the slope, crook, length and share
fields for its own index, the longest piece's index + 1, the whole length
and the stroke's code and attr.  Then the stroke is judged:

- **7, ParaGraph's straight stroke** (`SPDClass`): its longest piece long
  enough, straight enough and *level* enough for its bands, by the trained
  tables `minL_H_end`, `maxCR_H_end` and `maxA_H_end` (10x10 shorts,
  [top band][bottom band], -2 impossible, -32767 never; generated into
  `LowTables.cpp`), every point near the piece's line.  It is a *level*
  stroke - a dash, a t's bar - not an upright one: the tables allow
  nothing steep, and a stroke from the word's top down to the line is
  "never".  `YFilter` refuses one that is really a bar crossing an upright
  of the stroke before or after.
- **8, a dot** (`Dot`): one point, or a box under `maxX_H_end`/
  `maxY_H_end` that is not a short steep stick.
- **a hatch** (`HatchureS`, the biggest piece, with `ApprHorStroke`,
  `SpcElemFirstOccArr`, `DrawCross`, `ShiftsAnalyse`, `HatDenAnal`,
  `RMinCalc`, the filters `SCutFiltr`/`LeFiltr`/`LowStFiltr`/`RDFiltr`/
  `Oracle` and `StrokeAnalyse`): a stroke whose level piece crosses one of
  the upright sticks found first (`VertSticksSelector`: up to 80, the
  piece between two tops or two bottoms that is upright and straight -
  `fBars`) - a t or an f's bar written in one stroke with the letter, or a
  cross over an earlier stroke's stick.  The bar is cut off with a pen-up
  put into the trace (`InsertBreakAfter`) and the strokes found again, the
  rest of the stroke judged on its own (`StrokeAnalyse` builds a head and
  one piece on its stack so that `Dot` and `SPDClass` can be asked).
- **a crossing within the stroke** (`InStr`): a level piece low down that
  crosses a steep piece two before it is marked an arc (5).

Every stroke's arcs are found (`SlashArcs`: a low then a high between the
stroke's start and end, marked 5 with their size in `other`).  A dash or a
dot is drawn straight in the trace (`FantomSt`, through the working
buffers) and added to the list between a 0x10 and a 0x20 of its own, with
the first points of the two steps of it that other strokes' sticks cross
(`FillCross`).  Last, `Recount` takes the descriptions back to the trace
as it was given (buffer 2's map).

**angl** (`LowAngles.cpp`, 0x002a9f7c) marks the corners: buffer 3 holds,
for every point not of a dash or a dot and not within six of a pen-up or
the ends, the square of the distance between the points six either side;
where that is at most 1000 (the trace doubles back) the cosine of the turn
four points either side is taken, and a run with a cosine of -60 or more
is a corner (0x0b) at its sharpest point, with the way it opens
(`angle_direction`: 0x10, 0x20, 0x40, 0x80) as `other`.

`test_LowLevel`'s `TestPictPieces` checks the measurements (an arch's
description, a V described as a head, two arms and a tail, crossings);
`TestPict` takes three arches, a dash and a dot through `BaselineAndScale`
and AnalyzeLowData's first steps into `Pict`: the dash comes back 7, the
dot 8, three heads and three tails; `TestAngles` finds a hairpin's apex.
Nearly all of it was read from the disassembly: the decompiler lost most
conditions and took the unaligned halfword loads (`ldr` at an address two
past a word, whose *low* half is the halfword two before) for the wrong
fields - `CrookCalc`'s answer, `FillCross`'s crossing points and several of
`HatchureS`'s records are wrong in its output.  ROM behaviour kept:
`InitElementSDS` clears 0x28 of a description's 0x2c bytes; `InStr`
narrows its slope limit for good each time; `InsertBreakAfter` walks its
sticks by the old count after taking one out (skipping the next, seeing
the last twice); `angle_direction` is passed the slant and never uses it;
`SlashArcs`'s arcs carry whatever code and attr were on the stack
(DEVIATION: nought on the host); `LowStFiltr` reads the element two
before the list when no top is found and `RMinCalc` leaves a point unset
on one path (DEVIATION: the host takes -2); `FantomSt` divides by a line of
no length (DEVIATION: guarded).

**Circle** (`LowCircle.cpp`, 0x002bc5b8) finds the closed loops: each
foot (a bottom, 3) between two tops that looks like an o's
(`look_like_circle`) is tried, drawn left to right as a *back* circle
(the usual anticlockwise o) or right to left as a *forward* one standing
alone in its stroke.  `Clash_my` pairs every point of a stretch on the way
down into the foot with every point of a stretch on the way up out of it
and takes the nearest pair (the slant allowed for across,
`SlopeShiftDx`) as where the loop closes; how close is close enough comes
from `Ruler0` and `circle_type`, which know an e's eye (`is_e_circle`), a
g's, d's and b's bowl (`is_g_circle`, `is_d_circle`, `is_b_circle`) and a
loop that doubles back (`vozvrat_move`: "return movement").  A loop is
marked as a crossing pair (6), `other` 'c' at the later point and 'd' at
the earlier (`make_circle`).  ROM quirk kept: `circle_type` gives an
isolated loop 0x3c and writes it over with 0x23 at once.

**FindSideExtr** (`LowSide.cpp`, 0x00303584) asks of each side between a
top and a bottom whether it bends out (`SideExtr`: the bend found on the
filtered trace, `iMostFarFromChord`/`iMostCurvedPoint`, and judged on the
trace as first filled, where a real corner is a triangle the path fills -
`IsTriangledPath` over `TriangleSquare` and `ClosedSquare`, and two
unnamed helpers after it); a hook at a stroke's start or end moves that
top or bottom halfway to the bend.  With `strict` (as FindSideExtr asks)
the bend must be near one end - a symmetric bow is not one.  ROM quirk
kept: only a slant to the left is quartered.

**Cross** (`LowCross.cpp`, 0x002c8fb4) finds where the trace passes over
itself: `Grab` walks each stroke against itself and against each earlier
stroke whose box comes within `nbcut0`, points near each other by the
`eps0`..`eps3` tables (the limit grows with how far apart the two points
are along the trace), stepping faster where they are far apart; `Clash`
grows a near pair into the two stretches that run together, and the
crossing is a pair of elements: 6 an ordinary crossing, 9 where the trace
comes back along itself (`DrawEnds`), 0xa with a dash; one overlapping the
last is merged into it (`ChkMrgCrs`, `AnyCrosCont`).  The elements are
appended to the list's *array*, and those two functions find the last
crossing as the array's last two elements.  ROM quirks kept: the copy of
a 9's end takes `ipoint0` - the low half of an unaligned `ldr` at
`ipoint1`'s address - and `Grab` works out a row's start from
`const1[17]` only to write it over.

**lk_begin** (`LowBegin.cpp`, 0x002f8d68) gives the elements their `code`
- the kind of xr each will be - and `attr` its height band:
`init_proc_XT_ST_CROSS` tidies the crossings (a loop's inside another is
dropped), dashes and dots; `process_ZZ` folds each stroke's first and
last extremum into its start and end (3 at a top, 7 at a bottom, 0xd a
dash, 0x10 a dot, 0xf/0x27 an arc) and puts a break element (0x44, code
0x12 or 0x14) between strokes; `process_AN` keeps an angle only when the
extrema either side do not cover it (0xe or 0x11); `process_curves` codes
the tops 2 or 3 and the bottoms 8 or 7 by how sharp they turn; and
`DefineWritingStep` measures the writing's step across
(`delta_interval`) into low +0x70.  ROM quirks kept: `process_ZZ` writes
its break into the *array slot before* the stroke's end - the element it
folded into that end a moment before, now out of the list - and keeps
that element's two points; its stroke join is unreachable (the codes
that would reach it have gone to the break already); `process_curves`
writes an arc's height twice.

**Adjust_I_U** (`LowAdjust.cpp`, 0x00303038) looks again at a narrow
bottom between two tops, an i's or a u's: bends going in, coming out and
across that agree make it round (8), bends that disagree - or a strong
turn across straight sides - sharp (7), unless it is much wider than
deep.  ROM quirk kept: the kind it keeps is only ever 0 or 2, so its two
tests for a kind of 1 are dead.

`test_LowLevel` checks each: `TestCircle` (a cursive "uou": the o closes
at its top, the u's do not), `TestSides` (a hook bends left at its
corner; a straight side and, strictly, a symmetric bow do not),
`TestCross` (a stroke crossing itself is one pair; a t's stem drawn up
and back down is a 9 and its bar crosses it twice), `TestCodes` ("uou"
comes out a start and end at tops, three tops, four bottoms and the o's
crossings) and `TestIU` (a V's bottom becomes 7, a U's stays 8).  Nearly
all of it was read from the disassembly, as before.

**lk_cross** (`LowLkCross.cpp`, 0x002ca074) decides what each crossing
is.  `analize_sticks` takes the ones where the pen comes back along
itself (mark 9): the extrema it passes between going and coming back are
counted - uppers and lowers, and among them the stroke's own turns (3,
7) - and the stick coded 3 (up and down), 7, a hook (0x15, 0x18, 0x19,
0x1c: `EndIUIDNearStick`, `cos_normalslope` against the slant), flat
(0x1f, 0x20) or taken out when the pen barely moved; a d's bowl
(`IsDUR`/`IsShapeDUR`) or loop (`is_DDL`) is recognised on the way.
`analize_circles` takes each loop still uncoded (mark 6): the extrema
inside and outside it counted, and it coded 4 (a loop at the top: an e,
an l), 6 (at the bottom: a g's), 5 (closed: an o - `Decision_GU_or_O_`,
over the loop's box and length in `CrossInfoType`), 0x1d/0x1e (small),
0x1f/0x20 (flat, `CheckSmallGamma`) or 3/7/0x15 when thin enough to be a
stick (`Isgammathin`, over `GetMaxDxInGamma`, the loop's widest).
`del_inside_circles` then takes out the uncoded pairs and, for a coded
one, the elements its loop swallowed (`CheckInsideCrossing`), moving the
ones really outside it after it (`IsOutsideOfCrossing`) and restoring an
inner angle (`IsInnerAngle` over `IsRightGulfLikeIn3`, `Restore_AN` - an
angle lk_begin left in the array but out of the list is linked back);
each pair becomes one element spanning both passes.  The point-in-polygon
test `IsPointInsideArea`/`IsPointOnBorder` counts crossings along a ray
from x = 1 to the point.  Two places read an element's array neighbour
as its crossing partner (the ROM's +0x14/+0x16 loads).

**RestoreColons** (`LowRestore.cpp`) finds a colon written as two dots:
of up to eight dots and dashes that stand clear of their neighbours, two
next to each other in the trace, close across and 20 to 160 apart down,
not an i's dot (`LooksLikeIAndPoint`), are moved by `PutColonAtItsPlace`
to the break nearest their middle across, a break made where needed and
neighbouring breaks joined.  **PostFindSideExtr** asks the trace between
an upper and a lower extremum for a side bend (`SideExtr`, strictly) and
makes an arc element of one that is closed off and has nothing in
between (0x28 going down, 0x29 up), or an angle (0xe) where the pen
doubles back sharply.

**exchange** (`LowExchange.cpp`, 0x002c7a00) switches on each element's
`code` to write the xr (`xrd_el_type`, 0x18 bytes): +0 the xr type, +1
flags (1 last in its letter, `MarkXrAsLastInLetter`; 2 a stroke start
that carries a gap; 0x80 next to a break), +2 the input penalty
(`AssignInputPenaltyAndStrict`, over `penlDefX` and `penlDefH`), +3 the
height band, +5 the direction, +6 the link to the next
(`GetLinkBetweenThisAndNextXr`: a stick, an arc of a certain bend either
way, an S or a Z), +8 the point, +0xa/+0xc the first and last points,
+0xe..+0x15 the box (left, top, right, bottom); a break (type 1) at each
end.  The points are mapped back to the original trace (buffer 2), a
one-point xr widened by one either way by its type, and `check_xrdata`
puts in the crossing (type 5, `PutZintoXrd`) a gap in a letter stands
for.  **FillXrFeatures** (`LowXrFeatures.cpp`) then measures the
writing's slant (`GetCurSlope`) and gives each xr a height class (+3,
written over the band) and a shift (+4) against the four xrs that
bracket it (`FillSHR`, over `xr_type_merits`' upper/lower/end bits) and
its direction, one of 32 (`FillOrients`, `GetVect`, `GetAngle` over
`ratio_to_angle`).  `test_LowLevel`: `TestExchange` (the uou's xr
stream), `TestRestore` (a colon moved between two u's), `TestLkCross`.

**lk_duga** (duga is Russian for an arc) is begun (`LowLkDuga.cpp`):
`arcs_processing` folds a top (2, 3) or bottom (7, 8) that is only the
tip of a stroke's start or end beside it into it - close in height
(`DyLimit`: a quarter of the height to the nearest extremum of the other
kind, at least 27, or no tip at all when a low top's bottom is only just
below it), close across and narrow (`IsDx_Dy_in_arcs_OK`,
`IsDx_Dy_in_tips_OK`, `IsTipOK`) - the end becoming an arc (9..0xc) or
the extremum taking the end's mark; `delete_CROSS_elements` takes out the
loops too short to be letters (`ins_third_elem_in_circle` keeps one taller
than 60 with no loop or arc beside it, as a small loop 0x1b at the top or
0x17 at the bottom); `check_IUb_IDf_small` gives a stick the other height
band when its neighbours, or its lean across the points about it, say so.

`lk_duga` itself is whole: `prevent_arcs` (with rc +0x92 = 2: a narrow
end or top kept a stick, other set to 1), `arcs_processing`,
`conv_sticks_to_arcs` (a stick starting or ending a stroke made an arc
9..0xc when it runs level - a cosine of 0.85 with the horizontal,
`cos_horizline` - and is long or bent, its point moved to the extremum
across it; a flag that would also skip ones under 12 across is nought and
never set), `del_before_after_circles` (the elements either side of each
loop - an element marked 6 - handed round in a `NxtPrvCircle_type`:
`check_before_circle` moves the loop's start back over a loop-like
element before it and turns that element into a small loop or takes it
out when it goes up (or down) into the loop (`UpElemBeforeCircle`,
`DnElemBeforeCircle`, `Is_8` - the top of an 8 standing on the loop is
spared); `check_after_circle` merges a closed loop after it, makes a
0x21 of a top loop after it at least four fifths its size
(`check_next_for_circle`), makes the second half of one letter a small
loop (`check_next_for_common`, `change_circle_after`), folds an arc into
the 0x21 before it and settles the stroke's last stick
(`check_next_for_special`, `make_CDL_in_O_GU_f`); `O_GU_To3Elements`
turns a bottom loop with a stick either side into one letter of three
elements; `IsTipBefore`/`check_inside_circle` take out a tip that only
starts the loop), `delete_CROSS_elements`, `check_IUb_IDf_small` and
`delete_UD_before_DDL`.  ROM quirks kept: `check_next_for_circle`
compares the band of the element it first found, not the one it now
looks at, and measures that one's box to its ipoint1;
`check_next_for_special` reads the byte after the loop's element in the
array (its crossing partner's `other`) as the loop's own;
`O_GU_To3Elements` with nothing after the loop makes the element after
it in the array the new 0x1a.  `TestLkDugaWhole`.

**xt_st_zz** (`LowXtSt.cpp`) is the strokes written out of order and the
breaks: all of its passes but one are done - `conv_top_elem_to_ST`
(short strokes high up made dots), `find_umlaut`, `find_angstrem`,
`redirect_sticks`, `FindDelayedStroke` (a stroke lying left of
everything before it by the step - a t's bar - made one late element
0xd), `placement_XT_ST` (each late stroke and dot put after the letter
it belongs to: `Placement_XT_CUTTED`, `DoubleXT` - a bar across two t's
doubled -, `Placement_XT_With_HATCH` over its own crossings,
`FindQuotes` with `PutLeadingQuotes`/`PutTrailingQuotes`,
`Placement_XT_WO_HATCH_AND_ST` by distance across, `Put_XT_ST` and
`punctuation`, which offers a high dot to `RestoreApostroph` and puts a
break after punctuation with `insert_drop`), `del_close_MAX_MIN` (two
tops, or two ends, that are one), `SortXT_ST`, `placement_X` (the second
half of an x crossed out after the word), `FindMisplacedParentheses`,
`del_ZZ_HATCH`, `CheckStrokesForDxTimeMatch` (a last stroke written far
back left dropped), `change_last_IU_height`, `make_different_breaks`
(each break's gap across the three bands of the line,
`GetDxBetweenStrokes` over `GetTraceBoxInsideYZone`, compared with the
step and with the other gaps: no break, a letter break 0x12, or a space
0x14), `AdjustZZ_BegEnd` and `CheckSequenceOfElements`.  ROM quirks kept:
`insert_drop` takes the element after its argument in the array as the
new break rather than a free one; `RestoreApostroph` keeps the dot's
squared length in a short; `punctuation` compares a late stroke's whole
`attr` byte with 5; several flags in `Placement_XT_WO_HATCH_AND_ST` and
`RestoreApostroph` are nought and never set, so their tests never pass.
`TestXtSt`.

**FindDArcs** (`LowDArcs.cpp`) is the last of xt_st_zz's element finders:
every element and the next one past the angles and horizontal moves
(`SkipAnglesAndHMoves`) that do not overlap, an upper one no lower than
band 7 followed by a lower one, is described in an `SZD_FEATURES` block
(on the ROM's stack; `LowLevel.h` gives its 0x44 bytes): the pieces of
the initial trace each covers, the three points the pair turns at (i1 the
first's top a quarter of the way along, i2 the second's bottom, i0 where
they meet - half way, or the bottom of a gulf to the right), each arc's
point furthest from its chord, and each arc's bend as `CurvNonQuadr`
measures it - the area the trace closes with its chord in hundredths of
the chord's length squared, at most 1000, signed by the side it bows to.
Bends the opposite ways round are an S or a Z (`CheckSZArcs`): when they
are consistent along each arc (`CurvConsistent`: no S inside either),
within eight times of each other and strong enough, with the far points
out to the side, a new element goes between the two - code 0x24 (the top
arc bowing right) or 0x23 (left), band 7, its `other` 4 or 2 when the
bends are strong, weighed by how alike they are - and an upper or lower
stick whose piece is really wide becomes the arc it is (9/0xa, 0xc/0xb).
Bends the same way round, or a stroke that goes right, back and right
again before it comes down (`CheckBackDArcs`), are a d's bowl
(`CheckDArcs`): an element 0x25 (bowing right) or 0x26 between them
when the whole bends with them, and the sticks either side made arcs when
their pieces are wider than tall or follow a late stroke; `SideExtr`
vetoes the upper one when the lower bends out to the right.  The angles
and horizontal moves inside the new element are then moved before it or
taken out (`ArrangeAnglesNearNew`, `KillHAtNewElem`).  One of
`CheckDArcs`' box measurements reads the widths through word loads two
bytes past the word boundary, which take the halfword before the one
named: the offsets say bottom and top, the values are right and left.
`test_LowLevel`'s `TestDArcs` writes an S and gets its 0x23 element.

**low_level whole.**  `xt_st_zz` runs its passes in the order
`LowXtSt.cpp`'s header gives; `AnalyzeLowData` (`LowLevel.cpp`) runs the
filters, `Extr`, `Pict`, the refiltering, the slant (`measure_slope`;
nought in a numbers field or when rc +0x90 bit 0 says so), `Circle`,
`angl`, `FindSideExtr`, `Cross`, `lk_begin`, `lk_cross`, `lk_duga`,
`Adjust_I_U`, `xt_st_zz`, `RestoreColons` and `PostFindSideExtr`; and
`low_level` (0x0034ea74) makes the state on its stack (the `low_type` and
the `_SDS_CONTROL_TYPE`), takes the slant from rc +0xac, runs
`BaselineAndScale`, then - unless rc +0x90 bit 6 asks for the base line
alone - `AnalyzeLowData` and `exchange`, and writes the slant back.
`TestLowLevelWhole` takes the uou from its trace to its 13 xrs.
`GCTryToRecognize` calls it: with a cursive letter set, "ton" written on
the Notepad is cut into 13 xrs and "to" into 8 (`NEWTON_TRACE_CURSIVE=1`
prints each xr's type and height), and the reader stops one layer up.

### The xr reader (`recognition/XrMatrix.h`, `XrReader.h`)

`xrw_algs` (0x00362f08) is the layer above the low level: a word's xrs
read into a *word graph* (`RWG_type`) - what the writing may say, as a
list of symbols with, for each, the xrs it was read from.

**The matrix** (`XrMatrix.h`, `xrcm_type`) is how one letter is matched
against the xrs.  The letter table (the DTE, `ParaGraph.h`) says how each
letter may be written: up to sixteen *variants*, each a string of up to
twelve *prototype* xrs of 0x4c bytes - a skip penalty in byte 3, a flag
in byte 2 (bit 7: this prototype only reads an xr next to a break), then
nibble tables of what each xr type (64), height (16), shift (16), link
(16) and direction (32) is worth at that point of the letter.  Matching
is dynamic programming along the xr string: a *line* is a value per xr
position, the best score of a path that has read so far and ends there;
each prototype is one column, `CountXrAsm` turning the line before into
the line after - at each position the best of skipping the xr (the
position before, less the xr's penalty), skipping the prototype (the
input less the prototype's penalty) and the diagonal (the input's
position before, less 50, plus the sum of the five nibbles), a tie going
to the diagonal.  `CountXrAsm` and `TCountXrAsm` are hand-written
assembly in the ROM (0x0038cd38, 0x003ad244), transcribed register by
register: the prototype's first word is rotated right by eight so its
skip penalty is the top byte and its flag the bottom, and each xr's first
eight bytes are read as two words so the table lookups are shifts of
one register.  `TCountXrAsm` also writes how each cell was reached (1 the
xr skipped, 2 the prototype, 3 the diagonal) - and breaks ties the other
way, towards the earlier of the three.  A variant's lines are merged into
the letter's, each variant charged twice its *vex* (how rarely the
writer uses it, from the descriptor or, with the learning info, the
writer's own); a letter is counted in the case it is written in and,
when the field allows, the other (`CountLetter`); a word is counted
letter by letter, each letter's out line cut to the stretch within 50 of
its best position becoming the next one's input (`CountWord`).  With the
trace (flag 4) the reading is walked back into a *layout*: which
prototype of which variant read which xr (`CreateLayout`).  With rc +0x0a
bit 0 the matrix remembers, for each break, where each letter read from it
ended and what it added, and reads it from there again instead of
counting it (`CountSym`'s cache - the reader turns this off).
`test_XrMatrix` checks the inner loop's arithmetic by hand, and that each
of six letters of the ROM's table reads the xrs its own prototypes read
best (the type, height, shift, direction and link each gives most for)
better than any other letter does - 'o' 144 against 'c' 115, 'm' 206
against 'n' 159 - and "lo" better than "ol".

**The Viterbi** (`xrlv`, `Xrlv.cpp`) reads the word along its
*locations*: the xrs the low level marked as the last of a letter
(`XrlvSetLocations`; with rc +0x0e = 1 only the first and last).  Each
location keeps up to rc +0x10 *partial readings* (`xrlv_var_data_type`,
100 bytes: the letters so far, what each added, each letter's variant
and how many locations it spans, and where the dictionary walk allowing
them has got to), in a *position block* - only as many blocks as a letter
may reach ahead of a location (the most locations within seventeen xrs
of any one), handed on from a location behind to one ahead as the read
moves along.  From each location (`XrlvDevelopPos`) every reading is
offered what may come next (`XrlvGetNextSymbols`):

  - the vocabulary's next letters (`GF_VocSymbolSet`: each dictionary of
    the area's chain the reading still allows walked to the word so far
    with Airus's Verify and asked for what follows with NextSet9, whose
    callback sets a character's bit in a 256-bit set and writes the
    dictionary's state after it; `XrLex.cpp`), with their capitals at a
    word's start when the field allows them;
  - the lexical database's (a lexicon, whose nodes carry strings: Airus's
    `AL_NextSet9`, reconstructed for this), each two cheaper;
  - with no dictionary, the field's character set (`XrlvGetCharset`),
    each letter weighed by the trigram table against the two before it;
  - leading and ending punctuation.

Each symbol is counted from the location once (`CountSym`, the result
kept in a per-symbol cache for the location), and every location ahead
that its out line reaches gets a new reading when the reading's score,
plus what the letter read, less what a letter of its class costs after
the one before (a 7x7 table: other, lower, upper, digit, dictionary word,
math), what the xrs it skipped cost and what ending away from a break
costs, beats that location's threshold (`XrlvDevelopCell`; a full
location replaces its weakest reading).  At a break wide enough for a
word gap a reading may also close its word and start another.  Before a
location's readings go on they are sorted, trimmed to the beam
(`XrlvSortXrlvPos`, `XrlvTrimXrlvPos`) and each last letter is checked
against the line (`XrlvCHLXrlvPos`, 3.4 KB): its box, moved by the
writing's slant (`GetBaseBord`), against the letter before's (do their
bodies overlap where their variants' position nibbles say they should?),
its height against the letters before (what the variants' size nibbles
allow, halved when the boxes overlap little on the line), and its middle
against the line's - up to eight off the score for each.  The last
location's readings become the answers (`XrlvSortAns`: the score over
the word's length, in thousandths, at most 2000; `XrlvCleanAns` drops
duplicates) and the graph (`XrlvCreateRWG`: up to five answers within rc
+0x1a tens of the best, each letter a symbol with its xrs, variant, what
it added and, for a dictionary word, its id), each symbol's xrs found
again by reading the letter alone over them (`XrlvGetSymAliases`, a
traced `CountWord`).  A field that expects one fixed string (rc +0xc0)
skips all that: the string is the graph (`GetCMPAliases`, over
`create_rwg_ppd`).  `test_XrMatrix` reads the ideal xrs of l and o with
the character set alone as "lo" first, then "bo", "eo", "so", "lf".

`GCTryToRecognize` calls it after the low level (with
`SetMultiWordMarksWS`/`SetMultiWordMarksDash` first, which mark the
breaks the segmenter was unsure of): with a cursive letter set,
`cursive.ns`'s "to" comes out of the graph as "to" first, and "ton" as
For, ER, Eon, FR, EN (`NEWTON_TRACE_CURSIVE=1` prints the graph).  Some
ROM quirks kept: the line check takes the size of a single-letter word's
imaginary letter before from a constant (0x2a5778) where only its being
non-nought was meant; the check against the letter two back measures the
overlap against the letter just before; a capital's penalty for the
first letter of a word is booked against its last; the per-symbol cache
runs a line longer than sixteen positions into the next symbol's entry.

### The answers (`recognition/XrAnswers.cpp`)

After the graph, `GCTryToRecognize` turns it into the readings a word
descriptor keeps (`rec_w_type`, 0x50 bytes: the word, each letter's
variant and xr count, the score and the dictionary attribute) and says
which strokes each word of them is.

**The readings** (`MakeAndCombRecWordsFromWordGraph`, 0x0019f644, over
`MakeRecWordsFromWordGraph`): each answer of a list-of-answers graph
becomes a reading - its letters (at most 23), the graph's score for it
(its first symbol's weight), its dictionary attribute (its last
symbol's) - and, scaled against ten per xr less ten, the score is worked
out again from the letters: what each added less what each was charged
(+0x0b, which `EvaluateAndSortAnswers` fills in, and +0x0c), less the
answer's penalty, times a thousand over the scale, plus ten times the
answer's +0x08.  The readings are bubble-sorted best first, the graph put
into the same order (`SortGraph`), the scores divided by ten and held to
0..100, and the first reading that is more than rc +0x1a below the best,
or below rc +0x16, is dropped with everything after it.  (The ROM's code
has a flag it sets to one on entry and tests twice; with it clear a
letter's +0x0b would have counted half and a field of the reading's +0x4c
three times - both are dead.)

**Which strokes each word is** (`FillRecwordSplitInfo`, 0x0019e6ec): a
reading with spaces in it is several words, and the unit that carries it
is later cut into them (`GCWriteRW`).  The split information
(`RecwordSplitInfoType`, 0x5b bytes and one per stroke) holds, for each of
the first five readings, a bit for each letter a word ends at, and each
word's dictionary attribute (0xfd where the graph cut without a space);
then how many strokes each word of the best reading has and which they
are.  The best reading's letters are traced back to the stretches of the
trace their xrs were made from (`connect_trajectory_and_answers`/
`_letter`: a run of xrs between breaks is one stretch, a crossing mark -
0x34, 0x36, 0x3a, 0x3b - one of its own, at most four a letter and 0x5b
in all), each stretch to the strokes it runs through
(`AddStrokesOfSymbol`, `GetStrokeNumber`), a stroke no word claimed going
to the word whose box's middle is nearest its own
(`AttachLostStrokeToWord`); the other readings' words come from the
graph (`FillSplitInfoFromRWG` - ROM BUG, kept: it asks whether the symbol
after a letter ends the answer by that symbol's `sym` where its `type`
was meant).  A reading of one word needs none of that: the block just
says one word.  When the letters cannot be traced the block is dropped
and the low level's letter ends (xr attrib 4) cleared - and the stretches
are not freed when a stroke turns out to belong to an earlier word, a
ROM leak kept.  Both are fixed by default now (`NEWTON_ROM_BUGS=1` for
the ROM's behaviour).

**What the learning is given** (`GCFillLearningHandle`, 0x000d6ad4, over
`LHAddEntry`, 0x001059b4): training data is a block of entries - a count,
five words an entry (three ids, the data's offset and size), the data
after - grown by one entry at a time into a new block; by the flags at rc
+0xb2 the parameters ('LDRC' - DEVIATION: the host's block, sized by
sizeof), the trace ('TRAC'), the xrs ('XRD_'), the graph's symbols
('RWS_') and their xrs ('RPPD'), the readings ('RWRD') and the
orthographic learning's information ('ORTL').  The Notepad's words carry
0x20, the readings, which the word domain adds.

**The word domain** (`TXrWordDomain`, `TXrWordUnit`, `XrDomains.cpp`)
takes an STXR unit the reader has read (`Group`: a new 'XRWR' word unit
with the STXR unit as its sub, `TXrWordUnit`, 100 bytes over
`TStdWordUnit`) and gives it the readings (`Classify`/`ClassifyXrWord`:
each reading an interpretation - its word, a score of ten times how far
it is below 100, its attribute as the label, -4 for none), the ink's box
and base line (`GetWordBase`, `GetWordSize`, `GetWordSlant`, in tablet
eighths turned into pixels) and the training data; then, as
`NewClassification` (which the ROM has inlined here), a piece for the
recognisers above - `TWordRecognizer`'s `HandleUnit` turns it into
aeWord, and the page types it.  DEVIATION: the ROM's interpretations are
0x10 bytes; the host's hold pointers and are sized by sizeof.

With a cursive letter set `cursive.ns` now types what it wrote: the page
reads **"ton to"** (`NEWTON_TRACE_CURSIVE=1` prints each word's answers:
"ton" 85 ahead of "tor", "For", "for", "Ion"; "to" 84).  It read "For to"
until FillSHR's shift classes were put right (below, "Joined-up
writing"): with them negated "ton" was not among the five answers `xrlv`
left in the graph at all.  The capitals were never the cause - the
Notepad's field has rc +0x1e = 0x3f (`SetXrWordFieldType` gives a letter
set of style 1 all six bits, checked against the disassembly), and words
made of the letters' ideal xrs read as themselves, in lower case, with
those capitals allowed (`test_XrMatrix`'s `TestIdealWords`).

**The rules' whereabouts** (`recognition/XrRules.cpp`, the first piece
of `EvaluateAndSortAnswers`): the prototype data (the letter table's PDF
part, `CreatePDFHeader`) keeps a rule for a character's variant, and for
that variant next to another character, behind three levels of header -
each a bit set of which children there are (numbered from the top bit of
the first byte, `pdfMaskArray`) and the offsets of those there are, in
bit order, a child's slot being the number of set bits before its own
(`PDFReturnIndex`/`PDFReturnBitNumber`).  The main header has a bit per
character code (+0x10) and the characters' offsets (+0x30); a character's,
a bit per variant (+4) and their offsets (+8); a variant's, a bit per
neighbouring character (+4), whether it has a rule of its own (+0x24) and
the connections' offsets (+0x28) - its own rule inline after them; a
connection's, a bit per rule and their offsets.  `PDFGetRule` walks
them.  `test_XrMatrix` walks the ROM's own: 87 characters, 374 variants
(373 with a rule of their own) and 63 connections.

### The post-processing (`recognition/XrPost.h`, `XrPostCalc.cpp`, `XrPostEval.cpp`)

`EvaluateAndSortAnswers` (0x00337ee8) runs between the graph and the
readings: it makes unscaled readings of the graph, scores every letter
of every answer again, and moves the letters' +0x0b (what
`MakeRecWordsFromWordGraph` then charges each) and the answers' +0x08
and weights.  Its state is `_POST_PARAMS` (0x90 bytes in the ROM;
DEVIATION: `POST_PARAMS`, sized by sizeof, the ROM's offsets in the
comments).

**When it scores at all** (`EvaluateAnswers`, 0x00337624): only when the
best answer is at least the field's first control (rc +0x100, 60 on the
Notepad) and no further ahead of the second than its second control (rc
+0x102, 10), or it has digits or a negative attribute - so it is there
to settle close calls between good answers, and a weak or clear winner is
left alone - and only with rc +0xaf set.  A list graph is left as type 4
(and a type-4 graph is scored with flags 0xe rather than 0xf).

**A letter's score** (`EvaluateCharQuality`, 0x00335fe0): the letter
before and after it are found in the graph (each an alternative's last
or first symbol when the graph branches, a space counting as none); the
letter table's PDF part may have a rule for the letter's variant, and
one for it next to each neighbour (`PDFGetRule`); each rule is a group
of *queues*, and the letter's score is four times the rules' weighed sum
over the number of queues, plus four times each side reasoning's.

**The rules are programs** (`CalculateQueueResult`, 0x003359bc, read from
the disassembly - the decompiler cannot follow its two switches): a
queue is bytecodes for a stack machine of 15 entries - push a constant
(0x0b one byte, 0x0a two, 0x09/0x03 four, big-endian), push one of the
PDF's constants (0x10, section 1), push a field of one of the xrs the
letter (or the one before or after it) was read from (0x0f/0x07: byte 1
the prototype element, byte 2 the field - the xr itself, left, bottom,
right, top; +5 the letter before, +10 after), arithmetic (0x0c/0x04: add,
subtract, multiply, divide - by nought 10000 - power, negate, and three
*fuzzy* comparisons, `CalculateLess`/`Greater`/`Equals`, answering 0..20
rather than a truth value), and call one of the 74 functions of the
`Functions` table (0x0e one byte, 0x0d/0x05 two; generated by romtable.py
with the bytecode lengths `globalSizeArray`): measures of the trace
between points (curvature, beaks - `FindBeak` over the thinned trace of
`filtr_gid` - chords, closed areas, crossings), the letter's box and its
neighbours' (`FindXrLetterBox`), the base line, whether another letter
of the graph competes for the letter's xrs, ten variables
(`SetInternalVariable`), and `ChangePPDLetter`, with which a rule turns
the letter into another.  A function or a field that fails (an xr the
prototype skipped is 0x11, a pen-up in the way 0x17, ...) ends its queue
with nought.  The weights (the PDF's section 2) are negative: a queue
measures what is *wrong* with the letter, 0..20, and each is floored at
sixteen times the number of queues, so a rule can only take away.  An o's
fourth queue, decoded:

    push 300; push bottom(xr 3); push top(xr 4); subtract
    push 0; push xr 4; XofXrTop; push xr 1; XofXrTop; subtract; Max
    add; multiply; push bottom(xr 3); push top(xr 1); subtract; divide
    push 150; Less

- 300 x (how far the o's right side comes back up + how far its top
drifted right) over its height, fuzzily less than 150.
`NEWTON_TRACE_CURSIVE=1` prints each queue's weight, result and error,
and with `NEWTON_TRACE_RULES=1` its bytes too.  DEVIATION: the stack
holds `intptr_t`s, because some entries are xrs' addresses; the
arithmetic is the ROM's 32 bits.

**The side reasoning**: a word with a mark (a dot, 0x34/0x3b) that none
of its letters makes, or two more marks than such letters, is charged
the table's penalty (`EvaluateWordUsingSideReasoning` over
`EvaluateXrToLetters`, two tables in the initialised RAM area; ROM QUIRK:
the second table's answer is worked out and thrown away); an i, j or
colon with no dot among its xrs, its neighbours' or over it is charged
(`EvaluateLettersToXr`); a letter whose box sits over the one before's
is charged 1 or 4 (`CalculateBoxes_Side_Result`); crosses and dots the
letters' prototypes expect against those the word has, twice the
difference and at most six (`EvaluateMissingCross`); and a reading of
digits whose tops or bottoms wander is charged by how much
(`CheckDigitsLine`, for a field that allows numbers - the Notepad's
does).

ROM bugs kept: `GetXrCorr` adds three whole bytes of the prototype as
well as their nibbles; `CalculateCurvature`'s "middles" are half each
xr's length rather than points within it (these two are fixed by default
now, `NEWTON_ROM_BUGS=1` for the ROM's behaviour); `CalculatePow` multiplies for a
negative power too; `CalculateFunction`'s arguments a function does not
take are whatever the registers held (the host passes nought); an xr
field of a letter that is neither the rule's neighbour nor missing reads
the last one found.  (`ReturnZeroIfDoubleSkip`, once listed here as
always failing because it gives the xr the next type up for a moment and
asks whether the two differ, does not: the xr it is given is one of the
xrs the letter's elements index, so at its own element the bumped type
is on both sides and they compare equal - the bump is an identity test.)

NOT YET, deliberately: `CheckDiacriticsDirections` (0x0007c9a0, 684
bytes, over `AnalyseDiacriticsDirection` 0x0007c130 - 2160 bytes -,
`CurvFromSquare` and `LengthOfTraj`: about 3.2 KB), asked only for a
letter set whose language (rc +6) has bit 2 or 3 - French or German -
which the U.S. ROM's never do, so nothing can reach it; it answers
nought.  (`MakeRecWordsFromGraph`/`MergeTwoRecWordsSets`, once listed
here, are done - "A graph of alternatives" below.)

What it does to the demos: `cursive.ns`'s two words are not scored - each
is too far ahead of the next - so they read as the graph has them ("ton",
once FillSHR was right; "For" before).
`test_XrMatrix` runs queues made by hand and the ROM's rules for an l and
an o.

**Joined-up writing** (`src/host/demo/cursive-joined.ns`, ctest
`host.NewtonCursiveJoined`): the reader was made for a joined hand, so
the demo draws words the way one is written - one stroke, an entry
stroke, the letters' bodies, the joins, an exit, a t's bar after - with
smooth arcs and straight lines: "on", "no", "mum", "to" and "nun".  They
read as **"on" (82), "no" (90), "Mom" (67, "mum" fifth), "to" (86) and
"nun" (76)**.  Two things were in the way, and the story of finding them
is the way to look for the next:

- **A port bug in FillSHR** (`LowXrFeatures.cpp`).  The ROM keeps the four
  xrs that bracket an xr at `[sp+0x194]`..`[sp+0x1a0]` and the fill reads
  them back as an array from 0x194; the port had put the last two the
  wrong way round in every one of its six cases - an extremum was
  { the one before, it, the one after, it } where the ROM has { the one
  before, it, it, the one after }.  The height classes came out the same
  (both heights are absolute differences) but every shift class was
  measured across the wrong pair, which is to say negated: an n leaning
  right looked like one leaning left.  With that fixed the printed-stroke
  demo reads "ton to" (it had read "For to") and the joined "no", "to"
  and "nun" came first.  `test_LowLevel`'s `TestFillSHR` pins it on xrs
  made by hand (a maximum between two minima: height class 9, shift 11;
  the old order gave 4).  How it was found: words made of the letters'
  *ideal* xrs (`test_XrMatrix`'s `TestIdealWords`: the xr each prototype
  gives most for) read as themselves first, capitals allowed or not - so
  the matcher, `xrlv` and the capital handling were sound and the fault
  was in the xrs; `SetXrWordFieldType`'s capitals (rc +0x1e 0x3f for the
  Notepad) and exchange's element-to-xr switch checked out against the
  disassembly case by case; FillSHR did not.
- **The drawing of the o.**  The demo's o went up to the top centre and
  straight down the left, so it had no top arc for the reader to find
  ("on" came back "OR"); a cursive o's join arrives at its top right and
  goes over the top first, and drawn so it reads "on" (82, "or" 71).

"mum" and "nun" are read but **lose their arbitration to the scrub
gesture** (`NEWTON_TRACE_ARBITER=1` prints each arbitration: `'XRWR'/330
'SCRB'/0; won 'SCRB'`): the arbiter takes the lowest score and a scrub
scores nought.  The demo's m and n go down the stem and straight back up
it, arches of a few whole pixels on letters fourteen high, which makes
three or more turns of over 110 degrees that alternate - a zig-zag, which
is what `TestScrub` looks for.  `TestScrub` and `ValidTurnSequence` agree
with the ROM (the corner counts, the 110 and 170 degree limits, the half
turn that makes a loop, the spread of the turns), so this is the drawing:
rounder arches drawn with the pen in whole pixels do not change it.  With
nothing to erase under them the scrubs do nothing, the strokes expire
unclaimed, and they go down as ink words (the page's text holds 0x1a, the
paragraph's ink-word character, for each).  Whether a MessagePad takes a
joined "mum" for a scrub too cannot be checked without one.

### A graph of alternatives (`MakeRecWordsFromGraph`, in `XrAnswers.cpp`)

A graph that is not a list of answers - a fixed-string field's - is one
answer some of whose letters are groups of alternatives: a type 2 symbol
opens a group, a type 4 one comes between two alternatives, a type 3 one
closes it, and an alternative is one symbol or two in a row (the two
given the mean of their scores).  `MakeRecWordsFromGraph` first sorts each
group best first *in the graph itself* (a bubble sort that moves one- and
two-symbol alternatives about round the type 4 symbols), makes the first
reading of every group's best (scored as the mean of its letters'), and
then up to nine more, each the change of one letter of an earlier reading
to the next alternative down that loses least and makes a reading not
yet made (`MakeNewPath`); `FillRecWordsElement` puts each symbol in with
its variant (top bit set for a letter read in another case) and span.
`EvaluateAnswers` makes these readings twice - from a copy with every
letter's score lowered by a hundred (what a number would read as) and
from the graph with the digits' and `+ = %`'s lowered (a word's) - and
`MergeTwoRecWordsSets` merges them best first, no word twice.  ROM bug
kept: a letter the reader read as a *different* letter clears the
reading's first variant and span, not its own (`test_XrAnswers`).  Fixed
by default now, with the word graph's like case (`NEWTON_ROM_BUGS=1` for
the ROM's behaviour).

### Learning (`TWordRecognizer::DoLearning`, `XRWDoLearning`)

A reading the writer settles on (a word picked in the corrector, which
the ROM's `ReplaceWord` script does with the word info's `Learn`) reaches
`DoIndexedLearning`, which finds the recogniser by the word info's unit
id and hands it the word info; the word recogniser turns the strokes into
the engine's trace (`GetTraceFromStrokes`) and gives the word domain the
unit's training data (the 'RWRD' readings `GCFillLearningHandle` kept),
the trace and which reading it was (selector 0x20010).  With learning on
(rc +0x22, from the user configuration's `learningEnabledOption` through
selector 0x20033) `FlyLearn` moves the letter counters: the variant each
letter was read as is set back to nought, the others counted up, and the
letter states follow - which is what `GetLetterWeights` then answers.
Only an entry carrying training data (word info flag 1, which the
paragraph sets through `SetOffsetInfo` when it puts the word on the page)
has anything to teach.  `cursive.ns` reads "ton" again with learning on,
calls `Learn(0)`, and the letter weights stop being the defaults
(`NEWTON_TRACE_CURSIVE` prints `[cursive] learning: ...`).  Host bug found
on the way: `UnitID` read a host ULong out of the unit id, which
`EncodeUnitID` writes as two UniChars.

### The digit reader (`recognition/Chunk.h`)

In a field that allows numbers (rc +0xb6) ParaGraph's "chunk" reader
reads first: `ChunkProcessor` makes a trace of its own (`tag_WORD_TRACE`,
8 bytes a point), its extrema (`ExtrWordTrace_V`) and a polyline
approximation (`GetLineApprox`), cuts it into chunks (`ChunkConstruct`)
and reads the digits (`Digits` over four searchers); when it found a
number, `ChunkModifyRC` narrows the configuration to digits for the xr
reader and `ChunkRestoreRC` puts it back after (ROM bug kept: rc +0x92,
which the numbers-alone way sets, is not put back; fixed by default now,
`NEWTON_ROM_BUGS=1` for the ROM's behaviour).  Done: the context
and the configuration (`ChunkAllocCtx`, `ChunkCleanUp`, `IsChunkNumbers`,
`ChunkModifyRC`/`ChunkRestoreRC`, `ChunkWriteParamCtx`, called where
`GCTryToRecognize` calls them) and the first of its geometry -
`v_MostFarFromChord` (the point furthest from a chord; a flat run answers
its middle), `v_QDistFromChord` (the squared distance, the projection's
quotient and remainder taken apart to stay in 32 bits) and `GetDirection`
(twenty-four fifteen-degree directions counted anticlockwise from up, by
octant and then by products against the sines and cosines of 15 and 30
degrees - `ChunkTables.cpp`, generated).  `test_WordDescriptors`'s
`TestChunkContext`/`TestChunkChords`.

What the searchers work from is done too (`test_Chunk` draws a 4 and a 2
with a synthetic pen, in eighths of a pixel as the tablet gives them, and
follows them through):

- **The trace's turns** (`ChunkTrace.cpp`: `ExtrWordTrace_V`).  Each
  stroke's box first, and the height of the stroke one past the middle of
  them sorted by height (ROM quirk: one past, not the middle); then, a
  stroke at a time, a run from each turn until the pen has moved a
  seventh of that height, when the run's start is marked a turn at the
  bottom (`kTraceLow`, the pen went up from it) or the top
  (`kTraceHigh`), and a turn looked for where the next point goes back.
  The flags are the word at +4 of each 8-byte point.
- **The polyline** (`GetLineApprox`, `SetAllDirections`).  A pen-up marks
  the points either side as a stroke's end and start; the marked points
  are listed and each segment between two inside a stroke is split at
  its furthest point while that is far enough off the chord (up to fifty
  nodes a segment, two hundred passes, two hundred nodes).  Each node
  (`tag_wapx_type`, 0x1c bytes) gets the direction in and out; a turn of
  120 degrees or more is a corner, its two directions packed in a word.
- **The chunks** (`ChunkConstruct.cpp`).  `ChunkFillMainData` cuts the
  polyline at its segments into `tag_CHUNK`s (0x94 bytes: ends, box and
  the nodes of its extremes, chord and bulge, up or down, links to the
  chunks of its stroke) with a chunk of kind 3 for each jump between
  strokes, and tells each node its chunk; `ChunkMakeStrokes` makes the
  strokes; `ApxToBrackets` draws each chunk with *brackets* - lines and
  arcs, cut where the turn changes sign or at a corner, then tidied
  until nothing changes (a flat arc becomes a line, a bracket small
  against the mean chunk is run into its neighbour, arcs are run on
  through a join that keeps the turn) and the hooks at the strokes' ends
  dropped - and `ApxToCLine` classes each chunk by its brackets: 300 a
  line, 400 an arc, 500 an arc and a line (or arcs turning apart), 600
  two lines, 700 two arcs turning one way, 1400 anything else, the
  subclass saying which way it turns; an unnamed pass after it (0x00285bc8,
  read from the disassembly - the decompiler loses it) makes a tail - a
  piece much shorter than the chunk beside it - a line, and counts how far
  the turns along a curve keep one way from each end.
- **The list of low objects** (`ChunkLowObj.cpp`, `LO_*`): three hundred
  objects over a free list, in twenty classes (100..800, 1100..2200) each
  a linked list with a cursor, one class "worked in" at a time;
  `ChunkPutClassesToLO` puts each chunk in as its class, and the
  searchers add what they find.  ROM bugs kept: `LO_Add` with no object
  free answers -1 having switched the class worked in, and
  `LO_GetRealChunkInd`'s count inside a group starts too high, so it
  answers the object's last chunk.  Both are fixed by default now
  (`NEWTON_ROM_BUGS=1` for the ROM's behaviour).

`Digits` (0x0029c94c) is the searchers' driver: the writing's line
(`DefHeightsForNumber`), the chunks put in the list as their classes, the
circles (`GetCircles`), then `SearchDigit_L`, `_K`, `New_SearchDigit_V`,
`_S`, `FindPound`, `Check_4`, two unnamed passes, `CutNumberInDigits`, a
second look at everything found (0x0029ce20), `SearchNumber` and friends,
and a digest of the result.  A digit found is an object of class 1300 in
the list, its value 1400 + the digit (1300 + the digit once a second look
has changed it; 0x15 is the $, 0xffff a digit taken out again) and its
extra how it was found.  Done so far (`ChunkDigits.cpp`,
`ChunkSearchL.cpp`, all from the disassembly):

- **The line** (`DefHeightsForNumber`): the strokes' boxes, the small ones
  dropped or joined to the nearer neighbour, their mean top and bottom
  (the staff's `fTopLine`, `fBottomLine`, `fHeight`), and each chunk's ends
  placed in it - 60 above the middle half, 45 in it, 30 below (the byte
  fields `fZoneStart`/`fZoneEnd`, +0x44/+0x50: the ROM stores a byte there
  in the middle of what looked like a word).  ROM bug kept: the test that
  would join two overlapping boxes is `HWRAbs(0) * 3 > height` - the
  argument a register set to nought - so none ever is.  (Fixed by
  default now: the boxes' overlap is measured; `NEWTON_ROM_BUGS=1` for
  the ROM's behaviour.)
- **The circles** (`GetCircles`): a chunk going down (an arc) and the next
  coming back up, the taller between two thirds and four thirds of the
  line, turning more than eleven steps (sixteen when its ends are apart),
  with the neighbours that carry the turn on counted in and the shapes of
  a 2, 3 or 6 turned away - class 200, its object kept in the chunk
  (`f7C`).  A 0 gives one, a 1 or a 4 none.
- **`SearchDigit_L`**: each curve-down chunk (class 500, value 501) tried
  as a digit's main stroke - a $ (an S with an upright through it, near the
  writing's ends), a 2 or 7 (first turn near the top and well right, the
  line to it heading right), a 5 with its bar as a stroke of its own or
  beside it, a 4 or 9 (the upright and its foot, then which way the start
  heads), a 3, 5 or 9 (a corner at the first turn well above the bottom
  makes a 9); a chunk that does not go down may be a 5's bar.  ROM quirks
  kept: the 4 test asks a direction and throws the answer away; the $'s
  test of the chunk after it compares a boolean with an eighth of the
  height; the 2/7 test writes `*from` before it may still refuse.  Under it
  the searchers' geometry, shared with the others: `direct_suits`,
  `distance_between_directions`, `take_next_point`/`take_prev_point`,
  `x_in_line`, `x_in_curve`, `cross_with_line`.
- **Second looks**: `ThreeToFive` (a 3 whose writing turns back sharply at
  its left - a 5 whose bar was not lifted - becomes 1305; ROM bug kept: the
  two turns it finds are not forgotten between digits - fixed by default
  now), `RecognizeZCCW` (a
  curve down that turns the other way then an arc up: a 2 becomes a 6, an
  8 whose closing line misses its start a 0), with `CheckQIntersec`/`XY`;
  `Check_4` (the "4"s of value 0x605 taken out when too tall or sharing
  chunks with another digit).
- **`SearchDigit_K`** (`ChunkSearchK.cpp`, 0x00289604-0x0028d9d0): the
  digits made of lines and arcs, value 1500 + the digit.  A **#** first
  (four strokes of a few chunks, two level and two upright, each level one
  crossing each upright one, the uprights parallel and apart); then each
  line (class 300) of one chunk: in the first two, an **x** of two
  strokes (level tops and bottoms, the left one going down to the right
  and the right one to the left; the 4s over the same nodes are taken
  out); a **4** in three ways - in one stroke without lifting the pen, with
  its slant just before the upright, or as a bar and a left side found in
  the stroke after the upright (`BarStroke`) or before it (`UprightStart`)
  - of value 4, or 41 when its top is open; failing that a **7** (a stem
  continuing a bar that runs right, 17..20 steps against the stem, or with
  a cross-bar), failing that a **per cent sign** (a slash between two
  small rings).  Each arc (class 400) flat enough is tried as a 4, and
  each curve (class 500) not a stroke of its own as an **8** (paired with
  its taller neighbour, the two crossing).  ROM quirks kept: the ring test's
  "chunk before the arcs" is the first arc itself; the arcs' flatness test
  reads the first bracket however many there are; the 4 of value 44 the
  code writes out is never made.  Under it `find_direct_forward` and
  `find_direct_backward`, the direction to the first node far enough
  along.
- **The second-look pass** (`ChunkSecondLook.cpp`, 0x0029ce20 and the
  twelve statics it runs, 0x0029d428-0x0029fbcc and 0x002a01dc/0x002a0740):
  the digits found (up to thirty) sorted left to right and corrected by
  their neighbours - a low "1" between two digits a comma and a comma
  shorter than three tenths of its neighbour a full stop, a slanting "1"
  taller than the rest a solidus, a straight short ")" with no "(" a 1, a
  "1" before an unmatched ")" a "(", a 7 after a leftmost "(" a ")", two
  "<"s a guillemet, a "-" that is the bar of the stroke before dropped, a
  3/7/")" after an upright through its bowl a "B" or "D", 0s, 1s, 2s, 5s
  and 9s dropped for the unused strokes beside them, and each digit's
  letter-table variant (`DigitVariant`) checked against the field's
  `fDigits` - then `ThreeToFive` and `RecognizeZCCW`, and the digits and
  the gaps between them (class 1200) written out in order as class 1900.
  ROM quirks kept: the solidus test stops the pass at a lone "1" with
  nothing to measure it against; the last digit is never counted as a 2;
  nothing bounds the gaps kept to the room the ROM gives them (on the
  host they run into the sorted digits as on the stack, and stop at the
  end of the block - DEVIATION).

- **`New_SearchDigit_V`** (`ChunkSearchV.cpp`, 0x00296e04-0x0029bba8 and
  the named statics at 0x0028eb9c-0x0028fa14): each chunk that is not a pen
  jump asked, by its class, what it starts - kept in a `tagLocalStuff` (the
  chunk, the two before and after it in its stroke, the writing's height;
  DEVIATION: sized from `sizeof`).  A stroke of four nodes with two lines
  beside it is a **#** (the sign coded 71); two short sections a sign (23
  or 24 - a `>` and a `<`); an upright line (class 300) a **1** on its own,
  a 1 with its flag, a **7** (the hook before it its bar), a "H" (72), or
  a **2** (a hook before and a tail after it closing round); a curve down
  (subclass 502) an **8** (its end back at its hook's start), a 1, a 7 (its
  last bracket turning back, or its bottom right of its top), a **9** from
  the stroke two before, else a 2; an arc down (402) a **6** (an arc up
  closing it) or 9 and a 2, the **0** or 9 of the circle `GetCircles`
  found there, the horseshoe `DgtFromDnHorseshoe` (4, 5, 9), and signs 81
  and 99; an arc (401) and a curve starting a short stroke (501)
  `DgtFromAloneDnArc` (7, 9, 5, 2, 8); an S (702) a 2, 5 or **3**; three
  brackets (1400) a 3; a grey 9 (class 2200) from `GreyDgtFromELink`.
  `ComposeTrace` turns a run of nodes back into a trace for
  `v_MostFarFromChord`.  ROM bugs kept: the horseshoe's bar is read from
  the chunk `first` places after the one looked at (the index added to the
  chunk's own address, not the array's; DEVIATION: past the array's end
  the host takes it as not a line), a direction from (x, x), a circle's
  tail compared with half its width, a curve's height measured from node
  `fKind` (all four fixed by default now, `NEWTON_ROM_BUGS=1` for the
  ROM's behaviour).  DEVIATION: two tests read the chunk two after without asking
  whether there is one, and the ROM reads a nil pointer's fields from low
  memory; the host reads them as nought (`kNoChunk`).
- **`SearchNumber`** (`ChunkNumber.cpp`, 0x002a28d0-0x002a3ef8 and five
  statics from FindPound's range): whether the writing is a number, from
  statistics over the digits the second looks wrote out (class 1900) -
  digits and other codes counted by kind with the chunks they took, the
  7s and 0s that run on, how heights, tops and bottoms step from one to
  the next (0 steady, 1 a little, 2 much), characters too wide, small
  strokes written below the one before - and a verdict weighing the
  chunks the digits took against the rest, which any mark of 0x65 (101)
  rules out; on the way signs coded 13 that are really part of a
  neighbouring stroke are taken out (`CheckSignStroke`/`StrokeBeside`),
  and with staff f54 set a line read as a 1 that another stroke crosses
  (a t or f) makes it no number.  A number of nothing but 1s is ruled out
  by any step in its bottoms or heights; a single digit is never a
  number.  ROM quirks kept: a character wider than two and a half times
  its height is marked 0x65 and at once remarked 2 (wider than twice), so
  width alone never rules a number out (fixed by default now:
  `NEWTON_ROM_BUGS=1` for the ROM's behaviour); the walk that skips deleted
  objects answers a deleted one at a list's end; with no other stroke to
  look at, the sign check answers the caller's register (the last real
  chunk) and "takes out" the digits of the stroke numbered by the
  caller's r9 (a pointer, which no stroke index equals).
- **`FindPound`** (`ChunkPound.cpp`, 0x002a3ef8-0x002a4608): a bar (a sign
  coded 13) that is the last thing written, or in the second stroke, with
  the stroke before it for a **pound sign** (value 1570): the stroke
  falling steeply, turning left at a foot that lies below the crest of a
  wavy base, the bar across its middle, up from its foot.  ROM bug kept: a
  direction past the last is brought round by 23, not 24 (fixed by
  default now, `NEWTON_ROM_BUGS=1` for the ROM's behaviour).

`test_Chunk` draws the digits with a synthetic pen: a 5 with a separate
bar and a 5 in one stroke and a $ are found by `SearchDigit_L` (the 2, 3,
4, 7, 9 and 0 drawn there are other searchers' work), an unlifted 5 is
turned from 3 to 5, a 4 laid over another digit is taken out;
`TestSearchK` finds a 4, an x, a 7, a #, an 8 and a % (and no 1 - that
is `New_SearchDigit_V`'s), and `TestSecondLookPass` checks the order and
gaps written out and seven of the corrections; `TestSearchV` finds a
drawn 1, 0, 2, 3, 6, 7, 9, #, < and >; `TestSearchNumber` runs L, K, V,
`Check_4` and the second looks as `Digits` does and judges 42, 10, 217
and 11 numbers and a lone 2 and stepped 1s not; `TestFindPound` finds a
drawn pound sign, its bar the minus `SearchDigit_S` reads.

- **`SearchDigit_S`** (`ChunkSearchS.cpp`, 0x00290ed8-0x00296e04, 45
  unnamed statics, all from the disassembly): the searcher of the signs,
  the small marks and the digits of arcs, value 1600 + the code.  It marks
  every chunk unused (`f6C`) and runs an **arcs pass** first (each arc
  going down with what follows it in its stroke: a **0** from two arcs of
  a size, a **6** or **8** from a tall arc and a smaller one - which one
  by where the second's top is - a **9** read backwards, a **per cent
  sign** when a slash and a matching ring are the strokes either side, a
  grey 8, class 2200, from three stacked arcs), then **chunk by chunk**
  over what is left: an **@** (three to six chunks ending in two arcs
  round the outside), a **(** and a **)** (at least as tall as the line,
  narrow, with a tiny hook at most), a **9** (a short arc up and a taller
  one after it), a **bar** - short and wide - which with the stroke before
  or after it is a **5**'s bar, a crossed **7**'s, a **+** (the upright
  through its middle) or a **minus**, and otherwise a bar kept for later
  (class 1600, 1613), and a **3**.  Then the **small marks** by chunk: a
  dot kept for later (1600, 1614), a **comma**, a **solidus** (taller by a
  quarter than the strokes either side, each wholly to its side).  The
  writing's line is worked out again from the digits found, and four
  passes settle the marks kept for later: two marks stacked are an **8**,
  a per cent sign or a **colon**; a bar a **minus** or a **full stop** by
  its width against its neighbours; a small ring low a full stop and one
  half the line tall a **0**; a dot low a **full stop**.  Its chunks are
  marked 10 (13 for an arc read with the one before it).  ROM quirks kept:
  `HWRAbs(0)` in the arc-pair span tests (so only a chunk under four high
  skips the end test); two tests of a 9's tail demand a foot at least a
  third and at most a sixth of the span below the loop at once, which
  almost never is; the three-chunk @ asks for a chunk *kind* of 701,
  which no kind is; the minus found after the other stroke is never put
  in; two passes return at once leaving the list's work class
  unrestored; a digit put in whole is given the digit again as its extra
  (the callers load the character from beside their code - '@', '9',
  '3' - and it is never read).
- **`Digits`** whole (`ChunkDigitsMain.cpp`, 0x0029c94c and its statics):
  after the searchers, **the doubtful digits taken out** - a 7 whose
  neighbour crosses its middle is put in again with that stroke (a
  crossed 7), a 1 with a stroke across or over it and a 2, solidus or 81
  lying under another stroke's middle third or cut by it go
  (0x002a1320), and of digits one inside another or overlapping, the one
  that does not belong (0x002a0b38); S's doubtful 3s and wide dots too
  (0x002a2758) - then **`CutNumberInDigits`** (0x002a75b0): the real
  chunks gathered into strokes and the strokes into cells of up to three,
  a stroke joining the cell when it overlaps it by two fifths of its
  width, starts near where the stroke before began, or stands tall and
  narrow over its end (0x002a7168), each cell put in as class 1200 - the
  gaps the second looks place between the digits.  After the second
  looks, **the verdict**: with staff f50 set only digits written their
  usual way count (0x0029ccd4); otherwise an **area code in brackets**
  ("(" digits ")", the brackets together a half to a fifth of the width -
  0x002a1a98), a **lone #** (0x0029e888), a **lone digit written its
  usual way** (a 2, 3, 6, 7, 8 or 9 in one stroke, a 0, 1, 4 or 5 in two
  - 0x002a19ec/0x002a1e98), or `SearchNumber`.  The characters are handed
  back as `tagNumBox`es (0x002a2078: the character and an alternative -
  '&' for an 8 that starts going up - the box, and the height and trace
  points as big-endian halfwords), each stroke filed as a digit's (class
  2000) or into a run of other strokes (class 2100, handed back as point
  pairs - 0x0029fbcc), and the number thrown out again when its digits
  spell a **word the reader is known to take for a number** (0x002a0d74:
  a 1 and a one-stroke 5 is "is", 9009/9004/900 "good"/"goo", 7. 7- .7,
  9// and more) or hold a **colon** (0x0029ffc8).  The answer is 1 a
  number, 3 a number with nothing else written, 0 none.  ROM quirks kept:
  the bracket test counts brackets from the entry *after* the one it has
  just filled, which is stack garbage (DEVIATION: nought on the host), and
  a code of 400 or more keeps the character before it, the first one's
  being a register (DEVIATION: nought); three verdict passes return
  leaving the work class unrestored.  `DigitChar`, the chain from code to
  character the ROM writes out eight times over, is one function in
  `Chunk.h`.
- **`ChunkProcessor`** (`Chunk.cpp`, 0x002a6b50): in a field allowing
  numbers, the letter table asked which variants of each digit the field
  allows (`GetVariantState`, when it uses the learning info), the points
  copied into the reader's own trace (scaled down to under 200 high), the
  trace's turns, polyline and chunks, and `Digits`; the context keeps the
  characters, the runs, the scale and the verdict.  In a field of kind 1
  with flags 2 a lone digit is the answer whatever `Digits` said.

`TestSearchS` reads a full stop, minus, colon, +, 5 and crossed 7 with
their bars, both brackets, solidus, comma, 0, 6, 3, per cent sign and @
from drawn writing (and no 9 or 1: those are the other searchers');
`TestDigits` runs the whole of `Digits` - 42, 10 (the 0 V and S both read
written out once), 217, 11, 2, 5, 1.1, (42) and 15 are numbers, 1:1, "is"
and digits beside a letter are not; `TestProcessor` reads "42" and "10"
from points as `GCTryToRecognize` hands them over.

#### The merge (`recognition/ChunkMerge.cpp`)

What the digit reader found becomes the word's readings in three steps
around the xr reader, all read from the disassembly (the decompiler takes
the sort's registers for code pointers):

- **`ChunkPatchXrdata`**, after the low level: the xrs are cut down to
  what is not a digit.  For a number alone that is nothing - two breaks
  (the first xr made a break of height 7), so the xr reader, which wants
  three xrs, reads nothing (`ChunkModifyRC` has already narrowed the
  configuration so that the low level makes none).  Otherwise each run of
  strokes the processor found not to be digits (its point pairs) keeps
  the xrs that lie in it or across its ends, the runs ended by breaks
  that carry the height, shift and orientation of the next xr, so that
  the xr reader reads the letters beside the number as words of their
  own.
- **`ChunkSortAnswers`**, over an unnamed sort (0x002a4c04): the digits
  and whatever the xr reader read put together in writing order.  Each
  letter's box is worked out from the xrs it was read from (0x002a4a34),
  a letter other than an x is read as a digit where it can be (an o as a
  0, anything else as the first non-letter the other readings have in
  that place - the reading as it was becoming the second), and each is
  put among the digits where its middle falls (a comma or full stop by
  its left side).  A '(' ')' pair written the wrong way round before the
  first character is turned about, a lone '«' with two '>'s after it (or
  a ')' and a '>') becomes '«...»' and the other way round, and a
  bracketed four-digit run gets its brackets ("(ddd1" is "(ddd)").  The
  first reading is then the characters, weight 100, and the second the
  alternatives, weight 90.  "1)" or "12)" is taken for a list item and
  left alone by the check that follows.
- **`ChunkCorrectByLexDB`**: the reading walked through the lexical
  database a character at a time.  Each character has its confusable
  alternatives, itself first (0x002a5414: 1 as / ( ), 7 as ), c and C as
  ( 1, Z r z as 2, ( as 1 /, ) as / 7, / as 1 ( ), . as , -, , as ., ' as
  -); each is tried against what the database says may follow, and when
  none fits the walk goes back to the last character that had another to
  try (a stack of 32 choices, 0x002a5574/0x002a55bc, growing down and
  quietly dropping what does not fit).  A walk that ends on a whole word
  of the database makes that word the reading (weight 100, its id and
  attribute at +0x4a/+0x4c), the reading as written the second (50);
  one that runs out of choices leaves the reading at 99 and id -3.  Then
  a reading as long as the number is tried as a date with a '1' read for
  a '/': d1d, d1dd and dd1d, dd1dd, d1d1dd, d1dd1dd and dd1d1dd, dd1dd1dd
  - the month 1 to 12, the day within the month by the ROM's table
  (`kChunkMonthDays`, 0x0037ae10, generated: February has 29), the year
  0 to 99.  Where a form reads both ways ("1111"), the heights of the
  second and third characters decide which '1' is the slash.  The long
  forms go in place of the first reading (the others move down, ten
  lighter each), the short ones as the second - "217" is offered as
  "2/7".  Last, a reading with one x, not at its start, gets a space
  before it.

`GCTryToRecognize` calls `ChunkProcessor` where the ROM does, and
`FillRecwordSplitInfo` splits a number's words by the digit boxes of their
characters (0x0019e9e8) - which can only come from the x rule, and which
counts the boxes by the characters of the reading, so after the space the
x rule inserts they run one behind, as they do in the ROM.
`src/host/demo/numbers.ns` writes "42", "10" and "217" with the cursive
letter set: the page reads "42 10 217" (ctest `host.NewtonNumbers`).

### The orthographic learning (`recognition/Ortho.h`, `Ortho.cpp`, `OrthoDB.cpp`)

When a field asks for it (rc +0xb2 bit 6, which the domain parameter
0x20037 sets together with rc +0xb8 bit 3 - `SetUserConfig('bigLearningEnabled,
true)` does it for the Notepad, as `cursive.ns` shows) the reader records,
for every word it reads, which xrs each letter of its word graph came
from and so which stretches of the pen's trace made it: a *learn array*
(`ORCreateLearnInfo` over `OrtoCreate`/`OrtoEntries`, called from
`GCTryToRecognize` where the ROM calls it) that goes into the word's
training data as its 'ORTL' entry.  It is a big-endian block - its size,
where the parts start, the maximum entries and parts, the number of
runs of symbols the graph had, the entries and parts in use, then four
bytes an entry (first part, last part, graph symbol, character) and the
parts after them.  When the writer settles on a reading
(`XRWDoLearning`) `ORTraining` walks the array for the run of letters
that spells the word and trains each letter's own points into a
database of letter shapes (`TrainTrajectory`).

The database is 0x6000 bytes, big-endian too: a header (0x71 at +0, the
number of classes at +6, its size at +8, the bytes in use at +0xc), then
12-byte *classes* (a letter written with so many strokes, and where its
samples are) and 20-byte *samples* (the letter, its class, and fourteen
bytes of shape).  A shape is the letter's trace normalised to its box
(`TraceToOdata`), resampled at sixteen points evenly along its length
(`ResetParam`, `Repar`), put through a sixteen-point DCT each way
(`FDCT16`, over the `_2C16` cosine table from `romtable.py`), the first
seven coefficients after the constant kept for x and for y, normalised to
unit length and brought into a byte each.  Training looks the sample up
(`SearchInDataBase`: every sample within a box of the new one, widened
until something is found, then again within the square root of the best
distance, gathered per letter into an answer list), and *Occam* decides
whether it is worth keeping - it is added when the nearest letter is
another one, or this one only just nearer than the next.  ROM bug kept:
`Occam` reads the first entry of an answer list that may be empty
(now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour): an empty list's sample is kept).
DEVIATION: the ROM's `SDiv` by nought answers whatever the divide
routine leaves; the host answers nought.  The learn array's +0x14 is a
cached pointer to its parts, which a host pointer does not fit in; the
host leaves it nought and works the parts out each time, as `OrtoTraining`
does itself.  `test_Ortho` checks the DCT (a constant and a ramp, there
and back), a letter made a sample (the same whatever its size), the
database's adding, searching and Occam's verdicts with hand-made o's, c's
and l's, and a learn array built from a word graph of "to" and trained;
a live `cursive.ns` run makes a 21-entry, 4-part learn array and trains
the database to three classes (`NEWTON_TRACE_CURSIVE=1` prints both;
ctest `host.NewtonCursive` looks for the lines).

`AL_NextSet` (0x0002c214, Airus selector 8 for a lexicon) answers the
characters that may come next after the node reached, as one string in
the block's word buffer: each child's character set (a string, in a
lexicon) run together by `AL_NextSetCB`, terminated, and each character
then kept once (`AL_FilterString`).  `test_Airus` walks a small lexicon
with it.

A field's own lines reach the engine through
`TWordRecognizer::ConfigFromFrame` (which `ConfigureArea` tail-calls
after reading lineAtATime): a configuration's `rcBaseInfo` frame (base,
smallHeight, bigHeight, descent - `FromObject`) becomes the 'STXR'
domain's word geometry (`GetWordGeom`: 10, 100, the base line twice and
the small letters' top twice, in the tablet's eighths of a pixel by
`gTabScale.y`), and an `rcGridInfo` frame with a spacing (boxLeft,
boxRight, xSpace, boxTop, boxBottom, ySpace) its grid (`GetGridGeom`:
the first box's corner and the step to the next, 16.16), which is what
boxed grouping reads.  ROM bug kept: `FromObject` masks the heights and
the spacings with 0xff before taking their high byte, so each keeps only
its low byte (now fixed by default (`NEWTON_ROM_BUGS=1` for the ROM's behaviour)).  The domain's selectors are 0x2000b/0x2000d to ask the
geometry and grid and 0x2000c/0x2000e to set them (the host had the pairs
the wrong way round until ConfigFromFrame used them - the ROM's memcpy
takes the destination first).

## The Rosetta engine (`recognition/RosRecognizer.h`, `Rosetta.h`)

The engine the MP2x00 reads printed writing with is Apple's own
**Rosetta** (the cursive recogniser is ParaGraph's - see the section
above; this page once took Rosetta for ParaGraph's Calligrapher). It is a
subsystem in its own right, and a large one: about two hundred kilobytes
of code in some three hundred and fifty functions, with trained tables
beside it. Its layers, from the top:

| | what | where |
|---|---|---|
| 1 | `TRosRecognizer` — the `TWRecognizer` implementation | 0x001b5bac–0x001b7120 |
| 2 | `Rosetta*` — the fifteen calls the recogniser makes into the engine | 0x001b7120–0x001b8500 |
| 3 | `WordRecog*` — the word recogniser | 0x00272728–0x002766c0 |
| 4 | `CharBox*` — the boxed-character recogniser | 0x000561c8–0x00056a44 |
| 5 | `SL*` — the stroke lists they work on | 0x001fff98–0x00201320 |
| 6 | `low_type`/`EXTR`/`SPEC_TYPE` — the feature extraction and the classifier nets | 0x002a8090–0x0032fd24, and more |

Everything at level 1 is the Newton's; everything from level 3 down is
ParaGraph's, and the names are theirs — `neibour_susp_extr`,
`is_umlyut`, `glitch_to_super_min`, `Errorprov`. Level 2 is the join,
which is why `recognition/Rosetta.h` draws it explicitly: it is where a
modern recogniser would be put in instead.

**All of it is reconstructed, and it reads.**  The host installs
`TRosRecognizer`, and a word written on the Notepad comes back as typed
text: `src/host/demo/write.ns` writes "ton" and "to" and the page shows
"ton to".  `test_Reading` draws letters with a synthetic pen and checks
eight words ("to", "tin" and "ton" come back first, the rest within the
first two readings).  `NEWTON_TRACE_ROSETTA=1` prints each stroke that
goes down and the ten readings that come back.

Level 6 in the table above turned out not to be Rosetta's: nothing
reachable from `RosettaClassify` calls into it
(`analysis/callgraph.py build/MP2x00US RosettaClassify --through-done`
lists everything that is).  What the classifier is shown is the four
patternizer groups, and those are level 4's.

Running it for real found four bugs in the reconstruction, each worth
knowing about when writing more of it: two arrays of pointers or `ULong`
sized at four bytes a piece (`WordRecogAllocate`, the `LELTranCache`)
where the host's are eight; `RenderLine` missing the second
coordinate's step back when a line reverses (the decompiler had dropped
the instruction at 0x001a6fb8, and the render wrote one byte into the
header of the heap block after its bitmap); and `StrokeDestroy` not
answering early for nil.  `test_Reading.cpp` keeps the tools that found
them: a crash handler printing a symbolised stack, and a heap walker
(`ROSETTA_HEAPCHECK=1`).

### What TRosRecognizer does

It is thin. Strokes come down from the word domain, are copied out of
the tablet's packed samples into arrays of `FPoint`
(`AllocateAndConvertStrokeForRosetta`) and handed to the engine; the
engine calls back through `RosRecCheckWords` with the words it read, and
those go on the unit as interpretations. The grouping is the *engine's*:
`Group` passes every stroke straight down, because the engine decides
for itself where one word ends and the next begins.

Two things it does that are its own.

**Regrouping.** `RosRecCheckWords` is told how many of the group's
strokes the words cover. If that is fewer than the group holds, the
engine has decided the writing is two words rather than one: a group is
made of the first of them and given the words, another is made of the
rest, and the group that was there is invalidated.

**Ink.** Strokes the writer is *drawing* rather than writing go through
`GroupInkStroke`, which tells the engine to group but to read nothing
(`kRosettaDontClassify` = 1) and gathers them into the recogniser's own
array of up to eighty. When the drawing ends — or eighty strokes have
been gathered — the group is closed and handed back whole through
`EndInkStrokeGroup`.

`FindBaseline` is the same trick with `kRosettaBaselineOnly`: the
strokes go down, nothing is read, and the two Points the engine answers
are opened out into the four the caller wants.

### The area block

`RosettaAreaInfo` (0x68 bytes on the Newton) is what tells the engine
what sort of field is being written in: a word of flags, a 256-bit set
of the characters it allows, the line and the grid it is written on, up
to five dictionaries handed over by hand, five characters read as other
characters, and how far apart the letters are.
`AreaInfoFillDefaults` fills it with *everything* — every character
allowed, nothing mapped.

`AreaInfoConfigure` (0x001b62a8) reads a recognition configuration into
it, and the dictionaries are the interesting part. Most of the ROM's
lexicons stand for a **kind** of thing the engine knows how to read by
itself — names, dates, times, numbers, punctuation, telephone numbers,
addresses — so naming one of those only sets a flag and hands nothing
over; the rest go into `fDicts`, at most five of them, and only if the
locale's `rosIgnoreDicts` does not list them. A sixteen-bit dictionary
is never handed over, because this engine reads bytes. The first
dictionary a configuration names becomes the **label** every word read
in that area carries, which is how a reading is later traced back to
what it was read as.

A field that says `rcSingleLetters` is written one letter to a box, and
the engine is told so rather than being given a lexicon; anything else
takes its kinds from the field's `inputMask` instead, and is given the
main lexicon (dictionary 0x1f). `symbolSet` and `removeSymbol` set and
clear bits of the 256-bit character set.

### What is left

Nothing of the engine.  How well it reads has no reference to be
checked against, but one case was looked into: a perfectly round
synthetic "c" comes back with every code under 0.6% (so all the readings
of a word with one in it tie), while the picture the classifier is shown
is upright and unmirrored and a "c" a little narrower than an "o" reads
as "C" or "c" - the net is particular about its c's rather than broken.
There is nothing to train: the recogniser protocol has no training
calls, and the machine learns a writer's words only through the
dictionaries (`recognition/Learning.h`).  Beside the engine is the shape
domain (below, "The shape domain").

The rest of this section is the engine as it was read, bottom up.

Two things found while reading ahead, so that the next piece starts
from them:

* The recogniser at the bottom is a **back-propagation net**.
  `CharBoxNetEvaluate` (0x00056a44) sets the net's inputs from the
  strokes, runs `BPNetEvaluate`, and then maps its 256 outputs through
  the area's character set and two mapping tables, multiplying the two
  scores together where one character stands for a pair. The
  *patternizers* above it (`NetPatternImage`, `NetPatternMulti`,
  `NetPatternHeight`, `NetPatternBase`, `NetPatternCapHeight`,
  `NetPatternCount`, `NetPatternStrokePUD` — 0x00131f5c-0x00133c7c)
  are what turn strokes into those inputs: one per kind of thing the
  net is told about a piece of writing.
* **`BPNetEvaluate` (0x0001a260) is hand-written assembly.** Its inner
  loop is unrolled four ways and entered through a computed jump
  (`(*(code*)(table + (n & 3) * 0x10))(...)`), which Ghidra's
  decompiler cannot follow — it gives up at 0x0001a610 with "Could not
  recover jumptable". Read it with `analysis/disasm.py`, not the
  decompiler.

The bottom of it has been started: `toolbox/FixedGeometry.h` is the
ten routines the engine measures with — points and rectangles in 16.16
fixed point, because a stroke is sampled in eighths of a pixel and
everything is scaled by the tablet's resolution. `OrFixedRect` is the
one worth knowing: a destination that is not a rectangle yet — upside
down, or four noughts — simply *becomes* the source, which is how a
bounding box is started from its first stroke without anyone having to
say so, and an empty source is passed over, so an empty stroke does not
drag a box down to the origin.

Level 5 is done too. The Newton has a `TStroke` already — tablet
samples packed into a handle, in eighths of a pixel, with the pen's
timing — and the engine will not have it. It wants a stroke it can
scale, smooth, sort and measure without asking anyone, so
`TRosRecognizer` copies every stroke into a `RosStroke` on the way down
and the engine works on its own copy from then on
(`recognition/RosStrokes.h`).

A `RosStroke` is a plain array of `FPoint` with its bounding rectangle
beside it and **the middle of its horizontal range** worked out. That
middle is what the strokes of a word are sorted by (`StrokeSort`),
because the engine wants them in the order they sit on the line rather
than the order they were written in; `fIndex` remembers the order they
*were* written in, and `SLSort` puts them back into it before the word
goes back to the recogniser, so that the strokes of a unit still match
the strokes that came down. A `RosStrokeList` is a count, an array of
those and the rectangle round the lot.

Both `FindBounds` calls work the bounds out again only when what is
there is **not a rectangle** — which is how a stroke whose points have
been moved about says so. Whoever moves them leaves the rectangle
invalid, and the next question re-measures. That is what
`ValidFixedRect` is for, and it is why an empty rectangle has to count
as valid.

The engine's memory is all `NewNamedPtr` tagged `'RoCK'`, and its
`free` is a branch to `DisposPtr` — the Newton's pointer heap, not the
C library's.

**Tidying a stroke.** The tablet reports the pen on a grid, so a slow
stroke arrives as a staircase, and `StrokeDeQuantize` takes it off in
passes of two steps. `StrokeSmooth` moves every point by `weight`/4 of
its *second difference* — the point before, minus twice itself, plus
the point after — leaving the two ends where they were; a negative
weight therefore smooths and a positive one sharpens.
`StrokeConstrain` then pulls every point back to within half a
tolerance of where it really was. Smoothing moves the points, the
constraint says how far they may go, and each pass rounds the steps a
little more without letting the stroke wander away from what was
written. `StrokePreprocess` is what a stroke goes through before the
engine looks at it: dequantised when asked, smoothed when asked, and
always a copy — the caller's stroke is never the one handed back.

**Keeping the pieces of a cut stroke together.** `StrokeSortFrags` is
the sort the word recogniser uses once the engine has started cutting
strokes up. When a stroke has been cut in two the pieces must not be
separated, however their own middles fall — they are one piece of
writing — so the array is first gathered into groups (a stroke whose
`fJoinsNext` is set carries the one after it into the same group),
each group is measured as a whole, and it is the *groups* that are
sorted and written back out flat. Nothing at all is done unless the
array begins and ends at a group boundary, which is the ROM's way of
saying "these strokes are a whole word".

That is also what the two flags on a stroke mean. `fFragment` (+0x26)
says the stroke is a piece cut off something before it, and
`fJoinsNext` (+0x27) that the stroke after it is the rest of this one;
so the first piece of a cut stroke carries `fJoinsNext` and the later
ones `fFragment`, and "either flag" is exactly "the engine made this
stroke itself", which is what `WordRecogClearStrokes` asks before it
gives one back and what `WordRecogIsStrokeTooWide` asks before it cuts
one again.

`StrokeCentroid` is the average of the points, which is *not* the
middle of the box: a stroke that lingers at one end has its centroid
pulled that way, and each point is divided by the count before it is
added so that a long stroke cannot overflow.

### The word recogniser's state (`recognition/WordRecog.h`)

Level 3 is where a piece of writing lives while it is being read, and
all of it — the strokes coming in, the segments they are cut into, the
readings coming out, and the running measurements of the hand that
wrote them — is one flat block of **0x208 bytes**. There is exactly
one, made when the engine wakes (`RosettaAwaken`) and destroyed when it
sleeps, and every layer of the engine reaches into it at fixed byte
offsets. `WordRecog` gives those offsets names as far as the evidence
goes; a field still called `fFieldNNN` is one nothing reconstructed so
far reads.

It is made in two halves, and the split is the interesting part.
`WordRecogNew` allocates the block and nils **exactly** the ten
pointers `WordRecogDeallocate` gives back — nothing else, so the rest
of the block is whatever was in the heap until `WordRecogCreate2`
writes it. That is what makes a throw part way through making one safe:
the handler deallocates, and deallocating touches only the ten fields
that were nilled. The same pair is what `WordRecogSuspend` and
`WordRecogResume` are: while the engine is quiet the arrays are handed
back but the block itself does not move, so everything pointing at the
recogniser goes on pointing at it. `Resume` asks five of the arrays
rather than the flag alone, so a recogniser that still has its memory
is left alone even if it thinks it is suspended.

`RosettaAwaken` makes it with
`WordRecogCreate2(nil, nil, RosettaCheckWords, 10, ROMGrammar, theNet, 1)`:
**ten readings**, the engine's own grammar, and the strokes are the
engine's to free. That last argument is the one to watch —
`WordRecogClearStrokes` gives a stroke back only if the recogniser owns
them all *or* the stroke is a fragment the engine cut for itself; the
rest belong to whoever handed them over.

**The run.** `fRun` is twenty-two numbers describing the writing as it
is being read: **nine Gaussians and four lengths**. Each Gaussian is a
pair — the mean of what has been measured and the mean of its square,
which is all a classifier needs for a distribution whose spread grows
with its mean. One of the nine is how big a single stroke is; the other
eight are four measurements (the gap in front of a stroke, that gap as
a fraction of the writing's size, and both again in the direction the
writing runs) times two situations, a gap *inside* a letter and a gap
*between* letters. `WordRecogAddStroke2` learns each an eighth at a
time and holds it within a quarter of what ParaGraph trained. The four
lengths are the height of a word, the cap height and two letter widths,
and they are what `WordRecogDetermineMaxHeight`, `ComputeCapHeight` and
`IsStrokeTooWide` measure against.

The four between-letter distributions never actually move: the routine
that updates them works the second moment out from a mean it does not
change (see `docs/curiosities.md`, "Nine Gaussians are what the Newton
knows about your handwriting"). That is reproduced here, bug and all,
and fixed by default (the mean nudged as the within-letter ones are;
`NEWTON_ROM_BUGS=1` for the ROM's behaviour).

`fSavedRun` is the copy to go back to.
`WordRecogInvalRun` puts the run back as it was, `WordRecogSaveRun`
keeps what has been learnt, and `WordRecogReset` fills the saved copy
with ParaGraph's own starting values — every one of them the nominal
cap height (18.85 pixels) times a ratio, and the last five say so out
loud, being the nominal ratio divided by a number of their own. What
the engine learns into that run is in `docs/curiosities.md` under "The
engine learns how tall you write, an eighth at a time".

**The grammars.** `fGrammars` is a list of finite-state machines a word
is read against, and `WordRecogSetContext` picks one **by name** —
which is how a field asking to be read as a date or a telephone number
gets one. The ROM has eight built in (`ROMGrammar`, 0x00366e0c):
General, Date, Numbers&Money, Numbers, Phone, Time, Money and
PostalCode. A reset asks for "General".

**The readings.** `fWords` is `fWordCount` strings with a score each,
and `fCheckWords` is who they go to — `RosettaCheckWords` at level 2,
which turns the raw scores into confidences out of a thousand and
passes them up to `RosRecCheckWords`. When the engine has nothing at
all, `WordRecogReturnWords` puts `FailureString` — four question
marks — in the first slot with the worst score there is and says the
whole of the writing is covered by it, so the caller always gets an
answer. The stroke count it passes up is not the one it was given:
strokes the engine cut for itself do not count, because the layers
above never saw them, and at least one is always claimed.

### The engine's life (`recognition/Rosetta.h`)

There is one word recogniser, `gWordRecog`, and level 2 is mostly
about its life. `RosettaInitialize` writes down the tablet's
resolution and the Newton's callback and wakes the engine;
`RosettaAwaken` makes the common info, a classifier with as many
outputs as there are character classes (134, which `CharInitialize`
answers) and the word recogniser over both — ten readings, the ROM's
own grammar, and the strokes are the engine's to free.

`RosettaQuiesce` gives everything in hand back but leaves the engine
standing: the word recogniser keeps its block, so everything pointing
at it stays good, and `WordRecogResume` makes its arrays again when the
next stroke comes down. `RosettaSleep` is what takes it down, and it
gives the grammar context back only when the index is negative — which
is how a context the engine built for itself is told from one of the
ROM's eight.

`RosettaInitializeValues` is what a word starts from: reading again,
the sentence forgotten, the writing in hand cleared and the boxed
character recogniser given back. Whether the run is put back as it was
saved turns on how well the *last* word was read — over 899 out of a
thousand, the confidence `RosettaCheckWords` writes down each time.

`RosettaVerifyWordSymbols` is a walk over `RosCI->fLegalUse`, and it
says something about the engine: a **space is not a legal character**.
It reads one word at a time and a space is never a character of one.
166 of the 256 codes are legal: printable ASCII and the accented
Latin-1 letters.

### The classifier (`recognition/BPNet.h`, `docs/recognition/bpnet.md`)

At the bottom of reading a word is a back-propagation net, and it is
fixed point and tiny — which is what let it run on a 162 MHz
StrongARM with no floating point at all. A unit's activation is one
byte with 128 standing for nought; a weight is one byte biased by 128,
so a unit accumulates `activation * (weight - 128)`; and the sigmoid is
a 360-byte lookup table, `QSigLu`. There are 1002 units — 384 inputs,
484 hidden and 134 outputs — in one byte array, worked out in order so
that a unit may read any unit before it.

The connections are a *program* rather than a matrix: `newtConnects` is
2392 words, each saying how many connections follow, how far back in
the unit array they start, and whether to take more weight bytes; a
word whose count is nought ends a unit and carries the next one's bias.
There are 619 of those — 618 units and a terminator — which is one of
the three checks that made the format readable (`test_Rosetta` asserts
it).

`analysis/bpnet.py` generates the net's template and its eight trained
tables, and `BPNetEvaluate` is reconstructed: the ROM's is 950 bytes of
hand-written assembly, unrolled sixteen ways and entered through a
computed jump, but what it *does* is a page of C. It is checked
against three numbers the net writes down about itself — 618 units,
90,540 connections and a weight table whose last byte is the last one
touched — all three of which fall out of walking the connection
program and none of which are in it. `test_Rosetta` asserts them and
then runs the net.

The one thing that looked wrong turned out to be the best part of it:
the routine adds 0x03500000 to the weight pointer, and that is the
whole ROM mapped a **second** time, uncached
(`g8MegContinuousTableStart`, ROM 0x100). Ninety-one kilobytes of
weights streamed once would flush the StrongARM's entire data cache, so
the engine reads the one thing it cannot reuse through a mapping that
does not disturb it — a non-temporal load a decade before the
instruction existed. `docs/recognition/bpnet.md` has the MMU table and
the rest, and `docs/curiosities.md` tells it as a story.

### The patternizers (`recognition/NetPattern.h`)

What turns a piece of writing into the classifier's 384 inputs. A
**patternizer** knows how to measure one thing about a stroke list and
where in the net's input array to put the answer; a **pattern** is one
measurement it has taken. They are a little class system written in C
— every object begins with a pointer to its type, and the type is a
name, the size of an instance and eight entry points — and there are
seven kinds, named by the strings in the net's `inputType`.

The ROM's own net has four input groups, and they come to exactly its
384 inputs: a **fourteen-by-fourteen picture** of the writing (196), a
twenty-by-nine grid of where the pen went (180), how wide it is
against how tall (1), and how many strokes it took (7). That is
everything the classifier is shown.

All seven kinds are reconstructed. A scalar patternizer is the simple
case and shows how a measurement reaches the net: the value is nought to one in 16.16, written either
as one byte of brightness when the group is a single input or as a
**one-hot** over the group's cells when it is more than one. The five
scalars are `AspectNorm` (width over height, as a fraction of one and
a half), `StrokeCount`, `CapHeight`, `Height` and `Base` (where the
writing sits against the line, moved up by a half and scaled by seven
tenths so that sitting on the line reads about a third).

**The picture.** `ImageSplatLimited` is the biggest group and it
really does draw the writing: `recognition/Render.h` is the engine's
own renderer, and it anti-aliases by **drawing big and counting**. A
`RenderRec` is a one-bit bitmap with a pen made of eight pre-shifted
stencils, one per position within a byte, so putting the pen down is
an OR and never a shift; a `RenderAA` is one of those at four times
the size with a byte-per-pixel grid beside it, and `RenderAAFlush`
counts the set sub-pixels of each cell through a table (`rat0`..`rat3`
answer, for each of the 256 byte values, what each output cell gains -
a set sub-pixel is worth 15, so a four-by-four block comes to 240).
So the 14 x 14 picture is drawn as a 56 x 56 bitmap with a pen four
sub-pixels across, which is one cell of the grid.

The scale is where the thought is. Each axis wants to fill the grid,
but it is held to at most two and a half times life size and at most
1.6 of what the line's own height would give — and then the two axes
are held to within three times each other, which is what *Splat
**Limited*** means: a lower-case `l` is not blown up into a
letter-shaped smear, and an `m` is not squashed flat. With the scales
settled the writing is centred in the grid and drawn.

**Where the pen went.** `StrokePUD` is the other 180 inputs, and it is
the one that knows the writing is a *movement* rather than a shape.
Every point of every stroke goes into four parallel arrays — where it
was, how far it is from the one before, and whether the pen was *down*
getting there, because the first point of a stroke is a jump and not a
stroke of the pen. The whole length is then divided into twenty equal
steps and the engine walks it at a steady speed, writing down at each
step where it has got to and which way it is going.

A column of the grid is nine cells. Eight are the direction of
travel, spread between two neighbouring buckets by how far between
them it falls — and the buckets **wrap**, because a direction does.
The ninth, the first, is how much of that step the pen was *up*: 255
for a jump between strokes and nought for a stroke drawn on the paper.
That is what the name says.

The angle comes from `ApproxFixATan2Cycles`, which answers in
*cycles* — a whole turn is 0x10000 — as a cubic in the smaller of the
two coordinates over the larger, with the octant added afterwards and
no table at all.

`test_NetPattern` draws a stroke down and a stroke across with a jump
between them, and the pen row comes out as six steps down, seven up
and six across — which is the two strokes and the jump, in the right
proportion.

### The bigram grammar (`recognition/ROMGrammar.cpp`)

`ROMGrammar` is what `BiGrammarsLoad` answers: the eight grammars a
word is read against, and a field asks for one by name
(`WordRecogSetContext`). They are General, Date, Numbers&Money,
Numbers, Phone, Time, Money and PostalCode.

A grammar (`BiGrammar`) is a list of **kinds of word** (`BiGSlice`),
and a kind of word is a *lexicon* out of `gROMDictionaryData` with a
score for being what the writer is writing and another score for every
kind that may follow it. So the bigrams are over dictionaries, not
over characters, which is what the name says. A lower score is better
and 0x7ffe means never; they are the arithmetic coder's, the same ones
`ArProbDecodeLu` turns into probabilities for a reading.

`analysis/bigrammar.py` writes all eight out, both as
`recognition/ROMGrammar.cpp` and as `docs/recognition/grammar.md`.
The Phone grammar is the whole idea in five lines, and
`docs/curiosities.md` has it under "The grammar the Newton reads your
writing against is a grammar of *kinds of word*".

Two details matter for the layers above. The General grammar has six
kinds called `~user` and `~null1`..`~null5` whose dictionary is nought:
those are the slots an area's own word lists go into, and
`RosettaClassifySetup` writes the locked dictionary data straight into
their `fDictionary` field when a classify starts. And the structs are
the ROM's own, from `BiGrammarNew` (0x0003de8c) and `BiGSliceNew`
(0x0003dfb4); the ROM has a debug symbol on every slice, transition
list and weight list, so the generated file carries its names.

#### How a grammar is built

The eight grammars in ROM are read-only, but the engine builds its own
for a field - `RosettaSetArea` narrows the General grammar to the kinds
of word the field expects, or clones one of the other seven - and the
four routines that do the allocating are reconstructed.

Both a grammar and a slice keep their arrays **behind the struct in the
same block**. `BiGrammarNew(capacity)` asks for `4 x capacity + 0x20`
and points `fSlices` at the byte after the header;
`BiGSliceNew(capacity)` asks for `6 x capacity + 0x30`, which is the
header, then `capacity` pointers to the kinds that may follow, then as
many two-byte scores. So each is one allocation and one `DisposPtr`,
and `BiGrammarDestroy` frees the slices and then the grammar. (Nothing
else in the engine allocates that way - it is ParaGraph's habit, not
Apple's. DEVIATION: a host pointer is eight bytes, so the reconstruction
works the sizes and the two places out from the struct.)

Reading them settled two fields the generated tables had left
ambiguous. A slice's `+0x1c` and `+0x20` are the number of kinds that
may follow it and the number there is **room** for: `BiGSliceNew` sets
the second and leaves the first at nought, and they are equal in every
one of the ROM's own slices only because those are full. And `+0x2c`,
which the tables show as 0xff in 36 of the 46 slices, is what a slice
is *made* with - so the other two values mean something: the nine kinds
named `LexicalSymbols` carry nought and `wordlike` carries one.

`BiGrammarCreate` takes a name and never stores it. The register
holding it is overwritten with the capacity before the call to
`BiGrammarNew`, and `fName` is set to nil afterwards; `BiGrammarClone`
does not copy one either. A grammar the engine builds for a field is
therefore nameless, which nothing reads and nothing notices.

`BiGrammarClone` and `BiGrammarModifyContext` are still NOT YET. They
want `BiGSliceCreate`, which takes half a dozen **doubles** among its
arguments - the ParaGraph engine's training interface, showing through
- and the ROM only ever passes zeroes for them, so the argument list
has to be read off the caller's stack before it can be written down.

#### What a grammar's scores actually are

Everything in a grammar is scored, and the scores looked arbitrary
until the two lookup tables `BiGrammarModifyContext` uses were read.
They are **negative natural logarithms of probabilities, scaled by five
hundred**:

    score = -ln(p) x 500

`ArProbEncodeLu2[1]` is 5545, and `-ln(1/65536) x 500` is 5545.2.
`ArProbEncodeLu1[1]` is 3119, and `-ln(1/512) x 500` is 3118.9. An even
chance costs 347. `0x7ffe` means never, which is why it is the largest
score there is rather than a flag.

That is why the whole engine adds rather than multiplies, and why
`ArProbDecodeLu` exists at all: the tables are how it goes between
probability and cost without a logarithm. `ArProbDecodeLu` is indexed
by the score over eight and so reaches to 0x2000, a chance of about one
in 34 million; `ArProbEncodeLu2` is indexed by the probability itself
and covers the first 1/64 of the range finely, `ArProbEncodeLu1` by the
probability over 128 for the rest. `analysis/romtable.py` generates all
three into `ArProbTables.cpp`.

#### Building the grammar for a field (`RosettaSetArea`)

`RosettaSetArea` is what a field's configuration becomes: which grammar
to read against, which characters are allowed, which dictionaries to
look in, and - when the field says so - where its baseline and its box
are.

The grammar comes first. Eight of the `fFlags` bits pick one of the
ROM's seven special grammars outright (Phone, Numbers, Custom2,
Punctuation, Date, Address, Custom1); anything else, **and any field
that names dictionaries of its own**, gets the General grammar. Either
way the engine ends up holding a copy it owns, which is what a negative
`fContextIndex` records - the index is stored as `-(n+1)` so that
`RosettaSleep` knows to give it back.

A named grammar is simply cloned. The General grammar is cloned *and
reweighed*: a bitmask of slice indices is built from the same flags -
each flag ORs in a set of kinds of word, and they overlap, so a field
that wants times gets a quite different set from one that wants only
letters - plus one slice per dictionary the field named, which are the
`~user` and `~null1`..`~null5` slots. That list goes to
`BiGrammarModifyContext` with nine tenths of the probability for the
kinds that are wanted:

* every kind's score becomes a probability again (`ArProbDecodeLu`);
* the wanted ones are totalled, and so are the rest;
* each group is scaled so that it comes to its share;
* the scaled probabilities become scores again;
* and the smallest is subtracted from all of them, so the likeliest
  kind of word in the field costs nothing and the rest are priced
  relative to it.

For a plain letters field that leaves the General grammar's 25 kinds
scored from 0 to 2847 with three of them at never, which is what
`test_Rosetta` pins.

Then every slice's `fDictionary` stops being an index into
`gROMDictionaryData` and becomes the data itself - after any
substitutions the field asked for through `fMap`, which is how a field
can have one lexicon read as another. (DEVIATION: the field is
pointer-sized on the host, because that is what it ends up holding.)

Last come the characters. `RosCI->fLegalUse` is the ROM's own
`rosCharLegalUse` unless the field says otherwise, in which case a set
of its own is made - the ROM's narrowed by the field's `fSymbolSet` -
and given back the next time a field says nothing.

### The engine's own numbers (`recognition/RosEngine.h`)

`RosCI` is the block of trained numbers the whole engine measures
against, and it is **real**: `CharInitialize` (0x00057074) copies the
0x10c-byte template `rosCI` out of the ROM into a block of its own, and
`analysis/rosci.py` writes that template and the seventeen tables it
points at into `recognition/RosCITables.cpp`. The copy is not
ceremony: an area may replace the character set in the block, which is
what `RSfRcl` checks before it gives the block back.

What is in it:

* `fCharParams`, eight numbers for every one of the 256 character
  codes. Table 1 is **how tall the character is as a fraction of the
  cap height**, and it reads like one — 0.96 for `A`, 0.92 for `l`,
  0.45 for `o`, 0.08 for a full stop — which is what
  `WordRecogComputeCapHeight` divides the measured height by.
* `fLegalNet` and `fLegalUse`, 256 bits each: which character codes
  exist, and which the engine may answer at the moment.
  `RosettaVerifyWordSymbols` is nothing but a walk over the second.
* `fCharToNetNode` and `fCharOfNetNode`, the map between a character
  code and one of the classifier's 134 output nodes (`CharInitialize`
  answers that count, and `RosettaAwaken` makes the net that size).
* `fCompoundPart1`/`fCompoundPart2`: a character that is really two
  characters names them here, and `WordRecogNetEvaluate` scores it as
  the **product** of its two parts' net outputs.
* `fCapCaseFlags`, `fCapAltCase1`, `fCapAltCase2` — the capitals hack:
  whether a character has a case at all, and the one or two characters
  it may be read as instead.
* `fMinStrokeSize`, 4.5 pixels: under that a stroke has no shape worth
  talking about, and it is also the smallest cap height the engine will
  believe.

The other layers the word recogniser leans on — the grammars, the
segments, the classifier net and its patternizers — are declared in
`recognition/RosEngine.h` and are NOT YET; the seam is drawn there so
that one layer of the engine can be written at a time.
`BiGrammarsLoad` is the one that is not simply empty: the ROM's answers
`ROMGrammar` whatever it is asked for, and ours answers what it is
handed, so the word recogniser can be driven before that table is
extracted.

**When a stroke has to be cut in two.** The four tests a single stroke
is put are reconstructed with the block, and they are a nice piece of
reasoning about handwriting.

`WordRecogStrokeType` says which way a stroke goes, if it goes any way
at all: vertical if it is more than four times as tall as it is wide,
horizontal if it is more than four times as wide as it is tall *and*
short in its own right — no taller than a quarter of the engine's
small height. That second clause is the interesting one. A long
shallow arc drawn large passes the ratio test but is not a horizontal
stroke, because at that size a quarter of its height is still a
letter's worth of ink. A stroke whose longer side is under
`SegmentMinStrokeSize` has no shape worth talking about and is neither.

`WordRecogIsStrokeTooWide` measures it against what a letter of the
hand being read should be (`fRun[21]`), scaled up when the writing has
turned out bigger than the run expected — three parts of what the
word has measured so far and one of its tallest stroke, against the
small height the run holds. A piece the engine cut for itself is never
too wide, whatever it measures, because pieces are not cut again.

`WordRecogStrokeIntersectsTwoVerticalStrokes` is the question a long
horizontal stroke is put: *does it run through two letters?* One that
does is the cross of a double-struck t, or a line drawn under a word,
and has to be cut; one that runs through only one is part of that
letter. Every stroke in hand is asked twice, once by its first two
thirds and once by its last two thirds, because what matters is
whether an **end** of it is vertical — the middle of a letter can go
anywhere. The stroke not yet taken in is asked as well.

`WordRecogStrokeNeedsFragmenting` puts the three together: too wide
first, then a stroke with no shape of its own is cut, a vertical one
never is (one letter may be as tall as it likes), and a horizontal one
only if it runs through two letters.

**Taking a stroke in.** `WordRecogAddStroke2` (0x00274cf0) is both
halves of the job. Told to close the word, it works the **baseline**
out first — the mean height and the mean foot of every stroke in hand,
answered as a box relative to its own top-left corner and scaled into
seventy-seconds of an inch if the tablet's resolution is known — and
then sorts the strokes, has the segment layer cut them into characters
and hands them to `WordRecogAnalyzeWord`. (Or, when the engine has
been told to group but not to read, answers the word `gROSsegOnly` and
says so.) Then it takes the new stroke: the segment layer is told about
it, it goes into `fStrokes`, and everything in the run is learnt from
it. A dot, and a piece the engine cut for itself, teach it nothing
about size; a gap either side of such a piece is not a gap the writer
made.

Still to do at level 3: `WordRecogAddStroke`, the eight-kilobyte
function above it; `WordRecogAnalyzeWord`, `WordRecogNetEvaluate` and
`WordRecogNetSetInputs`, which read them (the last two are mostly
plumbing into the patternizers, so they want reading with level 6);
and the segment side (`WRSeg*`).

(Since done: the five deferred-recognition natives - `Recognize`,
`RecognizePara`, `RecognizePoly`, `RecognizeInkWord` and
`RecognizeTextInStyles` - answer, below under "Deferred recognition".)
Until then the eleven natives that ask for handwriting — `Recognize`,
`RecognizePara`, `RecognizePoly`, `RecognizeInkWord`,
`RecognizeTextInStyles`, `DoCursiveTraining`,
`UseTrainingDataForRecognition`, `GetLetterWeights`/`SetLetterWeights`
and the letter-shape group — have nothing to answer with.
`RosettaExtension` (0x001b6c3c) is the exception: it answers nil and
always did.

### Cutting joined-up writing (`recognition/Fragment.h`)

Joined-up writing arrives as one stroke covering several letters, and
nothing above can read it until it has been cut.  The **ligature
fragmenter** is what decides where - seventeen functions and about five
kilobytes, run by `FindStrokeFragments` and acted on by
`FragmentStroke`.  It is four passes over the stroke's points.

**The good runs.**  `CalcDeltas` reduces the stroke to one
`(dx, dy, length)` per step.  A step is *good* when it goes rightwards
and does so more than it goes downwards (`IsThisRunGood`) - the shape
of the join between two letters - and `FindGoodRuns` gathers every
stretch of good steps into a list.  A run starts where two good steps
follow one another and ends where two bad ones do, so a single wobble
neither starts nor ends one.

**The cusps.**  A run that turns sharply one way and then back is
really two runs.  `CheckCusps` measures each step against the run's own
direction as the ratio of the cross product to the dot product - the
tangent of the angle between them - and cuts the run where that ratio,
having risen past 0.30, comes back down below everything it has been so
far.  `ProcessCusps` takes the next entry *before* each check, so a run
cut in two is itself looked at again.

**The crossings.**  `CalcXProjection` divides the stroke's width into
eight columns to the pixel and counts, for each column, how many times
the ink crosses it - a step that turns the pen round counted once
rather than twice, so the top of an `n` does not read as two
crossings - and then works out what that count comes to at each point
of the stroke.  `CheckXProjection` narrows each run to the longest
stretch where the count is at its lowest, and throws the run away
entirely when even its best is more than one crossing: a place the pen
has been twice is not a place to cut.

**The break points.**  `DefineBreakPoints` puts the break three
quarters of the way along each surviving run (`FindBreakPoint`), holds
every piece to at least 0.15 of the writing's size, and walks a break
back when it would leave too little at the end of a stroke that is
mostly horizontal.  `FragmentStroke` then cuts the stroke at each break
with `StrokeSubsection`; the pieces share the point they were cut at,
and each carries `fFragment` and `fJoinsNext` so the segment layer can
put them back together again.

The thresholds live in the initialised data (`gFragmentParams`,
`gXProjectionParams`, generated by `analysis/romtable.py`) and the
writer's hand reaches the passes through the global `RUN`, which
`FragmentStroke` copies `WordRecog::fRun` into before it starts.

All of this hangs off the engine's own linked list
(`recognition/RosList.h`) - a doubly-linked list of pointers with a
cursor over two free lists of twelve-byte cells, carved from blocks of
a hundred, with a freed cell marked so that using one after it has been
given back is caught rather than followed.

Two ROM oddities are kept.  In `CheckXProjection`, a stretch that turns
out to be no longer than the best so far is **not** closed - the count
carries on over the values that are not the lowest and into the next
stretch, so two short stretches with a gap between them can be taken
for one long one.  And `ListAppendEntry` answers true whatever
`ListAddEntry` said, so a caller cannot tell that the list was nil.
Both are fixed by default now (`NEWTON_ROM_BUGS=1` for the ROM's
behaviour).

`test_Fragment` writes three `u`s without lifting the pen and gets back
four pieces: the cut lands on the rising right-hand side of each one,
which is the ligature, and the last one's exit stroke is cut off too
because the fragmenter cannot tell it from a join.

### Where one letter ends (`recognition/Segment.h`)

The segment layer is what decides that a run of strokes is *this* many
characters and *these* strokes belong to each. It is 16 KB in 29
functions, and its heart - `SegmentChars` over `SegmentStroke` (which
measures every stroke against its neighbours) and `SegmentMakeSegments`
(which decides where the cuts go) - is NOT YET. What is reconstructed
is what a segment *is* and the measurements the cutting is made of.

A `RosSegment` is 0x2c bytes: a stroke list, the box they fill, and a
few things worth knowing without looking again - whether any stroke in
it is a dot, whether any is a piece of a larger stroke or runs on into
the next, and how big the *smallest* of them is.

`SegmentSetStrokes` is worth a sentence of its own. The strokes are
sorted into writing order and then every run of adjoining pieces is
joined back into one stroke (`SLJoinFragments`, over `StrokesAdjoin`
and `StrokeJoin` in `recognition/RosStrokes.h`), so a segment always
holds *whole* strokes even though the layer that cut them works on
fragments. What it holds is its own copy, which is why
`SegmentDestroy` takes the strokes with it.

`SegmentBoundsDotsEtc` fills the rest in, and its odd measure is
deliberate: each stroke is taken at its *larger* dimension and the
answer is the *least* of those. So a segment made of a tall letter and
a dot is as small as the dot, which is how the layer above notices that
something in the piece is too small to be a letter on its own.

#### The five questions

Every one of the measurements is a small, sharp question about ink, and
each is worth knowing on its own:

* **`SegmentDot`** - is this small enough in *both* directions to be
  the dot over an i? Under `rosCI->fMinStrokeSize`, four and a half
  pixels. Asking twice is the whole point: a stroke four pixels wide
  and forty tall is a stem, not a dot.
* **`SegmentAspect`** - how wide against how tall, each measured
  inclusively so that a single point is one by one rather than nought
  by nought.
* **`SegmentOverlap`** - how much of the line two boxes share, as the
  *mean of the two fractions*. Two boxes lying on each other score
  one; a narrow box wholly inside a wide one scores about a half - all
  of itself and a little of the other. That asymmetry is what lets the
  layer tell "the same letter" from "a small mark sitting inside a big
  one".
* **`SegmentStrokeMinDistance`** - which two points of two strokes come
  nearest, and how near. The search compares **|dx| + |dy|**, not the
  real distance: a square root per pair of points would cost more than
  the answer is worth, and the taxicab distance picks the same pair
  nearly always. The real distance is worked out once, for the pair
  that won, and a pair that coincides exactly ends the search there and
  then.
* **`SegmentCrossed`** and **`SegmentNonTailLinked`** - and given that,
  does the nearest approach happen in the *middle* of both strokes, so
  they cross as the upright and bar of a t do, or at their ends, so
  they merely meet as the two halves of a V do? "The middle" is three
  tenths of the points in from either end (`rosCI->fEndFraction`), and
  "near" is three pixels (`rosCI->fLinkDistance`). Both names were
  nought before this; the two fields are now named in the generated
  `RosCITables.cpp`.

#### A bug in the second of them

`SegmentCrossed` is exactly right: both nearest points must be at least
a margin in from both ends of their own strokes. `SegmentNonTailLinked`
asks the opposite question and gets it wrong twice over. Written out,
"not tail-linked" is

    (idxB in the middle of B) or (idxA in the middle of A)

which is four clauses when it is multiplied out. The ROM tests two of
them - and one of those two uses the margin of the **wrong stroke**:
`idxB >= marginA` where the symmetry plainly wants `idxB >= marginB`.

The effect shows up when one stroke is much longer than the other,
since that is when the two margins differ most. The end of a
forty-one-point stroke touching the eighth point of a twenty-point one
is joined, and plainly not end to end - but three tenths of 41 is 12,
so the long stroke's margin swallows B's point at 8 and the answer
comes back "no". Ported as it stands, with the failing case in
`test_Segment`.  The fix - the default now, `NEWTON_ROM_BUGS=1` for the
ROM's - asks the question as written out, each stroke with its own
margin.

#### The first pass: one stroke against its neighbours

`SegmentChars` is the way in, and the first thing it does is work out
two widths from the height of the writing: how wide a letter is taken
to be (`fCharWidthFraction`, half the height, never under the four
pixels of `fMinCharWidth`) and how near two strokes must come to count
as touching (`fReachFraction`, a tenth of it, never under two). Each
is capped at the same fraction of the nominal 18.85 pixels stretched by
two and a half, so writing much larger than the engine expects stops
getting proportionally looser. A height smaller than half of
`fMinStrokeSize` plus the nominal is not believed at all.

Then `SegmentStroke` runs over every stroke in turn and answers two
questions about it.

**Are this stroke and the one before it part of one letter?** The
measure is `SegmentOverlap` - how much of the *line* the two boxes
share - and there are three thresholds it may beat:

| | threshold | and also |
|---|---|---|
| on its own | `fLinkOverlap` 0.700 | - |
| if they cross | `fCrossOverlap` 0.650 | `SegmentCrossed` |
| if joined elsewhere | `fJoinOverlap` 0.675 | `SegmentNonTailLinked` |

So the more the strokes are entangled the less they need to overlap.
Neither may be a dot, and they must come within the reach of each
other. A link is written on **both** strokes: 3 on this one, and on
the earlier one 1 if it starts a run of linked strokes or 2 if it is
already in the middle of one.

It is worth seeing what that measure does and does not catch. The two
halves of an `x` fill exactly the same span of the line, score 1.0 and
are linked at once. The upright and bar of a `t` *cross* - and are
still not linked, because an upright is one pixel wide: the bar covers
it completely, the upright covers a twenty-first of the bar, and the
mean of the two fractions is a little over a half. A `t` is two
segments at this stage and something above has to put it back together.
`test_Segment` pins both cases.

**May a cut go in front of this stroke?** Never in front of one that is
linked backwards. Otherwise either because there is a plain gap - the
horizontal space to the three strokes before it is more than a letter's
width, twice that if a dot is involved - or, failing that, because all
three of these hold: the strokes are more than a letter's width apart,
they lie side by side rather than one above the other (`fDX > fDY` for
*both* the nearest-of-three approach and the immediately-previous one),
and they share no more than `fBreakOverlap`, half the line.

The indices that pass are collected into a list, which is what
`SegmentMakeSegments` - still NOT YET - walks to make the segments.

#### Three strokes back, and one forward

Both of the neighbour measures - `SegmentMultiStrokeMinDistance` for
the points and `SegmentMultiStrokeMinDistBoundX` for the boxes - look
at the **three** strokes before this one, not one. A letter is often
written in pieces that are not consecutive: the bar of a t and the dot
of an i usually go in after the rest of the word, so the stroke that
belongs with this one may be two or three back.

And there is one case that looks the other way. If the *next* stroke
starts further left than this one does, the writer has gone back to add
something, and the pair worth measuring is the one before this against
that next one. That single test is how the engine copes with a word
being dotted and crossed after it has been written.

#### The second pass: the segments

`SegmentMakeSegments` turns the break candidates into segments, and it
is **incremental**: `SegmentChars` calls it once per stroke and then
once more with `last` set, and it keeps its working-out in a 0x44-byte
block of its own - the one `SegmentQuiesce` gives back, and the reason
that function exists. The block is `SegState` in `Segment.cpp`:

| | |
|---|---|
| `fStart` | the first stroke of the piece being built |
| `fBoundsTo` | how far `fBounds` has been accumulated |
| `fCut` / `fLastCut` | where this piece ends, and where the one before it did |
| `fBreakAt` | how far down the break candidates |
| `fMade` | how many segments have been made |
| `fHasDot`, `fBounds`, `fScratch` | the piece so far |
| `fAspect` / `fPrevAspect` | how wide against how tall, now and before this stroke |

Each call folds the new stroke into `fBounds` and `fHasDot`, works out
the new aspect ratio, and asks whether the piece should end here.
There are three reasons it might:

1. **The first pass said so** - this stroke's index is the next entry
   in the break candidates, and `fBreakAt` moves on.
2. **The piece has got too wide.** The aspect ratio has passed
   `fCutAspect` (1.5), or `fCutAspectWithDot` (1.75) when there is a
   dot somewhere in the piece - a dot has already widened the box
   without being a letter of its own. It must also be growing
   (`fAspect > fPrevAspect`), the stroke must share less than half the
   line with the one before it, and it must not be a fragment.
3. **The piece has too many strokes** - more than five, or six when
   `FragmentLigatures` is set.

A cut may not fall in the middle of a run of linked strokes, so cases 2
and 3 walk back to the last stroke whose link is 0 or 3. Case 3 has a
fallback the other does not: if there is no such stroke at all it cuts
at `index - 1` anyway and **rewrites the links** to make that legal,
which is the engine admitting that a run of six strokes it thought was
one letter cannot be. Case 2 has no fallback and simply falls through
into case 3.

#### What it hands up is a lattice, not a partition

This is the part worth knowing. For a piece running from `fStart` to
`fCut`, `SegmentMakeSegments` does not choose where the letters are.
It emits a segment of the first stroke, then of the first two, then of
the first three, and so on - and then starts again at the second
stroke, and at the third. Three strokes it cannot tell apart come back
as **six** segments:

    (0,1) (0,2) (0,3) (1,1) (1,2) (2,1)

Every grouping the links allow, for the layer above to score against
the classifier and the grammar. A grouping that would end in the middle
of a run of linked strokes is skipped, and `fRealCount` records how
many were - so `fRealCount` counts the letters a grouping stands for
while `fCount` counts its strokes. `test_Segment` pins both: two x's
written as four crossing strokes come back as exactly two segments of
two strokes each, `fRealCount` 1; three upright strokes five pixels
apart come back as all six groupings.

Each segment is made with `SegmentCreate`, given its first stroke,
stroke count and the separation carried over from that stroke, then
`SegmentSetStrokes` (which sorts them and rejoins the pieces the engine
cut), `SegmentBoundsDotsEtc` and `SegmentSetStrokeOverlaps` against the
segment before it. Every stroke gets `fSegment` set to the last segment
it landed in. There is a hard limit of 900, the same as the array
`WordRecog` keeps.

One thing in it is transcribed rather than understood: the per-stroke
`fField24` (`SegmentStrokeData`'s `how`, which takes values 0, 1 and 2)
chooses between the lattice above and emitting one grouping for the
whole piece, and it is `WordRecogAddStroke` - still NOT YET - that
decides what it is.

#### The writer's word spacing, and an extra zero

`SegmentSetWordSpacing` is what the recognition area's spacing setting
becomes. `RosettaSetArea` passes `9 - n` for the setting the area
carries, so the argument runs 1 to 9 with 5 in the middle, and 5 is the
writer of ordinary habits: a factor of exactly one and the middle of
the three thresholds.

It leaves three numbers behind - `gSegWordSpacing`, its natural
logarithm `gSegLogWordSpacing`, and `gSegOnlyThreshold` interpolated
between `MinSegOnlyThreshold` (0.4), `MidSegOnlyThreshold` (0.5) and
`MaxSegOnlyThreshold` (0.7). The logarithm is taken once, here, in
double precision, because what the layers above want is to **add** it
to a score rather than multiply by it. It is the only floating point
in the whole engine.

Below the middle setting the factor ramps gently: `0.15 + 0.85 x (n/5)`,
so the tightest setting still weighs a gap at about a third. Above it,
the ROM computes `1 + 23 x (n-5)/4`, and that 23 is almost certainly an
extra zero: the constant in the instruction is `0x170000` where the
shape of the rest of the routine wants `0x17000`, one and seven
sixteenths (the extra zero is taken out by default now;
`NEWTON_ROM_BUGS=1` for the ROM's curve). The curve it actually produces is

| setting | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| factor | 0.32 | 0.49 | 0.66 | 0.83 | **1.00** | 6.75 | 12.50 | 18.25 | 24.00 |

- a jump of nearly seven times between the middle setting and the one
next to it, where every step below the middle is a fifth or so, and
twenty-four times normal at the loosest. With the extra zero gone it
would run 1.00, 1.36, 1.72, 2.08, 2.44 and join the lower half
smoothly. The logarithm keeps it from being catastrophic - the score
term only runs from -1.14 to +3.18 - but the top half of the writer's
spacing slider does not behave like the bottom half. Ported as it
stands, and `test_Segment` pins the whole curve.

#### Where one word ends and the next begins

`SegmentWord` (`recognition/Segment.h`) is what
`WordRecogAddStroke` asks about every stroke, and its answer says not
only whether a new word has begun but *why*:

| | |
|---|---|
| 0 | the same word |
| 1 | the gap before it was too wide (`SegmentWordXGap`) |
| 2 | the writer went back (`SegmentWordBack`) |
| 3 | the writer went down a line (`SegmentWordVert`) |

The three tests are asked in that order and the first that answers
wins, so a pen that went back is reported as having gone back even if
it also went down.  `SegmentWordBkVt` is the first two without the
third.

All of them take the same twenty-three words - the ROM passes four in
registers and nineteen on the stack, which is why a decompile shows
twenty-three `undefined4`s.  They are three things: the **new stroke**
and the **reference** it is judged against, each as a box, the middle
of its ink and two sizes (`SegWordInk`), and the writer's hand - the
stroke size the word started at, the word's own size, and the
twenty-two running measurements.  The reference is either the stroke
just taken in or the whole word so far, and it carries the narrower
band the body of the writing lies in and how many strokes it covers
(`SegWordRef`).  Every threshold below is scaled by how big this
writing has turned out against the running mean, so the whole of it is
measured in the writer's own units rather than in pixels.

**`SegmentWordBack`** is a single test.  A stroke may start to the left
of where the reference starts - the dot of an `i` and the bar of a `t`
are written after the letter and well behind it - but only by so much:
2.7 stroke sizes or fifteen pixels, whichever is more
(`RosCommonInfo::fBackGapStrokes` and `fMinBackGap`), scaled by a
gentle function of the size ratio: twice the ratio while the writing is
small, one while it is between a half and twice the mean, and the ratio
less one above that, averaged with its own square root.

**`SegmentWordVert`** measures three things - the middle of the ink,
the top and the bottom - against bands allowed for them, and **all
three** must have moved out before the pen is said to have gone to a
new line.  That is what stops an ascender or a descender from breaking
a word: one of the three moves and the other two do not.  The bands
come out of the word's size with a floor of four or five pixels, and
are then worked over four times:

- small writing loosens the downward bands, by up to two and a half
  times, because a small stroke's box says less about where it sits;
  and when the reference is a single stroke the upward bands are
  loosened with them;
- a pen that has moved left of the reference tightens them, twice over
  and further the further it went, because moving left and moving down
  together is what the start of a line looks like;
- a pen still inside the reference's own span loosens everything by a
  quarter again;
- and a pen just short of that span is judged by whether it is clear of
  the reference vertically as well.

The top and the bottom are taken against three parts of the body band
to one of the whole box - the same number when the reference is a
single stroke, because the caller passes its top and bottom twice, and
the body's when it is a word.

`test_Segment` drives all of it: the next letter along is the same
word, the dot of an `i` is the same word, a stroke a long way left is
the pen going back, a stroke on the next line down is the pen going
down, and an ascender is neither.

**`SegmentWordXGap`** is the third test and a small piece of
statistics.  `WordRecog::fRun` carries eight running Gaussians - kept
as a mean and a mean of the square, so a standard deviation is one
square root away - which are **four measurements in two situations**:
how far apart two pieces of ink are within a word and between words,
taken both between the boxes and between the middles of the ink, and
each of those both in pixels and in stroke sizes.

Each of the eight is **pooled** before it is used: with a nominal for a
writer of ordinary habits, scaled by how big this writing is; with the
same measurement in the other situation, rescaled by the constant ratio
of their two nominals; and with the other two measurements of its
group, rescaled the same way.  They are all measuring much the same
thing, so four noisy estimates of it are better than one.  The result
is held to between a quarter and four times its nominal, so a few
strange strokes cannot run the model away.

The gap is then put to all four pairs.  A gap smaller than the
within-word mean is certainly a join and one wider than the
between-words mean is certainly a space; in between, the difference of
the two squared z-scores is the log-likelihood ratio of the two
Gaussians, and `ArSigmoid` turns it into a probability.  The writer's
spacing setting enters here as `gSegLogWordSpacing`, **added** to that
ratio - which is why the segment layer keeps the setting as a logarithm
at all.  The four probabilities are averaged, and a new word begins if
the average passes `gSegIntegrated` (a half, or nine tenths when the
writing has been called joined up) or `gSegOnlyThreshold` when the
engine has been told only to group the writing.

Two oddities are kept.  The stroke-size half of the model works its
nominal term out and then leaves it out of the sum, dividing by four
rather than five, so those four estimates are pooled with no prior at
all.  And two of the four questions go on to turn the same ratio into a
score and a probability, write the score into the global `xpsvx` and
drop the rest on the floor.  The first is fixed by default now: all
eight are pooled with the nominal term (`NEWTON_ROM_BUGS=1` for the ROM's
behaviour).

`test_Segment` gives the layer a hand that leaves two pixels between
the letters of a word and fourteen between words, and checks that the
next letter along is a join, that ninety pixels further on is a space,
and that a gap the size of this writer's own spaces reads as one with a
strength somewhere in between.

#### The word recogniser's own way into the classifier

`WordRecogNetEvaluate` and `WordRecogNetSetInputs` are the twins of the
`CharBox` pair, and the ROM has the 256-code mapping written out twice.
Two things differ.

The patternizer and its pattern are made the first time they are wanted
and then **kept on the word recogniser** (`fPatternizer`, `fPattern`),
because a word is read one candidate letter at a time and the lattice
the segment layer hands up may hold dozens of them; a `CharBox` makes
its own once and keeps it for as long as the box lasts.

And all twelve geometry numbers go straight through to the
patternizer's `SLToPat`, where `CharBoxNetSetInputs` passes two of its
own twice - a box has no separate second baseline to offer, so the
tenth and eleventh arguments repeat the fifth and the eighth.

`test_WordRecog` puts the same upright-crossed-by-a-level-stroke
through this path that `test_CharBox` puts through the other, and gets
the same three answers: `+` at 0xf100, `t` at 0xe500, `T` at 0x0100,
and nothing else out of 256.

#### How big is this word? (`CharGetAvgBoxBHW`)

**B, H and W**: base, height and width. This is what the engine
measures a word with, and everything the patternizers are told about
the writing's size comes out of it - six numbers, three means and three
extremes.

The mean base is the mean of the segments' **bottoms**, and only of the
segments that are more than a dot. That test - `fHasDot < fCount` - is
the whole reason the segment layer bothers to record `fHasDot`: the dot
over an i sits nowhere near the line, and counting it would drag the
baseline up by a third of a letter.

The mean height and width take only the segments that are at least two
fifths of the size the word started at, so punctuation does not drag
them down. When there is nothing that big, the height falls back to
twice the mean width, and failing that to three times the least a
stroke may be.

The two extremes are the tallest and the widest segment, and then the
tallest is **raised to at least what the word's overall shape
suggests**: 1.8 times the widest letter, or a quarter of the word's
height above the baseline plus the widest letter, whichever is more. A
word written entirely in short flat letters is therefore still measured
as though something in it were tall, which is what stops a row of o's
being read as a row of full stops. `test_WordRecog` pins that case: two
letters three pixels tall come back measured at thirty.

(A latent bug: `segments[0]` is read for its top and its bottom before
anything checks that there is a segment at all. Every caller has at
least one.)

#### Reading a word (`WordRecogAnalyzeWord`)

By the time this runs, the strokes have been cut into a **lattice** of
candidate letters. It measures the word, learns from it, and then asks
the classifier about every candidate in turn.

The learning is the same eighth-at-a-time as everywhere else in the
engine, with two guards. Three of the four lengths the engine keeps
about the writer's hand move an eighth of the way towards what this
word says - but **only if the word is between half and twice what the
engine already believed**, so one badly written word cannot drag the
measure away. Then each is held to between half and twice its nominal
whatever it has learnt. The fourth length is not touched here. The five
lengths are then combined, each scaled by the nominal ratio it was
measured against, into one number for how big this word is, which is
what the classifier gets as its cap height.

Then, for every segment in the lattice:

* `seg->fField04` is set to how far ahead the search may jump from here
  - the last grouping the stroke this one ends on belongs to, counted
  from this one;
* `WordRecogNetEvaluate` runs the classifier into `fBuffer48`, one
  probability per character code;
* `CharModifyProbs` (NOT YET) leans on those with where and how big the
  piece is;
* and `SearchProcessSegment` (NOT YET) is given them along with a
  **confidence** - the mean of how much of the line each of the
  segment's strokes shares with the one before it, capped at a half
  apiece. A letter written in strokes that lie on each other is trusted
  more than one written in strokes that merely follow.

`SearchBeginWord`, `SearchProcessSegment` and `SearchEndWord` are the
lexical search, and they are where the readings actually come from -
the lattice walked against the grammar and the dictionaries. All three
are NOT YET, so the engine now measures, classifies and scores a word
but still answers nothing.

### How the search remembers what it has read (`recognition/WordTails.h`)

The lexical search walks the lattice of candidate letters looking for
the likeliest paths through it, and at every step it is holding a few
dozen partial readings at once. Those readings share nearly all of
their text: halfway through "handwriting" it may be holding "handw",
"hanciw" and "haridw", and all three end in the same `w` that came from
the same piece of ink.

So a reading is not a string. It is a **word tail**: a backwards linked
list of single characters, reference counted, so the shared ends are
stored once. Two readings of four characters that share three cost two
cells, not eight, and dropping one costs only the character no other
reading is still using.

A tail is named by a 16-bit **reference** rather than a pointer, and
the cells live in tables of 32 made as they are needed - 128 tables at
most, which is 4096 characters of readings in 16 KB. The reference is
read straight out of the number:

| | |
|---|---|
| `0xffff` | the empty tail |
| `0xf000` and up | a **word list** - a whole set of alternatives, out of a pool of fifty |
| anything else | a cell: table `(ref >> 5) & 0x7f`, slot `ref & 0x1f` |

Cutting the table number out of the reference is why a cell never has
to move once it has been made, and why the pool can grow without any of
the outstanding readings caring.

`WordTailCompare` gets two things out of that arrangement. Two tails
with the same reference are the same reading without looking at
anything at all; and the same text spelled out twice still compares
equal, character by character, oldest first.

`WordTailSprint` turns a reading back into text. It recurses to the far
end and writes on the way back, because a tail runs backwards, and it
prints a word list as its first alternative followed by a **bullet** -
a set of alternatives has no one spelling. `WordTailSprint2` refuses a
word list altogether, for callers that will print the alternatives
themselves.

`WordTailBlockAllocate` makes a whole table of 32 and answers one of
them; the search pops the other 31 off the free list itself and only
calls it again when the list is empty.

**A ROM bug, kept.** `WordTailDeleteRef` begins by answering straight
away for the empty tail. `WordTailAddRef` does not, so the empty tail
takes the cell path and looks in table 127, which is almost never made.
Nothing in the engine ever adds a reference to nothing - the first
character of a reading has no tail to hold on to - so it has never
mattered, and `test_WordTails` notes where the engine sidesteps it.
Fixed by default now (`NEWTON_ROM_BUGS=1` for the ROM's behaviour).

### What the classifier does not know (`CharModifyProbs`)

The classifier only ever sees a picture of a piece of writing, scaled
to fill a 14x14 grid.  It cannot tell an `o` from an `O`, or a comma
from an apostrophe, because at that scale they are the same shape.
`CharModifyProbs` is what leans its answer with **where and how big**
the piece actually was.

There are four adjustments, each with a weight in the common info, and
**two of them are nought in the shipped ROM** - the stroke-count
penalty (whose table is nil as well) and the shape fit.  They are
compiled in and switched off.  What is left is:

**The capitals hack.**  A lower-case letter whose capital the
classifier liked better is pulled a fifth of the way up towards it
(`fCapCaseWeight`).  The two are the same shape; only the height tells
them apart, and the height is the next adjustment's job.

**The height model.**  Every character code has a mean height and a
spread in `CharHeight`, measured as a fraction of the word's own size,
and they are real trained numbers: an `l` is 1.01 of the word, an `o`
0.52, a full stop 0.14, and a `g` 1.16 because of its descender.  This
works out how far off the piece in hand is, in spreads, and asks how
likely that is - **a Gaussian**, `exp(-z^2/2)`.

And it computes that Gaussian with no exponential at all.  A score in
this engine is already `-ln(p) x 500`, so `z^2/2 x 500` **is** the
score, and `ArProbDecode` turns it straight back into a probability.
The whole model is two multiplies, a divide and a table lookup.  A
character whose case the height cannot settle gets a spread half again
as wide, on whichever side is in doubt (`fCapCaseFlags`).

Only the **ten best** codes survive; everything else is set to nothing,
and of the ten, all but the first two have to be worth more than 0xc3
on their own.  `test_Rosetta` drives it: the same classifier answer for
`l` and `o` comes out favouring `l` for a full-height piece and `o` for
one half as tall.

### The lexical search (`recognition/Search.h`)

The segment layer hands up a lattice of candidate letters and the
classifier says what each of them might be. Neither decides anything.
The lexical search is what does: a **Viterbi** that walks the lattice
from left to right keeping the best few partial readings at each point,
scored by the classifier, the grammar and the dictionaries together.

Its state is thirty-seven **columns** - one per stroke of the longest
word the engine will read, and one to start from - and each column
holds up to `MaxBestNodes` (27, with room for 30) nodes. A node is a
partial reading that reaches this point: what it cost, and a word tail
for the text so far. Those tails are reference counted and shared,
which is what makes holding twenty-seven of them at each of
thirty-six positions affordable.

`SearchAllocateGlobals` makes the lot once and keeps it for the life of
the engine, carved out of a handful of big blocks with arrays of
pointers into them - the ROM will not allocate in the middle of reading
a word. `SearchBeginWord` empties every column and puts one node in the
first, holding the empty reading that every path grows from.
`SearchEndWord` gathers the best paths, hands them to the word
recogniser and gives everything back. (DEVIATION: the arrays and the
column block are sized from `sizeof` on the host, a host pointer being
twice the ROM's.)

The search itself - `SearchProcessSegment`, `SearchDoViterbStep`,
`SearchDoVStepFromNode`, `SearchFindBest`, `SearchBestWords`,
`SearchSendWords` and `SearchSegwordRememberNBest` - is reconstructed;
only `GeoContextPenalty`, what the geometry between two letters costs,
is NOT YET.

#### One candidate letter offered to the search

`SearchProcessSegment` is what `WordRecogAnalyzeWord` calls for every
grouping in the lattice.  The classifier has left a probability for
each of the 256 character codes, and `CharModifyProbs` has left what it
made of them in a second array; both are turned into **scores** here -
negative logarithms, which is the currency everything above works in -
into two arrays the Viterbi step then reads:

* from the classifier's, the score scaled by `rosCI`'s
  `fNetScoreWeight` (four fifths), with nought meaning never;
* from the second, the same score quartered, which is what the search
  charges for the letter itself.

While it is about it, the probabilities of the first are added up by
turning each stored score back into a probability again.  That total is
how much the classifier believes in this piece of writing at all, and
it goes to the step as one more score.

Then the columns move along and the step runs.

`ShiftNetValues` is how they move, and it is worth a look: the columns
are a **ring**.  Nothing is copied - the thirty-seven pointers are
rotated so the last column becomes the first, and that one is then
emptied.  Column 0 is always "here", column 1 is one stroke back, and
so on, which is how the search looks back over a whole word without
ever moving a node.

`GetBestPath` copies out the best reading so far.  That is the **try
string** - what the Newton shows you while you are still writing,
before the word is finished.

#### What a reading looks like from outside

`CapHackDetermineContext` is the other half of the capitals hack.
`CharModifyProbs` leans a letter towards its capital by height; this
says what having written one **means** for whatever comes next, in
twelve classes the grammar then charges against.

One capital is a different context from two in a row - `Mc` is a name
and `MC` is an abbreviation, and what may follow them differs.  An
apostrophe with a lower-case letter in front of it is read as *inside*
a word rather than after one, which is how `don't` stays one word.  A
reading that has come to a word list rather than a tail has no context
at all.  The twelve are six classes twice over, with a node's
`fField04` choosing which half.

#### Keeping the beam varied

`RegisterNewPath` - still NOT YET, but read far enough to name two
fields - is what puts a grown reading back into a column, and it does
something a plain beam search would not.  Each `BiGSlice` carries a
**lexicon class** in `fField2c`, each column counts how many of its
readings are of each class (`fClassCounts`), and the grammar carries a
limit per class (`fClassLimits`).  When the column is full it throws
away a reading of a class that is over its limit in preference to the
worst one outright.

So the twenty-seven readings a column keeps are deliberately varied:
one kind of word - all dates, say, or all numbers - cannot crowd the
others out, however well the classifier happens to like it.  That is
diverse beam search, in 1996.

#### Gathering the best readings

`SearchFindBest` walks all thirty-seven columns and gathers the best
readings the search is holding, best first, leaving a triple per
reading - which column, which node in it, what it cost - with the best
brought down to nothing and the rest priced against it.

Readings from different columns are not directly comparable, because a
column that starts further into the word has had fewer chances to
spend.  Each column carries what it cost to reach at all in `fField88`,
and the least of those is added back as a bias before anything is
compared.

The part worth knowing is that **the same text found twice is one
reading**.  Two paths through the lattice can spell the same word - a
`cl` and a `d` written identically - and before inserting, the list is
searched for a reading whose word tail compares equal.  If one is
there, the cheaper spelling wins and moves up the list rather than the
word appearing twice.  And because readings share their tails, that
comparison is usually a single pointer test.  `test_Search` fills a
column by hand with `cot`, `cat`, `car` and a second, cheaper `cat`
spelled out separately, and gets three readings back with the cheaper
spelling kept.

`SearchSegwordRememberNBest` then takes those readings off the columns
and puts them on a **word list** that the column holds - ten at most,
each with its score, its flags and a reference to its word tail so the
text survives the columns moving on.  That is how a point in the
lattice comes to stand for "one of these ten things", and how the
engine can hand back a set of alternatives rather than one answer.

(`SearchFindBest` takes a sixth argument that both its callers pass
deliberately - `1.0` or nought - and never reads.  Another vestige of
the training build, like `BiGSliceCreate`'s doubles.)

#### Handing the readings back

`SearchBestWords` writes the best readings out as text, each with a
score and the dictionary it came from, and `GetBestPath` is the same
thing into a caller's buffer.  The strings a caller is handed are the
engine's own, out of the return cache, which is why that only ever
grows: they have to stay valid until it asks again.

`SearchSendWords` is what `SearchEndWord` uses, and it hands the
readings back **one word at a time**.  A word list's readings may run
into *another* word list - that is what a reference of `0xf000` or more
at the far end of a tail means - and when they do, the earlier list is
sent first.  So a piece of writing read as several words comes back as
several calls to the callback, each with its own alternatives, its own
scores and its own count of strokes; the stroke count and the score
base are taken off as the recursion goes in, so each call is told only
about its own part.  Only the alternatives that end where the first one
does are sent with it: the rest belong to a different word.

**An overflow to keep.**  `CharModifyProbs` multiplies its Gaussian by
five hundred to get the score's units, and that is a plain 32-bit
multiply.  A piece of writing wildly the wrong height for a character
makes it overflow; on the ARM it simply wraps, and the nonsense that
comes out either reads as a score too dear to matter or lands harmlessly
in the decode table.  The reconstruction wraps too, because a saturating
version would answer differently.  It turned up when `test_WordRecog`
measured a segment four hundred pixels tall.

#### Putting a grown reading back

`RegisterNewPath` takes a reading that has just been grown by one
letter and puts it into the column it now reaches.  While there is a
free slot that is just an insertion, cheapest first.  When the column
is full is where it stops being a plain beam search.

Every kind of word carries a **class** (`BiGSlice::fField2c`), each
column counts how many of its readings are of each
(`SearchColumn::fClassCounts`), and the grammar says how many it will
allow (`BiGrammar::fClassLimits` - the General grammar allows two of
class 0 and two of class 1).  So when the column is full:

* if the newcomer's own kind is **under** its limit, the search walks
  back from the worst end for a reading that is unlimited or already
  over quota, and recycles that one **without looking at the score at
  all**.  A column with room for another date takes one however dear it
  is, rather than keeping a twenty-eighth word;
* otherwise it walks back for one that is unlimited, of the newcomer's
  own class, or over quota - and takes it only if the newcomer is
  actually cheaper.

That is what keeps the twenty-seven readings a column holds varied.
`test_Search` fills a column, has a dear reading of an unlimited kind
refused, then has an equally dear one of a kind that still has room
accepted - and then, once that kind is at its quota, refused in its
turn.

(The two paths are not symmetrical about the counts: the second counts
the recycled reading's class down and the first does not.)

#### The backtrace, and when it becomes text

During a step, a reading in the column being filled is only a
**backtrace**: the node it grew from and the letter that was added,
held in the matching `gSearchBest` entry.  It has no text of its own at
all.

`StoreFinalPaths` is what turns those into real word tails, once the
step is over and it is known which readings survived.  Each one gets a
cell holding its letter, pointing at the tail it grew from, with a
reference taken so the older text stays alive - and it rebases the
scores so that the new column's cheapest reading is at nothing.

Keeping it until the end is what makes the whole thing affordable.  A
step may offer a column dozens of readings and keep twenty-seven; the
ones that are dropped never cost a cell.  And the ones that are kept
share everything before the letter that was just added, which is why
`test_Search` can grow `cat` and `car` out of one `ca` and see the
reference count go up by two rather than the text being copied.

#### The driver

`SearchDoViterbStep` is the step: one candidate letter offered to
every reading the search is holding.  What it does, in order:

1. empties the column that is about to be filled (`gSearchColumns[0]`,
   "here") and its class counts, and sets up the `SearchStep` block;
2. finds the cheapest `fCost` and `fAltCost` over the columns this
   candidate could start from, which is the bias readings out of
   different columns are compared against, and records `fAltCost` plus
   the classifier's total on the new column;
3. gives up straight away if no character code is readable at all;
4. works out what one stroke of this candidate costs - nothing when the
   strokes lie on each other well, up to 322 when they do not
   (`rosCI`'s `fStrokeCost*`) - and multiplies by one less than the
   stroke count;
5. fills `gSearchScratch` with which **case** of each character code is
   reachable here, from the classifier's scores for the code's two
   alternative cases and `fCapCaseFlags`;
6. if the column this candidate starts from has a finished word on it,
   fills `gSearchWordListNode` - a pseudo-node whose tail *is* that word
   list and whose score is what the gap before this candidate cost - and
   grows readings from it, so a new word can start straight on from an
   old one;
7. grows readings from every node of every eligible column
   (`SearchDoVStepFromNode`);
8. copies the candidate's jump and letter count onto the new column,
   works out what the column cost to reach, and calls
   `StoreFinalPaths`.

The gap before the candidate is read **both ways round**: its
separation is the probability that a new word starts here, so
`ArProbEncode` of it is what starting one costs and `ArProbEncode` of
its complement is what *not* starting one costs - which is what every
reading continuing within a word is charged.

#### One reading grown by one letter

`SearchDoVStepFromNode` (`recognition/Search.cpp`) is the innermost
thing the engine does, and the rest of the search exists to feed it.
Given one reading and one candidate piece of ink, it tries every
character the lexicon will allow next, adds up what each would cost,
and offers the result to `RegisterNewPath`.  There are three loops, one
inside the other.

**The kind of word.**  A reading may stay in the kind of word it is in,
or move to one the grammar allows after it - and the pseudo-node that
stands for a word already finished may start any kind at all.  The
transition's own weight is added to the reading's score.  The ROM
writes the candidate kind into the node itself and puts the old one
back at the top of each round, which is why the function keeps the
node's original slice, score and flags in locals.

**The letter.**  `LELangNodeNumOut` says which characters the lexicon
allows from here.  Each one is charged four things:

* what the classifier thought of it, plus more of the same the more
  loosely the strokes were written (the extra is the score again,
  multiplied by how many strokes the candidate took less one and by how
  far short of certainty the segment's confidence falls);
* what a character of this kind of word costs (`BiGSlice::fCharCost`);
* what its **case** costs in the context the reading is in -
  `CapHackDetermineContext` answers one of twelve, and the common
  info's three twelve-entry tables (`fCapCostUpper`, `fCapCostLower`,
  `fCapCostOther`) say what a capital, a lower-case letter or neither
  costs there.  A kind of word that says it will have the case being
  tried is charged its own `fCapCostUpper`/`fCapCostLower` instead, and
  in the upper six contexts `fCapExtraUpper`/`fCapExtraLower` are added
  on top.  This is where `Mc` and `MC` part company;
* and what the geometry between it and the letter before it costs -
  `GeoContextPenalty`, at a **quarter weight** when the letter before
  is in another word or there is no letter before it at all, because
  across a word boundary two shapes have much less to say about each
  other.

A reading picking up after a whole word pays the second score array as
well, which is the one `CharModifyProbs` left quartered.

**The case.**  Having tried the character the lexicon named, it tries
the other case of it (`rosCapHackAltCase1`) and then a third form
(`rosCapHackAltCase2`) - but only the cases the kind of word allows,
which `gSearchScratch` and the slice's own flags say between them.
That is why writing a word in the wrong case still reads.

Whatever survives the cost cap of `0x7ffe` is handed to
`RegisterNewPath` with the lexicon node the character leads to.  That
node is worked out once per character and reused across the three
cases: a run lexicon carries the next node two bytes past its flags,
and a chained one has to be built from the node's own flags, the
header's width nibble and `AckNodeSizeTab`.  Its sign bit says the word
may end here, which is how the search knows a reading is a whole word.

`LELangNodeNumOut` (`recognition/LELang.h`) is the other half: it walks
a node's *siblings* - the letters that may follow - into `LELTranCache`
and says how many there are.  A **run** lexicon keeps them together as
a string somewhere else in the data, all leading to the same next node;
a **chained** one has one node per character, each with a
variable-width offset to the next, one to four bytes wide by two bits
of its flags.  So a short hop costs one byte and a long one costs four,
and a lexicon of fifty thousand words is not paying four bytes a
letter.

#### How two letters sit against each other

`GeoContextPenalty` (`recognition/GeoContext.h`) is the last thing the
step charges for, and the part that tells `rn` from `m`.  The
classifier sees much the same ink either way; only the sizes and places
of the two boxes say which reading the writer meant.

The engine carries a **nominal drawing** of every character it can
read - sixteen numbers apiece in `rosCharParams`, all as fractions of
the cap height:

| table | |
|---|---|
| 0 | where its bottom sits above the baseline |
| 1 | how tall it is |
| 2 | how wide |
| 3, 4 | how much room it wants before it and after it |
| 5, 6 | how big its smallest stroke is, written in one stroke and in more |
| 7..13 | what each of those measurements is worth about this character |

The numbers are what you would expect: an `i` is 0.221 wide and an `m`
0.753; a full stop's bottom is 0.058 above the baseline and an
apostrophe's is 0.720; an `i` drawn in one stroke has a smallest stroke
of 0.482, which is its stem, and drawn in two it has 0.084, which is
its dot.

`GeoContextAux1` takes the two observed boxes, turns the y values over
so up is positive as it is in the nominal drawing, divides everything
by sixty-four so the squares below cannot overflow, moves the four y
edges and the four x edges so each set averages nought, and scales so
the eight edges come to sixteen between them.  What is left is the
shape and the relative placing, and nothing of the size.

`GeoContextAux2` lays the two nominal boxes out the same way - the
first with its right edge at nought, the second `dx` further along,
where `dx` is what the two characters say the gap should be (the room
one wants after it and the other before it; laid on top of each other
at a word boundary; wider still across two words) - centres them
likewise, fits the one remaining scale by least squares, and takes nine
residuals: each box's bottom, top, width and smallest stroke, and the
gap between them.  Each is weighted by what the two characters say that
measurement is worth about them, and the gap is dropped altogether when
the two expectations disagree about which way it is wrong.

The nine then go through a symmetric nine-by-nine matrix (`kGeoWeights`,
81 signed 16.16 numbers with no symbol on them) as a **quadratic
form** - a Mahalanobis distance, positive definite, so the residuals
are weighed against each other rather than added up.  The off-diagonal
entries among the four height residuals are large and positive, so two
residuals of the same sign cost far more than two of opposite sign:
what the fit could not absorb is damning, and two letters sitting a
little differently is not.  That is the whole `rn`/`m` decision - two
boxes seven wide and fourteen tall with a one-pixel gap score 478 read
as `rn` and 2487 read as `mm` (`test_GeoContext`).

The answer is multiplied by `RosCommonInfo::fGeoWeight` (twenty) and
truncated to a short.  A hundred answers are cached against the pair of
segments they were asked about, thrown away whenever the pair changes;
`SearchDoViterbStep` also empties it by hand.

With this the lexical search is complete.

#### Write it three times

`SearchCheckHashHit` looks at every reading on its way out and compares
it against eight words. Write one of them **three times in a row** and
the engine answers something else instead:

| write | and it answers |
|---|---|
| `larryy` | The Doctor is on. |
| `Larry` | larryy@apple.com |
| `Mondello` | Fine food 408/257-2383 |
| `Brandyn` | brandyn@brainstorm.com |
| `Rosetta!` | Hey, that's me! |
| `stafford` | bill |
| `Les` | lesv@angeltech.com |
| `lyon` | Richard |

Those are the people who built the Newton's handwriting recognition,
one restaurant, and the engine answering to its own name. The counts
are kept per word and every one but the word just seen is cleared on
each reading, so the three really do have to be consecutive.

`analysis/romtable.py` grew a `strN` type for the two tables, which are
arrays of fixed-width strings laid out in the ROM rather than arrays of
pointers.

### One letter in a box (`recognition/CharBox.h`)

`CharBox` is the shortest way through the engine, and the first end of
it that answers a question about a piece of writing in *characters*
rather than in numbers. A recogniser is made over a rectangle
(`CharBoxIntialize`), strokes are put into it (`CharBoxAddStroke`, at
most six), and `CharBoxGetChars` answers the character codes it thinks
were written, best first. It is what the Newton's boxed-entry fields
are read with, and it is also the simplest thing the classifier can be
asked, because there is no word, no grammar and no dictionary in it.

The state is 0x234 bytes: the box, a patternizer and a pattern, two
segments, up to six strokes, the net, and 256 shorts - one score per
character code, all of them starting at 0x7ffe, which is the engine's
"never".

A stroke is tidied on the way in. `CharBoxAddStroke` runs it through
`StrokePreprocess` with four of the net's own parameters
(`arBPParam[0xc4]`, `[0xcc]`, `[0xd0]` and the short at `[0xd4]`) and
appends what comes back - which may be more than one stroke, so the
count is checked against the whole list. **A ROM bug, kept:** the
function is plainly meant to answer nought when the stroke was taken
and one when the box was full, but it ends in a tail call to
`SLDestroy` and so gives back whatever *that* answers instead. The
flag does reach `SLDestroy`, which is what makes a refused list take
its strokes down with it; it is only the caller who never learns.
(Fixed by default now: the flag is answered; `NEWTON_ROM_BUGS=1` for
the ROM's behaviour.)
`CharBoxIntialize` has a smaller one: it asks both questions about the
box (`ValidFixedRect`, `EmptyFixedRect`) and throws the answer away -
a statement with no effect, all that is left of what was presumably an
assertion in the original source.

#### From 134 outputs to 256 characters

`CharBoxNetEvaluate` is the bridge, and it is where the common info's
character tables (`RosCITables.cpp`) earn their keep. The classifier
has 134 output nodes and the engine deals in 256 character codes, so
the mapping is not one to one:

* a code the area will not have - `RosCI->fLegalUse` says which - scores
  nothing at all;
* a code that stands for **one** shape takes the output of the node
  `fCharToNetNode` sends it to, widened from a byte to 16.16 by a shift
  of eight. So the most confident a node can be, 0xff, comes to 0xff00
  and not 0x10000: nothing is ever quite sure;
* a code that is really **two** characters - `fCompoundPart1` and
  `fCompoundPart2` say which two - takes the **product** of its two
  parts' outputs. That is how a net which was never shown the pair
  still has an opinion about it. When the two parts happen to map to
  the same node the product is dropped and the single output used, so a
  doubled letter is not charged twice for being written twice.

In the US ROM's tables 166 of the 256 codes are legal and 54 of those
are compound.

`CharBoxGetChars` sorts the scores, copies them into the caller's array
until it meets the first that says never, and clears whatever room was
left over. The sort is a `qsort` of 256 packed words - score in the
top half, code in the third byte - so the comparison is one unsigned
subtraction and the "never" codes fall out at the end by themselves.

#### What it reads

`test_CharBox` draws an upright stroke crossed by a level one inside a
box and runs it through: the patternizers turn it into the net's 384
inputs, `BPNetEvaluate` runs the net, and the engine answers

    +   0xf100
    t   0xe500
    T   0x0100

and nothing else at all. That is the reconstruction reading
handwriting for the first time. A capital T comes a distant third
because the cross-stroke is halfway down rather than at the top.

What is missing is `CharBoxEvaluate` (0x00056878), which turns those
probabilities into the engine's own scores and then leans on them with
the geometry of the box: the strokes go to the segment layer
(`SegmentSetStrokes`, `SegmentDot`), `CharModifyProbs` adjusts them and
`GeoContextPenalty` charges each code for how badly it sits in the box,
through the `ArProbEncodeLu1`/`ArProbEncodeLu2` tables. Until the
segment layer is reconstructed the scores stay at "never" and
`CharBoxGetChars` answers nothing - so the way in for now is
`CharBoxNetEvaluate` itself.

## The writer's recognition preferences

`ReadCursiveOptions` (`FReadCursiveOptions`, 0x0019cfd8, reached at boot
through `ReadDomainOptions` and again whenever the Prefs slip changes
something) is the recognition system reading what the writer has asked
for and putting it into force:

  - the **timeout** (`timeoutCursiveOption`) is how long the recogniser
    waits after the pen stops before it decides the writing is
    finished, in sixtieths of a second, kept between a quarter of a
    second and a whole one.  `SetDomainDelays` then makes every domain
    that waits at all wait that long; a domain whose delay is already
    nought - the strokes' and the clicks', which are ready the moment
    the pen lifts - is left alone.
  - the **double-tap interval** is worked out from the timeout rather
    than asked for: half way between a quarter of a second and it.
  - the **letter spacing** (`letterSpaceCursiveOption`) is stored the
    other way up from the way it is asked for - nine less what the
    writer chose - because the recogniser wants how *close* letters may
    be and the slip offers how far apart.
  - the **input mask** the configuration carries is rebuilt from the
    text-recognition options (`BuildInputMask` over strokes and
    gestures), which is what every area built from it starts with.
  - the **language** (`gEnabledLanguage`) is 8 when the locale names
    one and 1 when it does not; a language that names itself keeps its
    diacriticals, and so does writing the WRec engine read, which is
    what `StripRecognitionWord` asks before it strips them.
  - the **learning** (`learningEnabledOption`, `bigLearningEnabled`) is
    off unless the writer asked for it, and that is what decides
    whether a unit carries any training data at all.

Anything not set is written down as the writer's own, by
`GetDefaultedPreference` (0x0019cc04) - the configuration is a soup
entry, so it is told it has changed.  The area cache is purged at the
end, because the areas in it were built from the old answers.

`ReadDictPrefs` is called from here, which is what lets a change of
locale change the words the machine reads against.

NOT YET: `SetUpRosetta` and `SetUpParaGraph`, which hand the letter set
to the two engines, and the `_recognizerUserChoices` frame this leaves
on the root view for the slip to read back; both belong to parts that
are NOT YET.  Nothing here chooses *which* word recogniser is in use:
that is `UseWRec`'s, called from a script.

## Stroke bundles (`recognition/StrokeBundle.h`)

A *stroke bundle* is the NewtonScript form of a handful of strokes, and
what a recogniser hands a view when the writing is to be kept rather
than read.  It is a frame of class `'strokeBundle`, cloned from the
ROM's `Rstrokebundle`, with a `strokes` array, a `bounds`, and the
`startTime` and `endTime` of the writing.  Each member of the array is a
binary of class `'stroke`: one four-byte Point per sample, v then h,
both in eighths of a pixel and big-endian, because a bundle can go into
a soup.

The eighths are the ROM's own resolution rather than the tablet's.
Every function here takes a *format*: under two means pixels, and a
point is rounded to them with `(value + 4) >> 3`; two or more asks for
the eighths themselves.  `GetStrokePointsArray` reads two more things
out of the upper bytes of the same word - how far apart the points are
to be (one no further than that from the one before is dropped, though
the first is always kept, and the distance is the cheap one: the longer
side plus half the shorter) and whether they are wanted h before v
rather than in the Point order, which is the difference between
`GetPointsArray` and `GetPointsArrayXY`.

`StrokeBundle` (0x00144e54) makes one out of the units a recogniser has
finished with, the box let out by the two pixels the pen spills;
`MakeStrokeBundle` out of arrays of numbers a script hands in.
`StrokeBundleToTStrokes` turns one back into the recogniser's own
objects, `DrawStrokeBundle` draws it stretched from the box it was
written in into another, and `ink/InkShapes.h`'s `StrokeBundleToInkWord`
packs it up as ink - keeping the answer in the bundle's own `inkWord`
slot, so a bundle passed round several views is only packed once.
`TParagraphView::InsertInk` is what puts the result into a paragraph, as
the single character an ink word stands as.
