# The NewtonScript decompiler and its round trip

This is the first step of the long-term track of booting with no ROM image
(`docs/next-steps.md`). The ROM's NewtonScript has to become source that
can be read, edited and compiled back into the same functions.

- **Decompiler:** `tools/newton-rom/analysis/nsdecompile.py`.
- **Round-trip harness:** `newtonscript --roundtrip`, in
  `src/host/NSRoundTrip.cpp`.
- **ctest:** `host.NSDecompileRoundTrip` runs the round trip over a sample
  of 600 functions and fails below 96%.

```
python tools/newton-rom/analysis/nsdecompile.py build/MP2x00US Max 0x3c56f9     # the source of functions
python tools/newton-rom/analysis/nsdecompile.py build/MP2x00US --roundtrip      # every function, compiled back, compared
python tools/newton-rom/analysis/nsdecompile.py build/MP2x00US --compare 0x41a4f5   # one function: source and both codes
```

## How it reads the code

The ROM's functions were compiled by the same compiler the ROM carries,
which the reconstruction has as `frames/Compiler.cpp`. Its
`TCompiler::WalkForCode` turns each construct into a fixed shape of
bytecode, so the decompiler reads those shapes back:

- **Expressions:** a symbolic stack.
- **Conditionals:** each forward branch is matched against the shapes of
  `if` (as a value and as a statement), `and` and `or`.
- **Loops:** each backward branch marks one, and the loop is recognised
  by the branch it ends with:
  - `branch`: `loop`;
  - `branch-if-false` followed by `push nil`: `repeat`;
  - `branch-if-true`: `while`;
  - `branch-if-loop-not-done`: `for`;
  - the iterator test: `foreach`, with `do` and `collect`.
- **Inside a loop:** a branch to the loop's exit is `break`.
- **Exception handling:** `new-handlers` with its pairs is `try`, one
  `onexception` for each pair.

Some shapes come out of more than one construct. For example, `if a then
b` used as a value compiles exactly like `a and b`. Either is written:
the round trip only asks that the same code comes out.

## What compiling loses, and what the decompiler makes up

- **Names of stack variables:** arguments and locals kept on the stack
  have none in the code block. They are written `a1`, `a2`, … and `l1`,
  `l2`, … by stack index. A variable that an inner function closes over
  lives in the argFrame and keeps its name, and so does an argument the
  function copies there at entry (`CopyClosedArgs`).
- **Where `local` was written:** the compiler numbers stack locals in the
  order their declarations come, whether a `local`, a `for` (the variable,
  `|limit` and `|incr`) or a `foreach` (its variables and `|iter`, plus
  `|index` and `|result` for `collect`). The decompiler walks its tree as
  `WalkForDeclarations` does and puts each `local` where it gives the
  ROM's numbering.
- **Names, when the table of variables is sorted:** the compiler keeps a
  function's variables in a frame (`fVarLocs`), and `AddSlot` sorts a
  frame's map once it passes 20 tags (`ConvertToSortedMap`). A function
  with more than 20 arguments and locals therefore numbers its stack
  locals in *sorted* order: by symbol hash, then by name. The hash
  (`SymbolHashFunction`) is the sum of the upper-cased characters times
  0x9E3779B9, in 32 bits, so it depends on that sum alone. For such a
  function the decompiler chooses each local's sum so that the hashes come
  in the ROM's order, the loops' hidden locals (`name|limit`,
  `k` + `v` + `|iter`, ...) included, and makes a name adding up to it
  (`v19_yyx`). This is a depth-first search, each name aiming at an even
  share of the hash range. A hidden local may sort before the names it is
  made of, so it is checked when the last of them is chosen. Every `local`
  goes at the start, since the order of declarations no longer matters.
- **Constants:** these are inlined by the compiler, and come back as the
  values they stood for.

## Functions compiled on their own

The NTK compiled each function of a project at the top level. There, a
function that uses `self` or `inherited` needs no argFrame, and its
argFrame's `_parent` and `_implementor` are left nil. The ROM's own
`Compile` always makes a function of no arguments around its input, so
any `func` in the source is nested. For this the host has
`CompileFunctionString`, a host addition in `frames/Compiler.cpp`: the one
`func` of the source compiled as a function of its own.

Some functions push another function as a literal, without closing over
it (no `set-lex-scope`). These came from a constant the NTK evaluated when
the project was built. Written inline, such a function would be closed
over whenever the compiler gave it an argFrame, and a nested function gets
one for any free name - `self`, a global, even a global constant -
because `ClosureWalker` notes every name that is not a variable as the
receiver's (the ROM's does the same). The NTK's own compiler knew a
constant for what it was: a nested function that uses one needed no
receiver on its account, which `gCompilerNTKConstants` also gives. Such a function is written as its own definition,
a global constant `kFunction_<address>` compiled first. The main function
refers to it by name, which pushes the same literal:

```
@@ 0x41a4f5
@@const kFunction_41979d
func() begin ... end
@@main
func(a1) begin ... a1.Close := kFunction_41979d; ... end
@@end
```

### The NTK's constants

Three things in the ROM's code come from constants the NTK evaluated when
the project was built. The ROM's own compiler, which the reconstruction
has, does not make them from source.

- **A literal pushed from more than one place:** the same literal slot is
  pushed at each place. A quoted literal written twice makes two slots.
  The decompiler writes the object once, as a global constant
  (`kLiteral_<address>`, a `@@const` of the value). The compiler pushes a
  constant's value, the same object each time, as one literal.
- **The same object in two slots:** the ROM's build shared equal objects
  (two `"Paused..."` strings are one object with two literals). This is
  written quoted at each place.
- **A magic pointer pushed as a literal:** the NTK pushed a global constant
  whose value is a ROM object as a literal, where `@n` in source, and every
  constant in the ROM's compiler, is `push-constant`. These are written as
  a constant `kROM_<n>` bound to `@n`. `gCompilerNTKConstants`, a host flag
  in `frames/Compiler.cpp` that the round trip sets, makes the compiler
  push such a constant as the NTK did, one literal for each constant's
  name: one function has two literals of `@256`, two constants of the
  same value. Nothing the ROM does sets it.

## The round trip

`newtonscript --roundtrip records results` does two things for each
record:

1. It compiles each `@@const` and binds it as a global constant, then
   compiles the main function, all with `dbgNoVarNames` set, as the NTK
   built the ROM.
2. It compares the result with the ROM's function:
   - the instructions, byte for byte;
   - the literals: symbols by name (case included), strings and reals by
     their bytes, arrays and frames slot by slot, nested functions
     recursively;
   - the argFrame;
   - `numArgs`.

A failure is reported by category (compile, instructions, literals,
argFrame, numArgs, shape) and where it happened. `NSROUNDTRIP_DUMP=file`
also writes both instruction strings, which is what `--compare` shows side
by side.

### Results

| Round | Decompiled | Round-trip | What changed |
|---|---|---|---|
| 1 | 5398 of 5507 | 5122 (93.0%) | the first version |
| 2 | 5398 of 5507 | 5200 (94.4%) | functions compiled at the top level; pushed literal functions as constants; `foreach ... deeply in` |
| 3 | 5398 of 5507 | 5372 (97.5%) | locals a loop reuses declared first; the late locals put in the last loop's body; the NTK's constants (below) |
| 4 | 5447 of 5507 | 5418 (98.4%) | no assignment joined to a read across a branch target (a loop's top); quoted paths `'a.b`, `'[pathExpr: x]`; reals of class `'Real` |
| 5 | 5447 of 5507 | 5434 (98.7%) | names chosen in hash order for functions whose table of variables is sorted (more than 20) |
| 6 | 5447 of 5507 | 5440 (98.8%) | every function pushed without `set-lex-scope` made a constant; `a.b.(c) exists` (its get-paths are all 0) |
| 7 | 5447 of 5507 | 5442 (98.8%) | repeated literals told apart by slot, not object; an NTK magic-pointer constant a literal per name |
| 8 | 5447 of 5507 | 5444 (98.9%) | a branch to an `if`'s end that an inner construct's ends there too is the inner's: the outer `if` has no `else` |
| 9 | 5447 of 5507 | 5446 (98.9%; all but one of those decompiled) | a constant no receiver reference (NTK); `l := <loop>` kept a statement so the loop's locals are declared first |
| 12 | 5505 of 5507 | 5504 (99.9%) | the native-function frames the NTK put in literals (`{class: 0x132, funcPtr, numArgs}`, calling a C function with no global name, such as YieldToFork): the special immediate 0x132 is a constant, `GetGlobalFn('Length).class`; a function's constants come before it |
| 11 | 5498 of 5507 | 5497 (99.8%) | string subclasses from their text and rectangles from `MakeRect`: the host keeps both in its own byte order |
| 10 | 5498 of 5507 | 5491 (99.7%) | literals no quoted source makes (binaries; frames and arrays holding a binary or a function) written as constants that build them: `kBinaryFromHex`, `{tag: kFunction_x}` |

The 5507 functions are every top-level NewtonScript function in the ROM's
object area; functions that are literals of others are decompiled inside
them.

### What does not decompile yet

- **Binary literals** are decompiled (since round 10) as constants that
  build them: `kBinaryFromHex` out of the bytes' hex. Two kinds of binary
  are not kept as the ROM's bytes on the host, because the ROM importer
  (`frames/ObjectAreaImport.cpp`) turns them into host order: a string of
  any subclass (`'string.noData`), made instead as
  `SetClass(Clone("text"), 'class)`; and the shapes kept as shorts
  (`IsHalfwordShapeClass`: `'rectangle`, `'boundsRect`, ...), made with
  `MakeRect`. In the end these belong to the ROM-free track's resource
  extraction.
- **Reals of class `'Real`:** the capitalised class, which the lexer does
  not make.
- **Immediates beyond the lexer:** the special immediates such as 0x132
  (a native function's class) have no syntax. They are written as
  constants evaluated from an object that carries one (round 12).
- **A few branch shapes not yet understood.**

The remaining round-trip failures (instructions) are the next round's work
(`docs/next-steps.md`).
