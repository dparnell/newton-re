#!/usr/bin/env python3
"""A device on the Newton's serial port that hands the Newton its package.

    What the docking loader (src/comms/SCPLoader.h, the ROM's TSCPLoader)
    talks to when something is plugged into the serial port: a device that
    says who it is and, when the Newton has no service for it, sends the
    package that drives it.  On the host the serial port is a TCP socket
    (hal/host/HostSerialChip.h); this is the device at the other end.

    The link is framed as the ROM's framed serial tool ('fser) frames it:
    SYN DLE STX, the body with DLE doubled, DLE ETX, CRC-16/ARC low byte
    first (mnp.py's framing).  Each message is one frame: a run of tuples,
    each a four-character tag, a big-endian length and that many bytes,
    ended by an 'nofm' tuple (src/comms/CPMessages.h).  The exchange:

        device -> 'd_id' (type, manufacturer, version)
        Newton -> 'n_id' (machine type, manufacturer, ROM version),
                  'sire' ('pack', 0, 0), 'csre' (the speeds offered)
        device -> 'csrp' (the speed chosen), 'sirp' ('pack', version, size)
        Newton -> 'rese' ('pack', version)
        device -> 'pack' (size) followed by the package, then 'nofm'
        Newton -> 'abrt' (1: done)

    The device says who it is once, as soon as it has connected; the
    Newton reads it when it next opens the port (a script's
    HostInterconnect(1), src/host/demo/scpload.ns).  A device the Newton
    has a service for gets no answer, and nor does one the Newton did not
    ask about.

Usage:
    python tools/dock/scpdevice.py --package PKG [--type tdev] [--manufacturer appl]
                                   [--version 1   ] [--speed 0x08] --spawn <program...>
        runs <program> (a newton), waits for its "[host] serial port N"
        line and connects there; answers the program's exit status.
    python tools/dock/scpdevice.py --package PKG ... --connect 127.0.0.1:3679
        connects to a newton already running.

    It prints what it hears and sends ("scpdevice: ..."), and
    "scpdevice: package sent" once the Newton has said it is done.
"""

import argparse
import os
import re
import socket
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mnp import frame, crc16, SYN, DLE, STX, ETX      # the framing


def log(text):
    print("scpdevice: " + text, flush=True)


def fourcc(text):
    return struct.unpack(">I", text.encode("latin-1").ljust(4)[:4])[0]


def name(word):
    return struct.pack(">I", word).decode("latin-1")


def tuple_(tag, data=b""):
    return struct.pack(">4sI", tag.encode("latin-1"), len(data)) + data


def words(*values):
    return b"".join(struct.pack(">I", v & 0xFFFFFFFF) for v in values)


def message(*tuples):
    return b"".join(tuples) + tuple_("nofm")


def parse(body):
    """A message's tuples as (tag, data) pairs, up to its 'nofm'."""
    out = []
    at = 0
    while at + 8 <= len(body):
        tag, length = struct.unpack(">4sI", body[at:at + 8])
        tag = tag.decode("latin-1")
        data = body[at + 8:at + 8 + length]
        at += 8 + length
        if tag == "nofm":
            break
        out.append((tag, data))
    return out


class Link:
    def __init__(self, sock):
        self.sock = sock
        self.sock.settimeout(120)
        self.pending = bytearray()

    def _byte(self):
        while not self.pending:
            data = self.sock.recv(4096)
            if not data:
                raise EOFError("the Newton closed the port")
            self.pending += data
        b = self.pending[0]
        del self.pending[0]
        return b

    def read_frame(self):
        while True:
            state = 0
            while state < 3:
                b = self._byte()
                if state == 0:
                    state = 1 if b == SYN else 0
                elif state == 1:
                    state = 2 if b == DLE else (1 if b == SYN else 0)
                else:
                    state = 3 if b == STX else 0
            body = bytearray()
            while True:
                b = self._byte()
                if b == DLE:
                    b2 = self._byte()
                    if b2 == ETX:
                        break
                    body.append(b2)
                else:
                    body.append(b)
            lo, hi = self._byte(), self._byte()
            if crc16(bytes(body) + bytes([ETX])) == lo | (hi << 8):
                return bytes(body)
            log("a frame with a bad CRC skipped")

    def send(self, body):
        self.sock.sendall(frame(body))


def serve(host, port, args):
    with open(args.package, "rb") as f:
        package = f.read()
    for _ in range(500):
        try:
            sock = socket.create_connection((host, port))
            break
        except OSError:
            time.sleep(0.02)
    else:
        log("cannot connect to %s:%d" % (host, port))
        return 1
    link = Link(sock)
    log("connected to %s:%d" % (host, port))
    device = (fourcc(args.type), fourcc(args.manufacturer), fourcc(args.version))
    link.send(message(tuple_("d_id", words(*device))))
    log("said d_id %s %s %s" % (args.type, args.manufacturer, args.version))
    try:
        request = parse(link.read_frame())
        log("heard " + " ".join(tag for tag, _ in request))
        tags = dict(request)
        if "n_id" in tags:
            machine, maker, rom = struct.unpack(">III", tags["n_id"][:12])
            log("the Newton is machine %08x by %08x, ROM %08x" % (machine, maker, rom))
        speeds = struct.unpack(">I", tags["csre"][:4])[0] if "csre" in tags else 0
        answer = []
        if speeds & args.speed:
            answer.append(tuple_("csrp", words(args.speed)))
        answer.append(tuple_("sirp", words(fourcc("pack"), 0, len(package))))
        link.send(message(*answer))
        log("answered csrp %#x, sirp pack %d bytes" % (args.speed if speeds & args.speed else 0, len(package)))
        request = parse(link.read_frame())
        log("heard " + " ".join(tag for tag, _ in request))
        if "rese" not in dict(request):
            log("no package asked for")
            return 1
        link.send(tuple_("pack")[:4] + struct.pack(">I", len(package)) + package + tuple_("nofm"))
        log("sent the package")
        request = parse(link.read_frame())
        tags = dict(request)
        log("heard " + " ".join(tag for tag, _ in request))
        if "abrt" in tags:
            log("the Newton said abrt %d" % struct.unpack(">I", tags["abrt"][:4])[0])
        log("package sent")
    except (EOFError, OSError) as e:
        log(str(e))
        return 1
    finally:
        sock.close()
    return 0


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
    d = threading.Thread(target=serve, args=("127.0.0.1", port, args), daemon=True)
    d.start()
    status = proc.wait(timeout=600)
    t.join(timeout=5)
    log("done (the program answered %s)" % status)
    return status


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--package", required=True, help="the package the device sends")
    ap.add_argument("--type", default="tdev", help="the device's type (four characters)")
    ap.add_argument("--manufacturer", default="appl", help="its manufacturer (four characters)")
    ap.add_argument("--version", default="1   ", help="its version (four characters)")
    ap.add_argument("--speed", type=lambda s: int(s, 0), default=0x08,
                    help="the speed chosen: 4 19200, 8 38400, 0x10 57600, 0x20 115200, 0x40 230400")
    ap.add_argument("--connect", help="HOST:PORT of a newton's serial port")
    ap.add_argument("--spawn", nargs=argparse.REMAINDER, help="a newton to run")
    args = ap.parse_args()
    if args.spawn:
        return run_spawned(args.spawn, args)
    if args.connect:
        host, _, port = args.connect.rpartition(":")
        return serve(host or "127.0.0.1", int(port), args)
    ap.error("--spawn or --connect")


if __name__ == "__main__":
    sys.exit(main())
