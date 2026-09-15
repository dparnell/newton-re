# The user-mode side of OS600: what runs in a task, above the system-call
# interface.  For now only the entry points the kernel needs the addresses of.
add_library(os600_user STATIC
    host/MonitorGlue.cpp
)
target_link_libraries(os600_user PUBLIC newton_config)
target_include_directories(os600_user PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
