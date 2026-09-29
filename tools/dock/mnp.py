#!/usr/bin/env python3
"""MNP over TCP: the desktop end of a Newton serial link, for tests.

Purpose
    A MessagePad docks over its serial port with MNP (the Microcom Networking
    Protocol, classes 2-4): framed, sequenced, acknowledged data.  The host
    build of the reconstructed OS puts the Newton's external serial port on
    a TCP socket (hal/host/HostSerialChip.h, Einstein's port 3679), so a
    desktop program reaches it by connecting there and speaking MNP.  This
    module is that desktop end, written from the protocol (and checked
    against the ROM's own TMNP, src/comms/MNP.cpp): the framing (SYN DLE STX,
    DLE doubled, DLE ETX, CRC-16/ARC low byte first), the link request
    exchange as the acceptor (the Newton connects), LT data frames with
    their sequence numbers and LA acknowledgements, class 4's short
    headers, and LD.  With --v42bis it accepts V.42bis compression both
    ways when the Newton asks for it (tools/dock/v42bis.py, compressing in
    compressed mode from the start); otherwise it offers none, so none is
    negotiated.

Usage
    As a library:
        link = MNPLink.connect("127.0.0.1", 3679)   # retries until the port listens
        link.accept(class4)                               # the Newton's LR answered
        data = link.receive()                       # the next LT's data
        link.send(b"...")                           # LTs, each acknowledged
        link.close()
    As a test runner (ctest comms.MNP):
        python tools/dock/mnp.py [--echo] [--no-class4] [--v42bis] --spawn <program>
    runs <program>, which prints "port N" on its first line of output once
    its serial port listens; connects to it, accepts the link and, with
    --echo, sends back whatever it receives until the Newton disconnects;
    then answers <program>'s exit status.

Inputs / outputs
    The TCP port of a host Newton's serial port.  --spawn's program's exit
    status is the script's; its output is passed through.
"""

import argparse
import socket
import subprocess
import sys
import threading
import time

SYN, DLE, STX, ETX = 0x16, 0x10, 0x02, 0x03
LR, LD, LT, LA = 1, 2, 4, 5


def crc16(data, crc=0):
    """CRC-16/ARC (the Newton's TCRC16): reflected 0x8005, initial 0."""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def frame(body):
    """A frame on the wire: the header, the body with DLE doubled, the end,
    and the CRC of the body and ETX, low byte first."""
    out = bytearray([SYN, DLE, STX])
    for b in body:
        out.append(b)
        if b == DLE:
            out.append(DLE)
    out += bytes([DLE, ETX])
    c = crc16(bytes(body) + bytes([ETX]))
    out += bytes([c & 0xFF, c >> 8])
    return bytes(out)


class MNPError(Exception):
    pass


class MNPLink:
    def __init__(self, sock):
        self.sock = sock
        self.sock.settimeout(30)
        self.pending = bytearray()
        self.class4 = False
        self.window = 1
        self.max_data = 64
        self.send_seq = 0
        self.recv_seq = 0
        self.disconnected = False
        self.received_while_sending = []
        self.v42bis = None           # (N2, N7) once negotiated
        self.compressor = self.expander = None

    @classmethod
    def connect(cls, host, port, tries=500):
        for _ in range(tries):
            try:
                return cls(socket.create_connection((host, port)))
            except OSError:
                time.sleep(0.02)
        raise MNPError("cannot connect to %s:%d" % (host, port))

    # --- frames

    def _byte(self):
        while not self.pending:
            data = self.sock.recv(4096)
            if not data:
                raise EOFError("the Newton closed the link")
            self.pending += data
        b = self.pending[0]
        del self.pending[0]
        return b

    def read_frame(self):
        """The next frame's body (a frame with a bad CRC is skipped)."""
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

    def write_frame(self, body):
        self.sock.sendall(frame(body))

    # --- the link

    def accept(self, class4=True, v42bis=False):
        """The Newton's LR answered with ours: octet framing, its window
        and data size, class 4 if it asked for it (and class4 allows),
        V.42bis both ways if it asked for it and v42bis allows (a
        dictionary of at most 1024 entries and strings of at most 32
        characters: tools/dock/v42bis.py compresses what is sent and
        expands what comes); then its LA."""
        body = self.read_frame()
        if len(body) < 2 or body[1] != LR:
            raise MNPError("expected an LR, got %r" % body)
        params = {}
        i = 3
        while i + 1 < len(body):
            t, n = body[i], body[i + 1]
            params[t] = body[i + 2:i + 2 + n]
            i += 2 + n
        self.window = params[3][0] if 3 in params else 1
        if 4 in params:
            self.max_data = params[4][0] | (params[4][1] << 8)
        opt = params[8][0] if (8 in params and class4) else 0
        self.class4 = bool(opt & 2)
        reply = bytearray([0, LR, 2,
                           1, 6, 1, 0, 0, 0, 0, 0xFF,
                           2, 1, 2,
                           3, 1, self.window,
                           4, 2, self.max_data & 0xFF, self.max_data >> 8])
        if 8 in params and class4:
            reply += bytes([8, 1, opt & 3])
            if (opt & 1) and self.max_data == 64:
                self.max_data = 256
        if v42bis and 0x0e in params and len(params[0x0e]) >= 4 and params[0x0e][0]:
            import v42bis as v42
            n2 = min((params[0x0e][1] << 8) | params[0x0e][2], 1024)
            n7 = min(params[0x0e][3], 32)
            reply += bytes([0x0e, 4, 3, n2 >> 8, n2 & 0xff, n7])
            self.compressor = v42.Encoder(n2, n7)
            self.expander = v42.Decoder(n2, n7)
            self.v42bis = (n2, n7)
        reply[0] = len(reply) - 1
        self.write_frame(bytes(reply))
        # (an LR sent again - the Newton's timer ran out before our answer
        # came - is answered again)
        for _ in range(10):
            body = self.read_frame()
            if body[1] != LR:
                break
            self.write_frame(bytes(reply))
        if body[1] != LA:
            raise MNPError("expected an LA, got %r" % body)

    def _parse_la(self, body):
        if self.class4:
            return body[2], body[3]
        params = {}
        i = 2
        while i + 1 < len(body):
            params[body[i]] = body[i + 2:i + 2 + body[i + 1]]
            i += 2 + body[i + 1]
        return params[1][0], params[2][0]

    def _ack(self):
        if self.class4:
            self.write_frame(bytes([3, LA, self.recv_seq, 8]))
        else:
            self.write_frame(bytes([7, LA, 1, 1, self.recv_seq, 2, 1, 8]))

    def _handle(self, body):
        """A frame: an LT's data (acknowledged) or None."""
        kind = body[1]
        if kind == LT:
            if self.class4:
                seq, data = body[2], body[3:]
            else:
                seq, data = body[4], body[5:]
            if seq == (self.recv_seq + 1) & 0xFF:
                self.recv_seq = seq
                self._ack()
                return data
            self._ack()
            return None
        if kind == LD:
            self.disconnected = True
            raise EOFError("the Newton disconnected")
        return None

    def receive(self):
        """The next LT's data (expanded, with V.42bis: perhaps nothing, when
        the LT held only part of a codeword)."""
        data = self._receive()
        if self.expander is not None:
            data = self.expander.decode(data)
        return data

    def _receive(self):
        if self.received_while_sending:
            return self.received_while_sending.pop(0)
        while True:
            data = self._handle(self.read_frame())
            if data is not None:
                return data

    def send(self, data):
        """The data in LTs, each waited on until it is acknowledged (with
        V.42bis compressed first, and flushed)."""
        if self.compressor is not None:
            # (compressed mode from the start: this end does not weigh
            # which mode is better, as the ROM's encoder does)
            data = self.compressor.to_compressed() + self.compressor.encode(data) + self.compressor.flush()
        size = min(self.max_data, 256)
        for i in range(0, len(data), size):
            self.send_seq = (self.send_seq + 1) & 0xFF
            chunk = data[i:i + size]
            if self.class4:
                body = bytes([2, LT, self.send_seq]) + chunk
            else:
                body = bytes([4, LT, 1, 1, self.send_seq]) + chunk
            while True:
                self.write_frame(body)
                acked = False
                deadline = time.time() + 5
                while time.time() < deadline:
                    reply = self.read_frame()
                    if reply[1] == LA:
                        seq, _ = self._parse_la(reply)
                        if seq == self.send_seq:
                            acked = True
                            break
                    else:
                        extra = self._handle(reply)
                        if extra is not None:
                            self.received_while_sending.append(extra)
                if acked:
                    break

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def run_spawned(program, echo, class4=True, v42bis=False):
    proc = subprocess.Popen(program, stdout=subprocess.PIPE, text=True)
    port = None
    for line in proc.stdout:
        sys.stdout.write(line)
        if line.startswith("port "):
            port = int(line.split()[1])
            break
    if port is None:
        return proc.wait() or 1

    def passthrough():
        for line in proc.stdout:
            sys.stdout.write(line)
    t = threading.Thread(target=passthrough, daemon=True)
    t.start()

    link = MNPLink.connect("127.0.0.1", port)
    try:
        link.accept(class4, v42bis)
        print("mnp.py: link up (window %d, data %d, class 4 %s, V.42bis %s)"
              % (link.window, link.max_data, link.class4, link.v42bis))
        while echo:
            data = link.receive()
            if data:
                link.send(data)
    except (EOFError, socket.timeout, ConnectionError) as e:
        print("mnp.py: %s" % e)
    finally:
        link.close()
    if link.expander is not None:
        print("mnp.py: V.42bis: the Newton's data came %s" % (
            "in compressed mode at the end" if not link.expander.transparent else "in transparent mode at the end"))
    status = proc.wait(timeout=120)
    t.join(timeout=5)
    return status


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--spawn", nargs="+", required=True, help="the program to run (prints 'port N')")
    ap.add_argument("--echo", action="store_true", help="send back whatever is received")
    ap.add_argument("--no-class4", action="store_true", help="refuse class 4 (the long frame headers)")
    ap.add_argument("--v42bis", action="store_true", help="accept V.42bis compression (tools/dock/v42bis.py)")
    args = ap.parse_args()
    sys.exit(run_spawned(args.spawn, args.echo, not args.no_class4, args.v42bis))


if __name__ == "__main__":
    main()
