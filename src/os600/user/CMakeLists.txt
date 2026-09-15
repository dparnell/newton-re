# The user-mode side of OS600: what runs in a task, above the system-call
# interface.  On a host the system calls are direct calls into the kernel
# through the task runtime (docs/host-runtime.md).
add_library(os600_user STATIC
    UserObjects.cpp
    UserSharedMem.cpp
    UserPorts.cpp
    UserSemaphore.cpp
    UserMonitor.cpp
    UserTasks.cpp
    UserDomain.cpp
    UserEnvironment.cpp
    BuildEnvironments.cpp
    Exceptions.cpp
    ExceptionNames.cpp
    UserBoot.cpp
    NameServer.cpp
    UserNameServer.cpp
    SystemEvents.cpp
    UserGestalt.cpp
    UserTime.cpp
    Loader.cpp
    host/StackManager.cpp
    host/MonitorGlue.cpp
    host/SWI.cpp
    host/GenericSWIStub.cpp
)
target_link_libraries(os600_user PUBLIC newton_config os600_kernel utility)
target_include_directories(os600_user PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
