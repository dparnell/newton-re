# Use the zig toolchain (clang/LLVM, bundled libc) for a host build:
#   cmake -G Ninja -S src -B build/host -DCMAKE_TOOLCHAIN_FILE=src/cmake/zig-toolchain.cmake
# Set ZIG_TARGET (e.g. x86_64-linux-gnu, i386-linux-gnu) to cross-compile.
set(CMAKE_C_COMPILER zig cc)
set(CMAKE_CXX_COMPILER zig c++)
# no -Xlinker --dependency-file: CMake asks for one on a Linux build machine,
# and zig's linker crashed on it linking newton (segmentation fault, zig
# 0.16 - found building the reMarkable's newton on Linux for the release
# workflow, docs/releases.md)
set(CMAKE_LINK_DEPENDS_USE_LINKER OFF)
if(DEFINED ZIG_TARGET)
    set(CMAKE_C_COMPILER_TARGET ${ZIG_TARGET})
    set(CMAKE_CXX_COMPILER_TARGET ${ZIG_TARGET})
else()
    # Without a target zig compiles for the build machine's own CPU (AVX,
    # AVX2, AVX-512, BMI...), and the binary dies at once with an illegal
    # instruction on a CPU without them - a user's Celeron laptop ran none of
    # the Windows releases, which GitHub's runners built. ZIG_CPU=baseline
    # (plain x86-64, SSE2) runs anywhere; -DZIG_CPU=native for a build that
    # never leaves this machine (docs/releases.md).
    set(ZIG_CPU baseline CACHE STRING "zig -mcpu for a native build: baseline (portable) or native")
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ZIG_CPU)
    string(APPEND CMAKE_C_FLAGS_INIT " -mcpu=${ZIG_CPU}")
    string(APPEND CMAKE_CXX_FLAGS_INIT " -mcpu=${ZIG_CPU}")
endif()
if(CMAKE_HOST_WIN32)
    set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ar.cmd CACHE FILEPATH "" FORCE)
    set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ranlib.cmd CACHE FILEPATH "" FORCE)
else()
    set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ar.sh CACHE FILEPATH "" FORCE)
    set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ranlib.sh CACHE FILEPATH "" FORCE)
endif()
