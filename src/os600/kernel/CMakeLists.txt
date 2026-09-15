add_library(os600_kernel STATIC
    DoubleQ.cpp
    KernelGlobals.cpp
    KernelObjects.cpp
    ObjectTable.cpp
    Scheduler.cpp
    Semaphore.cpp
    SharedMem.cpp
    TimerEngine.cpp
    Task.cpp
)
target_link_libraries(os600_kernel PUBLIC newton_config hal_host toolbox)
target_include_directories(os600_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
