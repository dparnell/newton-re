#!/usr/bin/env python3
"""Run host tests under load, to reproduce (and check the fixes for) the
timing races a full parallel ctest shows now and then.

Usage:
    python tools/host/stress.py <build dir> --test NAME [--copies N] [--hogs N]
    python tools/host/stress.py <build dir> --suite [--rounds N] [--hogs N] [-j N] [-R REGEX]

--test runs N copies of one ctest test at once, each in a directory of its
own under tmp/stress/<name>/<run>/<copy> (<run> a time stamp and the
process id, so two stress runs never share or clear each other's copies)
with its own store (the test's command line, environment, pass/fail
expressions and timeout are read from `ctest --show-only=json-v1`; a
`--store` argument is moved into the copy's directory, and relative output
such as a script's tmp/x.pgm lands there too), and says how many passed;
the output of each failed copy is printed.  A test that needs a fixture
gets it: the fixture's setup tests that keep a store (--store) run in each
copy's directory first, as the copy's own steps (a restart test's first
run, a package installed on a fresh store), and those that keep none (an
extraction into the build directory) run once, before the copies, where
ctest runs them.  A test that holds a fixed port (--tcp-echo, a --port
of a server it runs, or any RESOURCE_LOCK in its ctest properties - the ctests sharing a port say so
with one) runs its copies one after another rather than at once.

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
import threading
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


def test_list(build, name):
    """The test and the fixture setups it needs, in the order ctest runs
    them (ctest's own listing of one test includes its setups)."""
    out = subprocess.run(['ctest', '--test-dir', build, '--show-only=json-v1', '-R', '^' + re.escape(name) + '$'],
                         capture_output=True, text=True, check=True).stdout
    tests = []
    for t in json.loads(out)['tests']:
        props = {p['name']: p['value'] for p in t.get('properties', [])}
        tests.append((t['name'], t['command'], props))
    if not any(n == name for n, _, _ in tests):
        sys.exit('stress: no test named %s in %s' % (name, build))
    return tests


def per_copy(command):
    """Whether a test keeps state of its own (a store) and so runs in each
    copy's directory; a setup that does not (an extraction into the build
    directory) runs once, before the copies, where ctest would run it."""
    return '--store' in command


def run_one(command, props, where, log_name, stores=()):
    """One test run in `where`: its --store moved there, as is any other
    argument naming one of the copy's stores (a checker reading the store
    file a setup wrote), and a relative argument naming a file of the
    test's working directory made absolute (a script run with a relative
    path).  ==> (passed, text)."""
    args = list(command)
    home = props.get('WORKING_DIRECTORY', REPO)
    for k in range(len(args)):
        if k > 0 and args[k - 1] == '--store' or os.path.isabs(args[k]) and os.path.basename(args[k]) in stores:
            args[k] = os.path.join(where, os.path.basename(args[k]))
        elif k > 0 and not os.path.isabs(args[k]) and not args[k].startswith('-')                 and os.path.exists(os.path.join(home, args[k])):
            args[k] = os.path.join(home, args[k])
    env = dict(os.environ)
    for setting in props.get('ENVIRONMENT', []):
        k, _, v = setting.partition('=')
        env[k] = v
    log_path = os.path.join(where, log_name)
    with open(log_path, 'w') as log:
        p = subprocess.Popen(args, cwd=where, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            p.wait(timeout=float(props.get('TIMEOUT', 600)))
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
    text = open(log_path, errors='replace').read()
    ok = p.returncode == 0 if 'PASS_REGULAR_EXPRESSION' not in props else         any(re.search(r, text, re.S) for r in props['PASS_REGULAR_EXPRESSION'])
    if any(re.search(r, text) for r in props.get('FAIL_REGULAR_EXPRESSION', [])):
        ok = False
    return ok, text


def run_copy(steps, where):
    """A copy's own steps in turn - its per-copy setups, then the test -
    each logged to a file of its own; ==> (passed, the failing step's name
    and output)."""
    stores = {os.path.basename(c[k + 1]) for _, c, _ in steps for k in range(len(c) - 1) if c[k] == '--store'}
    for i, (name, command, props) in enumerate(steps):
        ok, text = run_one(command, props, where, 'output.txt' if i == len(steps) - 1 else name + '.txt', stores)
        if not ok:
            return False, name, text
    return True, None, None


def run_copies(build, name, copies):
    tests = test_list(build, name)
    # a directory of this run's own, so two stress runs of one test at once
    # do not clear or share each other's copies
    base = os.path.join(REPO, 'tmp', 'stress', name, time.strftime('%Y%m%d-%H%M%S') + '-%d' % os.getpid())
    os.makedirs(base, exist_ok=True)
    started = time.time()
    # the setups that keep no state of their own, once, as ctest runs them
    steps = []
    for n, command, props in tests:
        if n != name and not per_copy(command):
            ok, text = run_shared(command, props, base, n)
            if not ok:
                print('%s: the setup %s failed:' % (name, n))
                print('\n'.join('    ' + l for l in text.splitlines()[-12:]))
                return False
        else:
            steps.append((n, command, props))
    results = []
    lock = threading.Lock()

    def one(i):
        where = os.path.join(base, str(i + 1))
        os.makedirs(os.path.join(where, 'tmp'), exist_ok=True)
        r = run_copy(steps, where)
        with lock:
            results.append((i + 1, where) + r)

    # a test that holds a fixed resource - a TCP port it listens on or
    # serves from (--tcp-echo, or a RESOURCE_LOCK, which is how the ctests
    # sharing one say so) - runs its copies one after another
    _, command, props = tests[-1]
    def fixed_port(option):
        # (a port of 0 is a free one the program picks, so copies never meet)
        return any(command[k] == option and k + 1 < len(command) and command[k + 1] != '0'
                   for k in range(len(command)))
    serial = fixed_port('--tcp-echo') or fixed_port('--port') or any(p.get('RESOURCE_LOCK') for _, _, p in tests)
    if serial:
        print('%s: holds a fixed port (--tcp-echo, --port or RESOURCE_LOCK): the copies run one at a time' % name)
        for i in range(copies):
            one(i)
    else:
        threads = [threading.Thread(target=one, args=(i,)) for i in range(copies)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
    passes = 0
    for number, where, ok, step, text in sorted(results):
        if ok:
            passes += 1
            continue
        lines = [l for l in text.splitlines() if not l.startswith('[packages]')]
        print('%s: copy %d failed at %s (%s):' % (name, number, step, where))
        print('\n'.join('    ' + l for l in lines[-12:]))
    print('%s: %d of %d copies passed in %.0f s' % (name, passes, copies, time.time() - started))
    return passes == copies


def run_shared(command, props, base, name):
    """A setup run once, in its own working directory (it writes into the
    build directory, which every copy then reads), logged in the run's."""
    where = props.get('WORKING_DIRECTORY', REPO)
    env = dict(os.environ)
    for setting in props.get('ENVIRONMENT', []):
        k, _, v = setting.partition('=')
        env[k] = v
    p = subprocess.run(command, cwd=where, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, errors='replace', timeout=float(props.get('TIMEOUT', 600)))
    text = p.stdout
    open(os.path.join(base, name + '.txt'), 'w').write(text)
    ok = p.returncode == 0 if 'PASS_REGULAR_EXPRESSION' not in props else         any(re.search(r, text, re.S) for r in props['PASS_REGULAR_EXPRESSION'])
    return ok, text


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
