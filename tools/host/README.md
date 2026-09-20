# tools/host

Tools for working with the host build of the reconstruction (`build/host`),
as opposed to the ROM itself (`tools/newton-rom/`).

## whichfunction.py - name the function a crash address is in

When the host falls over, `src/host/newton.cpp`'s crash handler prints the
faulting address twice: as it stood in memory, and as an offset into the
executable's own image.

    [host] the machine fell over: an exception (0xc0000005) at 00007FF65A985265 (image + 0x175265)

The first is useless on its own - Windows loads the image wherever it likes,
so it changes from run to run - but the second is fixed for a given build.
This tool turns it into a function name:

    python tools/host/whichfunction.py build/host/host/newton.exe 0x175265

    the function runs 0x174ea0-0x1752de (1086 bytes); the fault is 0x3c5 into it
    _ZN19TRecognitionManager4IdleEv
        build\host\recognition\CMakeFiles\recognition.dir\Recognizer.cpp.obj

**Inputs:** the executable that crashed, and the image offset from its crash
line. `--objs DIR` says where the object files are (default: the `build`
directory beside the executable, searched recursively for `*.obj`).

**Output:** the bounds of the function the offset falls in, how far into it
the fault is, the mangled name, and the object file it came from. Nothing is
written; it exits non-zero when no name can be found.

**How it works.** The linked executable has no symbol table, and the PDB is
not worth parsing here, so the name comes from the object files:

  * `.pdata` - the table Windows unwinds through - gives the bounds of the
    function containing the offset;
  * the object files still have their COFF symbol tables, so every function
    in them has a name and a place;
  * a function's bytes in the executable are its bytes in the object file,
    except where the linker patched a relocation - and each object records
    where its own relocation sites are.

So it takes the function's bytes out of the executable and looks for an
object-file symbol whose bytes match everywhere the object has no relocation.
A whole function matching to the byte is not a coincidence.

It needs nothing but the Python standard library, and works on any of the
host executables (`newton.exe`, `newtonscript.exe`, one of the tests). It is
Windows/COFF only, which is what the crash handler it serves is.
