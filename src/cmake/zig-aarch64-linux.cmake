# Cross-compile for an aarch64 Linux host with zig - the reMarkable Paper Pro
# (docs/host-remarkable.md), or any 64-bit ARM Linux:
#   cmake -G Ninja -S src -B <dir> -DCMAKE_TOOLCHAIN_FILE=src/cmake/zig-aarch64-linux.cmake
#         -DNEWTON_HOST_NEWTONSCRIPT=<a newtonscript built for the build machine>
# The glibc the program is linked against is the oldest it will run on;
# 2.31 is well below the Paper Pro's (and any current distribution's), and
# zig links its libc++ statically, so the program needs nothing on the
# device but the C library.  ZIG_GLIBC picks another.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
if(NOT DEFINED ZIG_GLIBC)
    set(ZIG_GLIBC 2.31)
endif()
set(ZIG_TARGET aarch64-linux-gnu.${ZIG_GLIBC})
include(${CMAKE_CURRENT_LIST_DIR}/zig-toolchain.cmake)
# char is unsigned on aarch64 Linux and signed on every host the
# reconstruction was written on (x86, Apple arm64): keep it signed
# (docs/host-remarkable.md, "char")
set(CMAKE_C_FLAGS_INIT "-fsigned-char -fPIE")
set(CMAKE_CXX_FLAGS_INIT "-fsigned-char -fPIE")
# and a position-independent program, loaded high: zig links one that is
# not at 0x200000, where its functions' addresses fall below 0x02000000 and
# are taken for ROM jump-table addresses (frames/NativeFunctions.h's
# kROMCodeLimit) - every host native then "not reconstructed"
set(CMAKE_EXE_LINKER_FLAGS_INIT "-pie")
# a Release build stripped: zig keeps the debug information otherwise (38 MB
# against 7 MB for newton), and zig objcopy cannot strip; a build to debug
# on the tablet (stacksample, a crash's C stack) is not a Release one
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s")
# programs and libraries of the build machine's, never the target's
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
