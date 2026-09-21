# Where to pick up

A standing note for whoever (or whatever) comes back to this next. It
records the state of the tree at the last piece of work and the two or
three things that are obviously next, with enough of the groundwork
already done that they can be started without re-deriving it.

Keep it current: when a piece listed here is finished, take it out and
put the next one in.

## State at 2026-09-21 (commit `701e1ae`)

- `cmake --build build/host` clean, `ctest --test-dir build/host` 73/73.
- `analysis/coverage.py build/MP2x00US --check`: 8363 citations, 0 bad;
  4420 of 16671 functions (26.51%).
- `build/host/host/newton --rom build/MP2x00US/rom.bin --display 320x480
  --headless 20 --store <file>` boots.

The last run of work was the **view side of ink**, six commits from
`3793a4a` to `701e1ae`. What it added:

- `views/DrawShape.h`'s `MakePolygonForm` and the `'ink` branch of
  `DrawOneShape`, so an ink shape draws out of its `originalBounds` and
  into its `bounds`.
- `ink/InkShapes.h`: `GetPolyAsTStrokes`, `SplitInkAt`, `MergeInk`,
  `AddInk`, `NextInkIndex`, `GetInkAt`, and the converters between ink
  and stroke bundles.
- `recognition/StrokeBundle.h`: the `'strokeBundle` frame and the
  `'stroke` binaries in it.
- `views/EditView.h`: `TEditView::HandleInk`/`HandleInkWord`,
  `ViewExpectsNumbers`, and the `aeRawInk` case of `RealDoCommand`;
  `TDataView`'s two ink slots and `TContainerView::HandleInkWord`.
- `TNotebook::InitToolbox` now calls `InitializeInkCodecs` and
  `InitializeParagraphCompression`. Nothing outside the tests had been
  calling the first at all, so no ink could be packed on a running
  system.

Two mistakes of the reconstruction's own were found and fixed on the
way, both worth remembering because they are the kind that hide:
`DisposeTStrokes` was calling `IDispose` (free outright) where the ROM
calls `Dispose` (release; free when the last user lets go), and
`InkExpand` was adding its offset in tablet units where the ROM converts
to pixels, scales, and only then moves - the test had been compensating
by multiplying by eight.

## Next: `TInkWordGlyph` and `InkOpenFont`

This is what makes an ink word *visible inside a line of text*, which is
the last thing missing before a written word can appear in a paragraph.
Without it an ink word in a paragraph draws as nothing.

The shape of it (already read out of the ROM):

- `InkOpenFont` (0x000ada30) is a **font-engine entry point**, not a
  drawing one. `qd/Fonts.h` already lists "ink fonts (InkOpenFont)" as
  NOT YET; `OpenFont` should dispatch to it when the style's font is an
  ink word binary, and it fills a `FontEngineInfo` whose one glyph is
  the ink. It hangs the glyph object off `FontEngineInfo + 0x98` and the
  ink itself off `+ 0xc0`, sets the ascent/descent/width from the glyph,
  and then does the superscript/subscript and style-table work the rest
  of the engine expects.
- `TInkWordGlyph` is 100 bytes, vtable at ROM 0x1cbec:
  `ReadMetrics` (slot 0), `DrawAt` (slot 1), `SetFontParms` (slot 2).
  Fields: `+0x04` the ink (a `RefStruct`), `+0x08` the face (-1 unset),
  `+0x0c` the font size (-1 unset), `+0x10` the advance width, `+0x14`
  the ascent, `+0x18` the descent, `+0x1c` an `InkWordInfo` (0x3c
  bytes, so `fWidth` at 0x1c through `fScaledDescent` at 0x54), `+0x58`
  the pen to draw with, `+0x5c` the slop in 16.16, `+0x60` the scale in
  16.16.
- `ReadMetrics` (0x000dc394): at the ink's own size everything comes
  straight out of the `InkWordInfo`'s scaled fields; at another size the
  scale is `fontSize / info.fFontSize` and the ascent, descent and
  width are scaled by it, the pen coming from `GetStdInkWordPenWidth`
  for a non-negative face and from the word's own pen otherwise. The
  slop is `(info.fFontSize / 10) * scale` and the width has twice the
  slop in it. On a grey or colour port the pen is halved (minimum 1).
- `DrawAt` (0x000dc568): monochrome draws through `CSDraw` (at the ink's
  own size) or `CSDrawInRect` (scaled); grey builds regions with
  `CSMakePathsGroup`/`CSMakePathsGroupInRect` and `FramePaths`, which
  can be left NOT YET.

Write it as its own unit with an offscreen-port test (the pattern
`test_Ink`'s `TestInkDraw` uses), then hook `OpenFont`.

## Then: `DoInsertItems`, and `CheckAndDoSplitInk`

`TParagraphView::CheckAndDoSplitInk` (0x00176208) is the ink half of the
caret gesture - a caret drawn through an ink word splits it, which
`SplitInkAt` can now do. It is blocked only on `DoInsertItems`
(0x00170f7c), which is the paragraph's *general* insert path and would
also replace three places where this reconstruction does the equivalent
by hand (see the `(host: the ROM inserts through DoInsertItems ...)`
comments in `views/ParagraphView.cpp`).

`DoInsertItems` itself is small: it clones `Rstarterinsertspec`, fills
in `insertItems`, `addSpace`, `undoable`, `insertOffset`,
`replaceChars`, `moveCaret` and `defaultFontSpec`, and sends the view
command 0x4d with that frame as the frame parameter (the helper at
0x00170e90 is "post a command with a frame parameter to the view of this
context"). The work is in whatever answers 0x4d.

## After the ink: the recogniser

The owner's order was the view side first, then the recogniser. Nothing
generates `aeRawInk` or `aeInkWord` yet, because the domains above the
gesture domain are NOT YET - so all of the ink view side above is
reachable only from tests until that is done. In rough order:

- `low_level` and `GetTraceFromStrokes` - the CIC feature extractor.
  `recognition/Words.h`'s `FindBaseline` already has the ROM's fallback
  path and will start answering properly once these exist.
- `TWRecDomain`/`TWRecognizer`, `TRosRecognizer`, the Airus
  dictionaries.
- The shape domain.

## Odds and ends still open

- The ink half of `TParagraphView::CheckAndDoJoin` (merging two ink
  words when a caret joins them) - `MergeInk` is ready for it.
- `TLiveInker` (0x00113840 onwards): the ink that follows the pen while
  it is still down, through `InkerLine` with a pen of its own.
- The `aeInkWord` case of `TEditView::RealDoCommand` (0x000a51b0): it
  wants `SetRemoteForCorrector`, `CorrectorUp` and
  `ResetHilitesForNewWord` first.
- `TInkWordGlyph`'s grey/colour drawing path (the `paths` regions).

## Working notes that keep being needed

- Unaligned `ldr rN,[X+2]` rotates the aligned word right by 16 - read
  halfword loads out of the disassembly, never the decompiler. Halfword
  stores come out as two `strb`.
- `__rt_sdiv`/`__rt_udiv` take (divisor, dividend) and answer the
  quotient in r0.
- `TArray::IArray(elementSize, count)` sets `fCount = count`: the array
  *has* that many entries, so `TStroke::Make(n)` starts with n points at
  nought. Use `Make(0)` when the points are to be added, `Make(n)` when
  they are to be written in place.
- ROM arithmetic wraps; host `long` is 32-bit on Windows and traps under
  the sanitiser. Wrap explicitly through `ULong`.
- A `\n` inside a C string literal written through a Bash heredoc loses
  a backslash. Use the Write/Edit tools for those, or build the two
  characters as `chr(92) + 'n'` in a Python helper.
- `coverage.py --check` matches one citation per line; a second name on
  the same line (or a trailing comma) breaks the match.
