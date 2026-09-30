# The tablet driver, the inker and the pen's calibration

How a pen touching the glass becomes samples in the tablet buffer, how
those reach the stroke world, and how Align Pen works out and keeps the
calibration that turns the panel's readings into the screen's
coordinates.  Reconstructed from the MP2x00 US ROM; the code is
`src/recognition/TabletDriver.h`, `Inker.h`, `InkerNatives.cpp`,
`TabletBuffer.h`, and the host's panel in `src/hal/host/HostTablet.h` /
`HostTabletDriver.cpp`.  Everything here was read from the ROM's
disassembly (`analysis/disasm.py`) and Ghidra's decompile of the
functions each piece cites.

## The pieces

```
 panel ──ADC──> TTabletDriver ──samples──> tablet buffer ──> TInker ('inkr) ──> stroke world
 (interrupt)    (gTabletDriver)            (250 words)        RealStrokeTime     {'newt,'idle,'inkr}
                  ConvertSample: calibration                  live ink           to the Newt port
```

* **The tablet driver** is a protocol, `TTabletDriver` (22 slots, the
  ROM's glue at 0x00385b8c-0x00385cb8).  `TabInitialize` (0x0025065c)
  asks the registry for `"TMainTabletDriver"` first and only then makes
  the MP2x00's own, `TResistiveTablet` - which is how a machine's own
  driver takes the place of the ROM's.  `TResistiveTablet` (the resistive
  panel read through the Voyager's ADC, a state machine over the pen-down
  interrupt and a timer) is hardware and NOT YET; the host registers its
  own `TMainTabletDriver`.
* **A sample** is `x8 << 18 | y8 << 4 | pressure`: x and y in eighths of
  a pixel.  The driver works it out from the raw 12-bit reading as
  `TResistiveTablet::ConvertSample` (0x0005b068) does: `X = xScale * raw
  + xOffset` in 16.16, likewise Y, X kept on the screen and Y only kept
  off its top, then packed as `(X & 0x7ffe000) << 5 | (Y & 0x7ffe0ff) >>
  9 | 4`.  While the inker calibrates (`SetDoingCalibration`) the raw pair
  itself goes in instead, x in the high field.
* **The calibration** (`Calibration`, 0x14 bytes) is those four 16.16
  numbers and two bytes (1, 1 after a calibration; `TResistiveTablet::Init`
  starts them at 200, 230).
* **The inker** is the `'inkr` world (`TInker`, 0x002173ec-0x00219100).
  `IInker` starts the tablet over the screen with the inker's port as the
  one the driver wakes; `TBCWakeUpInker` sends it `{'newt, 'inkr, 2}` as
  each record goes into the buffer.  Its handler reads the buffer into
  the strokes and wakes the application with `{'newt, 'idle, 'inkr}` when
  a stroke changed, keeping itself going on a 50 ms idler while the pen
  is down.  Everything else a script asks of the pen is an RPC to it:
  commands 7-16 and 0x14/0x15 the pen modes, 0x16 read the calibration,
  0x17 write it, 0x21 whether the tablet wants calibrating, 5 the
  calibration screen, 0x33-0x37 the busy box (NOT YET).  The live ink
  (`TInker::Convert`, `DrawInk`, `TLiveInker`) is NOT YET: the host's
  `StrokeTime` draws it.

## The calibration screen (TInker::Calibrate, 0x002180a0)

The driver is told to hand over raw readings and the buffer to keep only
the last (polling mode).  The screen shows the Newton bitmap and some
words, and a target 10 pixels in from the top left corner:

* `GetRawPoint` (0x00217bc4) draws an X there and polls every 5 ms.
  Twenty readings in a row that agree within 24 steps each way invert
  the target ("until it darkens"); the pen lifting then answers a
  weighted median of the twenty - half the tenth and a quarter each of
  its neighbours.  The pen lifting sooner starts the count again.  With
  the pen away it blinks the stylus picture once a second, gives up
  after the time limit (-56101) and abandons the screen when the power
  manager's `'ppen` system event arrives (-56102).
* Then a target 10 pixels in from the bottom right.  The two readings
  give a scale and an offset each way; which raw axis goes with which
  screen axis, and which way round, comes from how the panel is turned
  against the screen (the driver's orientation from `SetDoingCalibration`
  against `GetGrafInfo`'s).
* Then a third target, a quarter of the way down and three quarters
  across, put through the new calibration: if it lands more than ten
  pixels from where it should, all three again.

`CalibrateInker` (0x00141098) is the RPC, with the sleep time (at most
ten minutes) as the time limit at each target; when it worked the ROM's
`savecalibration` block writes the result into the System soup's
`"Calibration"` entry, under the screen's orientation.  `CalibrateTablet`
is what Prefs' Align Pen and the Setup assistant (leaving its Welcome
page) call.  `LoadInkerCalibration` (0x0013fc2c) is what the newt world's
`MainConstructor` and `PowerOff` call: the `loadcalibration` block hands
the kept calibration back, and the calibration screen appears when there
is none (`CheckTabletCalibration`).  `VerifyCalibration`
(`SetDisplayParams`) asks again when the screen turns to an orientation
with nothing kept.

The calibration binary a script sees is kept big-endian on every host
(`InkerNatives.cpp`), as the Newton wrote it.

## The host's panel

`hal/host/HostTabletDriver.cpp` is a `TMainTabletDriver`: newton's
window is the panel, sampled from a host interrupt source every sampling
interval (0xb400 ticks, 80 a second) while the mouse is down.  The panel
reads eight to the pixel and sits square on the display, so the factory
calibration (scale 0x2000, no offset) is exact and a mouse is an
accurate pen out of the box.  A test can put it askew
(`HostTabletSkew(dx, dy, sx, sy)` from a script, or
`NEWTON_TABLET_SKEW="dx,dy,sx,sy"`): the panel then reads the point
`(x*sx+dx, y*sy+dy)`.  `HostTabletTap(x, y, ms)` presses it from a
script.  The panel turns with the window, so it always reads in the
screen's own orientation.

A script's pen is a test's: newton taps the calibration screen's targets
for it (`HostTabletAutoCalibrate`, on whenever `--script` is given;
`HostTabletCalibrationTarget()` answers the last target shown).  The
test pen (`PenDown`/`PenMove`/`PenUp`, `HostTabletPenDown/Move/Up`)
still puts screen-coordinate samples straight into the buffer, so the
tests that draw with it are unchanged; a paced pen's queued records are
fed by the inker's idle.

`src/host/demo/alignpen.ns` (ctests `host.NewtonAlignPen` and
`.restart`) puts the panel askew, walks the Setup assistant (whose
calibration screen corrects it), checks a tap lands true, puts the
factory calibration back (a tap then lands 30 pixels off), runs Align
Pen, sleeps and wakes (the driver shut down and woken, `TabShutDown` /
`TabWakeUp`), and on a restart finds the calibration read back from the
store with no calibration screen.
