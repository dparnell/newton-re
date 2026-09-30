#!/usr/bin/env python3
"""A Hayes modem for the host build's serial port, with TCP for its line.

Purpose
    The reconstructed OS's modem tool (src/comms/ModemTool.h, the ROM's
    TClassOneModem, serv 'mods') drives a Hayes modem on the external serial
    port in AT commands.  On the host that port is a TCP socket
    (hal/host/HostSerialChip.h); this is the modem at the other end of it.
    It answers the commands the ROM sends - the identification ("AT",
    "AT&FE0V1", "ATI4"/"ATI0", "ATS0=0", "ATI5"), the configuration
    strings ("ATE0&C1S12=12W2&K0&Q0" and its kin), the dialing preferences
    ("ATM1L2X4S7=060S8=001S6=003S0=000"), "AT+FCLASS=", S-register reads
    and writes, "ATH0", "ATO0", "ATA" - and its telephone line is TCP:

    - "ATDT<number>" (or DP) calls: the number is looked up in the phone
      book (--number NUMBER=HOST:PORT), or taken as HOST:PORT itself; the
      TCP connection made is answered "CONNECT <speed>" and bridged to the
      serial port until either end drops it ("NO CARRIER") or the Newton
      escapes with "+++" (the guard time S12, fiftieths of a second, of
      silence either side) and hangs up with "ATH0".  A refused connection
      is "BUSY"; one that cannot be made "NO CARRIER".
    - --incoming HOST:PORT is a call to answer: once the Newton has started
      listening (its first "ATS1?", the rings so far) the modem rings -
      "RING" once a second, S1 counting - and "ATA" (or S0 rings, if S0 is
      set) connects it to HOST:PORT, bridged as above.

    Only data (+FCLASS=0) calls are made; the fax classes are answered but
    not yet carried (docs/comms/README.md, "The modem").

Usage
    python tools/modem/fakemodem.py [--number N=HOST:PORT]... [--incoming HOST:PORT]
                                    [--speed BPS] [--identity TEXT] --spawn <program...>
        runs <program> (a newton), waits for its "[host] serial port N"
        line, connects there as the modem; answers the program's exit
        status.
    python tools/modem/fakemodem.py ... --connect 127.0.0.1:3679
        the same with a newton already running.

Inputs / outputs
    Prints what the Newton says to the modem ("fakemodem: <- ATI4") and what
    the modem answers ("fakemodem: -> OK"), each call made or answered, and
    the bytes carried each way when a call ends.  --spawn's output is
    passed through.
"""

import argparse
import re
import select
import socket
import subprocess
import sys
import threading
import time


def log(text):
    print("fakemodem: " + text)
    sys.stdout.flush()


class Modem:
    def __init__(self, dte, numbers, incoming, speed, identity):
        self.dte = dte
        self.numbers = numbers
        self.incoming = incoming
        self.speed = speed
        self.identity = identity
        self.echo = True
        self.verbose = True
        self.quiet = False
        self.sreg = {0: 0, 1: 0, 2: 43, 3: 13, 4: 10, 5: 8, 6: 2, 7: 50, 8: 2, 12: 50}
        self.fclass = "0"
        self.line = None            # the TCP connection that is the call
        self.online = False         # data mode (the call bridged)
        self.buffer = b""
        self.ringing = False
        self.next_ring = 0.0
        self.listening_seen = False
        self.last_dte = time.monotonic()
        self.plus_held = b""        # "+" held back while it may be an escape
        self.plus_time = 0.0
        self.carried = [0, 0]

    # --- answers
    def send(self, data):
        self.dte.sendall(data)

    def result(self, word, number):
        if self.quiet:
            return
        log("-> " + word)
        if self.verbose:
            self.send(b"\r\n" + word.encode("latin-1") + b"\r\n")
        else:
            self.send(str(number).encode() + b"\r")

    def info(self, text):
        log("-> " + text)
        self.send(b"\r\n" + text.encode("latin-1") + b"\r\n")

    # --- the call
    def resolve(self, number):
        number = number.strip()
        if number in self.numbers:
            return self.numbers[number]
        m = re.match(r"^(.*):(\d+)$", number)
        if m:
            return m.group(1) or "127.0.0.1", int(m.group(2))
        return None

    def connect_line(self, address):
        try:
            self.line = socket.create_connection(address, timeout=10)
            self.line.settimeout(None)
            return True
        except ConnectionRefusedError:
            return "BUSY"
        except OSError:
            return False

    def go_online(self):
        self.online = True
        self.ringing = False
        self.sreg[1] = 0
        self.carried = [0, 0]
        self.last_dte = time.monotonic()
        self.result("CONNECT %d" % self.speed, 1)

    def hang_up(self):
        if self.line is not None:
            log("call ended: %d bytes to the line, %d from it" % tuple(self.carried))
            try:
                self.line.close()
            except OSError:
                pass
        self.line = None
        self.online = False

    def dial(self, number):
        log("dialing %s" % number)
        address = self.resolve(number)
        if address is None:
            self.result("NO CARRIER", 3)
            return
        got = self.connect_line(address)
        if got is True:
            log("connected to %s:%d" % address)
            self.go_online()
        elif got == "BUSY":
            self.result("BUSY", 7)
        else:
            self.result("NO CARRIER", 3)

    def answer(self):
        if not self.ringing or self.incoming is None:
            self.result("NO CARRIER", 3)
            return
        got = self.connect_line(self.incoming)
        if got is True:
            log("answered, connected to %s:%d" % self.incoming)
            self.go_online()
        else:
            self.ringing = False
            self.result("NO CARRIER", 3)

    # --- commands
    def command_line(self, text):
        log("<- " + text)
        upper = text.upper()
        if not upper.startswith("AT"):
            return
        body = text[2:]
        i = 0
        ok = True
        answered = False            # a result already given (CONNECT, ...)
        while i < len(body):
            c = body[i].upper()
            i += 1
            if c in " ":
                continue
            if c == "D":
                rest = body[i:]
                if rest[:1].upper() in ("T", "P"):
                    rest = rest[1:]
                semicolon = rest.endswith(";")
                number = rest.rstrip(";")
                if semicolon and self.resolve(number) is None:
                    break           # the start of a number still being dialed
                self.dial(number)
                answered = True
                break
            if c == "A":
                self.answer()
                answered = True
                break
            if c == "O":
                i = self.number(body, i)[1]
                if self.line is not None:
                    self.online = True
                    self.result("CONNECT %d" % self.speed, 1)
                    answered = True
                else:
                    ok = False
                break
            if c == "H":
                i = self.number(body, i)[1]
                self.hang_up()
                continue
            if c == "E":
                n, i = self.number(body, i)
                self.echo = bool(n)
                continue
            if c == "V":
                n, i = self.number(body, i)
                self.verbose = bool(n)
                continue
            if c == "Q":
                n, i = self.number(body, i)
                self.quiet = bool(n)
                continue
            if c == "Z":
                i = self.number(body, i)[1]
                self.reset()
                continue
            if c == "I":
                n, i = self.number(body, i)
                self.info(self.identity if n in (0, 3, 4) else "fakemodem")
                continue
            if c == "S":
                n, i = self.number(body, i)
                if i < len(body) and body[i] == "=":
                    v, i = self.number(body, i + 1)
                    self.sreg[n] = v
                elif i < len(body) and body[i] == "?":
                    i += 1
                    self.info("%03d" % self.sreg.get(n, 0))
                    if n == 1 and self.incoming is not None and not self.listening_seen:
                        # the Newton is listening: the call comes in
                        self.listening_seen = True
                        self.ringing = True
                        self.next_ring = time.monotonic() + 0.5
                continue
            if c == "&" or c == "\\" or c == "%" or c == ")":
                if i < len(body):
                    letter = body[i].upper()
                    i += 1
                    n, i = self.number(body, i)
                    if c == "&" and letter == "F":
                        self.reset()
                continue
            if c == "+":
                m = re.match(r"(?i)FCLASS\s*=\s*([0-9.?]*)", body[i:])
                if m:
                    value = m.group(1)
                    if value == "?":
                        self.info("0,1")
                    elif value in ("0", "1", "2", "2.0"):
                        self.fclass = value
                    else:
                        ok = False
                    i += m.end()
                    continue
                m = re.match(r"(?i)F(TM|TH|RM|RH)\s*=\s*\?", body[i:])
                if m:
                    self.info("3,24,48,72,96" if m.group(1).upper() in ("TM", "RM") else "3")
                    i += m.end()
                    continue
                ok = False
                break
            if c.isalpha():
                i = self.number(body, i)[1]
                continue
            ok = False
            break
        if not answered:
            self.result("OK" if ok else "ERROR", 0 if ok else 4)

    @staticmethod
    def number(body, i):
        j = i
        while j < len(body) and body[j].isdigit():
            j += 1
        return (int(body[i:j]) if j > i else 0), j

    def reset(self):
        self.echo = True
        self.verbose = True
        self.quiet = False
        self.fclass = "0"

    # --- the serial port
    def from_dte(self, data):
        now = time.monotonic()
        if self.online:
            guard = self.sreg.get(12, 50) / 50.0
            silent_before = now - self.last_dte
            self.last_dte = now
            if self.plus_held:
                data = self.plus_held + data
                self.plus_held = b""
            elif data == b"+++" and silent_before >= guard:
                self.plus_held = data
                self.plus_time = now
                return
            self.carried[0] += len(data)
            try:
                self.line.sendall(data)
            except OSError:
                self.lost_carrier()
            return
        self.last_dte = now
        if self.echo:
            self.send(data)
        self.buffer += data
        while b"\r" in self.buffer:
            text, _, self.buffer = self.buffer.partition(b"\r")
            text = text.replace(b"\n", b"").decode("latin-1")
            if text.strip():
                self.command_line(text.strip())

    def lost_carrier(self):
        self.hang_up()
        self.result("NO CARRIER", 3)

    def tick(self):
        now = time.monotonic()
        if self.plus_held:
            guard = self.sreg.get(12, 50) / 50.0
            if now - self.plus_time >= guard:
                self.plus_held = b""
                self.online = False
                log("escaped to commands")
                self.result("OK", 0)
        if self.ringing and not self.online and now >= self.next_ring:
            self.next_ring = now + 1.0
            self.sreg[1] = self.sreg.get(1, 0) + 1
            self.result("RING", 2)
            if self.sreg.get(0, 0) and self.sreg[1] >= self.sreg[0]:
                self.answer()

    def run(self):
        while True:
            sockets = [self.dte] + ([self.line] if self.line is not None else [])
            readable, _, _ = select.select(sockets, [], [], 0.05)
            for s in readable:
                try:
                    data = s.recv(4096)
                except OSError:
                    data = b""
                if s is self.dte:
                    if not data:
                        log("the serial port closed")
                        self.hang_up()
                        return
                    self.from_dte(data)
                else:
                    if not data:
                        log("the line dropped")
                        self.lost_carrier() if self.online else self.hang_up()
                    elif self.online:
                        self.carried[1] += len(data)
                        self.send(data)
            self.tick()


def run_modem(host, port, args):
    dte = socket.create_connection((host, port))
    dte.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    log("on the serial port at %s:%d" % (host, port))
    Modem(dte, args.numbers, args.incoming, args.speed, args.identity).run()


def run_spawned(program, args):
    proc = subprocess.Popen(program, stdout=subprocess.PIPE, text=True, errors="replace")
    port = None
    for line in proc.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
        m = re.search(r"\[host\] serial port (\d+)", line)
        if m:
            port = int(m.group(1))
            break
    if port is None:
        return proc.wait() or 1

    def passthrough():
        for line in proc.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
    t = threading.Thread(target=passthrough, daemon=True)
    t.start()
    modem = threading.Thread(target=run_modem, args=("127.0.0.1", port, args), daemon=True)
    modem.start()
    status = proc.wait(timeout=600)
    t.join(timeout=5)
    log("done (the program answered %s)" % status)
    return status


def address(text):
    host, _, port = text.rpartition(":")
    return host or "127.0.0.1", int(port)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--number", action="append", default=[],
                    help="a phone book entry, NUMBER=HOST:PORT")
    ap.add_argument("--incoming", type=address, help="HOST:PORT of a call to ring the Newton with once it listens")
    ap.add_argument("--speed", type=int, default=19200, help="the speed CONNECT reports (19200)")
    ap.add_argument("--identity", default="fakemodem 1.0", help="what ATI0/I3/I4 answer (an unknown modem)")
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--spawn", nargs=argparse.REMAINDER, help="the newton to run, and its arguments")
    group.add_argument("--connect", type=address, help="host:port of a newton's serial port")
    args = ap.parse_args()
    args.numbers = {}
    for entry in args.number:
        number, _, target = entry.partition("=")
        args.numbers[number] = address(target)
    if args.spawn:
        sys.exit(run_spawned(args.spawn, args))
    run_modem(args.connect[0], args.connect[1], args)


if __name__ == "__main__":
    main()
