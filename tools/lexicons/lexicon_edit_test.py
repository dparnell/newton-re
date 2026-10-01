#!/usr/bin/env python3
"""The test that a word list in the ROM source tree is editable: a word
added to romsrc/lexicons/gEnum80sh_words.words is known to the machine
built from it, and not to the one built from the unedited tree.

    python tools/lexicons/lexicon_edit_test.py --tree romsrc -o <dir> \\
        --newton <newton> --newtonscript <newtonscript> \\
        --script src/host/demo/lexiconedit.ns [--original <objects file>]

It copies the tree into <dir>/tree, adds the word "zorbleflax" to the
general word list (one line - the trie is rebuilt from the words by
tools/lexicons/newtonlex.py, and being bigger than the room it had it is
moved, the object file saying where), builds the copy with
tools/newton-rom/analysis/romsrc.py build --relayout (and the original
tree too unless --original gives its object file), and boots the OS on
each (newton --objects, headless) with the script, which asks LookupWord
about "hello" and "zorbleflax".  Exit 0 when the edited machine knows
both and the original only "hello" (ctest host.ROMSourceLexiconEdit).
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROMSRC = os.path.join(HERE, "..", "newton-rom", "analysis", "romsrc.py")
WORD = "zorbleflax"
LIST = os.path.join("lexicons", "gEnum80sh_words.words")


def build(tree, out, newtonscript):
	subprocess.check_call([sys.executable, ROMSRC, "build", tree, "--relayout", "-o", out,
						   "--newtonscript", newtonscript])


def boot(newton, objects, script):
	run = subprocess.run([newton, "--objects", objects, "--headless", "60", "--script", script],
						 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
	text = run.stdout.decode("utf-8", "replace")
	known = {}
	for line in text.splitlines():
		if "lexiconedit:" in line and " is " in line:
			word, what = line.split("lexiconedit:", 1)[1].strip().strip('"').split(" is ")
			known[word] = what == "known"
	return known, text


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
	with open(os.path.join(tree, LIST), "a", encoding="utf-8", newline="\n") as f:
		f.write(WORD + "\n")
	edited = os.path.join(a.out, "edited-objects.bin")
	build(tree, edited, a.newtonscript)
	original = a.original
	if original is None:
		original = os.path.join(a.out, "original-objects.bin")
		build(a.tree, original, a.newtonscript)

	before, text_before = boot(a.newton, original, a.script)
	after, text_after = boot(a.newton, edited, a.script)
	print("the original tree:", before)
	print("the edited tree:  ", after)
	if before.get("hello") and not before.get(WORD, True) and after.get("hello") and after.get(WORD):
		print("the word added to the word list is known")
		return 0
	print(text_before[-2000:])
	print(text_after[-2000:])
	print("the word added to the word list is NOT known as it should be")
	return 1


if __name__ == "__main__":
	sys.exit(main())
