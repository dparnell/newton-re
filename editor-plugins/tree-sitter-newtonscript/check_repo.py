#!/usr/bin/env python3
"""Parse every NewtonScript file in the repository with this grammar.

The repository holds about seven thousand NewtonScript files - the ROM's
own NewtonScript world as source (romsrc/), the host's demos and tests
(src/host/demo/) and a few more - which makes it the grammar's best test.

Usage (from this directory, after `npm install` and `npx tree-sitter
generate`):

    python check_repo.py [--show N] [PATH ...]

PATHs default to every `*.ns` file git tracks.  For each file that does
not parse it prints the file, the first ERROR/MISSING node's position and
the source line under it (at most N files, default 20), then a summary.
Exits 1 if any file failed.  Temporary files go under the repository's
tmp/.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
TMP = os.path.join(REPO, 'tmp')

RESULT = re.compile(r'^(?P<path>.+?)\s+Parse:.*?\((?P<kind>ERROR|MISSING[^\[]*)\s*\[(?P<row>\d+), (?P<col>\d+)\]')


def tracked_files():
    out = subprocess.run(['git', 'ls-files', '*.ns'], cwd=REPO, check=True,
                         capture_output=True, text=True).stdout
    return [os.path.join(REPO, line) for line in out.splitlines() if line]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('paths', nargs='*')
    parser.add_argument('--show', type=int, default=20)
    args = parser.parse_args()

    paths = [os.path.abspath(p) for p in args.paths] or tracked_files()
    os.makedirs(TMP, exist_ok=True)
    listing = os.path.join(TMP, 'tree-sitter-newtonscript-paths.txt')
    with open(listing, 'w', encoding='utf-8') as f:
        f.write('\n'.join(paths) + '\n')

    npx = 'npx.cmd' if os.name == 'nt' else 'npx'
    proc = subprocess.run([npx, 'tree-sitter', 'parse', '--paths', listing,
                           '--quiet', '--stat'],
                          cwd=HERE, capture_output=True, text=True,
                          encoding='utf-8', errors='replace')
    failures = []
    for line in proc.stdout.splitlines():
        m = RESULT.match(line)
        if m:
            failures.append((m['path'], m['kind'].strip(), int(m['row']), int(m['col'])))

    for path, kind, row, col in failures[:args.show]:
        with open(path, encoding='utf-8', errors='replace') as f:
            lines = f.read().splitlines()
        text = lines[row] if row < len(lines) else ''
        print(f'{os.path.relpath(path, REPO)}:{row + 1}:{col + 1}: {kind}')
        text = text.expandtabs(4)
        start = max(0, col - 80)
        print(f'    {text[start:start + 160]}')

    print(f'{len(paths) - len(failures)} of {len(paths)} files parse; {len(failures)} fail')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
