#!/usr/bin/env python3
"""Put newton together as an AppLoad application for a reMarkable Paper Pro
(docs/host-remarkable.md):

    python tools/remarkable/package.py --newton <aarch64 newton> --objects <romsrc-objects.bin> -o tmp/rmpp-app/newton
        [--display 320x480] [--name Newton] [--old-appload]

OUT/ then holds

    newton, romsrc-objects.bin   the program and what it boots from (the
                                 object file is the same bytes on every host)
    run.sh                       what AppLoad starts: newton on the internal
                                 store /home/root/newton-data/internal.store,
                                 its output appended to newton.log beside it
    external.manifest.json       AppLoad's description: qtfb on, full screen,
                                 turning with the tablet (AppLoad v0.6.0 on;
                                 --old-appload for an earlier one), the pen
                                 read directly
    icon.png                     the launcher's icon (tools/remarkable/icon.py)

Copied to /home/root/xovi/exthome/appload/<dir>, it shows in AppLoad's
launcher.
"""
import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from icon import newton_icon  # noqa: E402

RUN_SH = """#!/bin/sh
# Newton OS on the reMarkable (docs/host-remarkable.md) - started by AppLoad
# with QTFB_KEY set; the store and the log live in /home/root/newton-data/<app>
cd "$(dirname "$0")"
DATA=/home/root/newton-data/$(basename "$PWD")     # one store per app: each display size calibrates its own pen
mkdir -p "$DATA/printed"
echo "--- $(date) newton starting, pid $$" >> "$DATA/newton.log"
# the window's tracing and snapshots (kill -USR2 <pid> writes $DATA/panel-N.pgm)
export NEWTON_RM_TRACE=1 NEWTON_RM_SNAPDIR="$DATA" NEWTON_RM_PENLOG="$DATA/pen-$(date +%Y%m%d-%H%M%S).log"
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
    parser.add_argument("--display", default="810x1080")      # the Paper Pro panel at 2x - 1:1 (1620x2160) is too small to use
    parser.add_argument("--name", default="Newton")
    parser.add_argument("--old-appload", action="store_true",
                        help="for an AppLoad before v0.6.0 (September 2026): no supportsRotation, newton portrait unless "
                             "NEWTON_RM_ORIENTATION says otherwise")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE",
                        help="an environment variable for newton (the window's NEWTON_RM_* settings, docs/host-remarkable.md)")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    shutil.copy(args.newton, os.path.join(args.out, "newton"))
    shutil.copy(args.objects, os.path.join(args.out, "romsrc-objects.bin"))
    with open(os.path.join(args.out, "run.sh"), "w", newline="\n") as f:
        f.write(RUN_SH.format(display=args.display))
    manifest = {"name": args.name, "application": "run.sh", "qtfb": True, "disablesWindowedMode": True}
    # the Marker read from its own device, its points mapped by what AppLoad's
    # pen events show (remarkable/PenFit.h): AppLoad's own come bunched
    # behind xochitl's redraws, which made jagged ink
    manifest["environment"] = {"NEWTON_RM_PEN": "evdev"}
    if not args.old_appload:
        # AppLoad v0.6.0 on shows the framebuffer square on the glass however
        # the tablet is turned and says which way: the Newton's screen turns
        # to match as it runs - landscape with the type folio
        # (docs/host-remarkable.md, "Rotation")
        manifest["supportsRotation"] = True
        manifest["environment"]["NEWTON_RM_ORIENTATION"] = "appload"
    for setting in args.env:
        name, _, value = setting.partition("=")
        manifest.setdefault("environment", {})[name] = value
    with open(os.path.join(args.out, "external.manifest.json"), "w", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    newton_icon(os.path.join(args.out, "icon.png"))
    # (scp from Windows does not carry the executable bit: AppLoad then
    # starts run.sh, which cannot exec newton, and the application closes
    # at once with nothing in its log)
    name = os.path.basename(os.path.normpath(args.out))
    print("package: %s - copy it to /home/root/xovi/exthome/appload/ and make it executable there:\n"
          "  scp -r %s root@10.11.99.1:/home/root/xovi/exthome/appload/\n"
          "  ssh root@10.11.99.1 chmod +x /home/root/xovi/exthome/appload/%s/newton /home/root/xovi/exthome/appload/%s/run.sh"
          % (args.out, args.out, name, name))


if __name__ == "__main__":
    main()
