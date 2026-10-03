#!/bin/sh
# The reMarkable release's archive (.github/workflows/release.yml,
# docs/releases.md): the AppLoad application tools/remarkable/package.py makes
# (newton, the object file, run.sh, the manifest, the icon) in
# dist/<name>/newton, with tools/ci/README-remarkable.txt, then
# dist/<name>.tar.gz - a tar keeps newton and run.sh executable.
#
#   tools/ci/package-remarkable.sh <build> <name>
set -e
build="$1"
name="$2"
here="$(cd "$(dirname "$0")" && pwd)"
python="$(command -v python3 || command -v python)"
rm -rf "dist/$name"
mkdir -p "dist/$name"
"$python" "$here/../remarkable/package.py" --newton "$build/host/newton" --objects "$build/romsrc-objects.bin" -o "dist/$name/newton"
chmod +x "dist/$name/newton/newton" "dist/$name/newton/run.sh"
cp "$here/README-remarkable.txt" "dist/$name/README.txt"
cd dist
"$python" -m tarfile -c "$name.tar.gz" "$name"
ls -l "$name.tar.gz"
