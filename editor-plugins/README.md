# Editor plugins

Editor support for NewtonScript, the language the Newton's applications
and much of the ROM (`romsrc/`, the host's demos in `src/host/demo/`) are
written in.

| Directory | What |
|---|---|
| `tree-sitter-newtonscript/` | A tree-sitter grammar for NewtonScript, written from the ROM's own compiler; usable by any tree-sitter editor |
| `zed-newtonscript/` | A [Zed](https://zed.dev) extension over that grammar: highlighting, brackets, indentation and an outline |

Each directory's README says how to build, test and install it.
