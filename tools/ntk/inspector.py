#!/usr/bin/env python3
"""A Newton Toolkit inspector peer: the desktop end of the NTK's connection.

Purpose
    The Newton Toolkit (NTK) debugs a MessagePad through its inspector: a
    script on the Newton calls ntkListener, which makes the ROM's debugger
    nub (src/comms/NTK.h) over a serial (MNP) connection, and the desktop
    then sends it code blocks to run and packages to install, and reads
    back what the Newton prints, the objects NTKSend sends and the
    exceptions it reports.  ntkDownload asks the desktop for one package.
    This is that desktop end, for the reconstruction's host build, whose
    external serial port is a TCP socket (hal/host/HostSerialChip.h,
    Einstein's port 3679, so a tool written for Einstein's inspector can
    connect the same way).  It is written from the ROM's nub
    (TNTKNub, 0x0012aa84-0x0012c5a0) and uses tools/dock/mnp.py for MNP and
    tools/dock/nsof.py for the objects.

    The protocol: every message is 'newt' 'ntp ' and a command (big-endian
    words).  From the Newton: 'cnnt' (length 0; answered 'okln'), 'rslt'
    (length 4, an error), 'text' (what the REP prints), 'code' (a code
    block's result: the length the desktop's 'code' had - a ROM quirk -
    then the object's size and its NSOF), 'fobj'/'fstk' (NTKSend's object
    and a stack trace: no length, the object's size and its NSOF),
    'eerr'/'estr'/'eref' (an exception: no length; the whole length, the
    name's length and the name, then the error, the message's length and
    the message, or the object's size and its NSOF), 'eext'/'bext' (the
    break loop entered and left), 'dpkg' (ntkDownload asking for a
    package), 'teom', 'term'.  From the desktop: 'okln', 'code' (an
    object's size and a code block's NSOF: run, 'rslt' 0 and then 'code'
    with its result sent back), 'lscb' (a code block's NSOF for the REP,
    whose output comes back as 'text'), 'pkg ' (a package), 'pkgX' (a
    package deleted by its name, UTF-16), 'stou' (the timeout in seconds),
    'term'.

    Expressions are evaluated without a desktop compiler: the code block
    sent is hand-assembled bytecode that calls the Newton's own Compile on
    the source and runs what it answers (push the source, push 'Compile,
    call 1, invoke 0, return).

Usage
    As a library:
        link = MNPLink.connect("127.0.0.1", 3679); link.accept()
        ntk = NTKInspector(link)
        ntk.handshake()                         # 'cnnt' answered 'okln'
        value = ntk.evaluate("1 + 2")           # 3, with 'rslt' checked
        ntk.listener_line('Print("hi")')        # the REP's text: ntk.texts
        ntk.serve_package(open("x.pkg", "rb").read())   # ntkDownload's
    As a test runner (ctest host.NewtonNTK):
        python tools/ntk/inspector.py [--timeout S] [--install file.pkg]...
            [--delete NAME]... [--eval EXPR]... [--line EXPR]...
            [--finish EXPR] [--package file.pkg] --spawn <program...>
    runs <program> (a newton, which prints "[host] serial port N" and runs a
    script calling ntkListener(true, 3, nil, nil, nil)), connects, answers
    'cnnt', sets the timeout ('stou'), installs each --install ('pkg ') and
    deletes each --delete ('pkgX'), evaluates each --eval ('code') and sends each --line ('lscb'),
    printing what comes back, then evaluates --finish (which should tell
    the script to stop the listener), waits for the link to end and, with
    --package, answers the next link's 'dpkg' (the script's ntkDownload)
    with the package.  Interactively, against a newton already running
    and listening:
        python tools/ntk/inspector.py --connect 127.0.0.1:3679
    reads expressions from stdin and prints their values.

Inputs / outputs
    The TCP port of a host Newton's serial port; --package's file.  Lines
    beginning "inspector.py:" say what happened; --spawn's program's output
    is passed through and its exit status is the script's.
"""

import argparse
import os
import re
import socket
import struct
import subprocess
import sys
import threading

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "dock"))
from mnp import MNPLink  # noqa: E402
import nsof  # noqa: E402
from nsof import Symbol, Binary  # noqa: E402

HEADER = b"newtntp "


class NTKError(Exception):
    pass


def compile_and_run_block(source):
    """A code block that compiles the source on the Newton and runs it:
    push literal 0 (the source), push literal 1 ('Compile), call 1,
    invoke 0, return."""
    return {
        Symbol("class"): Symbol("CodeBlock"),
        Symbol("instructions"): Binary(Symbol("instructions"), bytes([0x18, 0x19, 0x29, 0x30, 0x02])),
        Symbol("literals"): [source, Symbol("Compile")],
        Symbol("argFrame"): {Symbol("_nextArgFrame"): None, Symbol("_parent"): None, Symbol("_implementor"): None},
        Symbol("numArgs"): 0,
    }


class NTKInspector:
    def __init__(self, link):
        self.link = link
        self.buffer = bytearray()
        self.texts = []           # what the REP printed ('text')
        self.objects = []         # NTKSend's objects ('fobj')
        self.traces = []          # stack traces ('fstk')
        self.exceptions = []      # (command, name, data)

    # --- bytes

    def read(self, n):
        while len(self.buffer) < n:
            self.buffer += self.link.receive()
        data = bytes(self.buffer[:n])
        del self.buffer[:n]
        return data

    def word(self):
        return struct.unpack(">I", self.read(4))[0]

    def signed(self):
        return struct.unpack(">i", self.read(4))[0]

    def send(self, command, data=b"", length=None):
        if length is None:
            length = len(data)
        self.link.send(HEADER + command + struct.pack(">I", length) + data)

    # --- the Newton's messages

    def message(self):
        """The next message from the Newton, as (command, value)."""
        header = self.read(8)
        if header != HEADER:
            raise NTKError("not a message: %r" % header)
        command = self.read(4)
        if command in (b"fobj", b"fstk"):
            value = nsof.decode(self.read(self.word()))
            (self.objects if command == b"fobj" else self.traces).append(value)
            return command, value
        if command in (b"eerr", b"estr", b"eref"):
            self.word()                                   # the whole length
            name = self.read(self.word()).rstrip(b"\0").decode("mac_roman")
            if command == b"eerr":
                data = self.signed()
            elif command == b"estr":
                data = self.read(self.word()).rstrip(b"\0").decode("mac_roman")
            else:
                data = nsof.decode(self.read(self.word()))
            self.exceptions.append((command, name, data))
            return command, (name, data)
        length = self.word()
        if command == b"rslt":
            return command, self.signed()
        if command == b"text":
            text = self.read(length).decode("mac_roman")
            self.texts.append(text)
            return command, text
        if command == b"code":
            return command, nsof.decode(self.read(self.word()))
        if length:
            return command, self.read(length)
        return command, None

    def expect(self, wanted):
        """Messages read until one of the command wanted (the others kept
        where they belong); its value."""
        while True:
            command, value = self.message()
            print("inspector.py: <- %s %r" % (command.decode("mac_roman"), value))
            if command == b"rslt" and value != 0 and wanted != b"rslt":
                raise NTKError("the Newton answered error %d" % value)
            if command == wanted:
                return value
            if command == b"term":
                raise EOFError("the Newton ended the connection")

    # --- the desktop's commands

    def handshake(self):
        self.expect(b"cnnt")
        self.send(b"okln")

    def evaluate(self, source):
        """The source compiled and run on the Newton ('code'); its value."""
        block = nsof.encode(compile_and_run_block(source))
        data = struct.pack(">I", len(block)) + block
        self.send(b"code", data)
        result = self.expect(b"rslt")
        if result != 0:
            raise NTKError("the code block was refused: %d" % result)
        return self.expect(b"code")

    def listener_line(self, source):
        """The source run by the REP, as a line typed in NTK's listener
        ('lscb'): its output comes back as 'text'."""
        self.send(b"lscb", nsof.encode(compile_and_run_block(source)))
        return self.expect(b"rslt")

    def set_timeout(self, seconds):
        self.send(b"stou", struct.pack(">I", seconds))

    def install(self, package):
        """A package installed on the default store ('pkg '); the result."""
        self.send(b"pkg ", package)
        return self.expect(b"rslt")

    def delete_package(self, name):
        """The package of that name removed ('pkgX'); the result."""
        self.send(b"pkgX", (name + "\0").encode("utf-16-be"))
        return self.expect(b"rslt")

    def serve_package(self, package, timeout=None):
        """ntkDownload's 'dpkg' answered with the package; the result."""
        self.expect(b"dpkg")
        if timeout is not None:
            self.set_timeout(timeout)
        self.send(b"pkg ", package)
        return self.expect(b"rslt")


def next_link(link):
    """The next MNP link, for the Newton's next endpoint: the host's serial
    port drops its connection when the endpoint that had it closes, so the
    wire is connected again."""
    host, port = link.sock.getpeername()[:2]
    link.close()
    fresh = MNPLink.connect(host, port)
    fresh.sock.settimeout(90)
    fresh.accept()
    return fresh


def wait_for_end(ntk):
    """Messages read until 'term' or the link goes down."""
    try:
        while True:
            command, value = ntk.message()
            print("inspector.py: <- %s %r" % (command.decode("mac_roman"), value))
            if command == b"term":
                print("inspector.py: the Newton said 'term'")
                break
    except EOFError as e:
        print("inspector.py: %s" % e)


def run_session(link, args):
    ntk = NTKInspector(link)
    ntk.handshake()
    print("inspector.py: connected ('cnnt' answered 'okln')")
    if args.timeout is not None:
        ntk.set_timeout(args.timeout)
        print("inspector.py: timeout set: rslt %d" % ntk.expect(b"rslt"))
    for path in args.install:
        with open(path, "rb") as f:
            print("inspector.py: %s installed: rslt %d" % (os.path.basename(path), ntk.install(f.read())))
    for name in args.delete:
        print("inspector.py: %s deleted: rslt %d" % (name, ntk.delete_package(name)))
    evals, lines, finish, package = args.eval, args.line, args.finish, args.package
    for source in evals:
        value = ntk.evaluate(source)
        print("inspector.py: %s => %r" % (source, value))
    for source in lines:
        ntk.listener_line(source)
        print("inspector.py: sent the line %s" % source)
    # what the lines printed, their exceptions: read until the Newton is
    # quiet, which the next evaluation (a round trip) makes sure of
    ntk.evaluate("nil")
    print("inspector.py: the REP printed %r" % ("".join(ntk.texts),))
    for command, name, data in ntk.exceptions:
        print("inspector.py: exception %s %s %r" % (command.decode(), name, data))
    for obj in ntk.objects:
        print("inspector.py: NTKSend sent %r" % (obj,))
    if finish:
        # `finish` is what tells the script to stop listening, so the link
        # may go down before the expression's own result comes back - the
        # script closes the listener as soon as it sees what finish set,
        # which it may do between the assignment and the reply.  Either way
        # the session is over and the download below goes on the next link.
        try:
            ntk.evaluate(finish)
            wait_for_end(ntk)
        except EOFError as e:
            print("inspector.py: %s" % e)
    if package is not None:
        link = next_link(link)
        ntk = NTKInspector(link)
        with open(package, "rb") as f:
            result = ntk.serve_package(f.read(), timeout=60)
        print("inspector.py: %s: rslt %d" % (os.path.basename(package), result))
        wait_for_end(ntk)
    return link


def run_spawned(program, args):
    proc = subprocess.Popen(program, stdout=subprocess.PIPE, text=True, errors="replace")
    port = None
    for line in proc.stdout:
        sys.stdout.write(line)
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

    link = MNPLink.connect("127.0.0.1", port)
    link.sock.settimeout(90)
    try:
        link.accept()
        link = run_session(link, args)
        print("inspector.py: done")
    except (EOFError, socket.timeout, ConnectionError, NTKError) as e:
        print("inspector.py: %s" % e)
    finally:
        link.close()
    status = proc.wait(timeout=180)
    t.join(timeout=5)
    print("inspector.py: the newton's exit status %d" % status)
    return status


def interactive(host, port):
    link = MNPLink.connect(host, port)
    link.accept()
    ntk = NTKInspector(link)
    ntk.handshake()
    print("inspector.py: connected; type NewtonScript, one expression a line")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            print(repr(ntk.evaluate(line)))
        except NTKError as e:
            print("inspector.py: %s" % e)
        for text in ntk.texts:
            sys.stdout.write(text.replace("\r", "\n"))
        ntk.texts.clear()
    ntk.send(b"term")
    link.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--eval", action="append", default=[], help="an expression to evaluate ('code')")
    ap.add_argument("--line", action="append", default=[], help="a line for the REP ('lscb')")
    ap.add_argument("--finish", help="an expression evaluated last, to tell the script to stop listening")
    ap.add_argument("--timeout", type=int, help="the timeout to set first, in seconds ('stou')")
    ap.add_argument("--install", action="append", default=[], help="a package to install ('pkg ') first")
    ap.add_argument("--delete", action="append", default=[], help="a package to delete by name ('pkgX') after the installs")
    ap.add_argument("--package", help="the package to answer ntkDownload's 'dpkg' with, on the next link")
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--spawn", nargs=argparse.REMAINDER, help="the newton to run, and its arguments (the rest of the line)")
    group.add_argument("--connect", help="host:port of a newton already listening")
    args = ap.parse_args()
    if args.spawn:
        sys.exit(run_spawned(args.spawn, args))
    host, _, port = args.connect.rpartition(":")
    interactive(host, int(port))


if __name__ == "__main__":
    main()
