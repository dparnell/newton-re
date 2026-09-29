# The memory manager: the Skia heap behind NewPtr/NewHandle and the VM heaps,
# the safe heap (the kernel's page-based heap), and NewtonMemory.h's API over
# them.  memory/host stands in for the page manager (the kernel heap).
add_library(memory STATIC
    SkiaHeap.cpp
    SafeHeap.cpp
    MemoryManager.cpp
    MemoryAllocator.cpp
    host/KernelHeap.cpp
)
target_link_libraries(memory PUBLIC newton_config os600_user)
target_include_directories(memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
