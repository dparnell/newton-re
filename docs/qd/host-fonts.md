# Host fonts: what it would take

*An investigation (2026-10-10, branch `host-fonts`), not yet a design that
has been agreed. Nothing here is built.*

The question: what would the reconstructed Newton need in order to draw
text in the fonts the host system provides (Windows' Segoe UI or Georgia,
a Linux machine's DejaVu, whatever is on the reMarkable) as well as in the
ROM's five bitmap faces?

The short answer: **the font engine already has the seam, and the ROM's
font menus already list whatever is in `vars.fonts`.** What is missing is a
rasteriser on the host and a few hundred lines to join it to the engine.
The open questions are which rasteriser, and how far host fonts should go
(more choices in the Styles menu, or the system's own look replaced).

## How text reaches the screen today

Established by reading the reconstruction; each point cites where.

1. **A font spec becomes a `StyleRecord`** (`CreateTextStyleRecord`,
   `src/qd/Fonts.cpp`, ROM 0x002618b8). A packed integer indexes the
   ROM's four-family list (`Rromfontlist`, ten bits); anything else is a
   frame `{family: 'sym, size, face}` whose family symbol is looked up in
   `vars.fonts`. A family with no number - a package's font, say - is
   always a frame. **An unknown family symbol falls back to the user's
   font, then the system font**, so a note written in a host font that a
   later machine lacks still opens and draws, in espy.
2. **`OpenFont` fills a `FontEngineInfo`** (ROM 0x002e229c,
   `Fonts.cpp` `OpenFont`). Before the open-font cache it already hands a
   style that is not a font family at all - an ink word - to another
   engine through a registered hook (`gInkOpenFont`, `ink/InkFont.cpp`,
   marked DEVIATION). Everything else goes through the four-entry cache to
   `SFNTOpenFont`; a family that cannot be opened is retried as the system
   font.
3. **The `FontEngineInfo` is already an engine interface**: line metrics
   (`fAscent`, `fDescent`, `fLeading`, `fWidMax`, `fMaxBeforeBL`, ...), a
   scale from the strike to the size wanted, and five procedures - `fMap`
   (character to glyph), `fGetGlyphInfo` (the advance, 16.16),
   `fGetGlyph` (height, width, bearings and a pointer to byte-aligned
   one-bit rows), `fReopen`, `fClose`. QuickDraw's text code never looks
   at the sfnt itself; it calls these.
4. **Measuring** (`Text.cpp`, `MeasureTextOnce`) calls `fGetGlyphInfo`
   for each character; **drawing** (`DrText.cpp`, `DrTextChunk`) calls
   `fGetGlyph` for each and ORs the glyph's **one-bit** rows into a
   one-bit slab, works the synthesised faces (bold smear, italic shear,
   underline, outline/shadow, gray) on the slab, and `StretchBits` it
   onto the port (scaled when the strike is not the size wanted). A slab
   over 8000 bytes is split.
5. **The font menus are NewtonScript and enumerate `vars.fonts`**:
   `GetAllFontFamilies` (ROM 0x4165f5) answers every family whose
   `usable` slot is absent or true, `GetFontNameItems` names them by their
   `name` slot, and `MakeFontMenu` offers the family's `userSizes`. The
   `'font` part handler (`packages/FontPartHandler.cpp`) is how a package
   adds a family there today.
6. **What does not go through the engine**: the alert manager reads the
   system font's sfnt itself (`alert/AlertDialog.cpp`,
   `TAlertGlyph::InitGlyph`), and a PostScript printer's port swaps a
   family for its `vars.psFonts` counterpart by `psName` inside `OpenFont`.

So a host font needs to be (a) a family frame in `vars.fonts`, for the
menus and for specs to name it, and (b) something `OpenFont` can turn into
a `FontEngineInfo`. There are two ways to do (b).

## Two ways in

### A. Make Newton fonts out of host fonts (no engine change)

Rasterise a host font at a chosen set of sizes into one-bit strikes and
pack them into the Newton's own `bloc`/`bdat` sfnt - the format the ROM's
fonts are in, which `tools/fonts/newtonsfnt.py` already packs from BDF
byte for byte (`docs/qd/fonts-sfnt.md`). The result is an ordinary family
frame, drawn by the reconstructed engine exactly as it draws espy.

- **Offline**, as a tool: host font → BDF strikes → sfnt → a package with
  a `'font` part, installed with `--package`. Works on `--rom` and could
  even go to a real MessagePad. Needs a rasteriser in Python (Pillow's
  `ImageFont` renders one-bit with `fontmode = "1"`; or `freetype-py`)
  and a package *writer*, which the repo does not have yet (it only reads
  packages).
- **At boot**, in the host: the same strikes made in memory and the family
  frames put into `vars.fonts`. Needs the host rasteriser (below) but
  still no engine change.

Limits: only the sizes made have strikes - any other size is the nearest
strike scaled by `StretchBits`, which looks as crude as the ROM's own
scaled text; the glyph repertoire is fixed when the strikes are made
(the cmap can cover far more than Mac Roman - glyph ids are 16-bit and
index format 3 takes any range); each strike is memory (a few KB to a few
tens of KB a size at Newton sizes).

### B. A host font engine behind `FontEngineInfo` (recommended for the host)

A second engine beside `SFNTOpenFont`, chosen in `OpenFont` the way the
ink engine is: a family frame carrying a `hostFont` slot (the host's name
for the face) goes to a registered `gHostOpenFont` instead of the cache
and `SFNTOpenFont`. It fills the info with the host face's line metrics
at the exact size wanted (`fScaling` 0: no scaling, ever), and its
`fMap`/`fGetGlyphInfo`/`fGetGlyph` answer from the host rasteriser,
keeping the last glyph's bits alive until the next call as the sfnt
engine's pointer into `bdat` is.

- Every size is drawn at its own size, hinted, not a strike stretched.
- On a printer's port the face is opened at the printer's resolution, so
  a raster printer (`print/`, the dot-printer and PCL/IPP drivers) gets
  real 300-dpi text rather than a screen strike scaled up. (The
  PostScript substitution by `psName` stays as it is; a host family can
  carry a `psName` too.)
- The synthesised faces keep working unchanged, since they are worked on
  the slab; a host family that has real bold/italic faces opens those and
  takes the bits off the face to synthesise, as `ChooseStrike` does.
- It is a DEVIATION (no such engine in the ROM), behind an explicit seam,
  as the ink engine and `TInkCodec`/`TWRecognizer` are.

Option A's offline tool and option B are not exclusive: A is the way to
give a *real* Newton or a `--rom` boot a host face; B is the way to make
the host's text look right.

## What B needs, piece by piece

1. **The seam in QuickDraw** (`qd/Fonts.h`, `Fonts.cpp`): a
   `FontHostOpenProc gHostOpenFont`, called from `OpenFont` for a family
   with a `hostFont` slot (before the cache, as for ink, so the four-entry
   cache's offset-keeping `SFNTReopenFont`/`SFNTCloseFont` never sees a
   host font). Falls through to the system font when it answers
   `kNoFont`. A host test with a fake engine (boxes for glyphs) proves
   measuring, drawing, the synthesised faces and the fallback.
2. **A host font interface** (`hal/host/HostFonts.h`, say): list the
   host's families and faces; open a face at a pixel size (x and y);
   answer line metrics (ascent, descent, leading, widest, max above and
   below the baseline); for a Unicode character, whether the face has it,
   its advance in 16.16, and its one-bit bitmap with bearings. A small
   per-face glyph cache (the engine is asked for the same glyphs on every
   redraw).
3. **The Newton's own characters**: Newton text is Unicode, so mapping is
   natural, but the ROM uses Apple's private-use characters (0xF714-6,
   0xF7FF, 0xFC00-0xFC0F - the menus' check mark `ﰋ` among them) that
   no host font has. The host engine should take a glyph the face lacks
   from the system font's strike (the bitmap and bearings are all
   `DrTextChunk` needs; the bitmap is placed by bearing y against
   `fMaxBeforeBL`, so a taller fallback glyph must not stand above it).
4. **Families in `vars.fonts`**: at boot, from the Host preferences page
   (as the owner wants host options: `docs/codebase-map.md`, Host panel),
   each chosen host face added as `{name, screenSym, hostFont, userSizes,
   usable: true}` - it then appears in every Styles slip and font picker
   with no ROM script changed. A family symbol must be stable across
   machines (made from the host name) so that saved specs find it again.
5. **Optionally, the system font itself**: a Host option to draw `'espy`
   (or Simple/Fancy) with a host face. This is the change people will
   *see* most, and the risky one: ROM views are laid out for espy's
   metrics (`GetStyleFontInfo` gives espy 12 as ascent 12, descent 4,
   widest 15), so a face with taller ascenders clips in fixed-height
   fields and wraps differently in narrow ones. It should map size for
   size by matching the cap height or x-height, not the em, and be off by
   default.
6. **Tests and determinism**: host fonts must be off by default, or every
   screen-comparison ctest (`host.NewtonNoROMSameScreen`, the
   walkthroughs) depends on what is installed. The seam is tested with
   the fake engine on every host; a real-rasteriser test runs only where
   its font is found (Windows always has Arial/Segoe UI; on Linux
   DejaVu if present) and checks properties (glyphs non-empty, advances
   monotonic in size), not pixels.

### Which rasteriser

| | Windows | Linux/X11 | reMarkable | macOS | One-bit, hinted | New dependency |
|---|---|---|---|---|---|---|
| **FreeType** (`FT_LOAD_TARGET_MONO`) | yes | yes | yes | yes | yes - mono hinting is what it is for | yes (FreeType licence, BSD-style with credit), built from vendored source by the zig toolchain |
| **Native**: GDI `GetGlyphOutlineW(GGO_BITMAP)` / CoreText | yes (gdi32 already linked) | no good native API (core X fonts are legacy; Xft *is* FreeType) | no (no X11, no Qt to borrow) | yes | yes on Windows | none |
| **stb_truetype** | yes | yes | yes | yes | **no hinting** - poor at 9-14 px in one bit; fine at printer resolution | one public-domain header |

The repo has no third-party library today (the zig toolchain builds
everything from source), so this is the main decision. FreeType is the
only choice that covers the reMarkable, which is a stated goal; GDI alone
would be the quickest proof on the owner's Windows machine. Font
*discovery* is per-host whatever the rasteriser: the Windows fonts folder
and registry (or `EnumFontFamiliesEx`), fontconfig or `/usr/share/fonts`
on Linux, `/usr/share/fonts` on the reMarkable, CoreText on macOS.

### Later: gray (anti-aliased) text

Every rasteriser can give 8-bit coverage, and the screen has 16 grays,
but `DrTextChunk`'s slab is one bit and so is everything worked on it
(bold's smear, outline, `MakeGrayText`'s masking, `StretchBits` from a
one-bit source). Anti-aliased host text would need a deeper slab and a
blend into the port - a larger departure from the ROM's drawing, for a
second stage if one-bit host text proves too jagged.

## Suggested order

1. The seam with a fake engine and its test.
2. A first backend (GDI on Windows, or FreeType straight away) behind
   `HostFonts.h`, one face registered by an environment variable, and a
   demo (`src/host/demo/`) that sets a paragraph in it.
3. Missing glyphs from the system font.
4. The Host page's list of host faces into `vars.fonts`.
5. Printing at device resolution.
6. Optional: the system-font substitution; the offline font-package tool
   (option A); gray text.

## Questions for the owner

- What is the aim: more typefaces to choose in the Styles menu, the
  system's own text in a host face, better printed text - or all three?
- Is a vendored FreeType acceptable as the project's first third-party
  library (needed for the reMarkable), or should Windows go native first?
- One-bit text only, in keeping with the ROM, or is gray text wanted?
