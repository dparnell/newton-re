# Printing

`src/print/` is the Newton's printing system. It is also how a fax is sent: to the ROM, a fax is a print job whose "printer" is the fax driver.

## The pieces

`print/Printer.h` defines two protocols and the engine between them.

- **`TPrinter`** is what a print job draws through.
  - A print view makes one by the printer frame's `imagingName` (`MakePrinter`) and constructs it with the frame's `driverName`.
  - It opens the printer with the job's connection frame, then draws each page into the printer's port. The port is a GrafPort at 72 dots an inch, the size of the paper.
  - The drawing happens between `OpenPage` and `ClosePage`, once more for every time `RepeatPage` answers true.
  - The protocol's first slot is not `New` but `Constructor(char* driverName)`. The class info's default New is `mov pc,lr`.
- **`TDotPrinterDriver`** is a bitmap printer's driver, the DDK's `DotDrivers.h`. It is the seam a printer's own code plugs into:
  - `GetBandPrefs`: how deep a band may be, and whether bands are sent asynchronously;
  - `GetPageInfo`: the page's resolution and size in dots;
  - `ImageBand`: a finished band;
  - the job's `Open`/`Close`/`OpenPage`/`ClosePage`.

  The ROM's drivers are the fax driver, the StyleWriter group, the LaserWriter LS and HP PCL. Only the fax driver is reconstructed. A modern printer would be a new implementation of this protocol; a host driver that writes the page to a PNG is the planned first one.
- **`TDotPrinter`** is the ROM's `TPrinter` for bitmap printers (`DotPrinter.cpp`). It prints a page a band at a time:
  - **Allocation.** `Open` asks the driver for the optimum band height. It halves it until the buffers can be had, and gives up below the minimum. There are three buffers: the band, a mask and a scaled pattern, plus a second band when the driver takes bands asynchronously.
  - **Drawing.** The page is drawn once per band. The printer port is clipped to the band's slice of the page (`fPageBand`). The *phantom port*, whose bits are the band itself, is set to the band's rectangle in the printer's dots (`fBandRect`).
  - **Imaging.** `RepeatPage` hands the finished band to the driver, then moves both rectangles on. When `wantMinBounds` is set, it also passes the smallest rectangle round the band's black (`CalcMinBounds`). A landscape page's band is first turned a quarter (`RotateBits`).
- **The scaling bottlenecks** (`ScalingBottlenecks.cpp`) are the printer port's drawing procs, which `Open` installs:
  - **Recording.** A shape drawn while a picture, region or polygon is being recorded goes to the standard proc as it is, so the recording stays at 72 dpi.
  - **Mapping.** Otherwise `SetupPhantomPort` makes the phantom port current, its clip being the printer port's clip within its visible region, scaled up. The shape is mapped from the page's rectangle to the printer's, and drawn if it meets the band.
  - **Direct drawing.** An inversion, or a shape in all-white or all-black, is drawn straight into the band.
  - **Patterned drawing.** Any other pattern is drawn in black into the mask. The pattern is scaled up to fill the band (`UpdateScalePat`: each row repeated, each pixel stretched), and the two are combined into the band in the pen's mode (`TransferShape`). Drawn directly, a 72-dpi gray would come out at a third of its size on a fax.
  - **Deeper patterns.** A pattern of more than one bit a pixel is dithered first (`DitherPattern`, over the ROM's sixteen grays in `DitherTables.cpp`).
- **`DriverCallbacks.cpp`** holds the driver callbacks. A job prints in the print view's world, which forked a task to take its event loop over. `PrReleaseControl` lets the world's mutex go, either for a time or, with a time of all ones, until `PrRegainControl`. The fax driver waits that way for its fax tool's replies.

Test: ctest `print.DotPrinter`. A 144-dpi test driver pastes each band into a page, and the shapes drawn at 72 dpi are checked pixel by pixel:
- rectangles, including one crossing a band boundary, which must show no seam;
- a frame 4 dots thick and a line 2 dots thick;
- an oval;
- a gray, which comes out exactly half black.

## How it was established

The layout offsets below are the ROM's.
- **Classes.** The interfaces follow the glue at 0x00387f2c-0x003880fc and `classinfo.py --name TDotPrinter`.
- **TDotPrinter's fields** (to +0x1cc) were named from their uses in 0x0020d0f0-0x0020e3f0.
- **GrafPort offsets** were read in the disassembly, not the decompile, whose names for them are off by a field. With `QD_Gray`:

  | Offset | Field |
  |---|---|
  | +0x24 | `visRgn` |
  | +0x28 | `clipRgn` |
  | +0x3e | `pnVis` |
  | +0x40 | `grafProcs` |
  | +0x44 / +0x48 / +0x4c | `picSave` / `rgnSave` / `polySave` |

- **PixelMap offsets.** A PixelMap's `rowBytes` is padded to a word, so its bounds start at +8.
- **SetPort.** The ROM's `SetPort` answers the port it replaced, and `OpenPage`/`RepeatPage` use that value. The host's `SetPort` is void, so they read `GetCurrentPort()` first.

## ROM bugs kept

- `TransferShape`'s notOr, notXor and notBic loops never advance the mask pointer. Everything they draw is masked by the band's first 32 dots.
- `UpdateScalePat` turns a pattern row left by the alignment *h*, but puts the bits that fall off back shifted right by 7-h rather than 8-h. One bit is doubled and one is lost.
- `ScaleStdRRect` scales the corners' oval as a Point with the width in `v`. On a fax, whose two resolutions differ (204 x 98 or 196), the corners come out scaled crosswise.
- `WhiteOrBlackPat` does not look at the pattern's last byte.
- `ConvertPattern` converts in place and reads each word again after writing the byte before it. The first pixels of a word are read from bits already converted.
- `TryAllocBands` gives back the buffers it had when a later one fails, but leaves them in the array. `bands[0]` then dangles, and `Open`/`OpenPage` take it for success.

## Deviations

- `QDProcs` are allocated by `sizeof`; the host's procs are pointers.
- `PrReleaseControl` clears the printer's pointer to its waiting state after the wait. The ROM leaves it pointing at a dead stack object; on the device, a later `PrRegainControl` sends to a port that no longer exists, which does no harm there.
- `Open`'s buffer array starts zeroed. The ROM reads stack rubbish there when a driver's minimum band is above its optimum.

## A host pitfall

`ULong` is pointer-sized on the host, so it cannot stand for an ARM register or a 32-bit word pointer. Word-at-a-time pixel code and shift-register emulation use `uint32_t`:
- `TransferShape` written with `ULong*` stepped 8 bytes at a time and ran off the end of the band.
- A `x <<= 1; if (x == 0)` sentinel never fires on 64 bits.

`romsizes.py` does not catch either.

## The print view

`views/PrintView.h` is `TPrintView`, view class 94, the view a transport renders an item into. The ROM's `PrintNow` (the transport's, 0x4c7f39) makes it with `BuildContext`, puts the item's fields in `fields` and the printer frame in `printer`, and `RenderIt` opens it, sends it `Render` and closes it.
- **Command 56 prints.** `ROMRealDoCommand` forks the world, so the fork takes the event loop over while the job runs in the original task. When the world cannot fork, the command is taken and nothing printed.
- **The job.** The printer is made from the printer frame's `imagingName` and `driverName` (`MakePrinter`) and opened with the fields. The view's `viewShowScript` runs with the printer's port current, and `PrintPages` prints the pages.
- **The pages.** Each page is drawn once per band: the view draws the band's rectangle (or the whole print form when `fUseFullPage` is set) until `RepeatPage` answers false. The transport's progress is set as the bands go (`SetPrintProgress`; a fax's through `SetFaxPrintProgress`, `gTransportProgress`), and the next page comes from `printNextPageScript`. A fax whose page failed is tried again.
- **Errors.** The job's error goes in the fields' `error` slot; the ROM prints it to the debugging port (`PrintObject`, the REP's output on the host). A printer problem goes to `HandleProblem`, which shows the root's `printProblem` slip.
- **Command 57 cancels**; 44 is taken and ignored.

**What floats over it on the screen does not reach the paper.** The print view is a child of the root view, so it has a clipper, and the clipper's visible region is wide open. `TView::SetupVisRgn` (0x00267974) stops at the first ancestor with a clipper, after intersecting its visible region, so no front mask is subtracted. The transport's floating "Sending" slip is in front of the print view the whole time. Until the host's `SetupVisRgn` stopped there as the ROM's does, the slip's screen rectangle was cut out of every page, taking the fax cover page's title and rule with it.

## The fax driver

`print/FaxDriver.h` is `TFaxDriver`, the ROM's `TDotPrinterDriver` for fax, over `comms/fax/FaxToolInterface.h`.
- **`TFaxToolInterface`** drives the fax tool (serv `'faxs'`, over the modem tool `'mods'`) with raw comm-tool requests to the tool's port:
  - opening: bind then connect, or listen then accept when answering;
  - pages: option requests `'fsgp'` and `'feom'`, whose op is 1 BeginPage, 2 EndPage, 3 Confirm or 5 PrintBand;
  - a band: a put request carrying the `'fcsb'` option;
  - closing: kill, then disconnect (opcode 6).

  It can run synchronously or asynchronously; four messages go out at once at most. The replies come back to the world's port with `this` as the refcon, and `TFaxDriverData` (its subclass) turns each into a call such as `OpenSessionComplete` or `PrintBandComplete`. Those unblock the job's `PrReleaseControl` through the printer's `fBlocked`. Its vtable is at ROM 0x1e798, `TFaxDriverData`'s at 0x1d7f0.
- **`Open`** starts the fax service over the modem tool and dials the connection frame's `phoneNumber` with the user's dialling preferences. The resolution is 1 (normal) when the frame's `faxResolution` says so, otherwise fine. `localId` goes to the other end, and the other end's identity comes back in `remoteId`.
- **The page** (`GetPageInfo`) is what the session agreed, from the four page tables at ROM 0x378be4-0x378c14 (`FaxDriverTables.cpp`, made by romtable.py). It is 200 dots an inch across, and 200 (fine) or 100 (standard) down:

  | Paper | Width | Fine rows | Standard rows |
  |---|---|---|---|
  | letter | 1650 | 2050 | 1025 |
  | A4 | 1596 | 2188 | 1094 |

  The margins are a quarter of an inch (`vRes >> 2` lines), and the line is 1728 pixels as T.4 has it.
- **Bands** (`GetBandPrefs`) are 25 lines deep, taken asynchronously, with `wantMinBounds`. `ImageBand` sends the rows above the black's bounds as white lines (`PrintBand(nil, n, ...)`), the rows the black covers (whole rows) as a band, and the rest as white lines again. A band with no black is all white lines.
- **`ContinueIO`** lets the job go on after a reply of 0, -44004 or -22005.

Tests: ctest `host.NewtonFaxSend` and, over a Class 2 modem (`fakemodem.py --fax-class 2`, where the modem runs T.30 and the fax tool drives it with `+FDT`/`+FET`), `host.NewtonFaxSendClass2` (demo `src/host/demo/fax-send.ns`) sends a note through the fax routing slip to `tools/modem/fakemodem.py --fax-answer`. `host.NewtonFaxSend.check` then runs `tools/modem/faxcheck.py` on the two pages that arrive (the cover page and the note, 1728 x 2148 each): the pages must be there and not blank, the cover page's title and rule and the note's text must be inked, and each page is written out as a PNG (`build/fax-sent.png`, `build/fax-sent-2.png`).

### Fax driver quirks kept

- `TFaxDriver::Open` does not look at the session's own error. It answers the driver's error (a cancel while it waited), and a failed session shows on the first page instead.
- `GetPageInfo`, asked before the session's open has come back, waits by spinning on a flag that nothing can set while it spins, so it never returns. `TDotPrinter` only asks after `Open`, which has waited properly.
- `ImageBand` answers 1, not the error, when the band before it failed.
- The Newton's Class 1 frames go out without an FCS, as a DTE's `+FTH` frames should, and its DCS asks for fine resolution (`00 46 00`).

### Fax deviations

- `TFaxToolInterface::SetMinScanLineTime` answers nought where the ROM reads the word after the option's time, past the end of the option. Nothing in the ROM calls it.
- The `TCMARouteAddress`, `TCMAPhoneNumber` and `TCMOServiceIdentifier` constructors size their options from the host's structs.

## The host's printer

`print/host/HostPrinter.h` is a printer of the host's own. It is not in the ROM. It shows that `TDotPrinterDriver` is a clean seam: a new printer is one implementation of that protocol, and the ROM's `TDotPrinter` still draws the page.
- **`THostPrinterDriver`** prints at 300 dots an inch. It keeps the whole sheet in host memory: letter is 2550 x 3300 dots, A4 is 2480 x 3508. The printable area is placed at the printer frame's `printableOrigin`, and its size comes from the `printerPageBounds` the Print slip chose.
  - Bands are 200 dots deep, halved as far as 25 when memory is short. 25 dots is exactly 6 points at 300 dpi, so the bands meet without a seam.
  - `ClosePage` writes the sheet as `<dir>/print-NNN.png`, a one-bit gray PNG. The writer uses deflate's stored blocks, so it needs only the C library. NNN counts the pages printed since the program started.
  - newton's `--print-dir DIR` sets the directory; the default is the working directory.
- **How a user reaches it.** `HostInstallPrinter` runs from the newt world's PreMain hook. It registers the driver and adds a printer frame, "Host printer (PNG files)", to `AvailablePrinters`: `imagingName` "TDotPrinter", `driverName` "THostPrinterDriver", type `serialSym`, and the StyleWriter's page bounds and origin.
  - That global array is the ROM's list of printer types. The Print slip's printer picker ends with "Choose Other Printer", whose chooser lists the array's serial printers. Picking one there makes it `userConfiguration.currentPrinter`, and the slip remembers it.
  - The script function `HostPagesPrinted()` answers how many pages have been written.
- **The picker needed `GetNames`** (`comms/AppleTalkNatives.cpp`). It is the AppleTalk native that turns NBP addresses ("name:type@zone") into names, and the slip calls it on the recent printers' names.

Tests: ctest `host.NewtonHostPrinter` runs demo `src/host/demo/print.ns`. It prints a note (Notepad, Action, Print Note, the printer chosen through Choose Other Printer, Print, Now) and a Names card (Action, Print Name). Then `host.NewtonHostPrinter.check` runs `tools/imaging/pagecheck.py`, which checks the two pages in `build/print`: 2550 x 3300, not blank, the note's text inked, and the card's frame and address inked.

Seen on the way, not yet looked into: a card added with `cardfile:AddCard` shows no name, neither on the screen nor on the printed card. The address and phone draw.

## Not yet

- `TPSPrinter`, the PostScript imaging engine, and the drivers other than the fax driver (a new printer goes behind `TDotPrinterDriver`, as the host's printer does).
- `TPrDriverPart`, the 'prnt part handler for printer-driver packages.
- `TQDLibraryDriver`, the QuickDraw library offered to drivers.
