cmake_minimum_required(VERSION 3.25)
project(NewtonOS CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Which ROM this is a reconstruction of.  NEWTON_ROM is the directory in
# DebugRom/ and NEWTON_ROM_BUILD the extracted build directory; the tests
# and the host programs read the image and the extracted rom.bin through
# the two paths below rather than naming a ROM themselves.  Switching ROMs
# means changing these three lines, re-running
# tools/newton-rom/analysis/regenerate.py and recite.py, and setting the
# localisation the DDK headers are configured with.
set(NEWTON_ROM "MP2x00 US" CACHE STRING "the ROM directory under DebugRom/")
set(NEWTON_ROM_FILE "Senior CirrusNoDebug image" CACHE STRING "the AIF image in it")
set(NEWTON_ROM_BUILD "MP2x00US" CACHE STRING "the extract_rom.py output directory under build/")
set(NEWTON_ROM_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/../DebugRom/${NEWTON_ROM}/${NEWTON_ROM_FILE}")
set(NEWTON_ROM_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../build/${NEWTON_ROM_BUILD}/rom.bin")

# The DDK headers are configured exactly as for that ROM build
# (see tools/newton-rom/newtonrom/headers.py), minus __arm.
add_library(newton_config INTERFACE)
target_include_directories(newton_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/ddk ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(newton_config INTERFACE forQ=1)
target_compile_options(newton_config INTERFACE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/host/host_compat.h
    -Wall -Wno-unknown-pragmas -Wno-return-type-c-linkage -Wno-mismatched-tags
    -Wno-invalid-offsetof)   # intrusive queues take offsetof() of non-POD kernel objects by design

# The reconstruction's static libraries are mutually dependent by design
# (an area's code and the areas it talks to: frames <-> intl for the number
# text, utility <-> os600/user for TAppWorld), and GNU ld makes a single
# pass over each archive on the link line, so a member pulled in late has
# nothing left to resolve it against.  It is told to rescan by bracketing
# the libraries in a group.
#
# lld - which the zig toolchain's clang uses, and so the Windows build -
# keeps every archive member available for the whole link and needs no
# group; zig 0.16's linker in fact falls over (signal 11) when it is given
# one, so the group goes on only where the linker is GNU's.  (Apple's ld64
# resolves archives repeatedly as lld does, so none there either.)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NEWTON_LINK_GROUP FALSE)
    if(CMAKE_CXX_COMPILER_LINKER_ID)            # CMake 3.29 and later: GNU, GNUgold, LLD, MOLD, ...
        if(CMAKE_CXX_COMPILER_LINKER_ID MATCHES "^GNU")
            set(NEWTON_LINK_GROUP TRUE)
        endif()
    elseif(NOT "${CMAKE_CXX_COMPILER}" MATCHES "zig")
        set(NEWTON_LINK_GROUP TRUE)             # cannot ask: assume the system's ld
    endif()
    if(NEWTON_LINK_GROUP)
        foreach(lang C CXX)
            foreach(rule LINK_EXECUTABLE CREATE_SHARED_LIBRARY CREATE_SHARED_MODULE)
                if(DEFINED CMAKE_${lang}_${rule})
                    string(REPLACE "<LINK_LIBRARIES>" "-Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group"
                           CMAKE_${lang}_${rule} "${CMAKE_${lang}_${rule}}")
                endif()
            endforeach()
        endforeach()
    endif()
endif()

enable_testing()
add_subdirectory(toolbox)
add_subdirectory(hal)
add_subdirectory(utility)
add_subdirectory(memory)
add_subdirectory(protocols)
add_subdirectory(compression)
add_subdirectory(sound)
add_subdirectory(comms)
add_subdirectory(thirdparty)
add_subdirectory(stores)
add_subdirectory(packages)
add_subdirectory(pcmcia)
add_subdirectory(system)
add_subdirectory(intl)
add_subdirectory(assist)
add_subdirectory(text)
add_subdirectory(qd)
add_subdirectory(views)
add_subdirectory(ink)
add_subdirectory(recognition)
add_subdirectory(testing)
add_subdirectory(books)
add_subdirectory(armcpu)
add_subdirectory(frames)
add_subdirectory(os600)
add_subdirectory(host)
add_subdirectory(newt)
