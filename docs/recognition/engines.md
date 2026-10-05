# More word recognisers, chosen from the Handwriting slip

Status: design, branch `hwr-engines`.  Nothing here is built yet.

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

## Design

1. **One recogniser per engine.**  Each engine is a `TWRecognizer`
   implementation with its own `TWRecDomain`/`TWRecRecognizer` and its
   own four-character unit type ('UNIS', 'NNET'), installed **asleep**
   beside the ROM's two, as `InstallWRecRecognizer` installs Rosetta.
   Because `TWRecDomain` finds its engine by protocol name, each engine
   registers under a name of its own and the domain is told which to
   make (a host-side constructor argument - DEVIATION).
2. **A host-side engine table** (`recognition/WordEngines.h`): id,
   unit type, the slip's button text, the letter-set value it answers to,
   and whether it is available on this host (the neural engine is not
   offered when ONNX Runtime or its model is missing).
3. **Letter-set values from 8 up are engines.**  8 = Unistroke, 9 =
   Neural, leaving 5..7 clear of anything Apple might have meant.
   `ReadCursiveOptions` gains a third call beside `SetUpRosetta` and
   `SetUpParaGraph` - `SetUpHostEngine(letterSet)` - and those two return
   for values >= 8 (DEVIATION).  Engines read a word at a time
   (`lineAtATime` nil) unless the table says otherwise.
4. **The slip** (`romsrc`, so only when booting from the reconstructed
   data - `--rom` shows the ROM's own two buttons):
   - the cluster gets one button per available engine, added at view
     setup from the engine table (a native, e.g. `HostWordEngines()`),
     so an engine the host lacks is never shown;
   - `viewSetupFormScript` accepts the engines' values as well as 0..4;
   - `wordBits` gets an example drawing per engine, or
     `drawExampleScript` falls back to printing's;
   - the `<> 2` letter-weight scripts are guarded to ParaGraph's values
     (0, 1, 3, 4), so choosing an engine does not save ParaGraph's
     weights.
5. **A demo and a ctest per engine**, like `write.ns`: pick the engine
   through the slip's value, write, check the text that comes back.

## Order of work

1. The framework: engine table, `SetUpHostEngine`, a second recogniser
   installed from the ink-only engine, the slip's buttons; demo shows the
   switch and that writing becomes ink.
2. The unistroke engine (alphabet, digits, punctuation shift; one stroke
   per character - the domain's grouping makes a run of strokes a word).
3. The neural engine: choose a model (licence first), the ONNX Runtime
   dependency per host (vendored or fetched by a documented script), the
   stroke-to-tensor encoding, CTC decoding against the area's
   dictionaries.

## Open questions

- Which model for the neural engine, and its licence.
- Whether an engine's readings should go through the area's
  dictionaries (`BuildChains`) or bring its own language model.
