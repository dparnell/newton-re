cmake_minimum_required(VERSION 3.25)
project(NewtonOS CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Signed overflow wraps, as on the ARM the ROM's C was compiled for (its
# adds, subtracts and multiplies simply wrap): a ported ROM sum that
# overflows computes what the MessagePad computed instead of being undefined
# - which the zig toolchain's sanitiser stopped newton on (Rosetta's
# SegmentStrokeMinDistance, StrokePUD, TDTMFCodec::Produce) and an optimiser
# may assume never happens.  The sanitiser's other checks (shifts, bounds,
# alignment, division) stay.  The wrapping helpers (Add32, WrapAdd, the
# ULong32 casts) still say where the ROM relies on wrapping, but are no longer
# needed for correctness (docs/host-lp64.md 'Signed overflow wraps').
# -DNEWTON_WRAPV=OFF builds without it, so the sanitiser stops at each
# overflowing sum - the way to find where the ROM's arithmetic overflows.
option(NEWTON_WRAPV "signed overflow wraps (-fwrapv), as on the ARM" ON)
if(NEWTON_WRAPV)
    add_compile_options(-fwrapv)
endif()

# Which ROM this is a reconstruction of.  NEWTON_ROM is the directory in
# DebugRom/ and NEWTON_ROM_BUILD the extracted build directory; the tests
# and the host programs read the image and the extracted rom.bin through
# the two paths below rather than naming a ROM themselves.  Switching ROMs
# means changing these three lines, re-running
# tools/newton-rom/analysis/regenerate.py and recite.py, and setting the
# localisation the DDK headers are configured with.
set(NEWTON_ROM "MP2x00 US" CACHE STRING "the ROM directory under DebugRom/")
set(NEWTON_ROM_FILE "Senior CirrusNoDebug image" CACHE STRING "the AIF image in it")
set(NEWTON_ROM_BUILD "MP2x00US" CACHE STRING "the extract_rom.py output directory: a name under build/, or an absolute path (a worktree's build pointing at the checkout's)")
set(NEWTON_ROM_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/../DebugRom/${NEWTON_ROM}/${NEWTON_ROM_FILE}")
if(IS_ABSOLUTE "${NEWTON_ROM_BUILD}")
    set(NEWTON_ROM_BUILD_DIR "${NEWTON_ROM_BUILD}")
else()
    set(NEWTON_ROM_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/${NEWTON_ROM_BUILD}")
endif()
set(NEWTON_ROM_BIN "${NEWTON_ROM_BUILD_DIR}/rom.bin")

# The OS boots from the reconstructed data - the object file the build
# makes from the committed romsrc/ tree (host/CMakeLists.txt,
# NEWTON_OBJECTS_FILE) - so neither the ROM image nor its extraction is
# needed to build or run it.  Only the tests that check the reconstruction
# against the ROM need them: when build/${NEWTON_ROM_BUILD}/rom.bin is not
# there (a checkout without the ROM, or -DNEWTON_ROM_BUILD=<elsewhere>),
# every test that reads the image - by naming it on its command line, or by
# a program compiled with NEWTON_ROM_BIN/NEWTON_ROM_IMAGE - is simply not
# registered.  add_test and set_tests_properties are wrapped for that, so a
# new test is sorted by the same rule without anyone having to remember it.
# (the object file, which host/CMakeLists.txt builds; the unit tests that
#  need only the ROM's objects - fonts, locale bundles, prototypes - import it
#  too, compiled with NEWTON_OBJECTS, so they run without the ROM image)
set(NEWTON_OBJECTS_FILE "${CMAKE_BINARY_DIR}/romsrc-objects.bin")
if(EXISTS "${NEWTON_ROM_BIN}" AND EXISTS "${NEWTON_ROM_IMAGE}")
    set(NEWTON_HAVE_ROM TRUE)
else()
    set(NEWTON_HAVE_ROM FALSE)
    message(STATUS "no ROM image (${NEWTON_ROM_BIN}): the tests that check against the ROM are not registered")
    set_property(GLOBAL PROPERTY NEWTON_ROM_TESTS "")
    # an argument quoted for cmake_language(EVAL) as it came (a regular
    # expression's backslashes and a list's semicolons intact)
    function(_newton_quote out arg)
        string(REPLACE "\\" "\\\\" arg "${arg}")
        string(REPLACE "\"" "\\\"" arg "${arg}")
        string(REPLACE "$" "\\$" arg "${arg}")
        set(${out} "\"${arg}\"" PARENT_SCOPE)
    endfunction()
    function(add_test)
        set(needs FALSE)
        set(name "")
        set(program "")
        math(EXPR last "${ARGC} - 1")
        foreach(i RANGE 0 ${last})
            set(arg "${ARGV${i}}")
            string(FIND "${arg}" "${NEWTON_ROM_BUILD_DIR}" at)
            if(arg MATCHES "DebugRom/" OR NOT at EQUAL -1)
                set(needs TRUE)
            endif()
            if(i GREATER 0)
                math(EXPR prev "${i} - 1")
                if("${ARGV${prev}}" STREQUAL "NAME")
                    set(name "${arg}")
                elseif("${ARGV${prev}}" STREQUAL "COMMAND")
                    set(program "${arg}")
                endif()
            endif()
        endforeach()
        if(TARGET "${program}")
            get_target_property(defs "${program}" COMPILE_DEFINITIONS)
            if(defs MATCHES "NEWTON_ROM_(BIN|IMAGE)=")
                set(needs TRUE)
            endif()
        endif()
        if(needs)
            set_property(GLOBAL APPEND PROPERTY NEWTON_ROM_TESTS "${name}")
            return()
        endif()
        set(code "_add_test(")
        foreach(i RANGE 0 ${last})
            _newton_quote(q "${ARGV${i}}")
            string(APPEND code " ${q}")
        endforeach()
        cmake_language(EVAL CODE "${code})")
    endfunction()
    # (a test that needs a fixture only an unregistered test sets up is
    #  registered but disabled: it cannot run either)
    set_property(GLOBAL PROPERTY NEWTON_ROM_FIXTURES "")
    function(set_tests_properties)
        get_property(skipped GLOBAL PROPERTY NEWTON_ROM_TESTS)
        set(kept "")
        set(dropped FALSE)
        set(i 0)
        while(i LESS ARGC AND NOT "${ARGV${i}}" STREQUAL "PROPERTIES")
            if(NOT "${ARGV${i}}" IN_LIST skipped)
                list(APPEND kept "${ARGV${i}}")
            else()
                set(dropped TRUE)
            endif()
            math(EXPR i "${i} + 1")
        endwhile()
        set(disable FALSE)
        get_property(fixtures GLOBAL PROPERTY NEWTON_ROM_FIXTURES)
        math(EXPR last "${ARGC} - 2")
        foreach(j RANGE ${i} ${last})
            math(EXPR next "${j} + 1")
            if(dropped AND "${ARGV${j}}" STREQUAL "FIXTURES_SETUP")
                set_property(GLOBAL APPEND PROPERTY NEWTON_ROM_FIXTURES ${ARGV${next}})
            elseif(kept AND "${ARGV${j}}" STREQUAL "FIXTURES_REQUIRED")
                foreach(f IN LISTS ARGV${next})
                    if(f IN_LIST fixtures)
                        set(disable TRUE)
                    endif()
                endforeach()
            endif()
        endforeach()
        if(disable)
            _set_tests_properties(${kept} PROPERTIES DISABLED TRUE)
            # (and what it sets up is not there either)
            foreach(j RANGE ${i} ${last})
                math(EXPR next "${j} + 1")
                if("${ARGV${j}}" STREQUAL "FIXTURES_SETUP")
                    set_property(GLOBAL APPEND PROPERTY NEWTON_ROM_FIXTURES ${ARGV${next}})
                endif()
            endforeach()
        endif()
        if(kept)
            # (the test names first, then everything from PROPERTIES on)
            set(code "_set_tests_properties(${kept}")
            string(REPLACE ";" " " code "${code}")
            while(i LESS ARGC)
                _newton_quote(q "${ARGV${i}}")
                string(APPEND code " ${q}")
                math(EXPR i "${i} + 1")
            endwhile()
            string(APPEND code ")")
            cmake_language(EVAL CODE "${code}")
        endif()
    endfunction()
endif()

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
add_subdirectory(alert)
add_subdirectory(pcmcia)
add_subdirectory(power)
add_subdirectory(system)
add_subdirectory(intl)
add_subdirectory(assist)
add_subdirectory(text)
add_subdirectory(qd)
add_subdirectory(views)
add_subdirectory(print)
add_subdirectory(ink)
add_subdirectory(recognition)
add_subdirectory(testing)
add_subdirectory(books)
add_subdirectory(armcpu)
add_subdirectory(frames)
add_subdirectory(os600)
add_subdirectory(host)
add_subdirectory(newt)
