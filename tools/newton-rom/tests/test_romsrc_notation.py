#!/usr/bin/env python3
"""The natural form's notation and rename (ctest tools.ROMSourceNotation).

    python tools/newton-rom/tests/test_romsrc_notation.py

romsrc/ is written in a notation romsrc.py's Reader reads (romsrc/README.md,
"Reading it"): a function in its frame (`func ... end`, NewtonScript to the
end of its expression), compile(...) for one with constants or compiler
options, @name for a magic pointer (magic.tsv), sums of constants
(constants.tsv).  These read a tree of their own, so nothing needs the host's
newtonscript: the records the builder would hand the compiler are checked
as text.  rename (romsrc.py rename) renames a definition and leaves the
tags, symbols and functions' source that spell the same name.  Temporary
files go under the repository's tmp/.
"""

import os
import shutil
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "newton-rom", "analysis"))
import romsrc			# noqa: E402

WORK = os.path.join(ROOT, "tmp", "romsrc-notation")

MAGIC = """# the magic-pointer table
table	3af000
0	someTable
1	protoThing	otherName
"""
CONSTANTS = """# constants
vVisible	0x1
vClickable	0x200
vfPen	<<8
"""


def tree(objects, layout=""):
	"""A tree under tmp/: objects {file: text}, a layout's lines."""
	shutil.rmtree(WORK, ignore_errors=True)
	os.makedirs(os.path.join(WORK, "objects", "sub"))
	for name, text in {"magic.tsv": MAGIC, "constants.tsv": CONSTANTS, "maps.ns": "",
					   "layout.tsv": "# layout\n" + layout}.items():
		with open(os.path.join(WORK, name), "w", encoding="utf-8", newline="\n") as f:
			f.write(text)
	for name, text in objects.items():
		with open(os.path.join(WORK, "objects", name), "w", encoding="utf-8", newline="\n") as f:
			f.write(text)
	return WORK


def read(text):
	names = romsrc.TreeNames(tree({}))
	return dict(romsrc.Reader(text, "test", WORK, names).definitions())


class Notation(unittest.TestCase):
	def test_function_in_its_frame(self):
		defs = read('thing := {a: func(a1) begin\n\tif a1 then begin x := "end, }"; $) end;\n\t// end\n\t1\nend, b: 2};')
		frame = defs["thing"]
		self.assertEqual(frame.tags, ["a", "b"])
		options, record = frame.items[0].source
		self.assertEqual(options, "")
		self.assertTrue(record.startswith("func(a1) begin"))
		self.assertTrue(record.endswith("\t1\nend\n@@end\n"))
		self.assertEqual(frame.items[1].ref, 2 << 2)

	def test_compile_with_constants_and_options(self):
		defs = read('f := compile(\n\tconstants: {\n\t\tkHelp: func() begin @protoThing end,\n\t\tkValue: {a: 1}\n\t},\n'
					'\toptions: "argframe=nil,0",\n\tfunc() call kHelp with ());')
		options, record = defs["f"].source
		self.assertEqual(options, " argframe=nil,0")
		self.assertEqual(record, "@@const kHelp\nfunc() begin @1 end\n@@const kValue\n{a: 1}\n@@main\nfunc() call kHelp with ()\n@@end\n")
		# (what record_flags makes of a record's @@ line)
		self.assertEqual(romsrc.record_flags("@@ 0" + options), options)

	def test_magic_names(self):
		defs = read("a := [b: @protoThing, @otherName, @0, @SOMETABLE];")
		self.assertEqual([v.ref for v in defs["a"].items], [(1 << 2) | 3, (1 << 2) | 3, 3, 3])
		with self.assertRaises(SyntaxError):
			read("a := @nothing;")
		with self.assertRaises(SyntaxError):
			read("a := func() @nothing;")

	def test_constants(self):
		defs = read("a := {viewFlags: vVisible + vClickable, viewFormat: vfPen(2) + 1, n: -3 + 4};")
		self.assertEqual([v.ref >> 2 for v in defs["a"].items], [0x201, 0x201, 1])
		with self.assertRaises(SyntaxError):
			read('a := "x" + 1;')

	def test_code_extent(self):
		text = "func(a) begin foreach k, v in a do Print('|end|); repeat a := a - 1 until a < 0 end, next"
		self.assertEqual(text[romsrc.code_extent(text, 0):], ", next")
		self.assertEqual(romsrc.code_with_magic_numbers('func() "@protoThing" & @protoThing', {"protothing": 1}),
						 'func() "@protoThing" & @1')

	def test_constants_in_functions(self):
		constants = {"vVisible": 1, "vClickable": 0x200, "kErr": -48406, "vfPen": lambda n: n << 8}
		same = lambda source: romsrc.code_with_constants(source, constants)
		# (a sum that is a whole operand is one number: what the ROM's source had)
		self.assertEqual(same("func() {viewFlags: vVisible + vClickable, x: vfPen(2)}"), "func() {viewFlags: 513, x: 512}")
		self.assertEqual(same("func() Throw('|evt.ex|, kErr)"), "func() Throw('|evt.ex|, -48406)")
		self.assertEqual(same("func() x - kErr"), "func() x - -48406")
		# (not a whole operand: each name its number)
		self.assertEqual(same("func() x + vVisible + vClickable"), "func() x + 1 + 512")
		self.assertEqual(same("func() vVisible + vClickable * 2"), "func() 1 + 512 * 2")
		# (a slot, a symbol, a message, a tag are not constants)
		self.assertEqual(same("func() {vVisible: a.vVisible, b: 'vVisible, c: x:vVisible()}"),
						 "func() {vVisible: a.vVisible, b: 'vVisible, c: x:vVisible()}")
		self.assertEqual(read("f := func() {viewFlags: vVisible + vClickable};")["f"].source[1],
						 "func() {viewFlags: 513}\n@@end\n")

	def test_old_form_still_read(self):
		defs = read('a := {b: function("functions/a.b.ns"), c: @1};')
		self.assertEqual(defs["a"].items[0].data, "functions/a.b.ns")


class Rename(unittest.TestCase):
	def test_rename(self):
		src = tree({"sub/one.ns": "// thing\n\nthing := {thing: 'thing, other: other, f: func() begin thing := 1 end};\n",
					"two.ns": "other := {back: thing, |thing|: 1};\n\nshared := same(\"thing.f.literals[0]\");\n"},
				   "3af100\tthing\t43\n3af140\tthing.other\t43\nalias\tthing.f.literals[0]\tshared\n")
		with open(os.path.join(src, "magic.tsv"), "a", encoding="utf-8") as f:
			f.write("2\tthing\n")
		with open(os.path.join(src, "objects", "three.ns"), "w", encoding="utf-8") as f:
			f.write("x := {p: @thing, f: func() @thing.y};\n")
		report = romsrc.rename_definition(src, "thing", "widget")
		self.assertTrue(report)
		with open(os.path.join(src, "objects", "sub", "one.ns"), encoding="utf-8") as f:
			self.assertEqual(f.read(), "// thing\n\nwidget := {thing: 'thing, other: other, f: func() begin thing := 1 end};\n")
		with open(os.path.join(src, "objects", "two.ns"), encoding="utf-8") as f:
			self.assertEqual(f.read(), "other := {back: widget, |thing|: 1};\n\nshared := same(\"widget.f.literals[0]\");\n")
		with open(os.path.join(src, "objects", "three.ns"), encoding="utf-8") as f:
			self.assertEqual(f.read(), "x := {p: @widget, f: func() @widget.y};\n")
		with open(os.path.join(src, "layout.tsv"), encoding="utf-8") as f:
			self.assertEqual(f.read().split("\n")[1:4], ["3af100\twidget\t43", "3af140\twidget.other\t43",
														 "alias\twidget.f.literals[0]\tshared"])
		with open(os.path.join(src, "magic.tsv"), encoding="utf-8") as f:
			self.assertIn("2\twidget", f.read())
		with self.assertRaises(ValueError):
			romsrc.rename_definition(src, "widget", "OTHER")		# (defined already, whatever its case)
		with self.assertRaises(ValueError):
			romsrc.rename_definition(src, "widget", "vVisible")		# (a constant's name)


if __name__ == "__main__":
	unittest.main()
