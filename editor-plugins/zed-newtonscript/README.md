# NewtonScript for Zed

A [Zed](https://zed.dev) extension for NewtonScript over the tree-sitter
grammar in `../tree-sitter-newtonscript` (whose README says what the
grammar accepts and where it comes from). It gives `.ns` files:

- **highlighting** (`languages/newtonscript/highlights.scm`) - keywords in
  any case, sends' message names, slot names, quoted symbols, characters,
  string escapes, magic pointers (`@123`), `romsrc/`'s record lines;
- **brackets** (`brackets.scm`) - `()`, `[]`, `{}` and `begin`/`end`
  matched; quotes and brackets closed as you type (`config.toml`);
- **indentation** (`indents.scm`) inside `begin ... end`, `repeat ...
  until`, frames, arrays and argument lists;
- **an outline** (`outline.scm`) of the functions and frames a file
  defines - `func f(...)`, `global ...`, `name := func ...` or `{...}`, and
  slots holding either - which is what Zed's outline panel and symbol
  search (`ctrl-shift-o`) list.

## Installing

1. In Zed, open the command palette and run **zed: install dev
   extension**, then pick this directory (`editor-plugins/zed-newtonscript`).
2. Zed fetches the grammar from the `repository` and `rev` in
   `extension.toml` (with `path` naming its subdirectory) into
   `grammars/` here (git-ignored) and compiles it to WebAssembly with a
   wasi-sdk it downloads the first time. Its log (**zed: open log**)
   shows any failure.
3. Open any `.ns` file - `src/host/demo/views.ns`, or one of `romsrc/`'s.

The `repository` is this checkout, `file:///F:/development/newtwon-re`;
change it if yours is elsewhere. It is not the project's git server
because that server only lets a commit be fetched by its hash when the
commit is a branch's tip (git's "Server does not allow request for
unadvertised object"), and `rev` is the commit that last changed the
grammar, which rarely is. Any commit in the checkout will do, pushed or
not.

## Changing it

- **The queries** (`languages/newtonscript/*.scm`) are read by Zed
  directly: after editing one, run **zed: rebuild dev extension** (or
  reinstall). Check a query compiles against the grammar first:
  `npx tree-sitter query ../zed-newtonscript/languages/newtonscript/highlights.scm <file.ns>`
  from `../tree-sitter-newtonscript` (after its `npm install`). Where
  several patterns match one node the later one wins, so the general
  patterns are at the top of `highlights.scm`. The capture names are
  Zed's (its "Syntax highlighting" documentation lists them).
- **The grammar**: edit `../tree-sitter-newtonscript/grammar.js`,
  regenerate and test there (its README), commit, then put that commit's
  hash in `extension.toml`'s `rev` and commit again.

`highlights.scm` is also the grammar's own highlight query (the
grammar's `tree-sitter.json` points here), so there is one copy.

Publishing to Zed's extension registry would need the extension in a
public repository and a licence file, which this repository does not have.
