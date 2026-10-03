#!/bin/sh
# A desktop release's archive (.github/workflows/release.yml, docs/releases.md):
# the build installed - newton, newtonscript and romsrc-objects.bin, which
# newton boots from and finds beside itself - with tools/ci/README.txt, into
# dist/<name>/, then dist/<name>.zip or dist/<name>.tar.gz.
#
#   tools/ci/package.sh <build> <name> zip|tar
set -e
build="$1"
name="$2"
kind="$3"
here="$(cd "$(dirname "$0")" && pwd)"
python="$(command -v python3 || command -v python)"
rm -rf "dist/$name"
mkdir -p dist
cmake --install "$build" --prefix "dist/$name"
mv "dist/$name/bin/"* "dist/$name/"
rmdir "dist/$name/bin"
cp "$here/README.txt" "dist/$name/README.txt"
cd dist
if [ "$kind" = zip ]; then
	"$python" -m zipfile -c "$name.zip" "$name"
else
	"$python" -m tarfile -c "$name.tar.gz" "$name"
fi
ls -l "$name"*
