# Use the zig toolchain (clang/LLVM, bundled libc) for a host build:
#   cmake -G Ninja -S src -B build/host -DCMAKE_TOOLCHAIN_FILE=src/cmake/zig-toolchain.cmake
# Set ZIG_TARGET (e.g. x86_64-linux-gnu, i386-linux-gnu) to cross-compile.
set(CMAKE_C_COMPILER zig cc)
set(CMAKE_CXX_COMPILER zig c++)
if(DEFINED ZIG_TARGET)
    set(CMAKE_C_COMPILER_TARGET ${ZIG_TARGET})
    set(CMAKE_CXX_COMPILER_TARGET ${ZIG_TARGET})
endif()
if(CMAKE_HOST_WIN32)
    set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ar.cmd CACHE FILEPATH "" FORCE)
    set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ranlib.cmd CACHE FILEPATH "" FORCE)
else()
    set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ar.sh CACHE FILEPATH "" FORCE)
    set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ranlib.sh CACHE FILEPATH "" FORCE)
endif()
