# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-23 (commit `c3cb664`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 75/75.
- `analysis/coverage.py build/MP2x00US --check`: 8513 citations, 0 bad;
  4560 of 16671 functions (27.35%).
- `build/host/host/newton --rom build/MP2x00US/rom.bin --display 320x480
  --headless 20 --store <file>` boots, and writing on the Notepad stays
  on the page.

Writing now travels the whole way from the tablet to a paragraph; the
route across the five areas is `docs/ink/README.md`'s "From the pen to
ink on the page", and `src/host/demo/ink.ns` photographs it with the pen
down and again after the recogniser has finished.

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
- the writer's recognition preferences (`ReadCursiveOptions`).

## Next: what a paragraph does with things dropped into it

`TParagraphView::HandleInsertItems` (0x001700a0) is the one big piece
left on the writing path, and three things are waiting on it:

- the **caret side of `aeInkWord`** (the tail of 0x000a51b0).  The ROM
  looks at the view the caret is in and, when it belongs to this page,
  puts the word into the paragraph the caret is in rather than starting
  a new one.  The end of that path is short and already read:
  `StrokeBundleToInkWord`, `AdjustInkWordXHeight` over
  `ViewExpectsNumbers`, a frame whose `insertItems` slot is the word,
  and `InsertItemsAtCaret`.
- **`CheckAndDoSplitInk`** (0x00176208), which cuts an ink word in two
  at a caret - `SplitInkAt` is ready for it.
- dropped items generally.

The groundwork already read out of the ROM:

- `InsertItemsAtCaret` (0x00171168) clones `protoCommand`, sets `id` to
  0x4d and `receiver` to the caret view's context, puts the spec in
  `frameParameter`, and sends it with the view's `DoCommand` (vtable
  +0x10); when nobody takes it, the root view's `SysBeep` script runs.
  `DoInsertItems` (0x00170f7c) is the same but for a named view, and it
  builds the spec first: a clone of `Rstarterinsertspec` with
  `insertItems`, `addSpace`, `undoable`, `insertOffset`,
  `replaceChars`, `moveCaret` and `defaultFontSpec`.  (Careful: the
  ROM's fifth argument goes to `insertOffset` and the sixth to
  `replaceChars`.)
- `HandleInsertItems` reads those slots, takes the hilites away unless
  `TRootView::GetPreserveHilites` says not to, builds a new text binary
  and styles array by appending each item in turn, and hands them to
  `HandleReplaceText`.  Its item kinds are: a string; an ink word; a
  frame with a `text` slot (with `styles`); a frame with an `ink`,
  `strokes` or `points` slot (compressed with `CompressStrokes` and put
  in as one 0xF701 character); and anything else, which is skipped.
- Five small helpers go with it, all in the same ROM file:
  - 0x00170064 `ItemCount(items)`: `IsArray(items) ? Length(items) : 1`.
  - 0x00170030 `ItemAt(items, i)`: `IsArray(items) ? items[i] : items`.
  - 0x0016f954 grows the text binary, rounding up to a multiple of forty
    characters.
  - 0x0016f9b0 grows the styles array, rounding up to a multiple of ten.
  - 0x0016fba8 merges a run with the last one when `EqualStyles`
    (0x0016fa08) says the styles match.
- **The hard part is 0x0016fd7c**, the appender: eleven arguments, and
  the decompiler's output for it is unusable (it sets the stack
  arguments by hand).  It has to be read out of the assembly.  What it
  does is work out the delimiter between the last item and this one
  (`GetAppendDelimiter`, 0x000edc24), append that and then the item's
  text to the binary, and append the item's styles to the array,
  merging with the last run when they match.

## Also still open

- The **dictionaries** (`LookupWord` 0x0013f4f4, `ExpandWord`
  0x001aa930, `BuildChains` 0x0013d808, `LookupWordOrVariant` 0x0008f098
  and everything under them).  This is the other large open area.  With
  none of them, a word list comes out in the order a machine with an
  empty dictionary would put it, an area has no chains, and nothing is
  ever "known".
- An engine that reads something: the ROM's own is the CIC handwriting
  library, and `TInkOnlyRecognizer` (`recognition/InkRecognizer.h`) is
  the socket it - or a modern one - plugs into.  Nothing above the
  socket would change.
- `SetUpRosetta`, `SetUpParaGraph` and `ReadDictPrefs`, which
  `ReadCursiveOptions` would call: all three belong to the engines.
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
