#!/usr/bin/env python3
"""Which functions in romsrc/ have no comment.

    python tools/newton-rom/analysis/romsrc_comments.py [tree] [--top N] [--list]

romsrc/ is commented (romsrc/README.md, "Comments and constants"): every
function has a `//` comment above it.  This counts the function slots
(`name: func(` in a frame, `name := func(` at the top) whose previous
non-blank line is not a `//` comment, and prints the total and the N files
with the most (default 15); --list prints each one as file:line name.
A function a comment above its group covers, or one commented on its own
line, is counted as bare too, so the count is an upper bound.

tree is romsrc (the default) or any tree in its notation (rex/<package>).
Reads only the .ns files; writes nothing.
"""

import argparse
import collections
import glob
import os
import re

SLOT = re.compile(r"^\s*([A-Za-z_]\w*|\|[^|]*\|)\s*:\s*func\s*\(")
DEFINITION = re.compile(r"^\s*([A-Za-z_]\w*)\s*:=\s*func\s*\(")


def bare_functions(tree):
	"""Each function with no comment above it: (file, line, name)."""
	for path in sorted(glob.glob(os.path.join(tree, "**", "*.ns"), recursive=True)):
		with open(path, encoding="utf-8") as f:
			lines = f.read().split("\n")
		for i, line in enumerate(lines):
			m = SLOT.match(line) or DEFINITION.match(line)
			if not m:
				continue
			j = i - 1
			while j >= 0 and not lines[j].strip():
				j -= 1
			if j < 0 or not lines[j].strip().startswith("//"):
				yield path, i + 1, m.group(1)


def count_functions(tree):
	n = 0
	for path in glob.glob(os.path.join(tree, "**", "*.ns"), recursive=True):
		with open(path, encoding="utf-8") as f:
			n += sum(1 for line in f if SLOT.match(line) or DEFINITION.match(line))
	return n


def main():
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument("tree", nargs="?", default="romsrc")
	p.add_argument("--top", type=int, default=15, help="files to show, most bare functions first")
	p.add_argument("--list", action="store_true", help="every bare function, file:line name")
	args = p.parse_args()
	bare = list(bare_functions(args.tree))
	print("functions %d, without a comment above %d" % (count_functions(args.tree), len(bare)))
	if args.list:
		for path, line, name in bare:
			print("%s:%d %s" % (path, line, name))
		return
	for path, n in collections.Counter(path for path, _, _ in bare).most_common(args.top):
		print("%5d %s" % (n, path))


if __name__ == "__main__":
	main()
