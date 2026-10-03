# tools/remarkable

newton on a reMarkable tablet - the Paper Pro first.  The whole story (the
tablet, AppLoad's qtfb, the build, the device steps) is
`docs/host-remarkable.md`; this is the list of tools.

| tool | what it does | run |
|---|---|---|
| `rmprobe.c`, `build_probe.py` | a probe of the tablet: system, glibc, display and input devices, AppLoad; `--events S`, `--qtfb S` | `python tools/remarkable/build_probe.py -o tmp/rmpp-app`, then `scp tmp/rmpp-app/rmprobe root@10.11.99.1:/home/root/newton/` and `ssh root@10.11.99.1 /home/root/newton/rmprobe` |
| `qtfbserver.py` | a stand-in for AppLoad's qtfb server (Linux only), with scripted steps and PNG snapshots | `python3 tools/remarkable/qtfbserver.py --socket /tmp/newton-qtfb.sock --step quiet:6 --step snap:x.png -- ./qemu-aarch64-static -L sysroot ./newton ...` |
| `package.py` | an AppLoad application directory for newton (`--old-appload`: for an AppLoad before v0.6.0) | `python tools/remarkable/package.py --newton <newton> --objects <romsrc-objects.bin> -o tmp/rmpp-app/newton` |
| `icon.py` | the launcher's icon: a MessagePad with handwriting and a stylus, 150 x 150 grays (what AppLoad shows), anti-aliased; `package.py` uses it | `python tools/remarkable/icon.py -o icon.png [--size 150]` |
| `snap.ns` | the headless check: Setup's screen as `native-welcome.pgm` | see its header |
| `rmsample.c` (built by `build_probe.py`) | the sampling profiler's trigger: sends newton's sample signal (SIGRTMIN+3) to each running thread every interval; newton appends the stacks to its sample file | `rmsample <pid> [seconds] [interval-us]` - below |

## Profiling on the tablet

The tablet has no `perf`, `gdb` or Python, so newton samples itself
(`src/host/newton.cpp`, `HostInstallSampler`) and `rmsample` only sends
the signal.  What is needed and how:

1. **A build with symbols**: the Release cross build is stripped (`-s`), so
   make a RelWithDebInfo one (`docs/host-remarkable.md`, "The build", with
   `-DCMAKE_BUILD_TYPE=RelWithDebInfo`; 38 MB).
2. **The sampler**: `python tools/remarkable/build_probe.py -o tmp/rmpp-tools`
   (`tmp/rmpp-tools/rmsample`).
3. **On the tablet**, with both copied to a directory of ours
   (`/home/root/newton-perf` here) and the object file beside newton:

        rm -f /tmp/newton-sample-*.txt
        ./newton --objects romsrc-objects.bin --display 320x480 --erase --store rb.store --headless 300 --script rbmany.ns &
        sleep 15; ./rmsample $(pidof newton) 20 2000  # 20 s, a round every 2 ms
        kill $(pidof newton)

   (`rbmany.ns` being `redrawbench.ns` with `rbRounds` raised), which leaves
   `/tmp/newton-sample-<pid>.txt` (some 7000 samples).
4. **Named on the development machine**, under WSL for `c++filt`:

        python3 tools/host/linuxsample.py --samples samples.txt --exe <the build's newton> --save tmp/rm-walk.json --top 40 [--callees NAME] [--callers NAME]

   prints the functions by self and inclusive time
   (`tools/host/stackreport.py`, as `profile.py --walk` prints them), and
   `--save` keeps the stacks for `profile.py --load` (or `linuxsample.py
   --load`) to be asked again.

Found with it (2026-10-03, branch `perf/drawing`): the tablet's profile of
a redraw has the same shape as the desktop's - the Cortex-A53 is about
twenty times slower than the development machine at everything, not at
anything in particular - except the display driver's conversion to grays,
18% of a redraw there against 11%.
