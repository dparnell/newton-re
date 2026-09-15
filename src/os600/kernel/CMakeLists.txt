add_library(os600_kernel STATIC
    Domain.cpp
    DoubleQ.cpp
    Environment.cpp
    KernelGlobals.cpp
    KernelObjects.cpp
    MemArchManager.cpp
    Monitor.cpp
    ObjectManager.cpp
    ObjectTable.cpp
    Port.cpp
    Reboot.cpp
    Scheduler.cpp
    Semaphore.cpp
    SharedMem.cpp
    TimerEngine.cpp
    Task.cpp
)
target_link_libraries(os600_kernel PUBLIC newton_config hal_host toolbox os600_user)
target_include_directories(os600_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
