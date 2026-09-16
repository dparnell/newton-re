cmake_minimum_required(VERSION 3.25)
project(NewtonOS CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The DDK headers are configured exactly as for the MP2100 D ROM build
# (see tools/newton-rom/newtonrom/headers.py), minus __arm.
add_library(newton_config INTERFACE)
target_include_directories(newton_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/ddk ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(newton_config INTERFACE forQ=1 forGerman=1)
target_compile_options(newton_config INTERFACE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/host/host_compat.h
    -Wall -Wno-unknown-pragmas -Wno-return-type-c-linkage -Wno-mismatched-tags
    -Wno-invalid-offsetof)   # intrusive queues take offsetof() of non-POD kernel objects by design

enable_testing()
add_subdirectory(toolbox)
add_subdirectory(hal)
add_subdirectory(utility)
add_subdirectory(memory)
add_subdirectory(protocols)
add_subdirectory(compression)
add_subdirectory(frames)
add_subdirectory(os600)
