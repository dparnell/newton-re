#!/usr/bin/env python3
"""The views of a profile made of whole stacks: shared by profile.py
(--walk, --load: Windows) and linuxsample.py (--samples: a Linux newton's
own sample file, the reMarkable's included).

A stack is a list of function names, innermost first.  report_stacks
prints the functions by self time and by inclusive time (each counted once
a sample however deep it recurs), and for each name in `callees` (a
substring of a function's name; the busiest function that matches) what
its time went to - the frame just inside it, its own time as "(self)" -
and for each in `callers` where it was called from; a function that
recurs is counted at every appearance, each callee or caller once a
sample, so those lines can add up to more than its share.  Standard
library only; nothing here is tied to a platform.
"""

import collections


def report_stacks(stacks, top=30, callees=(), callers=()):
	n = len(stacks)
	if n == 0:
		print("no samples")
		return
	selfs = collections.Counter(s[0] for s in stacks)
	incl = collections.Counter()
	for s in stacks:
		for f in set(s):
			incl[f] += 1
	print("\nself:")
	for f, k in selfs.most_common(top):
		print(f"  {100.0 * k / n:5.1f}%  {f}")
	print("\ninclusive:")
	for f, k in incl.most_common(top):
		print(f"  {100.0 * k / n:5.1f}%  {f}")

	def resolve(part):
		matches = [f for f, _ in incl.most_common() if part in f]
		return matches[0] if matches else None

	for part in callees:
		f = resolve(part)
		if f is None:
			print(f"\nno function matches {part!r}")
			continue
		inside = collections.Counter()
		total = 0
		for s in stacks:
			if f not in s:
				continue
			total += 1
			inside.update({"(self)" if i == 0 else s[i - 1] for i, g in enumerate(s) if g == f})
		print(f"\ncallees of {f} ({100.0 * total / n:.1f}% of the samples):")
		for g, k in inside.most_common(top):
			print(f"  {100.0 * k / n:5.1f}%  {g}")
	for part in callers:
		f = resolve(part)
		if f is None:
			print(f"\nno function matches {part!r}")
			continue
		outside = collections.Counter()
		total = 0
		for s in stacks:
			if f not in s:
				continue
			total += 1
			outside.update({"(the thread's start)" if i == len(s) - 1 else s[i + 1] for i, g in enumerate(s) if g == f})
		print(f"\ncallers of {f} ({100.0 * total / n:.1f}% of the samples):")
		for g, k in outside.most_common(top):
			print(f"  {100.0 * k / n:5.1f}%  {g}")
