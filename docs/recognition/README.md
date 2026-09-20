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
(`LookupWordOrVariant`, NOT YET) and answers a word of bits.

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
out of the ROM's own word data, which is NOT YET
(`InitROMDictionaryData`, `GetROMDictionaryData`,
`BuildDictionaryFromPtr`, the trie that is dictionary 32, and
`gDictList` beside them).

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
into the ROM (NOT YET), `AEnum` for the ones the machine writes, which
is what the user's words go in.  Everything goes through `CallAirusA`
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

### What a script sees

A dictionary frame's `dict` slot holds the engine's dictionary by
address (`GetScriptDictRef` 0x0008ea78).  `FAirusNew` 0x0008ee98 makes
one, `FAirusLookupWord` 0x0008fb28 looks a word up and fills in the
`attribute` and `terminalClass` of the frame it is handed, and
`FAirusAddWord` 0x0008fc3c adds one.  That is the chain the Setup
assistant's Continue button runs down when a name has been typed in, and
with it the assistant goes on to its next page.

NOT YET: the AL and AL16 walkers, AE16, deleting and the iterators
(`AEnum_DeleteWord`, `AEnum_FirstLast`, `AEnum_NextPrevious`,
`AEnum_ChangeAttribute`, `AEnum_NextSet`, `TAirusIterator`), and
`ReadRefDictionary`, which builds a dictionary out of a binary rather
than making an empty one.

NOT YET: TController and the arbiter, the domains (stroke, edge-listNOT YET: TController and the arbiter, the domains (stroke, edge-list
gestures, shapes, words), the area cache (`InitAreas`,
`GetAreasHit`, `BuildRecConfig`, `OtherViewInUse`, `ClicksOnlyArea`), the
inker and ink (`StrokeUpdate`, `TStroke::Draw`, the expired strokes'
grouping and compression, the stroke bundles), the word list and
dictionaries, the tablet driver, the journal, the caret popup.
