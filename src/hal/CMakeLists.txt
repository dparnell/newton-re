# Hardware abstraction.  A port provides one implementation of each interface
# in this directory; the host port is the only one so far.
add_library(hal_host STATIC
    host/Atomic.cpp
    host/Interrupts.cpp
    host/Timer.cpp
    host/HostIdleWait.cpp
    host/RealTimeClock.cpp
    host/System.cpp
    host/MMU.cpp
    host/Power.cpp
    host/HostInterruptSources.cpp
    host/Flash.cpp
    host/HostFlash.cpp
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

# The host's tablet: its driver (a TTabletDriver sampling the window's pen
# through a host interrupt source, calibrated as the ROM's), the test pen's
# records put straight into the recogniser's tablet buffer
# (recognition/TabletBuffer.h), and the wait hook that stands in for the
# inker task without the OS.
add_library(hal_host_tablet STATIC
    host/HostTablet.cpp
    host/HostTabletDriver.cpp
)
target_link_libraries(hal_host_tablet PUBLIC newton_config recognition os600_user hal_host qd)
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

# The cellular modem engine (host/HostCellular.h): a SIMCom module driven
# in AT commands over a byte link the host gives it - a board's UART, or a
# desktop's TCP connection to tools/modem/fakecell.py (HostCellularSocket.cpp,
# newton --cell-port).  Built without the Newton include paths: std::thread
# and std::mutex reach <locale.h>, which src/intl/Locale.h would shadow.
add_library(hal_host_cellular STATIC
    host/HostCellular.cpp
    host/HostCellularSocket.cpp
)
target_link_libraries(hal_host_cellular PUBLIC newton_config hal_host_sockets)
target_include_directories(hal_host_cellular PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(hal_host_cellular PUBLIC Threads::Threads)
endif()

# The GPS receiver (host/HostGPS.h): NMEA 0183 from a pretend receiver, a
# log, a TCP server or gpsd, or a receiver plugged into the host, on a
# thread of its own - at the far end of serial port 2 or the external one while the
# Host panel has it plugged in (newton --gps).  Built as the cellular
# engine is, without the Newton include paths.
add_library(hal_host_gps STATIC
    host/HostGPS.cpp
)
target_link_libraries(hal_host_gps PUBLIC newton_config hal_host_sockets)
target_include_directories(hal_host_gps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)
if(Threads_FOUND)
    target_link_libraries(hal_host_gps PUBLIC Threads::Threads)
endif()

# The LoRa mesh engine (host/HostMesh.h): a MeshCore chat node
# (thirdparty/meshcore) on the radio the host gives it - a board's SX1262,
# or a desktop's pretend one whose air is a UDP multicast group
# (HostMeshAir.cpp, newton --mesh-air).  Built as the cellular engine is,
# without the Newton include paths' sources around it.
add_library(hal_host_mesh STATIC
    host/HostMesh.cpp
    host/HostMeshAir.cpp
)
target_link_libraries(hal_host_mesh PUBLIC newton_config hal_host_sockets meshcore)
target_include_directories(hal_host_mesh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)
if(Threads_FOUND)
    target_link_libraries(hal_host_mesh PUBLIC Threads::Threads)
endif()
# meshpeer: a node on that air for the Mesh application and the tests to
# talk to (host/MeshPeer.cpp; tools/mesh/meshrun.py runs nodes together)
add_executable(meshpeer host/MeshPeer.cpp)
target_link_libraries(meshpeer PRIVATE hal_host_mesh)

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
# is a cable plugged in (Einstein's port, 3679, by default) - and the
# built-in IR (host/HostIRChip.h), whose medium is a TCP connection to
# another host Newton; and serial port 2 ('mdem'), a second chip for the
# GPS receiver (host/HostGPS.h).
add_library(hal_host_serial STATIC
    host/HostSerialChip.cpp
    host/HostIRChip.cpp
)
target_link_libraries(hal_host_serial PUBLIC newton_config hal_serial hal_host hal_host_sockets hal_host_gps)
target_include_directories(hal_host_serial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host)

# The host's PC card (host/HostCard.h: a card image in Einstein's container
# put into the socket and taken out) and the socket over it
# (host/HostCardSocket.cpp: TCardSocket, CardSocket.h, whose ROM
# implementation drives the Voyager's PCMCIA controller), with the card bus
# (CardBus.h) and the model of an ATA card behind it (host/HostATA.cpp).
add_library(hal_host_card STATIC
    host/HostCard.cpp
    host/HostCardSocket.cpp
    host/HostATA.cpp
)
target_link_libraries(hal_host_card PUBLIC newton_config hal_host os600_user)
target_include_directories(hal_host_card PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/host)

add_subdirectory(tests)

# The machine's timers the serial tools use (FIQTimer.h: the fast timers
# multiplexed over the free-running timer's match interrupt; the DDK's
# TDelayTimer's busy waits), over the host's clock (host/HostFIQTimer.cpp).
add_library(hal_timers STATIC
    FIQTimer.cpp
    DelayTimer.cpp
    host/HostFIQTimer.cpp
)
target_link_libraries(hal_timers PUBLIC newton_config hal_host)
target_include_directories(hal_timers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/host)
