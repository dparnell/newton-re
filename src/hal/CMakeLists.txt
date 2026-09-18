# Hardware abstraction.  A port provides one implementation of each interface
# in this directory; the host port is the only one so far.
add_library(hal_host STATIC
    host/Atomic.cpp
    host/Interrupts.cpp
    host/Timer.cpp
    host/RealTimeClock.cpp
    host/System.cpp
    host/MMU.cpp
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
