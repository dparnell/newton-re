"""newtonrom - tooling for extracting and annotating Apple Newton MP2x00 ROM images.

Modules:
    aif        - parser for the ARM Image Format (AIF) container the debug ROMs ship in
    symbols    - reader for the AIF low-level debug symbol table
    rex        - parser for ROM Extension (REx) blocks ("... high" files)
    jumptable  - decoder for the patchable jump table (ROM 0x2000 <-> virtual 0x01A00000)
    demangle   - demangler for the cfront/ARM-style C++ names used by the Newton toolchain
"""

__version__ = "0.1.0"
