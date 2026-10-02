# tools/remarkable

newton on a reMarkable tablet - the Paper Pro first.  The whole story (the
tablet, AppLoad's qtfb, rmkit, the build, the device steps) is
`docs/host-remarkable.md`; this is the list of tools.

| tool | what it does | run |
|---|---|---|
| `rmprobe.c`, `build_probe.py` | a probe of the tablet: system, glibc, display and input devices, AppLoad; `--events S`, `--qtfb S` | `python tools/remarkable/build_probe.py -o tmp/rmpp-app`, then `scp tmp/rmpp-app/rmprobe root@10.11.99.1:/home/root/newton/` and `ssh root@10.11.99.1 /home/root/newton/rmprobe` |
| `qtfbserver.py` | a stand-in for AppLoad's qtfb server (Linux only), with scripted steps and PNG snapshots | `python3 tools/remarkable/qtfbserver.py --socket /tmp/newton-qtfb.sock --step quiet:6 --step snap:x.png -- ./qemu-aarch64-static -L sysroot ./newton ...` |
| `fetch_rmkit.py` | rmkit (MIT) at a pinned commit made into `rmkit.h`, patched for 64 bits, with stb (Linux/WSL) | `python3 tools/remarkable/fetch_rmkit.py -o tmp/rmkit` |
| `package.py` | an AppLoad application directory for newton (`--rmkit`: the rmkit variant) | `python tools/remarkable/package.py --newton <newton> --objects <romsrc-objects.bin> -o tmp/rmpp-app/newton` |
| `icon.py` | the launcher's icon: a MessagePad with handwriting and a stylus, 150 x 150 grays (what AppLoad shows), anti-aliased; `package.py` uses it | `python tools/remarkable/icon.py -o icon.png [--size 150]` |
| `snap.ns` | the headless check: Setup's screen as `native-welcome.pgm` | see its header |
