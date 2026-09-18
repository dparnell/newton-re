add_library(os600_kernel STATIC
    Boot.cpp
    Domain.cpp
    DoubleQ.cpp
    Environment.cpp
    GenericSWI.cpp
    KernelGlobals.cpp
    KernelObjects.cpp
    MemObjManager.cpp
    MemObjTables.cpp
    MemArchManager.cpp
    Monitor.cpp
    ObjectManager.cpp
    ObjectTable.cpp
    Port.cpp
    RealTimeClock.cpp
    Reboot.cpp
    Scheduler.cpp
    Semaphore.cpp
    SingleQ.cpp
    SharedMem.cpp
    TimerEngine.cpp
    Task.cpp
    TaskSwitch.cpp
    host/TaskRuntime.cpp
)
target_link_libraries(os600_kernel PUBLIC newton_config hal_host toolbox os600_user memory)
target_include_directories(os600_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
