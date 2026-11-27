#!/usr/bin/env python3
"""A SIMCom A76xx-class cellular module for the host build, over TCP.

Purpose
    The host's cellular engine (src/hal/host/HostCellular.h) drives a SIMCom
    module in AT commands - the T-Deck Pro's 4G variant carries an A7682E
    on a UART.  A desktop newton has no module; given --cell-port it listens
    on a TCP port for one (src/hal/host/HostCellularSocket.cpp), and this is
    the module at the other end.  It speaks the part of the A76xx dialect
    the engine uses, as lilyphone1 (the owner's firmware for the board)
    found the real module speaks it:

    - configuration: ATE0, AT+CMEE, AT+CLIP, AT+CMGF, AT+CSCS ("GSM" or
      "UCS2" - under UCS2 every string parameter is hex), AT+CPMS,
      AT+CNMI, AT+CSDH, AT+CSMP (its fourth number the data coding scheme),
      AT+CTZU, AT+CTZR: each OK;
    - the network: AT+CPIN? (+CPIN: READY; --no-sim answers +CME ERROR: 10,
      --pin "+CPIN: SIM PIN"), AT+CSQ (+CSQ: 20,99), AT+CREG? (+CREG: 0,1;
      --roaming 0,5; 0,2 for the first --register-after seconds), AT+COPS?
      (+COPS: 0,0,"<--operator>",7), AT+CCLK? (now, in the zone --zone,
      quarter hours east of Greenwich);
    - calls: ATD<number>; is OK, then (a second later) "VOICE CALL: BEGIN" -
      or "BUSY" for a number given with --busy; ATA answers a ringing call
      ("VOICE CALL: BEGIN" after the OK); AT+CHUP hangs up ("VOICE CALL: END:
      0"); AT+CLCC lists the call (+CLCC: 1,<dir>,<stat>,0,0,"<number>",145);
    - messages: a message waiting is stored in a slot and told with +CMTI:
      "SM",<slot>; AT+CMGR=<slot> gives its header (with the stamp and the
      coding scheme: 8, the body in UCS2 hex, when it is not plain ASCII) and
      body; AT+CMGD deletes it; AT+CMGL="ALL" lists the slots; AT+CMGS="<n>"
      answers "> ", takes the body up to Ctrl-Z (UCS2 hex when the coding
      scheme is 8) and answers +CMGS: <n> and OK - "fakecell: text to N: T";
      to a number given with --refuse it answers +CMS ERROR: 500;
    - in a call: AT+VTS=<digit> (a keypad tone - "fakecell: tone 5 in the
      call with N"); AT+CSDVC=1 or 3 (the audio to the handset or the
      loudspeaker); AT+CMUT=1 or 0 ("fakecell: the microphone muted" or
      "on again");
    - anything else ERROR (AT+FCLASS=? among them: an LTE module carries no
      fax, the owner's to be checked on the real one).

    The scenario is the Newton's to drive, with commands of the fake's own
    sent through the same link (the natives' HostCellCommand):

        AT+FAKERING="<number>"      a call comes in: RING and +CLIP every
                                    3 s, six times, then it gives up
                                    (NO CARRIER); "" withholds the number
        AT+FAKETEXT="<number>","<text>"
                                    a message arrives (the text UTF-8; \\n a
                                    line break)
        AT+FAKEHANGUP               the far end hangs up the call

    --text NUMBER=TEXT stores a message before the Newton starts listening
    (picked up by AT+CMGL), as a module holding messages that came while it
    was off does.

Usage
    python tools/modem/fakecell.py [options] --spawn <program...>
        runs <program> (a newton given --cell-port 0), waits for its
        "[host] cell port N" line, connects there as the module; answers
        the program's exit status.  Its output is passed through.
    python tools/modem/fakecell.py [options] --connect 127.0.0.1:PORT
        the same with a newton already running.

Inputs / outputs
    Prints what the Newton says ("fakecell: <- AT+CSQ"), what the module
    answers ("fakecell: -> +CSQ: 20,99"), and each call and message
    ("fakecell: call to 5551234 connected", "fakecell: text to ...").
    ctest host.NewtonCellular (src/host/demo/cellular.ns).
"""

import argparse
import datetime
import re
import socket
import subprocess
import sys
import threading
import time


def log(text):
    print("fakecell: " + text)
    sys.stdout.flush()


def is_ascii(text):
    return all(ord(c) < 0x80 for c in text)


def ucs2_hex(text):
    return text.encode("utf-16-be").hex().upper()


def from_ucs2_hex(text):
    return bytes.fromhex(text).decode("utf-16-be")


class Module:
    def __init__(self, link, args):
        self.link = link
        self.args = args
        self.lock = threading.Lock()
        self.started = time.time()
        self.charset = "GSM"
        self.dcs = 0
        self.slots = {}                 # slot: (number, text, stamp)
        self.next_slot = 1
        self.call = None                # {"number", "dir", "stat"} - stat 0 active, 2 dialing, 4 incoming
        self.rings = 0
        self.next_ring = 0
        self.connect_at = 0
        self.sending = None             # the number of an AT+CMGS waiting for its body
        self.body = bytearray()
        self.sent = 0
        for entry in args.text:
            number, _, text = entry.partition("=")
            self.store(number, text.replace("\\n", "\n"), announce=False)

    # --- the link
    def send(self, text):
        self.link.sendall(text.encode("latin-1", "replace"))

    def say(self, line):
        log("-> " + line)
        self.send("\r\n" + line + "\r\n")

    def ok(self):
        self.say("OK")

    def param(self, text):
        """A string parameter as the Newton wrote it: hex under UCS2."""
        if self.charset == "UCS2":
            try:
                return from_ucs2_hex(text)
            except ValueError:
                return text
        return text

    def stamp(self, when=None):
        quarters = self.args.zone
        local = (when or datetime.datetime.now(datetime.timezone.utc)) + datetime.timedelta(minutes=15 * quarters)
        return local.strftime("%y/%m/%d,%H:%M:%S") + "%+03d" % quarters

    # --- messages
    def store(self, number, text, announce=True):
        slot = self.next_slot
        self.next_slot += 1
        self.slots[slot] = (number, text, self.stamp())
        if announce:
            self.say('+CMTI: "SM",%d' % slot)
        return slot

    def read_slot(self, slot):
        if slot not in self.slots:
            self.say("+CMS ERROR: 321")
            return
        number, text, stamp = self.slots[slot]
        dcs = 0 if is_ascii(text) else 8
        self.say('+CMGR: "REC UNREAD","%s",,"%s",145,4,0,%d,"+61418706700",145,%d'
                 % (number, stamp, dcs, len(text)))
        body = text if dcs == 0 else ucs2_hex(text)
        for line in body.split("\n"):
            self.say(line)
        self.ok()

    # --- calls
    def set_call(self, number, direction, stat):
        self.call = {"number": number, "dir": direction, "stat": stat}

    def end_call(self, words):
        if self.call is not None:
            log("call with %s ended" % (self.call["number"] or "a withheld number"))
        self.call = None
        self.rings = 0
        for word in words:
            self.say(word)

    def tick(self):
        now = time.time()
        if self.call and self.call["stat"] == 2 and self.connect_at and now >= self.connect_at:
            self.connect_at = 0
            if self.call["number"] in self.args.busy:
                log("call to %s: busy" % self.call["number"])
                self.end_call(["BUSY"])
            else:
                self.call["stat"] = 0
                log("call to %s connected" % self.call["number"])
                self.say("VOICE CALL: BEGIN")
        if self.call and self.call["stat"] == 4 and now >= self.next_ring:
            if self.rings == 6:
                log("call from %s not answered" % (self.call["number"] or "a withheld number"))
                self.end_call(["NO CARRIER"])
                return
            self.rings += 1
            self.next_ring = now + 3
            self.say("RING")
            if self.call["number"]:
                self.say('+CLIP: "%s",145,,,,0' % self.call["number"])

    # --- commands
    def command(self, text):
        log("<- " + text)
        upper = text.upper()
        if not upper.startswith("AT"):
            return
        body = text[2:]
        ubody = upper[2:]
        if ubody in ("", "E0", "E1", "+CMEE=1", "+CLIP=1", "+CMGF=1", "+CTZU=1", "+CTZR=1",
                     "+CNMI=2,1,0,0,0", "+CSDH=1", "+COPS=3,0") or ubody.startswith("+CPMS="):
            self.ok()
        elif ubody.startswith("+CSCS="):
            self.charset = body.split("=", 1)[1].strip('"').upper()
            self.ok()
        elif ubody.startswith("+CSMP="):
            fields = body.split("=", 1)[1].split(",")
            self.dcs = int(fields[3]) if len(fields) > 3 else 0
            self.ok()
        elif ubody == "+CPIN?":
            if self.args.no_sim:
                self.say("+CME ERROR: 10")
            else:
                self.say("+CPIN: SIM PIN" if self.args.pin else "+CPIN: READY")
                self.ok()
        elif ubody == "+CSQ":
            self.say("+CSQ: 20,99")
            self.ok()
        elif ubody == "+CREG?":
            registered = time.time() - self.started >= self.args.register_after
            self.say("+CREG: 0,%d" % ((5 if self.args.roaming else 1) if registered else 2))
            self.ok()
        elif ubody == "+COPS?":
            self.say('+COPS: 0,0,"%s",7' % self.args.operator)
            self.ok()
        elif ubody == "+CCLK?":
            self.say('+CCLK: "%s"' % self.stamp())
            self.ok()
        elif ubody.startswith("D") and ubody.endswith(";"):
            number = body[1:-1]
            if self.call is not None:
                self.say("ERROR")
                return
            self.set_call(number, 0, 2)
            self.connect_at = time.time() + 1
            log("calling %s" % number)
            self.ok()
        elif ubody == "A":
            if self.call is None or self.call["stat"] != 4:
                self.say("ERROR")
                return
            self.call["stat"] = 0
            log("call from %s answered" % (self.call["number"] or "a withheld number"))
            self.ok()
            self.say("VOICE CALL: BEGIN")
        elif ubody in ("+CHUP", "H", "H0"):
            had = self.call is not None
            self.ok()
            if had:
                self.end_call(["VOICE CALL: END: 0"])
        elif ubody == "+CLCC":
            if self.call is not None:
                self.say('+CLCC: 1,%d,%d,0,0,"%s",145' % (self.call["dir"], self.call["stat"], self.call["number"]))
            self.ok()
        elif ubody.startswith("+VTS="):
            if self.call is None or self.call["stat"] != 0:
                self.say("ERROR")
                return
            log("tone %s in the call with %s" % (body.split("=", 1)[1], self.call["number"]))
            self.ok()
        elif ubody.startswith("+CSDVC="):
            device = body.split("=", 1)[1]
            if device not in ("1", "3"):
                self.say("ERROR")
                return
            log("the audio to the %s" % ("loudspeaker" if device == "3" else "handset"))
            self.ok()
        elif ubody.startswith("+CMUT="):
            on = body.split("=", 1)[1]
            if on not in ("0", "1") or self.call is None:
                self.say("ERROR")
                return
            log("the microphone %s" % ("muted" if on == "1" else "on again"))
            self.ok()
        elif ubody.startswith("+CMGR="):
            self.read_slot(int(body.split("=", 1)[1]))
        elif ubody.startswith("+CMGD="):
            self.slots.pop(int(body.split("=", 1)[1]), None)
            self.ok()
        elif ubody.startswith("+CMGL"):
            for slot, (number, text, stamp) in sorted(self.slots.items()):
                self.say('+CMGL: %d,"REC UNREAD","%s",,"%s"' % (slot, number, stamp))
                self.say(text if is_ascii(text) else ucs2_hex(text))
            self.ok()
        elif ubody.startswith("+CMGS="):
            self.sending = self.param(body.split("=", 1)[1].strip('"'))
            self.body = bytearray()
            log("-> > ")
            self.send("\r\n> ")
        elif ubody.startswith("+FAKERING"):
            m = re.match(r'\+FAKERING="([^"]*)"', body, re.I)
            if self.call is not None or not m:
                self.say("ERROR")
                return
            self.ok()
            self.set_call(m.group(1), 1, 4)
            self.rings = 0
            self.next_ring = time.time() + 0.5
            log("a call from %s" % (m.group(1) or "a withheld number"))
        elif ubody.startswith("+FAKETEXT"):
            m = re.match(r'\+FAKETEXT="([^"]*)","(.*)"$', body, re.I)
            if not m:
                self.say("ERROR")
                return
            self.ok()
            text = m.group(2).replace("\\n", "\n")
            log("a text from %s: %s" % (m.group(1), text))
            self.store(m.group(1), text)
        elif ubody == "+FAKEHANGUP":
            self.ok()
            if self.call is not None:
                log("the far end hangs up")
                self.end_call(["VOICE CALL: END: 0", "NO CARRIER"])
        else:
            self.say("ERROR")

    def body_byte(self, byte):
        if byte == 0x1B:                # abandoned
            self.sending = None
            self.ok()
            return
        if byte != 0x1A:
            self.body.append(byte)
            return
        raw = self.body.decode("latin-1")
        text = from_ucs2_hex(raw) if self.dcs == 8 else raw
        if self.sending in self.args.refuse:
            log("text to %s refused: %s" % (self.sending, text))
            self.sending = None
            self.say("+CMS ERROR: 500")
            return
        self.sent += 1
        log("text to %s: %s" % (self.sending, text))
        self.sending = None
        self.say("+CMGS: %d" % self.sent)
        self.ok()

    def run(self):
        self.link.settimeout(0.1)
        line = bytearray()
        while True:
            self.tick()
            try:
                data = self.link.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            if not data:
                log("the Newton closed the link")
                return
            for byte in data:
                if self.sending is not None:
                    self.body_byte(byte)
                elif byte in (0x0D, 0x0A):
                    if line:
                        self.command(line.decode("utf-8", "replace"))
                        line = bytearray()
                else:
                    line.append(byte)


def run_module(host, port, args):
    link = socket.create_connection((host, port))
    link.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    log("connected to the Newton at %s:%d" % (host, port))
    Module(link, args).run()


def run_spawned(program, args):
    proc = subprocess.Popen(program, stdout=subprocess.PIPE, text=True, errors="replace")
    port = None
    for line in proc.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
        m = re.search(r"\[host\] cell port (\d+)", line)
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
    threading.Thread(target=run_module, args=("127.0.0.1", port, args), daemon=True).start()
    status = proc.wait(timeout=600)
    t.join(timeout=5)
    log("done (the program answered %s)" % status)
    return status


def address(text):
    host, _, port = text.rpartition(":")
    return host or "127.0.0.1", int(port)


def main():
    # (a message's emoji printed on a Windows console's code page would stop
    # the module)
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--operator", default="Fake Telecom", help="the network's name (AT+COPS?)")
    ap.add_argument("--zone", type=int, default=40, help="the network's zone in quarter hours east of Greenwich (+40: UTC+10)")
    ap.add_argument("--register-after", type=float, default=0, help="seconds before the module is registered")
    ap.add_argument("--roaming", action="store_true", help="registered roaming (+CREG: 0,5)")
    ap.add_argument("--no-sim", action="store_true", help="no SIM card (+CME ERROR: 10)")
    ap.add_argument("--pin", action="store_true", help="a SIM that wants its PIN")
    ap.add_argument("--busy", action="append", default=[], help="a number that answers busy")
    ap.add_argument("--refuse", action="append", default=[], help="a number the network will not send a text to (+CMS ERROR: 500)")
    ap.add_argument("--text", action="append", default=[], help="NUMBER=TEXT: a message stored before the start")
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--spawn", nargs=argparse.REMAINDER, help="the newton to run, and its arguments")
    group.add_argument("--connect", type=address, help="host:port of a newton's cell port")
    args = ap.parse_args()
    if args.spawn:
        sys.exit(run_spawned(args.spawn, args))
    run_module(args.connect[0], args.connect[1], args)


if __name__ == "__main__":
    main()
