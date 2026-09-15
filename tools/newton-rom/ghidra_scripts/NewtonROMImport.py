# Apply Newton ROM layout + symbols (layout.json / symbols.json / rom.bin from
# tools/newton-rom) to the current program.  Interactive counterpart of
# import_rom.py for use from the Ghidra Script Manager.
#
# Setup: Script Manager -> Manage Script Directories -> add tools/newton-rom/ghidra_scripts.
# The current program must be rom.bin imported with the Binary loader at 0 as
# ARM:BE:32:v4 / apcs, *before* running auto-analysis (say "No" to the analysis
# prompt, run this script, then Analysis -> Auto Analyze).
#
# @category Newton
# @runtime PyGhidra

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

from newtonrom import ghidra_import  # noqa: E402

build_dir = askDirectory("Directory containing rom.bin, layout.json and symbols.json", "Select")  # noqa: F821
layout, symbols, rom = ghidra_import.load_inputs(str(build_dir))
ghidra_import.apply(currentProgram, layout, symbols, rom, monitor, println)  # noqa: F821
