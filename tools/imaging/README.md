# tools/imaging

Small pure-Python tools (zlib and struct only, no imaging library) for the
pictures the host Newton writes: screen snapshots (binary PGM, from
`ScreenSnapshot` in a script or `hal/host/HostScreen.h`) and the host
printer's pages (PNG, `print/host/HostPrinter.h`).  Python 3.9+.

| Tool | Purpose |
|---|---|
| `png.py` | The PNG reader and writer the others share (`write_gray(path, w, h, rows, depth)`, `read_gray`). `test_png.py` checks it. |
| `pgm2png.py` | A PGM, PBM or PPM (the colour screen's snapshots, `docs/qd/colour.md`) made a PNG so it can be looked at anywhere: `python tools/imaging/pgm2png.py shot.pgm [shot.png]`. |
| `gifwrite.py` | A GIF (GIF87a) written in pure Python - `write_gif(path, w, h, palette, pixels)`, the plainest LZW (literals and clear codes); as a program it writes the colour test picture (four bands in a black frame) that `src/host/demo/www/colour.gif` is: `python tools/imaging/gifwrite.py colour.gif`. |
| `pagecheck.py` | Checks the host printer's pages: how many, their size, that none is blank, that given rectangles are inked, how many lines of text (`--lines`). Used by ctests `host.NewtonHostPrinter` and `host.NewtonPrintLong`. |
| `pgmdiff.py` | Compares two directories of screen snapshots pixel by pixel. |

## pgmdiff.py

```
python tools/imaging/pgmdiff.py BEFORE_DIR AFTER_DIR [--noise AGAIN_DIR] [--ignore-rows FIRST-LAST] [--png OUT_DIR]
```

Inputs: two directories of `.pgm` snapshots with the same names - a set taken
before a change and the same set taken after it (for instance every ctest's
snapshots, copied out of the build directory before and after).  For each
`.pgm` in BEFORE_DIR that has a namesake in AFTER_DIR it prints `same`, or how
many pixels differ, the box they lie in and the rows that hold them grouped
into runs.

- `--noise AGAIN_DIR`: a second run of the "before" snapshots.  A pixel that
  differs between the two before runs - the status bar's clock, a random
  deal, a timer - is not counted as a difference, so what is left is what
  the change did.
- `--ignore-rows FIRST-LAST`: rows left out of the comparison altogether.
- `--png OUT_DIR`: for each pair that differs, a PNG of the after image with
  the differing pixels marked mid-gray, to look at.

Exit code 0 when every pair is the same, 1 otherwise.  It is how the move of
the paragraph line layout to the ROM's `LineLoop` was checked
(`docs/views/README.md`): every pixel it changed on the walkthroughs' and
demos' screens accounted for.
