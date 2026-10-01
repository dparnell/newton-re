#!/usr/bin/env python3
"""The test that the collation table in the ROM source tree is editable:
'a' and 'z' given each other's primary weights in the built-in sorting
table's text, and the machine built from it sorts "zebra" before "apple",
where the one built from the unedited tree does not.

    python tools/tables/table_edit_test.py --tree romsrc -o <dir> \\
        --newton <newton> --newtonscript <newtonscript> \\
        --script src/host/demo/tableedit.ns [--original <objects file>]

It copies the tree into <dir>/tree, swaps the primary weights (the second
column) of U+0041/U+005A and of U+0061/U+007A in
resources/Sort/62cbb9.txt (the table with id 1, the ROM's default), builds
the copy with tools/newton-rom/analysis/romsrc.py build --relayout (and the
original tree too unless --original gives its object file), and boots the
OS on each with the script, which prints StrCompare("apple", "zebra").
Exit 0 when the original answers less than nought and the edited more
(ctest host.ROMSourceTableEdit).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROMSRC = os.path.join(HERE, "..", "newton-rom", "analysis", "romsrc.py")
TABLE = os.path.join("resources", "Sort", "62cbb9.txt")


def swap_weights(path, a, b):
	with open(path, encoding="utf-8") as f:
		lines = f.read().split("\n")
	ia = next(i for i, l in enumerate(lines) if l.startswith("U+%04X " % a))
	ib = next(i for i, l in enumerate(lines) if l.startswith("U+%04X " % b))
	pa, pb = lines[ia].split(), lines[ib].split()
	pa[1], pb[1] = pb[1], pa[1]
	lines[ia] = " ".join(pa[:3]) + "\t" + " ".join(pa[3:])
	lines[ib] = " ".join(pb[:3]) + "\t" + " ".join(pb[3:])
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines))


def build(tree, out, newtonscript):
	subprocess.check_call([sys.executable, ROMSRC, "build", tree, "--relayout", "-o", out,
						   "--newtonscript", newtonscript])


def boot(newton, objects, script):
	run = subprocess.run([newton, "--objects", objects, "--headless", "60", "--script", script],
						 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
	text = run.stdout.decode("utf-8", "replace")
	m = re.search(r"tableedit: apple against zebra (-?\d+)", text)
	return (int(m.group(1)) if m else None), text


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--tree", required=True)
	ap.add_argument("-o", "--out", required=True)
	ap.add_argument("--newton", required=True)
	ap.add_argument("--newtonscript", required=True)
	ap.add_argument("--script", required=True)
	ap.add_argument("--original")
	a = ap.parse_args(argv)

	tree = os.path.join(a.out, "tree")
	if os.path.exists(tree):
		shutil.rmtree(tree)
	shutil.copytree(a.tree, tree)
	swap_weights(os.path.join(tree, TABLE), 0x41, 0x5a)
	swap_weights(os.path.join(tree, TABLE), 0x61, 0x7a)
	edited = os.path.join(a.out, "edited-objects.bin")
	build(tree, edited, a.newtonscript)
	original = a.original
	if original is None:
		original = os.path.join(a.out, "original-objects.bin")
		build(a.tree, original, a.newtonscript)

	before, text_before = boot(a.newton, original, a.script)
	after, text_after = boot(a.newton, edited, a.script)
	print("the original tree: StrCompare(\"apple\", \"zebra\") =", before)
	print("the edited tree:   StrCompare(\"apple\", \"zebra\") =", after)
	if before is not None and after is not None and before < 0 < after:
		print("the collation edit reached the machine")
		return 0
	print(text_before[-2000:])
	print(text_after[-2000:])
	print("the collation edit did NOT reach the machine")
	return 1


if __name__ == "__main__":
	sys.exit(main())
