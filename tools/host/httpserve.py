#!/usr/bin/env python3
"""A web server on the host for a program that browses, run around it.

Purpose
    A host Newton browsing the web (NetHopper over the Newton Internet
    Enabler and the host's own TCP/IP stack) needs a web server to ask.
    This serves a directory over HTTP on 127.0.0.1 (Python's own
    http.server, HTTP/1.0 as the Newton's browsers speak it), runs the
    program given after `--`, and stops serving when the program ends.
    Each request is printed as `[http] GET /path 200`, interleaved with the
    program's own output, so a ctest's regular expression can see both
    (ctest host.NewtonNetHopper: src/host/demo/nethopper.ns browsing
    src/host/demo/www/).

Usage
    python tools/host/httpserve.py --dir <directory> --port <port>
                                   [--file NAME=PATH]... [--type .EXT=MIME]...
                                   -- <program> [args...]

    --file serves one more file, PATH, as /NAME (a package out of
    fixtures/packages, say, without a copy of it in the directory).
    --type serves files ending .EXT as MIME, where the Python library's
    guess is not what a server of the Newton's day sent (a WAV as
    audio/x-wav, which Newt's Cape's audio helper asks for, not
    audio/wav).  A .pkg is always application/x-newton-compatible-pkg,
    the Newton package's own type, whatever the host's table says.

Inputs / outputs
    The directory to serve and the port (on 127.0.0.1 only; 0 takes a free
    one, so two runs at once - two build directories, tools/host/stress.py
    copies - never meet).  The program is told the port in the environment
    variable NEWTON_HTTP_PORT, which a script reads with HostGetEnv
    (src/host/demo/nethopper.ns).  Output: the
    requests served and the program's output (stdout and stderr, merged).
    Exits with the program's exit status (1 when the port cannot be had).
"""

import argparse
import functools
import http.server
import os
import subprocess
import sys
import threading


class Handler(http.server.SimpleHTTPRequestHandler):
    extra_files = {}
    # a Newton package goes out as one, whatever the host's own MIME table
    # says (Linux's /etc/mime.types makes .pkg an Apple installer's XML,
    # which a browser then reads as text; Windows has no entry for it)
    extra_types = {".pkg": "application/x-newton-compatible-pkg"}

    def guess_type(self, path):
        for ext, mime in self.extra_types.items():
            if path.lower().endswith(ext.lower()):
                return mime
        return super().guess_type(path)

    def log_message(self, fmt, *args):
        pass

    def translate_path(self, path):
        name = path.split("?", 1)[0].lstrip("/")
        if name in self.extra_files:
            return self.extra_files[name]
        return super().translate_path(path)

    def log_request(self, code="-", size="-"):
        method_path = self.requestline.split(" ")
        path = method_path[1] if len(method_path) > 1 else "?"
        # (an HTTPStatus prints as "HTTPStatus.OK" before Python 3.11: the
        # number, whichever Python this is)
        if isinstance(code, int):
            code = int(code)
        print("[http] %s %s %s" % (self.command, path, code), flush=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--file", action="append", default=[], help="NAME=PATH: PATH served as /NAME")
    ap.add_argument("--type", action="append", default=[], help=".EXT=MIME: files ending .EXT served as MIME")
    ap.add_argument("program", nargs=argparse.REMAINDER)
    args = ap.parse_args(argv)
    program = args.program
    if program and program[0] == "--":
        program = program[1:]
    if not program:
        ap.error("no program to run")
    for spec in args.file:
        name, _, path = spec.partition("=")
        Handler.extra_files[name] = os.path.abspath(path)
    for spec in args.type:
        ext, _, mime = spec.partition("=")
        Handler.extra_types[ext] = mime
    try:
        server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port),
                                                 functools.partial(Handler, directory=args.dir))
    except OSError as e:
        print("[http] cannot serve on port %d: %s" % (args.port, e), flush=True)
        return 1
    port = server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    print("[http] serving %s on 127.0.0.1:%d" % (args.dir, port), flush=True)
    env = dict(os.environ, NEWTON_HTTP_PORT=str(port))
    proc = subprocess.Popen(program, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    for raw in proc.stdout:
        # (the program's bytes as they are: a console's code page cannot
        # take everything a Newton prints)
        sys.stdout.buffer.write(raw)
        sys.stdout.buffer.flush()
    status = proc.wait()
    server.shutdown()
    print("[http] the program answered %d" % status, flush=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
