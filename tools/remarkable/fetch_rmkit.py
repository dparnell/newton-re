#!/usr/bin/env python3
"""Fetch rmkit (github.com/rmkit-dev/rmkit, MIT) and make the single header
newton's reMarkable window can be built over (src/host/remarkable/
RMKitPanel.cpp; docs/host-remarkable.md).

rmkit is written in okp, a Python-like dialect its own transpiler (`okp`,
on PyPI) turns into C++; `rmkit.h` is the transpiled library in one file.
This script, run on Linux or WSL (okp's paths go wrong on Windows - the
header it writes there does not compile):

  1. clones rmkit at the pinned commit into OUT/rmkit-src (or reuses it),
  2. fetches okp and its one dependency (future) from PyPI into OUT/okp -
     okp is pure Python and published only as source, so the package
     directories are simply unpacked there (no pip, nothing installed),
  3. transpiles src/rmkit/rmkit.cpy into OUT/rmkit.h, as rmkit's own
     Makefile does (`okp -sh -ig RMKIT_IMPLEMENTATION -ns -ni -for ...`),
  4. patches it for a 64-bit build: rmkit's REMARKABLE build passes
     ioctl arguments as `(uint32_t) &value`, which loses the pointer's top
     half on aarch64 (and does not compile) - `pointer_size` is made
     `uintptr_t`,
  5. copies rmkit's vendored stb (stb.cpp, the headers) to OUT/stb.

Then configure newton with -DNEWTON_HOST_WINDOW=remarkable
-DNEWTON_RMKIT_DIR=OUT.

    python3 tools/remarkable/fetch_rmkit.py -o tmp/rmkit
"""
import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

RMKIT_URL = "https://github.com/rmkit-dev/rmkit.git"
RMKIT_COMMIT = "fc372b2"            # 2025-07-04, "[rmkit] single-header fixes (#234)"
PYPI = [("okp", "0.0.54", "okp"), ("future", "1.0.0", "future")]     # (project, version, package directory)


def run(command, cwd=None, env=None):
    print("+", " ".join(command), flush=True)
    subprocess.check_call(command, cwd=cwd, env=env)


def fetch_python_package(project, version, package, into):
    """The package directory of a pure-Python project from PyPI, checked
    against PyPI's digest and unpacked into `into`."""
    meta = json.load(urllib.request.urlopen("https://pypi.org/pypi/%s/%s/json" % (project, version)))
    files = sorted(meta["urls"], key=lambda u: u["packagetype"] != "bdist_wheel")     # a wheel if there is one
    chosen = files[0]
    print("+ fetch", chosen["url"], flush=True)
    data = urllib.request.urlopen(chosen["url"]).read()
    if hashlib.sha256(data).hexdigest() != chosen["digests"]["sha256"]:
        sys.exit("fetch_rmkit: %s does not match PyPI's digest" % chosen["filename"])
    if chosen["filename"].endswith(".whl"):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for name in z.namelist():
                if name.startswith(package + "/"):
                    z.extract(name, into)
    else:
        with tarfile.open(fileobj=io.BytesIO(data)) as t:
            prefix = "%s-%s/%s/" % (project, version, package)
            for member in t.getmembers():
                if member.name.startswith(prefix) and member.isfile():
                    target = os.path.join(into, package, member.name[len(prefix):])
                    os.makedirs(os.path.dirname(target), exist_ok=True)
                    with open(target, "wb") as f:
                        f.write(t.extractfile(member).read())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--out", default=os.path.join("tmp", "rmkit"))
    args = parser.parse_args()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    src = os.path.join(out, "rmkit-src")
    if not os.path.isdir(os.path.join(src, ".git")):
        run(["git", "clone", "-q", RMKIT_URL, src])
    run(["git", "checkout", "-q", RMKIT_COMMIT], cwd=src)

    okp_dir = os.path.join(out, "okp")
    for project, version, package in PYPI:
        if not os.path.isdir(os.path.join(okp_dir, package)):
            fetch_python_package(project, version, package, okp_dir)

    build = os.path.join(src, "src", "build")
    os.makedirs(build, exist_ok=True)
    env = dict(os.environ, PYTHONPATH=okp_dir, CXX="true")
    run([sys.executable, "-c", "import sys; from okp.main import main; sys.argv[0] = 'okp'; main()",
         "-sh", "-ig", "RMKIT_IMPLEMENTATION", "-ns", "-ni", "-for", "-d", "../.rmkit_cpp/",
         "-o", "../build/rmkit.h", "rmkit.cpy", "--", "-DREMARKABLE=1"],
        cwd=os.path.join(src, "src", "rmkit"), env=env)

    with open(os.path.join(build, "rmkit.h")) as f:
        header = f.read()
    patched = header.replace("#define pointer_size uint32_t", "#define pointer_size uintptr_t")
    if patched == header:
        sys.exit("fetch_rmkit: rmkit.h has no 32-bit pointer_size to patch - check the commit")
    with open(os.path.join(out, "rmkit.h"), "w") as f:
        f.write("/* rmkit " + RMKIT_COMMIT + " (MIT, github.com/rmkit-dev/rmkit), transpiled by okp and patched for a\n"
                "   64-bit build by tools/remarkable/fetch_rmkit.py: pointer_size is uintptr_t */\n")
        f.write(patched)
    stb = os.path.join(out, "stb")
    if os.path.isdir(stb):
        shutil.rmtree(stb)
    shutil.copytree(os.path.join(src, "src", "vendor", "stb"), stb)
    # stb's headers open with an include of rmkit's defines.h by a path
    # relative to rmkit's own tree, which stb.cpp's DEFINES_H makes empty
    # anyway: taken out so the copy stands alone
    for name in os.listdir(stb):
        path = os.path.join(stb, name)
        with open(path) as f:
            text = f.read()
        if '#include "../../rmkit/defines.h"' in text:
            with open(path, "w") as f:
                f.write(text.replace('#include "../../rmkit/defines.h"', "/* (fetch_rmkit.py: rmkit's defines.h include taken out) */", 1))
    print("fetch_rmkit: %s/rmkit.h and %s/ - configure with -DNEWTON_RMKIT_DIR=%s" % (out, stb, out))


if __name__ == "__main__":
    main()
