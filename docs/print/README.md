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

  The ROM's drivers are the fax driver, the StyleWriter group, the LaserWriter LS and HP PCL (`ThpPCL`). The fax and HP PCL drivers are reconstructed; the StyleWriters and the LaserWriter LS are not, by the owner's decision. The host's PNG printer is a new implementation of this protocol.
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

## The PostScript printer

`print/PSPrinter.h` is `TPSPrinter`, the ROM's `TPrinter` for PostScript printers (0x00155f28-0x0015a0ac, 0x0021ac14-0x0021bc00), and `TPSPrinterDriver`, the protocol its connection plugs into (the glue at 0x003881bc-0x00388250). A printer frame names it with `imagingName` "TPSPrinter".
- **No bitmap.** The printer port has no bits. Its drawing procs are the PostScript bottlenecks (`PSBottlenecks.cpp`: `PrStdRect`, `PrStdText` and the rest), which turn each QuickDraw verb into PostScript and send it as they go. The page is drawn once: `RepeatPage` is false.
- **The page.** 300 dots an inch: letter 2400 x 3233 dots, A4 2331 x 3323, so the port is 576 x 776 at 72 dpi. The prolog's `FlipCTM` puts PostScript's origin at the top left, so the port's coordinates are written as they are.
- **The document** is DSC. The first `OpenPage` sends the header: `%!PS-Adobe-3.0`, the title (the connection frame's `title`), `%%Creator: Out Box`, the date (`DateNTime(Time())`), the user (the `name` preference), then the prolog (`gPostscriptHeader`, `gPostscriptHeader2`: the drawing procedures, the Mac encoding `MacVec`, the patterns for Level 1 and Level 2), then a Mac-encoded copy of every face of every family in `vars.psFonts` (`/Helvetica-Mac /Helvetica EncodeFont`) but a Shift-JIS one (`prencoding` 6), and the job's name (`jn`). Every page is `%%Page: N N`, `90 rotate` when landscape (the port's clip and visible boxes turned too), the page setup and `showpage`; `Close` sends the trailer with `%%Pages: N`. The constant text is generated by romtable.py into `PSPrinterTables.cpp` (its new `text` type: one string, a literal per line; `icstr`: pairs of a code and a string).
- **Every bottleneck starts the same way.** A shape the port is recording - a picture with its pen hidden, a region (a frame), or for a polygon a polygon - goes to the standard proc with the pen hidden. Then, if the job is going and the pen is visible: the clip (`SetClip`: `PreC`, the box of the clip and visible regions' intersection, `PoC`, sent when either has changed), the gray from the verb's pattern (`SetGrayLevel`: a plain gray as `N 64 div setgray`, anything else made the current PostScript pattern and filled with `PatternFill`; invert is not supported), and the shape.
- **Shapes.** A fill is a path and `fill`. A frame is a path inset by half the pen and stroked by `SclPen`, which scales a unit pen to the port's pen size (`h v Pen`). A level or upright line is stroked at the pen's width; a slanted one is the outline of the pen dragged along it, filled (`Draw1QDLine`). A round rectangle is four corner arcs joined by its sides; arcs and ovals use the prolog's `FrameOval`; QuickDraw's curves become PostScript's cubic `curveto`; `paths` (how ink reaches a printer) are drawn a contour at a time through the path walker. Regions are not supported: a comment says so.
- **Text** goes a style run at a time: the run's screen family's `psName` looked up in `vars.psFonts`, its face's PostScript name, the size scaled by `psScale` (`DoSelectFont`: `/Helvetica-Mac 12 SF`); the characters converted to the font's encoding, escaped and shown by `show` - or `awidthshow` with the justification's extra for each character and each space, which `StdText`'s operation 0x400 answers (`qd/TextObject.cpp`, reconstructed for this). The Mac's maths signs, which PostScript's text fonts lack, switch to the Symbol font. Superscripts and subscripts move by three tenths of the size; underline uses the prolog's `UL`. An ink word is drawn by its glyph (`TInkWordGlyph`), which comes back through `PrStdPaths`.
- **Bitmaps** are hex rows for the prolog's `bimage`, a deeper map's pixels through its gray table, the transfer inverted (a Newton pixel of 1 is black). A fax's bitmap moves the transport's progress.
- **Errors.** -44000..-44099 are fatal, -44100..-44199 problems put to the user (`CallHandleProblem`), the rest passed over (`HandleError`); a busy printer is asked again every so often (`Open`).
- **`TPSPAPDriver`**, the ROM's one PostScript driver (the network LaserWriter's), is PAP over AppleTalk. Reconstructed is what is not AppleTalk's: the page brackets and `IsProblemResolved` (each takes the printer's status), `CancelJob`, and the reading of a PostScript printer's status message (`InterpretPAPString` over `gPSStatusStrings`: "status: busy" is busy, "PrinterError: out of paper" a problem). Its PAP calls are the host's IPP (below, "Printers on the network"); it is registered in the ROM's place in `InitPrintDrivers`.

Test: ctest `print.PSPrinter` prints two pages through a test driver that keeps the document, and checks the header, the prolog, each shape as the bottlenecks write it, the text in Helvetica, and the trailer; also `FixedToString` and the status strings.

### PostScript ROM bugs kept

- `FixedToString` answers the text after the minus sign, so a negative number printed through its answer prints positive. Where a caller prints the buffer itself (`SendRectangle`'s second and third corners, `Draw1QDLine`'s slanted lines, `EmitText`'s shifts) the sign is kept.
- `Draw1Path` fills a patterned contour with `PatternFIll`, misspelt: the PostScript stops on an undefined name. It also moves a line's end by a quarter of the pen when filling as well as framing, and moves a curve's end only after drawing it.
- `PrStdCurve` sets the line width without saving the old one, so its closing `SLW` finds nothing on the PostScript stack.
- `CountBitsInPattern` shifts both the row and the mask for two- and four-bit patterns, so it counts each row's first pixel over and over and never the others. A one-bit pattern's count reads only its first eight bytes.
- `FlushBuffer` takes the space's extra down by the character's again for every string after the first; and a string cut at 245 bytes ends after a character that the next string starts with again.
- `PrStdBits` ignores the source rectangle's top. A map deeper than 8 bits sends nothing and leaves `gsave` unmatched; the realigning buffer is never given back.
- `SendPSBinary`, sending again after a problem, does not make the size smaller.
- `Delete` does not give back `fFont`'s RefHandle.

### PostScript deviations

- `fBuffer` has 16 bytes of slack: `FlushBuffer` can write six past the ROM's 0x100, over `fEncoding`.
- `PrStdText` walks the text object's runs from its run lengths: the host's text objects have no `TextWalker`/`ScanNextChunk`.
- The title and user name are cut at 99 characters (the ROM's 100-byte buffers overflow on a longer one).
- `UnicodeToDestmap` answers a bullet for an encoding the host has no table for (Shift-JIS, which no U.S. font asks for).
- A bitmap with no gray table is sent as it is (the ROM reads whatever lies at address 0).
- `QDProcs` are allocated by `sizeof`.

## The HP PCL driver

`print/HPPCL.h` is `ThpPCL`, the ROM's `TDotPrinterDriver` for HP's PCL printers (0x002e56c0-0x002e6158). `TDotPrinter` draws the page in bands of 50 rows at 300 dots an inch (letter 2400 x 3150, A4 2331 x 3324); `ImageBand` sends them as PCL 5 raster graphics.
- **A page:** PJL's universal exit and a reset (`ESC %-12345X ESC E`), 300 dpi, the raster's height and width, `ESC *r0A`; each row with black as `ESC *b2m<n>W` and the row packed with PackBits; the white rows skipped by count (`ESC *b<n>Y`), the band's minimum bounds saying where the black is; then `ESC *rC ESC E ESC %-12345X`. After a cancel the rest of the page is skipped.
- **The connection** is a comm endpoint: the serial port at 57600 bps with hardware flow control for a DeskWriter (`prModel` 0), or IrDA's IrLPT for the LaserJet 5MP and DeskJet 340 (`prModel` 1, 2). A failed write is the job's error: an aborted one the user's cancel, one that timed out lost contact.
- **HOST EXTENSION:** `gPrinterServiceHook(driver, options)` is asked first; when it answers true it has put in the options of a printer model it knows (the service and anything else it needs: the IPP printer's URI). Nil on a device; the IPP printer sets it.

Test: ctest `print.HPPCL` images three bands through a test endpoint and reads the PCL back: the commands, the skips, and every row unpacked and compared with the band.

## Printing by IPP

`print/host/HostIPP.h` is the host's way out for both printers: a printer on the network, reached by IPP (RFC 8011) through the host's own network stack (`hal/host/HostSockets.h`). Not in the ROM.
- **The seam** is the ROM's own: the drivers send through a comm endpoint. The host adds one more service, `'ippc'` (`HostIPPTool.h`: `THostIPPTool`, `THostIPPService`). Connecting resolves the configured printer and opens a TCP connection. The first write sends the HTTP POST's head and the Print-Job request (version 1.1; `document-format` from the first bytes: `%!` PostScript, ESC PCL) as the first chunk of the body; every write is one more chunk. The disconnect sends the last chunk, reads the printer's answer and logs it (`[host] IPP: the printer answered 200, successful-ok (0x0000), job 1`). The socket is polled, so the tool's task never waits in the host.
- **The PostScript printer** gets a driver of the host's, `THostIPPPSDriver`, which writes its text to an endpoint of that service. The HP driver gets the service through `gPrinterServiceHook`, for the printer model 100.
- **Choosing it.** `newton --ipp-printer ipp://host:631/path` (or `NEWTON_IPP_PRINTER`, or the script's `HostSetIPPPrinter(uri)`) adds "IPP printer (PostScript)" (the ROM's network PostScript printer's page) and "IPP printer (HP PCL)" (the DeskWriter's) to `AvailablePrinters`; the Print slip's Choose Other Printer lists them. `HostIPPJobs()` and `HostIPPLastStatus()` say what the printer answered. `ipps://` (TLS) is not offered.
- **A job that does not print** is told to the user as the ROM tells of any failed job: the driver's error, a `kPR_ERR_...`, becomes the Print alert with the ROM's own message. The endpoint's disconnect carries no error, so the job goes with a *ticket* (`HostIPPNewTicket`, in the `'iuri'` option) and the tool leaves what became of it there for the driver to read once the endpoint is closed (`HostIPPTicketResult`): a printer that cannot be reached is `kPR_ERR_NotFound` ("No printer is connected."), a refusal - any answer but successful-* - `kPR_ERR_PrinterError` ("Printer problem."), server-error-busy or HTTP 503 `kPR_ERR_Busy`, a connection that ends with no answer `kPR_ERR_LostContact` ("Lost contact with the printer."). The PostScript drivers return it from `Close`; `ThpPCL` takes it through `gPrinterCloseHook` (HOST EXTENSION, beside `gPrinterServiceHook`). (A PCL printer that cannot be reached is "Newton is unable to print.", as the ROM's `Open` makes any failed connection.)
- **A printer problem** - no paper, a jam, a door open, no ink, off-line - is put to the user as the ROM's PostScript driver puts its printer's status messages: the PostScript drivers ask the printer's state (`HostIPPPrinterStatus`: IPP Get-Printer-Attributes for printer-state and printer-state-reasons, on a socket of the print task's own, waiting in `PrReleaseControl`) where `TPSPAPDriver` asks for PAP status - as each page opens and closes and every eighth write - and while it reports one, the next write fails with it, as PAP's `PutData` does. `TPSPrinter` then shows the print problem slip ("A printing problem has occurred (The printer has no paper.)") and asks `IsProblemResolved` - the state again - until the printer is well, when the job goes on. A reason with `-warning` or `-report` is no problem; a printer stopped for a reason not listed is off-line. A printer that will not say is taken to be well.

Tests: ctest `print.HostIPP` (the URIs, the request's bytes, answers with a Content-Length, in chunks and cut short, the Get-Printer-Attributes request and the problems states come to, what becomes of a job); ctests `host.NewtonIPPRefused` and `host.NewtonIPPPCLRefused` (`ippprinter.py --status 0x040a`: "Printer problem."), `host.NewtonIPPDown` (`--down`: "No printer is connected.") and `host.NewtonIPPProblem` (`--problem media-empty:2`: the problem slip, then the job printed); ctests `host.NewtonIPPPostScript` and `host.NewtonIPPPCL` (`src/host/demo/print-ipp.ns`) print a note through each to `tools/print/ippprinter.py`, and `.check` runs `tools/print/jobcheck.py` on the document saved (the PostScript's structure and the note's text; the PCL's commands and its raster, the note's line inked) - `tools/print/README.md`.

## Printers on the network

Two ways to find a printer on the network and print to it, both by the owner's decision, both over the host's own DNS-SD service browsing (`print/host/dnssd/HostDNSSD.h`: `DnsServiceBrowse`/`DnsServiceResolve` on Windows, `avahi-browse` on Linux and the BSDs - never a resolver of our own). The browse looks for `_ipp._tcp`, and each printer found is its instance name, an `ipp://address:port/rp` URI (the TXT record's `rp`, default `ipp/print`) and the formats its `pdl` names (PostScript, PCL; compared without case - HP's say `vnd.hp-PCL`). It runs on a host thread; a lookup is started and then asked how many it has found, as NBP's are. It is pluggable: `HostDNSSDInject` or `NEWTON_FOUND_PRINTERS` ("NAME|URI|ps,pcl;...") puts a list in its place, which is how the ctests find `tools/print/ippprinter.py --advertise`.

**The ROM's own chooser.** The Print slip's printer picker has "Choose Network LaserWriter", which opens the ROM's network chooser (`netChooser`): it asks `HaveZones`, `GetMyZone` and `GetZoneList`, looks the printers up with `NBPStart("=:LaserWriter@zone")`, lists `NBPGetNames` while `NBPGetCount` grows, and on its close box (`_defaultButton`, `networkChooserDone`) makes the picked printer the current one: a clone of `AvailablePrinters`' network PostScript printer (driver `TPSPAPDriver`, imaging `TPSPrinter`) with `printerName` "Office Laser:LaserWriter@Local network". DEVIATION (`print/host/HostNetworkPrinters.cpp`): AppleTalk is not reconstructed; those natives (and `OpenAppleTalk`/`CloseAppleTalk`) are answered from the browse - one zone, "Local network", whose LaserWriters are the IPP printers that take PostScript - and `TPSPAPDriver`'s PAP calls are host stand-ins (`print/host/HostPAPDriver.cpp`) that find the printer by its object name and send the job to it by IPP. HOST ADDITION: `NBPStop`, which the chooser calls as it closes, flushes the user configuration (`FlushUserConfig`), because the slip's `networkChooserDone` assigns `currentPrinter` without - so the choice outlasts a restart.

**The host's Network Printers panel** (`print/host/HostPrinters.ns`, compiled in and started by `HostInstallIPPPrinters`; registered with `RegPrefs`, so it is the last item of the Prefs list, with a printer icon). It lists each printer found once for each format the host's printers can send it ("Office Laser - PostScript", "Office Laser - HP PCL"; "(added)" after one added, and the printers added that are not found now), with Look again, Add and Remove. Adding one makes a printer frame - `THostIPPPSDriver`/`TPSPrinter` or `ThpPCL` (model 100)/`TDotPrinter` - carrying the printer's own `hostService` (its name) and `hostURI`, offers it in `AvailablePrinters`, and makes it the current printer as the slip's `saveNew` does (`currentPrinter`, the two recent `printers`) through `SetUserConfig`, which keeps it in the System soup. The printers added are kept in the panel's own entry of the System soup (`GetAppPrefs('|NetworkPrinters:host|)`, its `printers`) - the ROM keeps no list of printers but the two recent ones - and offered again at boot. Remove takes one away: no longer kept nor offered, gone from the recent printers, and when it was the current printer the most recent other one is current instead, else the one a new Newton starts with (the default user configuration's, magic pointer 285: the StyleWriter).

**Which printer a job goes to.** A printer frame with a `hostService` is looked for by that name each time it prints (`HostDNSSDResolve`, waiting up to four seconds for a browse: an address and port need not last), the `hostURI` taken when it is not found; a frame with neither goes to `--ipp-printer`'s. The IPP tool takes the URI as an option of its own (`'iuri'`, `THostIPPURIOption`); `gPrinterServiceHook` now hands `ThpPCL` and its options to the host, which adds the service and the URI.

Tests: ctest `print.HostDNSSD` (an injected list, and a real browse of the local network, which must finish whatever it finds); ctests `host.NewtonPrintNetworkChooser` (`src/host/demo/print-network.ns`: the chooser driven with the pen, the PostScript job checked by `.check`) and `host.NewtonPrintAddPrinter` (`print-addprinter.ns`: the panel, its HP PCL printer added), each then `.restart` on the same store, the printer still chosen and the note printed again (the fake printer on a new port: found again by its name); and `host.NewtonPrintAddPrinter.remove` and `.removed` (`print-removeprinter.ns`): the printer taken away in the panel, and after a restart still gone and not in the Print slip's printer picker.

## Not yet

- `TLaserWriterLSDriver` and the StyleWriter group, by the owner's decision; AppleTalk itself (PAP, NBP, ADSP).
- `TPrDriverPart`, the 'prnt part handler for printer-driver packages.
- `TQDLibraryDriver`, the QuickDraw library offered to drivers.
- `ipps://` (IPP over TLS).
- A PCL printer's problems (`ThpPCL` has no status over IPP).
