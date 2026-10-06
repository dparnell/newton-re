# More word recognisers, chosen from the Handwriting slip

Status: branch `hwr-engines`; the framework and the unistroke engine are built.

The owner wants handwriting engines beside the ROM's two (Rosetta for
printing, ParaGraph for cursive), chosen from the Newton's own
Handwriting Recognition slip, not from the Host page.  The first two are:

  - **Unistroke** - a Graffiti-style reader written in this repository,
    one letter per stroke, no dependencies, the same on every host;
  - **Neural** - an online (pen-trajectory) word recogniser run through
    ONNX Runtime, on Windows, Linux and the reMarkable (arm64).  It needs
    a pretrained model whose licence allows it to be redistributed or
    fetched by a documented script (CLAUDE.md's project rule).

Both are host additions, not ROM reconstruction: everything below that
changes ROM behaviour is a `DEVIATION`.

## What the ROM already gives us (how each fact was established)

- **Switching is by unit type, for any number of recognisers.**
  `SetWordRecognizer` (ROM 0x001442a0, `src/recognition/Recognizer.cpp`)
  finds the wanted and the current recogniser on the recogniser list by
  id, turns the current one's services off and puts it to sleep, wakes
  the wanted one and sets `gWordID`.  Nothing in it limits the count to
  two.  Scripts reach it through `UseWRec("XRWR")` (`FUseWRec`).
- **An engine plugs in through the `TWRecognizer` protocol.**
  `TWRecDomain` finds it by name (`NewByName("TWRecognizer")`) and the
  engine calls back through the protocol's own methods for the grouping
  (`docs/recognition/README.md`, "The word domain").
  `TInkOnlyRecognizer` (`src/recognition/InkRecognizer.cpp`) is a
  complete implementation that groups strokes into words and reads
  nothing - the skeleton a new engine starts from.
- **Only the letter set decides today.**  `ReadCursiveOptions` (ROM
  0x0019cfd8) reads `userConfiguration.letterSetSelection` and calls
  `SetUpRosetta` (set 2: `UseWRec("WREC")`, word at a time) and
  `SetUpParaGraph` (anything else: `UseWRec("XRWR")`, line at a time).
- **The slip's control is a two-button cluster.**  `romsrc` `obj_5afced`
  (`'LetterSetCluster`): "Printing (separate letters)" = 2 and "Cursive
  (connected letters)" = 0.  Its `viewSetupFormScript` accepts 0..4 and
  resets anything else to 2; `ClusterChanged` writes the value to
  `letterSetSelection`, dirties the example word and calls
  `ReadCursiveOptions`.
- **Letter-set values other than 2 mean cursive variants.**  ParaGraph
  reads `gLetterSetSelection` for its learning info and letter shapes
  (`WordRecognizer.cpp`, `XrDomains.cpp`, `StrXrDomain.cpp`; 4 turns
  learning off), so 1, 3 and 4 are taken.  The ROM's scripts test
  `letterSetSelection <> 2` to save and load ParaGraph's letter weights
  (`obj_63d7e5`, `obj_641585`).
- **The example word is indexed by the letter set.**
  `drawExampleScript` (`obj_5b033d`) draws `wordBits[letterSetSelection]`,
  so a new value needs an entry there (or a guard).

## What is built (the framework)

1. **One recogniser per engine.**  `recognition/WordEngines.h` holds the
   table of the host's engines: unit type, button value, implementation
   name, button text, whether it reads a line at a time.
   `RegisterHostWordEngines` (beside `RegisterRosettaWRec` in both boots)
   registers their implementations under the interface name
   `THostWordEngine` - not `TWRecognizer`, because the ROM's 'WREC' domain
   asks the registry for *any* TWRecognizer and another implementation
   would be as likely an answer as Rosetta.  `InstallHostWordEngines`
   (after `InstallWRecRecognizer`, in `InitRecognizers`) makes each a
   `TWRecDomain` of its own type (`TWRecDomain::MakeHostEngine`) and a
   `TWRecRecognizer` over it, installed asleep as Rosetta's is.
2. **The choice is its own slot.**  `userConfiguration.hostWordEngine` is
   the engine's button value (8 and up); no slot means the ROM's choice.
   The letter set stays one of the ROM's: ParaGraph's code reads it
   asleep or not and refuses anything above 4 (`AllocLearnInfo` fails,
   and with it `TXrWordDomain::InitializeParamStruct`), so choosing an
   engine sets the letter set to printing, 2 - which is also how the
   slip then draws its example word and spacing.
   `ReadCursiveOptions` calls `SetUpHostEngine` after the ROM's two set-up
   functions: an installed engine chosen is put in use in their place
   (`SetWordRecognizer`), with `currentWordRecognizer` and `lineAtATime`
   set as the ROM's set-up functions set them.
3. **The places that knew only the ROM's recognisers**:
   `TDomain::VUnitInClass` counts the engines' types as words;
   `WRecDomainInUse` (Words.cpp, behind `WRecVerifyWordSymbols`) answers
   the engine's domain when one is in use; `TWRecRecognizer::ConfigureArea`
   keys the area's block by the recogniser's own type, not 'WREC'.  Each
   is marked DEVIATION.  Left as they are: `WRecIsBeingUsed()` (true only
   for Rosetta) and auto-add (only for Rosetta, `AddAutoAdd`).
4. **The slip is patched at boot, not in `romsrc`.**
   `host/HostWordEngines.ns` (embedded in newton, run by
   `HostInstallWordEngines` after the Host panel) registers a copy of the
   `HWRecPreferencesForm` with `RegPrefs` - the form, its letter-set box
   and the cluster each a frame whose `_proto` is the ROM's, grown by a
   row per engine, with a radio button per engine from
   `HostWordEngines()` under "Printing" and "Cursive".  The cluster's
   `viewSetupFormScript`/`ClusterChanged` and the form's `UseDefault`
   handle the engines' values and hand the rest to the ROM's.  It works
   the same booted from the ROM image (`--rom`).  The Prefs roll lists
   the built-in panels by name (`GetPrefs`), so the panel keeps its place.
   Editing the slip in `romsrc` instead would have broken the
   byte-for-byte check of the tree (`host.ROMSourceCommitted`).
5. ctest `host.NewtonWordEngines` (`src/host/demo/engines.ns`): the
   button is in the slip, choosing it puts 'UNIS' in use with the letter
   set left at 2, printed writing is typed as Graffiti strokes rather
   than read, and "Printing" puts Rosetta back and the next word is read.

## The unistroke engine

`recognition/UnistrokeRecognizer.h` ('UNIS', button 8) is Palm's
Graffiti (the original, "Graffiti 1"), because so many people already
know it: one stroke, one character, typed at the caret the moment it
is written.

- **The classifier** (`recognition/Unistroke.h`) compares a stroke with
  a template per character - a path in a unit square, drawn as the
  Graffiti card draws it - after resampling both to 40 evenly spaced
  points and scaling them into the square: stretched to fill it for a
  figure (so a tall O and a wide one are both O), the same both ways
  for a straight stroke (I, X, the space, backspace, return and shift),
  so that its direction is kept.  Direction is part of the shape: the
  space is a line drawn left to right, backspace the same line drawn
  right to left.  The reference card,
  `docs/recognition/unistroke-card.svg`, is drawn from the templates by
  `test_Unistroke --card <file>`.  ctest `recognition.Unistroke`: every
  character drawn as its template, then stretched, sheared, turned and
  wobbled (2916 drawings, 98.2% read right; b/d, a stretched X, L/4 are
  the confusions), the commands never mistaken for each other.
- **One stroke is one unit**: `Group` makes a unit of the stroke and
  closes it at once, so it is arbitrated and read without waiting for
  the pen to rest.  Its readings are labelled as typing
  (`kHostTypedLabel`), and the host engines' recogniser
  (`THostWRecRecognizer`, `WordEngines.cpp`) posts the first at the
  caret as the keyboard posts keys (`PostKeyString` to the key view):
  letters and digits, space, backspace and return all do what those
  keys do.  It then sets `gHostUnitTyped`, and `HandleUnitList` counts
  the unit as handled and claims its strokes (DEVIATION) - a unit whose
  recogniser answers no command is otherwise left to the arbiter, which
  turns its strokes into ink (that is how the first try typed every
  character *and* left an ink word beside it).  On a fresh Notepad the
  page itself is the key view, and the first character typed at it makes
  the paragraph the rest are typed into.
- **Caps**: a stroke up is the caps shift (the next letter a capital),
  twice is caps lock, a third time unlocks.
- **Digits**: Graffiti read 0/O, 1/I and 5/S by where they were written
  (its letter and number areas).  A field whose input mask takes numbers
  but no letters reads them as digits; any other as letters, the digit
  being the second reading.  The other digits have shapes of their own
  and are read anywhere.
- A stroke read worse than `kUnistrokeGoodScore` is left as ink; a dot is
  a tap, and the Newton takes it as a click before any recogniser sees
  it.
- `NEWTON_TRACE_UNISTROKE=1` prints each stroke's readings and scores.
- ctest `host.NewtonUnistroke` (`src/host/demo/unistroke.ns`): strokes
  drawn by hand (not the templates), one on top of the other as on a
  Palm - caps shift, "hello", a space, "worlx", a backspace, "d" - and
  the page reads "Hello world".

NOT YET: punctuation (Graffiti's punctuation shift is a tap), the symbol
and extended shifts, accented letters.

## Order of work

1. ~~The framework.~~
2. ~~The unistroke engine's reading~~ - letters, digits, space,
   backspace, return, caps; punctuation next.
3. The neural engine: choose a model (licence first), the ONNX Runtime
   dependency per host (vendored or fetched by a documented script), the
   stroke-to-tensor encoding, CTC decoding against the area's
   dictionaries.  It goes into `kWordEngines` as 'NNET', button 9.

## Open questions

- Which model for the neural engine, and its licence.
- Whether an engine's readings should go through the area's
  dictionaries (`BuildChains`) or bring its own language model.
