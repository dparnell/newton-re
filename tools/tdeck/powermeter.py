#!/usr/bin/env python3
"""What the T-Deck Pro draws from its battery, measured by its own fuel gauge
in one state of the board after another (src/host/esp32, DevicePower).

    <PlatformIO's python> tools/tdeck/powermeter.py <port> [--window S] [--settle S]
        [--reset] [--keep-usb] [--only NAME,...] [--out tmp/tdeck/power.log]

Needs pyserial (PlatformIO's own Python has it).  The board must be running
the reconstruction (the NewtonScript prompt on its USB port) with its
battery fitted.

The board's charger is told to leave its USB input alone
(DevicePowerSet('battery, true): the BQ25896's EN_HIZ), so that the board
runs from its battery while the USB stays plugged in for the prompt and the
gauge's Current is everything the board draws; it is put back at the end
(and at the board's next start if this is cut short).  --keep-usb measures
with the USB powering the board instead (the gauge then sees only the
charging).

Each state is set up by NewtonScript lines, left to settle (--settle, 15 s),
then the gauge's one-second samples are averaged over --window seconds (60)
with nothing said to the board meanwhile.  The states, cumulative - each
switches one more thing off:

    as-is      the board as it was found
    keylight   the keyboard's light off (DevicePowerSet('keyLight, nil))
    gps        the GPS receiver off (HostSetSetting('gpsPower, nil))
    lora       the LoRa radio off (HostMeshSetPower(nil))
    cell       the 4G module off (HostSetSetting('cellPower, nil))
    wifisave   the WiFi's power save at its most (DevicePowerSet('wifiSave, 2):
               ESP-IDF's max modem - asleep across several beacons; its
               default, min modem, wakes for every one)
    wifi       the WiFi off (DevicePowerSet('wifi, nil))

A thing already off is left off and says so.  At the end everything is put
back as it was found (the radios' switches are kept in NVS by the board, so
a run cut short can leave one off: switch it on again in Prefs).  The
table (mA drawn, the step's saving, mV, samples) goes to the console and
--out.
"""

import argparse
import re
import sys
import time

import serial

# (a reply's first characters can be lost once the board runs from its
# battery - seen: "WR:TRUE" - so the marker has room to lose some)
MARK = "PWRPWR"
RESULT = re.compile(r'WR:([^"]*)"')
INTRUDER = re.compile(r'(\[[a-z]+\] |newton: )[^\n]*\n')


class Board:
    def __init__(self, port, log, reset):
        self.log = log
        self.port = serial.Serial()
        self.port.port = port
        self.port.baudrate = 115200
        self.port.timeout = 0.2
        self.port.write_timeout = 2
        # (DTR and RTS low: the USB serial/JTAG takes their changing as a
        # reset - serialcapture.py says more)
        self.port.dtr = False
        self.port.rts = False
        self.port.open()
        self.pending = b""
        if reset:
            self.port.rts = True
            self.port.dtr = self.port.dtr
            time.sleep(0.1)
            self.port.rts = False
            self.port.dtr = self.port.dtr
            self.wait_for("the OS is up", 120)

    def lines(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                chunk = self.port.read(4096)
            except serial.SerialException:
                time.sleep(0.5)
                continue
            self.pending += chunk.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
            while b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                text = line.decode("utf-8", "replace")
                self.log.write(text + "\n")
                self.log.flush()
                yield text

    def wait_for(self, text, seconds):
        for line in self.lines(seconds):
            if text in line:
                return line
        raise SystemExit("powermeter: the board never said %r" % text)

    # a NewtonScript expression's value, as text: the expression is wrapped
    # so that the board prints it after MARK
    def eval(self, expr, seconds=20):
        line = 'begin local v := (%s); "%s" & ":" & (if v = true then "TRUE" else if v then v else "NIL"); end' % (expr, MARK)
        self.log.write(">>> " + line + "\n")
        self.port.write((line + "\n").encode("utf-8"))
        # (another task's log line can land inside the reply - seen:
        # '"PWRPWR:TRU[cell] the module switched off' then 'E"' - so a reply
        # with no closing quote is joined with what follows, the
        # "[area] ..." and "newton: ..." lines taken out)
        reply = None
        for text in self.lines(seconds):
            if reply is None:
                if "WR:" not in text:
                    continue
                reply = text
            else:
                reply += "\n" + text
            reply = INTRUDER.sub("", reply)
            m = RESULT.search(reply.replace("\n", ""))
            if m:
                return m.group(1).strip()
        raise SystemExit("powermeter: no answer to %s" % expr)

    def power(self):
        # [mA now, mean mA since last asked, samples, mV]
        text = self.eval('begin local p := DevicePower(); if p then p[0] & " " & p[1] & " " & p[2] & " " & p[3] else "none"; end')
        if text == "none":
            raise SystemExit("powermeter: the board has no fuel gauge (or no battery)")
        return [int(x) for x in text.split()]


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("port")
    parser.add_argument("--window", type=float, default=60)
    parser.add_argument("--settle", type=float, default=15)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--keep-usb", action="store_true")
    parser.add_argument("--only")
    parser.add_argument("--out", default="tmp/tdeck/power.log")
    args = parser.parse_args(argv)

    log = open(args.out, "w", encoding="utf-8", newline="\n")
    board = Board(args.port, log, args.reset)
    board.eval("1")							# (the prompt answering)

    def setting(name):
        return board.eval("begin local on := \"absent\"; foreach s in HostSettingsList() do if s.setting = '%s then on := if s.value then \"on\" else \"off\"; on; end" % name)

    found = {
        "keylight": "on",
        "gps": setting("gpsPower"),
        "lora": "on" if board.eval("GetGlobalFn('HostMeshPowered) and HostMeshPowered()") == "TRUE" else "off",
        "cell": setting("cellPower"),
        "wifisave": "on",
        "wifi": "on",
    }
    # each step's NewtonScript: off, then back on
    switch = {
        "keylight": ("DevicePowerSet('keyLight, nil)", "DevicePowerSet('keyLight, true)"),
        "gps": ("HostSetSetting('gpsPower, nil)", "HostSetSetting('gpsPower, true)"),
        "lora": ("HostMeshSetPower(nil)", "HostMeshSetPower(true)"),
        "cell": ("HostSetSetting('cellPower, nil)", "HostSetSetting('cellPower, true)"),
        "wifisave": ("DevicePowerSet('wifiSave, 2)", "DevicePowerSet('wifiSave, 1)"),
        "wifi": ("DevicePowerSet('wifi, nil)", "DevicePowerSet('wifi, true)"),
    }
    steps = ["keylight", "gps", "lora", "cell", "wifisave", "wifi"]
    if args.only:
        steps = [s for s in steps if s in args.only.split(",")]
    print("found: " + ", ".join("%s %s" % (k, v) for k, v in found.items()), flush=True)

    rows = []
    try:
        if not args.keep_usb and board.eval("DevicePowerSet('battery, true)") != "TRUE":
            raise SystemExit("powermeter: the charger's input could not be cut")

        def measure(name, note):
            for _ in board.lines(args.settle):
                pass
            board.power()					# (the mean starts again)
            for _ in board.lines(args.window):
                pass
            now, mean, samples, mv = board.power()
            rows.append((name, -mean, mv, samples, note))
            print("%-9s %5d mA  %5d mV  (%d samples) %s" % (name, -mean, mv, samples, note), flush=True)

        measure("as-is", "")
        for step in steps:
            if found[step] != "on":
                rows.append((step, None, None, 0, "already " + found[step]))
                print("%-9s already %s" % (step, found[step]), flush=True)
                continue
            if board.eval(switch[step][0]) != "TRUE":
                print("%-9s could not be switched off" % step, flush=True)
                continue
            measure(step, "")
    finally:
        for step in reversed(steps):
            if found.get(step) == "on":
                board.eval(switch[step][1])
        if not args.keep_usb:
            board.eval("DevicePowerSet('battery, nil)")

    print("\n%-9s %8s %8s %6s" % ("state", "mA", "saved", "mV"))
    log.write("\n")
    last = None
    for name, ma, mv, samples, note in rows:
        if ma is None:
            line = "%-9s %8s %8s %6s  %s" % (name, "-", "-", "-", note)
        else:
            line = "%-9s %8d %8s %6d  %s" % (name, ma, "" if last is None else "%d" % (last - ma), mv, note)
            last = ma
        print(line)
        log.write(line + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
