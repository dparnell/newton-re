#!/usr/bin/env python3
"""Put newton together as an AppLoad application for a reMarkable Paper Pro
(docs/host-remarkable.md):

    python tools/remarkable/package.py --newton <aarch64 newton> --objects <romsrc-objects.bin> -o tmp/rmpp-app/newton
        [--display 320x480] [--name Newton] [--rmkit]

OUT/ then holds

    newton, romsrc-objects.bin   the program and what it boots from (the
                                 object file is the same bytes on every host)
    run.sh                       what AppLoad starts: newton on the internal
                                 store /home/root/newton-data/internal.store,
                                 its output appended to newton.log beside it
    external.manifest.json       AppLoad's description: qtfb on, full screen
    icon.png                     the launcher's icon

Copied to /home/root/xovi/exthome/appload/<dir>, it shows in AppLoad's
launcher.  --rmkit makes the variant whose window is rmkit's (a newton built
with -DNEWTON_RMKIT_DIR) under AppLoad's qtfb-shim, as KOReader runs:
LD_PRELOAD=/home/root/shims/qtfb-shim.so in native mode.
"""
import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from build_probe import icon  # noqa: E402

RUN_SH = """#!/bin/sh
# Newton OS on the reMarkable (docs/host-remarkable.md) - started by AppLoad
# with QTFB_KEY set; the store and the log live in /home/root/newton-data
cd "$(dirname "$0")"
DATA=/home/root/newton-data
mkdir -p "$DATA/printed"
echo "--- $(date) newton starting, pid $$" >> "$DATA/newton.log"
# the window's tracing and snapshots (kill -USR2 <pid> writes $DATA/panel-N.pgm)
export NEWTON_RM_TRACE=1 NEWTON_RM_SNAPDIR="$DATA"
# a test script left in $DATA runs at boot (docs/host-remarkable.md)
[ -f "$DATA/script.ns" ] && set -- --script "$DATA/script.ns" "$@"
exec ./newton --objects romsrc-objects.bin --display {display} --store "$DATA/internal.store" \\
    --print-dir "$DATA/printed" "$@" >> "$DATA/newton.log" 2>&1
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--newton", required=True)
    parser.add_argument("--objects", required=True)
    parser.add_argument("-o", "--out", required=True)
    parser.add_argument("--display", default="320x480")
    parser.add_argument("--name", default="Newton")
    parser.add_argument("--rmkit", action="store_true")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    shutil.copy(args.newton, os.path.join(args.out, "newton"))
    shutil.copy(args.objects, os.path.join(args.out, "romsrc-objects.bin"))
    with open(os.path.join(args.out, "run.sh"), "w", newline="\n") as f:
        f.write(RUN_SH.format(display=args.display))
    manifest = {"name": args.name, "application": "run.sh", "qtfb": True, "disablesWindowedMode": True}
    if args.rmkit:
        manifest["environment"] = {
            "NEWTON_RM_PANEL_KIND": "rmkit",
            "LD_PRELOAD": "/home/root/shims/qtfb-shim.so",
            "QTFB_SHIM_MODEL": "false",
            "QTFB_SHIM_INPUT_MODE": "NATIVE",
            "QTFB_SHIM_MODE": "N_RGB565",
            "QTFB_SHIM_RESPECT_FULL_REFRESH_REQUESTS": "1",
        }
    with open(os.path.join(args.out, "external.manifest.json"), "w", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    icon(os.path.join(args.out, "icon.png"), ["X   X", "XX  X", "X X X", "X  XX", "X   X"])
    print("package: %s (copy it to /home/root/xovi/exthome/appload/)" % args.out)


if __name__ == "__main__":
    main()
