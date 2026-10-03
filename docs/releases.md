# Releases

`.github/workflows/release.yml` builds newton for every host it runs on, on
GitHub's runners.  Pushing to `main` builds and smoke-tests all of them; pushing
a tag `v*` (`git tag v0.1.0 && git push github v0.1.0`) also publishes them as
a GitHub release with `tools/ci/release-notes.md` above GitHub's own notes.
`workflow_dispatch` runs it by hand from the Actions tab.

| job | runner | how | archive |
|---|---|---|---|
| `windows` | windows-latest | zig 0.16 (`src/cmake/zig-toolchain.cmake`), Release | `newton-windows-x86_64.zip` |
| `linux` | ubuntu-22.04 | clang, X11, ALSA, OpenSSL from apt, Release | `newton-linux-x86_64.tar.gz` (glibc 2.35 on) |
| `remarkable` | ubuntu-22.04 | a native `newtonscript` built first (a cross build cannot run its own to compile `romsrc/`), then zig's aarch64 cross build (`src/cmake/zig-aarch64-linux.cmake`, `-DNEWTON_HOST_WINDOW=remarkable`, `-DNEWTON_HOST_NEWTONSCRIPT`) | `newton-remarkable-aarch64.tar.gz`: the AppLoad application `package.py` makes |
| `release` | ubuntu-latest | on a tag only: the three archives attached to a release (`softprops/action-gh-release`) | - |

macOS is not built: its window and sound are not written yet
(`docs/host-macos.md`).

No ROM image is needed: the build compiles the committed `romsrc/` tree into
`romsrc-objects.bin`, which newton boots from and finds beside itself
(`host/HostObjectsFile.h`).  A desktop archive is the build's install
(`newton`, `newtonscript`, `romsrc-objects.bin`) and `tools/ci/README.txt`; the
reMarkable's is the AppLoad application directory and
`tools/ci/README-remarkable.txt`.

## The scripts (`tools/ci/`)

The workflow's steps are scripts, so a release can be made, or a step
checked, by hand:

| script | what it does | run |
|---|---|---|
| `smoke.sh` | `newtonscript` runs a line; `newton` boots headless from the object file to the Setup assistant and quits (`smoke.ns`) | `tools/ci/smoke.sh <build>/host` |
| `package.sh` | the build installed into `dist/<name>/` with `README.txt`, then `dist/<name>.zip` or `.tar.gz` (Python's `zipfile`/`tarfile`, so the same on every runner) | `tools/ci/package.sh <build> <name> zip\|tar` |
| `package-remarkable.sh` | `tools/remarkable/package.py`'s AppLoad application in `dist/<name>/newton`, `newton` and `run.sh` made executable, with `README-remarkable.txt`; then `dist/<name>.tar.gz` (a tar keeps the executable bits that a copy from Windows loses) | `tools/ci/package-remarkable.sh <build> <name>` |

## Checked by hand (2026-10-03)

The workflow's steps were run before it was first pushed:

- **Windows**: a fresh Release configure and build with the zig toolchain,
  `smoke.sh` (booted to Setup), `package.sh ... zip`; the zip unpacked
  elsewhere and its `newton.exe` run from another directory boots from the
  object file beside it.
- **Linux**: the same under WSL 2's Ubuntu 22.04 (clang 14, X11, OpenSSL; no
  ALSA headers there, so silent - the runner installs them), and the
  tarball unpacked and run.
- **reMarkable**: the job's steps under WSL with zig 0.16.0 for Linux,
  as on the runner; its archive unpacked on the owner's tablet and newton
  booted there (a made-up `QTFB_KEY`, so nothing was shown).  Cross-building
  on Linux found two things a cross build from Windows never met:
  - CMake found the build machine's own x86-64 OpenSSL for the aarch64
    newton (`openssl/opensslconf.h` not found): a cross build now looks for
    OpenSSL only where `OPENSSL_ROOT_DIR` says (`src/print/CMakeLists.txt`),
    and the reMarkable has no ipps printing, as before;
  - zig's linker crashed (segmentation fault) on the
    `-Xlinker --dependency-file` CMake adds on a Linux build machine:
    `CMAKE_LINK_DEPENDS_USE_LINKER OFF` in `src/cmake/zig-toolchain.cmake`.
  And a Release build for the tablet is now stripped (`-s` in
  `zig-aarch64-linux.cmake`): 7.3 MB against 38 MB, since zig keeps the
  debug information otherwise and `zig objcopy --strip-all` is
  unimplemented.

What only GitHub can show: the runners' own tool versions (cmake 3.25 or
later is needed; `pip install ninja` on Windows), `mlugg/setup-zig` finding
zig 0.16.0, and the release job's permissions.
