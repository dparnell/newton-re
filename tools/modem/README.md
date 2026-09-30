# tools/modem

## fakemodem.py

A Hayes modem for the host build's external serial port, whose telephone
line is TCP.  The reconstructed modem tool (`src/comms/ModemTool.h`, the
ROM's `TClassOneModem`, serv `'mods'`) drives a modem in AT commands; on the
host the serial port is a TCP socket (`hal/host/HostSerialChip.h`) and this
is the modem at the other end of it.

- **Purpose**: answers the commands the ROM sends - identification (`AT`,
  `AT&FE0V1`, `ATI4`/`ATI0`, `ATS0=0`, `ATI5`), the configuration strings,
  the dialing preferences, `AT+FCLASS=`, S-register reads and writes,
  `ATH0`, `ATO0`, `ATA` - with echo (`E`), words or digits for answers
  (`V`) and quiet (`Q`) as the modem is told.  `ATDT<number>` makes a TCP
  connection (the phone book, `--number`, or the number itself as
  `HOST:PORT`), answered `CONNECT <speed>` and bridged to the serial port
  until either end drops it (`NO CARRIER`) or the Newton escapes with `+++`
  and the guard time S12 either side; a refused connection is `BUSY`.
  `--incoming` is a call to answer: once the Newton starts listening (its
  first `ATS1?`) the modem rings once a second, and `ATA` (or S0 rings)
  connects it.  `--fax-call PAGE.pbm` is a Class 1 fax call to answer
  (`+FCLASS=1`; no TCP line - the calling fax machine is the file's
  `FaxCaller`): after `ATA` the modem is sending HDLC at once, as an
  answering Class 1 modem is; the Newton's CSI and DIS are read, and as it
  asks (`+FRH=3`, `+FRM=96`) the caller sends TSI and DCS (V.29 at 9600,
  standard resolution), the training check (1800 noughts), the page (the
  PBM coded by `t4.py`, two fill bytes before each end of line) after the
  Newton's CFR, and EOP; the Newton's MCF (or RTP/RTN) is read and DCN
  sent.  Frames carry their FCS; bytes of the value DLE are doubled and
  each frame or run of data ends with DLE ETX.
- **Inputs**: `--number NUMBER=HOST:PORT` (repeatable), `--incoming
  HOST:PORT`, `--fax-call PAGE.pbm` (1728 pixels wide), `--speed BPS` (what CONNECT reports, 19200), `--identity TEXT`
  (the `ATI0/3/4` answer; the default, `fakemodem 1.0`, is a modem the ROM
  does not know, which gets its generic profile), and either `--spawn
  <program...>` (a newton, run and waited for its `[host] serial port N`
  line) or `--connect HOST:PORT` (a newton already running).
- **Outputs**: each command and answer (`fakemodem: <- ATI4`, `fakemodem:
  -> OK`), each call made or answered and the bytes carried each way; with
  `--spawn`, the program's output passed through and its exit status
  answered.
- **Invocation** (ctests `host.NewtonModemDial`, `host.NewtonModemAnswer`,
  `host.NewtonFaxReceive`):

```
python tools/modem/fakemodem.py --number 5551212=127.0.0.1:52375 \
    --spawn build/host/host/newton --rom build/MP2x00US/rom.bin \
    --serial-port 0 --tcp-echo 52375 --headless 120 --script src/host/demo/modem.ns
python tools/modem/fakemodem.py --incoming 127.0.0.1:52376 \
    --spawn build/host/host/newton --rom build/MP2x00US/rom.bin \
    --serial-port 0 --tcp-echo 52376 --headless 120 --script src/host/demo/modem-answer.ns
python tools/modem/t4.py --test-page --pbm page.pbm
python tools/modem/fakemodem.py --fax-call page.pbm     --spawn build/host/host/newton --rom build/MP2x00US/rom.bin     --serial-port 0 --headless 200 --script src/host/demo/fax-receive.ns
```

On Windows give `--spawn` the program's full path (`...\newton.exe`).

## t4.py

ITU-T T.4 modified Huffman (MH) coding of Group 3 fax pages, written from
the recommendation alone (its tables 2, 3a and 3b), not from the ROM - so
that the reconstructed decoder (`src/comms/fax/T4FaxLine.h`, the ROM's
`TT4FaxLine`) is checked against an independent coder.

- **Purpose**: codes a page (lines of pixels, 1 black) as T.4 bytes - an
  EOL, each line's alternate white and black runs (make-up codes for runs
  of 64 and more, the extended ones past 1728) and its EOL, optional fill
  (nought bits) ahead of every EOL, and the RTC (six EOLs); the bits of
  each byte least significant first, as they go over the telephone.  Also
  decodes such bytes back, and makes a test page (1728 x 200: bars, a
  checkerboard, diagonals, lines starting black, solid lines, strokes,
  single pixels, a frame, runs of every length).
- **Inputs / outputs**: PBM images (P1 or P4 in, P4 out) and raw T.4 byte
  files.
- **Invocation** (ctest `comms.T4Page` writes the page `comms.T4FaxLine`
  decodes):

```
python tools/modem/t4.py --test-page --fill 2 --pbm page.pbm -o page.t4
python tools/modem/t4.py --encode page.pbm -o page.t4 [--fill N]
python tools/modem/t4.py --decode page.t4 --width 1728 --pbm out.pbm
python tools/modem/t4.py --self-test
```
