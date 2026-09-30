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

    - --fax-call PAGE.pbm is a fax machine calling (ITU-T T.30 over Class 1,
      +FCLASS=1; there is no TCP line - the calling machine is this file's
      FaxCaller): once the Newton listens the modem rings, and after "ATA"
      it is connected sending HDLC frames (+FTH=3 already in effect), as a
      Class 1 modem answering a fax call is.  The Newton's CSI and DIS are
      read; then, as the Newton asks (+FRH=3, +FRM=96), the caller sends
      TSI and DCS (V.29, 9600, standard resolution, 1728 pixels), the
      training check (1.5 s of noughts), and after the Newton's CFR the page
      - PAGE.pbm coded by t4.py (MH, two fill bytes before each end of
      line) - then EOP; the Newton's MCF (or RTP/RTN) is read and DCN sent.
      Frames go with their FCS (CRC-16, which a Class 1 modem passes on),
      bytes of the value DLE doubled, each ended by DLE ETX.  EOP is sent
      again after 3 seconds if the Newton asks for a frame and has not
      answered (T.30's T4).  +FTH/+FTM/+FRH/+FRM/+FTS are taken outside
      such a call too, though nothing answers there.

    - --fax-answer OUT.pbm is a fax machine the Newton calls (FaxAnswerer):
      "ATDT" in +FCLASS=1 is answered with its CSI and DIS at once; the
      Newton's DCS says the page's width and resolution, the training
      check is answered CFR, each page is decoded by t4.py and answered
      MCF, and the pages are written at the DCN (OUT.pbm, OUT-2.pbm, ...).

    Data calls are +FCLASS=0 (docs/comms/README.md, "The modem").

Usage
    python tools/modem/fakemodem.py [--number N=HOST:PORT]... [--incoming HOST:PORT]
                                    [--speed BPS] [--identity TEXT] --spawn <program...>
        runs <program> (a newton), waits for its "[host] serial port N"
        line, connects there as the modem; answers the program's exit
        status.
    python tools/modem/fakemodem.py ... --connect 127.0.0.1:3679
        the same with a newton already running.
    python tools/modem/fakemodem.py --fax-call page.pbm --spawn <program...>
        a fax call to the Newton, sending page.pbm (1728 pixels wide).
    python tools/modem/fakemodem.py --fax-answer out.pbm --spawn <program...>
        a fax machine for the Newton to call, writing what it sends.
    python tools/modem/fakemodem.py --self-test
        --fax-answer against a scripted Class 1 caller (t4.py's test page).

Inputs / outputs
    Prints what the Newton says to the modem ("fakemodem: <- ATI4") and what
    the modem answers ("fakemodem: -> OK"), each call made or answered, and
    the bytes carried each way when a call ends.  --spawn's output is
    passed through.
"""

import argparse
import os
import re
import select
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import t4                                   # the page's MH code

DLE = 0x10
ETX = 0x03

# What a received frame or page's data waits after CONNECT: a real modem
# says CONNECT when it finds the carrier, and the first byte comes a
# training or a preamble later (V.21's second of flags).  The Newton's modem
# tool sets its serial flow control after reading CONNECT, and a byte that
# came with it would be read under the old settings - an XOFF (0x13, a final
# frame's control field) taken as flow control.
CARRIER_TIME = 0.2

# T.30 facsimile control fields (the X bit, the least significant, set by
# the calling machine)
FCF_NAMES = {
    0x80: "DIS", 0x40: "CSI", 0x20: "NSF", 0x82: "DCS", 0x42: "TSI", 0x84: "CFR",
    0x44: "FTT", 0x8c: "MCF", 0x4c: "RTN", 0xcc: "RTP", 0x4e: "MPS", 0x2e: "EOP",
    0x8e: "EOM", 0xfa: "DCN", 0x1a: "CRP", 0x41: "CIG", 0x81: "DTC",
}


def fcs(frame):
    """The HDLC frame check sequence (ITU-T T.30 and V.42's CRC-16, x^16 +
    x^12 + x^5 + 1, from all ones, least significant bit first, complemented)
    as the two bytes that follow the frame."""
    crc = 0xffff
    for byte in frame:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    crc ^= 0xffff
    return bytes([crc & 0xff, crc >> 8])


def dle_stuff(data):
    return data.replace(bytes([DLE]), bytes([DLE, DLE])) + bytes([DLE, ETX])


def frame_name(frame):
    if len(frame) < 3:
        return "a short frame"
    return FCF_NAMES.get(frame[2] & 0xfe, "FCF %02x" % frame[2])


def t30_id(text):
    """A CSI/TSI FIF: 20 characters, the last first (T.30 5.3.6.2.4), the
    spaces that pad it out after the reversed text - its first character
    the last sent, as the Newton sends its own."""
    return text[:20][::-1].ljust(20).encode("latin-1")


class FaxCaller:
    """The calling fax machine behind the modem: T.30 as the caller."""

    rings = True                        # it rings the Newton once it listens

    def __init__(self, page_path, identity):
        rows = t4.read_pbm(page_path)
        self.lines = len(rows)
        self.page = t4.encode_page(rows, 2)
        self.identity = identity
        self.state = "start"
        self.frames = []                # frames for the Newton's +FRH, in turn
        self.last_command = None
        self.command_time = 0.0
        self.result = None

    @staticmethod
    def control(fcf, fif=b"", final=True):
        frame = bytes([0xff, 0x13 if final else 0x03, fcf]) + fif
        return frame + fcs(frame)

    def dcs(self):
        # V.29 at 9600, standard resolution, 1728 pixels, A4, 20 ms
        return self.control(0x83, bytes([0x00, 0x06, 0x00]))

    def from_newton(self, frame):
        """A frame the Newton sent."""
        name = frame_name(frame)
        if name in ("CSI", "TSI", "CIG"):
            detail = " '%s'" % frame[3:23][::-1].decode("latin-1").strip()
        else:
            detail = (" " + frame[3:].hex()) if len(frame) > 3 else ""
        log("fax: <- %s%s" % (name, detail))
        if name == "DIS" and self.state == "start":
            self.frames = [self.control(0x43, t30_id(self.identity), False), self.dcs()]
            self.state = "dcs"
        elif name == "CFR" and self.state == "cfr":
            self.state = "page"
        elif name == "FTT" and self.state == "cfr":
            log("fax: the training failed; DCS again")
            self.frames = [self.dcs()]
            self.state = "dcs"
        elif name in ("MCF", "RTP", "RTN") and self.state == "confirm":
            self.result = name
            log("fax: the page confirmed %s" % name)
            self.frames = [self.control(0xfb)]
            self.state = "dcn"

    def next_frame(self, now):
        """The frame for the Newton's +FRH (None: wait)."""
        if self.frames:
            frame = self.frames.pop(0)
            self.last_command = frame
            self.command_time = now
            if self.state == "dcs" and not self.frames:
                self.state = "tcf"
            elif self.state == "eop":
                self.state = "confirm"
            elif self.state == "dcn":
                self.state = "done"
                log("fax: DCN sent")
            return frame
        if self.state == "confirm" and now - self.command_time >= 3.0:
            log("fax: no response; EOP again")
            self.command_time = now
            return self.last_command
        return None

    def next_data(self):
        """What the Newton's +FRM receives: the training check, or the page."""
        if self.state == "tcf":
            self.state = "cfr"
            log("fax: -> the training check (1800 noughts)")
            return bytes(1800)
        if self.state == "page":
            self.state = "eop"
            self.frames = [self.control(0x2f)]
            log("fax: -> the page, %d lines in %d bytes" % (self.lines, len(self.page)))
            return self.page
        return b""


class FaxAnswerer:
    """The fax machine the Newton calls: T.30 as the called machine.  Its
    CSI and DIS go first; the Newton's TSI and DCS say how the page comes,
    its training check (TCF) is answered CFR when it is nought bytes, each
    page is decoded by t4.py and answered MCF, and the DCN ends the call."""

    rings = False                       # the Newton calls it

    def __init__(self, out_path, identity):
        self.out_path = out_path
        self.identity = identity
        self.state = "start"
        self.frames = []                # frames for the Newton's +FRH, in turn
        self.fine = False
        self.width = t4.WIDTH
        self.pages = []                 # each page's rows
        self.page_data = None           # the last page's T.4 bytes
        self.result = None

    @staticmethod
    def control(fcf, fif=b"", final=True):
        frame = bytes([0xff, 0x13 if final else 0x03, fcf]) + fif
        return frame + fcs(frame)

    def called(self):
        """The Newton's call answered: CSI then DIS (V.27 ter and V.29,
        fine resolution, 1728 pixels, unlimited length, 20 ms)."""
        self.frames = [self.control(0x40, t30_id(self.identity), False),
                       self.control(0x80, bytes([0x00, 0x4e, 0x08]))]
        self.state = "dis"

    def from_newton(self, frame):
        """A frame the Newton sent."""
        name = frame_name(frame)
        if name in ("CSI", "TSI", "CIG"):
            detail = " '%s'" % frame[3:23][::-1].decode("latin-1").strip()
        else:
            detail = (" " + frame[3:].hex()) if len(frame) > 3 else ""
        log("fax: <- %s%s" % (name, detail))
        if name == "DCS":
            fif = frame[3:]             # (a +FTH frame has no FCS: the modem adds it)
            self.fine = len(fif) > 1 and bool(fif[1] & 0x40)
            width_code = (fif[2] & 0x03) if len(fif) > 2 else 0
            self.width = {0: 1728, 1: 2048, 2: 2432}.get(width_code, 1728)
            log("fax: the page to come is %d pixels wide, %s resolution" %
                (self.width, "fine" if self.fine else "standard"))
            self.state = "tcf"
        elif name in ("MPS", "EOP", "EOM") and self.state == "post":
            self.frames = [self.control(0x8c)]
            log("fax: -> MCF")
            self.state = "page" if name == "MPS" else ("tcf" if name == "EOM" else "end")
        elif name == "DCN":
            self.state = "done"
            self.finish()

    def data_from_newton(self, data):
        """What the Newton's +FTM sent: the training check, or a page."""
        if self.state == "tcf":
            zeros = sum(1 for b in data if b == 0)
            good = len(data) > 0 and zeros >= len(data) * 9 // 10
            log("fax: <- the training check, %d bytes, %d noughts: %s" %
                (len(data), zeros, "CFR" if good else "FTT"))
            self.frames = [self.control(0x84 if good else 0x44)]
            self.state = "page" if good else "tcf"
        elif self.state == "page":
            rows = t4.decode_page(data, self.width)
            rows = [row + [0] * (self.width - len(row)) for row in rows]
            self.pages.append(rows)
            black = sum(sum(row) for row in rows)
            log("fax: <- page %d, %d bytes, %d lines, %d black pixels" %
                (len(self.pages), len(data), len(rows), black))
            self.state = "post"

    def next_frame(self, now):
        """The frame for the Newton's +FRH (None: wait)."""
        if self.frames:
            return self.frames.pop(0)
        return None

    def next_data(self):
        return b""

    def page_path(self, n):
        if n == 0:
            return self.out_path
        root, ext = os.path.splitext(self.out_path)
        return "%s-%d%s" % (root, n + 1, ext)

    def finish(self):
        for n, rows in enumerate(self.pages):
            if rows:
                t4.write_pbm(self.page_path(n), rows)
                log("fax: page %d written to %s (%d x %d)" %
                    (n + 1, self.page_path(n), self.width, len(rows)))
        self.result = "%d page(s)" % len(self.pages)
        log("fax: the call ended, %s received" % self.result)


def log(text):
    print("fakemodem: " + text)
    sys.stdout.flush()


class Modem:
    def __init__(self, dte, numbers, incoming, speed, identity, fax=None):
        self.fax = fax                  # a FaxCaller to ring the Newton with
        self.fax_call = False           # the fax call answered
        self.collect = None             # "hdlc" or "data": the Newton's framed bytes being read
        self.collected = bytearray()
        self.collect_dle = False
        self.pending_frh = False        # +FRH waiting for a frame
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
        self.fax_call = False
        self.collect = None
        self.pending_frh = False

    def dial(self, number):
        log("dialing %s" % number)
        if self.fax is not None and not self.fax.rings and self.fclass == "1":
            log("a fax machine answers")
            self.fax_call = True
            self.fax.called()
            self.pending_frh = True     # dialing, the modem receives HDLC at once
            self.give_frame()
            return
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
        if self.ringing and self.fax is not None and self.fax.rings and self.fclass == "1":
            log("answered a fax call")
            self.ringing = False
            self.sreg[1] = 0
            self.fax_call = True
            self.result("CONNECT", 1)
            self.start_collect("hdlc")          # answering, the modem sends HDLC at once
            return
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
                    if n == 1 and (self.incoming is not None or (self.fax is not None and self.fax.rings)) and not self.listening_seen:
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
                m = re.match(r"(?i)F(TM|TH|RM|RH|TS|RS)\s*=\s*(\d+)", body[i:])
                if m:
                    command = m.group(1).upper()
                    i += m.end()
                    if command in ("TS", "RS"):
                        time.sleep(int(m.group(2)) / 100.0)     # a silence
                        continue
                    self.class1(command, int(m.group(2)))
                    answered = True
                    break
                ok = False
                break
            if c.isalpha():
                i = self.number(body, i)[1]
                continue
            ok = False
            break
        if not answered:
            self.result("OK" if ok else "ERROR", 0 if ok else 4)

    # --- Class 1 (T.31)
    def class1(self, command, value):
        if command in ("TH", "TM"):
            log("-> CONNECT (to send %s)" % ("frames" if command == "TH" else "data at %d" % (value * 100)))
            self.send(b"\r\nCONNECT\r\n")
            self.start_collect("hdlc" if command == "TH" else "data")
        elif command == "RH":
            self.pending_frh = True
            self.give_frame()
        else:
            data = self.fax.next_data() if self.fax_call else b""
            if not data:
                self.result("NO CARRIER", 3)
                return
            log("-> CONNECT (data at %d)" % (value * 100))
            self.send(b"\r\nCONNECT\r\n")
            time.sleep(CARRIER_TIME)
            self.send(dle_stuff(data))
            self.result("OK", 0)

    def give_frame(self):
        """A +FRH answered once the caller has a frame for it."""
        if not self.pending_frh:
            return
        frame = self.fax.next_frame(time.monotonic()) if self.fax_call else None
        if frame is None:
            return
        self.pending_frh = False
        log("fax: -> %s" % frame_name(frame))
        self.send(b"\r\nCONNECT\r\n")
        time.sleep(CARRIER_TIME)
        self.send(dle_stuff(frame))
        self.result("OK", 0)

    def start_collect(self, kind):
        self.collect = kind
        self.collected = bytearray()
        self.collect_dle = False

    def collected_byte(self, byte):
        """A byte of a frame or of data the Newton sends (DLE stuffed)."""
        if self.collect_dle:
            self.collect_dle = False
            if byte == DLE:
                self.collected.append(DLE)
            elif byte == ETX:
                self.end_collect()
            return
        if byte == DLE:
            self.collect_dle = True
        else:
            self.collected.append(byte)

    def end_collect(self):
        data = bytes(self.collected)
        self.collected = bytearray()
        if self.collect == "hdlc":
            if self.fax_call:
                self.fax.from_newton(data)
            else:
                log("<- a frame: %s" % data.hex())
            if len(data) >= 2 and data[1] & 0x10:
                self.collect = None
                self.result("OK", 0)            # the final frame: the carrier off
            else:
                self.result("CONNECT", 1)       # another frame to come
        else:
            log("<- %d bytes of data" % len(data))
            if self.fax_call and hasattr(self.fax, "data_from_newton"):
                self.fax.data_from_newton(data)
            self.collect = None
            self.result("OK", 0)

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
        while self.collect is not None and data:
            byte, data = data[0], data[1:]
            self.collected_byte(byte)
        if not data:
            return
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
        self.give_frame()

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


def self_test():
    """--fax-answer against a scripted Class 1 caller in place of the
    Newton: dial, CSI/DIS read, TSI/DCS and the training sent, CFR read,
    t4.py's test page sent, EOP, MCF read, DCN; the page written must be
    the page sent."""
    newton, dte = socket.socketpair()
    tmp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tmp")
    os.makedirs(tmp, exist_ok=True)
    out = os.path.join(tmp, "fakemodem-answered.pbm")
    fax = FaxAnswerer(out, "fakemodem fax")
    modem = Modem(dte, {}, None, 9600, "fakemodem", fax)
    thread = threading.Thread(target=modem.run, daemon=True)
    thread.start()
    newton.settimeout(10)
    pending = b""

    def read_until(word):
        nonlocal pending
        while word not in pending:
            pending += newton.recv(4096)
        before, _, pending = pending.partition(word)
        return before

    def command(text, answer=b"OK\r\n"):
        newton.sendall(text.encode() + b"\r")
        return read_until(answer)

    def frame():
        nonlocal pending
        read_until(b"CONNECT\r\n")
        data = bytearray()
        while True:
            while len(pending) < 2:
                pending += newton.recv(4096)
            if pending[0] == DLE:
                if pending[1] == ETX:
                    pending = pending[2:]
                    break
                data.append(pending[1])
                pending = pending[2:]
            else:
                data.append(pending[0])
                pending = pending[1:]
        read_until(b"OK\r\n")
        return bytes(data)

    def send_frames(*frames):
        command("AT+FTH=3", b"CONNECT\r\n")
        for data in frames:
            data = data[:-2]            # a DTE sends its frames without the FCS
            newton.sendall(dle_stuff(data))
            read_until(b"OK\r\n" if data[1] & 0x10 else b"CONNECT\r\n")

    def send_data(data):
        command("AT+FTM=96", b"CONNECT\r\n")
        newton.sendall(dle_stuff(data))
        read_until(b"OK\r\n")

    command("ATE0")
    command("AT+FCLASS=1")
    newton.sendall(b"ATDT5551234\r")
    names = [frame_name(frame())]
    newton.sendall(b"AT+FRH=3\r")
    names.append(frame_name(frame()))
    assert names == ["CSI", "DIS"], names
    send_frames(FaxCaller.control(0x43, t30_id("scripted"), False),
                FaxCaller.control(0x83, bytes([0x00, 0x06, 0x00])))
    send_data(bytes(1800))
    newton.sendall(b"AT+FRH=3\r")
    assert frame_name(frame()) == "CFR"
    page = t4.test_page()
    send_data(t4.encode_page(page, 2))
    send_frames(FaxCaller.control(0x2f))
    newton.sendall(b"AT+FRH=3\r")
    assert frame_name(frame()) == "MCF"
    send_frames(FaxCaller.control(0xfb))
    assert t4.read_pbm(out) == page
    newton.close()
    thread.join(timeout=5)
    print("fakemodem: fax answer self test passed")


def run_modem(host, port, args):
    dte = socket.create_connection((host, port))
    dte.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    log("on the serial port at %s:%d" % (host, port))
    if args.fax_call:
        fax = FaxCaller(args.fax_call, "fakemodem fax")
    elif args.fax_answer:
        fax = FaxAnswerer(args.fax_answer, "fakemodem fax")
    else:
        fax = None
    Modem(dte, args.numbers, args.incoming, args.speed, args.identity, fax).run()


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
    ap.add_argument("--fax-call", metavar="PAGE.pbm", help="a fax machine to call the Newton, sending this page (Class 1)")
    ap.add_argument("--fax-answer", metavar="OUT.pbm", help="a fax machine answering the Newton's call, writing the page it receives (Class 1)")
    ap.add_argument("--speed", type=int, default=19200, help="the speed CONNECT reports (19200)")
    ap.add_argument("--identity", default="fakemodem 1.0", help="what ATI0/I3/I4 answer (an unknown modem)")
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--self-test", action="store_true", help="--fax-answer against a scripted caller")
    group.add_argument("--spawn", nargs=argparse.REMAINDER, help="the newton to run, and its arguments")
    group.add_argument("--connect", type=address, help="host:port of a newton's serial port")
    args = ap.parse_args()
    args.numbers = {}
    for entry in args.number:
        number, _, target = entry.partition("=")
        args.numbers[number] = address(target)
    if args.self_test:
        self_test()
        return
    if args.spawn:
        sys.exit(run_spawned(args.spawn, args))
    run_modem(args.connect[0], args.connect[1], args)


if __name__ == "__main__":
    main()
