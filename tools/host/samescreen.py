#!/usr/bin/env python3
"""Boot the host OS more than one way and check that the screens are the same.

    python tools/host/samescreen.py --newton build/host/host/newton \\
        --script src/host/demo/newton.ns --snapshot build/newton-demo.pgm \\
        --run "--rom build/MP2x00US/rom.bin" --run "--objects build/objects.bin"

Each --run is the newton program's arguments for one boot (the script and
`--headless 5` are added).  Each boot runs in a fresh directory of its own,
so the snapshot the script writes (--snapshot, relative to the working
directory, as newton.ns's ScreenSnapshot is) does not collide with another
boot's or another test's.  The snapshots are then compared byte for byte:
the exit code is 0 when they are all identical, 1 when they differ (how
many pixels, and where the first difference is, is printed), 2 when a
boot wrote none.  With --keep DIR the snapshots are copied there.

The ROM-free track uses it to show that the OS booted on the object file
built from the ROM source tree (newton --objects) draws exactly what it
draws booted on the ROM image (ctest host.NewtonNoROMSameScreen;
docs/rom-free/README.md).
"""
import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--newton", required=True)
	ap.add_argument("--script", required=True)
	ap.add_argument("--snapshot", required=True, help="the snapshot's path relative to the working directory")
	ap.add_argument("--run", action="append", required=True, help="one boot's arguments")
	ap.add_argument("--headless", default="5")
	ap.add_argument("--keep", help="copy the snapshots here")
	a = ap.parse_args(argv)
	newton = os.path.abspath(a.newton)
	if not os.path.exists(newton) and os.path.exists(newton + ".exe"):
		newton += ".exe"
	script = os.path.abspath(a.script)
	shots = []
	for i, run in enumerate(a.run):
		work = tempfile.mkdtemp(prefix="samescreen")
		os.makedirs(os.path.join(work, os.path.dirname(a.snapshot)), exist_ok=True)
		args = [os.path.abspath(x) if os.path.exists(x) else x for x in shlex.split(run)]
		cmd = [newton] + args + ["--headless", a.headless, "--script", script]
		result = subprocess.run(cmd, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
								errors="replace")
		path = os.path.join(work, a.snapshot)
		if not os.path.exists(path):
			print("boot %d (%s) wrote no snapshot; its output ended:\n%s" % (i, run, result.stdout[-2000:]))
			return 2
		with open(path, "rb") as f:
			shots.append(f.read())
		if a.keep:
			os.makedirs(a.keep, exist_ok=True)
			shutil.copy(path, os.path.join(a.keep, "boot%d%s" % (i, os.path.splitext(a.snapshot)[1])))
		shutil.rmtree(work, ignore_errors=True)
	status = 0
	for i in range(1, len(shots)):
		if shots[i] != shots[0]:
			diff = [k for k, (x, y) in enumerate(zip(shots[0], shots[i])) if x != y]
			print("boot %d's screen differs from boot 0's: %d bytes, the first at offset %d%s"
				  % (i, len(diff), diff[0] if diff else -1,
					 "" if len(shots[i]) == len(shots[0]) else " (the sizes differ too)"))
			status = 1
	if status == 0:
		print("the %d boots drew the same screen (%d bytes)" % (len(shots), len(shots[0])))
	return status


if __name__ == "__main__":
	sys.exit(main())
