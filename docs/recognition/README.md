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

BUG (kept): `ReadDictPrefs` does not check that there is a list.
`TRecognitionManager::Init` builds one only above level 1 but calls
`InitRecognizers` - and so `ReadDomainOptions`, and so this - at every
level, so a machine started at level 1 throws on a `vars.dictionaries`
that was never made.  The MP2x00 always starts at level 2, so nobody
ever saw it; the tests that do start at level 1 put an empty list there.

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

BUG (kept): the answer is set to true before the second add is checked,
so a word whose user-dictionary entry failed - and which is therefore
taken straight out of the auto-add dictionary again - is still reported
as added.

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

BUG (kept): a word whose last node carries no attribute - a path that is
not a word - returns without setting the block's result, which
`DeleteWord` had just set to 0, so deleting a word that was never there
is reported as success.  Deleting a word that begins nothing at all is
reported properly, as "not there".

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

BUG (kept): `AE8_NextSet9` assembles the attribute it hands the callback
from its bytes low one first, where `PutAttr` writes it and `GetAttr`
reads it high one first.  A one-byte attribute - which is what every
dictionary the machine writes has - is the same either way, so nobody
ever saw it; a two- or four-byte one comes out of that call
byte-reversed.

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

So the two dictionary natives still unanswered are
`ConvertDictionaryData` (wants the completions walk) and
`GetRandomDictionaryWord` (wants the generator).

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
throws if the list was never built.  The shape, word and WRec recognisers
of level 2 are still NOT YET, so nothing else about the level is true
yet.

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
left for the round after.

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
second arm.

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

NOT YET: the shape and word domains above this one, nor
`ArbitrateGraphicsWords`, the inker and ink
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
exactly when the engine has just run out of memory.

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

NOT YET: `RegisterWRec` (0x001b5bb4), which is the ROM's own handwriting
engine registering itself, so nothing answers to `TWRecognizer` and
nothing is installed until a host supplies an engine.
`TWRecRecognizer::ConfigureArea` (0x00144178), which hands the engine
the parameters an area is to be read with, needs the area-information
side of `TWRecDomain`.  `ReadDomainOptions` (0x0019cfd8) is what reads
the writer's recognition preferences at boot and calls
`SetWordRecognizer`.

## The Rosetta engine (`recognition/RosRecognizer.h`, `Rosetta.h`)

The engine the MP2x00 actually reads writing with is ParaGraph's
Calligrapher, which Apple licensed and shipped as **Rosetta**. It is a
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

**Reconstructed so far: all of level 1, and level 2 as a set of
declarations with no bodies.** `TRosRecognizer` is real; every call it makes into the
engine answers "could not", so the recogniser throws `evt.ex.abt`, which
is exactly what the ROM's own does when its engine fails. Nothing
installs it — the engine the host installs is still
`TInkOnlyRecognizer`, so the pen still leaves ink.

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

In order:

1. **Level 2**, the fifteen `Rosetta*` calls: the
   setup/analyze/cleanup passes a classify is made of, the area, the
   baseline, sleeping and waking. `RosettaClassifyAnalyze` (0x001b7cc4)
   is the top of the real work, and it calls straight into level 3.
2. **Levels 3 to 6**, bottom-up, which is the engine proper. The
   trained tables come out of the ROM's data through
   `analysis/romtable.py` as everything else does.

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

Until then the eleven natives that ask for handwriting — `Recognize`,
`RecognizePara`, `RecognizePoly`, `RecognizeInkWord`,
`RecognizeTextInStyles`, `DoCursiveTraining`,
`UseTrainingDataForRecognition`, `GetLetterWeights`/`SetLetterWeights`
and the letter-shape group — have nothing to answer with.
`RosettaExtension` (0x001b6c3c) is the exception: it answers nil and
always did.

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
