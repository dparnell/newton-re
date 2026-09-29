# The NewtonScript decompiler and its round trip

This is the first step of the long-term track of booting with no ROM image
(`docs/next-steps.md`). The ROM's NewtonScript has to become source that
can be read, edited and compiled back into the same functions.

- **Decompiler:** `tools/newton-rom/analysis/nsdecompile.py`.
- **Round-trip harness:** `newtonscript --roundtrip`, in
  `src/host/NSRoundTrip.cpp`.
- **ctest:** `host.NSDecompileRoundTrip` runs the round trip over a sample
  of 600 functions and fails below 93%.

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
the project was built. Such a function is written as its own definition,
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

The 5507 functions are every top-level NewtonScript function in the ROM's
object area; functions that are literals of others are decompiled inside
them.

### What does not decompile yet

- **Binary literals:** a bitmap, mask or pattern, a sound's samples,
  `'rectangle`, `'deskey`. There is no NewtonScript syntax for these; they
  belong to the resource extraction of the ROM-free track.
- **Literals that are functions:** a path expression or a function inside
  a quoted frame or array.
- **Reals of class `'Real`:** the capitalised class, which the lexer does
  not make.
- **Immediates beyond the lexer:** for example, characters above `$\u`
  range.
- **A few branch shapes not yet understood.**

The remaining round-trip failures (instructions) are the next round's work
(`docs/next-steps.md`).
