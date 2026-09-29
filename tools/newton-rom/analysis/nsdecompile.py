#!/usr/bin/env python3
"""Decompile the ROM's NewtonScript functions to source that compiles back to them.

Usage:
    python nsdecompile.py <build_dir> NAME|0xREF...          print each function's source
    python nsdecompile.py <build_dir> --all -o records.txt    every top-level function, as round-trip records
    python nsdecompile.py <build_dir> --all --sample N -o records.txt
    python nsdecompile.py <build_dir> --roundtrip [--sample N] [--newtonscript EXE] [--rom IMAGE]
    python nsdecompile.py <build_dir> --compare 0xREF [--newtonscript EXE]    the source, and both codes side by side

The ROM's NewtonScript functions are code blocks - [class, instructions,
literals, argFrame, numArgs | numLocals << 16] - compiled by Apple's
compiler, which is the one the ROM itself carries and the reconstruction
has as frames/Compiler.cpp.  That compiler turns each construct into a
fixed shape of bytecode (TCompiler::WalkForCode), so the decompiler reads
the shapes back: a symbolic stack for the expressions, and each branch
matched against the shapes the compiler makes of if/then/else, and, or,
while, repeat, loop, for, foreach (do and collect), try/onexception and
break.  The source is written so that the compiler makes the same code
of it again - the same instructions, the same literals in the same
order, the same argFrame - which --roundtrip checks: every function is
compiled by the host's newtonscript (--roundtrip, host/NSRoundTrip.cpp)
and compared with the ROM's, and the failures are counted by cause.

What is lost in compiling and what the decompiler makes up:
  - the names of arguments and locals kept on the stack (a1, a2 ...; l1,
    l2 ... by stack index); the names of variables inner functions close
    over are in the argFrame and are kept;
  - where `local` was written: the compiler numbers the stack locals in
    the order their declarations come (a `local`, a for or a foreach), so
    the declarations are put where that order comes out the same;
  - constants (inlined by the compiler), comments and the layout.

This is the first step of the long-term track of booting with no ROM
image (docs/next-steps.md): the ROM's NewtonScript as source that can be
edited and built again.  docs/frames/decompiler.md has the round trip's
results.
"""

from __future__ import annotations

import argparse
import collections
import os
import random
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nsfunctions as nf			# noqa: E402

FREQ = [("+", 2), ("-", 2), ("aref", 2), ("setAref", 3), ("=", 2), ("not", 1), ("<>", 2),
		("*", 2), ("/", 2), ("div", 2), ("<", 2), (">", 2), (">=", 2), ("<=", 2),
		("BAnd", 2), ("BOr", 2), ("BNot", 2), ("NewIterator", 2), ("Length", 1), ("Clone", 1),
		("SetClass", 2), ("AddArraySlot", 2), ("Stringer", 1), ("HasPath", 2), ("ClassOf", 1)]
INFIX = {"+", "-", "*", "/", "div", "<", ">", ">=", "<=", "=", "<>"}
# infix operators the compiler calls by name (push 'op; call 2)
NAMED_INFIX = {"mod": "mod", "<<": "<<", ">>": ">>"}
RESERVED = {"and", "begin", "break", "by", "call", "constant", "deeply", "do", "div", "else", "end",
			"exists", "for", "foreach", "func", "global", "if", "in", "inherited", "local", "loop", "mod",
			"native", "not", "onexception", "or", "repeat", "return", "self", "then", "to", "try", "until",
			"while", "with", "nil", "true", "collect"}

# opcodes (the high five bits)
POP, DUP, RETURN, PUSHSELF, SETLEXSCOPE, ITERNEXT, ITERDONE, POPHANDLERS = range(8)
OP_PUSH, OP_PUSHCONST, OP_CALL, OP_INVOKE, OP_SEND, OP_SENDIF, OP_RESEND, OP_RESENDIF = 3, 4, 5, 6, 7, 8, 9, 10
OP_BRANCH, OP_BIT, OP_BIF, OP_FINDVAR, OP_GETVAR, OP_MAKEFRAME, OP_MAKEARRAY = 11, 12, 13, 14, 15, 16, 17
OP_GETPATH, OP_SETPATH, OP_SETVAR, OP_FINDSETVAR, OP_INCRVAR, OP_BILND, OP_FREQ, OP_NEWHANDLERS = 18, 19, 20, 21, 22, 23, 24, 25


class DecompileError(Exception):
	pass


class Instr:
	__slots__ = ("pc", "a", "b", "length", "index")

	def __init__(self, pc, a, b, length, index):
		self.pc, self.a, self.b, self.length, self.index = pc, a, b, length, index

	@property
	def next(self):
		return self.pc + self.length

	def simple(self, which):
		return self.a == 0 and self.b == which

	def __repr__(self):
		return "%d:%d/%d" % (self.pc, self.a, self.b)


def decode(code: bytes):
	out = []
	pc = 0
	while pc < len(code):
		op = code[pc]
		a, b, length = op >> 3, op & 7, 1
		if b == 7:
			b = struct.unpack(">H", code[pc + 1:pc + 3])[0]
			length = 3
		out.append(Instr(pc, a, b, length, len(out)))
		pc += length
	return out


# ------------------------------------------------------------------------------
#	the tree
# ------------------------------------------------------------------------------

class Node:
	value = True			# whether it leaves a value


class Lit(Node):
	def __init__(self, ref, immediate=False):
		self.ref, self.immediate = ref, immediate


class Local(Node):
	def __init__(self, index):
		self.index = index


class Var(Node):				# a variable found by name (global, or an argFrame's)
	def __init__(self, name):
		self.name = name


class Self(Node):
	pass


class Call(Node):
	def __init__(self, name, args):
		self.name, self.args = name, args


class Invoke(Node):
	def __init__(self, fn, args):
		self.fn, self.args = fn, args


class Send(Node):
	def __init__(self, rcvr, msg, args, ifdef):
		self.rcvr, self.msg, self.args, self.ifdef = rcvr, msg, args, ifdef


class Path(Node):
	def __init__(self, obj, elem, nil_for_nil):
		self.obj, self.elem, self.nil_for_nil = obj, elem, nil_for_nil


class Assign(Node):
	def __init__(self, target, value, keeps):
		self.target, self.val, self.keeps = target, value, keeps
		self.value = keeps


class MakeArray(Node):
	def __init__(self, cls, elems):
		self.cls, self.elems = cls, elems


class MakeFrame(Node):
	def __init__(self, tags, values):
		self.tags, self.values = tags, values


class Func(Node):
	def __init__(self, fn):
		self.fn = fn			# a Decompiled


class If(Node):
	def __init__(self, cond, then, els, value):
		self.cond, self.then, self.els, self.value = cond, then, els, value


class Or(Node):
	def __init__(self, a, b, value):
		self.a, self.b, self.value = a, b, value


class Block(Node):
	def __init__(self, stmts, final):
		self.stmts, self.final = stmts, final
		self.value = final is not None


class While(Node):
	def __init__(self, cond, body):
		self.cond, self.body = cond, body


class Loop(Node):
	def __init__(self, body):
		self.body = body


class Repeat(Node):
	def __init__(self, stmts, cond):
		self.stmts, self.cond = stmts, cond


class For(Node):
	def __init__(self, var, limit, incr, start, stop, by, body):
		self.var, self.limit, self.incr, self.start, self.stop, self.by, self.body = var, limit, incr, start, stop, by, body


class Foreach(Node):
	def __init__(self, slot, val, iter_, index, result, coll, deeply, body, collect):
		self.slot, self.val, self.iter, self.index, self.result = slot, val, iter_, index, result
		self.coll, self.deeply, self.body, self.collect = coll, deeply, body, collect


class Try(Node):
	def __init__(self, body, handlers, value):
		self.body, self.handlers, self.value = body, handlers, value


class Break(Node):
	def __init__(self, val):
		self.val = val


class Return(Node):
	def __init__(self, val):
		self.val = val


class Exists(Node):
	def __init__(self, kind, a, b=None):
		self.kind, self.a, self.b = kind, a, b


class LocalDecl(Node):
	value = False

	def __init__(self, names):
		self.names = names


# ------------------------------------------------------------------------------
#	reading the code
# ------------------------------------------------------------------------------

class Decompiled:
	"""A function: its tree, and the names of its variables."""

	def __init__(self, rom, ref, outer=None):
		self.rom, self.ref, self.outer = rom, ref, outer
		s = rom.slots(ref)
		if len(s) < 5 or s[0] != 0x32:
			raise DecompileError("not a 2.x function")
		self.code = rom.data(s[1])
		self.literals = rom.slots(s[2]) if rom.is_ptr(s[2]) else []
		self.argframe = s[3]
		n = s[4] >> 2
		self.num_args, self.num_locals = n & 0xffff, n >> 16
		self.instrs = decode(self.code)
		self.at = {i.pc: i for i in self.instrs}
		self.frame_names = []
		if rom.is_ptr(self.argframe):
			self.frame_names = [t for t, v in rom.frame_slots(self.argframe)][3:]
		self.arg_names = {}			# stack index -> name (closed-over arguments)
		self.loop_exits = []
		self.body = None

	# ---- names

	def local_name(self, index):
		if index in self.arg_names:
			return self.arg_names[index]
		if index < 3 + self.num_args:
			return "a%d" % (index - 2)
		return "l%d" % (index - 2 - self.num_args)

	# ---- the whole function

	def decompile(self):
		ins = self.instrs
		k = 0
		# the closed-over arguments copied into the argFrame at entry (CopyClosedArgs)
		while (k + 1 < len(ins) and ins[k].a == OP_GETVAR and ins[k + 1].a == OP_FINDSETVAR
			   and 3 <= ins[k].b < 3 + self.num_args and ins[k].b not in self.arg_names):
			name = self.symbol(self.literals[ins[k + 1].b])
			if name not in self.frame_names:
				break
			self.arg_names[ins[k].b] = name
			k += 2
		self.first = k
		if not ins or not ins[-1].simple(RETURN):
			raise DecompileError("does not end with return")
		stmts, stack = self.parse(k, len(ins) - 1, [])
		if len(stack) != 1:
			raise DecompileError("the function's value: %d on the stack" % len(stack))
		self.body = Block(stmts, stack[0]) if stmts else stack[0]
		return self

	def symbol(self, ref):
		name = self.rom.symname(ref)
		if name is None:
			raise DecompileError("not a symbol: %s" % self.rom.describe(ref))
		return name

	def lit(self, index):
		if index >= len(self.literals):
			raise DecompileError("no literal %d" % index)
		return self.literals[index]

	def index_of_pc(self, pc):
		i = self.at.get(pc)
		if i is None:
			raise DecompileError("a branch into an instruction (%d)" % pc)
		return i.index

	# ---- a range of instructions

	def value_range(self, start, end):
		"""A range that leaves one value: its node (a block when it has statements)."""
		stmts, stack = self.parse(start, end, [])
		if len(stack) != 1:
			raise DecompileError("expected one value in %d..%d, got %d" % (start, end, len(stack)))
		return Block(stmts, stack[0]) if stmts else stack[0]

	def effect_range(self, start, end):
		stmts, stack = self.parse(start, end, [])
		if stack:
			raise DecompileError("expected no value in %d..%d, got %d" % (start, end, len(stack)))
		return stmts

	def parse(self, start, end, stack):
		"""The instructions [start, end) (indices): the statements they make and
		the values they leave on the stack."""
		ins = self.instrs
		stmts = []
		stack = list(stack)
		k = start

		def pop():
			if not stack:
				raise DecompileError("the stack is empty at %d" % ins[k].pc)
			return stack.pop()

		def popn(n):
			if len(stack) < n:
				raise DecompileError("the stack is short at %d" % ins[k].pc)
			args = stack[len(stack) - n:] if n else []
			del stack[len(stack) - n:]
			return args

		loop_tops = self.loop_tops()
		while k < end:
			i = ins[k]
			a, b = i.a, i.b
			# a loop with no header branch starts here (loop, repeat)
			if i.pc in loop_tops and not getattr(self, "_in_header", False):
				back = loop_tops[i.pc]
				if back.a == OP_BRANCH and back.index < end:
					body_end = back.index - 1
					if not ins[body_end].simple(POP):
						raise DecompileError("loop body not popped")
					self.loop_exits.append(back.next)
					body = self.value_range(k, body_end)
					self.loop_exits.pop()
					stack.append(Loop(body))
					k = back.index + 1
					continue
				if back.a == OP_BIF and back.index < end and back.b == i.pc and self.is_repeat(back):
					self.loop_exits.append(ins[back.index + 1].next)
					inner, st = self.parse(k, back.index, [])
					self.loop_exits.pop()
					if len(st) != 1:
						raise DecompileError("repeat's condition")
					stack.append(Repeat(inner, st[0]))
					k = back.index + 2
					continue
			if a == 0:
				if b == POP:
					v = pop()
					stmts.append(v)
				elif b == DUP:
					raise DecompileError("dup")
				elif b == RETURN:
					stack.append(Return(pop()))
				elif b == PUSHSELF:
					stack.append(Self())
				elif b == SETLEXSCOPE:
					f = pop()
					if not isinstance(f, Func):
						raise DecompileError("set-lex-scope of something else")
					f.lexical = True
					stack.append(f)
				else:
					raise DecompileError("simple instruction %d on its own" % b)
				k += 1
			elif a == OP_PUSH:
				ref = self.lit(b)
				if self.rom.is_ptr(ref) and self.rom.flags(ref) & 1 and len(self.rom.slots(ref)) >= 5 \
						and self.rom.slots(ref)[0] == 0x32 and not (self.rom.flags(ref) & 2 and False):
					stack.append(Func(Decompiled(self.rom, ref, self).decompile()))
				else:
					stack.append(Lit(ref))
				k += 1
			elif a == OP_PUSHCONST:
				ref = b
				if i.length == 3 and ref & 0x8000:
					ref -= 0x10000
				stack.append(Lit(ref & 0xffffffff, immediate=True))
				k += 1
			elif a == OP_CALL:
				name = pop()
				if not isinstance(name, Lit):
					raise DecompileError("call of a computed name")
				args = popn(b)
				stack.append(Call(self.symbol(name.ref), args))
				k += 1
			elif a == OP_INVOKE:
				fn = pop()
				args = popn(b)
				stack.append(Invoke(fn, args))
				k += 1
			elif a in (OP_SEND, OP_SENDIF):
				msg = pop()
				rcvr = pop()
				args = popn(b)
				stack.append(Send(rcvr, self.symbol(msg.ref), args, a == OP_SENDIF))
				k += 1
			elif a in (OP_RESEND, OP_RESENDIF):
				msg = pop()
				args = popn(b)
				stack.append(Send(None, self.symbol(msg.ref), args, a == OP_RESENDIF))
				k += 1
			elif a == OP_BRANCH:
				target = self.index_of_pc(b)
				if self.loop_exits and b == self.loop_exits[-1] and b > i.pc:
					stack.append(Break(pop()))
					k += 1
					continue
				if target == k + 1 + 0 and False:
					pass
				# a while, for or foreach: its header branch jumps to the test
				back = loop_tops.get(i.next)
				if back is not None and b > i.pc:
					k = self.loop(k, back, stack, stmts)
					continue
				raise DecompileError("an unconditional branch not understood at %d" % i.pc)
			elif a in (OP_BIF, OP_BIT):
				if b <= i.pc:
					raise DecompileError("a backward conditional branch not understood at %d" % i.pc)
				k = self.conditional(k, stack, stmts, end)
			elif a == OP_FINDVAR:
				stack.append(Var(self.symbol(self.lit(b))))
				k += 1
			elif a == OP_GETVAR:
				stack.append(Local(b))
				k += 1
			elif a == OP_MAKEFRAME:
				mapref = pop()
				values = popn(b)
				if not isinstance(mapref, Lit):
					raise DecompileError("make-frame of a computed map")
				tags = [self.symbol(t) for t in self.rom.map_tags(mapref.ref)]
				if len(tags) != b:
					raise DecompileError("make-frame's map")
				stack.append(MakeFrame(tags, values))
				k += 1
			elif a == OP_MAKEARRAY:
				cls = pop()
				if b == 0xffff:
					size = pop()
					stack.append(Call("Array", [size, Lit(2, True)]))
					stack[-1].sized_class = cls
				else:
					stack.append(MakeArray(cls, popn(b)))
				k += 1
			elif a == OP_GETPATH:
				path = pop()
				obj = pop()
				stack.append(Path(obj, path, b))
				k += 1
			elif a == OP_SETPATH:
				value = pop()
				path = pop()
				obj = pop()
				node = Assign(Path(obj, path, 1), value, b == 1)
				if b == 1:
					stack.append(node)
				else:
					stmts.append(node)
				k += 1
			elif a in (OP_SETVAR, OP_FINDSETVAR):
				value = pop()
				target = Local(b) if a == OP_SETVAR else Var(self.symbol(self.lit(b)))
				nxt = ins[k + 1] if k + 1 < end else None
				same = nxt is not None and ((a == OP_SETVAR and nxt.a == OP_GETVAR and nxt.b == b)
											 or (a == OP_FINDSETVAR and nxt.a == OP_FINDVAR and nxt.b == b))
				if same:
					stack.append(Assign(target, value, True))
					k += 2
				else:
					stmts.append(Assign(target, value, False))
					k += 1
			elif a == OP_FREQ:
				if b >= len(FREQ):
					raise DecompileError("freq-func %d" % b)
				name, n = FREQ[b]
				args = popn(n)
				stack.append(Call(name, args, ) if name != "setAref" else Assign(Call("aref", args[:2]), args[2], True))
				k += 1
			elif a == OP_NEWHANDLERS:
				k = self.try_(k, stack, stmts)
			elif a == OP_INCRVAR:
				raise DecompileError("incr-var outside a loop")
			elif a == OP_BILND:
				raise DecompileError("branch-if-loop-not-done outside a for")
			else:
				raise DecompileError("opcode %d" % a)
		return stmts, stack

	# ---- loops

	def loop_tops(self):
		"""{top pc: the backward branch's instruction} of every loop."""
		if not hasattr(self, "_tops"):
			tops = {}
			for i in self.instrs:
				if i.a in (OP_BRANCH, OP_BIF, OP_BIT, OP_BILND) and i.b <= i.pc:
					tops[i.b] = i
			self._tops = tops
		return self._tops

	def is_repeat(self, back):
		# repeat: <stmts> <cond> bif top; push nil
		n = back.index + 1
		return n < len(self.instrs) and self.instrs[n].a == OP_PUSHCONST and self.instrs[n].b == 2

	def loop(self, k, back, stack, stmts):
		"""A loop whose header branch is instruction k: while, for or foreach.
		==> the index to go on from."""
		ins = self.instrs
		head = ins[k]
		top = k + 1
		test = self.index_of_pc(head.b)
		if back.a == OP_BIT:
			# while: branch test; top: <body> pop; test: <cond> bit top; push nil
			if not ins[test - 1].simple(POP):
				raise DecompileError("while's body not popped")
			exit_pc = ins[back.index + 1].next
			self.loop_exits.append(exit_pc)
			body = self.value_range(top, test - 1)
			self.loop_exits.pop()
			cond = self.value_range(test, back.index)
			stack.append(While(cond, body))
			return back.index + 2
		if back.a == OP_BILND:
			# for: ...incr, i on the stack; branch test; top: <body> get-var incr; incr-var i;
			# test: get-var limit; branch-if-loop-not-done top; push nil
			if len(stack) < 2 or not isinstance(stack[-1], Local) or not isinstance(stack[-2], Assign) \
					or not isinstance(stack[-2].target, Local) or len(stmts) < 2:
				raise DecompileError("for's header")
			var = stack[-1].index
			incr = stack[-2].target.index
			by = stack[-2].val
			s_limit, s_var = stmts[-1], stmts[-2]
			if not (isinstance(s_limit, Assign) and isinstance(s_limit.target, Local)
					and isinstance(s_var, Assign) and isinstance(s_var.target, Local) and s_var.target.index == var):
				raise DecompileError("for's start and limit")
			limit = s_limit.target.index
			del stack[-2:]
			del stmts[-2:]
			if not (ins[test].a == OP_GETVAR and ins[test].b == limit and test + 1 == back.index):
				raise DecompileError("for's test")
			if not (ins[test - 1].a == OP_INCRVAR and ins[test - 1].b == var and ins[test - 2].a == OP_GETVAR and ins[test - 2].b == incr):
				raise DecompileError("for's step")
			exit_pc = ins[back.index + 1].next
			self.loop_exits.append(exit_pc)
			body = self.effect_range(top, test - 2)
			self.loop_exits.pop()
			stack.append(For(var, limit, incr, s_var.val, s_limit.val, by, Block(body, None)))
			return back.index + 2
		if back.a == OP_BIF:
			return self.foreach(k, back, stack, stmts)
		raise DecompileError("a loop not understood")

	def foreach(self, k, back, stack, stmts):
		ins = self.instrs
		top = k + 1
		test = self.index_of_pc(ins[k].b)
		# test: get-var iter; iter-done; bif top
		if not (ins[test].a == OP_GETVAR and ins[test + 1].simple(ITERDONE) and test + 2 == back.index):
			raise DecompileError("foreach's test")
		it = ins[test].b
		collect = not (ins[back.index + 1].a == OP_PUSHCONST and ins[back.index + 1].b == 2)
		# the statements before: iter := NewIterator(coll, deeply) [; result := Array(...); index := 0]
		index = result = None
		if collect:
			if len(stmts) < 2:
				raise DecompileError("foreach collect's header")
			s_index, s_result = stmts[-1], stmts[-2]
			if not (isinstance(s_index, Assign) and isinstance(s_result, Assign) and isinstance(s_result.val, Call)):
				raise DecompileError("foreach collect's header")
			index, result = s_index.target.index, s_result.target.index
			arr = s_result.val.args[0]		# aref(iter := NewIterator(...), 5|3)
			if not (isinstance(arr, Call) and arr.name == "aref" and isinstance(arr.args[0], Assign)):
				raise DecompileError("foreach collect's array")
			s_iter = arr.args[0]
			del stmts[-2:]
		else:
			if not stmts or not isinstance(stmts[-1], Assign):
				raise DecompileError("foreach's header")
			s_iter = stmts.pop()
		if not (isinstance(s_iter.target, Local) and s_iter.target.index == it and isinstance(s_iter.val, Call)
				and s_iter.val.name == "NewIterator"):
			raise DecompileError("foreach's iterator")
		coll, deeply = s_iter.val.args
		deeply = isinstance(deeply, Lit) and deeply.ref == 0x1a
		# the body: val := iter[1] [; slot := iter[0]] ... get-var iter; iter-next
		p = top
		def expect(cond, what):
			if not cond:
				raise DecompileError("foreach's " + what)
		expect(ins[p].a == OP_GETVAR and ins[p].b == it and ins[p + 1].a == OP_PUSHCONST and ins[p + 1].b == 4
			   and ins[p + 2].a == OP_FREQ and ins[p + 2].b == 2 and ins[p + 3].a == OP_SETVAR, "value")
		val = ins[p + 3].b
		p += 4
		slot = None
		if ins[p].a == OP_GETVAR and ins[p].b == it and ins[p + 1].a == OP_PUSHCONST and ins[p + 1].b == 0 \
				and ins[p + 2].a == OP_FREQ and ins[p + 2].b == 2 and ins[p + 3].a == OP_SETVAR:
			slot = ins[p + 3].b
			p += 4
		expect(ins[test - 2].a == OP_GETVAR and ins[test - 2].b == it and ins[test - 1].simple(ITERNEXT), "step")
		body_end = test - 2
		exit_after = back.index + 2			# after push nil / the collect's branch
		if collect:
			# result[index] := <body>; pop; push 1; incr-var index; pop; pop
			expect(ins[p].a == OP_GETVAR and ins[p].b == result and ins[p + 1].a == OP_GETVAR and ins[p + 1].b == index, "collection")
			q = body_end - 6
			expect(ins[q].a == OP_FREQ and ins[q].b == 3 and ins[q + 1].simple(POP) and ins[q + 2].a == OP_PUSHCONST
				   and ins[q + 3].a == OP_INCRVAR and ins[q + 4].simple(POP) and ins[q + 5].simple(POP), "collection step")
			exit_pc = ins[back.index + 1].next
			self.loop_exits.append(exit_pc)
			body = self.value_range(p + 2, q)
			self.loop_exits.pop()
			# after: branch end; set-var result; pop; pop; end: get-var result; push nil; set-var result; push nil; set-var iter
			j = back.index + 1
			expect(ins[j].a == OP_BRANCH and ins[j + 1].a == OP_SETVAR and ins[j + 2].simple(POP) and ins[j + 3].simple(POP)
				   and ins[j + 4].a == OP_GETVAR and ins[j + 5].a == OP_PUSHCONST and ins[j + 6].a == OP_SETVAR
				   and ins[j + 7].a == OP_PUSHCONST and ins[j + 8].a == OP_SETVAR, "collection's end")
			after = j + 9
		else:
			exit_pc = ins[back.index + 1].next
			self.loop_exits.append(exit_pc)
			body = Block(self.effect_range(p, body_end), None)
			self.loop_exits.pop()
			j = back.index + 1
			expect(ins[j + 1].a == OP_PUSHCONST and ins[j + 1].b == 2 and ins[j + 2].a == OP_SETVAR and ins[j + 2].b == it, "end")
			after = j + 3
		stack.append(Foreach(slot, val, it, index, result, coll, deeply, body, collect))
		return after

	# ---- if, and, or

	def conditional(self, k, stack, stmts, end):
		ins = self.instrs
		i = ins[k]
		cond = stack.pop()
		target = self.index_of_pc(i.b)
		before = ins[target - 1]
		if i.a == OP_BIF:
			if before.a == OP_BRANCH and before.b > before.pc and not (self.loop_exits and before.b == self.loop_exits[-1]
																	   and not self.else_fits(target, before)):
				els_end = self.index_of_pc(before.b)
				then_stmts, then_stack = self.parse(k + 1, target - 1, [])
				if len(then_stack) == 1:
					then = Block(then_stmts, then_stack[0]) if then_stmts else then_stack[0]
					if els_end == target + 1 and ins[target].a == OP_PUSHCONST and ins[target].b == 2:
						stack.append(If(cond, then, None, True))
					else:
						stack.append(If(cond, then, self.value_range(target, els_end), True))
					return els_end
				if then_stack:
					raise DecompileError("if's then leaves %d" % len(then_stack))
				els = self.effect_range(target, els_end)
				stmts.append(If(cond, Block(then_stmts, None), Block(els, None), False))
				return els_end
			then = self.effect_range(k + 1, target)
			stmts.append(If(cond, Block(then, None), None, False))
			return target
		# or
		if before.a == OP_BRANCH and before.b == ins[target].next and ins[target].a == OP_PUSHCONST and ins[target].b == 0x1a:
			other = self.value_range(k + 1, target - 1)
			stack.append(Or(cond, other, True))
			return target + 1
		other = self.effect_range(k + 1, target)
		stmts.append(Or(cond, Block(other, None), False))
		return target

	def else_fits(self, target, before):
		"""Whether the instructions from target to before's branch target make
		a sensible else (for a branch that also goes to the loop's exit)."""
		try:
			self.parse(target, self.index_of_pc(before.b), [])
			return True
		except DecompileError:
			return False

	# ---- try

	def try_(self, k, stack, stmts):
		ins = self.instrs
		n = ins[k].b
		pairs = []
		for _ in range(n):
			pc = stack.pop()
			sym = stack.pop()
			if not (isinstance(pc, Lit) and pc.immediate and isinstance(sym, Lit)):
				raise DecompileError("new-handlers' pairs")
			pairs.append((self.symbol(sym.ref), pc.ref >> 2))
		pairs.reverse()
		first = self.index_of_pc(pairs[0][1])
		# <body>; pop-handlers; branch end; handlers...; pop-handlers; end:
		if not (ins[first - 1].a == OP_BRANCH and ins[first - 2].simple(POPHANDLERS)):
			raise DecompileError("try's body end")
		end = self.index_of_pc(ins[first - 1].b)
		if not ins[end - 1].simple(POPHANDLERS):
			raise DecompileError("try's end")
		body_stmts, body_stack = self.parse(k + 1, first - 2, [])
		value = len(body_stack) == 1
		body = Block(body_stmts, body_stack[0] if value else None)
		handlers = []
		for h, (sym, pc) in enumerate(pairs):
			start = self.index_of_pc(pc)
			if h + 1 < len(pairs):
				stop = self.index_of_pc(pairs[h + 1][1]) - 1		# its branch to the end
				if ins[stop].a != OP_BRANCH:
					raise DecompileError("a handler's end")
			else:
				stop = end - 1
			hs, hst = self.parse(start, stop, [])
			handlers.append((sym, Block(hs, hst[0] if value and hst else None)))
		node = Try(body, handlers, value)
		if value:
			stack.append(node)
		else:
			stmts.append(node)
		return end


# ------------------------------------------------------------------------------
#	writing the source
# ------------------------------------------------------------------------------

def ident(name):
	if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) and name.lower() not in RESERVED:
		return name
	return "|" + name.replace("\\", "\\\\").replace("|", "\\|") + "|"


def string_literal(text):
	out = []
	for ch in text:
		c = ord(ch)
		if ch == '"':
			out.append('\\"')
		elif ch == "\\":
			out.append("\\\\")
		elif ch == "\r":
			out.append("\\n")
		elif ch == "\t":
			out.append("\\t")
		elif 0x20 <= c < 0x7f:
			out.append(ch)
		else:
			out.append("\\u%04X\\u" % c)
	return '"' + "".join(out) + '"'


def real_literal(v):
	text = repr(v)
	if "inf" in text or "nan" in text:
		raise DecompileError("an unwritable real")
	if "e" in text and "." not in text.split("e")[0]:
		m, e = text.split("e")
		text = m + ".0e" + e
	elif "." not in text and "e" not in text:
		text += ".0"
	if text.startswith("-"):
		return "(" + text + ")"
	return text


def uses_environment(node):
	"""Whether a function's code finds a variable by name, uses self or
	inherited, or has a function that does - what would give it an argFrame
	if the compiler made it inside another."""
	if isinstance(node, (Var, Self)):
		return True
	if isinstance(node, Send) and node.rcvr is None:
		return True
	if isinstance(node, Assign) and isinstance(node.target, Var):
		return True
	if isinstance(node, Func):
		return uses_environment(node.fn.body)
	return any(isinstance(c, Node) and uses_environment(c) for c in children(node))


class Writer:
	def __init__(self, rom):
		self.rom = rom
		self.constants = []			# (name, source) of the functions made global constants

	# ---- constants

	def immediate(self, ref):
		if ref & 3 == 0:
			v = ref if ref < 0x80000000 else ref - 0x100000000
			return str(v >> 2)
		if ref == 2:
			return "nil"
		if ref == 0x1a:
			return "true"
		if ref & 0xf == 6:
			c = ref >> 4
			if c == 0x0d:
				return "$\\n"
			if c == 9:
				return "$\\t"
			if 0x20 <= c < 0x7f and chr(c) not in "\\":
				return "$" + chr(c)
			if c < 0x100:
				return "$\\%02X" % c
			return "$\\u%04X" % c
		if ref & 3 == 3:
			return "@%d" % (ref >> 2)
		raise DecompileError("an immediate %#x" % ref)

	def constant(self, ref, quoted=False):
		"""A literal: source that makes it (quoted: inside '[...] or '{...})."""
		rom = self.rom
		if not rom.is_ptr(ref):
			if ref & 3 == 3 and not quoted:
				return self.immediate(ref)
			return self.immediate(ref)
		name = rom.symname(ref)
		if name is not None:
			return ident(name) if quoted else "'" + ident(name)
		f = rom.flags(ref)
		cls = rom.cls(ref)
		if f & 3 == 0:
			cname = rom.symname(cls) if rom.is_ptr(cls) else None
			if cname == "string":
				text = rom.data(ref).decode("utf-16-be")
				if not text.endswith("\0"):
					raise DecompileError("a string with no terminator")
				return string_literal(text[:-1])
			if cname == "real":
				return real_literal(struct.unpack(">d", rom.data(ref)[:8])[0])
			raise DecompileError("a binary of class %s" % (cname or rom.describe(cls)))
		if f & 3 == 3:
			if len(rom.slots(ref)) >= 5 and rom.slots(ref)[0] == 0x32:
				raise DecompileError("a function inside a literal")
			parts = []
			for tag, value in rom.frame_slots(ref):
				parts.append("%s: %s" % (ident(tag), self.constant(value, True)))
			text = "{" + ", ".join(parts) + "}"
			return text if quoted else "'" + text
		if f & 1:
			if len(rom.slots(ref)) >= 5 and rom.slots(ref)[0] == 0x32:
				raise DecompileError("a function inside a literal")
			cname = rom.symname(cls) if rom.is_ptr(cls) else None
			if cname is None:
				raise DecompileError("an array of class %s" % rom.describe(cls))
			if cname == "pathExpr":
				raise DecompileError("a path expression as a literal")
			items = ", ".join(self.constant(v, True) for v in rom.slots(ref))
			text = "[" + ("%s: " % ident(cname) if cname != "array" else "") + items + "]"
			return text if quoted else "'" + text
		raise DecompileError("a literal not understood")

	# ---- expressions

	def expr(self, fn, node, indent=0):
		w = lambda n: self.expr(fn, n, indent)
		if isinstance(node, Lit):
			if node.immediate:
				return self.immediate(node.ref)
			return self.constant(node.ref)
		if isinstance(node, Local):
			return ident(fn.local_name(node.index))
		if isinstance(node, Var):
			return ident(node.name)
		if isinstance(node, Self):
			return "self"
		if isinstance(node, Assign):
			return "(%s := %s)" % (self.target(fn, node.target, indent), w(node.val))
		if isinstance(node, Call):
			if getattr(node, "sized_class", None) is not None:
				raise DecompileError("a sized array")
			if node.name in INFIX and len(node.args) == 2:
				return "(%s %s %s)" % (w(node.args[0]), node.name, w(node.args[1]))
			if node.name in NAMED_INFIX and len(node.args) == 2:
				return "(%s %s %s)" % (w(node.args[0]), NAMED_INFIX[node.name], w(node.args[1]))
			if node.name == "aref" and len(node.args) == 2:
				return "%s[%s]" % (self.primary(fn, node.args[0], indent), w(node.args[1]))
			if node.name == "not" and len(node.args) == 1:
				return "(not %s)" % w(node.args[0])
			if node.name == "negate" and len(node.args) == 1:
				if isinstance(node.args[0], Lit):
					raise DecompileError("a negated constant")
				return "(-%s)" % self.primary(fn, node.args[0], indent)
			if node.name.lower() == "hasvar" and len(node.args) == 1 and isinstance(node.args[0], Lit) \
					and self.rom.symname(node.args[0].ref) is not None:
				return "(%s exists)" % ident(self.rom.symname(node.args[0].ref))
			if node.name.lower() == "hasvariable" and len(node.args) == 2 and isinstance(node.args[1], Lit) \
					and self.rom.symname(node.args[1].ref) is not None:
				return "(%s:%s exists)" % (self.primary(fn, node.args[0], indent), ident(self.rom.symname(node.args[1].ref)))
			if node.name == "Stringer" and len(node.args) == 1 and isinstance(node.args[0], MakeArray) \
					and self.is_array_class(node.args[0].cls) and node.args[0].elems:
				return self.stringer(fn, node.args[0].elems, indent)
			return "%s(%s)" % (ident(node.name), ", ".join(w(x) for x in node.args))
		if isinstance(node, Invoke):
			return "call %s with (%s)" % (self.primary(fn, node.fn, indent), ", ".join(w(x) for x in node.args))
		if isinstance(node, Send):
			rcvr = "inherited" if node.rcvr is None else self.primary(fn, node.rcvr, indent)
			return "%s:%s%s(%s)" % (rcvr, "?" if node.ifdef else "", ident(node.msg), ", ".join(w(x) for x in node.args))
		if isinstance(node, Path):
			return self.path(fn, node, indent)
		if isinstance(node, MakeArray):
			if not isinstance(node.cls, Lit) or self.rom.symname(node.cls.ref) is None:
				raise DecompileError("an array's class computed")
			cname = self.rom.symname(node.cls.ref)
			items = ", ".join(w(x) for x in node.elems)
			return "[" + ("%s: " % ident(cname) if cname != "array" else "") + items + "]"
		if isinstance(node, MakeFrame):
			return "{" + ", ".join("%s: %s" % (ident(t), w(v)) for t, v in zip(node.tags, node.values)) + "}"
		if isinstance(node, Func):
			if not getattr(node, "lexical", False) and uses_environment(node.fn.body):
				# compiled on its own (a constant of the NTK's): a global constant here
				name = "kFunction_%x" % node.fn.ref
				if name not in dict(self.constants):
					self.constants.append((name, self.function(node.fn, 0)))
				return name
			return self.function(node.fn, indent)
		if isinstance(node, If):
			text = "if %s then %s" % (w(node.cond), self.stmt_expr(fn, node.then, indent + 1, node.value))
			if node.els is not None:
				text += " else %s" % self.stmt_expr(fn, node.els, indent + 1, node.value)
			return "(" + text + ")" if node.value else text
		if isinstance(node, Or):
			return "(%s or %s)" % (w(node.a), self.stmt_expr(fn, node.b, indent + 1, node.value))
		if isinstance(node, Block):
			return self.block(fn, node, indent)
		if isinstance(node, While):
			return "(while %s do %s)" % (w(node.cond), self.stmt_expr(fn, node.body, indent + 1, True))
		if isinstance(node, Loop):
			return "(loop %s)" % self.stmt_expr(fn, node.body, indent + 1, True)
		if isinstance(node, Repeat):
			body = "; ".join(self.stmt(fn, s, indent + 1) for s in node.stmts)
			return "(repeat %s until %s)" % (body, w(node.cond))
		if isinstance(node, For):
			by = "" if isinstance(node.by, Lit) and node.by.immediate and node.by.ref == 4 else " by %s" % w(node.by)
			return "(for %s := %s to %s%s do %s)" % (ident(fn.local_name(node.var)), w(node.start), w(node.stop), by,
													self.block(fn, node.body, indent + 1))
		if isinstance(node, Foreach):
			names = ident(fn.local_name(node.val))
			if node.slot is not None:
				names = ident(fn.local_name(node.slot)) + ", " + names
			verb = "collect" if node.collect else "do"
			body = w(node.body) if node.collect else self.block(fn, node.body, indent + 1)
			return "(foreach %s%s in %s %s %s)" % (names, " deeply" if node.deeply else "", w(node.coll), verb, body)
		if isinstance(node, Try):
			parts = ["try %s" % self.stmt_expr(fn, node.body, indent + 1, node.value)]
			for sym, h in node.handlers:
				parts.append("onexception %s do %s" % (ident(sym), self.stmt_expr(fn, h, indent + 1, node.value)))
			text = " ".join(parts)
			return "(" + text + ")" if node.value else text
		if isinstance(node, Break):
			if isinstance(node.val, Lit) and node.val.immediate and node.val.ref == 2:
				return "break"
			return "break %s" % w(node.val)
		if isinstance(node, Return):
			return "return %s" % w(node.val)
		if isinstance(node, LocalDecl):
			return "local " + ", ".join(ident(n) for n in node.names)
		raise DecompileError("cannot write a %s" % type(node).__name__)

	def is_array_class(self, cls):
		return isinstance(cls, Lit) and self.rom.symname(cls.ref) == "array"

	def stringer(self, fn, elems, indent):
		# a & b & c; a " " between two parts is &&
		out = [self.expr(fn, elems[0], indent)]
		k = 1
		while k < len(elems):
			e = elems[k]
			if isinstance(e, Lit) and not e.immediate and self.rom.is_ptr(e.ref) and k + 1 < len(elems) \
					and self.constant(e.ref) == '" "':
				out.append("&& " + self.expr(fn, elems[k + 1], indent))
				k += 2
			else:
				out.append("& " + self.expr(fn, e, indent))
				k += 1
		return "(" + " ".join(out) + ")"

	def primary(self, fn, node, indent):
		text = self.expr(fn, node, indent)
		if isinstance(node, (Lit, Local, Var, Self, Path, Send)) or text.startswith("("):
			return text
		if isinstance(node, Call) and not text.startswith("call"):
			return text
		return "(" + text + ")"

	def path(self, fn, node, indent):
		obj = self.primary(fn, node.obj, indent)
		e = node.elem
		if isinstance(e, Lit) and not e.immediate:
			name = self.rom.symname(e.ref)
			if name is not None:
				return "%s.%s" % (obj, ident(name))
			if self.rom.is_ptr(e.ref) and self.rom.flags(e.ref) & 1 and self.rom.symname(self.rom.cls(e.ref)) == "pathExpr":
				names = [self.rom.symname(s) for s in self.rom.slots(e.ref)]
				if None in names:
					raise DecompileError("a path expression of other things")
				return obj + "".join("." + ident(n) for n in names)
		return "%s.(%s)" % (obj, self.expr(fn, e, indent))

	def target(self, fn, node, indent):
		if isinstance(node, Local):
			return ident(fn.local_name(node.index))
		if isinstance(node, Var):
			return ident(node.name)
		if isinstance(node, Path):
			return self.path(fn, node, indent)
		if isinstance(node, Call) and node.name == "aref":
			return "%s[%s]" % (self.primary(fn, node.args[0], indent), self.expr(fn, node.args[1], indent))
		raise DecompileError("an assignment to %s" % type(node).__name__)

	# ---- statements

	def stmt(self, fn, node, indent):
		if isinstance(node, Assign):
			return "%s := %s" % (self.target(fn, node.target, indent), self.expr(fn, node.val, indent))
		if isinstance(node, If) and not node.value:
			text = "if %s then %s" % (self.expr(fn, node.cond, indent), self.stmt_expr(fn, node.then, indent + 1, False))
			if node.els is not None:
				empty = isinstance(node.els, Block) and not node.els.stmts and node.els.final is None
				text += " else %s" % ("nil" if empty else self.stmt_expr(fn, node.els, indent + 1, False))
			return text
		if isinstance(node, Or) and not node.value:
			return "%s or %s" % (self.expr(fn, node.a, indent), self.stmt_expr(fn, node.b, indent + 1, False))
		return self.expr(fn, node, indent)

	def stmt_expr(self, fn, node, indent, value):
		"""A sub-statement (then, else, a loop's body): an expression, or a block."""
		if isinstance(node, Block):
			return self.block(fn, node, indent)
		return self.expr(fn, node, indent) if value else self.stmt(fn, node, indent)

	def block(self, fn, node, indent):
		pad = "\t" * indent
		lines = [self.stmt(fn, s, indent + 1) for s in node.stmts]
		if node.final is not None:
			lines.append(self.expr(fn, node.final, indent + 1))
		if not lines:
			return "begin end"
		inner = (";\n" + pad + "\t").join(lines)
		return "begin\n%s\t%s\n%send" % (pad, inner, pad)

	def function(self, fn, indent=0):
		args = ", ".join(ident(fn.local_name(3 + i)) for i in range(fn.num_args))
		body = fn.body
		decls = declarations(fn)
		if decls:
			body = insert_declarations(body, decls)
		if not isinstance(body, Block):
			body = Block([], body)
		return "func(%s) %s" % (args, self.block(fn, body, indent))


# ------------------------------------------------------------------------------
#	the locals' declarations
# ------------------------------------------------------------------------------

def loop_names(node):
	"""The stack indices a loop declares, in order (for: var, limit, incr;
	foreach: slot, value, iter [, index, result])."""
	if isinstance(node, For):
		return [node.var, node.limit, node.incr]
	if isinstance(node, Foreach):
		names = ([node.slot] if node.slot is not None else []) + [node.val, node.iter]
		if node.collect:
			names += [node.index, node.result]
		return names
	return []


def children(node):
	"""A node's children in the order the compiler's declaration walk visits them."""
	if isinstance(node, Block):
		return list(node.stmts) + ([node.final] if node.final is not None else [])
	if isinstance(node, (Call,)):
		return list(node.args)
	if isinstance(node, Invoke):
		return list(node.args) + [node.fn]
	if isinstance(node, Send):
		return list(node.args) + ([node.rcvr] if node.rcvr is not None else [])
	if isinstance(node, Assign):
		return [node.target, node.val]
	if isinstance(node, Path):
		return [node.obj, node.elem]
	if isinstance(node, MakeArray):
		return list(node.elems)
	if isinstance(node, MakeFrame):
		return list(node.values)
	if isinstance(node, If):
		return [node.cond, node.then] + ([node.els] if node.els is not None else [])
	if isinstance(node, Or):
		return [node.a, node.b]
	if isinstance(node, While):
		return [node.cond, node.body]
	if isinstance(node, Loop):
		return [node.body]
	if isinstance(node, Repeat):
		return list(node.stmts) + [node.cond]
	if isinstance(node, For):
		return [node.start, node.stop, node.by, node.body]
	if isinstance(node, Foreach):
		return [node.coll, node.body]
	if isinstance(node, Try):
		return [node.body] + [h for _, h in node.handlers]
	if isinstance(node, (Break, Return)):
		return [node.val]
	return []


def declarations(fn):
	"""(stack local index, before which loop node or None) for each local that
	must be declared with `local` so that the numbering comes out as the ROM's."""
	first = 3 + fn.num_args
	used = set()

	def collect(n):
		if isinstance(n, Local) and n.index >= first:
			used.add(n.index)
		for l in loop_names(n):
			if l is not None and l >= first:
				used.add(l)
		for c in children(n):
			if isinstance(c, Node):
				collect(c)
	collect(fn.body)
	for i in range(first, first + fn.num_locals):
		used.add(i)
	declared = []
	wanted = []			# (index, before)
	expected = [first]

	def walk(n):
		names = loop_names(n)
		new = [x for x in names if x not in declared]
		if new:
			while expected[0] < min(new):
				if expected[0] not in declared:
					wanted.append((expected[0], n))
					declared.append(expected[0])
				expected[0] += 1
			for x in new:
				declared.append(x)
			expected[0] = max(expected[0], max(new) + 1)
		for c in children(n):
			if isinstance(c, Node):
				walk(c)
	walk(fn.body)
	for i in sorted(used):
		if i not in declared:
			wanted.append((i, None))
			declared.append(i)
	# the closed-over locals, in the argFrame's order, first
	closed = [nm for nm in fn.frame_names if nm not in fn.arg_names.values()]
	return [(nm, "closed") for nm in closed] + wanted


def insert_declarations(body, decls):
	"""The body with `local` statements put in: the closed-over ones and the
	ones that come after every loop at the start (their order is theirs); each
	that must come before a loop just before the statement holding it."""
	if not isinstance(body, Block):
		body = Block([], body)
	front = [d for d, where in decls if where in ("closed", None)]
	before = [(d, where) for d, where in decls if where not in ("closed", None)]
	stmts = list(body.stmts)
	final = body.final
	names_front = []
	closed = [d for d, where in decls if where == "closed"]
	if closed:
		stmts.insert(0, LocalDecl(closed))
	for d, where in before:
		if not place_before(Block(stmts, final), where, d):
			raise DecompileError("cannot place a local declaration")
		# (place_before changes the statement lists in place)
	late = [d for d, where in decls if where is None]
	if late:
		# (at the start when no loop declares a local, else after everything)
		if before or any(loop_names(n) for n in walk_all(Block(stmts, final))):
			stmts.append(LocalDecl(late))
		else:
			stmts.insert(1 if closed else 0, LocalDecl(late))
	body = Block(stmts, final)
	body._late = late
	return body


def walk_all(node):
	yield node
	for c in children(node):
		if isinstance(c, Node):
			yield from walk_all(c)


def place_before(block, loop, index):
	"""Insert `local <index>` into the statement list holding the statement
	that contains loop, before it."""
	def contains(n, target):
		if n is target:
			return True
		return any(isinstance(c, Node) and contains(c, target) for c in children(n))

	def visit(n):
		if isinstance(n, Block):
			for k, s in enumerate(n.stmts):
				if contains(s, loop):
					if visit(s):
						return True
					n.stmts.insert(k, LocalDecl([index]))
					return True
			if n.final is not None and contains(n.final, loop):
				if visit(n.final):
					return True
				n.stmts.append(LocalDecl([index]))
				return True
			return False
		for c in children(n):
			if isinstance(c, Node) and contains(c, loop):
				return visit(c)
		return False
	return visit(block)


# the LocalDecl names are stack indices (or names, for the closed ones)
_orig_expr = Writer.expr


def _expr(self, fn, node, indent=0):
	if isinstance(node, LocalDecl):
		return "local " + ", ".join(ident(n if isinstance(n, str) else fn.local_name(n)) for n in node.names)
	return _orig_expr(self, fn, node, indent)


Writer.expr = _expr


# ------------------------------------------------------------------------------
#	the ROM's functions
# ------------------------------------------------------------------------------

def rom_functions(rom):
	"""Every 2.x function in the object area that is not a literal of another
	(those are decompiled with the function they are in)."""
	fns = []
	nested = set()
	for ref in nf.objects(rom):
		if rom.flags(ref) & 1 and rom.size(ref) >= 32:
			s = rom.slots(ref)
			if s[0] == 0x32 and rom.is_ptr(s[1]):
				fns.append(ref)
				if rom.is_ptr(s[2]):
					for lit in rom.slots(s[2]):
						nested.add(lit)
	return [f for f in fns if f not in nested]


def decompile_source(rom, ref, with_constants=False):
	fn = Decompiled(rom, ref).decompile()
	w = Writer(rom)
	src = w.function(fn)
	if with_constants:
		return src, w.constants
	if w.constants:
		return "".join("constant %s := %s;\n" % c for c in w.constants) + src
	return src


def record(rom, ref):
	src, constants = decompile_source(rom, ref, True)
	text = "@@ %#x\n" % ref
	if constants:
		text += "".join("@@const %s\n%s\n" % c for c in constants) + "@@main\n"
	return text + src + "\n@@end\n"


def write_records(rom, refs, out):
	failed = collections.Counter()
	written = 0
	for ref in refs:
		try:
			text = record(rom, ref)
		except (DecompileError, IndexError, KeyError, AttributeError, TypeError, ValueError) as e:
			reason = re.sub(r"\d+", "N", str(e)) if isinstance(e, DecompileError) else type(e).__name__ + ": " + re.sub(r"\d+", "N", str(e))
			failed[reason] += 1
			out.write("## %#x decompile %s\n" % (ref, e))
			continue
		out.write(text)
		written += 1
	return written, failed


def disassemble_bytes(code):
	"""Instructions as text, literals by index only."""
	out = []
	for i in decode(code):
		if i.a == 0:
			text = nf.SIMPLE_OPS[i.b] if i.b < 8 else "simple %d" % i.b
		else:
			text = "%s %d" % (nf.OPS.get(i.a, "op%d" % i.a), i.b)
			if i.a == OP_FREQ and i.b < len(FREQ):
				text += " (%s)" % FREQ[i.b][0]
		out.append("%4d: %s" % (i.pc, text))
	return out


def compare(rom, ref, exe):
	"""The function's source, then the ROM's instructions and ours side by side."""
	src = decompile_source(rom, ref)
	print(src)
	text = record(rom, ref)
	tmp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tmp")
	records = os.path.join(tmp, "nsdecompile-one.txt")
	dump = os.path.join(tmp, "nsdecompile-dump.txt")
	with open(records, "w", encoding="utf-8") as f:
		f.write(text)
	if os.path.exists(dump):
		os.remove(dump)
	env = dict(os.environ, NSROUNDTRIP_DUMP=dump)
	r = subprocess.run([exe, "--roundtrip", records, "-"], capture_output=True, text=True, env=env)
	print(r.stdout.strip())
	if not os.path.exists(dump):
		return
	lines = open(dump).read().split("\n")
	for k in range(0, len(lines) - 2, 3):
		print("-- %s: ours | the ROM's" % lines[k])
		ours = disassemble_bytes(bytes.fromhex(lines[k + 1][5:]))
		theirs = disassemble_bytes(bytes.fromhex(lines[k + 2][5:]))
		for n in range(max(len(ours), len(theirs))):
			x = ours[n] if n < len(ours) else ""
			y = theirs[n] if n < len(theirs) else ""
			print("%s %-40s | %s" % ("  " if x == y else "**", x, y))


def newtonscript_path(given):
	exe = given or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "build", "host", "host", "newtonscript")
	exe = os.path.abspath(exe)
	if not os.path.exists(exe) and os.path.exists(exe + ".exe"):
		exe += ".exe"
	return exe


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("build_dir")
	ap.add_argument("names", nargs="*")
	ap.add_argument("--all", action="store_true")
	ap.add_argument("--sample", type=int, default=0, help="only this many, chosen evenly")
	ap.add_argument("-o", "--output")
	ap.add_argument("--roundtrip", action="store_true", help="decompile, compile with newtonscript, compare, report")
	ap.add_argument("--newtonscript", default=None, help="the host's newtonscript (default: build/host/host/newtonscript)")
	ap.add_argument("--rom", default=None, help="the ROM image for newtonscript")
	ap.add_argument("--min-percent", type=float, default=None, help="fail when fewer round-trip (for ctest)")
	ap.add_argument("--compare", default=None, help="a function's source and both codes side by side")
	a = ap.parse_args(argv)
	rom = nf.ROM(a.build_dir)
	if a.compare:
		compare(rom, int(a.compare, 16), newtonscript_path(a.newtonscript))
		return 0
	if a.names and not a.all and not a.roundtrip:
		w = Writer(rom)
		for name in a.names:
			ref = nf.resolve(rom, name)
			if ref is None:
				ref = rom.resolve_magic(int(name, 16))
			print("// %s (%#x)" % (name, ref))
			print(decompile_source(rom, ref))
		return 0
	refs = rom_functions(rom)
	if a.sample:
		step = max(1, len(refs) // a.sample)
		refs = refs[::step][:a.sample]
	if not a.roundtrip:
		out = open(a.output, "w", encoding="utf-8") if a.output else sys.stdout
		written, failed = write_records(rom, refs, out)
		sys.stderr.write("%d of %d functions decompiled\n" % (written, len(refs)))
		for reason, n in failed.most_common(20):
			sys.stderr.write("  %5d  %s\n" % (n, reason))
		return 0
	# the round trip
	tmp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tmp")
	os.makedirs(tmp, exist_ok=True)
	# (the last run's files are left for looking at; --sample runs, as ctest's, have their own)
	suffix = "-sample" if a.sample else ""
	records = os.path.join(tmp, "nsdecompile-records%s.txt" % suffix)
	results = os.path.join(tmp, "nsdecompile-results%s.txt" % suffix)
	with open(records, "w", encoding="utf-8") as out:
		written, failed = write_records(rom, refs, out)
	exe = newtonscript_path(a.newtonscript)
	cmd = [exe] + (["--rom", a.rom] if a.rom else []) + ["--roundtrip", records, results]
	subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
	causes = collections.Counter()
	ok = 0
	for line in open(results, encoding="utf-8", errors="replace"):
		parts = line.split()
		if len(parts) >= 2 and parts[1] == "OK":
			ok += 1
		elif len(parts) >= 3:
			detail = re.sub(r"0x[0-9a-f]+|\d+", "N", " ".join(parts[3:]))[:90]
			causes["%s: %s" % (parts[2], detail)] += 1
	total = len(refs)
	print("%d of %d functions decompiled; %d round-trip (%.1f%%)" % (written, total, ok, 100.0 * ok / total if total else 0))
	print("decompiler failures:")
	for reason, n in failed.most_common(15):
		print("  %5d  %s" % (n, reason))
	print("round-trip failures:")
	for reason, n in causes.most_common(15):
		print("  %5d  %s" % (n, reason))
	if a.min_percent is not None and total and 100.0 * ok / total < a.min_percent:
		print("nsdecompile: fewer than %.1f%% round-trip" % a.min_percent)
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
