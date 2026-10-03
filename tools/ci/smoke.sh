#!/bin/sh
# A release build's smoke test (.github/workflows/release.yml, docs/releases.md):
# newtonscript runs a line, and newton boots headless from the object file to
# the Setup assistant and quits (tools/ci/smoke.ns).
#
#   tools/ci/smoke.sh <build>/host
#
# Exits non-zero if either does not get there.
set -e
dir="$1"
here="$(cd "$(dirname "$0")" && pwd)"
out="$("$dir/newtonscript" -e 'Print("smoke: " & NumberStr(6 * 7))' 2>&1)"
echo "$out"
echo "$out" | grep -q '"smoke: 42"'
out="$("$dir/newton" --display 320x480 --headless 120 --script "$here/smoke.ns" 2>&1)"
echo "$out" | tail -n 20
echo "$out" | grep -q 'smoke: booted'
echo "smoke test passed"
