#!/usr/bin/env python3
"""A printer on the network for a host Newton to print to, by IPP.

Purpose
    The host's IPP printers (src/print/host/HostIPP.h: "IPP printer
    (PostScript)" and "IPP printer (HP PCL)" in the Print slip) send a job
    as an IPP Print-Job request (RFC 8011, encoded as RFC 8010 has it) in an
    HTTP POST.  This is a printer that takes one: it answers Print-Job
    (and Validate-Job and Get-Printer-Attributes, so a real IPP client can
    talk to it too), saves each document it is sent, and says what it got.
    With a program after `--` it runs the program with
    NEWTON_IPP_PRINTER=ipp://127.0.0.1:PORT/ipp/print in its environment -
    which newton reads like --ipp-printer - and stops when the program ends
    (ctests host.NewtonIPPPostScript and host.NewtonIPPPCL).

Usage
    python tools/print/ippprinter.py [--port PORT] [--out DIR] [--path /ipp/print]
                                     [--status N] [-- <program> [args...]]

    --port   the port to listen on, on 127.0.0.1 (default 0: a free one,
             so two runs at once never meet; 631 is IPP's own)
    --out    where the documents go (default: the working directory), as
             job-N.ps (application/postscript), job-N.pcl
             (application/vnd.hp-pcl) or job-N.bin, N counting from 1
    --path   the printer's path in its URI (default /ipp/print)
    --status the IPP status-code to answer a Print-Job with (default 0,
             successful-ok; e.g. 0x040a document-format-not-supported, to
             see a refusal)

Inputs / outputs
    Each request is logged as `[ipp] ...` lines: the operation and its
    attributes, the document saved and its size, and the answer.  With a
    program, its output (stdout and stderr merged) is passed through, and
    the exit status is the program's (1 when the port cannot be had).
    Without one, it serves until interrupted.  tools/print/jobcheck.py
    checks the documents saved.
"""

import argparse
import os
import socket
import struct
import subprocess
import sys
import threading

OPERATIONS = {0x0002: "Print-Job", 0x0004: "Validate-Job", 0x000b: "Get-Printer-Attributes"}
FORMATS = {"application/postscript": "ps", "application/vnd.hp-pcl": "pcl"}


def log(text):
    print("[ipp] " + text, flush=True)


class Printer:
    def __init__(self, out_dir, path, status):
        self.out_dir = out_dir
        self.path = path
        self.status = status
        self.jobs = 0
        self.lock = threading.Lock()
        self.port = 0


def read_request(conn):
    """One HTTP request: its request line, headers (lower-cased) and body."""
    data = b""
    while b"\r\n\r\n" not in data:
        more = conn.recv(65536)
        if not more:
            return None
        data += more
    head, _, rest = data.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    request_line = lines[0]
    headers = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()

    def more_bytes():
        chunk = conn.recv(65536)
        if not chunk:
            raise EOFError
        return chunk

    if "chunked" in headers.get("transfer-encoding", "").lower():
        body = bytearray()
        buf = rest
        while True:
            while b"\r\n" not in buf:
                buf += more_bytes()
            size_line, _, buf = buf.partition(b"\r\n")
            size = int(size_line.split(b";")[0].strip() or b"0", 16)
            if size == 0:
                while not (buf.startswith(b"\r\n") or b"\r\n\r\n" in buf):
                    buf += more_bytes()
                break
            while len(buf) < size + 2:
                buf += more_bytes()
            body += buf[:size]
            buf = buf[size + 2:]
        return request_line, headers, bytes(body), True
    length = int(headers.get("content-length", "0"))
    body = rest
    while len(body) < length:
        body += more_bytes()
    return request_line, headers, body[:length], False


def parse_ipp(body):
    """The IPP request: version, operation, request id, attributes (name ->
    list of (tag, value)) and the data after the end tag."""
    version = (body[0], body[1])
    operation, request_id = struct.unpack(">HI", body[2:8])
    attributes = {}
    i = 8
    name = None
    while i < len(body):
        tag = body[i]
        i += 1
        if tag == 0x03:
            break
        if tag < 0x10:
            continue
        name_length = struct.unpack(">H", body[i:i + 2])[0]
        i += 2
        if name_length:
            name = body[i:i + name_length].decode("utf-8", "replace")
        i += name_length
        value_length = struct.unpack(">H", body[i:i + 2])[0]
        i += 2
        value = body[i:i + value_length]
        i += value_length
        if tag in (0x21, 0x23) and value_length == 4:
            value = struct.unpack(">i", value)[0]
        else:
            value = value.decode("utf-8", "replace")
        attributes.setdefault(name, []).append((tag, value))
    return version, operation, request_id, attributes, body[i:]


def attribute(tag, name, value):
    if isinstance(value, int):
        data = struct.pack(">i", value)
    else:
        data = value.encode("utf-8")
    return bytes([tag]) + struct.pack(">H", len(name)) + name.encode() + struct.pack(">H", len(data)) + data


def answer(version, status, request_id, groups):
    out = bytes(version) + struct.pack(">HI", status, request_id)
    for group_tag, attrs in groups:
        out += bytes([group_tag])
        for tag, name, value in attrs:
            out += attribute(tag, name, value)
    return out + b"\x03"


def serve_connection(printer, conn):
    try:
        request = read_request(conn)
        if request is None:
            return
        request_line, headers, body, chunked = request
        method, path = request_line.split(" ")[:2]
        if method != "POST" or headers.get("content-type", "").split(";")[0] != "application/ipp":
            conn.sendall(b"HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
            log("%s %s refused" % (method, path))
            return
        version, operation, request_id, attrs, document = parse_ipp(body)
        name = OPERATIONS.get(operation, "operation 0x%04x" % operation)
        log("%s %s (IPP %d.%d, request %d, %s, %d bytes)" % (name, path, version[0], version[1], request_id,
                                                             "chunked" if chunked else "content-length", len(body)))
        for attr_name, values in attrs.items():
            log("  %s = %s" % (attr_name, ", ".join(str(v) for _, v in values)))
        base = [(0x47, "attributes-charset", "utf-8"), (0x48, "attributes-natural-language", "en")]
        printer_uri = "ipp://127.0.0.1:%d%s" % (printer.port, printer.path)
        if path != printer.path:
            reply = answer(version, 0x0406, request_id, [(0x01, base)])		# client-error-not-found
            log("  no printer at %s" % path)
        elif operation == 0x0002:
            fmt = attrs.get("document-format", [(0, "application/octet-stream")])[0][1]
            with printer.lock:
                printer.jobs += 1
                job = printer.jobs
            if printer.status == 0:
                file_name = os.path.join(printer.out_dir, "job-%d.%s" % (job, FORMATS.get(fmt, "bin")))
                with open(file_name, "wb") as f:
                    f.write(document)
                log("  job %d: %d bytes of %s saved as %s" % (job, len(document), fmt, file_name.replace(os.sep, "/")))
            job_attrs = [(0x45, "job-uri", "%s/%d" % (printer_uri, job)), (0x21, "job-id", job),
                         (0x23, "job-state", 9 if printer.status == 0 else 8),
                         (0x44, "job-state-reasons", "job-completed-successfully" if printer.status == 0 else "job-aborted-by-system")]
            reply = answer(version, printer.status, request_id, [(0x01, base), (0x02, job_attrs)])
            log("  answered 0x%04x" % printer.status)
        elif operation == 0x0004:
            reply = answer(version, 0, request_id, [(0x01, base)])
        elif operation == 0x000b:
            printer_attrs = [(0x45, "printer-uri-supported", printer_uri), (0x42, "printer-name", "ippprinter.py"),
                             (0x23, "printer-state", 3), (0x22, "printer-is-accepting-jobs", 1),
                             (0x49, "document-format-supported", "application/postscript"),
                             (0x49, "", "application/vnd.hp-pcl"), (0x49, "", "application/octet-stream")]
            out = bytes(version) + struct.pack(">HI", 0, request_id) + b"\x01"
            for tag, n, v in base:
                out += attribute(tag, n, v)
            out += b"\x04"
            for tag, n, v in printer_attrs:
                if tag == 0x22:
                    out += bytes([tag]) + struct.pack(">H", len(n)) + n.encode() + struct.pack(">H", 1) + bytes([v])
                else:
                    out += attribute(tag, n, v)
            reply = out + b"\x03"
        else:
            reply = answer(version, 0x0501, request_id, [(0x01, base)])	# server-error-operation-not-supported
            log("  not supported")
        conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
                     % len(reply) + reply)
    except (EOFError, ConnectionError, ValueError, struct.error, IndexError) as e:
        log("a bad request: %s" % e)
    finally:
        conn.close()


def serve(printer, listener):
    while True:
        try:
            conn, _ = listener.accept()
        except OSError:
            return
        threading.Thread(target=serve_connection, args=(printer, conn), daemon=True).start()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--out", default=".")
    ap.add_argument("--path", default="/ipp/print")
    ap.add_argument("--status", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--advertise", metavar="NAME",
                    help="the program finds this printer on the network (NEWTON_FOUND_PRINTERS)")
    ap.add_argument("--formats", default="ps,pcl", help="what --advertise says it takes (ps, pcl)")
    ap.add_argument("program", nargs=argparse.REMAINDER)
    args = ap.parse_args(argv)
    program = args.program
    if program and program[0] == "--":
        program = program[1:]
    os.makedirs(args.out, exist_ok=True)
    printer = Printer(args.out, args.path, args.status)
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        listener.bind(("127.0.0.1", args.port))
        listener.listen(8)
    except OSError as e:
        log("cannot listen on port %d: %s" % (args.port, e))
        return 1
    printer.port = listener.getsockname()[1]
    uri = "ipp://127.0.0.1:%d%s" % (printer.port, printer.path)
    log("a printer at %s" % uri)
    thread = threading.Thread(target=serve, args=(printer, listener), daemon=True)
    thread.start()
    if not program:
        try:
            thread.join()
        except KeyboardInterrupt:
            pass
        return 0
    env = dict(os.environ, NEWTON_IPP_PRINTER=uri)
    if args.advertise:
        # (the host's DNS-SD layer takes this list in place of a browse:
        # src/print/host/dnssd/HostDNSSD.h)
        env["NEWTON_FOUND_PRINTERS"] = "%s|%s|%s" % (args.advertise, uri, args.formats)
        del env["NEWTON_IPP_PRINTER"]
        log("advertised as %r (%s)" % (args.advertise, args.formats))
    if os.path.exists(program[0]):
        program[0] = os.path.abspath(program[0])		# (Windows will not run a relative path with slashes)
    proc = subprocess.Popen(program, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    for raw in proc.stdout:
        sys.stdout.buffer.write(raw)
        sys.stdout.buffer.flush()
    status = proc.wait()
    listener.close()
    log("%d job(s); the program answered %d" % (printer.jobs, status))
    return status


if __name__ == "__main__":
    sys.exit(main())
