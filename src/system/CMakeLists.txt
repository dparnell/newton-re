# The machine's own NewtonScript functions (SystemNatives.h): the serial
# number now, the battery, the backlight and the power switch when the boot
# asks for them.  Frames code over hal/System.h, so a port supplies the
# machine and this supplies the script's view of it.
add_library(system STATIC
    SystemNatives.cpp
    ConfigServer.cpp
)
target_link_libraries(system PUBLIC newton_config frames qd views hal_host os600_user)
target_include_directories(system PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
