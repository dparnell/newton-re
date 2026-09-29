# Communications: the option arrays the framework is configured through
# (docs/comms/README.md for the plan: the comm tools, the comm manager,
# the endpoints and the NewtonScript endpoint over them, and the host's
# network behind a host comm tool).
add_library(comms STATIC
    Options.cpp
    SerialOptions.cpp
    CommTools.cpp
    CommManager.cpp
    Endpoint.cpp
    SerialEndpoint.cpp
    EndpointPipe.cpp
    host/HostTCPTool.cpp
    host/HostServices.cpp
    host/HostEchoServer.cpp
    host/HostDNSTool.cpp
    host/HostLinkTool.cpp
)
# hal_host_sockets: the host's network, which host/HostTCPTool.cpp (the
# stand-in for the NIE's TCP tool) works over
target_link_libraries(comms PUBLIC newton_config os600_user memory utility hal_host_sockets)
target_include_directories(comms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/host)

# The serial tools (SerialTool.h): TSerTool, TAsyncSerTool and the 'aser
# service, over a serial chip from the registry (hal_serial) and the
# machine's timers (hal_timers); MNP; and the IR tools (SharpIRTool.h: Sharp
# IR, 'slir').
add_library(comms_serial STATIC
    SerialTool.cpp
    AsyncSerialTool.cpp
    FramedSerialTool.cpp
    MNP.cpp
    MNPClass5.cpp
    MNPClass5Tables.cpp
    V42bis.cpp
    SharpIRTool.cpp
)
target_link_libraries(comms_serial PUBLIC comms hal_serial hal_timers)

# The NewtonScript side: the frame translators (values and option frames
# to and from bytes and option arrays) and the NewtonScript endpoint.
# host/HostLink.ns, the host's link for the NIE, is compiled in as a string
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostLinkSource.cpp
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/host/HostLink.ns
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/HostLinkSource.cpp -DNAME=gHostLinkSource
            -P ${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/host/HostLink.ns ${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake
    VERBATIM)
add_library(comms_script STATIC
    Translators.cpp
    HostOptionLayouts.cpp
    NewScriptEndpoint.cpp
    StreamingEndpoint.cpp
    host/HostLink.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/HostLinkSource.cpp
)
# newt: ExceptionNotify and CheckForDeferredActions; views: the root view
# brought up to date after an endpoint event
target_link_libraries(comms_script PUBLIC comms frames stores views newt)

# The dock (docs/comms/README.md, "The desktop connection"): the easy
# endpoint pipe the docker talks through, made from the Connection
# application's options frame (so over comms_script's translators), the
# modem navigator every such endpoint goes through, and the docker - the
# 'dock' protocol's session and its natives (over packages, for a package
# the desktop loads, and newt, whose world it forks).
add_library(comms_dock STATIC
    EzEndpointPipe.cpp
    ModemNavigator.cpp
    Docker.cpp
)
target_link_libraries(comms_dock PUBLIC comms_serial comms_script packages stores newt intl qd utility)

add_subdirectory(tests)
