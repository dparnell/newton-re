#!/usr/bin/env python3
"""A stand-in for AppLoad's qtfb server, to run newton's reMarkable window
(src/host/remarkable/) without a tablet.

AppLoad, on a reMarkable Paper Pro, lends a program it starts a framebuffer:
a SOCK_SEQPACKET Unix socket the program connects to, a POSIX shared-memory
object it draws into (RGB565 here) and messages saying which rectangle to
put on the glass and how; the pen, the touches and the keys come back over
the socket (src/host/remarkable/QTFB.h has the messages).  This program is
the server's side of that, on any Linux: it listens, runs the program given
after `--` with QTFB_KEY and NEWTON_QTFB_SOCKET set (under qemu-user for an
aarch64 newton built on another machine - docs/host-remarkable.md), counts
the updates by refresh mode, and carries out a list of steps against it:

    quiet:S        wait until no update has come for S seconds (at most --timeout)
    sleep:S        wait S seconds
    snap:FILE      write the framebuffer as a grayscale PNG (tools/imaging/png.py)
    tap:X,Y        the pen pressed and lifted at X,Y (framebuffer pixels)
    stroke:X1,Y1,X2,Y2   the pen pressed, drawn across in steps and lifted
    key:CODE       a key pressed and released (Qt's key code, decimal or 0x)
    gray:X,Y,W,H,MIN     check that the area has at least MIN pixels darker
                   than mid-gray (prints 'gray ... ok' or 'gray ... FAILED')

Then it stops the program and prints what it saw, ending
'qtfbserver: done' when every step ran.  Linux only (shm_open's objects are
files in /dev/shm).

    python3 tools/remarkable/qtfbserver.py --socket /tmp/newton-qtfb.sock \\
        --step quiet:5 --step snap:setup.png -- \\
        ./qemu-aarch64-static -L sysroot ./newton --display 320x480 --erase --store s.store
"""
import argparse
import os
import socket
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "imaging"))
import png  # noqa: E402

CLIENT_SIZE = 24        # QTFB.h's QTFBClientMessage on aarch64/x86-64
SERVER_SIZE = 32        # QTFBServerMessage
INITIALIZE, UPDATE, CUSTOM_INITIALIZE, TERMINATE, USER_INPUT, SET_REFRESH_MODE, REQUEST_FULL_REFRESH = 0, 1, 2, 3, 4, 5, 6
FORMATS = {0: (1404, 1872, 2), 1: (1620, 2160, 3), 2: (1620, 2160, 4), 3: (1620, 2160, 2),
           4: (954, 1696, 3), 5: (954, 1696, 4), 6: (954, 1696, 2)}
MODES = {0: "ufast", 1: "fast", 2: "animate", 3: "content", 4: "ui"}
PEN_PRESS, PEN_RELEASE, PEN_UPDATE = 0x20, 0x21, 0x22
BUTTON_PRESS, BUTTON_RELEASE = 0x30, 0x31


class Server:
    def __init__(self, path):
        self.path = path
        self.client = None
        self.width = self.height = self.bpp = 0
        self.shm = None
        self.shm_name = None
        self.mode = 4
        self.updates = {}
        self.full_refreshes = 0
        self.last_update = time.time()
        self.lock = threading.Lock()
        self.closed = False
        self.ready = threading.Event()

    def listen(self):
        if os.path.exists(self.path):
            os.unlink(self.path)
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.listener.bind(self.path)
        self.listener.listen(1)
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        # one client at a time; one that goes without asking for a
        # framebuffer (a program testing that the socket is there) is
        # simply followed by the next
        while not self.closed:
            client, _ = self.listener.accept()
            self.client = client
            while True:
                try:
                    data = client.recv(64)
                except OSError:
                    break
                if not data:
                    break
                self.message(data.ljust(CLIENT_SIZE, b"\0"))
            if self.ready.is_set():
                self.closed = True

    def message(self, data):
        kind = data[0]
        if kind in (INITIALIZE, CUSTOM_INITIALIZE):
            key, fmt = struct.unpack_from("<iB", data, 4)
            width, height, bpp = FORMATS.get(fmt, (0, 0, 2))
            if kind == CUSTOM_INITIALIZE:
                width, height = struct.unpack_from("<HH", data, 10)
            size = width * height * bpp
            shm_key = 4242
            self.shm_name = "/dev/shm/qtfb_%d" % shm_key
            with open(self.shm_name, "wb") as f:
                f.truncate(size)
            self.width, self.height, self.bpp = width, height, bpp
            print("qtfbserver: framebuffer %d x %d, %d bytes a pixel (key %d, format %d%s)"
                  % (width, height, bpp, key, fmt, ", custom" if kind == CUSTOM_INITIALIZE else ""), flush=True)
            self.client.send(struct.pack("<B7xi4xQ8x", INITIALIZE, shm_key, size))
            self.ready.set()
        elif kind == UPDATE:
            typ, x, y, w, h = struct.unpack_from("<5i", data, 4)
            with self.lock:
                name = MODES.get(self.mode, str(self.mode)) if typ == 1 else "all"
                self.updates[name] = self.updates.get(name, 0) + 1
                self.last_update = time.time()
        elif kind == SET_REFRESH_MODE:
            self.mode = struct.unpack_from("<i", data, 4)[0]
        elif kind == REQUEST_FULL_REFRESH:
            with self.lock:
                self.full_refreshes += 1
                self.last_update = time.time()
        elif kind == TERMINATE:
            print("qtfbserver: the client said it is going", flush=True)

    def send_input(self, kind, x, y, d=0):
        self.client.send(struct.pack("<B7x5i4x", USER_INPUT, kind, 0, x, y, d))

    def pixels(self):
        with open(self.shm_name, "rb") as f:
            return f.read()

    def gray_rows(self):
        data = self.pixels()
        rows = []
        for y in range(self.height):
            row = []
            for x in range(self.width):
                if self.bpp == 2:
                    v = struct.unpack_from("<H", data, (y * self.width + x) * 2)[0]
                    level = ((v >> 5) & 0x3f) << 2
                else:
                    o = (y * self.width + x) * self.bpp
                    level = data[o + 1]
                row.append(level)
            rows.append(row)
        return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--socket", default="/tmp/newton-qtfb.sock")
    parser.add_argument("--key", default="1234")
    parser.add_argument("--timeout", type=float, default=600, help="the longest a quiet: step waits")
    parser.add_argument("--step", action="append", default=[])
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("no program to run after --")

    server = Server(args.socket)
    server.listen()
    env = dict(os.environ, QTFB_KEY=args.key, NEWTON_QTFB_SOCKET=args.socket)
    child = subprocess.Popen(command, env=env)
    failed = 0
    try:
        if not server.ready.wait(args.timeout):
            print("qtfbserver: the program never asked for a framebuffer", flush=True)
            return 1
        for step in args.step:
            what, _, arg = step.partition(":")
            started = time.time()
            if what == "quiet":
                seconds = float(arg)
                while time.time() - server.last_update < seconds and time.time() - started < args.timeout:
                    if child.poll() is not None:
                        break
                    time.sleep(0.1)
                print("qtfbserver: quiet after %.1f s" % (time.time() - started), flush=True)
            elif what == "sleep":
                time.sleep(float(arg))
            elif what == "snap":
                png.write_gray(arg, server.width, server.height, server.gray_rows(), 8)
                print("qtfbserver: wrote %s" % arg, flush=True)
            elif what == "tap":
                x, y = (int(v) for v in arg.split(","))
                server.send_input(PEN_PRESS, x, y, 50)
                time.sleep(0.15)
                server.send_input(PEN_RELEASE, x, y, 0)
                print("qtfbserver: tapped %d,%d" % (x, y), flush=True)
            elif what == "stroke":
                x1, y1, x2, y2 = (int(v) for v in arg.split(","))
                server.send_input(PEN_PRESS, x1, y1, 50)
                for i in range(1, 21):
                    time.sleep(0.02)
                    server.send_input(PEN_UPDATE, x1 + (x2 - x1) * i // 20, y1 + (y2 - y1) * i // 20, 50)
                server.send_input(PEN_RELEASE, x2, y2, 0)
                print("qtfbserver: stroke %d,%d to %d,%d" % (x1, y1, x2, y2), flush=True)
            elif what == "key":
                code = int(arg, 0)
                server.send_input(BUTTON_PRESS, code, 0)
                time.sleep(0.05)
                server.send_input(BUTTON_RELEASE, code, 0)
                print("qtfbserver: key 0x%x" % code, flush=True)
            elif what == "gray":
                x, y, w, h, least = (int(v) for v in arg.split(","))
                rows = server.gray_rows()
                dark = sum(1 for r in rows[y:y + h] for v in r[x:x + w] if v < 128)
                ok = dark >= least
                failed += not ok
                print("qtfbserver: gray %s: %d dark pixels %s" % (arg, dark, "ok" if ok else "FAILED"), flush=True)
            else:
                print("qtfbserver: no such step %r" % step, flush=True)
                return 2
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(10)
            except subprocess.TimeoutExpired:
                child.kill()
        with server.lock:
            print("qtfbserver: updates by mode: %s; full refreshes %d"
                  % (", ".join("%s %d" % kv for kv in sorted(server.updates.items())) or "none", server.full_refreshes),
                  flush=True)
        if server.shm_name and os.path.exists(server.shm_name):
            os.unlink(server.shm_name)
        if os.path.exists(args.socket):
            os.unlink(args.socket)
    print("qtfbserver: done%s" % (", %d FAILED" % failed if failed else ""), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
