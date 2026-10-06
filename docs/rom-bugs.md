# The ROM's bugs, fixed

The reconstruction ports the ROM's behaviour exactly, bugs included: a bug
found on the way is ported as the ROM has it, so the reconstruction computes
what the device did, and described where it is (`ROM BUG, kept: ...`).

Since 2026-10-06 (the owner's decision) every known bug is also **fixed**,
beside the ROM's code, behind one switch:

| | |
|---|---|
| default | the bugs are fixed |
| `NEWTON_ROM_BUGS=1` | the ROM's own behaviour, bugs and all |

The switch is `src/host/RomBugs.h`: `RomBugFixed()` answers whether the fixes
are in force (the environment is read once), and a test sets it either way
with `SetRomBugFixed()`. A fix reads

```cpp
// ROM BUG (fixed): what the ROM does wrong, and what that does.  The fix ...
if (RomBugFixed())
	...the corrected code...
else
	...the ROM's code, as ported...
```

so the faithful machine is still there, unchanged, and still what the suite
can compare against. A test that pinned the ROM's behaviour keeps doing so
with `SetRomBugFixed(false)`, and a test of the fix sits beside it.

The year-2010 fix (`docs/intl/year-2010.md`) came first and keeps its own
switch, `NEWTON_ROM_2010_BUG`.

## What counts

A bug is a comment that says so: `ROM BUG`, `BUG (the ROM's)`, `bug kept`,
`leak kept`, `(sic: ...)`. Its marker becomes `ROM BUG (fixed)` when the fix
is in. `ROM QUIRK` comments - behaviour that is odd but not wrong - are not
bugs and are left as they are.

Bugs where the host could not reproduce what the ROM did (a register or stack
slot never written, a read past a buffer) were already ported with a value
the host substitutes; their fix is the behaviour the code meant.

Where a fix has no visible effect (a dead store, a leak), it is made all the
same: the fixed machine is meant to be the one the code describes.

## Tracking

`tools/newton-rom/analysis/rombugs.py` lists every bug site, kept or fixed,
by area:

```sh
python tools/newton-rom/analysis/rombugs.py              # all, with a count
python tools/newton-rom/analysis/rombugs.py --kept       # the work left
python tools/newton-rom/analysis/rombugs.py --check      # every "fixed" file asks RomBugFixed()
python tools/newton-rom/analysis/rombugs.py --markdown docs/rom-bugs-list.md
```

`docs/rom-bugs-list.md` is its generated list.
