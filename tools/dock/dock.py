#!/usr/bin/env python3
"""The desktop end of the Newton's 'dock' protocol, over MNP over TCP.

Purpose
    A MessagePad talks to a desktop with the 'dock' protocol: commands of
    a 16-byte header - the words 'newt', 'dock', the command and the length
    of its data - then the data, padded to a multiple of four, every word
    big-endian.  The Newton opens with 'rtdk' (ready to dock, its protocol
    version), and the desktop answers with the session it wants.  This is
    the desktop end of the simplest session there is, the old package
    downloader's: the answer is 'lpkg' with a package as its data; the
    Newton installs it and says 'dres' with the result, and the desktop
    then either sends another 'lpkg' or says 'disc'.  It runs over mnp.py's
    MNP link, to a host build of the reconstructed OS whose external serial
    port is a TCP socket (hal/host/HostSerialChip.h; the docker is
    src/comms/Docker.cpp).

    It is the test-side client of ctest host.NewtonDock: newton runs
    src/host/demo/dock.ns, which starts the Connection application's
    autodock (a serial connection), and waits for the package to arrive.

Usage
    python tools/dock/dock.py --package file.pkg [--package ...] --spawn <program...>
        runs <program> (a newton), waits for its "[host] serial port N"
        line, connects there and loads the packages; then answers the
        program's exit status (or 1 if a package was refused).
    python tools/dock/dock.py --package file.pkg --connect 127.0.0.1:3679
        the same against a newton already running (start the Connection
        application's connection on it after this is waiting).

Inputs / outputs
    The packages' files; prints each command exchanged and the result of
    each package ("dock.py: <file>: dres 0").  --spawn's output is passed
    through.
"""

import argparse
import os
import re
import socket
import struct
import subprocess
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mnp import MNPLink  # noqa: E402


class DockSession:
    def __init__(self, link):
        self.link = link
        self.buffer = bytearray()

    def _read(self, n):
        while len(self.buffer) < n:
            self.buffer += self.link.receive()
        data = bytes(self.buffer[:n])
        del self.buffer[:n]
        return data

    def read_command(self):
        """The next command: (its four characters, its data)."""
        newt, dock, cmd, length = struct.unpack(">4s4s4sI", self._read(16))
        if newt != b"newt" or dock != b"dock":
            raise RuntimeError("not a dock header: %r %r" % (newt, dock))
        data = self._read(length)
        if length & 3:
            self._read(4 - (length & 3))
        print("dock.py: <- %s (%d bytes)" % (cmd.decode("latin-1"), length))
        return cmd, data

    def write_command(self, cmd, data=b""):
        pad = b"\0" * ((4 - len(data) % 4) % 4)
        self.link.send(b"newtdock" + cmd + struct.pack(">I", len(data)) + data + pad)
        print("dock.py: -> %s (%d bytes)" % (cmd.decode("latin-1"), len(data)))

    def load_packages(self, packages):
        """The package loader's session.  ==> the results, one a package."""
        cmd, data = self.read_command()
        if cmd != b"rtdk":
            raise RuntimeError("expected rtdk, got %r" % cmd)
        print("dock.py: the Newton's protocol version is %d" % struct.unpack(">I", data[:4]))
        results = []
        for path in packages:
            with open(path, "rb") as f:
                self.write_command(b"lpkg", f.read())
            cmd, data = self.read_command()
            if cmd != b"dres":
                raise RuntimeError("expected dres, got %r" % cmd)
            result = struct.unpack(">i", data[:4])[0]
            print("dock.py: %s: dres %d" % (os.path.basename(path), result))
            results.append(result)
        self.write_command(b"disc")
        return results


def dock(host, port, packages):
    link = MNPLink.connect(host, port)
    link.sock.settimeout(120)
    try:
        link.accept()
        print("dock.py: link up")
        results = DockSession(link).load_packages(packages)
        # the Newton closes the link once it has read the 'disc'
        try:
            while True:
                link.receive()
        except (EOFError, ConnectionError, socket.timeout):
            pass
    finally:
        link.close()
    return results


def run_spawned(program, packages):
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
    ok = True
    try:
        results = dock("127.0.0.1", port, packages)
        ok = all(r == 0 for r in results)
    except (EOFError, ConnectionError, socket.timeout, RuntimeError) as e:
        print("dock.py: %s" % e)
        ok = False
    status = proc.wait(timeout=300)
    t.join(timeout=5)
    print("dock.py: done")
    return status or (0 if ok else 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--package", action="append", required=True, help="a package to load (in order)")
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--spawn", nargs=argparse.REMAINDER, help="the newton to run, and its arguments (the rest of the line; it prints '[host] serial port N')")
    group.add_argument("--connect", help="host:port of a newton already running")
    args = ap.parse_args()
    if args.spawn:
        sys.exit(run_spawned(args.spawn, args.package))
    host, _, port = args.connect.rpartition(":")
    results = dock(host or "127.0.0.1", int(port), args.package)
    sys.exit(0 if all(r == 0 for r in results) else 1)


if __name__ == "__main__":
    main()
