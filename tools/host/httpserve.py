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
    python tools/host/httpserve.py --dir <directory> --port <port> -- <program> [args...]

Inputs / outputs
    The directory to serve and the port (on 127.0.0.1 only).  Output: the
    requests served and the program's output (stdout and stderr, merged).
    Exits with the program's exit status (1 when the port cannot be had).
"""

import argparse
import functools
import http.server
import subprocess
import sys
import threading


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def log_request(self, code="-", size="-"):
        method_path = self.requestline.split(" ")
        path = method_path[1] if len(method_path) > 1 else "?"
        print("[http] %s %s %s" % (self.command, path, code), flush=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("program", nargs=argparse.REMAINDER)
    args = ap.parse_args(argv)
    program = args.program
    if program and program[0] == "--":
        program = program[1:]
    if not program:
        ap.error("no program to run")
    try:
        server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port),
                                                 functools.partial(Handler, directory=args.dir))
    except OSError as e:
        print("[http] cannot serve on port %d: %s" % (args.port, e), flush=True)
        return 1
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    print("[http] serving %s on 127.0.0.1:%d" % (args.dir, args.port), flush=True)
    proc = subprocess.Popen(program, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for raw in proc.stdout:
        sys.stdout.write(raw.decode("utf-8", errors="replace"))
        sys.stdout.flush()
    status = proc.wait()
    server.shutdown()
    print("[http] the program answered %d" % status, flush=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
