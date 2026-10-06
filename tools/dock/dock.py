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
    python tools/dock/dock.py --session --package file.pkg --spawn <program...>
        the same inside a docking session: 'dock', the Newton's 'name',
        'dinf'/'ninf', the timeout, and the password exchange (the empty
        password; newtondes.py is the Newton's DES) before the 'lpkg's.
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
import newtondes  # noqa: E402
import nsof  # noqa: E402
from nsof import Symbol  # noqa: E402


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
        """The next command: (its four characters, its data).  A 'bids'
        (a backup's ids: its length 0xffffffff) is read as the halfwords up
        to its 0x8000; a 'base' carries its id where the length goes."""
        newt, dock, cmd, length = struct.unpack(">4s4s4sI", self._read(16))
        if newt != b"newt" or dock != b"dock":
            raise RuntimeError("not a dock header: %r %r" % (newt, dock))
        if cmd == b"bids":
            data = bytearray()
            while True:
                half = self._read(2)
                data += half
                if half == b"\x80\x00":
                    break
            print("dock.py: <- bids (%d bytes)" % len(data))
            return cmd, bytes(data)
        if cmd == b"base":
            print("dock.py: <- base %d" % length)
            return cmd, struct.pack(">I", length)
        data = self._read(length)
        if length & 3:
            self._read(4 - (length & 3))
        print("dock.py: <- %s (%d bytes)" % (cmd.decode("latin-1"), length))
        return cmd, data

    def write_command(self, cmd, data=b""):
        pad = b"\0" * ((4 - len(data) % 4) % 4)
        self.link.send(b"newtdock" + cmd + struct.pack(">I", len(data)) + data + pad)
        print("dock.py: -> %s (%d bytes)" % (cmd.decode("latin-1"), len(data)))

    def expect(self, what):
        cmd, data = self.read_command()
        if cmd != what:
            raise RuntimeError("expected %s, got %r" % (what.decode("latin-1"), cmd))
        return data

    def docking_session(self, password=""):
        """A docking session begun, the Newton's 'rtdk' already read: the
        session asked for ('dock', load packages), the Newton's name read,
        the desktop's info given ('dinf': protocol 11, our challenge) and
        the Newton's read ('ninf': its challenge), the timeout set, then
        the passwords - the Newton's 'pass' (our challenge under its key)
        checked against the key of `password`, and ours sent.  The
        Newton then says 'dres' 0: the session is under way."""
        self.write_command(b"dock", struct.pack(">I", SESSION_LOAD_PACKAGE))
        name = self.expect(b"name")
        info_length = struct.unpack(">I", name[:4])[0]
        words = struct.unpack(">%dI" % (info_length // 4), name[4:4 + info_length])
        owner = name[4 + info_length:].decode("utf-16-be", "replace").split("\0")[0]
        print("dock.py: the Newton's name: id %08x, machine %08x, protocol %d, owner %r"
              % (words[0], words[2], words[17], owner))
        challenge = (0x12345678, 0x9abcdef0)
        self.write_command(b"dinf", struct.pack(">6I", 11, DESKTOP_WINDOWS, challenge[0], challenge[1],
                                                SESSION_LOAD_PACKAGE, 0))
        ninf = self.expect(b"ninf")
        version, n0, n1 = struct.unpack(">3I", ninf[:12])
        print("dock.py: the Newton speaks protocol %d" % version)
        self.write_command(b"stim", struct.pack(">I", 60))
        key = newtondes.char_to_key(password)
        theirs = struct.unpack(">2I", self.expect(b"pass")[:8])
        if newtondes.decode_nonce(key, theirs) != challenge:
            raise RuntimeError("the Newton's password is not ours")
        print("dock.py: the Newton knows the password")
        self.write_command(b"pass", struct.pack(">2I", *newtondes.encode_nonce(key, (n0, n1))))
        result = struct.unpack(">i", self.expect(b"dres")[:4])[0]
        if result != 0:
            raise RuntimeError("the Newton refused the session: %d" % result)
        print("dock.py: docked")

    def look_at_stores(self):
        """In a session: the stores ('gsto'), the default one ('gdfs'),
        the first made current with its soups ('ssgn'), the System soup
        made current ('ssou') and its info read ('gsin')."""
        self.write_command(b"gsto")
        stores = nsof.decode(self.expect(b"stor"))
        print("dock.py: stores: %s" % ", ".join("%s (%s)" % (st[Symbol("name")], st[Symbol("kind")]) for st in stores))
        self.write_command(b"gdfs")
        default = nsof.decode(self.expect(b"dfst"))
        print("dock.py: the default store is %s" % default[Symbol("name")])
        first = stores[0]
        self.write_command(b"ssgn", nsof.encode({Symbol("name"): first[Symbol("name")],
                                                 Symbol("kind"): first[Symbol("kind")],
                                                 Symbol("signature"): first[Symbol("signature")]}))
        names, signatures = nsof.decode(self.expect(b"soup"), many=True)
        print("dock.py: %d soups on %s, the System soup %s" % (len(names), first[Symbol("name")],
                                                              "among them" if "System" in names else "missing"))
        self.write_command(b"ssou", "System\0".encode("utf-16-be"))
        result = struct.unpack(">i", self.expect(b"dres")[:4])[0]
        print("dock.py: the System soup made current: %d" % result)
        self.write_command(b"gsin")
        info = nsof.decode(self.expect(b"sinf"))
        print("dock.py: the System soup's info: %s" % (type(info).__name__))
        print("dock.py: the soups: %s" % ", ".join(names))

    def word(self, cmd, data=b""):
        """A command answered by one word ('ldta', 'adid', 'dres'...)."""
        self.write_command(cmd, data)
        reply, answer = self.read_command()
        return reply, struct.unpack(">i", answer[:4])[0]

    def look_at_entries(self, soup="Notes"):
        """In a session: a cursor over a soup ('qury'), counted, walked and
        freed; its ids ('gids'); an entry added ('adde'), read back
        ('rete'), changed ('cent') and deleted ('dele').  The soup is left
        as it was.  ==> True if it all came out as it should."""
        ok = True
        reply, cursor = self.word(b"qury", nsof.encode({Symbol("querySpec"): None, Symbol("soupName"): soup}))
        _, count = self.word(b"cnt ", struct.pack(">I", cursor))
        print("dock.py: %s: cursor %d, %d entries" % (soup, cursor, count))
        added = self.word(b"adde", nsof.encode({Symbol("class"): Symbol("paragraph"),
                                                Symbol("text"): "written by dock.py",
                                                Symbol("viewBounds"): {Symbol("left"): 0, Symbol("top"): 0,
                                                                       Symbol("right"): 100, Symbol("bottom"): 20}}))[1]
        print("dock.py: added entry %d" % added)
        _, count2 = self.word(b"cnt ", struct.pack(">I", cursor))
        ok = ok and count2 == count + 1
        self.write_command(b"gids")
        data = self.expect(b"sids")
        ids = struct.unpack(">%dI" % struct.unpack(">I", data[:4])[0], data[4:])
        ok = ok and added in ids and len(ids) == count2
        print("dock.py: %d ids, the new one %s" % (len(ids), "among them" if added in ids else "missing"))
        self.write_command(b"rete", struct.pack(">I", added))
        entry = nsof.decode(self.expect(b"entr"))
        print("dock.py: entry %d reads %r" % (added, entry.get(Symbol("text"))))
        ok = ok and entry.get(Symbol("text")) == "written by dock.py"
        entry[Symbol("text")] = "changed by dock.py"
        _, result = self.word(b"cent", nsof.encode(entry))
        self.write_command(b"rete", struct.pack(">I", added))
        entry = nsof.decode(self.expect(b"entr"))
        print("dock.py: after 'cent' it reads %r" % entry.get(Symbol("text")))
        ok = ok and result == 0 and entry.get(Symbol("text")) == "changed by dock.py"
        self.write_command(b"rset", struct.pack(">I", cursor))
        self.expect(b"dres")
        self.write_command(b"crsr", struct.pack(">I", cursor))
        first = nsof.decode(self.expect(b"entr"))
        _, end = self.word(b"whch", struct.pack(">I", cursor))
        _, result = self.word(b"dele", struct.pack(">II", 1, added))
        _, count3 = self.word(b"cnt ", struct.pack(">I", cursor))
        _, freed = self.word(b"cfre", struct.pack(">I", cursor))
        ok = ok and result == 0 and count3 == count and freed == 0 and isinstance(first, dict)
        # an entry from a 1.x Newton ('sver' 1): converted by Notes' own
        # conversion (or taken as it is), then the version set back
        _, result = self.word(b"sver", struct.pack(">I", 1))
        converted = self.word(b"adde", nsof.encode({Symbol("class"): Symbol("paragraph"), Symbol("text"): "from a 1.x Newton"}))[1]
        _, result2 = self.word(b"sver", struct.pack(">III", 2, 0x01000000, 0x10003000))
        print("dock.py: a 1.x entry added as %d" % converted)
        if converted >= 0:
            self.word(b"dele", struct.pack(">II", 1, converted))
        ok = ok and result == 0 and result2 == 0
        print("dock.py: deleted (%d), %d entries again, cursor at end %d, freed; entries %s"
              % (result, count3, end, "all right" if ok else "WRONG"))
        return ok

    def backup_a_soup(self, soup="dock.py test"):
        """In a session: a soup made ('csop'), three entries added, and it
        backed up ('bksp': their ids, the desktop's time being later than
        they were made) and sent ('snds': the entries), then removed
        ('dsou').  ==> True if it all came out as it should."""
        ok = True
        reply, data = self.word(b"stme", struct.pack(">I", 0x1ffffff0))
        name = (soup + "\0").encode("utf-16-be")
        self.write_command(b"csop", struct.pack(">I", len(name)) + name + b"\0" * ((4 - len(name) % 4) % 4)
                           + nsof.encode([]))
        # (the length word, the name padded, then the indexes: the command's
        # length covers them all)
        result = struct.unpack(">i", self.expect(b"dres")[:4])[0]
        print("dock.py: soup %r made: %d" % (soup, result))
        ids = [self.word(b"adde", nsof.encode({Symbol("n"): i}))[1] for i in range(3)]
        print("dock.py: entries %s added" % ids)
        self.write_command(b"bksp")
        cmd, data = self.read_command()
        runs = []
        while cmd != b"bsdn":
            if cmd == b"bids":
                runs.append(list(struct.unpack(">%dh" % (len(data) // 2), data)))
            cmd, data = self.read_command()
        expected = [[ids[0], -2, -0x8000]] if ids == list(range(ids[0], ids[0] + 3)) else None
        print("dock.py: the backup sent ids %s" % runs)
        ok = ok and runs == expected
        self.write_command(b"snds")
        sent = []
        cmd, data = self.read_command()
        while cmd != b"bsdn":
            sent.append(nsof.decode(data).get(Symbol("n")))
            cmd, data = self.read_command()
        print("dock.py: the soup sent entries %s" % sent)
        ok = ok and sent == [0, 1, 2]
        self.write_command(b"dsou")
        result = struct.unpack(">i", self.expect(b"dres")[:4])[0]
        print("dock.py: soup removed: %d; backup %s" % (result, "all right" if ok and result == 0 else "WRONG"))
        return ok and result == 0

    def other_requests(self):
        """In a session: 'test' and 'rtst' echoed, the class inheritance
        ('ginh'), the sync options ('gsyn'), a global function called
        ('cgfn'), and a protocol extension installed and removed ('pext',
        'rpex').  ==> True if it all came out as it should."""
        ok = True
        self.write_command(b"test", b"echo")
        echoed = self.expect(b"test")
        ok = ok and echoed == b"echo"
        # (the ROM writes the 'rtst' header twice: a stray one first - a
        # ROM bug the reconstruction fixes unless NEWTON_ROM_BUGS=1, so the
        # stray header is taken when it is there: a header followed by
        # "newt" rather than a length)
        self.write_command(b"rtst", nsof.encode({Symbol("x"): 1}))
        head = self._read(12)
        while len(self.buffer) < 4:
            self.buffer += self.link.receive()
        stray = None
        if bytes(self.buffer[:4]) == b"newt":
            stray = head
        else:
            self.buffer[0:0] = head
        back = nsof.decode(self.expect(b"rtst"))
        print("dock.py: 'test' echoed %r; 'rtst' echoed %r after a stray %r" % (echoed, back, stray))
        ok = ok and stray in (None, b"newtdockrtst") and back == {Symbol("x"): 1}
        self.write_command(b"ginh")
        data = self.expect(b"dinh")
        count = struct.unpack(">I", data[:4])[0]
        names = data[4:].split(b"\0")
        pairs = dict(zip(names[0:2 * count:2], names[1:2 * count:2]))
        print("dock.py: %d classes in the inheritance: %s" % (count, ", ".join(
            "%s < %s" % (k.decode("mac_roman"), v.decode("mac_roman")) for k, v in list(pairs.items())[:4])))
        ok = ok and count > 0 and len(pairs) == count
        self.write_command(b"gsyn")
        options = nsof.decode(self.expect(b"sopt"))
        print("dock.py: the sync options: %r" % (options,))
        self.write_command(b"cgfn", nsof.encode({Symbol("function"): Symbol("Max"), Symbol("args"): [3, 7]}))
        result = nsof.decode(self.expect(b"cres"))
        print("dock.py: Max(3, 7) called on the Newton: %r" % (result,))
        ok = ok and result == 7
        # the system patches (none: 'patc' of nought bytes)
        self.write_command(b"gpat")
        patches = self.expect(b"patc")
        print("dock.py: the system patches: %d bytes" % len(patches))
        ok = ok and len(patches) == 0
        # a slip shown by the Connection application; dock.ns taps its first button
        self.write_command(b"dslp", nsof.encode({Symbol("title"): "Dock test", Symbol("message"): "Tap the first button",
                                                 Symbol("button1"): "First", Symbol("button2"): "Second"}))
        slip = struct.unpack(">i", self.expect(b"slrs")[:4])[0]
        print("dock.py: the slip was answered with button %d" % slip)
        _, installed = self.word(b"pext", struct.pack(">I", 0x7a7a7a7a) + nsof.encode(None))
        _, removed = self.word(b"rpex", struct.pack(">I", 0x7a7a7a7a))
        print("dock.py: extension 'zzzz' installed %d, removed %d; requests %s"
              % (installed, removed, "all right" if ok and installed == 0 and removed == 0 else "WRONG"))
        return ok and installed == 0 and removed == 0

    def load_packages(self, packages, session=False):
        """The package loader's session, or (session) a docking session
        that loads the packages.  ==> the results, one a package."""
        data = self.expect(b"rtdk")
        print("dock.py: the Newton's protocol version is %d" % struct.unpack(">I", data[:4]))
        if session:
            self.docking_session()
            self.look_at_stores()
            if not self.look_at_entries():
                raise RuntimeError("the entry commands went wrong")
            if not self.backup_a_soup():
                raise RuntimeError("the backup commands went wrong")
            if not self.other_requests():
                raise RuntimeError("the other requests went wrong")
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
        if session:
            # the packages on the current store ('gpin' with no name: all)
            self.write_command(b"gpin", nsof.encode(None))
            info = nsof.decode(self.expect(b"pinf"))
            print("dock.py: packages on the store: %s" % ", ".join(
                "%s (%d bytes, id %d)" % (p[Symbol("name")], p[Symbol("packageSize")], p[Symbol("packageId")]) for p in info))
            # each removed ('rmvp') and loaded again
            for p in info:
                _, result = self.word(b"rmvp", nsof.encode(p[Symbol("name")]))
                self.write_command(b"gpin", nsof.encode(p[Symbol("name")]))
                left = nsof.decode(self.expect(b"pinf"))
                print("dock.py: %s removed (%d), %d left of the name" % (p[Symbol("name")], result, len(left)))
            for path in packages:
                with open(path, "rb") as f:
                    self.write_command(b"lpkg", f.read())
                result = struct.unpack(">i", self.expect(b"dres")[:4])[0]
                print("dock.py: %s loaded again: dres %d" % (os.path.basename(path), result))
        if session:
            # the keyboard passthrough: dock.ns's DockTestKeyboard puts the
            # docker in its keyboard state; a string, a character, and done
            self.write_command(b"cgfn", nsof.encode({Symbol("function"): Symbol("DockTestKeyboard"), Symbol("args"): []}))
            print("dock.py: the keyboard passthrough started: %r" % (nsof.decode(self.expect(b"cres")),))
            self.write_command(b"kbds", "hello".encode("utf-16-be") + b"\0\0")
            self.write_command(b"kbdc", struct.pack(">HH", ord("!"), 0))
            self.write_command(b"opdn")
            # (the Connection application ends the session when the
            # keyboard is done: the Newton says 'disc')
            self.expect(b"disc")
            print("dock.py: the Newton disconnected after the keyboard")
        else:
            self.write_command(b"disc")
        return results


SESSION_LOAD_PACKAGE = 4        # the kinds of session: 1 none, 2 sync, 3 restore, 4 load packages
DESKTOP_WINDOWS = 1             # the desktop's platform (0 Macintosh, 1 Windows)


def dock(host, port, packages, session=False):
    link = MNPLink.connect(host, port)
    link.sock.settimeout(120)
    try:
        link.accept()
        print("dock.py: link up")
        results = DockSession(link).load_packages(packages, session)
        # the Newton closes the link once it has read the 'disc'
        try:
            while True:
                link.receive()
        except (EOFError, ConnectionError, socket.timeout):
            pass
    finally:
        link.close()
    return results


def run_spawned(program, packages, session=False):
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
        results = dock("127.0.0.1", port, packages, session)
        ok = all(r == 0 for r in results)
    except (EOFError, ConnectionError, socket.timeout, RuntimeError) as e:
        print("dock.py: %s" % e)
        ok = False
    status = proc.wait(timeout=300)
    t.join(timeout=5)
    print("dock.py: done (the program answered %s)" % status)
    return status or (0 if ok else 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--package", action="append", required=True, help="a package to load (in order)")
    ap.add_argument("--session", action="store_true",
                    help="dock (the handshake and the password exchange) and load the packages in the session, "
                         "rather than answering 'rtdk' with 'lpkg'")
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--spawn", nargs=argparse.REMAINDER, help="the newton to run, and its arguments (the rest of the line; it prints '[host] serial port N')")
    group.add_argument("--connect", help="host:port of a newton already running")
    args = ap.parse_args()
    if args.spawn:
        sys.exit(run_spawned(args.spawn, args.package, args.session))
    host, _, port = args.connect.rpartition(":")
    results = dock(host or "127.0.0.1", int(port), args.package, args.session)
    sys.exit(0 if all(r == 0 for r in results) else 1)


if __name__ == "__main__":
    main()
