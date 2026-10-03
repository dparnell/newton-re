# Colour

What the MessagePad 2x00 ROM (2.1, 717006) knows about colour, how each
fact was established, and how far the host takes it (branch `colour`,
2026-10-03, at the owner's asking - "I heard a rumour that the Newton OS
had some support for colour").

## What the ROM has: a colour API, drawn in grays

NewtonOS 2.1 lets applications *say* colours everywhere, and draws every
one of them as one of the screen's grays.  Established from the symbol
table (`build/MP2x00US/symbols.txt`), the reconstructed functions (each
cites its ROM address) and the ROM's own objects (`romsrc/`):

| where | what | how it reaches the screen |
|---|---|---|
| NewtonScript | `PackRGB(r, g, b)` (`FPackRGB` 0x000e387c), `GetRed`/`GetGreen`/`GetBlue`, `GetTone` | a packed colour is an integer with bit 28 set (`PackRGBvalues` 0x002befdc: `0x10rrggbb`), so it cannot be taken for a standard pattern (1-5) |
| style frames | `fillPattern`/`penPattern` a packed colour; a `'ditherPattern` frame with `foreground`/`background` colours; a `'grayPattern` binary of 48-bit RGB pixels | `GetPattern` 0x00197d2c -> `GetStdGrayPattern` 0x00328e90, `MakeSimpleGrayPattern` 0x0032840c, `MakeGrayPattern` 0x00328fc0: a pattern of grays at the port's depth |
| text | a font spec's colour (`CreateTextStyleRecord`; `gTXHasColor` in the text engine) | the same `GetPattern`, the slab drawn in the gray (`MakeGrayText` 0x0035dcd0) |
| bitmaps | a pixel map with a `colorTable` (indexed colour: RGB entries); 16- and 32-bit direct colour (padded, unpadded, by component: `kPixMapNoPad`, `kPixMapByComponent`) | the colour table made a gray table (`RGBtoGray` per entry); direct colour converted to four-bit grays (`SetupConversion` 0x002ae540, `PixelConvert.cpp`) |
| pictures | `RGBFgCol`/`RGBBkCol`, colour tables in `PixMap` records, `PutColorTable` 0x00332208 when recording | grays at playback (`PicPlay.cpp`) |
| printing | | PostScript gets `setgray` from the share of a pattern's bits set (`TPSPrinter::SetGrayLevel` 0x00156bc8) |

Every path ends in `RGBtoGray(r, g, b, depthIn, depthOut)` 0x002bf044 -
luminance weighted 0.299/0.587/0.114, inverted (the Newton's grays run 0
white to all ones black) - at the depth `GetGrafInfo(kGrafInfoDepth)`
reports, with `GrayToRGB` 0x002bf0ac the other way.  Both, with the
pattern makers, are also exported as `TQDLibraryDriver` methods through
the patchable jump table (0x00195d40...): the place a system update for
other display hardware would have hooked.

The ROM's own data holds no colour: its `'colordata` slots (280 of them,
`romsrc/layout.tsv`) are icons in several *depths* - a one-bit and a
four-bit image each (`bitdepth: 1` 63 times, `bitdepth: 4` 69 times) -
from which the depth that suits the screen is taken; no `colortable`, no
deeper bitmap, no `PackRGB` call outside the built-in functions frame.

## What the ROM cannot do: draw on a screen deeper than four bits

The ROM's drawing goes no further than four bits a pixel, so colour
hardware would have needed new code as well as a new screen:

- `StretchBits` (how text slabs and every bitmap reach the screen) has row
  routines for destinations of one, two and four bits; onto eight it picks
  `NotDrawn` (`SetupStretchRatio` 0x002ae6c4, whose table of routines has
  no pair with an eight-bit destination).
- `DrTextChunk` 0x0035c788 ORs glyphs straight into one-, two- and
  four-bit maps only ("no loop for deeper maps"); `MakeGrayText` makes gray
  text for two- and four-bit ports only.
- The gray pattern makers leave an eight-bit pattern white (`GetStdGrayPattern`)
  or its rows as allocated (`MakeSimpleGrayPattern`, `MakeGrayPattern`).

So on the ROM's own terms an eight-bit screen shows lines and fills and
nothing else: no text, no icons (seen on the host with `--display
320x480x8` before the work below).

## The host: an eight-bit screen

DEVIATION (an extension; four-bit screens are untouched): the host fills
those gaps, so `--display WxHx8` draws everything.

- `StretchBits` onto eight bits (`Stretch.cpp`, "Eight-bit destinations"):
  a one-, two- or four-bit row is made by the ROM's own routine to four
  bits and widened, each gray v as v * 17 (black stays all ones); an
  eight-bit source is copied, repeated, or folded keeping the darkest pixel;
  `BlitModeOr8` is the gray "or" a byte at a time.  ctest `qd.Stretch8`
  holds the eight-bit result to the four-bit one widened, over 600 random
  scenes in all eight modes, stretched, shrunk and clipped (the four
  operations commute with repeating a nibble, so it is exact).
- `DrTextChunk`'s direct drawing and `MakeGrayText` at eight bits.
- The three gray pattern makers at eight bits (a byte a pixel).

With them a whole screen at eight bits (the Notepad with the Extras
drawer open) is the four-bit screen pixel for pixel, the clock aside.

## The host: a colour screen (`--colour`)

DEVIATION (an extension, off unless asked for): `newton --colour` makes
the screen eight bits whose values index a palette (`qd/Colour.h`), the
way Color QuickDraw drove an indexed screen on the Mac.  Nothing in the
ROM's drawing changes: `RGBtoGray` at eight bits answers the palette entry
nearest the colour instead of a gray, and `GrayToRGB` an entry's colour,
so every path in the table above - PackRGB patterns, dither patterns, font
colours, colour tables - draws in colour.  The palette keeps what the
ROM's drawing takes for granted of a gray screen:

- 0 is white and 255 black (a one-bit pixel widened to eight is 255);
- entry 17 * v is the four-bit gray v, so the ROM's four-bit icons and
  grays (widened v * 17 by `StretchBits`) look as they do on a MessagePad;
- entry 255 - i is entry i's complement, so xor with all ones - how
  QuickDraw inverts, hilites included - turns a colour into its opposite.

Besides the sixteen grays it holds a 6 x 6 x 6 colour cube and 24 more
grays.  The nearest entry to a colour is looked up in a 32 x 32 x 32 table
made on first use.  The host's windows show the palette
(`HostWindowSetPalette`: Windows' 8-bit DIB, X11's pixel table, and the
reMarkable's RGB565 panel - the Paper Pro's is a colour e-ink one);
`ScreenColour(h, v)` answers a pixel's colour (0xRRGGBB) and
`ScreenSnapshot("x.ppm")` writes the screen in colour
(`tools/imaging/pgm2png.py` reads PPM now).

ctest `host.NewtonColour` (`src/host/demo/colour.ns`): four PackRGB fills,
blue text, a red-on-yellow dither and a red rectangle inverted to cyan, each
checked on the screen.

### Colour bitmaps

On the colour screen `StretchBits` makes a colour source eight-bit palette
entries rather than four-bit grays (`SetupColourConversion`;
`PixelConvert.cpp`'s host converters): 16-bit and 32-bit direct colour
(padded, unpadded and by component), and one-, two- and four-bit pixels
with a colour table, the row then stretched as eight bits to eight (where
several source rows fold into one, the first is kept: "the darker" means
nothing among colours).  Gray tables made from colour tables - a bitmap's
`'colordata` entry's `colortable` (`TPixelObj::GetFramBitmap`), a
picture's pixel map (`GetPicColorTable`) - are made at eight bits there,
so they hold palette entries; an eight-bit bitmap with a colour table then
needs nothing more.  ctest `host.NewtonColour` draws a 32-bit bitmap (red
and blue halves, `MakeBitmap(w, h, {depth: 32})` filled by `StuffByte`), a
16-bit one (green) and a four-bit `'colordata` bitmap whose table makes
its pixels magenta.

### Period software in colour: NetHopper

NetHopper 3.2 (1997, `fixtures/packages/apps/NetHopper3.2`) uses `PackRGB`
and colour tables: its GIF decoder (native ARM code, run by `src/armcpu`)
makes each picture a `'colordata` bitmap carrying the GIF's own colour
table, which a MessagePad draws in grays.  On the colour screen the same
bitmap is drawn in colour, untouched: ctest `host.NewtonNetHopperColour`
browses `www/nethopper-colour.html`, whose picture `www/colour.gif` is four
bands (red, green, blue, yellow) in a black frame
(`tools/imaging/gifwrite.py colour.gif`), and finds each band's colour on
the screen.  Newt's Cape, its helpers and NetHopper's JPEG viewer use
`PackRGB` and colour tables too (`grep -a` of the packages); not yet tried.
