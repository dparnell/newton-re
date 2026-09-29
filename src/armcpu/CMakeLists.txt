# An interpreter of the StrongARM's instruction set (ARMCPU.h): ARMv4, ARM
# state, over a memory the caller supplies.  No Newton dependencies - the
# adapter that binds it to the frames world (package natives) is
# PackageNativeCPU.h, in armcpu_frames.
add_library(armcpu STATIC
    ARMCPU.cpp
)
target_include_directories(armcpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
