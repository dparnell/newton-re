# HiDPI: the Newton laid out at one size, drawn at a finer one (study)

**Status: a study, nothing built yet** (2026-10-02, branch `rmpp`).  The
owner, after using newton on the reMarkable Paper Pro: "Newton 2x was
usable, whereas the one at full resolution was too small. Maybe we could
look at scaling up the UI elements when it is running on a high dpi screen
at native resolution?"  So: the Newton lays its views out in a logical
810 x 1080 (as `--display 810x1080` does now, pixel-doubled to the panel),
but text, lines, shapes and ink are drawn at the panel's 1620 x 2160.  The
same would serve a desktop 4K window (`--scale-ui N` on any host).  It
would be a host DEVIATION and opt-in: with it off nothing changes, byte for
byte.

How each fact below was found: reading the reconstruction (file:line
references are to `src/` on the branch), and the ROM's fonts as
`romsrc/resources/sfnt/` holds them (`docs/qd/fonts-sfnt.md`).

## Two ways to do it

### A. A permanent 2x scaler on the screen port

The obvious way, and the ROM has the machinery: `TQDScaler`
(`qd/Transform.h`, `gScale`) takes a port's procs over and maps every
coordinate QuickDraw is given through a `TTransform` - rectangles, points,
polygons, regions, curves and paths mapped, a frame's pen scaled, the clip
mapped and text drawn at the scales times the transform's.  Make the screen
1620 x 2160, have the views think it is 810 x 1080, and keep a 2x scaler in
force on the screen port for ever.

What it would catch: everything drawn through the port's procs - the rect,
region, oval, round-rect, arc, polygon, line, curve, path, text and bits
verbs (`Draw.cpp:770`, `:891`, `:635`; `Shapes.cpp`; `Polygons.cpp:372`;
`TextObject.cpp:146`) and DrawPicture's playback, which goes through them.

What it would not, and would therefore draw at the wrong place or size:

| What | Where | Why it escapes |
|---|---|---|
| reading the screen into an offscreen buffer: the caret's, a hilite's, a drag's, the view effects' saved bits | `Bits.cpp:119` (CopyFromScreen), `Animate.cpp:94`, `RootView.cpp:1253`, `:1647`, `View.cpp:2391`, `ShapeVerbs.cpp:584` | `CopyBits` to anything but the current port goes straight to `StretchBits` (`Draw.cpp:647-660`): a logical rectangle used as physical pixels.  (Writing back does go through the procs - and so is stretched again.) |
| scrolling | `ScrollRect.cpp:35-60` (protoTXView's `TXScrollRect`) | moves and erases with `RgnBlt` on `portBits` directly |
| the stroke ink, the live ink | `Draw.cpp:948-1066` (`InkerLine`, writing `fScreenBits` with no port), `Inker.cpp:482-525`, `LiveInker.cpp:141-167`, `StrokeQueue.cpp:533-590` | pixels set one at a time, tiles ORed onto the display with `BlitToScreens` |
| the busy box | `BusyBox.cpp:27-53` | blitted to the display directly |
| the alerts | `alert/AlertDialog.cpp` (`:108-282`, `:94-97`, `:647-690`) | write bytes into the screen's memory below QuickDraw, by design (they must work when nothing else does) |
| the dirty rectangle | `Screen.cpp:341-450`; `RootView.cpp:652` | every rectangle handed to StopDrawing is pixels; the root's `fDirtyScreen` is logical |
| the root's regions | `Ports.cpp:965` (visRgn = the map's bounds), `RootView.cpp:394-402`, `:560`, `:664`; `BuildView.cpp:527-568` | the base visRgn, portRect and root bounds are pixels; the views set visRgn in logical coordinates (`ViewDraw.cpp:533-716` and a dozen views); the scaler maps the visRgn only with `fMapVis` and only during a proc call |
| patterns | `Ports.cpp:612-625` | tiled in device pixels: a scaled gray stays a one-pixel dither (fine, but not "2x") |
| packages | any native code that pokes a pixel map or reads the screen | they would see 1620 x 2160 bits behind an 810 x 1080 world |
| the pen | `HostTabletDriver.cpp:322-342`, `Inker.cpp:497-505`, `Stroke.cpp:67` | a tablet sample keeps **11 integer bits**: no coordinate over 2047.  The panel's 2160 rows overflow it - which is also a bug of today's "Newton 1:1" (`--display 1620x2160`): the pen wraps in the bottom 113 rows (seen on the tablet: the owner's tap on
Welcome's Continue, at row 2136, arrived at row 88 - 2136 - 2048).  A logical 810 x 1080 fits. |

Each of these is a change to reconstructed ROM code (dozens of sites), each a
DEVIATION, and the alerts and inker by design work below QuickDraw.  The
risk is a UI that is subtly wrong in a hundred places, and the fidelity
tests (pixel-identical screens) no longer apply.  **Not recommended.**

### B. A high-resolution shadow of the screen (recommended)

Leave the Newton's screen exactly as it is - 810 x 1080, authoritative,
every test and every package seeing what they see today - and keep beside
it a **shadow** at 1620 x 2160 that the host shows instead where it is
known to be right.

1. **Draw twice.**  The screen port's procs get a host wrapper (the same
   seam `TQDScaler` uses): each proc call draws as now into the screen, and
   then once more through a 2x `TTransform` into the shadow (a second
   `GrafPort` over the shadow map - the ROM's own scaler code doing the
   mapping, text at twice the size, pens scaled, clip mapped).  Only the
   procs are wrapped; nothing in the views, the inker or the alerts changes.
2. **Know when the shadow is right.**  The shadow is kept in tiles (say 16 x
   16 logical pixels).  When a proc call draws into the shadow, the tiles it
   touched record a checksum of the *logical* tile as it is after the call.
   When the window sends a tile to the panel it compares the logical tile's
   checksum now with the recorded one: equal, the shadow tile is shown;
   different - something wrote the screen without the procs (a scroll, an
   alert, a restore of saved bits, the live ink, a package) - the logical
   tile is shown doubled, as today.  So the display is never wrong, only
   sometimes not sharper.
3. **What comes back for free.**  A caret or hilite drawn and taken away
   restores the logical bits exactly, so the checksum matches again and the
   sharp tile reappears.  An alert, a scroll or the busy box shows doubled
   until the area is next drawn through the procs.
4. **Live ink.**  `InkerLine` draws on the screen directly; a host hook in
   the live-ink path can draw the same stroke into the shadow from the
   sample points, which are in eighths of a pixel (`Inker.cpp:497-505`) - at
   2x that is a quarter of a physical pixel, so the ink is drawn at the
   panel's full resolution while the pen is down.  When the stroke becomes
   a word or a shape its view redraws it through the procs anyway.
5. **The pen** stays logical (810 x 1080 fits the sample format); the
   window maps the panel's coordinates down by two, as it does now.

Cost: every proc call drawn twice (the second at four times the pixels),
checksums over the tiles a call touched, a comparison per tile sent.  The
Newton's drawing is small next to what the A53 can do (the ink path already
runs well under a millisecond per update), so expect a few percent; it is
measurable with `demo/drawbench.ns` either way.

### Text

`ScaledText` hands the font engine the size times two
(`Transform.cpp:569-609`); `SFNTOpenFont` takes the nearest strike and
stretches it if it is not exact (`Fonts.cpp:660-700`, `LocateEntry`).  The
ROM's bitmap fonts (`docs/qd/fonts-sfnt.md`):

| Font | Strikes (px) | At 2x |
|---|---|---|
| System (`espyFont`), plain and bold - **the UI's font** | 9 10 11 12 14 16 | 18-32 wanted; only 16 exists: 9 -> 18 would stretch the 16 by 1.125 - worse than doubling |
| Simple (Geneva) | 9 10 12 14 18 24 36 48 | 9->18, 12->24, 18->36, 24->48 are **designed strikes** |
| Fancy (New York) | 9 10 12 14 18 24 36 | 9->18, 12->24, 18->36 designed |
| Casual (handwriting) | 10 12 18 36 | 18->36 designed |

So Geneva and New York come out genuinely sharper; System, which is most of
the UI, needs strikes at 18, 20, 22, 24, 28 and 32.  Three ways, cheapest
first: (a) the font engine, in HiDPI only, doubles a strike with a
smoothing 2x filter (EPX/Scale2x on the one-bit glyph slab, in
`DrTextChunk`'s stretch) instead of picking the nearest - rounded diagonals,
the same shapes; (b) the same done once, offline, by a tool that writes
2x strikes for System into a host-only font supplement built from `romsrc/`
(BDF files, `tools/fonts/newtonsfnt.py` packs them) - identical result,
no per-draw cost, and the strikes can then be touched up by hand; (c)
real 18-32 px System strikes drawn by a person.  The advance widths must be
exactly twice the 1x ones or the layout (which is logical) and the shadow
disagree; (a) and (b) give that by construction.

### Bitmaps and icons

Bitmaps (`'bits`, `'pixels`, pictures' bitmaps) have no more pixels to
give: drawn through the procs at 2x they are stretched - the same as today's
doubling, or through the same smoothing filter.  Better icons would be new
artwork, out of scope.

### Packages that poke pixels

With B they see the logical screen exactly as today and keep working; what
they write directly shows doubled until redrawn through the procs.  With A
they would break.

## Beyond the Paper Pro

The shadow lives in the host window code, so `--scale-ui N` (N = 2, 3, 4;
the logical display is `--display`, the shadow N times it) works the same in
the Windows and X11 windows - a 4K desktop at 3x with sharp text - and in
the reMarkable window.  Off by default; with it off the window shows the
logical screen as now.

## Estimate and recommendation

**B, in stages:**

1. The shadow port, the proc wrapper and the tile checksums, in the host
   window layer, with the logical screen shown doubled where the shadow is
   stale; a test that a scene drawn both ways shows the same as today with
   the shadow disabled, and that the shadow is used where it should be -
   2-3 days.
2. Text: (b), the tool that writes 2x System strikes into a supplement
   font, loaded only in HiDPI - 1-2 days; (a) as the fallback for any other
   size - a day.
3. Live ink into the shadow at full resolution - 1 day.
4. `ScrollRect` and the saved-bits restores taught to move/restore the
   shadow too, so scrolling stays sharp - 1-2 days, optional.
5. The desktop windows' `--scale-ui` - a day.

About two weeks for all of it, the first useful result (sharp lines and
Geneva/New York text, everything else as today) after stage 1.  A, the
permanent scaler, is not recommended: it touches dozens of ROM-faithful
sites, breaks fidelity and packages, and still needs the font work.

Separately, a real bug this study found: `--display 1620x2160` overflows the
pen sample's 11 integer bits (rows 2048-2159), so "Newton 1:1" should not be
offered as it is; 2x (the default now) is unaffected.
