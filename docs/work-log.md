# Work log

The record of finished work, newest first, moved here out of
`docs/next-steps.md` so that that file can stay a short account of where
things stand and what is next.  Each entry is as it was written when the
work was finished: a "Next" or NOT YET inside an older entry may since
have been done (a newer entry, or the subsystem's own page under
`docs/`, says so).  The subsystem pages are the reference for how things
work; this log is how and in what order they came to be, with the host
bugs and ROM bugs found on the way.

## 2026-09-27: deferred recognition

- `views/Rerecognize.h`: writing already on the machine read again -
  `Recognize` (`RecognizeStrokes`, `BulkUnitHandler`,
  `HandleBulkStrokes`, `gBulkStrokes`), `RecognizeInkWord`,
  `RecognizeTextInStyles`, `RecognizePara`/`RecognizePoly` and their
  natives, the two `RerecognizeWord`s with `ParagraphViewWordHandler`/
  `PolygonWordHandler`, `DrawCheckmark`.  Under them the controller's
  `RecognizeInArea` with `SpecialGetAreasHit`/`SpecialHandler`/
  `SpecialExpireStroke` and `gLastWordEndTime`, `MakeRerecognizeArea`,
  `BuildRecConfigForDeferred`, `StrokeCentral::New` (the ROM's
  constructor) and `AddExpiredStroke` (its CIC grouping NOT YET),
  `CountTStrokes(TUnit*)`.  The paragraph answers commands 0x19 and 0x1a
  (`RecognizeInkCommand`, `RecognizeRangeCommand`, `GetCachedRange`) and
  its double tap now reads an ink word again (the two branches that
  were NOT YET); `TPolygonView::RealDoCommand` answers 0x19.
- `src/host/demo/recognize.ns`, ctest `host.NewtonRecognize`: "ton"
  written, and the same strokes read by all four; "ton ton" on the page.
- Found on the way: a NewtonScript `Length` of a string is its bytes
  (terminator included), so a script's offsets want `StrLen`; a view's
  `viewClass` carries flags above the class number.

## 2026-09-28: the cursive recogniser's letter styles

- `InstallWordRecognizer` and ParaGraph's cursive recogniser short of its
  reading: `recognition/ParaGraph.h` (the engine's memory, character
  classes, letter table and learning infos, `SetRamParaData`),
  `XrDomains.h` (the 'STXR' and 'XRWR' domains; the word domain's whole
  `DomainParameter` over an `XRWORDPARAM` kept at the ROM's byte offsets),
  `WordRecognizer.h` (`TWordRecognizer`, `SetupXRD`, the letter weights
  and learning-data natives), `LetterShapes.h` (the Letter Shapes slip's
  natives over the ROM's `letterimages`).  22 natives answered; tables
  from `romtable.py` (`ParaGraphTables.cpp`).
- `ReadCursiveOptions` now calls `SetUpRosetta`/`SetUpParaGraph`, so the
  letter set chooses the word recogniser as on the machine; the host's own
  `SetWordRecognizer` calls are gone.  `SetUpRosetta` asks Gestalt for the
  CPU speed, so the kernel now answers the MP2x00's StrongARM at 162 MHz
  (`gMainCPUType`, `gMainCPUClockSpeed`, `InitCPUGlobals`,
  `hal/System.h`'s `LowLevelGetCPUType`/`GetCPUClockSpeed`).
- `TDomain::DomainParameter` answers a value, as the ROM's does (the
  WRec domain's comment that it did not was wrong).
- Correction: Rosetta is Apple's printed recogniser, not ParaGraph's
  Calligrapher; ParaGraph's is the cursive one (README and CLAUDE.md
  fixed).
- `nsfunctions.py --refs NAME` lists the ROM's NewtonScript functions that
  call a native or send a message by that name.
- The newtonscript host's stand-in for the boot now makes the System soup
  and a default letter set, which the recogniser's installation reads.
- ctest `host.NewtonLetterStyles` (`src/host/demo/letterstyles.ns`).

## 2026-09-27: packages loaded from the host

- `newton --package file.pkg` (repeatable) and a .pkg dropped onto the
  window install a package through the package manager
  (`host/HostPackages.h`, `docs/packages/README.md`'s "Loading a package
  from the host"): a queue, a `'scpt` event to the newt world naming the
  root view's `hostPackages:Install`, `LoadPackage` from a block of the
  package's own.  `TNewtWorld::PreMain` gained the host hook
  `gNewtHostPreMain`.  The window accepts dropped files (`shell32`).
- `packages.py --extract` gained `--relocatable` and `--rename OLD=NEW`:
  a ROM package's refs are image addresses, which crashed the importer
  when an extracted one was loaded from memory; rebased, a renamed copy
  of Formulas installs beside the ROM's (`GetPackages()` lists it).
  ctest `host.NewtonPackage` checks it.
- Host note: `<mutex>`/`<string>` cannot be included in a file built
  with the Newton include paths (libc++'s locale support finds
  `intl/Locale.h` for `<locale.h>`); `<atomic>` is fine.

## 2026-09-27: the package manager

- **The package manager** (`packages/PackageManager.h`, 0x0015bf00-
  0x0015fe48, 0x00161b68-0x00161f90): the 'pckm task started by
  `InitialKSRVTask`, `TPackageEventHandler` (begin-load, next part,
  install part, remove, registry, safe-to-deactivate, backup walk), the
  events (`PackageEvents.h`), `TPMIterator`, `InstallPackage`,
  `LoadPackage`, `DeinstallPackage`.  The part handlers
  (`PartHandlers.h`, `FramePartHandler.h`: 'form, 'auto;
  `stores/PackageStore.h`: 'soup via `InitPackageSoups`, the tail of
  `InitQueries`), `CPackagePipe`, `FramesException`.
  `LoadHighROMFramesPackages` now sends the ROM's packages to the manager
  as the ROM does; `GetPackages`, `PidToPackage`, `GetPackageStores`,
  `IsPackage` go over it.  Boot, `open-apps.ns` and `assist-tasks.ns`
  unchanged (compared against the previous commit's build).
- **The "extrasState" DEVIATION was the ROM's own code**: `TNewtWorld::
  PreMain` sets the extras soup's `extrasState` to `'initialized` after
  loading the packages.  Now done there, as the ROM does.
- Host bugs found: the package names went to NewtonScript byte-swapped
  (the directory's UniChars are big-endian; `GetPackages` showed empty
  titles) - the manager now turns them round once; a part's remove
  object went through a 32-bit `long` and was truncated on removal; the
  newt world's fork opened its port while the forking world was still
  running, and the host's single current-port global left the parent
  holding (and later closing) the fork's port (`TNewtWorld::
  ForkConstructor` puts it back); the app world's event buffer is doubled
  for the host's wider events.
- ROM bugs kept: `docs/packages/README.md`, "ROM bugs kept".

## 2026-09-27: the lock-ups, the clock, and the day view

- **`FrameDirty`** (`stores/SoupNatives.cpp`): `EntryDirty` as a script
  sees it; the Time Zones application asks it of a city's entry.
- **The host clock jumped 19.4 minutes after boot.**  `ULong` is
  pointer-sized on the host, so an `Int64`'s `lo` can hold more than 32
  bits; `HostSteadyClock` put the whole tick count in `lo` as well as its
  high part in `hi`, and once the 3.6864 MHz count passed 2^32 `CompDiv`
  counted the high part twice.  The time read 2^32 ticks ahead and every
  delayed call due in the window fired over and over (384,899 times in a
  25-minute run).  Fixed in both places: `HostSteadyClock` keeps `lo` to
  32 bits and `CompMath`'s `Value` reads only its low 32 bits.  Found with
  a scratch hook starting the steady clock just short of the wrap.
- **The Time Zones home city "lock-up"** was an endless repaint:
  `TRootView::UpdateDefaultButtonAndCaretSlip` had been transcribed as
  doing one thing per call, so a default button that stopped being the
  key view's was dirtied on every update for ever (27,332 repaints of the
  Schedule button in one scripted run), which starved the pen.  The ROM
  dirties the old view and takes and dirties the new one in one pass.
  `tools/host/stacksample.py` (a running host's busy thread sampled
  without a debugger) and `NEWTON_TRACE_UPDATE` (each region repainted)
  found it.  The city lists themselves are closed by their own close box,
  which is how the ROM's scripts are written.
- `newton --headless` longer than 582 seconds overflowed a 32-bit
  TTimeout; it sleeps a minute at a time now.
- The host starts the Newton's clock at local time (the Newton's clock
  keeps local time; `time()` is UTC).
- **The 2010 bug, reproduced**: walking Setup on a 2026 host sets the
  date back to 17 September 1992 - `TimeInSeconds` is a 30-bit integer
  of seconds since 1993.  Kept; `docs/curiosities.md` has it.
- Pattern handles were allocated at the ROM's 0x1c-byte PixelMap plus
  eight rows, where the host's PixelMap is bigger: every pattern made
  from rows (the five standard ones included) wrote past its handle.
- Shape binaries imported from the ROM (`'boundsRect`, `'rectangle`, ...)
  are turned into the host's byte order by `ObjectAreaImport`
  (`nsfunctions.py --binary-classes` lists the classes).
- `Disasm(fn)`, a host function printing a script's bytecode.

## Earlier work (as recorded in next-steps.md before 2026-09-27)

The last run of work closed, in order:

- **QuickDraw pictures played back** (`qd/PicPlay.h`, `docs/qd/README.md`,
  "QuickDraw pictures played back"): `DrawPicture`, `ParsePicCodes` and
  `GetPicBits` over the picture's own bytes, so the World Clock slip the
  Assistant's "time in Paris" opens shows its world map; a picture shape's
  frame is now read big-endian by `MakeShape`.  Text, curves, paths and the
  picture turned into shapes are NOT YET.

- **a meeting in the day view** (`views/MeetingView.h`, `docs/views/README.md`,
  "A meeting in the day view"): `TMeetingView`, `LayoutMeeting`,
  `GetMeetingTypeInfo`/`GetMeetingIcon`, so the Assistant's "schedule lunch
  with Daniel" slip now opens Dates on the day with the meeting in it.  With
  it two host bugs: every pattern made from rows wrote past its handle (the
  heap broke far away, in a `DrawShape`), and the ROM's shape rectangles were
  read in the host's byte order (`ObjectAreaImport` now turns them round).
  `Disasm(fn)` is a host function for reading a script the static tools
  cannot reach.  `TSliderView`, the duration bar (drawn, dragged to change
  the length, scrubbed to delete the meeting), came after.  Next: the date
  the Assistant's "tomorrow" comes to (the meeting lands today).

- **the outline list** (`views/ListView.h`, `docs/views/README.md`,
  "The outline list"): `TListView` and its thirteen natives, so the
  To Do list opens - tapping "Do" on the Assistant's "remind me to
  call Daniel" slip now shows the task in the list with its check box
  and priority.  With it the paragraph's baselines and the ROM's own
  `TParagraphView::SetBounds` (which clamps to the parent, writes the
  box to the data frame and lays the lines out again - the host's had
  been only `TView`'s), `TDataView::HandleTap`, and `FClicker`,
  `FDrawXBitmap` and `FRedoChildrenX` made callable from C++.  Next on
  that smoke run: tapping "Schedule" on a meeting slip opens the Dates
  day view, whose `LayoutMeeting` native (0x001ca1a8) and
  `TMeetingView` are NOT YET.

- **the audit of the code after virtual calls.**  Ghidra had stopped
  disassembling after a `mov lr,pc; add pc,rN,#slot` that was already
  marked a fall-through call, so the code after 60 such calls had never
  been read (`ghidra_import.fix_virtual_calls` now opens it too).  Of
  the functions it cut short, three were fine, `TFaxTool::C2StateUpdate`,
  `TP3Tool::HandlePacket`, `TSharpIRTool::NextState` and
  `GetGraphicBiasedScore` are not reconstructed at all, and the three
  `RealDoCommand`s of `TView`, `TEditView` and `TParagraphView` had
  whole cases missing - which is where the scrub being read as writing
  afterwards came from.  Those are done, and with them what they led
  to: the page and the paragraph as a drag's source and target
  (`GetDragInfo`, `GetDropData`, `Drop`, `DropMove`, `DropRemove`,
  `DropDone`, `FindDropView`), the editing commands
  (`TView::DoEditCommand`: cut, copy, paste, clear), `aeShow`'s wait
  while a modal dialog is up (`ModalSafeShow`), `TView::EndDrag`/
  `DragAndDrop`/`Drag` as
  the ROM has them with `DragBits`, the clicks on a selection
  (`HiliteClick` of both, `IconClick`, the `aeClick`/`aeTapDrag` cases),
  resizing a selection by its gray border (`TrackScale`,
  `DrawScaledViews`, `DrawScaledData`, `DiceHilited`/`AddHilited`, the
  paragraph's `aeScaleData`), and `CleanupData`.  Host bugs found on the
  way: `OffsetBoundsRef` moved `bounds` where the ROM moves `viewBounds`;
  `TView::ClickOptions` answered 0 where the ROM answers 1;
  `aeScaleData`'s four parameters are a rectangle's two words each
  (`CommandIndexRect`), not four shorts; a drop on plain background made
  a clipping, where the ROM makes one only at the screen's edge
  (`PointOnClipboard`); and the paragraph inherited `TView`'s
  `IsCompletelyHilited` (always yes) and `DeleteHilited`.

- **the shape domain**, whole: the units and the grouping of strokes,
  the key points and curves, circles and ellipses, the angle and length
  clustering (`TTrend`), the equations a shape of straight sides is
  written as and the fixed-point conjugate-gradient minimiser that
  solves them, and the snapping onto shapes already on the page - with
  `TPolygonView` and `TEditView::HandleShape` putting the result on the
  page, and `SlopeFromAngle` in `toolbox/Angles.h`;
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
  `OffsetBoundsRef` came with it.  The pen-tracked drag itself
  (`TView::Drag`) came later, with the audit of the code after virtual
  calls.

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

### Also recorded as done then

- The Intelligent Assistant parses and acts (`docs/assist/README.md`):
  the lexicon over the ROM's trie and a run-time one, the phrase
  generator, the Names-file heuristics and `ParseUtter`/`IaAtWork`;
  `src/host/demo/assist.ns` asks "call Daniel" and the Call slip opens.
  Every Assistant native is answered (natives.py).

- The modal dialogs are done (`docs/views/README.md`, "Modal dialogs",
  and `docs/newt/README.md`, "Forks"): `FilterDialog`, `ModalDialog`
  (the newt world forks - `TForkWorld::Fork` now really starts a task -
  and the asking script waits on a `TPseudoSyncState`),
  `ExitModalDialog`, and the unnamed `ForkScript`/`YieldToFork`.
  `src/host/demo/modal.ns` asks `ModalConfirm` and taps OK.  Finding it
  needed a fix below everything: `TULockingSemaphore::Release` waited
  where the ROM does not (`kNoWaitOnBlock`), so the first release that
  had to wake another task hung for good.  Still NOT YET on this path:
  `DoPopup`'s modal `canonicalPopup`, and the task stack limits a fork's
  globals would record (`GetTaskStackInfo`).

## The natives as they stood when the thin wrappers were finished

### What was left of the natives, and why (then)



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



## The Rosetta engine, as it was reconstructed

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



**The search's state and its life are done** (`recognition/Search.h`):

`SearchAllocateGlobals`/`DeallocateGlobals`, `SearchAllocateReturnCache`,

`SearchBeginWord`, `SearchEndWord`, `GCBestNodes` and

`SearchCheckHashHit`.  Thirty-seven columns, one per stroke of the

longest word plus one to start from, each holding up to `MaxBestNodes`

(27) partial readings; `SearchBeginWord` puts one node in the first

column holding the empty reading that every path grows from.

`SearchCheckHashHit` turned out to be an **easter egg** - write one of

eight words three times in a row and the recogniser answers the

recognition team's names and addresses instead; `docs/curiosities.md`

has it, and `analysis/romtable.py` grew a `strN` type for its two

tables.



`SearchProcessSegment` is done too, with `ShiftNetValues` and

`GetBestPath`: the classifier's probabilities and `CharModifyProbs`'s

turned into the two score arrays the step reads, the columns moved

along (they are a **ring** - the pointers rotate, nothing is copied),

and the try string copied out.  `rosCI`'s `fNetScoreWeight` (four

fifths, what the classifier's opinion is worth against everything else)

is named.



**The search is done.**  `SearchDoVStepFromNode`, the innermost thing

the engine does, is reconstructed: one reading grown by one letter,

every way it can be - every kind of word the grammar allows after it,

every character the lexicon allows next, and every case of each.  With

it `LELangNodeNumOut` and the `LE` node formats

(`recognition/LELang.h`: a run lexicon and a chained one, the

variable-width offsets of `AckNodeSizeTab`), and the capitals model the

step charges through - `RosCommonInfo::fCapCostUpper`/`fCapCostLower`/

`fCapCostOther`, twelve contexts each, and `BiGSlice::fCapExtraUpper`/

`fCapExtraLower`/`fCapCostUpper`/`fCapCostLower` for a kind of word

that has opinions of its own.  `test_Search` now drives the whole

search over the ROM's own lexicons and gets a letter back.



**`GeoContextPenalty` is done too**, with `GeoContextAux1`,

`GeoContextAux2` and the cache (`recognition/GeoContext.h`): what the

geometry between two adjacent letters costs, which is the part that

tells `rn` from `m`.  The engine's nominal drawing of a character is

sixteen numbers in `rosCharParams` (now all generated and named: its

bottom, height and width, the room it wants either side, its smallest

stroke written in one stroke and in more, and what each of those is

worth), and the two observed boxes are brought to a common size and

place, fitted by least squares and reduced to nine residuals which go

through a symmetric nine-by-nine matrix as a quadratic form - a

Mahalanobis distance.  The step charges it at **a quarter weight** when

the letter before is in another word or there is no letter before at

all, and in full within a word.  `docs/curiosities.md` has the whole

story.



**The lexical search is now complete**: nothing in it is NOT YET.



`SearchDoViterbStep`, `RegisterNewPath`, `StoreFinalPaths`,

`CapHackDetermineContext`, `SearchFindBest`,

`SearchSegwordRememberNBest`, `SearchBestWords` and `SearchSendWords`

are all done, so a reading that has been grown knows where to go, when

it turns into text, and how it comes back out.  The beam is kept

deliberately varied: `SearchColumn::fClassCounts` and

`BiGrammar::fClassLimits` limit how many readings of each kind of word

a column may hold, and `RegisterNewPath` prefers to evict one that is

over its quota rather than simply the worst.



**`CharModifyProbs` is done** - what leans the classifier's answer with

where and how big the piece of writing was.  Two of its four

adjustments are nought in the shipped ROM; what is left is the capitals

hack and a Gaussian height model over `CharHeight`'s trained means and

spreads, worked out with no exponential because a score in this engine

is already a logarithm.  `rosCI`'s `fStrokeCountWeight`,

`fCapCaseWeight`, `fHeightSpread`, `fShapeWeight` and `fFragmentWeight`

are named for it.



### Done: the engine reads, and writing becomes text



`WordRecogAddStroke` (the driver: a stroke into the word, the word

spacing asked three ways, ligatures cut and each piece taken in by a

recursive call, the word closed when it is full) and the classify

passes (`RosettaClassifySetup`/`Analyze`/`Cleanup`/`RosettaClassify`,

`RosettaCheckWords`, the boxed-letter path `RosICBX`) are real, so

**nothing in the Rosetta engine is NOT YET** any more.  The feature

extraction once thought to be under it (`low_type`/`EXTR`, 556 KB) is

not Rosetta's: the call graph (`analysis/callgraph.py ...

RosettaClassify --through-done`) shows Rosetta reaches none of it - its

features are the four patternizer groups, which were done already.



`test_Reading` draws letters with a synthetic pen and checks the engine

reads eight words ("to", "tin" and "ton" come back first, the others

within the first two).  Four bugs in the reconstruction came out of

running it for real - two host-size mistakes (arrays of pointers and of

`ULong` sized at four bytes: `WordRecogAllocate`, the `LELTranCache`),

`RenderLine` missing the second coordinate's step back (the decompiler

had dropped it; it wrote one byte into the next heap block's header),

and `StrokeDestroy` not answering early for nil as the ROM does.  The

debugging aids that found them stay in `test_Reading.cpp`: a crash

handler that prints a symbolised stack (dbghelp) and a heap walker

(`ROSETTA_HEAPCHECK=1`).



The host OS registers `TRosRecognizer` now (`TNotebook::InitToolbox`,

`HostBootNewtWorld`), and `TEditView` answers `aeWord`: the command's

case in `RealDoCommand`, `HandleWordUnit`, `RemoveInk`, and

`HandleWord`'s remote-writing branch - on by default, which sends a

written word to the caret: into the caret's paragraph (through

`InsertItemsAtCaret`, a space in front unless it is a letter written

into the middle of a word), onto the end of the text under a caret on

the page itself, or a paragraph of its own.

### Listed as next at the time, done since

- A double tap on a read word opens the corrector with the engine's

  other readings and the spelling checker's, and picking one replaces

  the word (`src/host/demo/correct.ns`).  There is no training to do:

  the MP2x00's engine learns only through the dictionaries

  (`recognition/Learning.h`), which is done.

- **The shape domain**, so a drawn circle or line is cleaned up rather

  than read as a letter.

- The ink demo (`ink.ns`) now gets its writing *read*: to keep ink, a

  page has to ask for ink (`doInkWordRecognition`) or the writing has

  to be unreadable.



