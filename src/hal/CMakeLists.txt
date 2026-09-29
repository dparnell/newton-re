# Hardware abstraction.  A port provides one implementation of each interface
# in this directory; the host port is the only one so far.
add_library(hal_host STATIC
    host/Atomic.cpp
    host/Interrupts.cpp
    host/Timer.cpp
    host/RealTimeClock.cpp
    host/System.cpp
    host/MMU.cpp
    host/Power.cpp
    host/HostInterruptSources.cpp
)
target_link_libraries(hal_host PUBLIC newton_config toolbox)

# The host's display: a TScreenDriver (qd/Screen.h) keeping gray bytes it
# can write out as an image.  A library of its own: it needs the protocols
# (which need the kernel, which needs hal_host).
add_library(hal_host_display STATIC
    host/HostScreen.cpp
)
target_link_libraries(hal_host_display PUBLIC newton_config protocols qd)
target_include_directories(hal_host_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)

# The host's tablet: pen records put into the recogniser's tablet buffer
# (recognition/TabletBuffer.h) from a window or a test, and the wait hook
# that stands in for the inker task.
add_library(hal_host_tablet STATIC
    host/HostTablet.cpp
)
target_link_libraries(hal_host_tablet PUBLIC newton_config recognition os600_user)
target_include_directories(hal_host_tablet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)

# The host's sound output: a PSoundDriver (sound/SoundDriver.h) whose
# buffers end as host interrupt sources, keeping what was played for tests.
add_library(hal_host_sound STATIC
    host/HostSoundDriver.cpp
)
target_link_libraries(hal_host_sound PUBLIC newton_config sound hal_host)
target_include_directories(hal_host_sound PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)

# The host's network (host/HostSockets.h): TCP connections and name
# resolution over the host's own sockets, for the comm tools that stand in
# for the Newton Internet Enabler's (docs/comms/README.md).  Built without
# the Newton include paths - the platform's socket headers and the DDK's
# names do not mix - so it is a plain C interface over integer handles.
add_library(hal_host_sockets STATIC
    host/HostSockets.cpp
)
target_include_directories(hal_host_sockets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)
if(WIN32)
    target_link_libraries(hal_host_sockets PUBLIC ws2_32)
endif()

# The serial chips' seam (HALSerialChip.h): the TSerialChip and
# PSerialChipRegistry protocols and the ROM's registry, PTheSerChipRegistry;
# the serial tools find their chip here (docs/comms/README.md, "The desktop
# connection").  Over the options (comms) and the name server (os600_user).
add_library(hal_serial STATIC
    SerialChipRegistry.cpp
)
target_link_libraries(hal_serial PUBLIC newton_config protocols comms os600_user)
target_include_directories(hal_serial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The host's serial port (host/HostSerialChip.h): a TSerialChip registered
# as the external port whose wire is a TCP socket - a desktop that connects
# is a cable plugged in (Einstein's port, 3679, by default).
add_library(hal_host_serial STATIC
    host/HostSerialChip.cpp
)
target_link_libraries(hal_host_serial PUBLIC newton_config hal_serial hal_host hal_host_sockets)
target_include_directories(hal_host_serial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)

add_subdirectory(tests)
