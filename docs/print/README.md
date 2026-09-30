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

## Not yet

- `TPSPrinter`, the PostScript imaging engine, and the drivers other than the fax driver.
- `TPrDriverPart`, the 'prnt part handler for printer-driver packages.
- `TQDLibraryDriver`, the QuickDraw library offered to drivers.
