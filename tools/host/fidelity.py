#!/usr/bin/env python3
"""The ROM-free boot's fidelity beyond the Setup screen.

Purpose
    The OS booted on the object file built from romsrc/ should draw what it
    draws booted on the ROM image.  host.NewtonNoROMSameScreen checks the
    Setup screen; this walks on through the built-in applications, pickers,
    slips and Prefs panels (src/host/demo/fidelity.ns, a snapshot per step)
    in three boots and compares the snapshots with tools/imaging/pgmdiff.py:

      rom       --rom <image>
      original  --objects <file built with `romsrc.py build --original`> -
                the committed tree less what was added to it on purpose (the
                Newton Internet Enabler's three packages in the extension)
      default   newton's default boot, <build>/romsrc-objects.bin

    rom and original must be the same at every step: a difference there is
    the builder or romsrc/ getting something wrong.  default may differ from
    rom only at the steps EXPECTED lists, each with its reason (what the
    NIE's packages add); any other difference, or an expected one that has
    gone, fails.  A fourth boot, rom again, gives pgmdiff its --noise run, so
    a pixel that differs between two boots of the same image (a timer) is not
    counted.

Usage
    python tools/host/fidelity.py <newton> --rom <rom.bin> --original <objects.bin>
        [--default <objects.bin>] [--out DIR] [--no-noise]

Inputs / outputs
    The snapshots go in DIR/{rom,original,default,noise}/NN-step.pgm (default
    tmp/fidelity), marked PNGs of the differences in DIR/diff-*/.  Prints
    each comparison and a verdict; exits 0 when every difference is accounted
    for (ctest host.NewtonROMFreeFidelity).
"""

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PGMDIFF = os.path.join(ROOT, "tools", "imaging", "pgmdiff.py")
SCRIPT = os.path.join(ROOT, "src", "host", "demo", "fidelity.ns")

# The steps at which the default boot (with the NIE built into the ROM
# extension, romsrc/README.md) draws something the ROM does not, and why.
EXPECTED = {
	"preferenceRoll": "Newton Devices (rex/newtdev.pkg) adds an AppleTalk panel to the Prefs list",
	"prefs-sound": "the Prefs list showing below the Sound panel has Newton Devices' AppleTalk entry",
	"prefs-sleep": "the Prefs list showing below the Sleep panel has Newton Devices' AppleTalk entry",
	"prefs-handwriting": "the Prefs list showing below the Handwriting panel is one row longer with Newton Devices' AppleTalk entry: its last row (Network Printers, the host's Host page above it) shows",
}
# (the NIE's icons are not in the Extras drawer's Unfiled Icons, so that step
# is the same in both; the Handwriting Recognition panel covered the list's
# last rows until the host's Host preferences page made it one row longer)


def boot(newton, args, out, label, timeout):
	shots = os.path.join(out, label)
	os.makedirs(shots, exist_ok=True)
	store = os.path.join(out, label + ".store")
	cmd = [newton, "--display", "320x480", "--serial-port", "none", "--erase", "--store", store,
		   "--headless", str(timeout), "--script", SCRIPT] + args
	env = dict(os.environ, FIDELITY_DIR=shots)
	r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, text=True, errors="replace")
	if "fidelity: done" not in r.stdout:
		print("%s: the walk did not finish; its output ended:\n%s" % (label, r.stdout[-3000:]))
		return False
	if "waited in vain" in r.stdout:
		print("%s: a step waited in vain:\n%s" % (label, "\n".join(l for l in r.stdout.splitlines() if "vain" in l)))
		return False
	print("%s: %d snapshots" % (label, len([f for f in os.listdir(shots) if f.endswith(".pgm")])))
	return True


def compare(a, b, out, name, noise):
	cmd = [sys.executable, PGMDIFF, a, b, "--png", os.path.join(out, "diff-" + name)]
	if noise:
		cmd += ["--noise", noise]
	r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
	differ = {}
	for line in r.stdout.splitlines():
		step, _, rest = line.partition(".pgm: ")
		if rest and not rest.startswith("same"):
			differ[step.partition("-")[2]] = rest		# (keyed by the step's name, not its number)
	return differ


def main():
	ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
	ap.add_argument("newton")
	ap.add_argument("--rom", required=True)
	ap.add_argument("--original", required=True)
	ap.add_argument("--default")
	ap.add_argument("--out", default=os.path.join(ROOT, "tmp", "fidelity"))
	ap.add_argument("--no-noise", action="store_true")
	ap.add_argument("--headless", type=int, default=240)
	a = ap.parse_args()
	newton = os.path.abspath(a.newton)
	if not os.path.exists(newton) and os.path.exists(newton + ".exe"):
		newton += ".exe"
	shutil.rmtree(a.out, ignore_errors=True)
	os.makedirs(a.out)

	runs = [("rom", ["--rom", os.path.abspath(a.rom)]),
			("original", ["--objects", os.path.abspath(a.original)]),
			("default", ["--objects", os.path.abspath(a.default)] if a.default else [])]
	if not a.no_noise:
		runs.append(("noise", ["--rom", os.path.abspath(a.rom)]))
	for label, args in runs:
		if not boot(newton, args, a.out, label, a.headless):
			return 1
	noise = None if a.no_noise else os.path.join(a.out, "noise")
	rom = os.path.join(a.out, "rom")
	ok = True

	differ = compare(rom, os.path.join(a.out, "original"), a.out, "original", noise)
	for step, what in sorted(differ.items()):
		print("rom vs original: %s: %s - the builder or romsrc/ draws differently" % (step, what))
		ok = False
	if not differ:
		print("rom vs original: every step the same")

	differ = compare(rom, os.path.join(a.out, "default"), a.out, "default", noise)
	for step, what in sorted(differ.items()):
		if step in EXPECTED:
			print("rom vs default: %s: %s - expected: %s" % (step, what, EXPECTED[step]))
		else:
			print("rom vs default: %s: %s - not explained" % (step, what))
			ok = False
	for step in sorted(set(EXPECTED) - set(differ)):
		print("rom vs default: %s: the same, where a difference was expected (%s)" % (step, EXPECTED[step]))
		ok = False

	print("fidelity: %s" % ("every difference accounted for" if ok else "FAILED"))
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main())
