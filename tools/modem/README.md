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
  connects it.  Fax classes are answered but not carried yet.
- **Inputs**: `--number NUMBER=HOST:PORT` (repeatable), `--incoming
  HOST:PORT`, `--speed BPS` (what CONNECT reports, 19200), `--identity TEXT`
  (the `ATI0/3/4` answer; the default, `fakemodem 1.0`, is a modem the ROM
  does not know, which gets its generic profile), and either `--spawn
  <program...>` (a newton, run and waited for its `[host] serial port N`
  line) or `--connect HOST:PORT` (a newton already running).
- **Outputs**: each command and answer (`fakemodem: <- ATI4`, `fakemodem:
  -> OK`), each call made or answered and the bytes carried each way; with
  `--spawn`, the program's output passed through and its exit status
  answered.
- **Invocation** (ctest `host.NewtonModemDial` and `host.NewtonModemAnswer`):

```
python tools/modem/fakemodem.py --number 5551212=127.0.0.1:52375 \
    --spawn build/host/host/newton --rom build/MP2x00US/rom.bin \
    --serial-port 0 --tcp-echo 52375 --headless 120 --script src/host/demo/modem.ns
python tools/modem/fakemodem.py --incoming 127.0.0.1:52376 \
    --spawn build/host/host/newton --rom build/MP2x00US/rom.bin \
    --serial-port 0 --tcp-echo 52376 --headless 120 --script src/host/demo/modem-answer.ns
```

On Windows give `--spawn` the program's full path (`...\newton.exe`).
