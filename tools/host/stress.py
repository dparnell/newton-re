#!/usr/bin/env python3
"""Run host tests under load, to reproduce (and check the fixes for) the
timing races a full parallel ctest shows now and then.

Usage:
    python tools/host/stress.py <build dir> --test NAME [--copies N] [--hogs N]
    python tools/host/stress.py <build dir> --suite [--rounds N] [--hogs N] [-j N] [-R REGEX]

--test runs N copies of one ctest test at once, each in a directory of its
own under tmp/stress/<name>/<copy> with its own store (the test's command
line, working directory, pass/fail expressions and timeout are read from
`ctest --show-only=json-v1`; a `--store` argument is moved into the copy's
directory, and relative output such as a script's tmp/x.pgm lands there
too), and says how many passed; the output of each failed copy is printed.
A test that listens on a fixed port (--tcp-echo) cannot run as copies.

--suite runs the whole ctest (or those matching -R) with `ctest -j`, ROUNDS
times, and reports each round's wall time and failed tests.

--hogs N starts N processes that spin on a CPU for as long as the run
lasts, so the machine is short of time as a loaded build machine is: a test
that waits a fixed time for something asynchronous (a package installing,
the NIE's procrastinated setup) fails under it, where one that waits on a
condition does not.  This is how the races in host.NewtonThirdPartyPackages,
host.NewtonInetSetup and host.NewtonThirdPartyApps were reproduced
(docs/work-log.md, 2026-09-30).
"""

import argparse
import json
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def spin():
    while True:
        pass


def start_hogs(n):
    hogs = []
    for _ in range(n):
        p = multiprocessing.Process(target=spin, daemon=True)
        p.start()
        hogs.append(p)
    return hogs


def stop_hogs(hogs):
    for p in hogs:
        p.terminate()
    for p in hogs:
        p.join()


def test_info(build, name):
    out = subprocess.run(['ctest', '--test-dir', build, '--show-only=json-v1', '-R', '^' + re.escape(name) + '$'],
                         capture_output=True, text=True, check=True).stdout
    tests = json.loads(out)['tests']
    if not tests:
        sys.exit('stress: no test named %s in %s' % (name, build))
    t = tests[0]
    props = {p['name']: p['value'] for p in t.get('properties', [])}
    return t['command'], props


def run_copies(build, name, copies):
    command, props = test_info(build, name)
    base = os.path.join(REPO, 'tmp', 'stress', name)
    shutil.rmtree(base, ignore_errors=True)
    procs = []
    for i in range(copies):
        where = os.path.join(base, str(i + 1))
        os.makedirs(os.path.join(where, 'tmp'), exist_ok=True)
        args = list(command)
        for k in range(len(args) - 1):
            if args[k] == '--store':
                args[k + 1] = os.path.join(where, os.path.basename(args[k + 1]))
        log = open(os.path.join(where, 'output.txt'), 'w')
        procs.append((subprocess.Popen(args, cwd=where, stdout=log, stderr=subprocess.STDOUT), log, where))
    timeout = float(props.get('TIMEOUT', 600))
    started = time.time()
    passes = []
    for p, log, where in procs:
        try:
            p.wait(timeout=max(1, timeout - (time.time() - started)))
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
        log.close()
        text = open(os.path.join(where, 'output.txt'), errors='replace').read()
        ok = p.returncode == 0 if 'PASS_REGULAR_EXPRESSION' not in props else \
            any(re.search(r, text, re.S) for r in props['PASS_REGULAR_EXPRESSION'])
        if any(re.search(r, text) for r in props.get('FAIL_REGULAR_EXPRESSION', [])):
            ok = False
        passes.append(ok)
        if not ok:
            lines = [l for l in text.splitlines() if not l.startswith('[packages]')]
            print('%s: copy %s failed (%s):' % (name, os.path.basename(where), where))
            print('\n'.join('    ' + l for l in lines[-12:]))
    print('%s: %d of %d copies passed in %.0f s' % (name, sum(passes), copies, time.time() - started))
    return all(passes)


def run_suite(build, rounds, jobs, regex):
    all_ok = True
    for r in range(rounds):
        cmd = ['ctest', '--test-dir', build, '-j', str(jobs)]
        if regex:
            cmd += ['-R', regex]
        started = time.time()
        out = subprocess.run(cmd, capture_output=True, text=True).stdout
        failed = re.findall(r'^\s*\d+ - (\S+) \(', out, re.M)
        summary = re.findall(r'^\d+% tests passed.*$', out, re.M)
        print('round %d: %.0f s, %s%s' % (r + 1, time.time() - started, summary[0] if summary else '?',
                                          (' - failed: ' + ', '.join(failed)) if failed else ''))
        all_ok = all_ok and not failed
    return all_ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('build', help='the CMake build directory (e.g. build/host)')
    ap.add_argument('--test', help='the ctest test to run as copies')
    ap.add_argument('--copies', type=int, default=8)
    ap.add_argument('--suite', action='store_true', help='run the whole ctest instead')
    ap.add_argument('--rounds', type=int, default=1)
    ap.add_argument('-j', type=int, default=8, help='ctest parallelism for --suite')
    ap.add_argument('-R', help='only the tests matching (with --suite)')
    ap.add_argument('--hogs', type=int, default=0, help='CPU-spinning processes alongside')
    a = ap.parse_args()
    if not a.test and not a.suite:
        ap.error('--test NAME or --suite')
    hogs = start_hogs(a.hogs)
    try:
        ok = run_copies(a.build, a.test, a.copies) if a.test else run_suite(a.build, a.rounds, a.j, a.R)
    finally:
        stop_hogs(hogs)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
