# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-25 (commit `14f9d21`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 87/87.
  (`intl.Dates` fails about one run in ten: it reads the real clock.)
- `analysis/coverage.py build/MP2x00US --check`: 9643 citations, 0 bad;
  5129 of 16671 functions (30.77%).  (The engine's functions are plain
  C names with no mangling, so they count as citations but not towards
  the function total, which comes from the demangled symbols.)
- `analysis/natives.py`: 863 of the ROM's 1326 natives answered.
- The machine boots into the Setup assistant, `src/host/demo/setup.ns`
  taps its way through to the Notepad, and Names, Dates, Extras and the
  Preferences roll (down to the Handwriting Recognition slip and its
  Options popup) all open and draw.
- `build/host/host/newton --rom build/MP2x00US/rom.bin --display 320x480
  --headless 45 --script src/host/demo/ink.ns` boots, and writing on the
  Notepad stays on the page (`build/ink-kept.pgm`).

Writing travels the whole way from the tablet to a paragraph; the route
across the five areas is `docs/ink/README.md`'s "From the pen to ink on
the page", and `src/host/demo/ink.ns` photographs it with the pen down
and again after the recogniser has finished.

The last run of work closed, in order:

- the word domain and the engine's side of it, the readings
  (`TWordList`), the try string, the word info frame, the word
  recogniser front, an ink-only engine and the `aeInkWord` command -
  which together are what put writing on the page;
- `AddNewParagraph`'s geometry for a word the recogniser *reads*
  (`TextBounds`, `AlignBounds`, `AlignToLineSpacing`);
- `UpdateStyleTable`, so a font drawn scaled gets its synthesised faces
  scaled with it;
- `SetInkWordFontParms`/`TInkWordGlyph::SetFontParms`, so a style run
  restyles the writing in it;
- the ink half of the join gesture - two words of writing merged into
  one;
- the area information an engine keeps per writing area
  (`DomainParameter`, `ConfigureArea`, `SetParameters`, `DomainOn`);
- the writer's recognition preferences (`ReadCursiveOptions`);
- **things put into a paragraph from outside**
  (`TParagraphView::HandleInsertItems` and the eleven-argument appender
  under it, `DoInsertItems`, `InsertItemsAtCaret`,
  `GetAppendDelimiter`), which was the piece three others were waiting
  on;
- the **caret side of `aeInkWord`**: a page whose caret is in one of its
  own paragraphs now takes the word into that paragraph;
- **`CheckAndDoSplitInk`**, a word of writing cut in two at a caret -
  and with it `OffsetToBounds` answering a character's right edge as
  well as its left.
- **`TParagraphView::HandleWord`** and everything under it - the
  Finder, `FindWordInRun`/`FindWordInParagraph`/
  `SetFinderBelowParagraph`, `AddWord`, the last-added-word geometry -
  so a recognised word goes into the paragraph it was written on, and
  `TextContainingPoint` finally finds the text under a point;
- the **third case of `aeInkWord`**, a word that starts a new line in
  the paragraph above the caret.
- **the correction information** (`recognition/CorrectInfo.h`), the list
  of what the machine remembers about the words already on a page -
  made, found, kept up to date as the text is edited, learnt from when
  a word falls off the end of it, and written to at last by
  `TEditView::HandleWord`;
- **a letter written over a letter of a word**
  (`ReplaceCharacter`/`DoReplaceSym`), the last branch of
  `FindWordInRun` and the only claim that scores 6 - and with it the
  remote-writing bracket the corrector puts round a word, and a rich
  string keeping its writing when it is dropped into a paragraph.
- **searching the text of entries** (`docs/stores/README.md`): a `text`
  or `words` query walks the compressed text object beside each entry
  rather than reading the entry, so Find now finds a note.  What is left
  of it is the word hints (`TWordHintsHandler`, so the filter in front
  of the walk is always open) and the large binaries of an entry.
- **the spelling checker** (`recognition/Spelling.h`): the session and
  its chains, whether a word is spelled right, and what it might have
  been meant to be - five kinds of change against the dictionary, two of
  them walking the trie and a table of 181 letter groups together
  (`analysis/spellmaps.py`).  With it, a double tap on a word opens the
  corrector.
- **the whole of the writing path above the engine**: the machine boots
  into the Setup assistant, walks through it to the Notepad, opens
  Names, Dates, Extras and the Preferences roll, takes writing and keeps
  it, and a double tap on a word asks for the corrector.
- **the dictionaries**, end to end: the list and the three chains a
  lookup walks (`recognition/Dictionaries.h`), the 129 lexicons built
  into the ROM opened where they lie
  (`recognition/ROMDictionaryData.h`, whose table
  `analysis/romdicts.py` recovers from the code that writes it), the
  words the locale brings with it, the Airus delete path, and the
  writer's own dictionaries - the expansions and the words the machine
  adds on their behalf (`recognition/Learning.h`).  `LookupWord` now
  answers out of the ROM's own word lists.  All thirteen of the
  dictionary and cursor natives a script uses are answered, the last of
  them being `TAirusIterator` (`recognition/AirusIterator.h`), the
  cursor a script walks a dictionary with, and the list's own
  (`AddDictionary`, `GetDictionaryData`, `SetDictionaryData`).  Two
  dictionary natives are left, each blocked on a piece of the engine
  nothing else wants: `ConvertDictionaryData` on the completions walk
  (`AEnum_FirstLast`/`AEnum_NextPrevious` under `FirstCompletion`/
  `NextCompletion`) and `GetRandomDictionaryWord` on the random word
  generator (`RandomCommonWord` over `GetDistributedWord` and the
  `charWeights` table).  The sixteen-bit walkers are the third gap, and
  no dictionary in this ROM is sixteen-bit - see
  `docs/recognition/README.md`'s "What is left of the engine".

- **the machine's own power natives** (`docs/system/README.md`): the
  battery frame and its raw twin, `BatteryLevel`, `BatteryCount`,
  `SetBatteryType`, the backlight pair and `SetRandomSeed` - all of them
  over `hal/Power.h`, which a port supplies.  Finding them turned up a
  hole in the reconstruction: `SetGrafInfo` had no case for the
  backlight (selector 5 is the driver's feature 2) and a case for a
  selector 6 the ROM has not got, so nothing could switch the light.

- **a selection restyled** (`docs/views/README.md`,
  "Restyling a range"): `ChangeStylesOfRange` is now the ROM's own -
  the range replaced by itself through `aeReplaceText`, so a restyle
  undoes - with the `fontParms`/`command` form the Styles slip sends
  (add, remove or toggle face bits, the toggle deciding on the first
  run).  The font arithmetic under it is `qd/Fonts.h`
  (`MakeCompactFont`, `SetFontParms`, `IntFontToFontParms`,
  `FamilySymToNum`, `GetFontFamilyNum`) and the script side is the new
  `views/FontNatives.cpp`, with `GetRangeText`/`ExtractTextRange`.
- **the caret from a script** and the four word scanners
  (`docs/views/README.md`, "The caret from a script"): `SetCaretInfo`,
  `ShowCaret`/`HideCaret`, `ScanNextWord`/`ScanPrevWordEnd`.

- **plugging in the native functions**, which is a measured job now:
  `tools/newton-rom/analysis/natives.py` says which of the ROM's 1326
  natives the reconstruction answers, groups what is missing by area,
  and with `--unbound --ready` picks out the ones whose ROM function is
  already reconstructed.  Three batches went in - the strokes, ink and
  try string; the popups and what a view allows to be written on it; the
  key commands; text put in, views tied and the key commands sorted;
  the colours, `StrWidth` and the protocol registry; a bitmap made and
  drawn into, and a view drawn into one; the tones, the sound settings
  and the large-binary questions; the power statistics; two sweeps of
  the thin wrappers picked out with `natives.py --sizes`; the polygon
  shapes; a class info as a script reads it - taking it from 728 to 832.  Two reconstruction bugs
  came out of the tests for them: `FMakeRichString` wrote its halfwords
  the ROM's way round, so no rich string it made ever read back as
  having ink in it; and `TRootView::SetPopup` was missing the arm that
  closes the popup that is up, without which `DismissPopup` loops for
  ever.

- **the clipboard** (`docs/views/README.md`, "The clipboard"):
  `TClipboard` and the root view's two arrays of contexts, so a drag let
  go on the background now becomes a clipping - the items' data taken
  off the source view, the picture of it kept in the clipping's `bits`,
  the label cut to fifty pixels with an ellipsis and laid out as an icon
  pinned to whichever edges of the application area it touches (and put
  back against them by `FReOrientLabelForm` when the screen turns).
  `GetClipboard`, `SetClipboard`, `ClipboardCommand` and
  `GetClipboardIcon` are answered; `TView::GetClipboardDataBits`,
  `TView::DoMoveCommand`, `PointOnClipboard`, `CheckViewBounds` and
  `OffsetBoundsRef` came with it.  NOT YET: the pen-tracked drag itself
  (`TView::Drag`, the icon following the pen), so the host's simplified
  `DragAndDrop` takes a drop with no target as "let go on the
  background", and `MoveIcon` has no caller.

- **the hilite stroke** (`docs/views/README.md`, "The hilite stroke"):
  the pen held still on something and then drawn through it or round it,
  which is how the Newton is told what to select.  `TRootView::Hiliter`
  follows the pen and draws the line over a saved copy of the screen;
  `TView::AddHiliter` and `TEditView::AddHiliter` decide whether it was a
  lasso and offer it to the children, who answer with the kind of hilite
  they would take (`TContainerView::HandleHilite` promoting a child's
  whole-object claim to the container); `TParagraphView` answers all
  four of its kinds, the interesting one being `HiliteRange`, which
  walks the stroke's outline (`TUnitPublic::RoughShape`/`AsPolygon`)
  from each end for the word boundaries it runs through.  `hiliter` and
  `HiliteViewChildren` are answered.

  Two holes in the reconstruction came out of it: `TView::DoCommand` did
  not pass an unanswered command on to the parent, so nothing posted to
  a view could ever reach the root or the application (and `aeHide` and
  `aeDropChild` were not marking themselves taken); and
  `TRootView::CommonSetKeyView` asked `GetHiliteView` where the ROM asks
  `GetEnclosingEditView`, so selecting a second paragraph dropped the
  first one's selection.

- **the Intelligent Assistant's C++ side** (`docs/assist/README.md`):
  the whole of the ROM file at 0x00084064-0x000871d0 - the class
  hierarchy a sentence is matched against (`ISATest` and the six
  functions over it, `GetClasses` picking one class per word out of what
  it might mean), the task templates (`RegTaskTemplate`,
  `GetRelevantTemplates`, `FillPreconditions`, `AddEntry`) and the
  string tidying a sentence goes through (`GenerateSubstrings`, which
  makes every run of consecutive words so that a phrase of several is
  found in the lexicon at all).  `GetRelevantTemplates(@8.person)` now
  answers `["schedule", "find", "mail", "fax", "call"]` - the five
  things the machine knows how to do to a person.

  NOT YET: the lexicon's own trie (`TrieAdd`, `DynaTrieDelete` over
  `gDynaTrie`), which sits on top of the Airus engine (that engine
  itself is reconstructed, `docs/recognition/README.md`), so a
  registered template's words are not indexed and nothing finds it by
  writing one of them.

- **the text engine, from the bottom** (`docs/text/README.md`): the
  Newton's other text system - the document engine behind protoTXView,
  1800 symbols of its own - started at its foundation, `text/TXArray.h`:
  the growable array in a relocatable handle whose `chunk` is the whole
  memory policy, the array sorted by a leading long, and `TXRanges`,
  which records a division of the text by storing only the end of each
  range and answers `OffsetToRangeIndex` and `SectRanges` over it.  That
  one representation is how every division - style runs, lines,
  paragraphs - is kept, so it is what the rest stands on.  Then the
  attributes a run points at (`text/TXAttributes.h`) and the character
  storage itself (`text/TXChars.h`): the text in chunks of at most 512
  characters, the three ways `Replace` tries to get text in, and the
  running-together of chunks that keeps an edited document from ending
  up made of crumbs.  Then the byte streams under all of it
  (`text/TXStream.h`): `TXStream` and its `ReadBytes`/`WriteBytes`, the
  handle stream, the binary stream with its slack, the temporary stream
  factory, and the chunk table written out and read back
  (`WriteChunksRanges`/`ReadChunksRanges`) - which is what a text
  descriptor needs to name a stream at either end.

  Then the rulers (`text/TXRuler.h`): `TXTab` and the sorted
  `TXTabsArray`, `TXBasicRuler` and `TXAdvancedRuler` over the attribute
  object, the blanks and tab widths a line is laid out with, the line
  spacing, and the ruler frame a script sees.  Then the object ranges
  (`text/TXObjectRange.h`), where the rulers and the styles meet the
  text: which run of characters points at which attribute object, the
  sharing of equal objects and the running-together of neighbours that
  hold one, `TXObjectIterator` and the six-slot pool of shared objects.

  NOT YET in the streams: the factory's large-binary arm (it wants
  `FLBAllocCompressed` and large binaries, which are not reconstructed;
  it answers `kError_No_Memory` as the ROM's own does when nothing came
  of it).  NOT YET in the rulers: `TXRulerRange` (the rulers a
  document's paragraphs actually point at, which wants the runs) and the
  ruler's user interface (`TXRulerUI` and its three bars).  Above them,
  nothing yet.  The next piece is the runs themselves - `TXRun` (the
  attribute object a piece of text points at, with `TXTextRun` and
  `TXGraphicsRun` under it), `TXRunRange` and `TXRulerRange` over the
  object ranges, and `TXStyledText`, which holds the characters and the
  two ranges together.  Then `Textension`, the formatter and the lines,
  and `TXView` with its forty-one `FTX...` natives.

## Next: what is left of the natives, and why

The thin wrappers are done.  What `natives.py --unbound` still lists is
463 natives, and they are not a long tail of small jobs: nine out of ten
of them are the script-facing face of a subsystem that has no
reconstruction behind it at all.  Binding one of those means writing the
subsystem, not the wrapper.

| how many | what is under it |
|---|---|
| 145 | communications: endpoints, CCL, AppleTalk, IR, NTK, the desktop connection |
|  47 | the Intelligent Assistant: its lexicon and the sentence-level functions |
|  46 | the books and newspaper system |
|  38 | the text engine (TXView/TXFrames: styled documents with rulers) |
|  38 | the Rosetta handwriting engine: letters, training and reading |
|  36 | the test agent and the debug hooks |
|  35 | the package manager and the card |
|  12 | sound channels (the sound server) |
|   6 | the text engine's ranges and the book reader's HiliteBlock |
|   4 | large binaries on a store, and store passwords |
|  55 | everything else, a handful each |

Regenerate that table at any time with `natives.py --unbound --csv`, and
find the cheapest work inside a group with `--sizes build/MP2x00US`.

**The handwriting engine has been started.**  `TRosRecognizer`, the
`TWRecognizer` implementation the ROM plugs its engine in through, is
reconstructed (`recognition/RosRecognizer.h`), and the fifteen calls it
makes into the engine are declared as an explicit seam
(`recognition/Rosetta.h`) with no bodies yet.  The engine below is
ParaGraph's Calligrapher: about two hundred kilobytes in six layers,
mapped out in `docs/recognition/README.md` under "The Rosetta engine",
which also says what to do next and in what order.  Level 1 is
finished, and so are the geometry the engine measures in
(`toolbox/FixedGeometry.h`) and its strokes and stroke lists
(`recognition/RosStrokes.h`, level 5).

**Level 3 has been opened at its state block.**  The word recogniser
keeps everything about a piece of writing in one flat 0x208-byte block
that every layer reaches into at fixed offsets, so the block had to be
named before anything above or below it could be written, and
`recognition/WordRecog.h` now names it as far as the evidence goes,
with its whole life: made, allocated, suspended, resumed, reset,
cleared and destroyed, the run of measurements saved and put back, the
grammar context picked by name, the cap height learnt from a word, the
readings handed back, and the four tests that decide whether a stroke
has to be cut in two before it is read (`WordRecogStrokeType`,
`IsStrokeTooWide`, `StrokeIntersectsTwoVerticalStrokes`,
`StrokeNeedsFragmenting`), and `WordRecogAddStroke2` - the baseline of
a closed word and the run learnt from every stroke, which is where the
nine Gaussians turned up.  What is left of level 3 is
`WordRecogAddStroke` itself (eight kilobytes) and the segment side.  **Level 2's life is reconstructed** with it
(`recognition/Rosetta.h`): waking, quietening and sleeping over the one
`gWordRecog`, the working values, the baseline, the character set and
what the engine is told to stop doing.  What is left of level 2 is
`RosettaSetArea` and the three passes a classify is made of.

**The engine wakes.**  `analysis/bigrammar.py` generates
`src/recognition/ROMGrammar.cpp` - the eight bigram grammars a field
asks for by name, each a list of *kinds of word* (a lexicon out of
`gROMDictionaryData`, a score of its own, and a score for every kind
that may follow it), written out in `docs/recognition/grammar.md` - so
`RosettaInitialize` now makes a word recogniser that knows the eight
grammars and the 166 characters it may answer.  What is missing is
*reading*, and it is a subsystem of its own rather than a piece of
work: `docs/recognition/bpnet.md` inventories it.  The bottom of it - the
classifier net, its trained tables, its life and `BPNetEvaluate` - is
reconstructed (`recognition/BPNet.h`, `analysis/bpnet.py`), and that
page has the assembly it came out of and the three numbers the net
records about itself that the reconstruction is checked against.  The
0x03500000 the routine adds to its weight pointer turned out to be the
whole ROM mapped a second time *uncached*
(`g8MegContinuousTableStart`, ROM 0x100), so that streaming 91KB of
weights does not flush the StrongARM's data cache.

**The patternizers are done** (`recognition/NetPattern.h`): the
little class system they are written in, the composite that holds one
per input group, and the five scalars.  That established what the
classifier is actually shown - the net's 384 inputs are a 14x14
picture of the writing (196), a 20x9 grid of where the pen went (180),
the aspect ratio (1) and the stroke count (7).  `ImageSplatLimited` is done too, over the
engine's own renderer (`recognition/Render.h`, `analysis/render.py`),
which anti-aliases by drawing at four times the size into a one-bit
bitmap and counting the set sub-pixels through a table.  `StrokePUD` is done as
well, so **all seven patternizers are reconstructed** and a stroke
list now reaches all 384 of the classifier's inputs in one call
(`test_NetPattern` does exactly that and then runs the net).

**And the engine reads.**  `CharBox` (`recognition/CharBox.h`), the
boxed-character recogniser, is the piece that joins the classifier to
the character codes: a recogniser over a rectangle, up to six strokes
put into it, and `CharBoxNetEvaluate` turning the net's 134 outputs
into a probability for each of the 256 codes - nothing for a code the
area will not have, the node's own output for a code that stands for
one shape, and the *product* of two nodes for a code that is really two
characters (166 of the US ROM's codes are legal and 54 of those are
compound).  `test_CharBox` draws an upright stroke crossed by a level
one and gets back `+` (0xf100), `t` (0xe500) and `T` (0x0100) and
nothing else, which is the reconstruction reading handwriting for the
first time; `docs/curiosities.md` has it.  `CharBoxEvaluate` - the
scores and the geometry penalties - is NOT YET, because it wants the
segment layer.

**The segment layer is begun** (`recognition/Segment.h`): what a
`RosSegment` is - a stroke list, its box, whether any stroke in it is a
dot, and how big the smallest of them is - and the five measurements
the cutting is made of: `SegmentDot` (small in *both* directions),
`SegmentAspect`, `SegmentOverlap` (the mean of the two fractions of the
line two boxes share), `SegmentStrokeMinDistance` (which two points of
two strokes come nearest, searched by |dx|+|dy| and only then measured
properly) and the pair `SegmentCrossed`/`SegmentNonTailLinked`, which
say whether that nearest approach is in the middle of both strokes or
at their ends - a t against a V.  The second of those two carries a ROM
bug: it tests two of the four clauses its question comes to and uses
the wrong stroke's margin in one of them, so the end of a long stroke
touching the middle of a short one is missed.  Kept and demonstrated in
`test_Segment`.  `SegmentSetStrokes` needed the stroke joiners, so
`StrokesAdjoin`, `StrokeJoin` and `SLJoinFragments` are in
`recognition/RosStrokes.h` now.

**And the first pass of the cutting is done too**: `SegmentChars`
works the two widths out of the writing's height (a letter is half of
it, two strokes touch within a tenth of it, each capped at two and a
half times what the nominal 18.85 pixels would give) and
`SegmentStroke` runs over every stroke, deciding whether it and the one
before it are part of one letter - three thresholds on how much of the
line they share, the lower two needing `SegmentCrossed` or
`SegmentNonTailLinked` as well - and whether a cut may go in front of
it.  `SegmentMultiStrokeMinDistance` and
`SegmentMultiStrokeMinDistBoundX` look **three** strokes back, because
a letter is often written in pieces that are not consecutive, and one
forward for the case where the writer went back to dot an i.
`rosCI`'s `fMinCharWidth`, `fCharWidthFraction`, `fReachFraction` and
the four overlap thresholds are named for all this.

`SegmentSetStrokeOverlaps` is done as well - a segment's strokes told
how much of the line each shares with the one before it *now that the
segment has them in its own order*, the first measured against the last
stroke of the segment before it.

**And the second pass is done: the segment layer cuts writing into
letters.**  `SegmentMakeSegments` is incremental - called once per
stroke and once more at the end, keeping its working-out in the
0x44-byte `SegState` that `SegmentQuiesce` gives back - and ends a
piece for one of three reasons: the first pass marked the stroke, the
aspect ratio passed 1.5 (or 1.75 when a dot has already widened the
box), or the piece has more than five strokes (six with
`FragmentLigatures`).  A cut may not land in the middle of a run of
linked strokes, so it walks back to one it may land on; failing that it
cuts anyway and rewrites the links, which is the engine admitting that
a run it thought was one letter cannot be.

**What it hands up is a lattice, not a partition**: for a piece it
emits every grouping the links allow - the first stroke, the first two,
the first three, then the same from the second stroke - so three
strokes it cannot tell apart come back as six segments, for the layer
above to score.  `test_Segment` drives the whole thing: two x's written
as four crossing strokes come back as exactly two segments, and three
upright strokes five pixels apart as all six groupings.  One thing is
transcribed rather than understood - the per-stroke `fField24`, which
chooses between the lattice and one grouping for the whole piece - and
it is `WordRecogAddStroke` that decides what it is.

`SegmentSetWordSpacing` is done too - the writer's spacing slider (1 to
9, five in the middle) turned into the factor the layer weighs gaps by,
its natural logarithm (taken in double precision, the only floating
point in the engine, because the layers above add it) and the threshold
interpolated between `Min`/`Mid`/`MaxSegOnlyThreshold`.  It carries a
ROM bug worth reading: the constant above the middle setting is
`0x170000` where `0x17000` was surely meant, so the top half of the
slider runs 6.75, 12.5, 18.25, 24 where the bottom half runs 0.32 to
1.00.  `docs/curiosities.md` has it.

The word recogniser's own way into the classifier is done as well -
`WordRecogNetEvaluate`/`WordRecogNetSetInputs`, the twins of the
`CharBox` pair, keeping the patternizer on the recogniser because a
word is read one candidate letter at a time.  `test_WordRecog` puts the
same writing through them that `test_CharBox` puts through the other
path and gets the same three answers.

The grammar's own allocation is done as well - `BiGrammarNew`,
`BiGrammarCreate`, `BiGSliceNew`, `BiGSliceDestroy` and a real
`BiGrammarDestroy`.  Both a grammar and a slice keep their arrays
behind the struct in the same block, which is what makes each of them
one allocation and one `DisposPtr`.  Reading them settled two fields:
a slice's `+0x1c`/`+0x20` are its live count of following kinds and the
room it has for them (equal in the ROM's tables only because those are
full), and `+0x2c` is what a slice is *made* with, 0xff, so the nine
`LexicalSymbols` kinds carrying nought and `wordlike` carrying one
mean something.  `BiGrammarCreate` takes a name and never stores it, so
a grammar the engine builds for a field is nameless.

**And `RosettaSetArea` is done**, with the grammar machinery under it:
`BiGSliceCreate`, `BiGrammarAddSlice` (unnamed in the ROM, inside
`BiGrammarModifyContext`), `BiGrammarClone` and
`BiGrammarModifyContext`.  Reading them turned up what a grammar's
scores actually are - **negative natural logarithms of probabilities
scaled by five hundred** - which is why the engine adds everywhere and
why `ArProbDecodeLu`/`ArProbEncodeLu1`/`ArProbEncodeLu2` (now generated
into `ArProbTables.cpp`) exist at all.  `docs/curiosities.md` has it.

So a field's configuration now becomes a grammar: eight flag bits pick
one of the ROM's seven special grammars, anything else gets the General
grammar narrowed by `BiGrammarModifyContext` (nine tenths of the
probability to the kinds the field wants, the rest to everything else,
and the likeliest brought down to nought), every slice's dictionary
index becomes the data itself, and the field's own symbol set narrows
`RosCI->fLegalUse`.  `test_Rosetta` drives all four paths.

A ROM bug kept: `BiGrammarClone` copies the shorts at +0x08, +0x0a and
+0x0c but not the one at +0x0e, and `BiGSliceNew` does not clear it, so
a cloned slice's `fField0e` is whatever was in the heap.  It is nought
in all 46 of the ROM's own slices.

**`WordRecogAnalyzeWord` and `CharGetAvgBoxBHW` are done**, which is
the top of the reading path.  A word is measured - the mean base over
the segments that are more than a dot, the mean height and width over
the ones big enough to count, and the tallest and widest raised to what
the word's overall shape suggests - and three of the four lengths the
engine keeps about the writer's hand are moved an eighth of the way
towards it, but only when the word is within half to twice what it
already believed, and then held to between half and twice their
nominal.  Then every candidate letter in the lattice goes through
`WordRecogNetEvaluate` and on to the search with a confidence worked
out from how much of the line its strokes share.  `WordRecogEndWord`
closes the word.

**The search's readings are done** (`recognition/WordTails.h`): a
reading is a backwards linked list of single characters, reference
counted so the dozens of partial readings the search holds at once
share their ends, named by a 16-bit reference whose bits are the table
and the slot.  `WordTailBlockAllocate`, `AddRef`/`DeleteRef`, the two
`Sprint`s, `WordTailCompare` and the word-list pool
(`WordListFreeAll`/`DeleteRef`/`Sprint`) are all real, and
`docs/curiosities.md` has the idea.  A ROM bug kept: `AddRef` does not
answer early for the empty tail where `DeleteRef` does.

Still NOT YET: **the search itself**, which is a Viterbi over the
lattice - `SearchAllocateGlobals` (728 B, 37 columns of up to
`MaxBestNodes` = 27 nodes each), `SearchBeginWord`,
`SearchProcessSegment` (584 B), `SearchDoViterbStep` (1324 B),
`SearchDoVStepFromNode` (2120 B), `SearchSegwordRememberNBest`,
`SearchFindBest` (1108 B), `SearchBestWords`, `SearchSendWords`,
`SearchEndWord`, `SearchCheckHashHit`, `GCBestNodes` - about 8 KB in
all; and `CharModifyProbs` (1632 B), which leans on the classifier's
probabilities with where and how big a piece of writing is.  The state
block is already mapped: 37 columns of 0x94 bytes, each holding up to
30 node pointers, a count at +0x78 and a word list at +0x90, with nodes
of 16 bytes carrying a word-tail reference at +0x0a.  That is the next
piece: the engine now measures, classifies and scores a word but still
answers nothing.

Also still NOT YET: the word-spacing and gap functions `SegmentWordXGap`
(5952 B) and `SegmentWordVert` (1892 B), about 10 KB in all;
`WordRecogAddStroke`/`AnalyzeWord` (10 KB), `RosettaSetArea` and the
classify passes (3 KB), and the feature extraction
`low_type`/`EXTR`/`SPEC_TYPE`, which is 556 KB and 2384 symbols on its
own.  `CharBoxEvaluate`, `WordRecogAddStroke` and
`WordRecogAnalyzeWord` all wait on the segments.

**The engine's own numbers are real.**  `analysis/rosci.py` generates
`src/recognition/RosCITables.cpp` - the 0x10c-byte `rosCI` template
`CharInitialize` copies, and the seventeen character tables it points
at - so `RosCI` holds the ROM's trained numbers rather than a
stand-in.  The rest of the neighbouring layers are declared in
`recognition/RosEngine.h` and are still NOT YET: the grammars
(`ROMGrammar`, eight finite-state contexts in the ROM's data), the
segments, and the classifier net with its patternizers.  The rest of level 3 is
`WordRecogAddStroke`/`AddStroke2` (the strokes in),
`WordRecogAnalyzeWord` and the net calls (reading them) and the segment
side, `WRSeg*`.

The smallest of those that would close a group of its own:

- **sound channels** (12): `TSoundServer`/`TSoundChannel` above the
  codecs, which are done.

And a handful that are blocked on one function each, named in the list
that `natives.py --unbound` prints: `Dispatch`, `RegisterGestalt` and
`ReplaceGestalt` want `PrimCallProtocolFromFrames` (the marshalling of
NewtonScript values into a C call); `GetBitmapInfo` wants the
large-binary questions to mean something; `ComputeParagraphHeight` wants
its geometry read out of the assembly rather than the decompiler.

Keep going through `natives.py --unbound`.  The areas whose machinery
exists are `views` (18 left), `recognition` (46), `qd` (13), `sound`
(13), `system` (16) and `stores` (11); `comms`, `books`, `assist`,
`testing` and `packages` are mostly areas that are not reconstructed at
all, and a native there is a project of its own rather than a wrapper.
The named pieces whose machinery *is* there:

- `HiliteBlock` 0x00164d64, which looks like a view native but is the
  book reader's: it wants `TLibrarian` and the page frames;
- the rest of the bitmap and shape verbs (`MakePict`, `PictToShape`,
  `MungeShape`, `MungeBitmap`, `GetShapeInfo`, `FindShape`);
  `GetBitmapInfo` also wants `GetBinaryStore`/`GetBinaryCompander`,
  which answer nil on a host because there are never large binaries;
- `instance:Dispatch` 0x00195228, which wants
  `PrimCallProtocolFromFrames` - the marshalling of NewtonScript values
  into a C call - and with it the `Gestalt` registration natives
  (`RegisterGestalt`, `ReplaceGestalt`);
- `ComputeParagraphHeight` 0x001ecfd0: its geometry is built on the
  stack through an unaligned `ldr` and is worth reading from the
  assembly rather than the decompiler.

## Also still open

- `SetUpRosetta` and `SetUpParaGraph`, which `ReadCursiveOptions` would
  call: both belong to the engines.
- The printing path's outlined paths for ink (`CSMakePathsGroup`,
  `FramePaths`), which want the PostScript path machinery.
- `TWRecognizer::EndInkStrokeGroup` (the CIC library's
  `WRecEndInkStrokeGroup`).
- Nothing chooses *which* word recogniser is in use at boot: `UseWRec`
  is a native and `SetWordRecognizer` is reconstructed, but the ROM
  calls them from a script, and the host calls `SetWordRecognizer`
  itself in `HostStartViews` and `TNotebook::InitToolbox`.

## Working notes that keep being needed

- Unaligned `ldr rN,[X+2]` rotates the aligned word right by 16 - read
  halfword loads out of the disassembly, never the decompiler. Halfword
  stores come out as two `strb`. This matters most in functions that
  build `Rect`s and `Point`s on the stack: Ghidra's output for
  `AddNewParagraph`'s geometry is almost unreadable, and the assembly is
  not.
- The view classes have no vtable in `romfacts.json` - they are built
  by `BuildView`, not by a self-allocating constructor - so a virtual
  call like `add pc,r3,#0x148` cannot be named from it.
  `analysis/vtable.py build/MP2x00US --find <mangled name> --slot 0x148`
  works back from any method of the class to the table it sits in.
- A function with more than four arguments spills the rest above the
  frame: with `sub r11,r12,#N`, argument five is at `[r11,#N]`. The
  decompiler often loses these entirely.
- `__rt_sdiv`/`__rt_udiv` take (divisor, dividend) and answer the
  quotient in r0 and the remainder in r1.
- `TArray::IArray(elementSize, count)` sets `fCount = count`: the array
  *has* that many entries, so `TStroke::Make(n)` starts with n points at
  nought. Use `Make(0)` when the points are to be added, `Make(n)` when
  they are to be written in place.
- ROM arithmetic wraps; host `long` is 32-bit on Windows and traps under
  the sanitiser. Wrap explicitly through `ULong`.
- **Editing the sources from a script: match the file's line endings.**
  Most of `src/` is CRLF in the working copy and LF in the repository.
  A Python helper must read and write with `newline=''` and splice with
  the endings the file already has, or the whole file shows up as
  changed. `sed -i` is safe; a bare `io.open(p, 'w')` is not.
- A Bash heredoc whose delimiter is *not* quoted (`<<PY`, not
  `<<'PY'`) runs command substitution on the backticks inside it,
  which silently mangles any Python that writes Markdown.  Quote the
  delimiter and put paths in the script rather than interpolating
  them.
- A `\n` inside a C string literal written through a Bash heredoc loses
  a backslash. Use the Write/Edit tools for those, or build the two
  characters as `chr(92) + 'n'` in a Python helper.
- `git checkout -- <file>` throws away uncommitted work. Commit the
  piece first, or stash it.
- `RemoveView(parent, child)` takes two arguments; calling it with one
  throws `evt.ex.fr.intrp` from inside `Eval`, which is easy to misread
  as a fault in whatever was being tested.
- A `StyleRecord`'s scalars now start clear (`qd/Fonts.h`), because the
  ROM's callers allocate theirs in cleared memory and rely on it. It
  holds a `RefStruct`, so it still must not be `memset`.
- A test that starts the recognition system without booting must put a
  `userConfiguration` and an `international` frame in the globals first:
  `ReadCursiveOptions` reads both, as it does on a real machine.
- A test that needs the protocol registry (anything making an instance
  by name) must run as the kernel services task: `gHostKernelServicesTask
  = ...; OsBoot();`, ending with `HostStopTasks()`.
- A `starterParagraph` form is built through the `'para` stationery,
  which only a booted Notepad has registered; a test that has no
  stationery can check the form but not the view made from it.
- `coverage.py --check` matches one citation per line and checks the
  mangled name against the ROM's symbol at that address - a plain name
  where the ROM has a mangled one (or the other way round) is reported.

- **The text engine's `TXOffset` is a two-word struct, not a long.** Its
  mangled name appears as a class (`...F8TXOffset`), and the ROM passes
  it in two registers: the offset, and a flag saying whether an offset
  that falls exactly on a boundary belongs to the range it ends or the
  one it starts.  `src/text/` renders it as a `long` plus an explicit
  `atStart` argument, which is right for every function reconstructed so
  far; but `TXRulerRange::CharRangeToParagRange(TXOffset*, TXOffset*)`
  takes two of them *by pointer* and writes the flag back, so that one
  needs the real struct.  Introduce it (offset + atStart) before
  reconstructing the ruler range, and let the existing two-argument
  calls keep working.

- The text engine's next piece is `TXRun` (0x00245e64: an abstract
  attribute object with twelve virtuals, of which only `Assign`,
  `FullJustifPortion`, `VisibleLen`, `Click`, `SetHilite` and
  `DrawHilite` have bodies - the rest are pure and answered by
  `TXTextRun` and `TXGraphicsRun`) and `TXRunRange` (0x00245cc4: a
  TXObjectRange whose `CharToTextRun` searches backwards and then
  forwards for a range whose run `IsTextRun`).  Then `TXRulerRange`
  (0x00242c68), which is a TXObjectRange plus a `TXChars*`, a *pending
  ruler* and a flag: when the caret sits at the very end of the text
  after a line break, the ruler a slip sets belongs to the paragraph not
  yet typed, so it is held in `fDefaultRuler` until a character arrives
  (`GetPendingRuler` 0x00242eac, `InvalidatePendingRuler`,
  `NukePendingRuler`, and the `OffsetToObject`/`UpdateRangeObjects` that
  answer out of it).  It wants `TXGetParagStartOffset`/
  `TXGetParagEndOffset` as well.
