# Hardware abstraction.  A port provides one implementation of each interface
# in this directory; the host port is the only one so far.
add_library(hal_host STATIC
    host/Atomic.cpp
    host/Interrupts.cpp
    host/Timer.cpp
    host/System.cpp
    host/MMU.cpp
)
target_link_libraries(hal_host PUBLIC newton_config toolbox)
