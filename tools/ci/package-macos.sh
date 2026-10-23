#!/bin/sh
# macOS's release archive (.github/workflows/release.yml, docs/releases.md):
# Newton.app - the bundle the build makes, signed ad hoc (docs/host-macos.md) -
# with newtonscript and romsrc-objects.bin beside it and
# tools/ci/README-macos.txt, in dist/<name>/, then dist/<name>.zip made by
# ditto, which keeps the bundle's signature, symbolic links and permissions.
#
#   tools/ci/package-macos.sh <build> <name>
set -e
build="$1"
name="$2"
here="$(cd "$(dirname "$0")" && pwd)"
rm -rf "dist/$name" "dist/$name.zip"
mkdir -p "dist/$name"
ditto "$build/host/Newton.app" "dist/$name/Newton.app"
cp "$build/host/newtonscript" "$build/romsrc-objects.bin" "dist/$name/"
cp "$here/README-macos.txt" "dist/$name/README.txt"
codesign --verify --strict "dist/$name/Newton.app"
lipo -archs "dist/$name/Newton.app/Contents/MacOS/newton"
cd dist
ditto -c -k --keepParent "$name" "$name.zip"
ls -l "$name.zip"
