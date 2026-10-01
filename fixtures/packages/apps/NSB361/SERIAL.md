# NS BASIC 3.61: the serial number

On its first run NS BASIC asks "Enter your Serial Number now:" and keeps
asking before every command until it has one it accepts.

**Use `1002301`.**

How it was found: NS BASIC 3.61's own check (its `commsCheck`) accepts a
number `n` when `(n - 1000001) mod 23 = 0`; 1002301 is one such number
(1000001 + 23 x 100), and any number satisfying the check works.  The serial numbers in `../NewtCard/README.md` are NewtCard's, not
NS BASIC 3.61's - they satisfy `(n - 10000017) mod 127 = 0`, which 3.61
does not accept.

`src/host/demo/apps-nsbasic.ns` (ctest `host.NewtonAppNSBasic`) types this
number when the prompt appears.

For NewtCard itself the test dials `10002557` on its digit wheels
(`src/host/demo/apps-newtcard.ns`).
