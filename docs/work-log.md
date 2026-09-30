# Work log

The record of finished work, newest first, moved here out of
`docs/next-steps.md` so that that file can stay a short account of where
things stand and what is next.  Each entry is as it was written when the
work was finished: a "Next" or NOT YET inside an older entry may since
have been done (a newer entry, or the subsystem's own page under
`docs/`, says so).  The subsystem pages are the reference for how things
work; this log is how and in what order they came to be, with the host
bugs and ROM bugs found on the way.

## 2026-10-01: the Setup walk's lost tap

`common.ns`'s walkSetup sometimes stopped at Welcome ("setup name page:
waited in vain").  The tap past Welcome starts the pen calibration, and
the host's automatic calibration taps were held a fixed 500 ms; with
the inker late, `GetRawPoint` saw fewer than the twenty pen-down polls
it wants, and the calibration waited out its time limit.  The taps now
lift once the inker has taken 24 samples (6638ed6f, 4dc1c9db); a 50 ms
hold, which stalled every time before, gets through all eleven Setup
pages.  Seen in passing, not chased: `intl.Dates` failed once in a run
that crossed midnight, and `host.NewtonBeamIrDA` passed 5 of 6 under
stress ("the note sent: waited in vain").

## 2026-09-30: Newt's Cape browses, with images

ctest `host.NewtonNewtsCape` (aa6c590e, c9667bc7): Newt's Cape 2.0 over
the built-in NIE opens a page, checks its heading, text, link and the
picture's ALT text, then runs View > Load with Images…, ticks All, picks
Load and checks the GIF drawn from the image cache.  The GIF had shown
only as ALT text because Newt's Cape loads images only when asked, and,
once asked, its native GIF decoder stopped on two glue calls armcpu did
not answer (`EQ`, ROM 0x0031c820, and `BlockMove`); with both answered
it decodes in 157k instructions.  Two of Newt's Cape's own quirks are
kept and written down in `docs/comms/README.md` ("The web browsers").
`stress.py` counts `--port 0`/`--tcp-echo 0` as free ports.  Seen but not
chased: `common.ns`'s walkSetup now and then loses the tap past Welcome
("setup name page: waited in vain").

## 2026-09-30: the user-reachable NOT YET sweep finished

Eleven items done: clicks and sounds, keyboard editing, selections,
the caret, dates as text, the boot splash, picker keys, ink in pickers
and printing ink; the busy box and live ink came with c7129677, and
`TCursor::EntryChanged` turned out to be reconstructed already (only a
stale comment said otherwise, as did the Cursors notes on words, text
and tagSpec queries - bdb25ce7, f73c48bc).

## 2026-09-30: the host runs on Linux as well as Windows

The whole reconstruction now builds and runs on Linux with the system
compiler, and `newton` shows the booted machine in a window there as it
does on Windows.  `ctest` passes all 233.  Windows was left as it was: every
change either only applies where `long` is wider than the ARM's word, or
is the same value spelt so that it cannot be read two ways.

- The window and the sound are one implementation per host behind the two
  headers that were in `host/win32/` and are now `host/HostWindow.h` and
  `host/HostAudio.h`: `host/x11/HostWindow.cpp` is X11 (a Wayland session
  reaches it through XWayland) and `host/alsa/HostAudio.cpp` is ALSA, and
  `src/host/CMakeLists.txt` picks them.  A key still reaches
  `HostKeyboard.cpp` as its Windows virtual key code whichever host it
  came from, so there is one map from a key to the Newton's (ADB) code and
  not two.  A host with neither runs headless and silent, as before.
  Tapping Continue on the Welcome screen through the window goes through
  the tablet to the view system and brings up "Enter your Name", and a key
  types into it.  NOT YET: a package dropped onto the window (XDND).

- What a 64-bit `long` changes, and the six places that relied on its
  being the ARM's 32-bit word, are `docs/host-lp64.md`: the sound's
  decibel constants, the ROM's `rand` (which is why Mahjongg dealt itself
  a tile index of -6), `TNSDebugAPI`'s frame base (an arithmetic shift in
  the ROM, 0x002d2688), the shape solver's coefficient rows, the sides
  block's size and a dictionary chain's "nowhere".  `Fixed` and `Fract`
  are now pinned to 32 bits on such a host (`sync_ddk_headers.py`), which
  is what makes qd's `ToFixed` wrap as the ARM's does.

- Three that are Linux's rather than the word's: includes spelt in the
  wrong case; a static `std::condition_variable` destroyed with task
  threads still parked on it, which hung every program that booted the OS
  *after* its checks had passed (`docs/host-runtime.md`); and the window
  reading the display's pixels through a pointer `ScreenSetup` had freed
  and allocated again - which Windows' allocator hid by handing the same
  block back.

- `compression.LZ` was failing on Linux, and both halves of why are now
  settled.  Its data came from the C library's `rand()` - a different
  sequence on each host, so it was not the same test on two machines - and
  it asked the coder for an exact length that the coder does not give for
  a last block that is not a whole 0x400 bytes.  A stored one of 1021 to
  1023 bytes comes back padded to 0x400 (`DecompressBlock` 0x000ffa60,
  `CMP r0,#0x400; SUBLS r3,r0,#4; MOVHI r3,#0x400`), and a coded one can
  come back a few bytes long because the decoder reads codewords until the
  input runs out and the padding bits of the last byte can make one more.
  Both are the ROM's, and the new ctest `compression.LZOracle` is what says
  so: it runs the ROM's own `TLZCompressor` and `TLZDecompressor` on the
  ARM interpreter over the ROM image (the jump table aliased at
  0x01A00000, host traps for the two allocators the compressor's `New`
  calls) and holds the reconstruction to the ROM's answer - the same
  compressed bytes, the same restored bytes and the same length - at every
  size from 0 to 0x900 and around the stored path's boundary.  All 2305
  agree, the 76 that do not round-trip exactly included; "fixing" the
  stored-block length makes it fail, which is what it is for.  The round
  trip now asks for the bytes back always and the length back whenever the
  source is a whole number of blocks, which is all the store compander
  ever hands it.  The full suite is 233 of 233 (five of those want
  `build/<ROM>/symbols.json`, so run `dump_symbols.py` on a fresh
  checkout).

- `tools/ntk/inspector.py` no longer skips the download half of
  `host.NewtonNTK`: the expression that tells the script to stop listening
  may have the link go down before its own result comes back, which it
  did on Linux every time and on Windows never.

## 2026-09-30: the ROM-free track, step 2 - the object area as editable files

- Functions as source (62cbe15): the builder hands every function to one
  run of `newtonscript --compile-records` - the host's own compiler, with
  no ROM image - and the object area still comes out identical; 5480
  top-level functions are `functions/<addr>.ns`; 3784 `alias` lines in
  the manifest say which named object a shared literal is; symbols
  matched whatever their case.  362 functions nested inside others stay
  bytecode.
- Bitmaps as grayscale PNGs at their own depth (525; the 16-byte header
  kept beside each in hex, its pad word not always zero), read back by
  `tools/imaging/png.py`, a pure-Python reader of whatever an editor
  writes (287ce02).  Simple 8-bit sounds as WAV, byte for byte; the 12 IMA
  ADPCM sounds and the 19 ring-tone tables stay opaque (0177c0b).  The 7
  QuickDraw pictures as .pict, the 13 fonts as .ttf (five bitmap fonts,
  eight metric-only) (a07f134).  The tree: 19 MB in 6,500 files
  (6d2b19a); `host.ROMSourceRoundTrip` byte-identical at every step,
  about 25 s.

## 2026-09-30: the ROM-free track, step 2 stage 1 - the object area as source

- How frame maps are shared (`nsfunctions.py --census`): 8336 maps for
  12838 frames, 282 of them supermaps; only 359 shared by more than one
  frame (one `left, top, right, bottom` map carries 1096); 5783 belong to
  NTK code blocks, one per function though their tags are the same; 318
  sit in ordinary slots.  So a frame's map cannot be worked out from its
  slots: maps are named objects (`maps.ns`), each frame's map a fact in
  the manifest.
- `analysis/romsrc.py` (b9cc001): `extract` writes 3038 definitions
  (`objects/NNN.ns`, a subset of NewtonScript literals plus `string`,
  `real`, `binary`, `array`, `map`; shared objects named after the ROM's
  R constants), `maps.ns` and `layout.tsv` (each object's address, path,
  header flags and map), binaries other than strings and reals as `.bin`
  files; `build` lays the area out again, `--check` compares: all 46538
  objects byte for byte.  ctest `host.ROMSourceRoundTrip` (about 5 s),
  build target `romsrc`.  The tree (12 MB, 12,000 files) is generated,
  not committed, until the host builds from it (`docs/rom-free/README.md`,
  "Where the tree lives").

## 2026-09-30: the ROM-free track, step 1 - a round-tripping NewtonScript decompiler (100%)

- `analysis/nsdecompile.py build/MP2x00US --roundtrip --newtonscript
  <newtonscript>` decompiles every one of the ROM's NewtonScript
  functions to source and compiles it back with the reconstruction's own
  compiler, comparing instructions, literals and argFrame byte for byte
  (`docs/frames/decompiler.md`; ctest `host.NSDecompileRoundTrip` over a
  sample).  94.4% at first (b00be8e), 99.7% - 5491 of 5507 - after ten
  rounds (6a1ce06, a2556eb, 952624d, 1749b85, 9011b47, 5061331, b7f32c4,
  12c05b6).
- What it took: locals declared where the ROM numbered them; loops found
  at their tops, quoted paths; in a function of more than 20 variables
  the compiler's variable frame is sorted (`AddSlot` sorts a map past 20
  tags), so locals are named to hash into the ROM's order (the sum of the
  upper-cased characters x 0x9E3779B9), loops' hidden locals included;
  function literals pushed without `set-lex-scope` written as
  `kFunction_` constants; `exists` paths; repeated literals told apart by
  slot; an empty else told from an inner construct ending at the same
  place; unquotable literals built by constants (`kBinaryFromHex`,
  constructors for frames and arrays holding binaries or functions).
- Compiler: the NTK's constant handling behind the host flag
  `gCompilerNTKConstants` - a magic-pointer constant gets one literal per
  name and a reference to a constant is not a receiver reference.
- To 100%, 5507 of 5507 (8b1d0e6, 65648d9, ae17ab4, 7db2775): string
  subclasses and halfword shapes built in the host's byte order (the
  importer swaps them); the "immediates" were native-function frames the
  NTK put in literals (special immediate 0x132, now a constant); a
  foreach's variable closed over by an inner function; the build-time
  closure 0x5acf1d rebuilt by the function that made it
  (`kClosureMaker_...`).  The ctest's 600-function sample requires 100%.
- Step 2 planned in `docs/rom-free/README.md` (e688ad4):
  `nsfunctions.py --census` counts the object area (46538 objects, 2.9
  MB; 10679 shared; 1012 reachable only from the magic pointers or C);
  bitmaps to PNG, sounds to WAV, pictures as PICT, fonts as .sfnt, the
  object graph as NewtonScript under `romsrc/objects/` with a
  `layout.tsv` manifest, and `rombuild.py` proved by a byte-identical
  rebuild of the area.

## 2026-09-30: the modem

- `comms/ModemTool.h` (dd3acd65): `TClassOneModem` (serv 'mods') and
  `TModemService`, whole from 0x5cce0-0x651cc - the AT commands and their
  answers, identifying the modem, connect, listen, answer, hang-up, TAPI,
  and the Class 1/2 fax paths (untested so far); the modem options
  (`ModemOptions.h`, `ModemToolOptions.cpp`); the 214 strings and the
  response table generated by `analysis/modemstrings.py`.  There is no
  CCL script engine in the ROM: `startCCL`/`stopCCL` only block.
- `tools/modem/fakemodem.py`: a Hayes modem at the far end of the host
  serial port with TCP for its line (ATD through a phone book or
  HOST:PORT, ATA with ringing, +++ with the guard time).  ctests
  `host.NewtonModemDial` (the ROM's whole sequence - AT, &FE0V1, I4, the
  config string, ATDT, CONNECT, an echo, the ROM's own hang-up) and
  `host.NewtonModemAnswer` (RING, ATA, CONNECT) pass against it;
  `comms.ModemTool` checks the command builder and parser.
- ROM bugs kept, among them: `UiToA` takes each digit from the low byte
  (300 becomes "304"); command 0x5c's "+FEA=" is thrown away; a result
  code compared with an error number; cellular always marked supported;
  the 'mpro' default reads past the stack; `C2ParsePhoneNum`'s halfword
  count wraps.  `HostOptionLayouts`' '*' copies an option's trailing
  bytes as they are ('rout' numbers, 'mpro' profiles).

## 2026-09-30: a fax sent from the Newton

- `TFaxToolInterface`, `TFaxDriver`/`TFaxDriverData` and
  `InitPrintDrivers` (9c0247a9); `TPrintView` (class 94) and the print
  job's C side, `TNotebook::InitToolbox` calling `InitPrintDrivers`
  (53d281b2); `docs/print/README.md` (20c5e858).
- A note sent with Send('fax) dials `fakemodem.py --fax-answer`, trains,
  sends the ROM's cover page and the note (1728 x 2148, fine
  resolution) each answered MCF, and hangs up; the cover page has its
  header, "Page 1 of 2", "From", the Newton logo; page 2 reads "hello
  from the Newton's fax".
- The cover page's title and rule were cut where the "Sending" slip lay
  on the screen: the host's `TView::SetupVisRgn` went on past the first
  ancestor with a clipper and subtracted that ancestor's front mask,
  where the ROM (0x00267974) takes the clipper's visible region and
  stops - a print view's clipper is wide open, so what floats over it
  on the screen never reaches the page (82db9ab6).
- ctest `host.NewtonFaxSend` (b3187bfb): `fax-send.ns` sends a note
  through the ROM's fax transport to `fakemodem.py --fax-answer`, and
  `tools/modem/faxcheck.py` checks both pages (1728 wide, not blank, the
  cover's title and rule and the note's text inked) and makes PNGs of
  them.  fakemodem reads a DTE's Class 1 frames without an FCS, as they
  come, so it sees the fine resolution the Newton asks for (5d20b626).

## 2026-09-30: the caret's "nowhere"

- The "arrow" over the In/Out Box clock's first digit was stale caret
  bits (8250593c): the ROM's "no caret" is a rect whose top and bottom
  are -32768 (a point's v -32768), where the host used an empty rect, so
  a plain view as the key view (the Setup screen) drew its caret at 0,0
  and the saved bits were later restored there.  `OffsetToCaret`/
  `PointToCaret`, `GetCaretPoint` (the rect's left and bottom + 1),
  `CaretValid`, `DrawCaret` follow the ROM; the caret rect is the ROM's
  11 wide by 12 tall (the host had it transposed).  `test_Views` checks a
  plain key view draws nothing at the corner.
- The default button's outline drawing only its top and bottom is the
  ROM's own: `TView::PostDraw` (0x2685f8) marks a default button with a
  line above and one below.

## 2026-09-30: the printing system's imaging engine

- `fakemodem.py --fax-answer OUT.pbm` (a32f64de): a Class 1 fax machine
  for the Newton to call - CSI/DIS, the DCS read for the page's width and
  resolution, CFR or FTT on the training check, each page decoded by
  t4.py and answered MCF, written out at DCN; `--self-test` against a
  scripted caller (ctest `comms.FakemodemFaxAnswer`).
- `src/print/` (d89f2eb1, library `print`): the `TPrinter` and
  `TDotPrinterDriver` protocols, `TDotPrinter` imaging a page a band at a
  time through the scaling bottlenecks (each shape mapped to the
  printer's dots and drawn into the band through a phantom port, a
  patterned one through a mask), the driver callbacks.  ctest
  `print.DotPrinter` checks shapes pixel by pixel across bands with a
  144-dpi test driver.  `sync_ddk_headers.py` patches three printing
  headers that included "Objects.h" for objects.h.
- Host bug found: `ULong` is pointer-sized on the host, so word-at-a-time
  pixel code written with `ULong*` stepped 8 bytes and ran past the band,
  and `x <<= 1; if (x == 0)` sentinels never fired - such code uses
  `uint32_t` (`romsizes.py` does not catch it).  ROM bugs kept
  (`docs/print/README.md`): among them `TransferShape`'s notOr/notXor/
  notBic loops never advance the mask pointer; `ScaleStdRRect` scales the
  corner oval crosswise on a fax.

## 2026-09-30: ink in pickers, printed ink, key-command widths

- Ink in pickers (96e6c6af): a strokeList item drawn from its bounds at
  most 28 high, and a grid picture with a mask picking no cell over a
  blank part of it.
- Printed ink (38b6bdf8): ink had been left out of printouts; on a
  printer's port a sketch's ink and an ink word in text are now turned
  into outlined paths (`CSRawExpandGroup`, `GenericCSMakePathsGroup`,
  `InkMakePaths`) and framed; the printed pixels match the screen's.
- Key-command widths as the ROM's (6d36d2c9): the modifier icons' width
  added to each item (`GetKeyCommandModifierWidth`, counted twice
  against pickMaxWidth - ROM behaviour kept); the Action menu reads
  "Print Note ⌘P".  The boot sound is kept out of the host's sample
  counts without delaying the boot (`HostSoundSetAside`,
  `HostSoundBootPlaying()`) - the earlier wait had shifted boot timing.
  292 of 292.

## 2026-09-30: the boot splash; pickers' key commands

- The boot splash (c803c9e2): `TNotebook::DrawSplashScreen` (0x14602c)
  paints the screen black with the logo and, in white, "Newton 2.1
  (717006)" and the copyright lines, from `VersionString` (0x146cb8);
  `InitToolbox` then plays the boot sound and starts the international
  utilities, in the ROM's order.  The boot sound had to be left out of
  the tests that count sound samples.  ctest `host.NewtonSplash`.
- Pickers with a keyboard (10fce955): each item's key command
  (`GetKeyCommandInfo`/`GetKeyCommand`) drawn at its right, and a picked
  item's key message sent to the key view.  Found: `PickItem` returns at
  once for "no item" (0x187a84), so a cancelled picker runs no
  `pickActionScript` - the host had.

## 2026-09-30: dates, times and numbers typed as text

- `TDate::StringToDateFields` (0x8de6c), `StringToTime` and
  `StringToNumber` read through the locale's lexical dictionaries
  (a5582247): `ParseString` (0x18176c) finds the longest run of words the
  dictionary knows and walks it a character at a time with the Airus
  `VerifyCharacter`/`VerifyWord`, each character's attribute saying
  whether to gather it or convert what is gathered; `ConvertBuffer`
  (0x181adc) turns it into a field; numbers through the ROM's
  `TNumberParser`; `ReplaceDictionaryHandle` reopens the lexicons on a
  locale change.  The Assistant's "lunch at 1 pm tomorrow" opens its
  slip at 13:00 tomorrow (ctest `host.NewtonDateParse`).
- ROM behaviour kept: a two-digit year goes into the current century
  ("98" is 2098 in 2026); "tomorrow" in a frame can give 31 September;
  the U.S. time dictionary has no seconds; "1,234" reads as 1.

## 2026-09-30: selections follow their text; the caret under a hilite

- A selection follows its paragraph's text (f7913359): `AdjustHilites`
  (0x16a824) moves its ends past an insertion or a removal before them,
  shortens it for a removal at its start, takes it away for one reaching
  into it; `UpdateHiliteArea` remakes the areas whenever the lines are
  laid out again.  NOT YET: the copy of the selected text a
  `TParagraphHilite` carries for a drag.
- `TView::Hilite` (0x2660c4) hides the caret over the view it inverts and
  shows it after - a button pressed over the caret had inverted the caret
  with it (47348851).

## 2026-09-30: 'pixels binaries in the ROM's layout; NewtsCape's JPEG drawn

- A 'pixels binary has the ROM's 0x1c-byte big-endian header on every
  host (checked against ROM 0x415a4); drawing builds a host map from it
  (`qd/Pictures.h`'s `PixelsToPixMap`/`PixMapToPixels`); the store
  compander copies the header as the ROM does - the old host copy had
  also put the bounds at +6 rather than +8 (2d8b7832; 177cd11f restored a
  test the commit had dropped).
- The test servers take free ports (`newton --tcp-echo 0` and
  `HostEchoPort()`, `httpserve.py --port 0` and `NEWTON_HTTP_PORT`), so
  two build directories' tests never meet; the port locks are gone
  (735c506a).
- ctest `host.NewtonNetHopperNewtsCape` (c4382a70): NetHopper's JPEG
  viewer through NewtsCape's converter, its libjpeg on the ARM
  interpreter, the 56x28 icon checked pixel by pixel.
- Typing in a paragraph as the ROM's (45dc7fa7): a key over a selection
  takes it out first, the arrows move the caret across lines and between
  a page's paragraphs (`HandleUpDownKey`), a paragraph emptied by
  backspace goes on key up.

## 2026-09-30: the Newton Internet Enabler built into the ROM

- The owner's decision, and the first intentional edit of `romsrc/`
  (f0f58bbe, ce78858e): Newton Devices, the Enabler and Internet Setup
  follow WorldData in the ROM extension, so every boot has the NIE with
  no store and no `--package`.
- The ROM's loader relocates nothing and never installs a part's
  `_ImportTable` for a part in the ROM, so the builder puts each package
  into the ROM's own form (`packages.py` `rom_form_package`): refs made
  addresses, the relocation applied and dropped, the exports given 28 new
  'fexp entries and the imports resolved to them.
- Found: a package in the ROM has no pkgRef, so the NIE's
  `GetPkgRefInfo(ObjectPkgRef(...)).id` threw in `InetStartUp`; four
  checked five-byte patches (`inetenbl.patches.tsv`).  A copy installed
  over the built-in is kept but not activated, with the ROM's own "already
  in use" notice.  `build --original` keeps `host.ROMSourceCommitted`
  comparing the rest of the tree with the ROM.  The extension grows by
  0x92000 bytes; boot about 0.15 s longer; 18 KB less frames heap free.
  ctests `host.NewtonNIEBuiltIn`, `host.NewtonNIEOverBuiltIn`; 289 of 289
  with the ROM configure, 277 of 277 without.

## 2026-09-30: NewtScape's JPEG decoder runs

- NewtScape's JPEG converter (jpeg10e2.pkg, which calls into the
  NewtScape application nwcp20r2.pkg) is 63 KB of native C - a libjpeg -
  using the ARM's FPA floating-point instructions, which the ROM carries
  out with its own emulator: `armcpu/FPA.cpp` emulates them (DEVIATION),
  with `IsReal`, `IsNumber`, `IsBinary`, `CoerceToInt`, `ThrowMsg`
  answered (bc6500b8; `TestFPA`).  The decoder writes one row past its
  bitmap's end; a data window now reaches to the end of its page and such
  writes are dropped (DEVIATION).
- The decode runs to the end, but the picture's rows come out 20 bytes
  shifted: 'pixels binaries carry the host's 0x30-byte PixelMap header,
  where the ROM's is 0x1c bytes and package code reads the ROM's offsets
  - being changed to the ROM's layout everywhere.

## 2026-09-30: the pen's clicks and the views' sounds

- The sound natives were there; the views' calls were not.  Now, where
  the ROM makes them (b32a26bd): `FClicker` for a tap that places the
  caret, a tracked button, a picker's pick, the pen on the caret, caps
  lock; `FPlaySound` for a gauge's and the on-screen keyboard's sounds
  and the view effects' show and hide.  `HostSoundSamples()` lets a
  script hear what was played; ctest `host.NewtonClicks`.
  `host.NewtonRecorder` turns the pen and action sounds off, which
  otherwise diluted its test-tone check.  `docs/next-steps.md` lists the
  user-reachable NOT YETs.

## 2026-09-30: NetHopper's JPEG viewer without a converter

- NetHopperJPEG.pkg has no decoder of its own: its viewer calls
  NewtsCape's `JPEGConvert:NewtsCape` and answers nil without it.  With
  no NewtsCape, the viewer keeps that nil and `mScaleImage` throws on it,
  so the user sees "Sorry, a problem has occurred. (-48200)" and the
  picture stays a placeholder with its ALT text - as a MessagePad without
  NewtsCape would (58ff61a8; ctest `host.NewtonNetHopperJPEG`).  NewtScape
  is now a fixture; decoding through it is being done.

## 2026-09-30: a paragraph's SetupDone finished

- `TParagraphView::SetupDone` as the ROM's (0x17f5d8, 94c02bb8):
  `CheckStyles` (an ink word, or a face that draws past its width),
  the caches made for a paragraph that calculates its bounds and dropped
  again when its parents do not show it (the walk stopping at a print or
  remote view), none for one that does not (made when first asked, as
  the ROM's other callers do), text flag 0x20 (the first baseline put on
  the page's lines, `AlignToLineSpacing`), `ProcessStyles` (ink words
  read where the view's recognition keeps none; `InPrintOrPreview`).
- Exposed: the host measured a move for `RealDraw` from `fCachedBounds`,
  set only by `CreateAllCaches`, so lines laid out again after a view
  moved were offset twice - Internet Setup's Local IP Address slip lost
  its fields; `FillAllCaches` now notes where it lays them out.
- Reflow no longer cuts a long paragraph that does not calculate its
  bounds - by the agent's reading of the ROM, such a piece has no lines
  when `OffsetPastVisible` is asked; `host.NewtonReflow` expects 3 groups.
- Challenged and completed (334721bc): a note's text *does* calculate its
  bounds, and what cuts it between pages is `FillAllCaches`' clip (0x16bcd0)
  - with text flag 0x800, which `ReflowText` (0x1a5014) sets, only the
  lines within the view's own bounds are kept, else those within what its
  parents show - so `OffsetPastVisible` answers the end of the last whole
  line on the page.  Only a paragraph that does not calculate its bounds
  is never cut (ROM).  ctest `host.NewtonPrintLong`: a 40-line note
  prints on 3 pages, every line whole (`pagecheck.py --lines N`).

## 2026-09-30: live ink and the busy box

- The inker's live ink (c7129677): `TInker::Convert` takes the tablet
  buffer a record at a time (a pen-up rewritten with the pen size and
  the bounds its ink covered), `DrawInk` joins the points reading ahead
  up to 80, `LCDEntry`/`InkThem` read the strokes and wake the
  application; `TLiveInker` draws into a tile of at most 64x64 pixels'
  bytes ORed onto the display, never into the screen's bits.  The busy
  box (`TBusyBox`, commands 0x33-0x37) over `qd/BusyBox.cpp` and the
  ROM's busy picture.  `StrokeTime` does nothing while the inker runs, as
  the ROM's does.  ctest `host.NewtonLiveInk`.
- Found: `gWireRecog` is 1 (read at the RW data's base, 0x0C100800 -
  a report that layout.json's RAM_RW was wrong was a decimal-to-hex
  slip); `TBusyBoxEvent`'s command must be a ULong - as a Windows `long`
  its high half was rubbish and the commands were ignored.
- `host.NewtonKeyHelp` and `host.NewtonInet` wait on conditions instead
  of timers (cb7adb07).

## 2026-09-30: two NIE loose ends

- A new Manual Host network setup showed Ethernet's card picker again:
  Ethernet's `SetupViews` (inetstup 0x1f051) answers a manual
  configuration with its prototype's `newEntryViews`, which from the
  host's definition are Ethernet's; the host's data definition now
  answers `SetupViews` from its own card-less pages (3d1af11d).
- The NIE's connection slip was thought to lack a title strip; it draws
  all its template has - the NIE's own `SetupBounds` makes an envelope
  (the airmail border, a frame, the tab) with no stamp or title, unlike
  the ROM's routing slip; `nethopper.ns` checks the border (3406af97).

## 2026-09-30: the flakes - a world copied short

- The one-off crashes under a loaded parallel run (the boot crash in
  `TUPort::Receive` among them) were heap damage: `TInker` had no
  `GetSizeOf` (the ROM's is unnamed, 0x0038aadc, 0x118 - found in its
  vtable with `vtable.py`), so the inker's task ran on a copy only a
  `TAppWorld` long and its own fields lay past its stack block, putting
  'inkr' into a fork's `TAppWorldState` (215c6602).
  `host.NewtonBigStore.write` crashed in 2 of 30 stress copies before, 0
  of 36 after.
- The dns, echo and stream demos quit when done instead of a fixed 6-8 s;
  the tests sharing the 52372 echo port take a `RESOURCE_LOCK` (933663cb).
- `analysis/worldsizes.py` (ctest `tools.WorldSizes`) checks every task
  world in `src/` answers its own `sizeof`, beside the ROM vtable's
  +0x04: all 30 do (`TCommTool`/`TSerTool` abstract in the ROM); the
  inker was the only one missing.  `host.NewtonNetHopper` takes
  `RESOURCE_LOCK tcp_52381` (e1c26ea1).
- `tools/host/stress.py` (65533334): `--test` builds a test's fixtures in
  each copy's own directory; each run has a directory of its own; a
  fixed-port test's copies run one at a time.  The full suite passes 3
  rounds of 279/279 under `--suite --rounds 3 --hogs 8`.

## 2026-09-30: NetHopper browses

- NetHopper 3.2's native code is Newton C++ Tools code in four
  'nativeModule binaries called through BinCFunction frames (the HTML
  parser, ReadGIF and two small ones), its calls into the ROM anywhere in
  the binary; `gluetable.py`/`pkgns.py` now read such binaries (59
  entries).
- The ARM interpreter (e897aeff, e64499a8): one heap at 0x80000000 that
  outlives a call (`NewPtr`, `malloc`, `operator new` and their frees),
  `LockedBinaryPtr` windows at 0x58000000 kept until the last unlock,
  RefVar/RefStruct, `MakeInt` and the RefArg object functions,
  `NSCallGlobalFn`, the C string library; the package's relocation
  applied to the code binary where the ARM world maps it; binary bytes
  seen big-endian as they lie (a string's characters excepted).
- Bug found (0187f2b1): `TParagraphView::SetupDone` ran the view's
  `viewSetupDoneScript` before laying out a paragraph that calculates its
  height; the ROM (0x17f5d8) lays it out first - NetHopper's pages were
  blank.  The NIE's options are listed in `HostOptionLayouts` (a0c37a37).
- ctest `host.NewtonNetHopper` (52560c3c): `tools/host/httpserve.py`
  serves `src/host/demo/www`; NetHopper opens a URL typed on the keyboard,
  connects over the Host network and draws the page, its GIF decoded.

## 2026-09-30: shapes scrubbed, dragged and scaled; two test races

- The polygon view (aaa84574): `HandleScrub` (a scrub over more than half
  the view removes it - ROM quirk: TView's threshold is three quarters),
  `HitSegment`/`ScrubSegment` (the run of sides a scrub covers taken out),
  drag and drop (`AddDragInfo`, `GetDropData`, `DropRemove`), `Scale`/
  `ScaleInk`/`DrawScaledData`, and a double tap on ink reading it again
  (aeDoubleTap); `test_Views` TestPolygonEditing.
- The package-install ctests waited a fixed 4 s for a package the host
  sends only once the world is up; they now end on `demo/installed.ns`
  (Formulas2 installed) (029fae4e).  walkthrough 2 keeps its card beside
  its store (`HostStoreFile()`), so copies in their own directories stay
  apart (9a42afa5).
- The "^" at a page's top left after a scrub is the new note's insertion
  caret, shown because the host reports a keyboard - the ROM's behaviour.

## 2026-09-30: the month overview and traced selections

- The Dates month overview (ab187149): `DrawMonthOverView` (0x122174)
  and its helpers - each day a gray-framed box, each meeting a black bar
  from its start to its end (7 am-7 pm at half an hour a pixel in a small
  box), the day's notes as icons; ROM bug kept: if asking for the month
  throws, every bar is drawn with the month's first meeting's times;
  ctest `host.NewtonMonthOverview`.
- `HiliteTraced` (1f6e7672; `views/PolygonTraced.cpp`): part of a shape
  selected by tracing along it, the ends snapped to segment starts,
  middles and ends; deleting it cuts the shape (`RemovePoints`' partial
  path, the undoable points command 0x44); `TPolygonView::OuterBounds`.
  ROM bugs kept: `AddInterval` merges an interval spanning several others
  with the first only; `ValidatePoly`'s bounds check sets nothing.  ctest
  `host.NewtonTraced`.
- Picture playback's text, curves and paths were already in: no ROM
  picture uses them (`analysis/pictures.py`).

## 2026-09-30: editing the Host network setup

- Setting the Host network setup's domain name threw -48418: Internet
  Setup's `ValidateTarget` (inetstup.pkg 0x1a001) takes
  `Length(entry.DNSServers)`, and the host's setup had none of an
  Ethernet setup's slots.  `HostLink.ns` now gives it Ethernet's
  `blankEntry` (less the card, configuration 'usingServer), gives its
  data definition that `blankEntry` and calls the prototype's
  `FillNewEntry`, and repairs setups kept from before (94120b30).
- On a restart the host's link was never registered - the store's
  packages are activated before `HostLink.ns` starts, so it missed their
  soup changes; it now looks again two seconds after starting.
- The host's DNS tool keeps the setup's domain ('ddom) and asks again
  with it for a name with no dot the resolver cannot find as it is
  (`NEWTON_TRACE_DNS`).  ctests `host.NewtonInetHostSetup` (and
  `.restart`).

## 2026-09-30: the tablet driver and the inker

- The ROM's `TTabletDriver` protocol and `TabInitialize` (a registered
  "TMainTabletDriver" before the MP2x00's `TResistiveTablet`);
  `TabShutDown`/`TabWakeUp` real, so sleep shuts the tablet down; the
  inker, the 'inkr world `TInker` - woken for each sample, a 50 ms idler
  while the pen is down, the pen modes, the calibration read and written,
  and the ROM's own calibration screen (two corner targets give the scale
  and offset, a third checks them within 10 px, repeated until it
  passes); the script side in `InkerNatives.cpp` (c0d0778f).
- The host's panel `hal/host/HostTabletDriver.cpp` samples newton's
  window, exact under the factory calibration so the mouse is accurate
  out of the box; `HostTabletSkew`/`NEWTON_TABLET_SKEW` put it askew for
  tests; a `--script` run's calibration targets are tapped for it.
  ctests `host.NewtonAlignPen` (a skewed panel lands a tap 30, 4 off;
  after Align Pen it is exact) and `.restart` (the calibration read back
  from the store, no calibration screen).

## 2026-09-30: a fresh store mounts cleanly again

- `HostPrepareStore` makes the Names soup at mount, before the boot's
  NewtonScript has made `vars.userConfiguration`; the soup's modification
  time read the clock, whose zone offset (`RealClockZoneOffset`, since the
  one-clock change) asked `GetPreference`, which throws as the ROM's does
  (0x001290d8).  The ROM never makes a soup that early.  The offset is now
  0 until the user configuration exists (10e8dd06).  No other soup was
  lost: the Cardfile's InstallScript made Names later.  ctests
  `host.NewtonFreshStore` (and `.memory`): a fresh store mounts cleanly
  and has the 15 soups a ROM boot makes.

## 2026-09-30: the rest of a first day

- `src/host/demo/walkthrough2.ns` (6af90e31; ctests
  `host.NewtonWalkthrough2` and `.restart`): every Prefs panel with three
  settings changed, Bold, Undo after a scrub, the on-screen keyboard, the
  home city set to Seattle, Formulas, the minute timer ringing, the
  Dock's picker, the help book, a note duplicated and filed on a memory
  card - and a restart that finds it all.  Found and fixed on the way:
  the Sound panel's empty output picker (24c84fb2) and alarms that never
  rang (47a76c4c, then the year-2010 fix).  The walkthroughs share
  `demo/walkhelpers.ns`; the year-2010 demo is ctest
  `host.NewtonYear2010` (and `.romBug`) (11e253b7).

## 2026-09-30: a bigger internal flash, in a sparse file

- What the ROM allows (`docs/stores/README.md`, "Bigger flash"): chips
  are found by a flash driver (the ROM's own knows three 1-2 MB parts),
  a range's size is chips x chip size, at most two banks and three
  ranges; the read windows (from 0x30000000) meet the write windows
  (0x34000000) at 64 MB - the largest the ROM's code takes unchanged; the
  store's migrated directory entries name at most 1024 blocks, 128 MB.
  128 MB with the write windows moved to 0x38000000 (DEVIATION
  `InternalFlashWriteWindow`, `stores/flash/Flash.h`; nothing on the
  flash changes) (37cb0497).
- The sparse host flash image 'NewtFlsh' (`hal/host/HostFlash.h`): a
  header, a map of 1 KB chunks, only the chunks not all 0xFF stored,
  written chunk before map word so a torn write leaves a readable file;
  new stores are sparse by default - 17 KB at 4 MB, 1.5 MB formatted at
  128 MB, 11.75 MB with 9.9 MB of entries; flat files (Einstein's) still
  open.  `newton --flash-size`/`--flat-flash`, `tools/stores/flashimage.py`;
  ctests `stores.HostFlash`, `host.NewtonBigStore*`, `tools.FlashImage`.
- Bug found: `SDirEnt::IsValidMigratedObjectInfo` was reconstructed as
  `< 0x3FC0 && < 0x3FF` where the ROM's is `<= 0x3FFF && <= 0x3FF`
  (177aeb33).

## 2026-09-30: the year-2010 fix

- The owner's decision: fix the ROM's year-2010 overflow rather than
  shift the host clock (8155b447; `docs/intl/year-2010.md`, DEVIATION).
  The community's fixes, from what their pages say: Avi Drissman's
  Fix2010 shifts the minute-based functions by 16-year "hexades", Eckhart
  Köppen's Patch 71J059 and 711000 move the seconds functions' time base
  (the exact patched offsets are not published).
- Ours: a script's seconds value, which wraps at 2^30 on 5 January 2010,
  is read back as the moment within 2^29 seconds (about 17 years) of now
  it is congruent to (`intl/Dates.h`'s `ClockSecondsFromScriptSeconds`),
  where seconds become a date or a clock value again -
  `TimeInSecondsToTime`, `DateFromSeconds`, `SetTimeHardware`/
  `SetTimeInSeconds`, `SetSysAlarm`; `TimeInSeconds` itself and what is
  already stored are unchanged, and up to 2010 the results are the
  ROM's.  `NEWTON_ROM_2010_BUG=1` keeps the ROM's arithmetic.  The
  28-year clock shift is gone: the clock chip holds the true GMT.
- With the true 2026 clock notes are dated today and an alarm rings once
  (`test_Dates` TestYear2010, `demo/year2010.ns`); with the switch the
  ROM's 1992 comes back.

## 2026-09-30: power

- `src/power/` (d68de1dc; `docs/power/README.md`): the 'pg&e power
  manager world (`TPowerManager`: the batteries by RPC, the power switch
  'powr - every system event handler told, then the application asked to
  sleep, a machine that does not answer in ten seconds taken for hung and
  rebooted - and the backlight button), `CyclePower` (the machine put to
  sleep through generic system call 0x44, alarm-only wakes back to sleep
  through `SleepingCheckFire`), the `PBatteryDriver` protocol (a
  machine's own PMainBatteryDriver looked for first); the newt world's
  'powr, 'pwch, 'dead and 'bats; the battery natives ask the manager by
  RPC, as the ROM's do.
- The host: asleep, the machine waits for the power switch (F12), a tap,
  a key, an alarm or `HostWakeAfter(ms)`; the display blanks asleep and
  washes out under the backlight (F11); the host battery driver reads
  Linux's sysfs, else fresh cells at 100%.  ctests `power.PowerManager`,
  `host.NewtonPower` (the idle timer sleeps the machine and a tap wakes
  it, the switch likewise, the backlight toggles, a battery gauge reads
  100).
- A curiosity: the power-off functions are named for a Japanese office's
  approval process (`PowerOffSoodan`, `JooHooShuuShuu`, `YobiKaiGi`,
  `RingiSho`).

## 2026-09-30: one real-time clock, and alarms that ring

- The host had two clocks that never met: the kernel's real-time clock
  (what every alarm is set on) stood at 1904, and `RealClockSeconds`
  read a base of its own - so alarms never rang.  Now `RealClockSeconds`
  and `SetRealClockSeconds` read and set `TURealTimeAlarm`'s clock, which
  holds GMT, adding the home city's offset and daylight saving as the ROM
  does (0x255578); `TTime::ConvertTo` wraps at 32 bits as the ARM does
  (47a76c4c).  The status bar shows the home city's time (San Francisco
  until Setup or Time Zones sets another).
- DEVIATION (`HostViews.cpp`): the host sets the clock chip to GMT
  brought back by whole 28-year cycles below 2010, so weekdays and leap
  years still match - 2026 shows as 1998.  With the true date the ROM's
  own year-2010 overflow (`TimeInSeconds` past a NewtonScript integer)
  made every alarm fire the moment it was set, in an endless loop.  The
  owner then decided to fix the overflow instead (below).
- The volume gestalt carries the MP2x00's outputs and flags (0x1d), so
  the Sound panel's "Play using" picker no longer throws and the
  Recording panel shows in Prefs (24c84fb2).

## 2026-09-30: a fax received over a Class 2 modem

- `fakemodem.py --fax-call --fax-class 2` (and 2.0) (2e712a16): the ROM's
  Class 2 receive path (ATA, +FDR, +FPTS=1, +FDR) worked as reconstructed;
  ctest `host.NewtonFaxReceiveClass2` receives the page into the In Box,
  the same bytes as over Class 1.  A Class 2 modem hands the page over
  most significant bit first, which the ROM reverses byte by byte; the DC2
  the Newton sends to say it is ready is not a command.  Fax is now done
  both ways over Class 1 and Class 2.

## 2026-09-30: the default boot is the reconstructed data

- The owner's decision: boot from the reconstructed data by default, with
  no hard dependency on the ROM files (32a086d6, d5336a77).
- The default build makes `<build>/romsrc-objects.bin` from `romsrc/`
  (Python 3 and newtonscript, no ROM image), rebuilt only when `romsrc/`,
  the builder or newtonscript changes; `newton` and `newtonscript` boot
  it with no option (`host/HostObjectsFile.h`: `NEWTON_OBJECTS`, beside
  the program, the build's path).  A missing file is explained and fatal
  - no fallback to the image, so a broken build cannot hide behind a
  boot that looks the same; `--rom` boots the image and says so.
- A configure with no ROM image builds and runs; the tests that read the
  image are not registered (`src/CMakeLists.txt` wraps `add_test`).  The
  host demo ctests run on the reconstructed data: 258 of 258 with the
  ROM, 198 of 198 (9 disabled) without; `host.Newton`, the two
  SameScreen tests and `host.NSDecompileRoundTrip` stay on the image as
  the cross-check.
- No-ROM coverage (d1ec7786, 054f56c5): `packages.py` reads the ROM
  extension out of an object file, so `host.NewtonPackage.extract` makes
  Formulas2.pkg from `romsrc-objects.bin` (byte for byte the image's)
  and the 9 package tests run without the image; the 39 unit tests that
  needed only the ROM's objects import the object file (`NEWTON_OBJECTS`).
  No-ROM configure: 248 of 248 run and pass (198 before); with the ROM,
  259 of 259.
- The last run-time read of the image: package native code reads
  `gROMVersion` (0x13dc), now carried as `romsrc/romdata/gParamBlock.bin`
  (`romsrc.py romdata`).

## 2026-09-30: the IR port is always there

- newton installs the host IR chip and the IR comm services on every
  boot (aea67589): with no `--ir-peer` it is a port with nobody in front
  of it (`HostIRChipInstall(nil)` - what it sends goes nowhere, its
  interrupts asked for only while a tool has work), so Beam from the
  Action button looks for a receiver and says "No response." where it
  had answered -26005 (no such service); ctest `host.NewtonBeamNobody`.

## 2026-09-30: a fax sent over a Class 2 modem

- `fakemodem.py --fax-class 2` (and `2.0`) is a T.32 modem running T.30
  itself (6c6ee709); ctest `host.NewtonFaxSendClass2` sends the note
  through the ROM's Class 2 path (`FaxToolClass2.cpp`: +FDT for each
  page, +FET between and at the end), unchanged since it was
  reconstructed, and checks both pages.  The Newton prefers Class 1
  whenever a modem offers it, and never picks 2.0 with a default modem
  profile (`TCMOModemFaxEnabledCaps`' service classes are 0, 1 and 2),
  so 2.0 is covered only by fakemodem's own self-test.

## 2026-09-30: a user's first hour

- `src/host/demo/walkthrough.ns` (08a2e923; ctests
  `host.NewtonWalkthrough` and `.restart` on the same store): Setup, a
  note written and read back, a Names card typed in, Find, a meeting, a
  to-do scrubbed out, the Assistant's "call ann", a note faxed Later into
  the Out Box, filing, the Calculator, the volume - then a restart that
  finds it all.
- Fixed on the way (da528d7b, ecc30aaf; `test_Views` checks each):
  `MakeTextLines` (0xdd234) ends each line's shape at the ascent, so a
  Names card's name is no longer dropped by `AddAllInfoItem`; an input
  line's first baseline sits at viewLineSpacing - 3 (- 4 over 20) and
  only a one-line paragraph is justified vertically (0x10d8d4, 0x10ddc0),
  so input-line text is no longer drawn above its line; a paragraph that
  calculates its bounds grows to its lines in `CreateAllCaches`
  (0x16baa8), so a `MakeTextNote` note is no longer invisible.
- A demo's globals must not have generic names (index, next, open,
  wait): ROM scripts that reference a free variable find them.

## 2026-09-30: the ROM's fonts as editable text

- The 13 'sfnt binaries are sfnt containers with no outlines: five bitmap
  fonts with Apple's bloc/bdat strikes (System plain and bold, Fancy,
  Simple, Casual) and eight metric-only printer fonts (Helvetica and
  Times Roman, four faces each).  Renamed .ttf to .sfnt (6116f016), then
  unpacked by `tools/fonts/newtonsfnt.py` (pure Python) into a BDF file
  per strike and a text file per table, packed back byte for byte on
  build (11734169; the committed `romsrc/` converted, ea150e00).
- What the tables hold (`docs/qd/fonts-sfnt.md`): every strike is index
  format 3 with image format 1, one bit deep; glyphs past maxp's 244 and
  the last end offset are at offset 0 (the missing-glyph box); the ROM
  picks a strike by ppemX and reads only the horizontal line metrics;
  `hsty` is the extra advance each synthesised face adds, read only for
  the metric-only fonts - ROM quirk: subscript reads past its end, into
  the file's padding.  `docs/qd/README.md`'s System strikes were its
  `userSizes`; corrected.
- ctests `tools.NewtonFonts` (all 13 round-trip; edits to a pixel, a
  width, a cmap entry, an hsty value change only what they should) and
  `host.ROMSourceFontEdit` (a pixel flipped in the 9-point System "A"'s
  BDF is the one pixel that changes on screen).

## 2026-09-30: printing to the host

- `THostPrinterDriver` (0d2d4de9; `print/host/HostPrinter.h`) behind
  `TDotPrinterDriver`: each page a 300-dpi one-bit PNG of the whole sheet
  (letter 2550x3300, A4 2480x3508) in `newton --print-dir` (default the
  working directory), written with deflate's stored blocks so it needs
  only the C library; bands of 200 dots, halved to 25 when memory is
  short.  `HostInstallPrinter` adds "Host printer (PNG files)" to
  `AvailablePrinters`, which the Print slip's Choose Other Printer lists.
- `GetNames` and `ExtractNameFromNetAddress` (1f99b702,
  `comms/AppleTalkNatives.cpp`): the Print slip's Printer picker failed
  without them.
- ctest `host.NewtonHostPrinter` (`src/host/demo/print.ns`): a note and a
  Names card printed from their Action buttons by taps alone;
  `tools/imaging/pagecheck.py` checks the pages.

## 2026-09-30: a text shape's baseline

- The In Box item header's two lines lay over each other: the ROM's
  `DrawOneShape` (0xdfd00) draws a 'text shape with its baseline on its
  bounds' bottom, and the host drew it at the top plus the ascent, so
  `GetTitleInfoShape`'s full-height info line landed on the title line
  (1c983e9c; `test_Views` checks the baseline).  Other text moved as the
  ROM places it: "There are no Names in this folder" 3 px lower, the
  button bar's labels 1 px higher.

## 2026-09-30: the system alerts

- `src/alert/` (99c1f26e): the ROM's own alert engine (0x2e7d4-0x30bcc) -
  `TAlertDialog`/`TAlertItem`, the pen tracked through `PollTablet`,
  drawing straight into the screen's bits in the ROM's alertFont
  (`TAlertGlyph` over its bloc/bdat tables; the ARM's big-endian words
  and unaligned loads emulated), `TOSErrorAlertDialog`,
  `TErasePersistentDataAlert`, and the 'alrt world (`TAlertManager`,
  started from `TNewtWorld::MainConstructor` before the card server).
  `pcmcia/CardAlerts.h`: the card reinsert and position dialogs;
  `ReinsertCard` and the card server's alert procs are real; qd's
  `SetScreenInfo` is real.
- ctest `host.NewtonCardAlert` (`card-alert.ns`): a card whose store is
  locked is pulled, "Newton still needs the card you removed..." appears
  (checked with a new `ScreenPixel(h, v)`, snapshot `build/card-alert.png`),
  and putting the card back takes it down.
- Found: a host card's memory must stay at the same address across a
  reinsertion, as a socket's window does; while an alert is up the
  display's updates are held (`BlockLCDActivity`, DEVIATION: no screen
  semaphores on the host), or the application's redraws cover it.  ROM
  quirks kept (`docs/alert/README.md`): the font asked for size 9 as a
  raw value where 16.16 is meant (the smallest strike); a glyph range
  check with && for ||; half-width glyphs at depth 2; button hit
  rectangles offset; alerts taken off the list never freed.
- qd's `LocateEntry` made the ROM's (7a457507, 0xaebec): the search
  stops when the distance grows and does not step past a strike that is
  not one bit deep (ROM quirk kept).  The Setup, Notepad, Names and Dates
  screens are byte-identical apart from the clock and every text test
  passes, so the host had been picking the same strikes.

## 2026-09-30: a received fax shown and turned

- `qd/Tile.h` (c66294d5, ctest `qd.Tile`): `TTile` turns a bitmap too big
  to turn whole (a fax page, 1728x1146) a 64x64 tile at a time -
  `RotTiledBitmap` copies the rows into 512-byte tiles, `RotateTilesR`/
  `RotateTilesL` write the turned tiles and `Untile` them, the progress
  reported through the options' callback, the pixels made on the
  bitmap's store like the original's.  ROM quirk: leftover rows are
  turned only in whole groups of 8, so a fax page loses its last two
  rows.  ROM bug kept: `RotateTilesL` turns the leftover rows wrongly and
  writes up to 512 bytes past the end of the new bitmap - DEVIATION: the
  host drops those out-of-range bytes, which on the device landed in
  whatever followed (a fax page on a store is a large binary, where
  slack would not help; `test_Tile` checks the bytes after it stay
  untouched, 6990690e).
- The In Box shows a received fax (`GetRoot().iobox:Open()`,
  `:ShowItem(entry, 'itemLayout)`) and FaxViewer's rotate turns it
  through `MungeBitmap` -> `RotTiledBitmap`; `fax-receive.ns` snapshots
  both (`host.NewtonFaxReceive`).
- The `host.NewtonNTK` flake did not reproduce under stress.py (18
  copies beside hogs, 5 suite rounds).
- Found: the modem tool changes flow control only after it reads
  CONNECT, so an XOFF in a frame arriving with the CONNECT was eaten
  under the old soft-flow settings and the call's DCN lost;
  `fakemodem.py` now waits 0.2 s between CONNECT and the data, as a real
  modem's carrier training does.

## 2026-09-30: a card with Einstein's layout

- `tools/cards/linearcard.py` (64c294bd): `info FILE` prints a
  TLinearCard image's footer, name and CIS tuple by tuple; `make FILE
  --size MB --cis HEX` writes a blank card with any CIS (the layout from
  its description, no Einstein code copied).  A card with Einstein's
  default CIS - 2 MB of Intel 28F008SA - is recognised, formatted at the
  ROM's prompts and written on (ctest `host.NewtonCardEinstein`).  Not
  yet checked: an image Einstein itself wrote, and ours opened in
  Einstein.

## 2026-09-30: memory cards mount

- The PSS manager's 'pssm world (d7abcf11; `stores/flash/PSSManager.h`):
  `TPSSManager` - `CardAvailable`, `CardGone`, `CardIsSame`, `UIEngine`,
  `Register`/`DeregisterStores`, `GCStores` - making a flash store for each
  device a new card's 'card event names; `stores/PSSInfo.h`.  The newt
  side: `pcmcia/NewtCardEvents.h` (`TNewtCardEventHandler`,
  `HandleCardEvents`) and `newt/StorageCards.h`
  (`StorageCardInserted`/`MountStore`, `StorageCardRemoved`/`UnmountStore`);
  `TNewtWorld` dispatches 'card, 'rstr and 'stor.  `HostMountStores`
  starts the card server before the PSS manager, the ROM's order.
- `newton --card file` (a blank 4 MB card made if missing);
  `HostCreateCard`/`HostInsertCard`/`HostRemoveCard` for scripts.  ctest
  `host.NewtonCard` (`src/host/demo/card.ns`): a blank card goes in, the
  ROM's NewtonScript card handler asks to erase and format it, the store
  mounts ('flsh, slot 0), a soup written on it survives the card being
  pulled and put back.
- Found: without the internal flash `InitPSSManager` never ran, so
  `TFlashStore` was never registered and a flash store could not be made
  by name.  ROM bug kept: `HandleCardEvent`'s 0x6f case replies into the
  message it has just deleted (the host skips the write).

## 2026-09-30: the card server

- `pcmcia/CardServer.h` (caccd716, ctest `pcmcia.CardServer`):
  `TCardServer` ('cdsv') watching the sockets and `TCardProcessor`
  ('cdpr') reading a new card's CIS and offering it to each handler; the
  socket states; the 'card' system event a new card is announced with
  (`TNewCardAsyncMsg`); `TCardDomains` (DEVIATION: no fault monitor - host
  card memory never faults); `InitCardServices`, `GetSocketInfo`, a real
  `GetCardInfo`.  Nothing is detected until the application sends its
  port (message 100).  The test puts a card in, takes it out, puts it in
  again.
- Host bugs found: the socket's `ResetInterrupts` disabled card detection
  (the ROM's keeps it enabled), so a second insertion was never seen;
  `GetVPCPins` had no voltage-sense pins, so every card was bad power.
  ROM bugs kept: `TCardProcessor::DoCommand` checks socket > count rather
  than >=; `GetCardInfo` asks the CIS it has just done, not the card, for
  the next CIS.

## 2026-09-30: a fax received end to end

- `tools/modem/fakemodem.py --fax-call PAGE.pbm` (dded806c) is a Class 1
  fax machine calling: T.30 as the caller behind +FTH/+FRH/+FTM/+FRM, the
  page coded by t4.py.
- `src/host/demo/fax-receive.ns` (97154442, ctest `host.NewtonFaxReceive`,
  about 15 s): the ROM's own 'FaxReceive:Newton transport answers and
  runs the whole exchange - CSI and DIS, TSI and DCS, the training check
  and CFR, the page (201 lines), EOP, MCF, DCN - and the fax lands in the
  In Box, "From "fakemodem fax"".
- Host bug found: `HostOptionLayouts`' 'w' is pointer-sized, but a
  NewtonErr is a C long, 4 bytes on Windows, so 'feom''s bytes after the
  extended option's result were read 4 bytes off and the tool aborted
  (-10007); a new 'l' field kind is a C long.
- ROM behaviour kept: a received page always ends with one bad line
  (`DecodeLine` runs past the RTC into the underflow - well within
  MCF's 5.2%); after MCF the caller's DCN is taken as an unknown phase D
  command and the tool aborts with -22001, which the transport survives,
  the page kept.

## 2026-09-30: the fax tool

- `comms/fax/FaxTool.h` (24d4622f, library `comms_fax`): `TFaxTool` (serv
  'faxs') whole - the ITU-T T.30 procedure over the modem tool one phase
  at a time (A the call; B CSI/DIS/TSI/DCS and the training check; C the
  page, coded by `EncodeT4` or decoded by `TT4FaxLine`; D MPS/EOP and
  MCF/RTP/RTN; E DCN), each step a control request to the modem tool, and
  the Class 2 and 2.0 sequencers; the fax options; `TFaxService`,
  registered in newton beside 'mods'; `EncodeT4`/`T4AddRTC`.  ctest
  `comms.T4Encode`: t4.py decodes a page `EncodeT4` coded.  The training
  check passes within 25% of 1.5 seconds' bytes with a run of noughts at
  least two thirds of it; a page is answered MCF up to 5.2% bad lines,
  RTP up to 15.2%, RTN beyond.
- ROM bugs kept, among them: `TimeOutKillComplete` clears the
  modem-request flag in the wrong word; `GetIdentification` scans back
  past the FIF's start when it is all spaces; `C2ParseDISResponse` never
  parses the eighth parameter; `C2DisFromCapabilities`' last rate test
  answers '0' either way; two phase-B functions read an uninitialised
  "DCS valid" register (false on the host).

## 2026-09-30: memory cards, the first three layers

- The host's PC card (2c50936d, ctest `hal.HostCard`): a card image in
  Einstein's TLinearCard container (`hal/host/HostCard.h`) and a
  `TCardSocket` over it, two sockets (`hal/CardSocket.h` replaces the
  DDK's).
- The CIS (ded9228d, c9862e92, ctest `pcmcia.CardCIS`): every function of
  `TCardCISIterator`, `TPCMCIA20Parser` and `TCardPCMCIA`.
- The memory card handler and the card's store (a470185b, ctest
  `pcmcia.MemoryCard`): `TCHMemModem`, `TCardHandler`
  (`pcmcia/CardHandler.h` replaces the DDK's), the card power manager,
  `TCardMessage`, the host's `TFlashSeries2` (`stores/flash/CardFlash.h`)
  and `TFlashStore`'s card branches - a 4 MB card store formatted,
  written, read back after the card is pulled and put back, and
  write-protected.
- Findings: the ROM's `TCardDevice` is 0x20 bytes because the ARM
  compiler gave its three one-bit fields a whole word, so Ghidra's field
  names for it are three bytes out; `IdentifyCard` needs
  CISTPL_DEVICE_GEO (without it the block size comes out as 1).

## 2026-09-30: the fax page decoder

- `comms/fax/T4FaxLine.h` (bdc0ad06; 0x204698-0x204e34): `TT4FaxLine`,
  the MH code kept in a ring as it arrives (the fill between lines
  dropped), read least significant bit first and decoded a line at a
  time over the ROM's decoding trees (`T4Tables.cpp`, from romtable.py's
  new `ptr` type - tables of pointers into other tables of the run).
  `tools/modem/t4.py` is an MH coder written from ITU-T T.4 alone; ctest
  `comms.T4FaxLine` decodes its page, 200 of 200 lines.
- ROM bug kept: `GetNextBit` pre-increments its read pointer, so the
  ring's first byte after a Reset is never decoded (a real fax machine's
  fill before the first EOL hides it).  The exception it throws on
  underflow is named `evt.ex` itself, so catching it catches every
  evt.ex.

## 2026-09-30: the internal store in the ROM's own flash format

- The flash (9fd409e3, ctest `stores.Flash`): `hal/host/HostFlash`, a
  file in Einstein's layout (writes buffered and flushed at exit,
  15143842), `THostFlashDriver`, `TFlashRange` and its 8/16/32-bit
  forms, `TNewInternalFlash`, `memory/MemoryAllocator`, the host MMU's
  sections.  On-flash structures use a 32-bit `FlashWord`: the host's
  ULong is 64-bit.
- `TFlashStore` (d61974ee, ctest `stores.FlashStore`): the log, blocks and
  directories, transactions and separate transactions, compaction into
  the spare, recovery at mount.
- `TMuxStore` and its monitor (015307a2): the store every task uses.
- The internal store on the flash (b1119750): `stores/flash/PSSManager`'s
  `InitPSSManager`, `gInRAMStore`/`gMuxInRAMStore`, `GetInternalStore`
  answering the mux; `newton --store` is the flash file.  Bug found: the
  host's large-object commit (`JoinStoreTransaction`) read 0x20 bytes of
  a store package's 0x14-byte root, got ObjectOverRun and silently
  skipped the join - `THostStore` saved the objects anyway, and only the
  flash store's recovery, rightly throwing away the unjoined separate
  transaction, exposed it.  225 of 225 ctests.

## 2026-09-30: the comm trace frame and the event collector

- `utility/EventCollector.h` (c6bd3857; 0x002dc18c-0x002dc714): the
  TEventCollector protocol and `THistoryCollector`, a ring of (time word,
  event) entries registered in `gEventTraceBufArray` for a debugger;
  `InitEvents`, which `TLoader::TheMain` now calls.
- `comms/CommTrace.cpp`: the natives of the ROM's "Client Trace" frame
  (@661: `CFInstantiate`, `CFRecord`, `CFDispose`) and `translate`
  through the flatteners.  ctests `utility.EventCollector`,
  `host.NewtonCommTrace`.
- ROM bugs kept: `AddDescriptions` answers false even when it added them;
  `CFInstantiate` gives the collector the count of every trace element
  though only those with a string are entered, and writes it before
  checking the allocation; `translate` throws a stale MemError after a
  translation that went well.  DEVIATIONs: cfrecord on a frame with no
  collector (the ROM calls through nil) does nothing; the time word is
  GetClock's low word, not the counter at 0x0f181800.

## 2026-09-30: protoEndpoint, the 1.x endpoint

- `comms/ScriptEndpoint.h` (b6feda84): `TScriptEndpointClient`
  (0x00067440-0x0006b5c0), protoEndpoint's C++ side, and the 41 CI
  natives - 1.x option frames, synchronous Connect/Listen then
  asynchronous, output through a 1024-byte window with Yield, input specs
  by byteCount, endCharacter or end of packet; `comms/ModemOptions.h`
  ('mdo ', `SetDialingOptionsFromPrefs`); `CNullPipe`.
  `src/host/demo/protoendpoint.ns` (ctest `host.NewtonProtoEndpoint`)
  echoes through `--tcp-echo`.  In NewtonScript `$
` is a carriage
  return (13), which an endCharacter test must match.
- ROM bugs kept: a raw-binary option's bytes are copied from the option
  frame, not its data; the dialing option's five switches get the Ref's
  low byte (true and nil both true); `CIJustListen` passes the options as
  Listen's address; the destructor tests the frame against 0, not nil;
  startCCL sets gCCLState a second time instead of clearing it;
  `AEHandlerProc` gives the exceptionHandler MAKEINT(data) whatever the
  data is.
- Host bug fixed: `TSerialEndpoint::HandlePutReply` did not give the
  client the count of bytes sent (the ROM copies `fPutBytesCount`), so a
  client counting its output never saw it drain.

## 2026-09-30: the NTK inspector

- `comms/NTK.h` (library `comms_ntk`; aceda234, 4885defe): the task-safe
  ring buffer and pipe; `TNTKNub` speaking the 'newt' 'ntp ' protocol
  (connect, code blocks evaluated and answered, package download and
  delete, text, exceptions and the break loop, ...) over a `TNTKTask`
  and `TNTKEndpointClient`; the NTK and serial REP translators;
  `TREPEventHandler`; `NTKStackTrace`; the six natives.
  `TNewtWorld::MainConstructor` now calls `NTKInit()` (0x0030d280) and
  `ResetREPIdler()` (0x0030d29c).  `tools/ntk/inspector.py` is a desktop
  inspector over MNP on port 3679 - it evaluates expressions (through the
  Newton's own `Compile`), installs and deletes packages (ctest
  `host.NewtonNTK`; `--connect 127.0.0.1:3679` interactively).
- ROM bugs kept: `NTKInit` registers `PStdioInTranslator` twice and never
  `PStdioOutTranslator`; the nub's destructor frees the buffers without
  waiting for the task, so an `ntkDownload` straight after
  `ntkListener(nil)` answers -10068 (the demo retries); `ntkDownload`
  deletes a running listener's nub without stopping it; a 'code' answer
  carries the command's length, not the result's.  Host bug found:
  `SetPtrName` on a block from the C library's allocator wrote a Newton
  heap header into the Windows heap (0xC0000374); the translators now
  allocate those blocks from the pointer heap, as the ROM's malloc and
  operator new do (the pipes constructed in place), so they are named as
  on the device (83843786).

## 2026-09-30: the last natives outside comms

- `store:HasPassword`/`SetPassword` (825c01ab) over the ROM's password
  check, which holds a **master key** - a password whose key is
  `c1855223 d339abef` opens every store (`docs/curiosities.md`);
  `GetCardSlotStores` (nil: no sockets, 81391c5c); `RegisterGestalt`/
  `ReplaceGestalt` (a script's gestalt read back in its marshalled order;
  the ROM's leak of the block it builds kept), `BootSucceeded`/
  `QuickLookDone` (the `bootResults`/`bootFailure` files), `UnmountCard`
  (13c7f765); `RepeatInfoToText` (`intl/RepeatText.cpp`) and
  `MeasuredNumberStr` (ROM bugs kept: a whole number too wide answers
  nil; the cut is written into the caller's string) (6607113e);
  `DrawOriginal` (ROM bug kept: the ratio upside down, so the box grows),
  `LoadFontCache` (d0050d50); `DrawExpando` (7998ac73); the serial
  debugger's two natives (`testing/SerialDebugger.cpp`, 726d07f6);
  `store:SuckPackageFromEndPoint` (`packages/EndpointPackages.cpp`,
  `packages` linking `comms_script`; ctest `host.NewtonEndpointPackage`,
  06970bfe); `instance:Dispatch` on a monitor protocol (b72c71b9).
- natives.py 1214 -> 1231 of 1326; ctests `host.NewtonGestalt`,
  `host.NewtonNatives`; 219 of 219.

## 2026-09-30: beaming over the default path

- ctest `host.NewtonBeamIrDA` (43717fe6): with zapCommToolId nil the probe
  answers IrDA and a note goes from one host to the other over 'irda' -
  discovery, the IAS lookup, the LSAP connection - with no code change
  beyond the IrDA stack.
- The 'paperroll note was not lost (508c1b82): a beamed note of that
  class is put away into the receiver's Notes soup at once (the In Box's
  AutoFunction at 0x4b3ef5 calls the Notes app's `AutoPutaway`, as
  autoPutawayEnabled asks) - the ROM's behaviour, governed by the Beam
  preference dontAutoPutAway.

## 2026-09-30: the IrDA stack

- `comms/irda/` (603569db): `TIrDATool`/`TIrDAService` ('irda') over the
  ROM's whole IrDA engine - `TIrStream` and its event blocks (a request's
  block travels down the layers and comes back as its own answer, code +
  1; the glue's run queue drives every state machine from the tool's
  task), `TIrGlue`, the IAS client and server (GetValueByClass on LSAP
  0), `TLSAPConn`, `TIrLMP`, `TIrLAPConn`, and `TIrLAP` - XID slot
  discovery, SNRM/UA with the `TIrQOS` negotiation, the NRM windows,
  RR/RNR/REJ/FRMR, DISC/RD/UA/DM, TEST, the timers.
  `NEWTON_TRACE_IRDA` prints every event and frame; ctest `comms.IrDA`:
  discovery, the IAS lookup, connection, data both ways (a 1500-byte
  message in three I frames), disconnect.
- ROM bugs kept: a second address conflict leaves a discovery hanging; a
  failed put allocation releases requests[n]..[1] (off by one);
  `LSAPLookupStart` releases a block its client still holds when the IAS
  server fails.  Host bug found: device addresses are 32-bit, so the
  ROM's -1 is `kIrAllDevices` (0xffffffff) - with a 64-bit ULong the
  listener ignored every XID.

## 2026-09-30: the ROM source tree's leftovers

- Every function decompiles (a673717e): a greedy solver names a loop's
  hidden locals where the search gave up, names kept under the lexer's
  253-character limit; Cardfile 336 and Connection 417 all source.
- The REx header's checksum is read by nothing in the ROM (28263829):
  `TestForREx` checks only the signatures and id, and the ROM's own
  check for a changed ROM is `CalculateROMREXCheckSums`, computed fresh.
- The 12 IMA sounds are WAV, compressed back byte-identically on build by
  the reconstruction of the ROM's own codec (`newtonscript --ima-expand`/
  `--ima-compress`, 9587ffa4).
- Tables as text where they have structure, each checked against its
  bytes (d4be0a8b): `tonescore` (the 36 touch-tone scores), `shorts`,
  `fixed`, `hexfile` (keyboard layouts, 256-entry tables); 32 files
  stay binary, reasons in `docs/rom-free/README.md`.
- An edit that reorders slots, inserts array elements or renames slots
  needs no `layout.tsv` change (e62f2576; ctest
  `host.ROMSourceEditMoved`).
- The committed `romsrc/` was brought up to date with these by copying
  the changed files from a fresh extraction, still byte-identical
  (b6d918cb, 0347d4ce, df2156f7) - safe only while nobody has edited it
  by hand.

## 2026-09-30: the ROM source tree committed as `romsrc/`

- The owner's decision: the tree is committed and is now the source,
  extracted once, never re-extracted over (ac045347: the tree and
  `.gitattributes` only - its .bin/.png/.wav/.pict/.ttf/.pkg binary,
  .ns/.tsv/.md LF text).
- The build switched to it (50774437): target `romsrc` and fixture
  `host.ROMSourceBuild` build `<build>/romsrc-objects.bin` from it with
  `--relayout`; the no-image boots and the edit tests use it (the edit
  tests on a copy); `host.ROMSourceRoundTrip` tests the extractor into
  `<build>/romsrc-extracted` (target `romsrc-extract`);
  `host.ROMSourceCommitted` reports whether `romsrc/` still builds the
  ROM byte for byte; the builder's working files go to a temporary
  directory, never into the tree.  209 of 209 ctests.

## 2026-09-30: the IR probe

- `comms/IrProbeTool.h` (061d413): `TIrProbeTool` and 'pkir' - connecting,
  four IrDA TEST frames carrying 'prbe' each waited for a tenth of a
  second, then the port switched to ASK for a Sharp offer of protocols
  0xf (or every third time an ENQ), round and round for about two
  minutes; listening, half-second ticks in auto-receive, a TEST frame
  echoed and answered IrDA (8), two ENQs or an offer without IrDA's bit
  answered Sharp (7); the answer read back as 'irpt'.
- `comms/IrSIR.h`: IrDA's SIR framing (`TIrSIR`: BOFs, escapes, the
  CRC-16 low byte first, EOF; the 0xf0b8 residue checked on receive) and
  `TIrLAPPutBuffer`.  ctest `comms.IrProbe`: probe to probe answers IrDA,
  probe to a Sharp listener answers Sharp IR.

## 2026-09-30: a Note beamed from one host to another

- `comms/Beamer.h` (e92e34c): `TBeamer` whole (0x3b6f0-0x3dc10) - Open
  (the probe, then 'irda' or 'slir', or the service zapCommToolId
  names), the endpoint opened in a forked world with the ROM's options,
  the pipe (framed for 'slir'), the item count, a header frame, the
  receiver's room answer and the item as NSOF with the progress gauge;
  1.x Newtons through 'oneO form; the Sharp Wizard path failing -50006
  as on the device (its translators come in a package); `ZapSend`/
  `ZapReceive`/`ZapCancel` as the Beam transport's natives;
  `NEWTON_TRACE_BEAM`.
- `tools/host/twonewtons.py` runs two newtons with their IR ports facing
  each other, each with its own script and expectations; ctest
  `host.NewtonBeam`: `beam-send.ns` sends a note from the Out Box,
  `beam-receive.ns` finds it in the In Box, about 4 s.

## 2026-09-30: the ROM source tree - debug names, new frames, a growing extension

- ListView's functions (8a6d860): its `'dbg1` DebuggerInfo is what the
  compiler makes when variable names are kept (the ROM's own
  `dbgNoVarNames` switch), so they decompile with their real names and
  compile back with names kept (266 of 266); a nested function that
  closes over nothing was still compiled inside its parent in that debug
  build, and is written inline.
- The builder makes maps and symbols for new or changed frames
  (0d28cdb); `edit-test` adds a frame and `host.ROMSourceEditValue` reads
  it back.
- The extension's parts relaid out (f2977b4), the package directories,
  the REx config table and `'fexp` kept in step; `edit-test` grows a
  Cardfile string, every later package moves, and the boot still draws
  the same screen.  The REx header's checksum (0x98e6) is left as it was:
  no obvious sum reproduces it and the host does not check it.

## 2026-09-30: the ROM source tree made editable

- The extension's frames packages from source (ef83602): each package's
  frames part is a tree of its own (`rex/<Package>/`), the package bytes
  around it kept as `.head.bin`/`.tail.bin`, the extension still
  byte-identical.  Every function is verified before it is written as
  source - each tree's functions compiled in one `newtonscript` run and
  compared with the ROM's; those that differ stay bytecode with the
  reason in `bytecode.tsv` (all 5480 of the object area pass; 1071 of the
  packages' do; ListView's 266 carry `DebuggerInfo`, which the compiler
  does not make).  The layout records the parts' alignment, their 0xbf
  padding and ListView's 0xbeacebad gap words.
- `build --relayout` (4e96f01) lays objects out afresh at the sizes they
  now have; rather than generating `ROMConstants.h` (which would tie a
  host build to one layout), the object file (version 3) carries a table
  of moved objects that the importer looks the C++ constants up in
  (`MovedRef`), so an edited tree needs a new object file but no host
  rebuild.  `romsrc.py edit-test` lengthens one string, moving 43566
  objects, and the boot still draws the Setup screen pixel for pixel
  (ctests `host.ROMSourceEdit`, `host.NewtonEditedSameScreen`).
- Files grouped by the root object that dominates them, function files
  named by the path that holds them (`functions/Rbuiltinfunctions.
  AddAlarm.ns`) (5b5d319).  205 of 205 ctests.

## 2026-09-30: Sharp IR

- `comms/SharpIRTool.h` (0f017f4): `TSharpIRTool`, serv 'slir' - the
  packets (lead-in, control, negotiation, data of up to 0x200 bytes with
  a checksum), the ROM's 32-event `NextState` machine, the negotiation,
  ENQ/SYN before each data packet with ACK/NAK and three tries, the two
  timers as delayed messages to the tool's own port; `TIRService`,
  `RegisterIRCommServices`; the five slow-IR options (the ROM's
  `TCMOSlowIRBitBang` defaults bit-banging off where the DDK comment says
  on).  The host IR chip reports the Voyager IR channel's features -
  without `kSerFeatureTxConfigNeeded` the tools never configured for
  output, so half duplex never applied.  `NEWTON_TRACE_IR` prints every
  byte a chip sends, hears or loses.
- ctest `comms.SharpIR`: two chips negotiate protocol 4
  (irUsingSeniorIR, the MP2x00's codename) at 19200 and move stream and
  framed data.  ROM behaviour: one counter numbers packets both ways, so
  after an unframed put a reply is refused as out of sequence (-38006
  after the retries); only a frame's last packet (0xffff) resets both
  ends - harmless for the beamer, which only sends frames.

## 2026-09-30: beaming planned, and the host's IR chip

- Sized with the new `analysis/classsizes.py build/MP2x00US REGEX` (each
  class's code size and how much is reconstructed) (f5580c9): the Beam
  transport (ROM NewtonScript) -> `ZapSend`/`ZapReceive` -> `TBeamer` ->
  the probe 'pkir' (`TIrProbeTool`, switching between IrDA SIR and
  Sharp's ASK) -> 'irda' (about 32 KB) between two 2.1s, or 'slir'
  (`TSharpIRTool`) for older Newtons; the preference zapCommToolId names
  the service outright.  Every tool is a `TAsyncSerTool` on the chip at
  'infr', switched by the 'irlk' option.
- `hal/host/HostIRChip` (1731dbd): a TSerialChip at 'infr' over a TCP
  connection to another host (`newton --ir-peer listen:PORT` /
  `HOST:PORT`); each byte crosses with its modulation, a receiver hearing
  only what 'irlk' tunes it to (both in auto-receive, its status saying
  which came last - the Voyager's behaviour, which the probe relies on);
  half duplex; bytes arriving while it is off are lost.  ctest
  `hal.HostIRChip` runs two chips in one process.

## 2026-09-30: the OS boots with no ROM image

- `tools.PNG` runs the PNG reader's tests (459ac8e).  No bytecode is left
  in the source tree: of the "362 nested functions" only 10 were; the
  rest were bytecode equal functions share (the ROM's build stored equal
  objects once), now written `same("<path>")` - the object the compiled
  function holds at that path - 721 definitions (2915178).
- Step 3 planned in `docs/rom-free/README.md` (bec7fbd): a table of what
  still reads the image.  `romsrc.py build -o` writes one object file -
  the object area, the magic-pointer table (`magic.tsv`) and the
  recognisers' 40 lexicons (`lexicons/`: the first boot without an image
  crashed in `ReplaceDictionary` without them); `ImportBuiltObjects`
  loads it and `ROMBytesAt(address, length)` answers ROM bytes from
  either the image or the file; `newton --objects <file>` (or `--rom`,
  which recognises it).  ctest `host.NewtonNoROM` builds the file and
  boots with no image anywhere to be found, to the Notepad (a55bc3a).
  The `--rom` boot shows the Setup assistant instead, Setup being one of
  the ROM extension's packages, which do not come from the tree yet.
- The ROM extension in the tree (e7d945c): `rex/` and `rex.tsv` cut at
  every config entry and package - the header, `dio`/`gpio`/`ralc`, the
  ten packages as `.pkg` files (kept as the ROM's bytes first: a ROM
  package's frames parts hold ROM addresses; reasons in
  `docs/rom-free/README.md`), `ptpt`/`glpt`/`fexp`/`jump` - carried as a
  block of the object file and found through `ROMRegion`/`ROMAddressOf`.
  The no-image boot now draws the Setup Welcome pixel for pixel as the
  `--rom` boot does (ctest `host.NewtonNoROMSameScreen`,
  `tools/host/samescreen.py`); 201 of 201 ctests.

## 2026-09-30: V.42bis

- `comms/V42bis.cpp` (d091c7e): the ROM's BTLZ coder (`BTEncode`,
  `BTFlush`, `BTDecode`, the bit packing, `SendCodeword`, the inits) over
  its own 0x39d4-byte block, the node arrays big-endian halfwords at the
  ROM's offsets; MNP's class-4 hooks call it.  Cross-checked against
  `tools/dock/v42bis.py`, written from the ITU recommendation
  independently of the ROM - both directions, transparent, compressed
  and alternating, N2 512/1024/2048 with N7 6/32/250 (ctests
  `comms.V42bis`, `comms.V42bisRoundTrip`).  The ROM's arrays hold 2048
  nodes, so N2 is at most 1024 each way with both directions compressed.
- `mnp.py --v42bis` accepts it as the desktop; `comms.MNPV42bis` echoes
  over a live link with (1024, 32) negotiated (9518bc6).
- The 1-in-36 in-session 'lpkg' hang did not recur in 102 stressed copies
  after 12e55a2's host-runtime fixes, whose symptom it matched.

## 2026-09-30: the keyboard passthrough - the docker complete

- The keyboard passthrough (3f611a7): `DoKeyboardPassthrough`,
  `KeyboardProcessCommand`, `ConnDoKeyboardPassthrough`; the desktop's
  'kbds'/'kbdc' keys posted to the key view until 'opdn'/'opca'.  ROM bug
  kept: the last idle's time is never moved on, so once the interval has
  passed `IdleConnection` runs after every command.
  `host.NewtonDockSession` ends by typing "hello!" from the desktop into
  a paragraph.
- 'gpat' (`WritePatches` over `SizeOfPatches`, 78687be).  The docker now
  answers every desktop command but 'rpat'.

## 2026-09-30: the host runtime's livelock - two host bugs, the ROM innocent

- `TPMIterator::Init`, `TULockingSemaphore` and `TForkWorld`'s mutex all
  match the ROM (Init releases the world mutex before it waits).  Two
  host bugs made the livelock (12e55a2):
- `SemaphoreOpGlue` retried a semaphore op whenever its exit switched
  tasks, where SWIBoot (0x003adf04) retries only one that blocked
  (`gCurrentTask` nil after `DoSemaphoreOp`): a `TULockingSemaphore`
  wake-up was raised twice and a satisfied wait waited again, so the fork
  mutex and `gPackageSemaphore` handed a wake-up round for ever.
- `IsSuperMode` (0x00394410) answered false at interrupt level, so the
  serial receive interrupt's `GetGlobalTime` (`TSerTool::IHRequest`) made
  a real system call that overwrote the interrupted task's r1/r2 - a
  fork's start `Receive` - so `Fork` failed with -10048 and `GetPackages`
  threw on a changed interpreter.  `gHostInterruptLevel` now makes
  `IsSuperMode` true in `HostDeliverInterrupts`, and a system call from a
  handler is refused.
- `test_HostRuntime`, `test_HostInterruptSources`, ctest
  `host.NewtonDockGetPackages` (a script polling `GetPackages()` every
  tick while the docker loads a package): 54 of 54 copies beside 8 hogs;
  before, an unstressed crash 2 in 3 and a hang 2 in 6.
- Seen on the way and checked: a fork whose start fails deletes its
  family's mutex - a ROM bug, kept (1bbe3cd).  `ForkInit` (0x000cb2e4)
  clears only `fIsMain`; `TForkWorld::TaskDestructor` (0x000cb618) picks
  the main world's branch by `fRunsMain`, still true in a fork until it
  forks; `TUTaskWorld::TaskEntry` (0x0025ba94) destructs on every
  failure, so such a fork runs `MainDestructor`, deletes the parent's
  shared `fMutex` and decrements `fWorlds` it never added.

## 2026-09-30: 1.x entries, selective restore, and a desktop's slip

- `ConvertEntry` (a 1.x entry through its owner's conversion frame; ROM
  quirk kept: with no owner `fOwnerApp` becomes 0, not nil, so it is never
  looked up again) and `IsDuplicateEntry` (the first plain index less the
  slots the ROM's table at 0x00472c1d lists, else `_uniqueID`, with
  `ConnectionDupValidTest`) (0dc5104).
- The Connection app's own read/write natives (`WriteCommand`,
  `ReadCommand`, `ProcessBuiltinCommand`, ...) (17c9f78); ROM bug kept:
  `WriteCommandHeader`'s "no data" flag is the integer 0, not nil.  A
  desktop's 'dslp' slip is found and tapped headless and answers 'slrs'
  1; while it is up its idle's 'helo' waits 10 s on the busy docker and
  is dropped, as the ROM does.
- Test fix (fb877ba): the controllable clock jumped to the next deadline
  whenever every task was idle and ran MNP's timers out while the Python
  peer was still sending; the serial and MNP tests use the real clock.

## 2026-09-30: the rest of the docker's commands

- Package restore and removal ('rpkg' `DoRestorePackage`, 'rmvp'
  `DoRemovePackage`), 'ginh' (the inheritance frame), 'gsyn' (the sync
  options), 'test'/'rtst' echoes, remote function calls ('cgfn'/'crmf'),
  the Connection app's slips ('dslp', 'islp', 'gpwd'), the desktop's
  protocol extensions ('pext'/'rpex'), 'ress' (a0966b4).  ROM bug kept:
  `TestRefMessage` writes its header twice.  `dock.py --session` echoes,
  reads the 13-class inheritance frame and the sync options, calls
  Max(3, 7) on the Newton, installs and removes an extension, and removes
  and reloads a package.

## 2026-09-30: the docker backs up soups and lists packages

- 'csop' (CreateSoup), 'cdsp' (CreateSoupFromSoupDef, swallowing
  evt.ex.nosoupdef as the ROM does), 'snds' (SendSoup), 'bksp'
  (BackupSoup: entries changed since the desktop's 'stme' sent whole,
  the rest as runs in 'bids' - an id, minus the count after it, 0x8000
  to end, a 'base' first when an id does not fit a short; 'ndir' for an
  unchanged soup), `GetBackupCursor`, `CheckCancel`, `FinishSequence`
  (cc1b30a).  ROM bugs kept (`docs/curiosities.md`, "A backup that cannot
  be cancelled"): `CheckCancel` returns when 90 ticks have already passed
  since its last look, so after the first 1.5 s of uptime it never looks
  again; `BackupSoup` never keeps the base its 'base' announces.
- 'gpin' (GetPackageInfo over `TPMIterator`, answering 'pinf') (1235149).
  `dock.py --session` makes a soup, adds three entries, backs it up
  (bids [0, -2, 0x8000]), sends it, removes it, and finds the package it
  loaded in the list.

## 2026-09-30: the docker's cursor and entry commands

- `TCursorArray`; the cursor commands ('qury', 'cmap', 'goto', 'crsr',
  'move', 'next', 'prev', 'rset', 'rend', 'cnt ', 'whch', 'cfre'), the
  entry commands ('rete', 'rcen', 'adde', 'auni', 'cent', 'dele',
  'esou'/'dsou', 'sver') and the id lists ('gids', 'gcid'), over
  `GetEntryFromID`, `WriteEntry`, `ReplaceEntryContents`,
  `AddChangedSoup`, `ShouldBackupEntry`, `GetSoupIDCount` (8a2af65).  ROM
  bug kept: when `WriteSoupIDs` fails to add an id it adds it again to
  get the error it throws.  `host.NewtonDockSession` opens a cursor over
  Notes, adds an entry, finds its id, reads, changes and deletes it.

## 2026-09-30: the docker's store and soup commands

- Stores ('gsto', 'ssto'/'ssgn', 'sdef', 'gdfs', 'ssig', 'ssna') and
  soups ('gets', 'ssou'/'ssgi', 'gsin'/'cinf', 'gind'/'cidx', 'sinf',
  'ssos'); ROM bug kept: a 'SystemScratch soup is "forgotten" by storing
  0 rather than nil.  `tools/dock/nsof.py` is NSOF for the desktop side;
  `NEWTON_TRACE_DOCK` traces the commands each way.  `dock.py --session`
  lists the stores, makes Internal current with its 15 soups and reads
  the System soup's info (9bd3a3a).
- The `host.NewtonDockSession` flake (2 in about 40 under stress.py),
  traced with per-thread stack samples: a livelock between the script's
  task in `TPMIterator::Init`'s semaphore (the demo polling
  `GetPackages()`) and the docker's forked world in
  `TForkWorld::AcquireMutex`.  `dock.ns` now waits on the docker's own
  slots (5d366de): 6 of 6 copies in each of 5 rounds beside 8 hogs.  The
  lock order itself is still open.

## 2026-09-30: drawing speed

- Measured first: `src/host/demo/drawbench.ns` (Names, Dates, Extras
  opened and closed, the Notepad scrolled and redrawn, 20 rounds; the
  processor time from a new `HostCPUTime()`) and `tools/host/profile.py
  <pid>`, a sampling profiler: `RgnBlt` 77% inclusive, the per-pixel
  helpers about 60% self; then the host display driver's `Blit`.
- The oracle: the pixel-at-a-time blitter kept as `BlitPixelsSlow`
  (`NEWTON_QD_SLOW=1`, `SetQDSlowBlitter`); ctest `qd.Blitter` draws 240
  random scenes both ways (every verb, the 16 pen modes, patterns,
  `CopyBits` at depths 1/2/4/8 with masks, overlapping and stretched,
  `ScrollRect`, complex clips, maps with edges mid-byte) and compares
  every byte - a planted one-pixel error shows in 65 scenes;
  `test_Screen` checks the display driver the same way.
- `BlitPixelsFast` (DEVIATION, performance): the maps and pattern looked
  up once per call, pattern rows once per row, one combined visibility
  row, byte moves for aligned same-depth copies, packed copy rows; the
  display driver's `Blit` through tables of grays (9b68f08, bb12f7d,
  ab8af8f, 2067c01, b95cb60, ea502ea).  drawbench: 11.0 s to 3.4 s of
  processor (1.6 s in an optimised build); a Notepad redraw 18.9 ms to
  6 ms.

## 2026-09-30: the docking session, and the ROM's DES

- `utility/DES.h` (628b899): the ROM's DES - the key schedule, the
  rounds, the nonce calls, `DESCharToKey` - tables from romtable.py.  It
  is not standard DES (each key half is shifted one bit left before PC1,
  so the published vectors do not match), so `utility.DES` checks it
  against the ROM's own `DESEncodeNonce`/`DESDecodeNonce`/`DESCharToKey`
  run on the ARM interpreter over the ROM image (26 blocks, 7
  passwords).  `DESCharToKey`'s OR quirk kept as a ROM BUG
  (`docs/curiosities.md`).  `tools/dock/newtondes.py` is the desktop's
  copy.
- The docking session (3afbdea): Connect's 'dock' branch - 'dock',
  'name', 'dinf'/'ninf', 'wicn', 'stim', the password exchange - and
  `DoConnection`'s command loop through `ProcessCommand`: packages,
  session kinds ('ssyn', 'rrst', 'rins', 'dsnc'), 'stme', 'stim',
  'wicn', 'opca'/'opdn', 'cvbo', 'helo', 'dres', 'unkn', protocol
  extensions; `ReadRef`/`WriteRef` over NSOF; `ConnRetryPassword`,
  `DESCreatePasswordKey`.  `dock.py --session` docks with the empty
  password and loads a package in the session (ctest
  `host.NewtonDockSession`).

## 2026-09-30: the desktop connection loads a package end to end

- `host.NewtonDock` (01c53cf): `dock.ns` sets the Connection app's
  autodock address to serial and calls `autodock()`; the Newton connects
  over MNP to `tools/dock/dock.py` and says 'rtdk' (version 9), dock.py
  answers 'lpkg' with a fixture package, the docker installs it and says
  'dres' 0, dock.py says 'disc'.
- Missing and added: `ConnBuildStoreFrame` (with `StoreGetPasswordKey`
  in `stores/Soups`).
- Bug found: an option a script makes is laid out as the device's
  big-endian 4-byte words, but the host's option classes have
  pointer-sized fields, so the serial tool set its speed to 4 out of the
  app's 'siop' (0.4 bytes a second).  `comms/HostOptionLayouts.h`
  (DEVIATION) rewrites 'siop', 'mnpn', 'mnpc' and 'eter' word by word
  both ways.  Hardened (914d086): the table lists all 26 option classes
  the host constructs (pointer-sized words signed and unsigned, 4-byte
  enums, halfwords, bytes; the host TCP tool's 'itrs'/'ilpt' passed on as
  they are), a script's option with data no table lists is reported once
  per label on stderr (`NEWTON_QUIET_OPTIONS` hushes labels), and ctest
  `comms.HostOptionLayouts` fails on an unlisted class or a field list
  that does not add up to the class's host size.
- `CommManager.cpp`'s `SetReply(0xC)` calls are `sizeof(TAESystemEvent)`
  (60335af); `romsizes.py` reports `src/comms` clean with `--lp64`.

## 2026-09-30: an audit of ROM byte counts sizing host structs

- `analysis/romsizes.py` (`tests/test_romsizes.py`) scans for hex sizes
  given to message, allocation and copy calls - also through a variable
  assigned a few lines before, as the sound bug hid - and says whether the
  struct holds anything pointer-sized on the host (`ULong`, `Long`,
  `Ref`, pointers, `TObjectId`, `ArrayIndex`, `TRegister`): WIDE, NARROW
  or UNKNOWN; `--lp64` treats `long` as wide as on Linux (9fb8718).  On
  the tree before 6aef861 it flags exactly the sound server's three reply
  sizes; outside `src/comms` it finds nothing else, and the 23 unknown
  hits are byte buffers or all-byte records.  In `src/comms`,
  `CommManager.cpp`'s three `SetReply(0xC, event)` are right on Windows
  but would truncate on an LP64 host.
- The recording node's rubbish error on Stop is the ROM's: `InitNode`
  (0x1e494c) allocates the codec state and never sets its error and
  state words - commented and kept.

## 2026-09-30: the desktop connection, layer 4 part 1 - TDocker

- `TEzEndpointPipe` and the modem navigator (4567d7a); `CBufferPipe::Init`
  now makes both segments - a virtual call had cut its decompile short
  (118b200).
- `comms/Docker.h` (library `comms_dock`): `TEzPipeProtocol` (the 'newt'
  'dock' headers), `TDocker` - Connect ('rtdk' 9, then 'lpkg'),
  DoConnection (forking the world), the package loader's session
  (`CompatabilityHacks`/`ReadPackage`: `SuckPackageThruPipe`, 'dres',
  then 'lpkg' or 'disc'), stopping and aborting - `TDockerDynArray` and
  the protocol extensions (the Connection app's `SetupEndpoint` wanted
  `InstallAnyProtocolExtension`), and 13 `Conn*` natives
  (`RegisterDockerNatives`) (cdb5e39).
- `newton --serial-port port|none` (default 3679, loopback only) starts
  the FIQ timer, the host serial chip and the serial and MNP services at
  boot (ctest `host.NewtonDocker`).

## 2026-09-30: the Sound Recorder plays to the end

- The recorder stayed at "Playing...": the sound server answered a pause
  or stop with the ROM's 0x20/0x14 bytes, which on the host (a
  pointer-sized `ULong`) stopped short of the node id and position, so
  Stop reported nothing played, the recorder never cut its recording to
  length (`SetRecordingLength`), and Play played the whole 64K buffer.
  The sizes now come from the struct (`kSndShortReplySize`,
  `kSndNodeReplySize`, DEVIATION; 6aef861).  `demo/recorder.ns` waits for
  the playback to finish; `host.NewtonRecorder` requires "finished: Ready
  to play or record".

## 2026-09-30: the host demos wait on conditions

- `tools/host/stress.py` (`tools/host/README.md`): copies of one ctest
  at once, each with its own store (`--test NAME --copies N`), or whole
  `ctest -j` rounds (`--suite --rounds N`), beside CPU hogs (`--hogs N`)
  (0bceee7).
- `src/host/demo/common.ns`, loaded with `HostInclude()`: `tapAt`,
  `typeKey`, `waitFor` (a condition asked four times a second, "waited in
  vain" past its limit), `walkSetup` (the Setup assistant by page).  The
  handwriting demos wait for the page to show what was written (55-76 s
  down to 6-14 s), the rest wait on what they need and end with
  `HostQuit()`; every converted ctest requires "<name>: done" and fails
  on "waited in vain" (4db2463, 138a9b9).
- The host tests' summed time 896 s to 359 s; a full parallel ctest
  131-165 s to 104-119 s, 186 of 186; each converted test 6 of 6 copies
  beside 8 hogs.

## 2026-09-30: the desktop connection, layer 3 - MNP

- `TFramedAsyncSerTool` ('fser: DLE framing and CRC; 8118a0e), `TMNP`
  with class 5 compression and `TMNPService` ('mnps; 350a8c4, b879aa5),
  read from the disassembly because the decompiler merges tail-called
  functions (RetransTimeOut ran into XmitLT).  `tools/dock/mnp.py` is the
  desktop end of the link; ctests `comms.MNP`, `comms.MNPLongHeaders`,
  `comms.MNPClass5` - the Newton originates the link, as when docking.
- ROM quirks kept: a failed CCB allocation answers noErr; a speed
  sub-parameter other than 1 is counted but not read past; XmitLD sends
  the whole 10-byte LA buffer for the 8-byte LD; the kill flags in
  KillPutComplete/KillGetComplete look swapped; the current 'mdct option
  is read without checking there is one; a virtual Disconnect nothing
  calls.  V.42bis's coder (0x25ce5c-0x25dab0) NOT YET.

## 2026-09-30: the flaky host tests pinned down

- Reproduced under load (copies in parallel beside CPU hogs); the
  scripts now wait on conditions (aec6fe3, 7591dc0).
  `host.NewtonThirdPartyPackages` checked the drawer before the queued
  packages (and their fonts) had arrived; `host.NewtonInetSetup` started
  before the NIE's service registry and HostLink's host network were
  there; `host.NewtonThirdPartyApps` hung in NewtHack.
- NewtHack's hang is faithful: unregistered Mahjongg seeds the one
  random-number generator with `TimeInSeconds() mod 5 + 1`;
  `TimeInSeconds` (seconds from 1993 in a 30-bit integer) overflowed in
  2010 and is negative, so one second in five the seed is 0, with which
  every `Random(lo, hi)` answers `lo` - and NewtHack searches for ever for
  a free square for its second staircase.  A real Newton set to today
  would do the same (`docs/curiosities.md`).  The test gives NewtHack a
  seed of its own.
- `HostQuit()` ends a run from its script, so `--headless` seconds are a
  limit: ThirdPartyPackages 56 s to 18 s, ThirdPartyApps 154 s to 110 s.
  A full parallel ctest: 182 of 182.

## 2026-09-30: the desktop connection, layer 2 - the serial tools

- `hal/FIQTimer.h` (the machine's fast timers) and `TDelayTimer`
  (`hal/DelayTimer.cpp`) (ctest `hal.FIQTimer`, 53914aa).
- The name server's resource arbitration, NOT YET until now - an
  endpoint's Bind failed with -10005 without it (c8e8e5d).
- `comms/SerialTool.h` (library `comms_serial`): `TSerTool` claims a chip
  through the registry and the arbitration, `TAsyncSerTool` streams bytes
  between two TCircleBufs from the chip's interrupts, the 'aser service
  is `TAsyncService`; an 'aser endpoint echoes "hello" and 1000 bytes
  through the socket (ctest `comms.SerialTool`, 9f2d072).  ROM bugs kept:
  `SetEventEnables` clears fIntMask's output-done and input-ready bits
  (masking with the event mask, not its complement);
  `SetOutputFlowControl` sets the modem interrupts before recording the
  new hardware-flow setting; the first byte of a repeated break-framed
  frame is not masked to the data bits.
- Host bug found: the host chip must deliver a desktop's bytes at the
  line's speed - a socket's whole window at once overran the tool's
  512-byte buffer (a soft overrun, -18003).

## 2026-09-29: the desktop connection, layer 1 - the serial chip over TCP

- `hal/HALSerialChip.h`: the DDK's TSerialChip and PSerialChipRegistry
  re-expressed, methods virtual in the ROM's dispatch order (not named
  SerialChip.h: the V1 DDK header would shadow it); `SerialChipRegistry.cpp`
  is the ROM's PTheSerChipRegistry - eight slots, ids 0x80 + slot, a
  chip named to the name server by its location, a service's default
  chip in the config server's "DefHWLoc", FindByOption over a
  TCMOSerialChipSpec, the ROM's no-op 'slot loop in FindByLocation kept.
- `hal/host/HostSerialChip.h`: `THostSerialChip` (DEVIATION, hardware),
  the external port 'extr' whose wire is a TCP socket on localhost:3679
  (Einstein's): a desktop connecting raises DCD/DSR/CTS, bytes reach the
  tool's receive interrupt and leave from its transmit-empty interrupt,
  polled every 5 ms as a host interrupt source (ctest
  `hal.HostSerialChip`, 371b3ae).

## 2026-09-29: comms, round 11 - an NIE bug confirmed on its own ARM

- `NEWTON_NIE_ON_CPU` leaves the NIE's natives unregistered, so a whole
  host session runs its own ARM on the armcpu fallback.  With it the -48404
  thrown at link shutdown was confirmed as the NIE's own: `DoEvent_Loop`
  computes level - 1 on a context its own action (the link manager's
  CleanUp, disposing the manager) has emptied - commented NIE BUG (kept)
  in `ProtoFSMLoop.cpp` (163e4c6).
- The Host network setup's pages drop Ethernet's card picker
  (`viewsToDisplay`'s 'cardData and the new-setup pages' card child)
  (efb3e2b).

## 2026-09-29: comms, round 10 - Internet Setup and protoEndpointFSM

- Internet Setup driven on the host (`src/host/demo/inetsetup.ns`, ctest
  `host.NewtonInetSetup`, 5bac85a): "Host network" is listed and opens,
  and New / Generic Setup / "Connect using" offers it beside LocalTalk,
  Modem and Serial.  What stopped it, all in `HostLink.ns`: a
  LogicalService needs an `excludeFrom`; a setup is shown and edited
  through the data definition its class names, one per physical layer,
  offered only when its `DependenciesLoaded` is true - so the host
  registers `HostNetwork:host`, proto'd from Ethernet's, with Ethernet's
  view definitions.
- protoEndpointFSM (`src/host/demo/inetfsm.ns`, ctest
  `host.NewtonInetFSM`, 037b2a3), taken from the 'Inet Protos:NIE unit:
  connects, echoes "hello", disconnects.  A machine made from the proto
  needs its own `fsm_private_states`, even empty.  The host link services
  now answer Release, so the link manager's CleanUp completes and goes
  back to idle.

## 2026-09-29: the third-party apps used for real

- `src/host/demo/thirdparty-apps.ns` (ctest `host.NewtonThirdPartyApps`)
  opens each of RPNcalc, Daleks, Mahjongg, NewtHack and Register from its
  Extras icon, taps it and takes a snapshot: 7 Enter 8 + shows 15; the
  Dalek player steps; Mahjongg is let turn the screen and a matching pair
  is taken off (144 to 142), its natives on armcpu; NewtHack plays two
  steps, its native running each turn; Register's Program picker picks
  (dcae3e0).
- Found and fixed: `TView::RecalcBounds` was transcribed inverted (ROM
  0x2652a4: the bounds are set when viewBounds converts and the
  children recalculated always), so a view moved by `SyncView` left its
  children behind - Mahjongg's tiles-remaining digits after rotating
  (8c8184a).
- armcpu answers every ROM entry the fixtures' native code reaches that
  the host has (123, 5dde85e), `TranslateException` over the ARM
  exception record and `StrEndsWith` (8a40206); not answered: `Debugger`,
  `EnableFramesFunctionProfiling`, `GetGlobals`, `PublicFiller_236`.

## 2026-09-29: comms, round 9 - the NIE end to end over the host's own link

- The oracle is counted: all seven groups of `test_NIEProtoFSM` agree
  run on the NIE's own ARM code under armcpu (which now answers the 123
  ROM entries the fixtures' native code reaches, 5dde85e) and on the
  re-expressions (2ae6d73).  The one failure left was the test's: a host
  stand-in registered at an unused offset, which the ARM code jumped
  straight into.
- `ictl`: `THostLinkService`/`THostLinkTool` answer every
  connection-control option with success (DEVIATION, 225fb90).
- The host's link (6cca01f, DEVIATION): `comms/host/HostLink.ns`,
  compiled in by `src/cmake/EmbedText.cmake` and started from newton's
  PreMain hook, watches the "Packages" soup and, once the NIE's
  `InetServices:NIE` exists, registers a physical and a link service
  answering each step the link machine asks for, and a "Host network"
  setup in the "Internet Setups" soup (made the default link when there
  is none).  It never calls `InetStartUp` - doing so from the watcher
  hung package removal.
- `src/host/demo/inet.ns` (ctest `host.NewtonInet`): `InetGrabLink` runs
  the NIE's link state machine to connected, `DNSGetAddressFromName
  ("localhost")` answers 127.0.0.1, a TCP endpoint on the link echoes
  "hello", and `InetReleaseLink`/`InetDisconnectLink` take it back down.
- Found: the NIE's globals appear only after a "Packages" soup change
  and a procrastinated call, so a script must wait for them;
  `NEWTON_TRACE_PACKAGE_NATIVES` lists a package native with nothing
  registered (f1aa955).

## 2026-09-29: comms, round 8 - the whole protoFSM, and an oracle

- The printer `f` (0x14649, 15 KB) re-expressed, so all 19 of the NIE's
  protoFSM natives are host code: `ObjectToString({a: 1, b: "x", c: [2,
  'y], d: {_parent: 'hidden}, e: nil})` gives `{<1> a: +1, b: "x", c:
  [<2> +2, 'y], d: {<3> _parent: <ignored>}, e: nil}` - the + on a
  positive number is the NIE's own (e5047e4).
- `test_NIEProtoFSM` runs every group of checks first on the package's
  own ARM code through `src/armcpu` with nothing registered, then on the
  re-expressions: the queue group agrees; the others wait on three ROM
  entries armcpu does not answer yet, so that phase is advisory
  (05c78ba).
- Host bug fixed on the way: `NEWTON_TRACE_EXCEPTIONS` recursed without
  end when printing the thrown data itself threw (ab28683).

## 2026-09-29: the ARM interpreter, round 2

- NewtHack's five-argument native runs (ctest `armcpu.NewtHack`,
  8589946).
- Objects in a code binary come back as host objects (symbols interned,
  strings' UniChars swapped, binaries, arrays' slots translated; frames
  NOT YET), and exception data crosses both ways (a ref as a RefVar, a
  message as a C string copy; an uncaught ARM throw goes out to the host
  translated back; `ThrowRefException` handled by name) - ctest
  `armcpu.PackageNativeCPU` over hand-assembled natives (2804e11).
- `NativeEntry` on another code binary's function answers a callback
  that runs it as the interpreter would - its host re-expression, else a
  new CPU run (7e8021a).
- Survey of the 19 fixture packages (`docs/armcpu/README.md`, "Which
  fixtures have native code"): NTK native code in Mahjongg, NewtHack and
  the NIE's `modmsup` and `inetenbl` (all the same NTK runtime: 128
  stubs, code from 0x29ec); protocol parts only in the NIE's packages
  (5773a7b).

## 2026-09-29: comms, round 7 - the NIE's protoFSM re-expressed

- `analysis/ntknative.py <pkg> <holder> --rom BUILD` lists an NTK
  native-compiled NewtonScript function as statements (which RefHandle
  each stack slot holds, the literals, the outgoing arguments;
  NativeEntry's fast path elided) (4a8227b, 658ef2a).
- `src/thirdparty/nie/`: 18 of the protoFSM's 19 native functions
  re-expressed and registered through `frames/PackageNatives.h` - the
  queue, `DoEvent`/`DoUniqueEvent`/`DoEvent_Check`, the engine view's
  idle, the ancestor collectors, the periodic events,
  `KillPeriodicEvent`, `ProtoClone`, `ObjectToString` and its trim, and
  `DoEvent_Loop` (18980 bytes, tested with a whole machine of native
  methods) - over `NIERuntime.h` (the variable lookup, global calls,
  the arithmetic fast paths, try/onexception) (a91a24e, ebe10ae,
  ee223dd, cd06c72, 5949830, c50f5b4, 0c3c7b7).  NIE bugs kept:
  `ProtoClone` reaches itself through the variable `f`; `DoEvent_Check`
  ignores its argument.  The test asserts no fallback is installed and
  that the registry answers each offset (3d97eca).

## 2026-09-29: the ARM interpreter for packages' native code

- `src/armcpu/ARMCPU.h`: `TARMCPU`, an ARMv4 interpreter (ARM state, the
  StrongARM's set with the long multiplies), no Newton dependencies,
  memory through a callback interface, calls out through traps (test
  `armcpu.ARMCPU`) (509082e).
- `analysis/gluetable.py`: 0x018xxxxx is the ROM's public jump table
  (`gROMPublicJumpTable`, physical 0x13000-0x15e0c, 2947 entries) mapped
  at 0x01800000, each entry a branch into a named private jump-table
  slot; `--package` shows which a package's stubs reach, `-o` generates
  the adapter's table.
- `PackageNativeCPU.h`: `InstallPackageNativeCPU`, the seam's fallback,
  installed by `TNewtWorld::MainConstructor` (6a87bc8).  Refs are 32-bit
  handles into a per-call table the collector sees, immediates pass
  unchanged; `RefHandle`s, `RefVar`s and the stack in an ARM arena;
  `BinaryData`/`Slots` are live windows; the ROM image at 0 (NTK's
  runtime reads the version words at 0x13dc).  Calls out answered by
  name (about 90) or as the host's natives through `ResolveNativeFunction`;
  NTK's exception frames with ARM-side setjmp/longjmp.  Mahjongg's two
  native-compiled functions run and the game deals its 144 tiles (ctest
  `armcpu.Mahjongg`).

## 2026-09-29: comms, round 6 - the package-native seam and NTK's glue

- `frames/PackageNatives.h` (f7fcc8b): a package's native-compiled
  function (a 0x232 array or a binCFunction frame) runs a host
  re-expression registered with `RegisterPackageNative` (keyed by the
  code binary's length, its FNV-1a hash and the offset, so it survives
  the package loading anywhere), else the fallback
  `SetPackageNativeFallback` installs, else throws
  `kNSErrNativeNotReconstructed`.  `TInterpreter::CallCFunction`
  reconstructed (ROM 0x002f4da8).  Test `frames.PackageNatives`.
- NTK's glue decoded (874b041): package native code calls the ROM
  through `ldr pc,[pc,#-4]; .word 0x018xxxxx`; the ROM maps 0x01800000 +
  4k onto a table of branches at ROM 0x13000 + 4k (0x13000-0x16000, just
  after the jump table), each into a jump-table slot - the only base
  that lands every entry on a slot.  `pkgdisasm.py`/`pkgns.py --rom
  BUILD` name the calls; it confirmed the DNS tool's `OptionAt`,
  `RemoveOptionAt`, `InsertVarOptionAt`.
- Native-compiled NewtonScript's version-checking routines resolve on
  2.x to the ROM's own support (`GetGInterpreter`,
  `TInterpreter::IsSend`, `GetReceiver`, `GetImplementor`,
  `SetSendEnv`) (ef1672b).

## 2026-09-29: comms, round 5 - the flatteners and the streaming endpoint

- The NIE's DNS tool replaces an answered record through
  `TOptionArray::RemoveOptionAt`/`InsertVarOptionAt` (identified by their
  arguments - NTK's glue table at 0x018xxxxx is not yet mapped onto ROM
  symbols); the request's array is the client's own, so it may grow.
  The host tool does the same, so a record asked for with the NIE's
  empty names comes back whole (caa1644).
- `CPtrPipe` (`utility/Pipes.h`), `CRefPipe` (`stores/RefPipe.h`) and the
  flatteners `PFlattenPtr`/`PUnFlattenPtr`/`PFlattenRef`/`PUnFlattenRef`;
  the endpoint's `'frame` form (`echo.ns` sends a frame and reads it
  back).  ROM quirk kept: `PFlattenPtr` starts at offset 4 whenever
  there is a header.  DEVIATION: its block is `NewPtr`'d where the ROM
  mallocs, its callers asking `GetPtrSize` and freeing from the pointer
  heap (dd09202).
- protoStreamingEndpoint: `TEndpointPipe` (`comms/EndpointPipe.h`),
  `PStreamInRef`/`PStreamOutRef`, `TStreamingEndpointClient`,
  `TStreamingCallBack` and the four `CIS*` natives
  (`comms/StreamingEndpoint.h`); `InitTranslators` registers all ten in
  the ROM's order; `PipeCallBack::Status` given the signature its one ROM
  subclass uses.  `stream.ns` (ctest `host.NewtonStream`) streams a
  300-element frame out and back with progress every 512 bytes (729142e).

## 2026-09-29: the port-region audit

- `analysis/portfields.py` checks every `src/` use of a port's
  visRgn/clipRgn against the ROM function it cites (Ghidra's `GrafPort`
  lacks `QD_Gray`'s word, so its `clipRgn` is the ROM's visRgn and its
  `fgPat` the ROM's clip): OK / DIFFERS / NO-PORT, the last checked in
  the disassembly.
- Six more swaps fixed (19fd089): `TMonthView::HandleClick` and
  `TEditView::ValidateCaret` restored the saved visRgn into the clip
  (after a calendar tap the clip stayed replaced - `test_Views` now
  checks both regions after one), `TRootView::RestoreBitsUnderCaret`,
  `DoCaretClick`, `TInkWordGlyph::DrawAt`, `ZoomRect`.  68 functions
  checked, 62 were right.

## 2026-09-29: comms, round 4 - the host's DNS service

- `analysis/pkgns.py`: a package's NewtonScript listed and disassembled
  where it lies in the .pkg (`--functions`, `--disasm`, `--refs`), and
  its NTK native-compiled functions (`--natives`, `--native-disasm`)
  (72b809c).
- The `dnst` service (`comms/host/HostDNSTool.h`): the NIE's `dnsq` +
  four `rrcd` option request (op 1024) answered through the host's
  resolver; not found is the NIE's own -60791.  `src/host/demo/dns.ns`
  (ctest `host.NewtonDNS`): "localhost" to 127.0.0.1 and back (9f784a6).
- Bug found: a script's own `'sid '` option is two big-endian longs,
  which the host read as host words (a garbage port, -10015); the
  translators now convert it (DEVIATION).
- Finding: the NIE's protoFSM engine (`DoEvent`, the 19 KB
  `DoEvent_Loop`, the event queue, ...) is native-compiled - 19 functions
  in one 61 KB binary of ARM - so `InetStartUp` stops at its first call.

## 2026-09-29: the book reader, round 4 - the thumbnail's clipping was the host's

- A page thumbnail showed only its top-left 17x23 pixels.  First taken
  for a ROM bug from the decompile; checked against the disassembly it
  was two host transcription errors: the ROM's `GrafPort` has `visRgn`
  at +0x24 and `clipRgn` at +0x28, and Ghidra's decompile names +0x24
  `clipRgn`.  `ViewIntoBitmap` sets its fresh port's `portRect` and
  visible region (0x3f2f8, 0x3f30c), and `TRemoteView::RealDraw` saves
  the visible region, maps it back into the page's coordinates for the
  scaler's mapVis and restores it (0x1a67c4, 0x1a684c, 0x1a6978); the
  host had both on the clip.  Thumbnails now show the whole page;
  `books.Copperfield` checks the right and lower halves (eead505).

## 2026-09-29: comms, round 3 - a script's endpoint talks TCP (M3)

- Templates read back in the device's byte order with `'array` fields
  (`ConstructReturnValueFromDevice`, `UnmarshalArray`; 1aae5dc).
- `TNewScriptEndpointClient` and its 22 `CINew*` natives
  (`comms/NewScriptEndpoint.h`, `RegisterCommsNatives`): requests
  synchronous or queued to `completionScript`, errors to
  `exceptionHandler`, output by form, the input spec (byteCount,
  endSequence, end of packet, filters, binary targets, receive options,
  `partialScript`/`inputScript`) (676f8e5).  The newt world starts the
  comm manager, the host services and the translators (DEVIATION: the
  loader does it on the device).  `newton --tcp-echo port` and
  `src/host/demo/echo.ns`: bind, connect, "hello" back through a 5-byte
  termination, an asynchronous line back through an end sequence,
  cancel (the pending receive reported as -16005), disconnect (ctest
  `host.NewtonEcho`).
- Bugs found: the translators' bad-typelist error is -54011, not -54005;
  the endpoint's buffers mixed the host's malloc with the pointer heap's
  ReallocPtr/DisposPtr, crashing the first Output.  ROM bug kept: an
  asynchronous Bind never reads its options back (the wrong word of the
  bind event) and leaks their array.

## 2026-09-29: the book reader, round 3 - the remote view and page thumbnails

- `views/RemoteView.h`: `TRemoteView` (classes 87/88), made by
  `BuildView` - it hides its one child and draws it itself, scaled into
  its own bounds keeping the aspect ratio (the scaling undone if the
  drawing throws); printing a full page it draws only what the print
  form covers (2db2779).
- `PageThumbnail` builds the ROM's thumbnail template (from the page
  template's `thumbnailScript` or the page's blocks) as a `TRemoteView`,
  which Copperfield draws into a 60x80 bitmap; `books.Copperfield`
  checks it (b3f9b77).  Found: text drawn under `TQDScaler` comes out
  about a third of its width.

## 2026-09-29: the book reader, round 2 - the search, and Copperfield reads a book

- The search (`src/books/Search.cpp`): `TLibrarian::Find`/`CuFind` over a
  book's hints and the `FiveBitASCII_Adobe` table (`BookTables.cpp`,
  romtable.py), `TextSearch` (a hit only at the start of a word, the
  result titled by the words around it), `FindContentBySlot`,
  `FindPageByValue`/`BySubject`, `TurnToContent`, `AddInkMarks`.  ROM
  bugs kept: a book without hints is never searched; a partial match
  loses the character that broke it ("aab" does not contain "ab");
  `FindPageByValue`'s first-page stop is per page (d8bab4a).
- `PageContents`, `PageScroll`, `ZoomView` over `qd/ZoomRect.cpp`'s
  `ZoomRect`/`FixStep` (db6b82d).
- Copperfield opens a book (the help book under another ISBN, put in by
  `BookAvailable`), turns to page 5, scrolls, keeps its place in the
  Library soup and takes a bookmark (ctest `books.Copperfield`,
  d2a2812).  The title overlapping a help page's first line is thought
  to be the ROM's own layout (help pages are laid out for Tiny Tim), not
  confirmed.

## 2026-09-29: comms, round 2 - the endpoint, the marshalling and the translators

- The NIE's option layouts read out of its own native code with the new
  `analysis/pkgdisasm.py` (part 4 PInetToolCE, part 10 TDNSTool): `itrs`
  address + big-endian port, `ilpt` port + a byte defaulting to 1, `itsv`
  a long 1 = TCP / 2 = UDP, `ilid` a long defaulting to -1 - the table
  and the evidence in `docs/comms/README.md` (d60be75).
- M2: `CMGetEndpoint`, `TEndpoint`, `TEndpointEventHandler`, the endpoint
  events, `TSerialEndpoint` and its request blocks; `comms.Endpoint`
  runs Open to Close against a TCP echo server (83ebf98).
- Marshalling out, `MarshalArguments` (`frames/MarshalOut.cpp`): script
  values into a block by a template, big-endian; ROM bugs kept - a
  `'char` field writes the address of the converted bytes, a binary goes
  as a real (8960879).
- The frame translators (`comms/Translators.h`, library `comms_script`):
  `PScriptDataOut/In`, `POptionDataOut/In`, `GetDataForm`,
  `InitTranslators`; a `'service` frame becomes a `'sid '` option
  (ffb0bed).
- `host.NewtonThirdPartyPackages` was seen failing once ("missing:
  [|Internet Setup|]") in another build; it passed four runs in the comms
  build and runs no comms code, so it is suspected to be a race on the
  last install, not proven.

## 2026-09-29: the book reader, round 1 - the help book opens and reads

- `src/books/`: `TLibrarian` (`gLibrarian`, the Library soup,
  `BookAvailable`/`BookRemoved`, `vars.findApps`) and the `'book` part
  handler `TBookPartHandler`, registered by `TNewtWorld::MainConstructor`
  with `RegisterBookNatives`; `TNotebook::Constructor` calls
  `InitLibrarian`.  The ROM's help book installs at boot and gets its
  Extras icon (`SetupROMHelpBook`).
- `Outline.h`: `TOutline`/`THelpOutline` (view classes 102-107), built by
  `BuildView`; `RefreshTopics`, `TopicByName`, `ScrollToCurrent`.
- `Pages.h`: `PageTurnTo`, `PageTurnToSpread`, `PageTurnAway`,
  `MakeBlockView` - a page's blocks made views; `AddToContentArea`,
  `TurnToPage`, `HiliteBlock`.  26 book natives bound.
- `views`: `TView::SoundEffect`, `TruncateText`; `FCloseX` no longer
  static; `views` and `books` link each other.
- `tinytim:OpenHelpBook` shows the topics, "Learn the Basics" opens and
  "Erase text, writing, or drawings" draws its text and picture (ctests
  `books.Library`, `books.HelpBook`).

## 2026-09-29: comms, round 1 - the comm tool and manager over the host's sockets

- The owner's decision: networking goes to the host's TCP/IP stack
  through host implementations of the NIE's services, not a TCP/IP stack
  of our own or the NIE's ARM code.  The plan with sizes is
  `docs/comms/README.md`.
- Options (`TOption`, `TOptionArray`, `TOptionIterator`), `CBufferList`
  and `CShadowBufferSegment` (`utility/`), `TCommTool` whole with
  `StartCommTool`/`ServiceToPort`/`OpenCommTool`, the comm manager
  (`TCMWorld`, `CMStartService`), and the host's `inet` service and TCP
  tool (`THostTCPTool`, a `TCommTool` subclass: only connect, listen,
  get, put and termination are host code) over `hal/host/HostSockets.h`
  (a plain C interface built without the Newton include paths, library
  `hal_host_sockets`).  M0: the tool itself echoes through a local TCP
  server; M1: `CMStartService` hands back an open tool that does.  ROM bugs
  kept, among them `ImportConnectPB` checking the wrong pointer and a lost
  disconnect event.  Found: the host clock standing still lets
  `TaskMain`'s timeout countdown reach 0 ("no timeout"), so a polled tool
  re-arms it.  Commits `19d8e59`, `5f26289`, `45890d6`, `1ec804b`.

## 2026-09-29: third-party packages install and uninstall (the owner's fixtures)

- Stored apps came in as extensions: the Extras drawer's
  `HandleNewPackage` reads `GetPkgRefInfo(pkgRef).parts` as soon as a
  package is stored, before it is activated, and the host only had a
  part's frame once the part was imported at install - now imported on
  demand and reused by the install (`aa7fe7c`), and such provisional
  areas go with their package (`f85988a`).
- `SuckPackageFromBinary` read from a binary it had not locked, so a
  collection while storing moved it and garbage was stored (Mahjongg,
  Daleks, Times refused with -10401) - locked as the ROM's `TObjectPtr`
  does (`69e8ac8`).
- The 'font part handler (`TFontPart`, `InitFontLoader`; fonts into
  `vars.fonts` and out again) (`eef305e`); `PackageContaining` through the
  domain manager (the NIE's later parts lie beyond where the host looked),
  `ObjectPkgRef`/`ObjectPid` for a part's objects (`226f0cb`);
  `TCardPartHandler` for 'cdhl parts, and unregistered protocol parts named
  on stderr with `classinfo.py --package` listing them (`3b73f9c`); docs
  and `host.NewtonThirdPartyPackages` (`63b583d`).  MDaleks1 is refused as
  the ROM refuses it: a second package of the same name.

## 2026-09-29: the text engine, round 6 - pages; finished

- Pagination (`e3d153b`): `TXPageFrames` (a frame per page, rows of
  `fColumns`, a 5-pixel gutter), `TXMultiFrameFormatter`/`TXPageFormatter`
  (lines moved between pages after each edit; several functions read from
  the assembly), page breaks (a character 10 ends its page, the table of
  breaks kept and streamed), `TXNewtPageFrames::Draw`, `GetCountPages`.
  ROM bug kept: the edit note has room for two pages, the display records
  every page in view, so with three or more showing it overflows into
  `gTXParagCtrlChars` (the globals kept in the ROM's order so it fails the
  same way).  `txpages.ns`, `host.NewtonTXPages`.
- The mark left after a scrub (`0e50447`) was the caret; behind it a host
  bug: live ink was drawn into the screen's pixel memory where the ROM's
  inker ORs it onto the display only, so the caret's saved bits put
  scrubbed ink back.  Live ink now goes to a scratch map ORed onto the
  display (the host screen driver honours `srcOr`); `TRootView::PostDraw`,
  `TController::UpdateInk` and `CleanupStrayInk` redraw ink still waiting
  to be read.
- `analysis/protousers.py` (`4e1fc6d`, `0eb34da`): protoTXView has no
  users in the ROM or its packages.  Stale NOT YETs removed (`bda15b6`).

## 2026-09-29: the text engine, round 5 - clipboard, gestures, text on a store, the ruler bar

- The clipboard and drag and drop (`04db04e`): Copy makes a clipping of
  the selection, Cut copies and deletes undoably, Paste puts the front
  clipping in over the selection; a press on the selection drags it out,
  a drag inside the view is a move; a drop is re-read into text and
  styles, anything else becomes a picture run.
- The gestures (`c5f370f`): a scrub deletes, with a poof, the selection
  it lands on, else lines it covers at least 30%, else the characters or
  words it spans; the caret gestures insert a space or a return, or
  delete a character.
- Text kept on a store (`774136e`): after `SetStore` the text lives in a
  large binary; Externalize/Internalize carry it as `txText` and a chunk
  table; the finder searches it; the stream factory puts undo data of 4K
  or more into a large binary on the first store.
- The ruler bar (`9b9b1f0`): drawn from the ROM's own ruler pictures;
  justification and spacing icons, tabs dragged in, along or off, the
  margin and indent markers - each an undoable paragraph change.
  `test_TXStream` updated for the real large-stream path (`7576a16`).
- `txview.ns` / `host.NewtonTXView` cuts, pastes, scrubs, stores 2100
  characters and reads them back, puts ~4K of undo on the store and uses
  the ruler.  Found: a page is the view's height unless `SetGeometry`
  gives one; the ROM's memcpy is its memmove.  ROM bugs kept: a picture
  dragged out gets a drag rectangle partly from a stale stack word, some
  ruler tab changes carry a stack value, a drag that changes nothing
  loses its list of changes.  Left: pagination.

## 2026-09-29: the sound server, round 5 - GSM, and the Sound Recorder recording through it

- The GSM 06.10 full-rate coder - the ROM's compiled Toast library,
  transcribed function by function (`sound/GSM.h`; the library's unnamed
  static helpers cited by address; tables from the ROM's initialised data,
  `GSMTables.cpp`) - and `TGSMCodec` over it, registered in the ROM's order
  (mu-law, IMA, GSM, DTMF).  ROM quirk kept: an all-silent subframe is
  scaled by 6 instead of 0 in the lag search (no effect on the output).
  `test_GSM`: 33-byte frames with the magic nibble, 13-bit samples, a
  voice-like signal back at correlation 0.999, silence silent,
  deterministic, a bad frame refused - not yet bit for bit against the
  standard test sequences.
- The Sound Recorder records through it: 98% of the headless demo's
  playback is the test tone (47% before, when the "GSM" frames held raw
  samples).  Commits `5c3a5ab`, `30b4a57`.

## 2026-09-29: pictures finished - the plan as next-steps.md carried it

Rounds 1-4 of finishing pictures (commits in each item below; round 4
also fixed the heap damage it had caused - `RgnBlt`'s mask buffers were
sized for one bit per pixel after region masks were made at the port's
depth).  The section as it stood when pictures were finished:


The owner asked (2026-09-29) for the pictures to be finished.  What the
machine's own pictures use was measured first
(`analysis/pictures.py build/MP2x00US`, which walks every 'picture in
the ROM and the extension's packages opcode by opcode): 30 pictures, and
between them only bitmaps (BitsRect/PackBitsRect), clip regions,
comments, the pen size and short lines - all of which `DrawPicture`
already plays.  Text, curves, paths and pixel patterns only ever appear
in pictures the machine *records* itself: `MakePict` (the ROM's one
caller is the credits' `creditPict`, `MakeText` shapes recorded into a
picture) over `OpenPicture`/`ClosePicture` and the recording branches of
every standard proc.  So the order is recording first, then what
recording produces.  Sizes are `callgraph.py` lower bounds (not done):

1. DONE (`3ef412d`) **Recording**: `OpenPicture` (788 B), `ClosePicture`, `KillPicture`,
   `PutPicOpcode`/`Byte`/`Word`/`Long`/`Rect`/`Point`/`Data`/`Rgn`,
   `PutPicVerb` (the pen, patterns and oval size written only when they
   changed), `PutPicPat`/`PutPixPat`/`PutPat1Data`/`PutPixMap`/
   `PutColorTable`, `CheckPic` (the clip region), `EqualPat`, and the
   recording branches of `StdRect`, `StdRRect`, `StdOval`, `StdArc`,
   `StdPoly`, `StdRgn`, `StdLine`, `StdBits`, `StdComment` - about 2.5 KB
   plus the branches.  Test: a picture recorded and played back to the
   same pixels.
2. DONE (`be66d0e`; the text objects' other operations and scaled
   drawing NOT YET) **Text in pictures**: playing it (`DrawPicText`, `TextCleanup`,
   `NewText`, `CallDrawText`, `DisposeText`, `InvalCachedTextInfo` - 1 KB)
   and recording it (`StdText`'s `DoPutText` 2.5 KB, `UpdateLayoutState`).
3. DONE (`64a793c`) **`MakePict`** (`FMakePict`, `CommonMakePict`,
   `SetStandAloneBoundsInViewsRecursively`) - the credits' picture made
   and drawn.
4. DONE (`3f1d0f8`) **Curves and paths**: drawn and recorded (`MapCurve`/`CallCurve`/
   `StdCurve`/`DrawCurve`/`FrCurve`/`GetCurveBounds`/`OffsetCurve`/
   `ScaleCurve`/`PutPicCurve`/`EqualCurve`; `MapPaths`/`CallPaths`/
   `StdPaths`/`DrawPaths`/`FrPaths`/`FramePath` and the path walker/
   `GetPathsBounds`/`OffsetPaths`/`ScalePaths`/`PutPicPaths`) - about 3 KB.
5. DONE (`a2f0ceb`) **Pixel patterns of type 1**: `ConvertPixPat` (340 B) and its
   converters.
6. The neighbours a picture draws through: arcs of less than a full turn
   (`Shapes.cpp` - DONE, `fd38560`) and italic (`Text.h` - DONE,
   `cdfd8c8`).
7. DONE (round 3) **`TQDScaler`** (0x00196018-0x001973c8, about 5 KB): a picture (or
   any drawing) under a transform that scales.
8. DONE (round 4: `6ed07c2`, `fb762cc`, `e622a1b`, `0734071`, `0e10570`)
   **The ROM's own blitting of pictures and text**: `StretchBits` whole
   (`SetupConversion`, the `Combine*`, 33 row stretchers, the blit modes
   under region masks - now at the port's depth, as the ROM makes them);
   text composed a style run at a time into a slab and stretched
   (`DrText`/`DrTextChunk`, with outline and shadow, the broken underline,
   gray text through `MakeGrayText` and a font spec's `color`);
   `CalcTextBounds`; `DrawShapeScaled` for bitmaps not at 72 dpi; a
   `colorData` entry chosen and its colour table made a gray table.

Pictures are finished.  Left, and recorded as NOT YET where they lie:
`TGrayShrink` (the view protocol that shrinks an anti-aliased one-bit ink
word into grays - the ordinary stretch stands in, as on a ROM with no
implementation registered), a text object's layout numbers (0x400) and
`TextArrow` (0x2000), `ZoomRect`, a `MakeBitmap` kept on a store.

## 2026-09-29: pictures, round 4 - the ROM's text drawing, CalcTextBounds, DrawShapeScaled

- `StretchBits` as the ROM's (`6ed07c2`): the row stretchers, converters,
  combiners and blit modes, transcribed through `transcribe_words.py`.
- Text composed as the ROM composes it (`fb762cc`, `qd/DrText.cpp`):
  `DrTextChunk` ORs a style run's glyphs into a one-bit slab at the
  strike's size, works bold, italic, the underline (broken by
  descenders) and outline/shadow on it, and `StretchBits` it onto the
  port - or ORs straight into the port for plain srcOr text.  The mode is
  the options' (srcOr with none), not the pen's.  `CalcTextBounds` and
  the 0x200 operation; `TextBoundsInfo` +0x10/+0x18 are the leading and
  the vertical advance.
- Host bug found: every region mask was made one bit per pixel, where the
  ROM makes it at the port's depth (`InitRgnRec`); on the four-bit screen
  `StretchBits` through a complex visible region lost most of what it
  drew (the Notepad's icons, the credits' text).  Fixed, and `RgnBlt`'s
  mask buffers sized to match - the first cut of that overran them and
  damaged the heap for every booted run until it was caught with
  `NEWTON_HEAPCHECK`.
- A font spec's `color` made the style's pattern (`0734071`), gray text
  through `MakeGrayText`; `fFontPattern` is 0 for none, as the ROM clears
  it, not nil.
- `DrawShapeScaled` (`e622a1b`): `DrawIntoBitmap` at a bitmap's own
  resolution; `GetFramBitmap`'s entry choice and gray table (`0e10570`).
- Pixels that changed: the scaled-map demo's stretched text (the slab is
  stretched whole rather than each glyph, nearest pixel); a descender row
  below the strike's `minAfterBL` is clipped (typing.ns's "y"); text in
  srcCopy now blanks its slab's rectangle; `test_Text`'s pen-mode check
  became an options-mode one.  Pictures are finished bar `TGrayShrink`.

## 2026-09-29: the sound server, round 4 - byte order, the microphone, the Recorder driven

- 16-bit samples big-endian in memory on every host - a frame's samples,
  codec and DMA buffers - read and written as numbers through
  `sound/SampleWords.h` (converters, resamplers, mixer, IMA and mu-law,
  DTMF), swapped only at the host driver; a recording kept in a soup entry
  reads and plays the same (`test_PlaySound`).  No DEVIATION needed.
- The waveIn microphone (`host/win32/HostAudio.cpp`, a ring the driver's
  record step drains without waiting); `newton --microphone-tone HZ` for
  the headless one.
- The Sound Recorder driven through its own buttons (`demo/recorder.ns`,
  `host.NewtonRecorder`: Rec, 2 s, Stop, Play; the status line at each
  step).  Found that it records through `TGSMCodec` (next).
- Commits `938decb`, `29b1d2e`, `9103d09`, `77266e1`.

## 2026-09-29: the text engine - protoTXView with its 39 methods

- Round 1 (`3a06b6e`, `3c413b6`, `ccfdf89`, `f9bc46a`): `TXOffset`, `TXRun`/
  `TXRunRange`, `TXRulerRange`, the helpers, `TXLinesHeights`.
- Round 2 (`c27fefb`, `f2e1ca2`, `8e9e996`, `ade7ff9`, `9269a3a`,
  `71d34a6`, `b12ae2e`, `da24b22`, `f01e06b`, `9ba0071`): QuickDraw's
  text-object questions (`qd/TextObject.h`: `CharToPoint`, `PointToChar`,
  `GetTextObjField`), the text and graphics runs, `TXStyledText`,
  `TXLine`, the frames and frame formatters, `TXFormatter` (reflow after
  an edit equals formatting from scratch).
- Round 3 (`c88636a`, `2b2b689`, `3908395`, `2fc557f`): `qd/ScrollRect.h`
  and `qd/LocalToGlobal.h`, `TXDisplay`, `TXHilite`, `Textension`.
- Round 4 (`0e2b6d9`, `708e265`, `42cce1c`): the containers
  (`TXContainer`, `TXStdContainer`, `TXLocalContainer`,
  `TXPrivateContainer`, `TXNewtContainer`), the edit commands with undo
  (`TXCommand`, `TXEditCommand`, `TXKeyCommand`, `TXMoveTextCommand`,
  `TXReplaceTextCommand`), `TXView` (class 108) with
  `TXNewtDisplay`/`TXNewtHilite`/`TXNewtPen` and `TXBinaryChars`, and all 39
  `protoTXView` methods; `txview.ns` / `host.NewtonTXView` puts text in,
  types, makes it bold undoably and scrolls.
- ROM bugs kept, among them: `ReplaceAll` gives Format a wrong length after
  the last search; `GetCountPages` answers the view's own address when
  there are no pages; `OffsetToCaret` ignores its offset and uses the
  selection; `CheckBounds` swaps offsets but not their flags.

## 2026-09-29: the sound server, round 3 - coded sounds, recording, the Sound Recorder opens

- The codec channel: coded frames decoded by the `'codc` task a buffer at
  a time and scheduled on the output channel through the server's port;
  an IMA frame played with `PlaySoundSync` comes out exactly as
  `ExpandIMA` makes it (the round-2 coded-frame deviation gone).
- Recording: the server's input side (input channels, the input
  interrupt, `EmptyDMABuffer`), the compressor recording through a codec,
  the host driver's input with a test-signal microphone
  (`HostSoundSetSource`); a recorded-then-played round trip gives back all
  9600 samples, and IMA recording is exact against `CompressIMA`.  Bug of
  its own fixed: `protoSoundChannel`'s record direction is `'record`, not
  `'input`.  The Sound Recorder opens: open-apps reports 0 failed.
- `TSoundPowerHandler`; `TDTMFCodec` - an FM synthesiser, not a decoder: a
  score of up to 12 tones with envelopes, five algorithms, over the ROM's
  quarter-sine table (the "1" key: power at 697 and 1209 Hz, 2200 samples
  for its 100 ms envelope); `StopFrameSound`, now called before a package
  goes out of use (`DeActivatePackage`, `PackageUnavailable`; packages and
  stores link sound).
- Commits `5a7210a`, `ed5a2b1`, `5e00f2d`, `73b8ad5`, `5cb73d3`, `395649a`.

## 2026-09-29: frames - the last natives, and a NOT YET sweep

- The last three frames natives: `GetFrameStuff`, and the store frame's
  `CardSlot`/`CardType` over `GetStorePSSInfo` (`f438557`).
- The sweep of `src/frames/` (40 NOT YET comments to 20, all genuine):
  strings read and written through `TRichString` as the ROM does
  (`aref`/`setAref`, `StrMunger`, `GetChar`/`SetChar`,
  `StripDiacriticals`; comparisons collate by the sort tables, so
  `"a" < "B"` - `0196f25`); `vars.breakOnThrows`' break loop once per
  exception name, `&` keeping a rich string's ink, and `Stringer`'s
  trailer written in a byte order the host could not read back
  (`e83f0ab`); `TNSDebugAPI::Return` throws -48215 as the ROM's does
  (`17b623e`); `Uriah`/`UriahBinaryObjects`, the heap census (`64b9f97`);
  the stale reasons reworded and the GC's verbose report through the REP
  (`f5c8ffa`).
- Found on the way, for the sound agent: the importer compared class names
  with strcmp, so the ROM's reals of class 'Real (its own spelling) were
  never byte-swapped; now compared as symbols compare (`611feca`).

## 2026-09-29: the sound server, round 2 - sounds played from scripts

- The client `TUSoundChannel`, `TFrameSoundChannel` (`sound/FrameSoundChannel.h`),
  `GlobalSoundChannel` and the twelve `protoSoundChannel` natives:
  `PlaySoundSync` of the ROM click plays through the server - 260 samples
  reach the driver, 0.965 correlated with the click's own
  (`sound.PlaySound`, `host.NewtonSound`, `src/host/demo/sound.ns`).
  `newton` installs the host sound driver, a waveOut loudspeaker when
  windowed (`host/win32/HostAudio.cpp`, not yet heard) and the null
  backend headless; `NEWTON_TRACE_SOUND` says why a frame cannot be played.
  Found on the way: imported reals of class 'Real were never byte-swapped
  (the importer compared class names with strcmp; fixed with symcmp in
  `611feca`), which is what stopped `host.NewtonKeyHelp`.  Commits
  `1734952`, `e547245`, `81cb4dc`, `788e8a4`.

## 2026-09-29: the sound server, round 1

- The `PSoundDriver` seam (`sound/SoundDriver.h`, the ROM's driver
  protocol in its dispatch order); `TSoundServer` (`sound/SoundServer.h`),
  the `'sndm` app world with the output and decompressor channels and
  `FillDMABuffer` mixing (two 0xea0-byte buffers scheduled in turn); the
  host's `PMainSoundDriver` (`hal/host/HostSoundDriver.h`: 21600 Hz 16-bit,
  each buffer's end a host interrupt source - the new generic registry
  `hal/host/HostInterruptSources.h`, polled by `HostDeliverInterrupts` and
  folded into the idle task's wait, `docs/host-runtime.md` - and a null
  backend that captures what was played).  `test_SoundServer` plays
  16-bit blocks unchanged and 8-bit 11025 Hz blocks resampled.  Host bug
  fixed: an immediate command's value read at the wrong offset (`ULong` is
  pointer-sized).  Commits `f3b2f14`, `28ba50a`, `231091d`, `651a2bb`.

## 2026-09-29: the view natives finished - the key-help slip, a roll over a soup, ReFlow

Views are 95 of 95 natives bound.
- `354ede4`: the key-help slip (`views/KeyHelpSlip.cpp`:
  `FKeyHelpSlipSetup`, `FKeyHelpSlipDraw` and `GetCommandCharWidth`,
  `GetModifiersWidth`, `DrawModifierIcons`, `GetSlipWidth`);
  `src/host/demo/keyhelp.ns` photographs it over the Notepad
  (`host.NewtonKeyHelp`).  ROM quirks kept: the widest letter is carried
  from one column to the next, and a truncated name is drawn one
  character longer than fits.
- `c302bb3`: `TView::SyncScrollSoup` - a roll whose items are a soup
  cursor, stepped by the roll's height less `overlapScrollAmount` (else
  twice the `viewLineSpacing` inherited from the parents, which is what
  the first try of the demo measured); `src/host/demo/soupscroll.ns`
  (`host.NewtonSoupScroll`).
- `6579778`: `ReFlow` and `ReflowPreflight` with `ReflowText`,
  `SplitStyles`, the three style mungers, `SaveStylee`, the C
  `SetFontSize` and `TParagraphView::OffsetPastVisible` - the Notepad's
  print format laying a page out again for the printer
  (`views/Reflow.cpp`).  The decompiler dropped `ReflowText`'s text walk
  as unreachable; read from the assembly, it only ever stops at the end
  of the text, so a paragraph is poured whole and cut only where the page
  runs out.  ROM bugs kept: the styles of a piece not cut again, the 'all
  fonts and the ink print scale worked out after the styles slot was set,
  the room left reset to a whole page after every piece, `SplitStyles`'
  length for a part inside one run, a gutter test that is never true.
  `TestReflow`; `src/host/demo/reflow.ns` (`host.NewtonReflow`) walks the
  setup assistant, reflows a note into a page 160 wide and photographs
  the groups: a centred group of shapes shows the view lasso moving the
  group and not its children, which the ROM's Constructor does too.

## 2026-09-29: the rest of the view natives

- `e58914d`: `TView::SyncScroll` and its native (protoRoll's scrolling -
  the Preferences roll), `GrayShrink`, `FormatVertical`,
  `ExtractRangeAsRichString`, `ExtractRichStringFromParaSlots`,
  `ComputeParagraphHeight` (its box built through an unaligned load, read
  from the assembly), the picker's `GetScrollerValues`/`Scroll`,
  `KeyboardInput`, `DV`, `ViewAutopsy`.  ROM bugs kept: `FormatVertical`'s
  spread divides by `ChildrenHeight`'s count (one more than the children);
  `GrayShrink` offsets the view's global bounds a second time and resets a
  non-array `grayLevels` preference to nil.  New `test_ViewExtraNatives`.
- `86e36ad`: `DrawGraphic` over the `TSplashScreenInfo` protocol and
  `DrawSplashGraphic` (no implementation in this ROM: nil, and the script's
  own picture).
- natives.py (`6962519`) had filed a dozen view methods under frames;
  views are 91 of 95.  NOT YET, measured: the key-help slip
  (3.5 KB), `ReFlow` and its reflow group (about 7 KB, the print formats),
  `TView::SyncScrollSoup` (1.5 KB) - `docs/views/README.md`.
- Worked in parallel with the pictures, recognition and text agents in a
  build directory of its own (`tmp/build-views`); the other agents'
  in-progress edits to the shared tree broke the build twice (a `qd`
  function not yet declared, a recognition test) and were waited out.

## 2026-09-29: the recognition system's last gaps

- The seven unanswered methods (`024ee51`): the correction info's
  `GetAlternatives` (FGetAlternates), `Extract` (FExtractRange - its own
  walk, one comparison unsigned and the other signed, as the ROM) and
  `insert` (FInsertRange), `LookupCompletions` over
  `GetWordCompletions` (the prefix copied into 64 UniChars with no room
  for the terminator and only its first letter lowered, as the ROM;
  completions come back in the cursor's sorted order), `HandleUnit`,
  `HandleRawInk` and `VoteOnWordUnit` (the point at the middle of the
  unit's base-line box, clamped to its bounds - from the disassembly).
  `src/host/demo/alternatives.ns`, ctest `host.NewtonAlternatives`;
  `test_Dictionaries`' completions.  Recognition's natives: 125 of 125.
- The code gaps (`6a59f42`): `ValidateWord` asks the dictionaries and
  `WRecVerifyWordSymbols` (over the unnamed 0x00144470: Rosetta's domain
  when it reads, ParaGraph's `VerifyWordSymbols` otherwise);
  `FindBaseline` takes its first path over ParaGraph's `low_level` in its
  base-line-only mode - the decompiler read each of its three unaligned
  loads (the two heights, rc +0xec/+0xea, and the trace's count put in
  rc +0x96) as the halfword after the one the load takes; the arbiter's
  `ArbitrateGraphicsWords`, `ArbitrateByRules`, `GetGraphicBiasedScore`,
  `GetFirstWordIndex` and the shape half of `ArbitrateEarly`;
  `TWRecognizer::EndInkStrokeGroup`.  The engine's own VM heap is decided
  against (nothing on the host depends on it).
- Host test note: `test_Words` now makes the dictionaries (and a stand-in
  `rcbuildchains`) before validating, the ROM never running without them;
  `WRecVerifyWordSymbols` answers "nothing to object to" with no
  recognition system (DEVIATION, host tests only).

## 2026-09-29: the text engine, round 1 - towards protoTXView

- Sized the path to the 39 protoTXView methods natives.py now files
  under `text`: 292 functions not done, about 45 KB (`callgraph.py`, a
  lower bound); the plan by class and layer is `docs/text/README.md`'s
  "Not yet reconstructed - the plan".
- `text/TXOffset.h`: `TXOffsetPos` (the ROM's two-word TXOffset, for
  where it is passed by address) and `TXOffsetRange` (CheckBounds' quirk
  kept: the offsets swap, the flags do not).
- `text/TXRun.h`: `TXRun`, its virtuals in the ROM's slot order (the pure
  ones named from TXGraphicsRun's vtable), and `TXRunRange`
  (`CharToTextRun`: the text run an offset takes its style from).
- `text/TXRulerRange.h`: the ruler ranges with the pending ruler of the
  paragraph not yet typed, ValidateRuler/ValidateRulerRange (the
  discarded recursive answer kept), CharRangeToParagRange,
  GetReplaceExtraChars, and `TXGetParagStartOffset`/`EndOffset`
  (SearchChar's 0x0c means any line break).
- `text/TXUtilities.h`: long rectangles (IsPointInside counts both edges,
  kept), the scratch region pool, TXClipFurther/TXCalcClipRect/
  TXInvalSectRect (the root view's vtable +0x54 is `Dirty`, found with
  `vtable.py --find ClassID__9TRootViewCFv --slot 0`),
  TXGetNewDefaultObject.  `text` now links `qd` and `views`.  NOT YET:
  TXScrollRect (QuickDraw's ScrollRect).
- `text/TXLinesHeights.h`: the lines' heights as groups of equal lines,
  PixelToLine (it gives back the found line's top), TXFormatReflowLines,
  TXParagCtrlChars.
- Tests: `test_TXRunRange`, `test_TXUtilities`, `test_TXLinesHeights`.
  Built in `tmp/build-text` (in parallel with other agents); ctest there
  130/130, open-apps only the Sound Recorder.

## 2026-09-29: pictures round 3 - MakePict, the credits, word breaks

- `MakePict` (`64a793c`): `FMakePict`, `CommonMakePict`,
  `ROM_CommonMakePict`, `SetStandAloneBoundsInViewsRecursively`, and
  `MakeShape` of a view.  The About slip (magic pointer 152) opens its
  credits view for an application with `aboutInfo.credits`, which makes
  the lines one picture with `MakePict`; no application in the ROM has
  credits, so `src/host/demo/credits.ns` supplies one.  On the way:
  `DrawPicture(RefArg...)` had no shape branch (a picture shape given to
  `CopyBits` drew nothing), `DrawShape`'s offset workaround went, and the
  ROM's `ForceScaling` brackets were added to `HitShape`, `PointInShape`,
  `DrawIntoBitmap` and `MakeRegion`.
- `FindWordBreaks` (`ef99232`) runs the locale's break table (a Script
  Manager table: a class table and two state machines) instead of
  breaking at spaces; `DoTextOnce`'s option selectors 9 and 10.
- A width to fit is measured with a stretched strike's scaled advances,
  as the ROM measures it (`test_Views`' espy-18 paragraph updated).

## 2026-09-29: pictures round 3 - TQDScaler and text at a scale

- `TQDScaler` (`d0d51bb`, `qd/Transform.h`): the scaler takes the port's
  procs over while a transform is in force, and its eleven procs map
  what they are given through the transform the stack comes to
  (`RecalcTransform`); the frame pen is scaled (its height from its
  width - ROM bug kept) and the port's clip, set in the drawing's own
  coordinates, mapped and cut by the clip outside.  `SetStdProcs` now
  fills all fourteen procs, which the scaler starts from for a port with
  none.  `TQDScaler::Offset` answers nothing now, so `views/DrawShape.cpp`'s
  offset DEVIATION is a no-op awaiting removal.
- Text at a scale: `DrText` opens the fonts at the size times the scale
  and stretches the nearest strike when there is none of that size
  (espy 12 at 2.0 is the 16-point strike at 1.5).  Found on the way:
  espy 24 at 1.0 is not the same pixels as espy 12 at 2.0 - the ROM's
  ratios come out 1.49998 and 1.5.  NOT YET: the ROM fits a width with the
  stretched advances; the host keeps the strike's, which the paragraphs'
  line breaks (and `test_Views`' espy-18 style runs) depend on.
- `src/host/demo/scaledmap.ns` (ctest `host.ScaledMapDemo`): the World
  Clock's map at half size, and shapes with text doubled and stretched.
- Ink words and italic in the recorded scenes (`2737dc6`): `test_Ink`
  records text whose runs are ink words and plays it back to the same
  pixels, and pins the ROM bug that one ink-word style alone plays back
  as nothing.  Host bug found on the way: `GlyphInkData` read an ink word
  held as a block (a rich string's, or a picture's 0x81a4) two bytes
  short - the size halfword counts the word, not itself - cutting its
  strokes.

## 2026-09-29: pictures round 2 - curves, paths, pixel patterns, arcs, italic

- Curves and paths (`3f1d0f8`, `qd/Curves.h`, `qd/Paths.h`): the verbs,
  `StdCurve`/`StdPaths` recording and drawing, `FrCurve`'s five halvings,
  the TrueType path walker, and `ParsePicCodes` mapping and drawing both.
  ROM bugs kept: a curve is mapped twice on playback; a "same curve" is
  recorded as 0x0c88+verb, which playback reads as a reserved opcode;
  `GetCurveBounds` never lets its first point reach the maximum.  The
  DDK's path words became `Long32` (sync patch).
- Pixel patterns of type 1 (`a2f0ceb`, `qd/PixelConvert.h`):
  `GetPicPixPat` reads them whole and `ConvertPixPat` makes them the
  screen's kind through the row converters.  ROM bugs kept:
  `ConvertIndex8to4`'s first pixel takes its gray's low nibble; rows not
  a multiple of four bytes are laid out skewed.  A recorded four-bit
  pattern comes back a shade out for some grays - the ROM's own round
  trip through `PutColorTable`'s ramp and `RGBtoGray`.
- Arcs of less than a full turn (`fd38560`): `DrawArc`'s row loop, each
  row cut by the lines at the two angles.
- Italic (`cdfd8c8`): each glyph row moved right as `DrTextChunk` shears
  its slab, from a slab bottom that lies below the descent by the descent
  again - so the baseline row itself moves.  The open-apps smoke still
  opens everything but the Sound Recorder, and the World Clock's map
  still draws.

## 2026-09-29: pictures round 1 - recording, and text in pictures

- Pictures recorded (`3ef412d`, `qd/PicRecord.h`): `OpenPicture`,
  `ClosePicture`, `KillPicture`, `CheckPic`, the `PutPic*` family,
  `StdPutPic`, `PackBits`, `EqualPat`, and the recording branches of
  `StdRect`, `StdRRect`, `StdOval`, `StdArc`, `StdLine`, `StdPoly`,
  `StdRgn`, `StdBits`, `StdComment`.  A scene of every standard proc,
  recorded and played back, gives the same pixels as drawn directly.
  `analysis/pictures.py` walks the ROM's own 30 pictures: they use only
  bitmaps, clips, comments, pen size and short lines, so text, curves
  and paths only reach a picture by being recorded.
- Text in pictures (`be66d0e`, `qd/TextObject.h`): text objects
  (`NewText`, `DisposeText`, `DrawTextObj`, `CallDrawText`, `StdText`)
  so that `DrawTextOnce` draws through the port's text proc, as the ROM
  does; `DoPutText` records both the Newton's text (0x81a0-0x81a4) and
  the Macintosh's (LongText); `DrawPicText`/`TextCleanup` and the
  0x28-0x2b branch play it, `PicPlay` now keeping the ROM's text state.
  ROM bugs kept: 0x81a0 never remembered; LongText's byte count; a single
  style's carried family never filled in on playback.  A ROM bug not
  kept: `TextCleanup` gives back blocks inside another (host heap).  A
  correction on the way: `kPicDefaultTextOptions`' 1 is the transfer
  mode (srcOr), not the last word.

## 2026-09-29: packages round 6 - segments, the progress callback, compressed large objects; packages closed

- `store:RestoreSegmentedPackage(soup, keys)` over `CPackageArchivalPipe`
  (`packages/PackageArchivalPipe.h`, `10c38f9`): a buffer pipe over a soup
  of chunk entries (each 4K a `PackageEntry` binary, the entries' unique
  ids the keys).  Host bug found on the way: `CBufferPipe::ResetRead` left
  out the ROM's `Seek(0, end)`, so a fresh read buffer looked full of
  whatever its block held and the first read returned it;
  `test_Pipes` had asserted the old behaviour.  The end-to-end test had to
  go into a booted demo (ctest `host.NewtonSegmented`): the ROM's own
  `RegisterNewPackage` wants the booted machine's globals, and it refuses
  a package whose name the store already has, so the demo takes Formulas2
  off the store before restoring it from its chunks.
- The progress callback, `TLOCallback` (`stores/LargeObjects.h`,
  `4663bea`): a record, not a class - the function to call, the script's
  function and info frame, the frequency - made by `AllocatePackage` and
  told by `TPackageIterator::Store` as a package streams in.  The first
  call only makes the info frame.
- `LODefCreateFromComp`/`FillChunkArrayCompressed` (`56bb4ce`): a large
  object made again from `LODefaultBackup`'s compressed stream, so a VBO
  through NSOF written with `SetCompressLargeBinaries` reads back.
- XIP packages measured and recorded, not started: about 11 KB at
  0x00277d54-0x0027aa00, standing on the ROM domain manager's page
  faulting.
- Packages closed: `docs/packages/README.md` has a Status table of what
  is left and what each waits on; `docs/next-steps.md`'s plan moved into
  this log (below).  31 of 32 package natives answered.
- ctest 125/125; coverage 12453 citations, 0 bad, 7251 of 16671 (43.49%);
  natives 1041 of 1326 (78.5%); open-apps: only the Sound Recorder fails.

## 2026-09-29: packages round 5 - 1.x packages, TPixelMapCompander, SuckPackageOffDeskTop

- **The 1.x packages** (`c3bf661`; `StorePackageNatives.cpp`): the
  store's 1.x package directory (a System soup entry listing pssids) and
  `Activate1.XPackage`, `DeActivate1.XPackage`, `Remove1.XPackage`,
  `store:1.XPackageToVBO`, the uncalled `StorePackages*Available`, and the
  1.x `NewPackage` (RestorePatchFromPipe's).  ROM quirk kept:
  `Activate1.XPackage` answers the id or the error as one integer.  ctest
  `host.NewtonOneX` (`src/host/demo/onex.ns`).
- **TPixelMapCompander** (`e8c2e7d`; `stores/PixelMapCompander.cpp`): a
  store bitmap's default compander - LZ over row-delta filtered pages,
  the row length from a 0x2c-byte header the first write keeps; registered
  by `InitQDCompression` from `InitGraf`.  The host's domain manager now
  passes the object's base as a page's last argument (as the ROM's does)
  and writes pages from a copy (DEVIATION: the ROM writes a page out as it
  lets it go, and this compander filters the page in place).  ROM bug
  found: `FillChunkArray` passes nought as the base, so a bitmap filled
  from a pipe reads its PixelMap from the vectors page.  `MakeBitmap`'s
  store bitmaps need no compander named now (`host.NewtonVBO`).
- **SuckPackageOffDeskTop** (`857813b`) over `utility/StdioPipe.h`'s
  `CStdioPipe` - the C library's stdio, the desktop's files on the
  MessagePad and the host's own here (DEVIATION); ROM quirk kept: a read
  past the end sets the count and eof and still throws -3.
- The heapcheck failure of round 4 is the entry below.

## 2026-09-29: a system call's answer lost to a switch at its exit

- The boot's store packages failed to activate whenever `NEWTON_HEAPCHECK`
  was set, even at a count that never walks.  The hooks change nothing in
  the heap; they only slow the host (every `DisposPtr` walks it).  The
  failing call was `InstallPackage`'s RPC to port **0**: `PackageManagerPortId`'s
  name server lookup had gone to port 0, because `TUNameServer`'s
  `GetPortSWI(kGetNameServerPort)` had answered 0.
- Root cause, in the host runtime (`os600/user/host/SWI.cpp`'s
  `ExitWithResult`, and `GenericSWIStub.cpp`): a glue's result was only
  returned, and when `HostSWIExit` switched the task out (a timer or the
  time slice falling due at the exit - which a slow host makes likely) the
  stub answered the saved r0 instead - whatever r0 held at the call.  The
  ROM's exit path saves the switched-out task's registers with r0 still
  holding the glue's answer.  Fixed: the result is stored in the saved r0
  before the exit, so a pre-empted call keeps it and a later completion
  still overwrites it (`docs/host-runtime.md`).
- Any system call could have been hit on a slow run; this is the likely
  cause of other rare "impossible" failures seen under load.  ctest
  `host.NewtonPackageStoreSlow` (the store-package boot under
  `NEWTON_HEAPCHECK=100000`) fails without the fix and passes with it;
  `NEWTON_HEAPCHECK=1` and `=20` runs activate the package too.

## 2026-09-29: packages round 4 - packages on a store

- **The store side** (`135a556`; `stores/PackageObjects.cpp`):
  `DeallocatePackage`/`RemoveIndexTable`, `PackageAvailable`/
  `PackageUnavailable`, `DeletePackage`, `IdToStore`/`IdToVAddr`/
  `StoreToId`, the `TLrgObjStore` and `TStoreDecompressor` protocols,
  `GetLOAllocator`; `InstallPackage` tells the domain manager a store
  package's id.  Found on the way: `LODefaultDelete` is a branch to
  `DeallocatePackage`, not the no-op it was first read as.
- **Writing and reading** (`338308e`; `packages/StorePackages.h`): the C
  and frame relocation generators, `TStorePackageWriter`,
  `TPackageIterator::Store`, the six store decompressors,
  `TStoreCompanderWrapper`, `TLOPackageStore`, `AllocatePackage`/
  `NewPackage`, `BackupPackage`.  Host bug found: the domain manager read
  a 0x20-byte large-object root from a package's 0x14-byte one.  A test
  run after `TestStreamed` crashes in the 'pipe' world that test leaves
  waiting, so `TestOnStore` runs before it.
- **The NewtonScript side and the boot** (`d15dea5`;
  `packages/StorePackageNatives.cpp`): `SuckPackageFromBinary`,
  `RestorePackage`, `ActivatePackage`/`DeActivatePackage`, `ObjectPid`,
  `ObjectPkgRef`, `PidToPkgRef`, `PssidToPkgRef`, `PssidToPid`,
  `GetPkgRefInfo`, `GetPkgInfoFromPssid`, `PidToPackageLite`,
  `IsProtocolPartInUse`, `IsPackage`, `GetPackages`' store slots, the
  imports' `client`; `PreMain` activates the internal store's packages
  (`FPenPos` over `PollTablet`, which `ActivateStorePackages` asks);
  `newton --package` stores the package, so with `--store` it survives a
  restart (ctest `host.NewtonPackageStore`).  A package's name is
  big-endian UniChars on every host: `GetPkgRefInfo` converts it (the
  first version answered "").

## 2026-09-29: packages round 3 - the large binaries

- **Large binaries (VBOs)** (`2735709`; `stores/LargeBinaries.h`,
  `stores/Ephemerals.h`): `LBData` and its eight indirect-binary procs,
  `AllocateLargeBinary`, `WrapLargeObject`, the entry cache `gLBCache`,
  `Load`/`Duplicate`/`DeleteLargeBinary`, commit and abort through the
  store wrapper's `TEphemeralTracker`, the store object format's tag 12,
  NSOF's large binaries (and `LOWrite`, `LOSizeOfStream`,
  `LODefaultBackup`, `LODefaultStreamSize`, `DuplicatePackageData` under
  them), and the natives (`NewVBO`, `NewCompressedVBO`, `IsVBO`,
  `GetVBOStore`, `GetVBOCompander`, `GetVBOCompanderData`,
  `GetVBOStoredSize`, `ClearVBOCache`, `VBOUndoChanges`).  `THostStore`
  answers "LOBJ".  Host bugs found: `TStoreWrapper::Dirty` and
  `SparklingClean` did not return early as the ROM's do (Dirty now locks
  the store, SparklingClean flushes the ephemerals).  The test's one
  surprise was its own: an `LBData*` kept across an allocation is stale
  once the heap compacts.  `test_LargeBinaries`.
- **The consumers** (`01ec11f`): `GetBitmapInfo`, `MakeBitmap`'s store
  option, `GetLearningData`'s VBO, `IsValid` of a large binary.  The
  booted host never registered the store companders (the ROM does it in
  `RegisterROMDomainManager`, NOT YET) - `HostMountStores` does now.
  ctest `host.NewtonVBO`.
- **An older store file still opens.**  A store written by the build
  before `a93d860` (`newton --store`, walked through the Setup
  assistant) boots on the new build with its user and fourteen soups, a
  VBO can be added to it (the `'ephemerals` list is made on first use)
  and is read back on the next boot.  `a93d860` changed only the
  companders' words, and no host store held compander data before the
  large objects - so nothing on an older file is read differently.

## 2026-09-29: packages round 2 - streamed sources and large objects

- **Streamed package sources** (`5b665c9`; `packages/PackageLoader.h`,
  `PartPipe.h`): `TPackageLoader` and `cPackageLoad`, the `'pipe'` world
  (`TPipeApp`, `TPipeEventHandler`, `TPipeEvent`), `CPartPipe`, and the
  manager's stream branches in `BeginLoadPackage`, `LoadNextPart`,
  `LoadProtocolCode` and `TPartHandler::Copy`.  A streamed frames part is
  one NSOF object.  `test_PackageManager`'s `TestStreamed`.
- **The store companders read big-endian** (`a93d860`): a root's
  chunk-table id and the table's block ids were read as native words.
- **Large objects** (`6b06f94`; `stores/LargeObjects.h`,
  `stores/host/HostLargeObjects.cpp`): the large-object layer over a host
  ROM domain manager that keeps each mapped object whole
  (DEVIATION).  `test_LargeObjects`.  The large binaries on top of it
  (`LBData` and the rest) are round 3.

## 2026-09-29: the 'dict and 'comm part handlers

- **`TDictPartHandler`** (`recognition/DictPartHandler.h`, 0x0008fa44,
  0x0008fd5c-0x00090284): a 'dict part's dictionaryList copied into the
  heap and registered with `AddDictionary`, the ids kept, and disposed of
  again on removal (`FDisposeDictionary`); `Expand` for a streamed part.
  DEVIATION: the part imported into a host area first (the frame part
  handler's import, now `ImportPackagePart`) and kept until removal.
- **`TCommPartHandler`** (`packages/FramePartHandler.h`, 0x0013a5d8,
  0x0013a760): an 'auto part whose configurations are registered with
  `RegCommConfigArray`.  ROM bugs kept: the 'auto installation's answer is
  dropped, and `RemoveFrame` looks for the configurations in the remove
  object, which never has them, so they are never unregistered.
- The newt world registers both in the ROM's order (`'form`, `'dict`,
  `'auto`, `'comm`; `'book` NOT YET).  `recognition` now links `packages`.
  `test_Dictionaries` and `test_Units` test them at the function level.

## 2026-09-29: packages - the plan, and units

- **The plan for finishing packages**, sized with `callgraph.py`, in
  `docs/next-steps.md` ("Now: finishing packages"): units, the `'dict`
  and `'comm` handlers, streamed sources, then large binaries on a store
  (which packages on a store, 1.x packages and most of the remaining
  package natives stand on), the `'book` handler over the book reader,
  and protocol parts (recorded, not portable).
- **Units** (`packages/Units.h`, 0x000cf868-0x000d1038): the export and
  import tables (`InstallExportTables`/`RemoveExportTables`,
  `InstallImportTable`/`RemoveImportTable`, `InitMPTableRegistry`, the
  comparers), the pending imports (`PkgPendingImport`/`RExPendingImport`,
  `RegisterPendingImport` x2, `FulfillPendingImports`,
  `RemovePendingImports`), `ResolveImportRef` - an import ref is a magic
  pointer of table 2 + the unit's slot - `AllocateExportTable`/
  `FreeExportTable`, `InitRExMagicPointerTables` (now run by
  `InitMagicPointerTables` through a hook: frames sits below packages),
  and the natives `CurrentExports`, `CurrentImports`, `PendingImports`,
  `FlushImports`, `GetExportTableClients`, `FulfillImportTable`, plus the
  trivial `BackupPatchPackage`/`RestorePatchPackage`.  The frame part
  handler installs and removes them; the boot's "units are not
  installed" line is gone and the ROM's five exporting parts register
  seven units.  `CSortedList::Insert` answers its error, as the ROM's does.
- DEVIATION: with no ROM domain, a part's import refs are resolved over
  its imported host area (`RelocateImportRefs`, the import-ref half of
  `RelocateFramesInPage`, reading the untouched words from the package's
  bytes - `TImportedObjectArea` now keeps them, `fBytes`), and
  `FlushPackageCache` relocates that area again.
- ROM bugs kept: `RegisterPendingImport`'s allocation test looks at the
  wrong pointer; `InstallImportTable` throws out-of-memory for a part
  installed twice; the removed export and import items are never freed;
  a part whose `InstallFrame` fails keeps its exports.
- **Host bug found**: `RemoveFramesPart` gives the area's memory back,
  and the next part imported can be given the same addresses, so the
  find-offset cache answered a new frame's slots from a freed map (the
  exporter's `_ExportTable` came back as its `_ImportTable`).  It now
  clears the cache.  `test_Units` builds an exporting and an importing
  part in the MessagePad's layout and installs and removes them in both
  orders; ctest `host.NewtonUnits` lists the ROM's units.

## 2026-09-28: the recognition system's last pieces - status complete

- **The orthographic learning** (`recognition/Ortho.h`, `Ortho.cpp`,
  `OrthoDB.cpp`, `OrthoTables.cpp` from romtable.py's `_2C16`; commit
  c7d5594): `ORCreateLearnInfo` records which stretches of the trace
  made each letter of a word read (the big-endian learn array, 'ORTL' in
  the training data), and `ORTraining` - from `XRWDoLearning` - trains the
  letters of the word the writer settled on into the 0x6000-byte
  letter-shape database (`TrainTrajectory`: the trace normalised,
  resampled at sixteen points, `FDCT16` each way, fourteen bytes kept;
  `SearchInDataBase`, `Occam`, `AddToDataBase`).  ROM bug kept: `Occam`
  reads an empty answer list's first entry.  DEVIATION: `SDiv` by nought
  answers nought; the learn array's cached parts pointer is left nought.
  (Beware `Repar`: its loop is a do-while the ROM runs fourteen times for
  sixteen points - a for loop comes out one short.)  `cursive.ns` turns
  `bigLearningEnabled` on, and a live run makes a 21-entry learn array and
  trains three classes (ctest `host.NewtonCursive` checks the trace);
  `test_Ortho`.
- **`AL_NextSet`** (Airus selector 8 for lexicons, with `AL_NextSetCB`;
  commit 4c43df9): what may follow a node as one string, each character
  once.  `test_Airus`.
- **`TEditView::TrackDistort`** over the polygon selection (commit
  915145e): `TPolygonHilite` (the selection's own copy of its points,
  `MakeHilite`/`MakeInkHilite`/`HiliteAll`, `ClickOptions` 1/+2/+4,
  `Encloses` over `LineHitRatio`, the two-pass `DrawHilites`,
  `DeleteHilited`, `AddHilited`, `UpdateBounds`, `SetPenSize`), commands
  0x43 (a point moved) and 0x4b (the pen size), and
  `TDataView::DrawHilitedData`.  A corner pressed in `HiliteClick` dices
  the shape into a copy, drags the corner and sends 0x43 when the pen
  lifts.  Host bug fixed: `TView::LocalOrigin` took the view's own
  contents origin where the ROM takes the parent's (a partly selected
  paragraph was diced to the wrong place).  `test_Views`' `TestDistort`.
  NOT YET: `HiliteTraced` (a traced part of a shape, ~7.5 KB), and with
  it the partial `RemovePoints` and command 0x44.
- **`RotTiledBitmap`** recorded rather than reconstructed (commit
  7b38433): `Tilable`'s sizes are fax pages (216-byte rows by 1146, 2292,
  1152 or 2304), not the screen as the comments had it; the tile turn
  works on a large binary on a store with its compander, and the host
  has neither large binaries nor a fax receiver.
- **The French/German accent checks** (`CheckDiacriticsDirections`,
  `AnalyseDiacriticsDirection`, about 3.2 KB) left: only a French or
  German letter set asks for them.
- `docs/recognition/README.md` now opens with the system's status and
  the short list of what is left and why.

## 2026-09-28: a field's base line and grid reach the cursive engine

- `TWordRecognizer::ConfigFromFrame` (0x00167158) split out of
  `ConfigureArea` as the ROM has it, now handing the 'STXR' domain a
  configuration's `rcBaseInfo` (`FromObject` 0x00035830, `GetWordGeom`
  0x001686ec over `gTabScale.y`) and `rcGridInfo` (`FromObject`
  0x0003598c, `GetGridGeom` 0x00167010).  ROM bug kept: the heights and
  spacings keep only their low byte.
- Host bug fixed: the STXR domain's selectors 0x2000b/0x2000c and
  0x2000d/0x2000e copied the wrong way round (the ROM's memcpy takes the
  destination first; b and d answer, c and e set).  Tests:
  `test_WordDescriptors`' `TestGeometry`, `test_RecConfig`'s FromObject.

## 2026-09-28: the digit reader's merge - written numbers read

- `recognition/ChunkMerge.cpp`: `ChunkPatchXrdata`, `ChunkSortAnswers`
  (over the unnamed sort 0x002a4c04 and the letter boxes 0x002a4a34) and
  `ChunkCorrectByLexDB` (the lexical-database walk with its confusable
  characters and backtracking stack, 0x002a5414-0x002a55bc and
  0x002a7078-0x002a7168; the date check over `kChunkMonthDays`,
  romtable.py 0x0037ae10; the x rule), all from the disassembly.
  `GCTryToRecognize` now calls `ChunkProcessor`; `FillRecwordSplitInfo`'s
  number branch (0x0019e9e8) is real.  `src/host/demo/numbers.ns`: "42",
  "10", "217" written with the cursive letter set, the page reads "42 10
  217" and "217" is offered as "2/7" (ctest `host.NewtonNumbers`).
- `TParagraphView::HandleWord` divided by an empty word's length, which
  trapped on the host; the ROM's `__rt_udiv` throws evt.ex.div0
  (0x0038cb54), so the host now throws too.  The empty word came from the
  cursive reader before the merge existed; nothing produces one now.
- `packages.PackageIterator`'s intermittent failure was its test: the
  ROM's `TPackageIterator` never looks at a pipe's eof, so a package cut
  short is verified out of uninitialised memory (ROM bug, commented); the
  case is now a deterministic bad-processor directory.

## 2026-09-28: SearchDigit_S, Digits whole, CutNumberInDigits, ChunkProcessor

- **`SearchDigit_S`** (`recognition/ChunkSearchS.cpp`, 0x00290ed8, 45
  unnamed statics, all from the disassembly): the arcs pass, chunk by
  chunk (@, brackets, 9, the bars - 5 and crossed 7 with a bar, +, minus -
  and 3), the small marks (dot, comma, solidus), and the passes that
  settle the marks kept for later (stacked marks: 8, per cent, colon;
  bars; rings; dots).  `TestSearchS` draws each and checks what is read;
  `TestFindPound` now takes its bar from S.  The 7 with a bar is the
  crossed 7: a bar at the top of the stem is refused (the stem's foot
  must be 20-70% below the bar's end) and read as a minus.
- **`Digits`** (0x0029c94c) with all its statics and
  **`CutNumberInDigits`** (0x002a75b0) (`recognition/ChunkDigitsMain.cpp`):
  the doubtful digits taken out (which is what writes a 0 V and S both
  read out once - running S in the number test before these were done
  wrote "100"), the cells, the verdicts (area code in brackets, lone #,
  lone digit written its usual way, SearchNumber), the tagNumBoxes, the
  runs of other strokes, and the words taken for numbers ("is", "good").
  `TestDigits`.
- **`ChunkProcessor`** (0x002a6b50, `recognition/Chunk.cpp`), tested
  directly (`TestProcessor`) and **not yet called**: wired in, "42" and
  "10" written on the Notepad with the cursive letter set were found as
  numbers, but the narrowed configuration leaves the low level no xrs
  and the merge that makes the readings (`ChunkPatchXrdata`,
  `ChunkSortAnswers`, `ChunkCorrectByLexDB`) is NOT YET, so the empty
  word crashed `TParagraphView::HandleWord` (a divide by the word's
  length).  The call stays out until the merge is in.
- ROM quirks kept, the notable ones: `HWRAbs(0)` again (S's span tests),
  a pair of bounds no positive span meets (S's 9 tail), a subclass
  compared with a chunk kind (the three-chunk @), the bracket counter
  reading the stack entry after the one it filled (DEVIATION: nought).


## 2026-09-28: the digit reader's chunk searcher, SearchNumber and FindPound

- **`New_SearchDigit_V`** (`recognition/ChunkSearchV.cpp`, 0x00296e04 and
  its thirteen unnamed statics, the five named ones at 0x0028eb9c-
  0x0028fa14, `ComposeTrace`): each chunk asked by its class what it
  starts - 1, 7, H, 2 from a line; 8, 1, 7, 9, 2 from a curve down; 0, 6,
  9, 5, 7, 2, 8 from arcs and circles; 2, 5, 3 from an S; 3 from three
  brackets; #, <, > and the other sign codes.  All from the disassembly.
  ROM bugs kept: the horseshoe's bar read from the chunk `first` places
  on; a direction taken from (x, x); a tail compared with half a circle's
  width; a height from node `fKind`.  DEVIATION: a nil chunk's fields read
  as nought (the ROM reads low memory).
- **`SearchNumber`** (`recognition/ChunkNumber.cpp`, 0x002a28d0 and
  twelve statics): the statistics over the digits the second looks wrote
  out and the verdict over them.  ROM quirks kept: the too-wide mark is
  overwritten at once by a milder one; the deleted-skipping walk answers
  a deleted object at a list's end; the sign check with no stroke to
  compare answers the caller's register.
- **`FindPound`** (`recognition/ChunkPound.cpp`): a bar and the stroke
  before it as a pound sign.  ROM bug kept: directions wrapped by 23.
- `test_Chunk`: `TestSearchV` (a drawn 1, 0, 2, 3, 6, 7, 9, #, <, >),
  `TestSearchNumber` (L, K, V and the second looks run as `Digits` runs
  them: 42, 10, 217 and 11 numbers, a lone 2 and stepped 1s not),
  `TestFindPound` (a drawn pound sign found - its base must be wavy: the
  ROM wants the foot below the base's crest).
- ctest 112/112; coverage 11939 citations, 0 bad; 6852 of 16671 functions
  (41.10%).

## 2026-09-28: the digit reader's second looks and SearchDigit_K

- **The second-look pass** (`recognition/ChunkSecondLook.cpp`, ROM
  0x0029ce20, the twelve statics it runs and the variant test
  0x002a01dc): the digits found sorted left to right, corrected by their
  neighbours (commas, full stops, solidi, brackets, guillemets, B and D,
  the bars of other strokes, digits the unused strokes beside them rule
  out, variants the field does not allow), then ThreeToFive and
  RecognizeZCCW, the digits and gaps written out as class 1900.  All from
  the disassembly - the decompiler lost the two stack arrays the pass
  keeps its digits in and the argument order of the twelve.
- **`SearchDigit_K`** (`recognition/ChunkSearchK.cpp`, 0x00289604-
  0x0028d9d0, twenty functions, and `find_direct_forward`/`_backward`):
  #, x, the 4 in three ways, 7, %, 8.  The statics' names are ours (the
  ROM has none): what each one recognises was read out of its tests.
- ROM quirks kept: the solidus test ends the whole second-look pass at a
  lone "1"; the last digit is never counted among the 2s; the ring test's
  "chunk before" is the first arc; the arcs' flatness loop reads one
  bracket; the 4 of value 44 is never made; nothing bounds the gaps the
  pass keeps (DEVIATION: the host stops at the end of the ROM's block).
- `test_Chunk`: `TestSecondLookPass` (order, gaps, seven corrections) and
  `TestSearchK` (a 4, x, 7, #, 8, % drawn and found; a 7 is found only
  with its bar 17..20 steps against its stem, as a hand writes it).
- ctest 112/112; coverage 11904 citations, 0 bad.

## 2026-09-28: the digit reader's first searcher

- `Digits`' top level read; done under it (all from the disassembly,
  `recognition/ChunkDigits.cpp`, `ChunkSearchL.cpp`): the writing's line
  (`DefHeightsForNumber` - each chunk's ends placed 60/45/30 in bytes at
  +0x44/+0x50 of what had looked like two words), the circles
  (`GetCircles`), `SearchDigit_L` and its seventeen statics (a $, a 2 or
  7, a 5 with its bar, a 4 or 9, a 3, 5 or 9), the geometry the
  searchers share, and the second looks `ThreeToFive`, `RecognizeZCCW`
  and `Check_4`.  A digit found is a class-1300 object, value 1400 + the
  digit.
- ROM bugs kept: `HWRAbs(0)` in the box joiner (no boxes are ever
  joined); the 4 test that asks a direction and throws the answer away;
  the $ test that compares a boolean with an eighth of the height;
  `ThreeToFive` not forgetting its turns between digits.
- The decompiler dropped arguments to several divisions (`__rt_sdiv(6)`),
  read `__rt_sdiv`'s quotient as its remainder once more, and lost
  `HWRAbs`'s argument; every function here was read from the assembly.
- `test_Chunk`: a 0 has one circle; a 5 with a separate bar, a 5 in one
  stroke and a $ are found; an unlifted 5 is turned from 3 to 5; a 4 laid
  over another digit is taken out.

## 2026-09-28: the digit reader's chunks

- The cursive reader's digit reader, steps (1) and (2) of its plan and
  the start of (3): the trace's turns (`ExtrWordTrace_V`) and polyline
  (`GetLineApprox`, `SetAllDirections`) in `recognition/ChunkTrace.cpp`;
  the list of low objects (`LO_*`) in `ChunkLowObj.cpp`; `ChunkConstruct`
  and everything under it - chunks, strokes, brackets, the chunk classes,
  the unnamed reclassing pass (from the disassembly) - plus
  `ChunkPutClassesToLO` and `DefRectForChunks` in `ChunkConstruct.cpp`.
  `tag_WORD_TRACE`'s +4 is a word of flags (it had been two shorts);
  `tag_wapx_type`, `tag_CHUNK`, `brack_type`, `tag_STK` keep the ROM's
  layouts, `LOBlock` and `tag_CHUNK_STAFF` are host layouts (pointers).
  ROM quirks and bugs kept: the "median" height one past the middle, a
  dead order check, a split clearing the wrong node's first flag,
  `LO_Add` losing the class worked in when full, `LO_GetRealChunkInd`'s
  group count, `DefRectForChunks` writing four words through a `_RECT`
  (the low level's `_RECT` is four halfwords: two types of one name).
  The ROM's `memcpy` copies top-down only when the source is below, so
  the brackets' removal is a `memmove`.  `test_Chunk` (new) draws a 4
  and a 2.  Found: `SearchDigit_L` is not small - it calls seven statics
  inside `RecognizeZCCW`'s and `DgtFromDnHorseshoe`'s extents, and reads
  staff +0x40, which `Digits` sets - so step (3) starts at `Digits`.

## 2026-09-28: the cursive reader's leftovers; the digit reader begun

- `SetStrXrRC`: a recognition configuration's `strxrCommands` carried out
  on the strokes-to-xrs block (byte commands reach the host's own fields
  by their ROM offsets; ROM quirk kept: commands 0x45 and 0x46 both name
  +0x54).
- The readings of a graph of alternatives (a fixed-string field's):
  `MakeRecWordsFromGraph`, `MakeNewPath`, `FillRecWordsElement`,
  `MergeTwoRecWordsSets`, and `EvaluateAnswers`' two passes (a number's
  readings and a word's) merged - all from the disassembly; ROM bug kept:
  a letter read as another clears the reading's first variant.
- Learning: `TWordRecognizer::DoLearning` hands the pen's trace
  (`GetTraceFromStrokes`, there all along - the NOT YET named a wrong
  address) to the word domain; host bug fixed: `UnitID` read a host ULong
  out of a unit id written as two UniChars, so `DoIndexedLearning` found
  no recogniser and crashed.  `cursive.ns` now learns "ton" and the
  letter weights move (ctest `host.NewtonCursive`).
- The digit reader sized (102 functions, 150 KB) and planned
  (`docs/next-steps.md`), and begun: its context and the configuration it
  narrows (`Chunk.h`; ROM bug kept: `ChunkRestoreRC` leaves rc +0x92), and
  `v_MostFarFromChord`, `v_QDistFromChord`, `GetDirection` (sines and
  cosines generated into `ChunkTables.cpp`).
- Deferred: the orthographic learning (`ORCreateLearnInfo`/`ORTraining`,
  about 17 KB with its letter-shape database), which the Notepad never
  reaches.

## 2026-09-28: the cursive readings put right (round 11)

- **Port bug: FillSHR's bracketing xrs** (`recognition/LowXrFeatures.cpp`,
  ROM 0x0027ea0c).  The ROM stores the four xrs that bracket each xr at
  `[sp+0x194]`, `0x198`, `0x19c`, `0x1a0` and reads them back as an array;
  the port had assigned the last two the other way round in all six
  cases.  The height classes were unaffected (absolute differences) but
  every shift class was negated.  Fixed; `test_LowLevel`'s `TestFillSHR`
  pins it (a maximum between two minima: height 9, shift 11 - the old
  order gave 4).  The printed-stroke `cursive.ns` now reads "ton to" (was
  "For to").
- **How it was isolated**: `test_XrMatrix`'s `TestIdealWords` reads
  "ton", "on", "no", "mum", "to", "nun", "lo" made of their letters'
  ideal xrs, capitals allowed (the Notepad's rc +0x1e 0x3f) and not -
  each reads as itself first, so the matcher, `xrlv` and the capitals
  were sound.  `SetXrWordFieldType`'s capitals and exchange's
  element-to-xr switch were checked against the disassembly case by case
  and agree; FillSHR did not.  PhatWare's GPL release of a descendant of
  ParaGraph's recogniser (`docs/next-steps.md`, "A reference for the
  cursive reader") named the xr types and showed the disagreement - it is
  a reference for meaning only, never transcribed.
- **The joined-up demo's o** now arrives at its top right and goes over
  the top, as a cursive o does (it went up to the top centre and straight
  down the left, leaving no top arc): `cursive-joined.ns` reads "on" 82,
  "no" 90, "Mom" 67 ("mum" fifth), "to" 86, "nun" 76 (were "OR", "bb",
  "maps", "to", "Rap").
- **"mum" and "nun" lose to the scrub gesture**: their synthetic stems
  are retraced exactly, three or more alternating turns over 110 degrees;
  `TestScrub`/`ValidTurnSequence` agree with the ROM, so it is the
  drawing; the scrubs erase nothing and the strokes go down as ink words
  (0x1a in the paragraph's text).  New host trace `NEWTON_TRACE_ARBITER`
  (`tools/host/README.md`) prints each arbitration's units, scores and
  winner.

## 2026-09-28: the cursive reader's post-processing (round 10)

- `EvaluateAndSortAnswers` (0x00337ee8) real (`recognition/XrPost.h`,
  `XrPostCalc.cpp`, `XrPostEval.cpp`): the letter table's rules turned out
  to be bytecode for a stack machine (`CalculateQueueResult`, transcribed
  from the disassembly - the decompiler cannot follow its switches), with
  74 functions in a table (`Functions`, generated with the bytecodes'
  lengths); `EvaluateCharQuality`, `EvaluateAnswers`, the side reasoning
  (two tables in the initialised RAM area, generated by address), the
  letter boxes, the missing crosses and `CheckDigitsLine`.  ROM bugs kept
  (`GetXrCorr`'s whole bytes, `ReturnZeroIfDoubleSkip` that can never
  succeed, `CalculateCurvature`'s half-length "middles", `CalculatePow` for
  a negative power).  NOT YET: the diacritics' directions (French/German
  letter sets) and a fixed-string field's readings.
- It scores only close calls between good answers, so `cursive.ns` reads
  as before.  A joined-up demo, `cursive-joined.ns` (ctest
  `host.NewtonCursiveJoined`), writes five words in one stroke each; the
  answers are recorded in the recognition README ("Joined-up writing").
- An o's rule decoded by hand from the trace (`NEWTON_TRACE_RULES=1`
  prints a queue's bytes) - the evidence that the bytecode is read as the
  ROM reads it.
- `romtable.py` writes the bytes of a C string outside printable ASCII as
  octal escapes (the side-reasoning tables hold Mac Roman letters).

## 2026-09-28: the cursive reader's answers - the first cursive word typed (round 9)

- **The readings** (`recognition/XrAnswers.cpp`):
  `MakeAndCombRecWordsFromWordGraph`/`MakeRecWordsFromWordGraph` - the
  word graph made into readings, scored again from the letters, sorted
  (the graph with them), scaled to 0..100 and cut (rc +0x1a, +0x16).
- **Which strokes each word is**: `FillRecwordSplitInfo`,
  `connect_trajectory_and_answers`/`_letter` (and the unnamed
  0x002d48cc), `GetStrokeNumber`, `GetBegEndOfStroke`,
  `AddStrokesOfSymbol`, `AttachLostStrokeToWord`, `FillSplitInfoFromRWG`.
  ROM bugs kept: `FillSplitInfoFromRWG` tests the next symbol's `sym`
  where its `type` was meant; the stretches leak when a stroke belongs
  to an earlier word.
- **Training data**: `LHAddEntry` (an entry added by making a new block)
  and `GCFillLearningHandle`.  DEVIATION: the 'LDRC' entry is the host's
  parameter block, sized by sizeof.
- **The word domain reads**: `TXrWordDomain::Group`/`Classify`/
  `Reclassify`/`ClassifyXrWord`/`Dispose`, `TXrWordUnit` (Make,
  IXrWordUnit, IDispose, GetWordBase/Slant/Size, GetTrainingData,
  DisposeTrainingData) and `GetTraceFromStrXrUnit`.  DEVIATION: a word
  unit's interpretations sized by sizeof (the ROM's are 0x10 bytes) -
  the first run with 0x10 read a stray handle in `SetWordBase`.
- `GCTryToRecognize` goes on after the graph: readings, split
  information, training data, and answers nought.  `cursive.ns` types
  "For to"; `test_XrAnswers` (new) and ctest `host.NewtonCursive` check
  it.  `EvaluateAndSortAnswers` is NOT YET.
- **The rules' whereabouts** (`recognition/XrRules.cpp`): `PDFGetRule`,
  `PDFGetCharAddress`/`VarAddress`/`ConnectionAddress`/`RuleAddress`,
  `PDFReturnNumberOfBits`/`Index`/`BitNumber`; `pdfMaskArray` from
  romtable.py (`XrRulesTables.cpp`, in regenerate.py).  The ROM's rules:
  87 characters, 374 variants, 63 connections.
- Why the synthetic "ton" reads "For": the capitals flags allow a capital
  at every word start (rc +0x1e = 0x3f) and the vocabulary is there (rc
  +0x08 = 0x0f); "ton" is not among `xrlv`'s five answers - decided before
  the NOT YET re-scoring.  The cursive trace prints the flags.
- `docs/recognition/README.md` had 88 cp1252 dashes in the middle of its
  UTF-8 (an earlier edit's); they are UTF-8 again.

## 2026-09-28: the cursive reader's xr reader (round 8)

- **The matrix** (`recognition/XrMatrix.h`/`.cpp`): `xrcm_type` and its
  lines, `CountWord`/`CountLetter`/`CountSym`/`CountVar`/
  `MergeVarResults`, the trace (`TraceAlloc`, `TDwordAdvance`) and the
  layout (`CreateLayout`), and the hand-written assembly column loops
  `CountXrAsm`/`TCountXrAsm` (read with disasm.py: the prototype's first
  word rotated by eight, the xr read as two words; the traced one breaks
  ties the other way).  DEVIATION: the trace is carved on eight-byte
  boundaries so the pointers in it are aligned.
- **The Viterbi** (`Xrlv.cpp`): `xrlv` and every `Xrlv*` function;
  `XrlvCHLXrlvPos` from the disassembly (its stack rectangles lost by the
  decompiler).  ROM quirks kept: a constant (0x2a5778) standing for a
  single-letter word's letter before, the two-back size check measuring
  the overlap against the letter just before, a first letter's capital
  penalty booked against the last, the symbol cache overrunning into the
  next symbol's entry (DEVIATION: slack after the last).
- **The dictionaries** (`XrLex.cpp`): `GF_VocOrLexSymbolSet`,
  `Enum_fcn9CB`/`Lex_fcn9CB`, `GetWordAttributeAndID`,
  `AssignDictionaries`; Airus gained `AEnum_NextSet9` (selector 9 routed)
  and `AL_NextSet9`.
- **The graph** (`XrWordGraph.cpp`): `xrw_algs`, `create_rwg_ppd(_node)`,
  `GetCMPAliases`, `fill_RW_aliases`, `SortGraph`, `FreeRWGMem`,
  `GetSymBox`, `GetBaseBord`; and `SetMultiWordMarksWS`/`Dash`
  (`CursiveReader.cpp`).  `ParaGraph.cpp` gained `HWRStrChr`,
  `HWRStrRev`, `IsPunct`, `GetVarRewcapAllow`, `GetVarPosSize`.
- `GCTryToRecognize` now calls `xrw_algs`: `cursive.ns`'s "to" comes out
  of the graph as "to" first.  The answers are NOT YET, so it still ends
  as ink (-9).
- Tests: `test_XrMatrix` (new), `host.NewtonCursive` checks both words'
  graphs.  ctest 109/109; coverage 11599 citations, 0 bad, 6624 of 16671
  functions (39.73%); open-apps: only the Sound Recorder fails;
  `cursive.ns` clean under `NEWTON_HEAPCHECK=5` three runs out of three.

## 2026-09-28: the cursive reader's low level whole

- `FindDArcs` and its group (`recognition/LowDArcs.cpp`, 17 functions,
  from the disassembly): an upper element and the lower one after it
  described in an `SZD_FEATURES` block and judged an S or a Z
  (`CheckSZArcs`: a new element 0x23/0x24 between the arcs) or the sides
  of a d's bowl (`CheckDArcs`, `CheckBackDArcs`: 0x25/0x26), the sticks
  either side turned into arcs.  `CheckDArcs` reads two box widths
  through unaligned loads that take the halfword before the one named.
- `xt_st_zz`, `AnalyzeLowData` and `low_level` themselves: the low level
  is whole, and `GCTryToRecognize` calls it.  With a cursive letter set
  "ton" is cut into 13 xrs and "to" into 8; the reader stops at
  `xrw_algs` (-9, the word marked 0x400).  `test_LowLevel`'s `TestDArcs`
  and `TestLowLevelWhole`; `test_WordDescriptors` and ctest
  `host.NewtonCursive` now expect -9; six runs under `NEWTON_HEAPCHECK=5`
  clean.  Stage 3 (`xrw_algs`, 65 functions and about 28 KB not done)
  sized and planned in `docs/next-steps.md`.

## 2026-09-28: the cursive reader's lk_duga, and xt_st_zz but FindDArcs

- **lk_duga** whole (`recognition/LowLkDuga.cpp`): `prevent_arcs`,
  `conv_sticks_to_arcs`, `del_before_after_circles` and the loop
  neighbours over `NxtPrvCircle_type` (15 functions), and
  `delete_UD_before_DDL`; `cos_horizline`, `xHardOverlapRect`,
  `yHardOverlapRect`, `HardOverlapRect` (`LowGeometry.cpp`), `brk_left`
  (`LowSide.cpp`).  `TestLkDugaWhole`: the uou's o is left as its loop
  0x22, the crossing lk_cross coded (too short a loop) taken out.
- **xt_st_zz's passes** (`recognition/LowXtSt.cpp`, new): 50 functions,
  about 30 KB, every one from the disassembly - the late strokes
  (FindDelayedStroke, placement_XT_ST and its four placements, DoubleXT,
  the quotes, punctuation, insert_drop, RestoreApostroph with four
  unnamed helpers, IsNearI), find_umlaut, find_angstrem, placement_X,
  FindMisplacedParentheses, the breaks (make_different_breaks,
  GetDxBetweenStrokes, GetTraceBoxInsideYZone, CalcDistBetwXr),
  del_close_MAX_MIN, redirect_sticks, CheckSequenceOfElements and the
  rest.  `TestXtSt`.  NOT YET: `FindDArcs`'s group and `xt_st_zz` itself,
  so none of it is called yet.
- Worth knowing for what is left: the unaligned-halfword trap again -
  `conv_top_elem_to_ST` works out a box's width and height by
  subtracting two unaligned words, whose low halves are the right less
  the left and the bottom less the top (the decompiler says the
  heights); `RestoreApostroph`'s widening of the dot's box the same way
  moves its left and right, not its top and bottom.  Several functions
  here use the element after one in the specl array (its crossing
  partner, or simply the next slot) as scratch: `insert_drop`,
  `DoubleXT` (two after), `O_GU_To3Elements`.

## 2026-09-28: the cursive reader's first xr stream; colons; lk_cross

- **exchange** (`recognition/LowExchange.cpp`): the special elements
  written as xrs - each code to an xr type by its height band, the
  penalty (`AssignInputPenaltyAndStrict`), the link to the next
  (`GetLinkBetweenThisAndNextXr` over `CalculateLinkWithoutSDS`,
  `CalculateStickOrArc`, `CalculateLinkLikeSZ`), last-in-letter
  (`MarkXrAsLastInLetter`), the points mapped back to the original
  trace and each xr's box, and `check_xrdata`/`PutZintoXrd` putting in
  the crossing a gap stands for.  The ROM copies GetBoxFromTrace's box
  into the xr with unaligned loads whose low half is the halfword before
  the address (a load at the top's address yields the left): the order
  that comes out is left, top, right, bottom.  The ROM's memcpy copies
  from the top down when the source is below the destination, so
  PutZintoXrd's overlapping move is a memmove.
- **FillXrFeatures** (`LowXrFeatures.cpp`): the writing's slant
  (`GetCurSlope`), each xr's height class and shift over the four xrs
  that bracket it (`FillSHR`; two limit tables that were function-local
  arrays copied onto the stack, generated as `kSHRRatioLimits`/
  `kSHRShiftLimits`), and its direction (`FillOrients` over `GetBlp`,
  `GetVect`, `GetAngle`).  ROM quirk kept: a stroke end first in the list
  would read the merits' byte before the first, which is the last shift
  class's (the two arrays are laid out one after the other).
- **RestoreColons, PostFindSideExtr** (`LowRestore.cpp`): two dots one
  over the other found to be a colon and moved to the break nearest
  their middle across; the side bends found after the codes were given.
- **lk_cross** (`LowLkCross.cpp`, 32 functions): `analize_sticks`,
  `analize_circles` (with `CrossInfoType`/`FillCrossInfo`,
  `GetMaxDxInGamma`, `Isgammathin`, `CheckSmallGamma`,
  `Decision_GU_or_O_`, `IsDUR`/`IsShapeDUR`, `is_DDL`), and
  `del_inside_circles` (`IsOutsideOfCrossing`, `CheckInsideCrossing`,
  `IsInnerAngle` over `IsRightGulfLikeIn3`, `Restore_AN`), with the
  point-in-polygon test `IsPointInsideArea`/`IsPointOnBorder` (its ray
  runs from x = 1 to the point).  Two places read an element's array
  neighbour as its crossing partner (`SPEC_TYPE` + 1, as the ROM's +0x14
  and +0x16 loads do).
- **lk_duga's first passes** (`LowLkDuga.cpp`): `arcs_processing` folds
  an extremum that is only the tip of a stroke's end into it (the end
  becomes an arc, 9..0xc, or the extremum takes the end's mark), over
  `DyLimit`, `IsDx_Dy_in_arcs_OK`, `IsDx_Dy_in_tips_OK` and `IsTipOK`;
  `delete_CROSS_elements` takes out the loops too short to be letters
  (`ins_third_elem_in_circle` keeping a tall one as 0x1b or 0x17);
  `check_IUb_IDf_small` sets a stick's band.  `lk_duga` itself and the
  circle-neighbour passes are NOT YET.
- `test_LowLevel`: `TestExchange`, `TestRestore`, `TestLkCross`,
  `TestLkDuga`.

## 2026-09-28: the cursive reader's low level, round 4

- `Circle` (`recognition/LowCircle.cpp`): the loop finder - a foot
  between two tops tried as an o, a, d, g, b or e's loop, the nearest
  pair of points on the way down and up (`Clash_my`) judged by the
  yardsticks `Ruler0`/`circle_type` work out, a closed loop marked as a
  crossing pair 'c'/'d'.
- `FindSideExtr` (`LowSide.cpp`): a side's bend (`SideExtr`,
  `IsTriangledPath`, `TriangleSquare`, `ClosedSquare`, two unnamed
  helpers) moving a hooked stroke start or end.  coverage.py wants an
  unnamed function cited exactly `(unnamed)`, the note after it.
- `Cross` (`LowCross.cpp`): the crossing finder (`Grab`, `Clash`,
  `DrawEnds`, `ChkMrgCrs`, `AnyCrosCont`) over the `eps0`..`eps3` tables;
  ROM quirk: a 9's end is copied from `ipoint0` through an unaligned
  `ldr`'s low half.
- `lk_begin` (`LowBegin.cpp`): the elements' codes (`init_proc_XT_ST_CROSS`,
  `process_ZZ`, `process_AN`, `process_curves`, `DefineWritingStep` over
  `delta_interval`); ROM quirks: the break between strokes is written
  into a freed array slot, and `process_ZZ`'s join is unreachable.
- `Adjust_I_U` (`LowAdjust.cpp`): a narrow bottom recoded an i's (7) or
  a u's (8).
- `low_type`'s +0x70/+0x72 named (`fStep`, `fStepKind`); `const1` is 26
  shorts, not 8.
- Tests: `test_LowLevel`'s `TestCircle`, `TestSides`, `TestCross`,
  `TestCodes`, `TestIU`.  ctest 108/108; coverage 11359 citations, 0 bad,
  6402 of 16671 functions (38.40%); open-apps: only the Sound Recorder
  fails; `cursive.ns` under `NEWTON_HEAPCHECK=5` clean.

## 2026-09-28: the cursive reader's Pict and angl

- `Pict`, AnalyzeLowData's first element finder, whole (`LowPict.cpp`,
  59 functions, about 34 KB): the stroke descriptions (`_SDS_TYPE`,
  `iMostFarDoubleSide`, `StrElements`, `RareAngle`), the heights
  (`BildHigh`, `RelHigh`), the level straight strokes (`SPDClass` over
  `FieldSt` and the trained `maxA/maxCR/minL_H_end` tables, `YFilter`),
  dots (`Dot`, `maxX/maxY_H_end`), the upright sticks (`VertStickBorders`,
  `VertSticksSelector`), the hatch finder (`HatchureS` and its eighteen
  helpers), `InStr`, `SlashArcs`, `FantomSt`, `FillCross`, `Recount`; and
  `angl` with `store_angle`/`angle_direction`/`cos_vect` (`LowAngles.cpp`).
  Commits a95c376, d02ec5a, 87c4fa2.
- Found on the way: ParaGraph's mark 7 is a level stroke (a dash or a
  bar), not an upright stick - the trained tables refuse anything steep;
  the decompiler's handling of unaligned halfword loads misnames fields in
  a dozen places (`CrookCalc`, `FillCross`, `HatchureS`), so every such
  copy was taken from the disassembly.
- ROM reads of unset or out-of-range memory in `SlashArcs`, `LowStFiltr`
  and `RMinCalc` are replaced by nought or -2 (DEVIATION), `FantomSt`'s
  division by a zero-length line guarded.
- test_LowLevel: `TestPictPieces`, `TestPict` (a word through the base
  line into Pict), `TestAngles`.  ctest 108/108; coverage 11301
  citations, 0 bad, 6353 of 16671 functions (38.11%).

## 2026-09-28: the cursive reader's base line

- `transfrmN`, the base-line finder, and everything under it:
  `LowPunct.cpp` (the stroke tests - commas and brackets, leading and
  trailing punctuation, an i's dot, an umlaut, a bar, a t's stem - and
  `extract_all_extr`), `LowGeometry.cpp` (`QDistFromChord`, `is_cross`,
  `FindCrossPoint`, `cos_pointvect`), `LowLine.cpp` (the gaps and glitches
  in a line of extrema and what they are made: `find_gaps_in_line`,
  `find_glitches_in_line`, the three `glitch_to_*`, `all_susp_extr`,
  `bord_correction`, `num_bord_correction`; tables `TG1`/`TG2`/`H1`/`H2`/
  `CS` generated), `LowClassify.cpp` (`classify_strokes`,
  `classify_num_strokes`, `numbers_in_text`), `LowBorders.cpp`
  (`SpecBord`, `calc_med_heights`, `FillRCNB`, `line_pos_mist`,
  `transfrmN`) and `BaselineAndScale` (`const1` generated).  Then
  AnalyzeLowData's first passes (`LowAnalyze.cpp`).  45 functions, about
  44 KB.  `test_LowLevel`'s `TestBaseline`: synthetic arches come back
  with the right height and lower border and the trace rescaled to them;
  an ascender and a descender are found and left out of the lines.
- Every one of these was read from the disassembly; the decompiler lost
  most of their conditions (its `SBORROW4` chains, and a pointer loaded
  from a literal pool it inlined as constants - `BaselineAndScale`'s
  `const1`).  `EXTR`'s +8 turned out to be one short, the stroke's
  shift, not two bytes; low_type's +0x7c..+0x9b are the thresholds
  `DefLineThresholds` sets.

## 2026-09-28: the cursive reader's low level begun

- `recognition/LowLevel.h` (`LowLevel.cpp`, `LowFilter.cpp`,
  `LowExtr.cpp`, `LowBaseline.cpp`, `LowTables.cpp` generated by
  romtable.py): the `low_type` block and its memory, the strokes
  (`InitGroupsBorder`, `GetGroupNumber`), the trace utilities, the
  engine's integer roots, the filters (`Errorprov`, `PreFilt`, `Filt`,
  `PSProc`/`NewIndex`), the extremum finders (`Extr`, `BigExtr`,
  `DirectExtr`, `MarkSpecl`/`NoteSpecl`), the element list operations,
  and about thirty pieces of the base-line finder `transfrmN` (the
  smoothed lines, the suspect-extremum passes, the medians).  84
  functions, about 24 KB; 305 functions (189 KB) left below `low_level`.
  `test_LowLevel`.  `low_level` is not called yet.
- Read in the assembly where the decompiler went wrong: `BigExtr`'s
  direction is two shorts it showed as signed bytes (and it adds each
  coordinate with its weight's sign rather than multiplying, a quirk);
  `NoteSpecl` failed to decompile at all; `neibour_susp_extr`'s average
  is `__rt_sdiv`'s quotient where the decompile used the remainder;
  `ixMin`/`ixMax` tail-call `iMidPointPlato`, which the decompile lost.
- ROM bugs kept: `InitGroupsBorder` writes one group past the array when
  full; `GetGroupNumber` answers its argument's address for a point in
  no stroke; two functions read an unset register for an unexpected
  kind.

## 2026-09-28: the cursive lock-up was heap damage at boot

- `cursive.ns` locked up one run in three in the heap's compaction.  A
  new host heap walker (`NEWTON_HEAPCHECK`, `host/HostHeapCheck.h`: the
  newt heap walked after allocations and before each `DisposPtr` - block
  sizes, parents, master pointers, the free list against the free
  blocks - stopping with the C stack) caught it at boot, 196
  allocations in: `CreateTrigramHeader` (ROM 0x002d4cbc) asked
  `HWRMemoryAllocHandle` for the ROM's 0x98 bytes, and the host's
  `TrigramHeader` is 0xa8 (its three trailing words pointer-sized), so
  the memset zeroed the next block's header.  Allocated by `sizeof` now
  (DEVIATION).  It runs on every boot (`GetLetterWeights` ->
  `LIBeginWeights` -> the word domain's `LoadVocAndData`), which is also
  the one-off boot stall in `GetLetterWeights` noted before; the 'STXR'
  bisect had only moved the layout.  18 of 18 runs clean (3 of 6 locked
  up before); a whole boot checked at every allocation and open-apps
  every 20th are clean.  ctest `host.NewtonCursive`.
- ROM bug found by the same runs (`test_Newt` failing now and then under
  the sanitizer): `WordRecog`'s +0x68, the running mean of stroke
  heights, is never initialised - the ROM writes it only in the mean
  itself (0x002751d4) - so it starts as heap rubbish and its products
  overflow.  Kept, with the ARM's wrapping arithmetic
  (`WordRecogIsStrokeTooWide`, `WordRecogAddStroke2`).

## 2026-09-28: the cursive reader, stage 1 - writing reaches it

- The GC layer's word descriptors (`recognition/WordDescriptors.h`): the
  list of eight, the segmenter's words written into them, a word after a
  dash joined to the one before (`GCMergeWordDesc`), the trace a word is
  read from (`GCWDGetTrace`, `GCMergeLinesAndRemoveDash`);
  `GCGroupStrokes`/`GCTryToRemoveLastWords` take descriptors as the ROM
  does.  `GCTryToRecognize`'s frame (`recognition/CursiveReader.h`): the
  base line (`GCFillBaseLineParameters`, `SetRCB`, `GetInkBox`), the
  recognition data locked (`GCLockRecognitionData`,
  `TDictChain::LockChain`); `rc_type`'s +0xf8 and +0x108 are host
  pointers now.  `test_WordDescriptors`.
- The strokes-to-xrs domain and its unit (`recognition/StrXrDomain.cpp`):
  `TStrXrDomain` (classify, group on line and in boxes, `DomainParameter`
  bar `SetStrXrRC`, `SetParameters`, `SetStrXrFieldType`), `TStrXrUnit`,
  `CallGroupAndClassify`, `GroupAndClassifyStrokes`, `GCClassifyStrokes`,
  `GCReleaseRecResults`, `WriteRecResults`, `GCWriteRW`,
  `GCFillRecParmStruct`, `GCAllocRecTrace`; the word domain's
  `SetUpChains`/`AdjustRecParmStruct`.  `src/host/demo/cursive.ns` with
  `NEWTON_TRACE_CURSIVE=1`: "ton" and "to" in two words, each reaching the
  reader, which fails at the (NOT YET) low level.
- ROM bugs kept: extra strokes walked while the index is below the stroke
  number; the dash's removal renumbering the wrong entry; the caller's r8
  answered by `GCMergeLinesAndRemoveDash`; an uninitialised r6 tested in
  `GroupAndClassifyStrokes`.
- Found: an intermittent lock-up in the host heap's compaction with a
  cursive letter set, which goes away when the 'STXR' block does not load
  its own letter table (next-steps has the bisect).

## 2026-09-28: the test agent and the debug hooks

- `testing/TestAgent.h` (0x00226a40-0x0022bbb8, `docs/testing/README.md`):
  `TTestAgent`, the `'tagt` application world (`InitTestAgent`), its
  `'tste` event handler and idle proc, `TTestReporter`/`TAgentReporter`,
  `TMessageQueue`, the `'tstp`/`'tsps` part handlers; `TestNatives.cpp`:
  `ActivateTestAgent`/`DeactivateTestAgent`, the newt world's
  `TNewtTestScriptEventHandler`, the `Test*`/`TestM*` natives, `debug`
  (`FindForm`, `DebugHashValue`), `DebugRunUntilIdle`, `DebugMemoryStats`,
  `StdioOn`/`StdioOff`, `HobbleTablet`.  `RemovePackage` added to the
  package manager (packages on a store NOT YET, so it deinstalls).
- The journal is now played by the agent's idle proc, as on the machine:
  the host inker's `JournalAgentIdle` DEVIATION is gone (the unit tests,
  which have no agent, still call it).  `journal.ns` activates the agent
  as its test manager and plays the strokes as one stroke file - one at a
  time they came three seconds apart (the agent idles that long after a
  replay ends) and were read as four words.
- ROM bugs kept: `TestReportErrorValues`/`AgentReportDirect` formats
  short of arguments, a data file asked of the manager also queued with
  an unset kind.  DEVIATIONs: natives answer nil rather than report
  through a nil reporter; the queue pointer cleared when the agent goes.
- ctest `host.NewtonTestAgent` (`src/host/demo/testagent.ns`).  Testing
  natives: 32 of 38 answered.

## 2026-09-28: a read word no longer also left as ink

- The journal demo's replayed "tor" was followed by an ink word of the
  same strokes.  Not the journal: any word written away from the text
  with remote writing on (the caret path of `TEditView::HandleWord`) did
  it.  The ROM keeps the best child in r8 and never clears it, so on the
  two caret paths it answers its caller's r8 - in `HandleWordUnit` the
  word's text pointer.  The host started from nil, so `HandleWordUnit`
  answered false, the aeWord's result was 0, the unit handler did not
  claim the unit, `TArbiter::DoArbitration` marked the at-once winner
  claimed and invalid, and `CleanUp` expired its strokes into ink.  Now
  ported as the ROM does it (the bug commented); `journal.ns` and ctest
  `host.NewtonJournal` check the page reads "ton tor" and nothing more.
- Checked on the way: the ROM bug `DoArbitration`'s last loop carries
  (marking the unit in hand, not the gathered entry) is real - r5 is never
  reloaded (0x002089ec).

## 2026-09-28: the journal - strokes recorded and played back

- `testing/Journal.h` (a new area, `src/testing/`, `docs/testing/
  README.md`): `JournalRecordAStroke` (called by `StrokeCentral::
  IdleStrokes` while recording), `JournalReplayHandler` (timing,
  `GetNextTabletSample`, the stroke file), `JournalInsertTabletSamople`,
  `JournalStopReplay`, and the natives `JournalStartRecord`,
  `JournalStopRecord`, `JournalReplayAStroke`, `JournalReplayALine`,
  `JournalReplayStrokes`, `JournalReplayBusy`; the tablet's bypass
  (`StartBypassTablet`/`StopBypassTablet` over the host tablet's driver
  state, the window's pen ignored while bypassed) and `InsertTabletSample`.
- The test agent that plays the journal on the machine is NOT YET: the
  host's inker task runs its idle proc's journal half every tick
  (`JournalAgentIdle`, DEVIATION).  Journal binaries keep their words
  big-endian (DEVIATION, so a journal is portable).
- ROM quirks kept: a replayed stroke's last point is replaced by the
  pen-up (the demo's "ton" comes back as "tor"), the bypass outlives the
  replay, a format 1 JournalStroke's binary has eight bytes too many, a
  format 2 stroke is moved in the wrong words.
- ctest `host.NewtonJournal` (`src/host/demo/journal.ns`), `test_Views`'s
  `TestJournal`.

## 2026-09-28: PictToShape - a picture turned into shapes

- `PictToShape` (0x000dd6dc) over `DrawPicture`'s toShapes path: the
  `OpcodeProcs` table and its eleven procs, `storeShape`/`flushShape`,
  `MungeStyleFrame`, `StylesEqual`, `GetNSFont`/`StyleToNSFont`/
  `GetNSPattern`, `FlushAnyInk`, `ImpossibleToDraw`, `MapFPoint`
  (`views/PictureShapes.cpp`, `qd/PicPlay.cpp`; `docs/qd/README.md`'s "A
  picture turned into shapes").  `ParsePicCodes` now fills the ROM's
  `fProc*` fields and calls the procs where the ROM does (including the
  second, positive call after a state opcode), keeps 0x81a1's style and
  0x81a3's text for them, and `DrawPicture` answers the shapes.
- The pattern forms both ways: `MakeNSPattern`, `MakeGrayPattern`,
  `BlackOrWhitePat`, `MonochromePat`, `GrayToRGB` (`qd/Ports.h`), and
  `GetPattern` reconstructed in full (packed colours, `'grayPattern`,
  `'ditherPattern` frames; the host had only the standard patterns and
  eight-row binaries).
- `PicPlay` holds Refs now (the ROM's does), so `test_PicPlay` starts an
  object heap.  The recognition area's natives are all answered.

## 2026-09-28: MungeShape and MungeBitmap

- `views/ShapeVerbs.cpp`: `MungeShape`/`DoMungeShape` and the eight point
  and rectangle turners; `toolbox/Matrix.h`: the 3x3 16.16 matrices
  (`MxInit` ... `MxMove`, `RotateMatrix`, `TransformPoints`, `idMatrix`),
  which make `TStroke::Rotate`/`Scale` real (they only updated the box).
- `qd/MungeBitmap.cpp`: `MungeBitmap` and its five routines (`RotBitmapL`/
  `RotBitmapR` transposing 32 x 8 blocks into a new 'pixels object,
  `FlipBitmapH`/`V`, `RotBitmap180`), `Tilable`; the bit-reversal table
  `bitFlip` generated (`qd/BitFlipTable.cpp`).  The bits are read as the
  ARM's big-endian words.  ROM quirks kept: a half turn moves the rows'
  padding to their start, skips a few middle bytes for sizes not a
  multiple of sixteen and does nothing under sixteen bytes; FlipBitmapH
  does not give its row buffer back; DoMungeShape leaves a drawn shape at
  the origin.  NOT YET: `RotTiledBitmap` (screen-sized bitmaps, tiles in
  a large binary on a store).
- `FMungeShape`'s centre: the decompiler reads the y wrong (an unaligned
  load's rotation); the disassembly gives the box's middle.

## 2026-09-28: strokes nobody read grouped into ink

- `recognition/WordSegment.h`: ParaGraph's word segmenter (`WordStrokes`
  and the twenty `WS_*` functions, 0x0026e8e8-0x00271da0) - the line's
  histogram along x, the gaps, the line height, pitch, slope and word
  distance learnt as it goes - and the net that says whether a gap is a
  space (`NeuroNetWS`, `Rget_answer`, `EXP`; the tables generated into
  `WordSegmentTables.cpp` by `romtable.py`).  `toolbox/FixedMath.cpp`
  gained `FixMul32`, the library's 24.8 multiply.
- `recognition/InkGroups.h`: the IG and GC layers
  (`IGGroupAndCompressStrokes`, `IGCompressStrokes`, the group's upkeep,
  `GCGroupStrokes`, `GCResizeAndLockGResHandle`, ...) and
  `NewGetTraceFromStrokes`/`GetTraceFromStrokes` (the trace the ParaGraph
  library reads; the letter styles' `DoLearning` waited on it too).
  `IGGroupAndCompressStrokes` was transcribed from the disassembly: the
  decompiler loses its 64-bit returns.
- The stroke world's side: `AddExpiredStroke` and `ExpireAll` now group,
  `IGCompressGroup`, `CompressGroup`, `ExpireGroup`,
  `ExpireUsingCommand` (the aeRawInk/aeInkWord command to the view, and
  the once-a-day memory warning) and `WRecEndInkStrokeGroup`;
  `HandleExpiredStroke` hands its stroke over (it only took the ink off
  before).  `StrokeCentral`'s `fUnused24`/`fUnused3c` turned out to be the
  group's count and the expire proc.
- A ROM quirk found and kept: `Recognize`'s `HandleBulkStrokes` gives the
  grouped ink to `AddWordInfo`, which keeps only word infos with a word,
  so the ROM's `Recognize` answers nothing for unread strokes.
- `recognize.ns` checks both: unread strokes to `Recognize` come back as
  nothing, and "ton" written twice on a view that reads nothing reaches
  its `viewRawInkScript` as two pieces of four strokes.
- NOT YET: the word descriptors (the cursive recogniser's side of the GC
  layer).

## 2026-09-28: the shape verbs and the last recognition natives

- `views/ShapeVerbs.cpp`: `FindShape` (`DoFindShape` over `PointInShape`,
  `DistanceFromRect` and `qd/Rects.h`'s `DistanceFromLine`), `GetShapeInfo`,
  `MakeInk`, `StrokeInPicture`, `AnimateSimpleStroke`; `WedgeBox` (a stub
  until now) answers the quarter of the box a wedge starts in.  ROM bugs
  kept: `DoFindShape` compares a distance with the path's first slot as a
  Ref (four times the distance it holds), a filled oval or wedge ignores
  what `PointInShape` answers, and the polygon's fake handle and the ink's
  expanded strokes leak; `AnimateSimpleStroke` offsets the stylus picture's
  bounds by their own top left (doubled, not taken back).
- `ink/CICConvert.cpp`: the codec's converter (`ConvertData`,
  `ConverterRun`, `ProcessNewStroke`/`LongStrokeNear`/`ShortStrokeNear`)
  and `InkConvert` over it.  Host bug found: the codec seam took format 2
  to be "the old uncompressed ink" and refused it; it is the codec's own
  older, headerless code-book-2 format (`ReadNewStroke` reads it,
  `InkConvert` writes it for 'ink), and `TCICInkCodec` now reads it.
- `ConvertDictionaryData`, `AddUnit`, `HandleInkWord`,
  `MoveCorrectionInfo` - the last with two ROM bugs: the offsets go on as
  Refs, and the native table gives it three arguments where the function
  reads four, so its new offset is whatever the ARM stack held
  (DEVIATION: nil on the host).
- Left: `MungeShape` and `PictToShape` (next-steps, item 3), and
  `TEditView::TrackDistort`.

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

## Finishing the recognition system, as planned in next-steps.md (2026-09-27 to 2026-09-28)

The plan and round-by-round progress that `docs/next-steps.md` carried
while the recognition system was finished - the deferred recognition,
the letter styles, the shape verbs, and the whole of ParaGraph's cursive
and digit readers - moved here verbatim when it was done (each round also
has its own entry above).  Its section headings are demoted one level.

### Now: finishing the recognition system

The owner asked (2026-09-27) for the recognition system to be put to bed,
then for the testing system.  What is left of recognition is its natives
(`natives.py --unbound`, the recognition area: all 116 answered since
2026-09-28), in the order planned:

1. ~~**Deferred recognition**~~ - DONE 2026-09-27 (`views/Rerecognize.h`,
   `docs/recognition/README.md`'s "Deferred recognition", ctest
   `host.NewtonRecognize`).  The grouping of unread strokes into ink is
   DONE too (2026-09-28, `recognition/InkGroups.h` over ParaGraph's word
   segmenter `WordSegment.h`; `HandleExpiredStroke` hands strokes to it);
   `Recognize` still answers nothing for them, as the ROM's own does.
2. DONE (2026-09-28, `docs/recognition/README.md`'s "The cursive
   recogniser and the letter styles"; the cursive engine's reading is
   NOT YET - measured below) **Letter styles**: `DoCursiveTraining`, `GetLetterWeights`/
   `SetLetterWeights`, the letter-shape natives, `RosettaExtension`.
3. DONE (2026-09-28, `views/ShapeVerbs.cpp`,
   `docs/views/README.md`'s "Questions asked of shapes") **Shape verbs**:
   `MakeInk`, `FindShape`, `GetShapeInfo`, `StrokeInPicture`,
   `AnimateSimpleStroke` (and `WedgeBox`, a stub until now).  NOT YET:
   - ~~`MungeShape`~~ DONE (2026-09-28, `views/ShapeVerbs.cpp`,
     `qd/MungeBitmap.cpp`, `toolbox/Matrix.h`; `MungeBitmap` too) bar
     `RotTiledBitmap` (a screen-sized bitmap turned in tiles out of a large
     binary on a store, over `TTile`).
   - ~~`PictToShape`~~ DONE (2026-09-28, `views/PictureShapes.cpp` over
     `DrawPicture`'s toShapes path, `docs/qd/README.md`'s "A picture
     turned into shapes"; `GetPattern` now takes every pattern form and
     `qd/Ports.h` has `MakeGrayPattern`/`MakeNSPattern`).  The picture's
     text becomes text boxes but is still not *drawn* by DrawPicture.
4. DONE (2026-09-28) `InkConvert` (over the codec's converter
   `ConvertData`, `ink/CICConvert.cpp`), `ConvertDictionaryData`,
   `MoveCorrectionInfo`/`AddUnit`/`HandleInkWord`; the boot's `UseWRec`
   choice was already made by `ReadCursiveOptions` (item 2).  NOT YET:
   `TEditView::TrackDistort` 0x000a9634 (dragging a selected shape's
   corner to distort it, which `MungeShape`'s neighbours would draw).

#### The cursive reader (ParaGraph's xr engine), measured

Measured 2026-09-28 with `analysis/callgraph.py build/MP2x00US
GCTryToRecognize__FP13PS_point_typeP15GCWordDescrTypeP7rc_typeP17GCGroupParmStruct
CallGroupAndClassify__FP12TStrXrDomainP10TStrXrUnitP11TStrokeUnitUiN24`:
**715 functions reached, 663 not done, about 431 KB** - twice the whole
of Rosetta - and a lower bound, since the domains' own `Classify` calls
reach it through function pointers (`TStrXrDomain::Classify` is 56 bytes).
The biggest pieces: a digit and number reader over "chunks" of writing
(`Digits` 24 KB, `SearchDigit_S`/`_K`/`_L`/`New_SearchDigit_V` 17-24 KB
each, `SearchNumber`, `FindPound`, `RecognizeZCCW` - about 110 KB
together), the low-level feature extraction over `low_type`
(`BaselineAndScale`, `transfrmN` 6.4 KB, `Extr`/`BigExtr`,
`line_pos_mist`, `StrElements`), punctuation and apostrophes
(`punctuation`, `RestoreApostroph`), and the lexical correction
(`ChunkCorrectByLexDB`).  The word descriptors the GC layer keeps for it
(`GCWordDescr*`, `GCWriteNewGroupResults`, the rest of
`GCTryToRemoveLastWords`, `InkGroups.h`) come first.  A plan, in the
order the reading reaches things: (1) the word descriptors and
`GCTryToRecognize`'s own frame (`GCLockRecognitionData`,
`GCFillBaseLineParameters`, `GCMergeLinesAndRemoveDash`); (2) the
low-level layer (`low_type`: the trace cut into elements and
extrema - the part once mistaken for Rosetta's feature extraction);
(3) the xr matching against the DTE/PPD tables already loaded
(`ParaGraph.h`); (4) the word search and the lexical DB; (5) the digit
and number reader last, since it is a reader of its own inside the
engine.  Like Rosetta it should sit behind the `TWRecognizer`-style seam
so a modern cursive recogniser can replace it.  Until then a cursive
letter set on the host reads nothing (its writing stays ink).

**Stage 1 DONE (2026-09-28, commits 46030bd, 2a0b8be;
`docs/recognition/README.md`'s "The way writing reaches the cursive
reader"):** the word descriptors (`WordDescriptors.h`), `GCTryToRecognize`'s
frame and the base line handed to the engine (`CursiveReader.h`:
`GCFillBaseLineParameters`, `SetRCB`, `GCLockRecognitionData`), and the
strokes-to-xrs domain that feeds it (`StrXrDomain.cpp`: `TStrXrDomain`,
`TStrXrUnit`, `CallGroupAndClassify` and the rest of the GC layer,
`WriteRecResults`/`GCWriteRW` making each word read a unit).  With a
cursive letter set the writing is now grouped into words and each word
reaches the reader; the reader answers -8 (the low level is NOT YET), so
the word is kept as ink.  `test_WordDescriptors`;
`src/host/demo/cursive.ns` with `NEWTON_TRACE_CURSIVE=1`.  Left of
stage 1: `SetStrXrRC` (0x000651e4, a configuration's `strxrCommands`).

The lock-up found on the way (`cursive.ns` stopping one run in three in
the heap's compaction) was heap damage done at boot: `CreateTrigramHeader`
asked for the ROM's 0x98 bytes and zeroed the host's 0xa8-byte header
over the next block's header - fixed (2026-09-28, `docs/work-log.md`).
The 'STXR' bisect only moved the heap's layout.  `cursive.ns` is now
ctest `host.NewtonCursive` (18 of 18 runs clean, where 3 of 6 locked up).

**Stage 2 sized** (callgraph.py, not-done functions below each root):
`low_level` 389 functions, 213 KB (the trace cut into xrs: `low_type`,
`BaselineAndScale`, `transfrmN`, `Extr`/`BigExtr`, `line_pos_mist`,
`StrElements`, ...); `xrw_algs` 70, 29 KB (stage 3/4: the xr matching and
the word graph); the `Chunk*` digit reader 72, 140 KB (stage 5).  Stage 2
is itself several rounds; a first testable piece would be the trace's
preprocessing and `BaselineAndScale` checked against a word of known
shape, then `Extr` (the extrema) - each layer can be tested on its own
because `low_level` writes an `xrdata_type` (0x18-byte elements) that
can be printed and compared with what the letter shapes imply.

**Stage 2 begun (2026-09-28, commits 9242531, 170373e, bdf5d96, 4334066,
d8a649a; `docs/recognition/README.md`'s "The low level"):** the
`low_type` state and its memory, the strokes, the filters (`Errorprov`,
`PreFilt`, `Filt`, `PSProc`), the extremum finders (`Extr`, `BigExtr`,
`DirectExtr`), the element list operations and about thirty of the
base-line finder's pieces (`LowBaseline.cpp`) - 84 functions, about 24 KB,
each checked by `test_LowLevel`.  Left below `low_level`: 305 functions,
189 KB.  The next piece is the rest of `transfrmN` (32 functions, 38 KB:
`classify_strokes`, `bord_correction`, `line_pos_mist`, the gap and
glitch finders, `calc_med_heights`, `extract_all_extr` and the
punctuation tests under it), which completes `BaselineAndScale` and
gives the first check against a word of known shape; then
`AnalyzeLowData`'s passes and `exchange`.  At this round's pace that is
seven or eight more rounds for `low_level`, and the whole reader
(`xrw_algs`, the lexical search, the digit reader) perhaps fifteen.
Working notes: the decompiler mangles this code's signed-byte and
two-result patterns (`__rt_sdiv` answers the quotient in r0 and the
remainder in r1; a direction kept as two shorts shows as bytes), so each
function's arithmetic is checked in the assembly (`analysis/disasm.py`),
and a struct holding pointers (`SPEC_TYPE`, `EXTR`, `low_type`) is
`sizeof`-allocated on the host.

**Stage 2, round 2 (2026-09-28, commits b66b2ca, 3089d57, 65ba664):** the
base-line finder is whole - `transfrmN` and the 32 functions below it
(`LowPunct.cpp`, `LowGeometry.cpp`, `LowLine.cpp`, `LowClassify.cpp`,
`LowBorders.cpp`) and `BaselineAndScale`, 38 functions and about 41 KB -
and the first end-to-end check passes: synthetic arches 40 high on y = 200
come back as a height of 40 on a lower border of 200, the trace rescaled
to 0x2796..0x27e6 (`test_LowLevel`'s `TestBaseline`).  AnalyzeLowData's
first seven passes are done too (`LowAnalyze.cpp`).  Left below
`low_level`: 267 functions, 148 KB - the rest of `AnalyzeLowData` (`Pict`,
the circle finder `Circle` with `work_with_circle`/`Orient00` and the back
and forward circles, `angl`, `FindSideExtr`/`PostFindSideExtr`, `Cross`,
the `lk_*` passes over sticks, circles and arcs, `Adjust_I_U`, `xt_st_zz`
with its dozen helpers, `RestoreColons`) and `exchange` (the xrs written:
`FillXrFeatures`, `AssignInputPenaltyAndStrict`, `check_xrdata`,
`MarkXrAsLastInLetter`, `GetLinkBetweenThisAndNextXr`).  Revised estimate:
at this round's pace (about 45 KB a round) three to four more rounds for
`low_level`, and the whole reader perhaps twelve.

**Stage 2, round 3 (2026-09-28, commits a95c376, d02ec5a, 87c4fa2):**
`Pict` whole (`LowPict.cpp`: the stroke descriptions, the dashes, dots,
hatches and crossings, `VertSticksSelector`'s upright sticks, `FantomSt`,
`FillCross`, `Recount`; 59 functions, about 34 KB) and `angl`
(`LowAngles.cpp`, 4 functions), with `CreateSDS`/`DestroySDS`.
`test_LowLevel`'s `TestPict` takes a word through the base line and
AnalyzeLowData's first steps into Pict (a dash marked 7, a dot 8, every
stroke described); `TestAngles` finds a hairpin's corner.  Worth knowing
for the rest: mark 7 is a *level* straight stroke, not an upright stick;
the decompiler reads an unaligned `ldr` at a word + 2 as the halfword there
when a `strb` of its low byte takes the halfword *before* it - check every
such copy in the disassembly.  Left below `low_level`: 207 functions,
115 KB - `Circle` (26 functions, 10 KB: `work_with_circle`, `Clash_my`,
`circle_type` and the `is_*_circle` tests), `FindSideExtr` (8 KB), `Cross`
(8 KB), the `lk_*` passes, `Adjust_I_U`, `xt_st_zz`, `RestoreColons`,
`PostFindSideExtr`, and `exchange` (20 KB).  At this pace (about 36 KB a
round) three more rounds for `low_level`; the whole reader perhaps eleven.

**Stage 2, round 4 (2026-09-28, commits cd697fb, 12949ca, 6d79cb8,
b20942c, 4462175, c65b08d):** `Circle` (`LowCircle.cpp`, 26 functions),
`FindSideExtr` (`LowSide.cpp`, 10, two of them unnamed), `Cross`
(`LowCross.cpp`, 6), `lk_begin` (`LowBegin.cpp`, 11) and `Adjust_I_U`
(`LowAdjust.cpp`) - about 55 functions and 25 KB, each with a test in
`test_LowLevel`; the `eps0`..`eps3` and `nbcut` tables and the rest of
`const1` from romtable.py.  AnalyzeLowData now runs, by hand in the test,
from the start through `lk_begin` and `Adjust_I_U`, and the cursive "uou"
comes out coded (a start and end at tops, three tops, four bottoms, the
o's crossings).  `low_level` is still not called.  Left below it: 157
functions, 90 KB - `lk_cross` (`del_inside_circles`, `analize_sticks`,
`analize_circles` and their helpers, about 12 KB), `lk_duga`
(`arcs_processing`, `conv_sticks_to_arcs`, the circle neighbours, about
10 KB), `xt_st_zz` (the t-bars, umlauts, quotes and punctuation,
`make_different_breaks`, `FindDArcs`: the biggest, about 30 KB),
`RestoreColons` (3 KB), `PostFindSideExtr` (3 KB), and `exchange` with
`FillXrFeatures` (about 12 KB: its layout and the tables it needs are in
`docs/recognition/README.md`'s low-level section).  The quickest route to
a first xr stream is `exchange` next - the test can call it after
`Adjust_I_U` without the passes in between - then the passes in the
order AnalyzeLowData calls them.  Revised estimate: about three more
rounds for `low_level` (this round did about 25 KB, every function from
the disassembly), and the whole reader perhaps ten.

**Stage 2, round 5 (2026-09-28, commits edb3d06, 94c1eb6, 7e8b3df,
44f6286):**
`exchange` and `FillXrFeatures` (`LowExchange.cpp`, `LowXrFeatures.cpp`:
the first xr stream - `test_LowLevel`'s `TestExchange` takes the "uou"
through to breaks at each end, 5 upper and 4 lower extrema, points and
boxes inside the trace; `penlDefX`/`penlDefH`, `xr_type_merits`,
`ratio_to_angle` and FillSHR's two limit tables from romtable.py),
`RestoreColons` and `PostFindSideExtr` (`LowRestore.cpp`: `TestRestore`
moves a colon written last back between two u's), and `lk_cross`
(`LowLkCross.cpp`, 32 functions: sticks, loops, the point-in-polygon
test - `TestLkCross` codes the uou's o as a closed loop).  About 60
functions and 42 KB; and of `lk_duga` (`LowLkDuga.cpp`, `TestLkDuga`)
the arc, loop and stick passes: `arcs_processing` (with `DyLimit`,
`IsDx_Dy_in_arcs_OK`, `IsDx_Dy_in_tips_OK`, `IsTipOK`),
`delete_CROSS_elements`/`ins_third_elem_in_circle` and
`check_IUb_IDf_small`.  Left below `low_level`: the rest of `lk_duga` (30
functions, about 11 KB: `lk_duga` itself, `prevent_arcs`,
`conv_sticks_to_arcs` over `cos_horizline`, `del_before_after_circles`
and the circle neighbours over the `NxtPrvCircle_type` block -
`check_before_circle`, `check_after_circle`, `check_next_for_*`,
`UpElemBeforeCircle`/`DnElemBeforeCircle`, `Is_8`, `O_GU_To3Elements`,
`HardOverlapRect` - and `delete_UD_before_DDL`; the disassembly is
0x002fa2f8-0x002fd920) and `xt_st_zz` (67 not done, about 40 KB: the
t-bars, umlauts, quotes and punctuation, `make_different_breaks`,
`FindDArcs`), then wiring `low_level` into `GCTryToRecognize` (the
order is AnalyzeLowData's: `lk_begin`, `lk_cross`, `lk_duga`,
`Adjust_I_U`, `xt_st_zz`, `RestoreColons`, `PostFindSideExtr`; then
`exchange`).  Estimate: `lk_duga` one round, `xt_st_zz` one or two, then
`low_level` is whole; the reader after it (`xrw_algs`, the lexical
search, the digit reader) perhaps seven or eight more.

**Stage 2, round 6 (2026-09-28, commits 25a1bd7, 948b6e4, b04803c):**
`lk_duga` whole (`LowLkDuga.cpp`: `prevent_arcs`,
`conv_sticks_to_arcs`, `del_before_after_circles` and the fifteen
circle-neighbour functions over `NxtPrvCircle_type`,
`delete_UD_before_DDL`; `cos_horizline` and `x/y/HardOverlapRect` in
`LowGeometry.cpp`; `TestLkDugaWhole` takes the uou through it), and of
`xt_st_zz` (`LowXtSt.cpp`) everything but `FindDArcs`: 50 functions and
about 30 KB - the late strokes found and placed, quotes, punctuation and
`RestoreApostroph` (with four unnamed helpers), umlauts, angstroms, the
crossed-out x, the parentheses, the breaks weighed
(`make_different_breaks`, `GetDxBetweenStrokes`,
`GetTraceBoxInsideYZone`), `del_close_MAX_MIN`, `CheckSequenceOfElements`
(`TestXtSt`).  Left below `low_level`: `xt_st_zz` itself (240 bytes; its
order is in `LowXtSt.cpp`'s header) and `FindDArcs`'s group (about 10 KB:
`CheckSZArcs` and `CheckDArcs` are 2.9 KB each, the rest small; they
share an `SZD_FEATURES` block of 0x44 bytes on the ROM's stack - +0 the
low_type, +4/+8 the two elements looked at, +0xc a new element, +0x10..
+0x20 x, y, the initial x and y and the point map); then `low_level`
and `AnalyzeLowData` themselves and the wiring into `GCTryToRecognize`.
Estimate: one round for `FindDArcs`, `xt_st_zz` and the wiring; the
reader above it (`xrw_algs`, the lexical search, the digit reader) about
seven more.

**Stage 2 DONE, round 7 (2026-09-28, commit 548e364):** `FindDArcs` and
its group (`LowDArcs.cpp`, 17 functions and about 10 KB, all from the
disassembly: the S/Z and d-bowl finders over `SZD_FEATURES`), `xt_st_zz`
itself, `AnalyzeLowData` and `low_level` - **the low level is whole** -
and `GCTryToRecognize` now calls `low_level`.  `test_LowLevel`'s
`TestDArcs` (an S gets its 0x23 element between the arcs) and
`TestLowLevelWhole` (the uou from its trace to its 13 xrs through
`low_level`); with a cursive letter set `cursive.ns` cuts "ton" into 13
xrs and "to" into 8 and the reader stops at `xrw_algs`, -9 (ctest
`host.NewtonCursive` checks both; clean under `NEWTON_HEAPCHECK=5`).

**Stage 3 DONE, round 8 (2026-09-28, commits cfc92ea, 1f1ffc2, efe50d9):
`xrw_algs` whole** - the matrix (`XrMatrix.h`: `xrcm_type`, `CountWord`,
`CountLetter`, `CountSym`, `CountVar`, `MergeVarResults`, the trace and
layout, and the two hand-written assembly loops `CountXrAsm`/`TCountXrAsm`
transcribed register by register), the Viterbi (`Xrlv.cpp`: all of
`xrlv`, `XrlvCHLXrlvPos` included), the dictionaries it asks what may come
next (`XrLex.cpp`, over two new Airus routes: `AEnum_NextSet9` and the
lexicon's `AL_NextSet9`), the word graph (`XrWordGraph.cpp`:
`create_rwg_ppd`, `GetCMPAliases`, `fill_RW_aliases`, `SortGraph`,
`GetSymBox`, `GetBaseBord`) and `SetMultiWordMarksWS`/`Dash`; tables from
romtable.py (`XrReaderTables.cpp`).  `test_XrMatrix`: six letters of the
ROM's table each read their own ideal xrs best, and `xrlv` reads the
ideal xrs of l and o as "lo" first.  Wired into `GCTryToRecognize`: with
a cursive letter set `cursive.ns`'s "to" comes out of the word graph as
"to" first, "ton" as For/ER/Eon/FR/EN (`NEWTON_TRACE_CURSIVE=1`; ctest
`host.NewtonCursive` checks both graphs; clean under
`NEWTON_HEAPCHECK=5`).  The host still answers -9 after the graph.

**Stage 4 begun, round 9 (2026-09-28): the answers, and the first
cursive word typed.**  `MakeAndCombRecWordsFromWordGraph`/
`MakeRecWordsFromWordGraph` (the graph made into readings, sorted, scaled
and cut - `XrAnswers.cpp`), `FillRecwordSplitInfo` and its helpers (which
strokes each word of a reading of several is: `connect_trajectory_and_*`,
`AddStrokesOfSymbol`, `AttachLostStrokeToWord`, `FillSplitInfoFromRWG`),
`GCFillLearningHandle` over `LHAddEntry`, and the word domain's own
reading - `TXrWordDomain::Group`/`Classify`/`Reclassify`/`ClassifyXrWord`
and `TXrWordUnit` - so an STXR unit becomes an 'XRWR' word unit, the
arbiter hands it to `TWordRecognizer`, and the page types it.
`cursive.ns` read **"For to"** then ("ton to" since FillSHR was put right,
below; ctest `host.NewtonCursive` checks the
answers and the page's text; `test_XrAnswers` the readings, the split
information and the training data).  The rules' headers are walked too
(`XrRules.cpp`: `PDFGetRule` and its address helpers, checked against
the ROM's 87 characters' rules in `test_XrMatrix`).

**Stage 4, round 10 (2026-09-28): the post-processing.**
`EvaluateAndSortAnswers` is real (`XrPost.h`, `XrPostCalc.cpp`,
`XrPostEval.cpp`; `docs/recognition/README.md`, "The post-processing"):
the letter table's rules are little programs, and the stack machine that
runs them (`CalculateQueueResult`, from the disassembly) and all 74
functions of their `Functions` table are there, with `EvaluateCharQuality`,
the side reasoning, the boxes, the missing crosses and `CheckDigitsLine`.
It only scores close calls between good answers (the best at least rc
+0x100 = 60 and no more than rc +0x102 = 10 ahead), so `cursive.ns` reads
as before; `test_XrMatrix` runs hand-made queues and the ROM's rules for
an l and an o, and a joined-up demo (`src/host/demo/cursive-joined.ns`,
ctest `host.NewtonCursiveJoined`) writes "on", "no", "mum", "to", "nun" in
one stroke each: they read "OR", "bb", "maps", "to", "Rap" - "no" was the
one scored, its o's rules sinking "Do"/"no" below "bb".  Left of stage 4:
`CheckDiacriticsDirections`/`AnalyseDiacriticsDirection` (only for a
French or German letter set, rc +6 bits 2-3), `MakeRecWordsFromGraph`/
`MakeNewPath`/`FillRecWordsElement`/`MergeTwoRecWordsSets` (the readings
of a fixed-string field's graph, rwg type 2), and `ORCreateLearnInfo`/
`Orto*` (about 2 KB; only with rc +0xb2 bit 6, which the Notepad does not
set).

**Stage 4, round 11 (2026-09-28): the readings put right.**  The poor
readings (and their capitals) were a port bug, found by isolating the
stages (`docs/recognition/README.md`, "Joined-up writing"): words made of
the letters' ideal xrs read as themselves with the Notepad's capitals
allowed (`test_XrMatrix`'s `TestIdealWords`), so the matcher and `xrlv`
were sound; the fault was **FillSHR**, whose four bracketing xrs had the
last two swapped in all six cases, negating every shift class
(`test_LowLevel`'s `TestFillSHR`).  Fixed: `cursive.ns` reads "ton to";
with the demo's o drawn as a cursive o is (its join arriving at the top
right), `cursive-joined.ns` reads "on" 82, "no" 90, "Mom" 67 ("mum"
fifth), "to" 86, "nun" 76.  "mum" and "nun" then lose to the scrub
gesture - their synthetic stems are retraced exactly, a zig-zag -
and go down as ink words; `TestScrub` agrees with the ROM, so that is the
drawing (`NEWTON_TRACE_ARBITER=1` prints each arbitration).  Left of
stage 4 as above; then the `Chunk*` digit reader (only for a field that
allows numbers, rc +0xb6; 82 not done, about 146 KB), three or four
rounds.  Other stages might hold slips like FillSHR's - a transcribed
store order is the thing to check: the rest of FillXrFeatures
(FillOrients) agreed with ParaGraph's own later source where compared,
and the published source (below) is the quickest way to find a
suspect.

**Round 12 (2026-09-28): the reader's leftovers.**  Done: `SetStrXrRC`
(a configuration's `strxrCommands`, byte commands reaching the host's own
fields by their ROM offsets); the readings of a graph of alternatives
(`MakeRecWordsFromGraph`, `MakeNewPath`, `FillRecWordsElement`,
`MergeTwoRecWordsSets` - a fixed-string field's graph, read twice, once
for a number and once for a word, and merged); **learning** - `DoLearning`
now hands the pen's trace (`GetTraceFromStrokes`, which was there all
along; the NOT YET note named a wrong address) to the word domain, and a
word info's unit id is read back right (`UnitID` read a host ULong out of
two UniChars, so `DoIndexedLearning` found no recogniser and crashed):
`cursive.ns` reads "ton" again with learning on, learns it, and the
letter weights are no longer the defaults; and the digit reader's context
(`Chunk.h`: `ChunkAllocCtx`, `ChunkCleanUp`, `IsChunkNumbers`,
`ChunkModifyRC`/`ChunkRestoreRC`, `ChunkWriteParamCtx`), called where
`GCTryToRecognize` calls them, and the first of its geometry
(`v_MostFarFromChord`, `v_QDistFromChord`, `GetDirection` over
`ChunkTables.cpp`).  Left:
- **The orthographic learning** (only with rc +0xb2 bit 6 / +0xb8 bit 3,
  which the Notepad never sets): `ORCreateLearnInfo` over `OrtoCreate`,
  `OrtoGetmem`/`OrtoCalcSize`/`OrtoResize`/`OrtoFasten`, `OrtoEntries`
  (796 B, which the decompiler mangles - read the disassembly) and
  `RemovePointAndSort` (0x00147548-0x00147d70, about 2.5 KB); and
  `ORTraining` (a tail call into `OrtoTraining`, 0x00147e74) over
  `LearnPartsCopy` and `TrainTrajectory` - a letter-shape database of its
  own (`FillNwtSample` over `TraceToOdata`/`RjctAppr` and the DCT
  (`FDCT4/8/16`, `IDCT...`), `AddToDataBase`, `SearchInDataBase` with
  `FirstSearch`/`SecondSearch`, `Occam`, `SQRT32_ORTO`): 56 not done,
  about 15 KB.  The `_LEARN_ARRAY_tag` block goes into the training data
  ('ORTL'), so keep its bytes as the ROM lays them out (big-endian halves;
  the pointer at +0x14 is only a cache, recomputed from +0x04 each time -
  on the host leave it unused).  (`ConfigureArea`'s base-line and grid
  geometry is DONE, 2026-09-28: `ConfigFromFrame`, `GetWordGeom`,
  `GetGridGeom`, the two `FromObject`s - and the STXR domain's geometry
  selectors, which the host had copying the wrong way.)  **This is the
  last of the recognition system that is NOT YET**, bar `AL_NextSet`
  (Airus selector 8 for lexicons), `TEditView::TrackDistort`,
  `RotTiledBitmap` and the French/German accent checks the US ROM never
  reaches.
- **The digit reader** (below).

**The digit reader, sized** (`callgraph.py build/MP2x00US ChunkAllocCtx
ChunkProcessor ChunkModifyRC ChunkWriteParamCtx ChunkPatchXrdata
ChunkRestoreRC ChunkSortAnswers ChunkCorrectByLexDB ChunkCleanUp
--through-done`): 176 functions reached, **102 not done, about 150 KB**.
`ChunkProcessor` (0x002a6b50, 2.6 KB) is the whole of it: the points made
a `tag_WORD_TRACE`, `ExtrWordTrace_V` (its extrema) and `GetLineApprox`
(a polyline approximation, `tag_wapx_type`, with `SetAllDirections`,
`GetDirection`, `v_MostFarFromChord`, `v_QDistFromChord` - about 5 KB
together), `ChunkConstruct` (the writing cut into chunks: `ApxToBrackets`,
`ApxToCLine`, `ChunkFillMainData`, `ChunkMakeStrokes`, `LO_*` - the list
of low objects), then **`Digits`** (24 KB) over the four digit searchers
`SearchDigit_S` (24 KB), `SearchDigit_K` (17 KB), `SearchDigit_L` (3.7 KB)
and `New_SearchDigit_V` (20 KB), `SearchNumber`, `FindPound` (the £ sign,
6 KB), `RecognizeZCCW`, `GetCircles`, `Check_4`, `CutNumberInDigits`,
`DefHeightsForNumber`, and after the xr reader `ChunkPatchXrdata` (1.2
KB), `ChunkSortAnswers` (an unnamed sort at 0x002a4c04) and
`ChunkCorrectByLexDB` (3.5 KB).  In that order, bottom up: (1) the trace,
`ExtrWordTrace_V`, `GetLineApprox` (with `SetAllDirections`; its
`v_MostFarFromChord`, `v_QDistFromChord` and `GetDirection` are done) and
the `LO_*` list (`LO_Create` is a 0x482c-byte block with a pointer at
+0x28 and 0x3c-byte objects from +0x1dc - a host layout of its own; the
ROM's `LO_Destroy` frees it with an inlined `DisposHandle` of the handle
`HWRMemoryAlloc` keeps in front of the block), each testable on a drawn
digit; (2) `ChunkConstruct` and its helpers; (3) `Digits` with
`SearchDigit_L` (the smallest searcher) first, then `_V`, `_K`, `_S`;
(4) the rest, and `ChunkProcessor` itself wired in, with a demo writing
"42" into a numbers field (rc +0xb6 is set by a field whose
recognition flags allow numbers).  Four or five rounds.

*Progress (2026-09-28)*: steps (1) and (2) are done -
`recognition/ChunkTrace.cpp` (`ExtrWordTrace_V`, `GetLineApprox`,
`SetAllDirections`), `ChunkLowObj.cpp` (all eleven `LO_*` and the free
list), `ChunkConstruct.cpp` (`ChunkConstruct`/`ChunkDestroyData`,
`ChunkFillMainData`, `ChunkMakeStrokes`, `ApxToBrackets` with its unnamed
bracket maker, tidier (0x00286fb4) and hook dropper, `ApxToCLine` and
the classes it gives a chunk (300 a line, 400 an arc, 500 mixed, 600
two lines, 700 two arcs, 1400 more), the unnamed reclassing pass
0x00285bc8 (from the disassembly - the decompiler garbles it),
`CreateRealChunkInd`, the arc measures) and, of step (3), the two the
searchers start from (`ChunkPutClassesToLO`, `DefRectForChunks`).
`test_Chunk` draws a 4 and a 2 with a synthetic pen and checks the
turns, the polyline, the chunks (the 4's bent stroke is class 600, its
upright 300; the 2's hook an arc), the brackets and the list.  Left
(`callgraph.py build/MP2x00US ChunkProcessor__FPvP13PS_point_typei
--through-done`): 60 functions, about 124 KB.  **`SearchDigit_L` is not
the small one it looks**: its body (0x0028ddb4-0x0028eb9c, ten unnamed
statics, 3.5 KB) calls seven more unnamed statics that sit inside
`RecognizeZCCW`'s extent (0x0028ffe8, 0x0029082c, 0x002909f0,
0x00290c68, 0x00290de8 - which writes the digit found - and 0x00290e2c)
and `DgtFromDnHorseshoe`'s (0x0028f17c), so it and `RecognizeZCCW`
(4.5 KB) are one piece of about 8 KB; it also reads staff +0x40, which
`Digits` sets.  So step (3) is better begun at `Digits` itself (read its
top level first to see what it sets up and in what order it calls the
searchers), then `RecognizeZCCW` with `SearchDigit_L`; then `_V`, `_K`,
`_S`.  Three or four rounds.

*Progress (2026-09-28, round 3)*: of step (3), `Digits`' top level has
been read (its order: `DefHeightsForNumber`, `ChunkPutClassesToLO`,
`GetCircles`, `SearchDigit_L`, `SearchDigit_K`, `New_SearchDigit_V`,
`SearchDigit_S`, `FindPound`, `Check_4`, unnamed 0x002a09b0 and
0x002a2758, `CutNumberInDigits`, the second-look pass 0x0029ce20, then
with staff +0x50 clear 0x002a1a98, 0x0029e888, 0x002a19ec and
`SearchNumber` until one answers, else 0x0029ccd4; then 0x002a2078,
0x0029fbcc over a scratch block the size of the stroke count,
0x002a0d74, the 0x834 class's objects copied out as (first point, last
point) pairs for the caller, and 0x0029ffc8 deciding the answer's second
bit), and these are done (`recognition/ChunkDigits.cpp`,
`ChunkSearchL.cpp`, `test_Chunk`'s `TestLineAndCircles`, `TestSearchL`,
`TestSecondLooks`): `DefHeightsForNumber` with its four statics,
`GetCircles`, `SearchDigit_L` with its seventeen statics (the $, 2/7, 5
with its bar, 4/9, 3/5/9 tests), the searchers' shared geometry
(`direct_suits`, `distance_between_directions`, `take_next_point`,
`take_prev_point`, `x_in_line`, `x_in_curve`, `cross_with_line`,
`CheckQIntersec`/`XY`), `ThreeToFive`, `RecognizeZCCW` and `Check_4`.
The `DgtFrom*`, `GreyDgtFromELink` and `SignFromTwoSections` statics
that sit between them belong to `New_SearchDigit_V` (it is their only
caller, and they take its `tagLocalStuff`), so they go with it.

Next, in order: (a) the second-look pass 0x0029ce20 - it sorts up to
thirty digits by their first node, runs twelve statics over the sorted
array (0x0029d900, 0x0029dd6c, 0x0029dbd8, 0x0029d808, 0x0029e30c,
0x0029e530, 0x0029e048, 0x0029d428, 0x0029e6bc, 0x0029eaac, 0x0029eeb4 -
the largest, 870 instructions - and 0x002a0740), then `ThreeToFive` and
`RecognizeZCCW`, then files the class-1200 objects between the digits
and writes each digit and gap out as class 0x76c objects; about 2800
instructions in all, and testable by laying digit objects over drawn
writing as `TestSecondLooks` does; (b) `SearchDigit_K` (17 KB); (c)
`New_SearchDigit_V` with its statics (20 KB + 3 KB); (d) `SearchDigit_S`
(24 KB), `FindPound`, `SearchNumber` and the other statics `Digits` runs;
(e) `ChunkProcessor` wired in, `ChunkPatchXrdata`, `ChunkSortAnswers`,
`ChunkCorrectByLexDB`, and a demo writing "42" into a numbers field.
About five more rounds.

*Progress (2026-09-28, round 4)*: (a) and (b) are done -
`recognition/ChunkSecondLook.cpp` (the second-look pass and all twelve
statics, with the variant test 0x002a01dc under 0x002a0740;
`DigitsSecondLooks` in `Chunk.h`) and `recognition/ChunkSearchK.cpp`
(`SearchDigit_K` and its nineteen statics, `find_direct_forward`/
`_backward`); `test_Chunk`'s `TestSecondLookPass` and `TestSearchK` (a 4,
x, 7, #, 8 and % drawn and found).  Next: (c) `New_SearchDigit_V`
(0x00296e04, 20 KB; its statics `DgtFromDnHorseshoe` 0x0028eb9c,
`DgtFromUpCCWArc`, `DgtFromAloneDnCCWArc`, `GreyDgtFromELink`,
`SignFromTwoSections` take its `tagLocalStuff`) - it is the one that
reads a lone 1, which K leaves alone; then (d) and (e) as above.  Three
or four more rounds.

*Progress (2026-09-28, round 5)*: (c) is done - `New_SearchDigit_V` and
all its statics (`recognition/ChunkSearchV.cpp`, 0x00296e04-0x0029bba8
and 0x0028eb9c-0x0028fa14; `ComposeTrace`), and of (d) `SearchNumber`
(`recognition/ChunkNumber.cpp`, with five statics from FindPound's range:
0x002a3608 and 0x002a4608-0x002a4a34) and `FindPound`
(`recognition/ChunkPound.cpp`); `test_Chunk`'s `TestSearchV`,
`TestSearchNumber` (42, 10, 217 and 11 judged numbers after L, K, V and
the second looks) and `TestFindPound`.  Left of (d): **`SearchDigit_S`**
(0x00290ed8-0x00296e04, 24 KB): its top level marks every real chunk
unused (f6C = 0), then runs 0x002926a0 over the writing's width, the
per-chunk passes 0x0029634c and 0x00296470 over the unused chunks,
0x00294fd0, 0x002950d4, 0x00291060, 0x002960d8 and 0x00293f7c over the
whole - 45 unnamed statics in all, the largest 0x002924b4-0x00292ae8,
0x002946d8 and 0x002953f8-0x00295d60; it is the searcher that reads the
signs coded 13 (the bar `FindPound` builds on) and most of the other
non-digit codes, so it is one round of its own.  Then the rest of
`Digits` (its statics 0x002a09b0, 0x002a2758, 0x002a1a98, 0x0029e888,
0x002a19ec, 0x0029ccd4, 0x002a2078, 0x0029fbcc, 0x002a0d74, 0x0029ffc8),
`CutNumberInDigits`, and (e).  Three more rounds.

*Progress (2026-09-28, round 6)*: (d) is done and of (e) `ChunkProcessor`
- the digit reader is whole but for the merge.  `SearchDigit_S` and its
45 statics (`recognition/ChunkSearchS.cpp`; `TestSearchS` reads a full
stop, minus, colon, +, 5 and crossed 7 with their bars, both brackets,
solidus, comma, 0, 6, 3, per cent sign and @ from drawn writing;
`TestFindPound` now takes the bar from S instead of laying it in);
`Digits` whole with every static and `CutNumberInDigits`
(`recognition/ChunkDigitsMain.cpp`; `TestDigits`: 42, 10, 217, 11, 2, 5,
1.1, (42) and 15 numbers, 1:1, "is" - a 1 and a one-stroke 5 - and digits
beside a letter not); `ChunkProcessor` (`recognition/Chunk.cpp`;
`TestProcessor` reads "42" and "10" from points as GCTryToRecognize will
hand them over).  `DigitChar` moved to `Chunk.h` (the ROM writes the
chain out eight times).

**(e) DONE (2026-09-28, round 7) - the digit reader is whole and live.**
The merge (`recognition/ChunkMerge.cpp`, all from the disassembly):
`ChunkPatchXrdata` (the xrs cut down to the runs of strokes that are not
digits, each ended by a break; two breaks and nothing more for a number
alone, so the xr reader has nothing to read), `ChunkSortAnswers` over
the unnamed sort 0x002a4c04 (the xr reader's letters - an o read as a 0,
another letter as the first non-letter the other readings have there -
put among the digits by where their boxes lie, 0x002a4a34 working the
boxes out; brackets written the wrong way round, guillemets and
"(ddd1"-style brackets settled; "d)" marked a list item) and
`ChunkCorrectByLexDB` (the reading walked through the lexical database a
character at a time, the confusable characters - 1 / ( ), 7 ), c ( 1, . ,
- and so on - tried in turn with a 32-deep backtracking stack, 0x002a5414
to 0x002a55bc and 0x002a7078-0x002a7168; then read again as a date with a
'1' taken for a '/', against the ROM's days-per-month table
`kChunkMonthDays` (romtable.py, 0x0037ae10, February 29), the long forms
in place of the reading, the short ones as the second; then an x given a
space before it).  `GCTryToRecognize` calls `ChunkProcessor` where the ROM
does, and `FillRecwordSplitInfo`'s number branch (0x0019e9e8) is real.
`TParagraphView::HandleWord`'s divide by an empty word's length now
throws evt.ex.div0 as the ROM's `__rt_udiv` does (it had trapped on the
host); nothing hands it an empty word now.  Tests: `test_Chunk`'s
`TestMerge`, `test_XrAnswers`' number split, ctest `host.NewtonNumbers`.

**A reference for the cursive reader**: PhatWare, who bought ParaGraph's
recogniser, published a descendant of it under the GPL v3
(https://github.com/phatware/WritePad-Handwriting-Recognition-Engine;
`WindowsTools/NNLegacyTool/` is the oldest tree - LOW/, XRWS/, POST/).
It is a later version and is not the ROM, so it is never transcribed:
it names things (the xr types are its `X_...` codes, `LOW/STD/XR_NAMES.H`:
0x14 `X_UD_F`, a forward lower arc; 0x0e `X_UU_B`; heights 1..13 from
super-uplinear to super-underlinear) and says what a function is for,
and where the ROM and the port disagree with it, the ROM's disassembly
decides.  The FillSHR slip showed up as exactly such a disagreement.

## Finishing packages, as planned in next-steps.md (2026-09-29)

The plan `docs/next-steps.md` carried while packages were finished,
with each item's outcome as it was recorded there, moved here verbatim
when the last reachable pieces were done (each round also has its own
entry above).  Its heading is demoted one level.

### Now: finishing packages

The owner asked (2026-09-29) to finish packages (`docs/packages/README.md`).
Sized with `analysis/callgraph.py build/MP2x00US <roots>` (functions not
yet done below the roots, and their bytes; a lower bound - indirect calls
are not seen).  In order, what a third-party package and the ROM's own
need first:

1. ~~**Units**~~ - DONE (2026-09-29, `packages/Units.h`,
   `docs/packages/README.md`'s "Units"; `test_Units`, ctest
   `host.NewtonUnits`): the export and import tables, the pending
   imports, `ResolveImportRef` over the host's `RelocateImportRefs`,
   `InitRExMagicPointerTables`, and the six unit natives; the ROM's five
   exporting parts register their seven units at boot.  With it
   `BackupPatchPackage` (nil) and `RestorePatchPackage` (0).  Left of it:
   `GetEntryFromLargeObjectVAddr` (a package's store entry, which the
   `client` slots of `CurrentImports`/`PendingImports` would carry - nil
   until packages are on a store, (5)).
2. ~~**The `'dict` and `'comm` part handlers**~~ - DONE (2026-09-29,
   `recognition/DictPartHandler.h`, `packages/FramePartHandler.h`'s
   `TCommPartHandler`; `docs/packages/README.md`): registered at boot in
   the ROM's order ('form, 'dict, 'auto, 'comm - 'book waits on (7)).
   Tested at the function level (`test_Dictionaries`, `test_Units`); no
   package with either part was to hand to install whole.
3. ~~**Streamed sources**~~ - DONE (2026-09-29, `packages/PackageLoader.h`,
   `PartPipe.h`; `docs/packages/README.md`'s "Streamed sources";
   `test_PackageManager`'s `TestStreamed`): `TPackageLoader`, the 'pipe'
   world (`TPipeApp`, `TPipeEventHandler`), `CPartPipe` and the manager's
   stream branches.  A streamed frames part is one flattened (NSOF)
   object, so an ordinary .pkg cannot be streamed - `newton --package`
   stays on the memory path.  Its endpoint side (`TEndpointPipe`,
   `SuckPackageFromEndpoint`) waits on the comms area.
4. ~~**Large binaries on a store**~~ - DONE (2026-09-29; the
   large-object layer `stores/LargeObjects.h`, the large binaries
   `stores/LargeBinaries.h`, the ephemerals `stores/Ephemerals.h`;
   `docs/stores/README.md`'s "Large objects" and "Large binaries";
   `test_LargeObjects`, `test_LargeBinaries`, ctest `host.NewtonVBO`).
   `TPixelMapCompander`, a store bitmap's default compander, is DONE too
   (2026-09-29, `stores/PixelMapCompander.cpp`: LZ over row-delta
   filtered pages).  Still NOT YET of the object layer: `TLrgObjStore`,
   objects made from compressed streams (`LODefCreateFromComp`), the
   backup progress callback (`TLOCallback`).
5. ~~**Packages on a store**~~ - DONE (2026-09-29, `packages/StorePackages.h`,
   `StorePackageNatives.cpp`, `stores/PackageObjects.cpp`;
   `docs/packages/README.md`'s "Packages on a store"; `test_PackageManager`'s
   `TestOnStore`, ctest `host.NewtonPackageStore`): `newton --store f
   --package x.pkg` stores the package (store:SuckPackageFromBinary) and
   the next boot activates it again.  Left of it: relocating a page to a
   base (`RelocateFramesInPage`, not needed on the host), XIP packages,
   the progress callback, `LODefCreateFromComp`, a card's 'stor event
   (`StorageCardInserted`/`MountStore`), `RestoreSegmentedPackage` over
   `CPackageArchivalPipe` (11 functions, about 1.5 KB at 0x0010d190-
   0x0010d9xx: a package restored from its segments), `SuckPackageFromEndpoint`
   (comms), `StopFrameSound`.  `SuckPackageOffDeskTop` is DONE (2026-09-29,
   over `utility/StdioPipe.h`'s `CStdioPipe` - the host's own files).  (The failure under
   `NEWTON_HEAPCHECK` was a host runtime bug, fixed 2026-09-29: a task
   switched out on its way back from a system call lost the call's answer
   - `docs/work-log.md`; ctest `host.NewtonPackageStoreSlow`.)
6. ~~**1.x packages**~~ - DONE (2026-09-29, `StorePackageNatives.cpp`,
   `docs/packages/README.md`'s "The 1.x packages"; ctest `host.NewtonOneX`):
   `Activate1.XPackage`, `DeActivate1.XPackage`, `Remove1.XPackage`,
   `1.XPackageToVBO`, the store's package directory and the 1.x
   `NewPackage`.  `GetCardReinsertionInfo` (a card's patch package) is
   NOT YET.
7. **The `'book` part handler** over the book reader - the handler and
   `TLibrarian::BookAvailable`/`BookRemoved` are 11 functions, 5.6 KB, but
   they stand on the book reader itself (`TLibrarian`, 49 methods; the
   19 `books` natives), a subsystem of its own.  The ROM's help book is
   refused for want of it.
8. **Protocol parts' class info** - a `'code`-kind part is raw ARM code
   registering protocol implementations (the ROM's ScreenBuffer and
   ScreenDrivers packages).  The host cannot run it; recorded, not ported
   (the host has its own screen driver).

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



