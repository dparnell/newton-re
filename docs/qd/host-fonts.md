# Host fonts

The reconstructed Newton can draw text in the fonts the host system has
- Georgia, Verdana, Segoe UI, Consolas and the rest on Windows - beside the
ROM's own five bitmap faces. They are offered in every Styles slip and
font picker the ROM has, as if a font package had been installed, and are
drawn by the host's own rasteriser at the exact size asked for.

**It is a DEVIATION** (the ROM has the 'sfnt' engine and the ink engine
and nothing else) and **it is off until asked for**, so that nothing drawn
differs from the ROM's unless the user wants it to - the screen-comparison
ctests stay pixel-exact.

The owner's decisions (2026-10-10): the host provides the rendering
engine; host fonts are optional; the aim is to give users more fonts (not
to change how the system's own text looks); anti-aliased text is wanted
eventually, and nothing here may stand in its way (below).

## Using it

- **The Host preferences panel**: "More fonts from the host" (shown only
  on a host that draws fonts) adds the families chosen - the host's usual
  ones until others are - to the font menus, and takes them out again.
  **"Choose fonts..."** opens a list of every family the host has (the
  ROM's protoTextList with several selections, scrolling): tick those the
  menus should offer and tap Done - the menus change at once, and the
  host's fonts are turned on if they were off and something is ticked;
  Usual goes back to the host's usual families. Both the setting and the
  families chosen are kept in the Host panel's entry in the System soup
  (`fontFamilies`, an array of names), the families given to the host
  first at boot.
- **`NEWTON_HOST_FONTS`** at start: `1` the host's usual families, `*`
  every family it has (several hundred on Windows - a long picker), or a
  comma-separated list of family names (`Georgia,Consolas`); `0` or unset,
  none (unless the panel's setting is kept on). A list given here is also
  what the panel's setting adds.
- From NewtonScript: `HostFontFamilies()` (every family the host has that
  a family symbol can name, nil when it draws none), `HostFontsAdded()`
  (the symbols of those in `vars.fonts`), `HostFontsChosen()` (the names
  the setting adds), `HostSetFontFamilies(names)` (nil: the usual ones),
  `HostSetSetting('hostFonts, true)`.

A host family is named in a font spec by its symbol, the family name and
`.host`: `{family: '|Georgia.host|, size: 12, face: 0}`. A note written in
one and opened where the host lacks it (or with host fonts off) is drawn
in the user's font, as any spec naming an unknown family is
(`CreateTextStyleRecord`).

The demo `src/host/demo/hostfonts.ns` (ctest `host.NewtonHostFonts`,
Windows) turns host fonts on, picks four families in the Styles slip and
checks the note is set and drawn in each; `hostfontchooser.ns` (ctest
`host.NewtonHostFontChooser`) ticks two families in the chooser with the
pen, taps Done and checks the menus and what is kept, then Usual.

## How it works

### Where it joins the ROM's font engine

Established by reading the reconstruction; each point cites where.

1. **A font spec becomes a `StyleRecord`** (`CreateTextStyleRecord`,
   `src/qd/Fonts.cpp`, ROM 0x002618b8). A packed integer indexes the
   ROM's four-family list (`Rromfontlist`, ten bits); anything else is a
   frame `{family: 'sym, size, face}` whose family symbol is looked up in
   `vars.fonts`. A family with no number - a package's font, a host font -
   is always a frame.
2. **`OpenFont` fills a `FontEngineInfo`** (ROM 0x002e229c). Before the
   open-font cache it hands a style that is not a font family at all - an
   ink word - to the ink engine through a hook (`gInkOpenFont`). **A family
   frame with a `hostFont` slot now goes to `HostOpenFont`
   (`src/qd/HostFontEngine.cpp`) in the same place**, outside the
   four-entry cache; when the host cannot open it the system font is used.
3. **The `FontEngineInfo` is the engine interface**: line metrics, a scale
   from what was opened to the size wanted, and the procedures `fMap`,
   `fGetGlyphInfo` (the advance, 16.16), `fGetGlyph` (height, width,
   bearings, one-bit rows), `fReopen`, `fClose`. QuickDraw's text code
   (`Text.cpp`'s measuring, `DrText.cpp`'s `DrTextChunk`) calls only these.
   The host engine fills it as `SFNTOpenFont` does. One field is added at
   the end, `fHostFace` (host only, not in the ROM's 0xc4 bytes).
4. **The font menus are NewtonScript and list `vars.fonts`**:
   `GetAllFontFamilies` (ROM 0x4165f5) answers every family whose `usable`
   slot is absent or true, `GetFontNameItems` names them by `name`,
   `MakeFontMenu` offers the family's `userSizes`. So adding a family frame
   to `vars.fonts` - which is what the `'font` part handler does for a
   package - is all the menus need.

### The host engine (`src/qd/HostFontEngine.cpp`)

- **The face**: the one the host has nearest the face wanted, in
  `ChooseStrike`'s order (bold italic, italic, bold, plain); the bold and
  italic the host's face does not have are synthesised on the slab, as for
  the ROM's fonts. A provider must never fake a face itself.
- **The size**: opened at the style's size times the scale, so text is
  never a strike stretched (`fScaling` 0); with unequal scales it is
  opened at the size and `StretchBits` scales it (2). Superscript and
  subscript as `SFNTOpenFont`: four fifths of the size, the baseline moved
  by three eighths of the ascent.
- **The slab's bounds**: `DrTextChunk` makes its one-bit slab from
  `fMaxBeforeBL`/`fMinAfterBL` (rows) and `fMinOriginSB`/`fMinAdvanceSB`
  (columns), and clips rows but **trusts the font for columns** - a glyph
  wider than they say would be written outside the slab. The engine takes
  them from the face's bounding box (no more than twice the size), allows
  a quarter of the size past a glyph's advance, and **cuts every glyph to
  them** before QuickDraw sees it.
- **Characters the face lacks** - Apple's private-use characters (the
  menus' check mark 0xFC0B, the Apple 0xF7FF, ...) above all - are taken
  from the system font's strike at the same size, opened through
  `SFNTOpenFont` with an info and cache copy of the engine's own.
- **Caches**: eight open faces (one being drawn is never evicted; a ninth
  needed at once is made outside the cache and given back on close), each
  with 256 glyphs by the character's low byte.

### The provider (`src/qd/HostFonts.h`)

A host that can draw its system's fonts registers a `THostFontProvider`:
its families, the faces each has (plain, bold, italic, bold italic), a
face opened at a size in pixels per em, a face's metrics (ascent, descent,
leading, widest advance, how far any glyph reaches above, below and left),
and a character's glyph. The engine is tested with a made-up provider
whose glyphs are boxes (`src/qd/tests/test_HostFonts.cpp`, ctest
`qd.HostFonts`), so it is tested on every host.

On the host side the platform's rasteriser sits behind a plain interface
with no Newton types (`src/host/HostFontRaster.h`; the platform's headers
and the DDK's do not mix), and `src/host/HostFontProvider.cpp` makes the
provider of it, registers it at boot and reads `NEWTON_HOST_FONTS`.

| Host | Rasteriser | State |
|---|---|---|
| Windows | GDI (`host/win32/HostFontRaster.cpp`): the TrueType/OpenType families `EnumFontFamiliesExW` lists (not raster fonts, not the `@` vertical ones), each family's real styles by weight and slant; `CreateFontW` at the em size with `NONANTIALIASED_QUALITY`; `GetGlyphOutlineW(GGO_BITMAP)` - hinted, one bit - by glyph index; `GetGlyphIndicesW` marks a missing character | done |
| Linux/X11 | fontconfig for the families, FreeType (`FT_LOAD_TARGET_MONO`) for the glyphs - the system's libraries, as X11 and ALSA are | not yet |
| reMarkable | FreeType over the tablet's `/usr/share/fonts` (no X11, no Qt to borrow) | not yet |
| macOS | Core Text | not yet (the port has not reached macOS) |

A host with no rasteriser (`HostFontRaster.cpp`'s `#else`) offers no host
fonts and the Host panel shows no setting.

## Anti-aliased text, later

Not built, but the design keeps the door open:

- A provider's glyphs carry their **depth**: 1 (a bit a pixel) or 8 (a
  byte of coverage a pixel, 0 to 255), and the engine asks for the depth
  it wants. GDI already answers both (`GGO_GRAY8_BITMAP`'s 65 levels made
  0-255). The engine asks for one bit today, and makes an eight-bit
  glyph one bit (coverage of half or more) if that is all a provider has.
- What anti-aliasing would still need is all on QuickDraw's side:
  `DrTextChunk` composes glyphs into a **one-bit slab** and works the
  synthesised faces (bold's smear, italic's shear, outline/shadow,
  `MakeGrayText`'s masking) on it before `StretchBits` puts it on the
  port. Gray glyphs would need a deeper slab (eight bits of coverage),
  those faces done on it, and a blend of the pen's pattern through the
  coverage onto the port's 1/2/4/8-bit pixels - most likely as a separate
  path taken only for a host face, with a glyph depth in the
  `FontEngineInfo` saying which. The ROM's fonts would keep their one-bit
  path untouched.
- Anti-aliased text would be a Host setting of its own, off by default,
  like the fonts themselves.

## Limits and open points

- **Pictures**: a picture recorded with text in a host font names it by
  `macFontID` 0 (`PicRecord.cpp`), so it plays back in the system font.
- **Printing**: a host family has no `psName`, so on a PostScript printer
  it is not swapped for a printer font; it has not been tried on any
  printer yet. On a raster printer's port it is opened at the printer's
  resolution, which should give real 300-dpi text - untested.
- **The system font is not replaced**: the ROM's views are laid out for
  espy's metrics; drawing the system's own text in a host face is not
  offered (it was not the aim, and fixed-height views would clip).
- **Family names** a symbol cannot carry (beyond printable ASCII, or with
  `|`) are not offered; nor are more than 63 characters of a name kept.
- **The chooser** lists names only (not each in its own face), and a name
  longer than the list is wide runs under its scroll arrows, as in any
  protoTextList.
- **Line spacing**: a host face's ascent and descent are the host's
  (Windows' `tmAscent` covers accents), so lines in a host font are
  somewhat taller than in the ROM's fonts at the same size.

## Background: the other way considered

Before the decision, the investigation also looked at making Newton fonts
out of host fonts: rasterising a host font at a few sizes into the ROM's
own `bloc`/`bdat` sfnt (`tools/fonts/newtonsfnt.py` packs that form from
BDF) and installing it as a font package. It needs no engine change and
would work on `--rom` and a real MessagePad, but only at the sizes made
(others stretched as crudely as the ROM's), and the repo has no package
writer yet. It remains a possible tool for taking a host face to a real
Newton; for the host itself the engine above is better.
