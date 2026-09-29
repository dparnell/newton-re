# Communications: the option arrays the framework is configured through
# (docs/comms/README.md for the plan: the comm tools, the comm manager,
# the endpoints and the NewtonScript endpoint over them, and the host's
# network behind a host comm tool).
add_library(comms STATIC
    Options.cpp
    CommTools.cpp
    CommManager.cpp
    Endpoint.cpp
    SerialEndpoint.cpp
    host/HostTCPTool.cpp
    host/HostServices.cpp
    host/HostEchoServer.cpp
)
# hal_host_sockets: the host's network, which host/HostTCPTool.cpp (the
# stand-in for the NIE's TCP tool) works over
target_link_libraries(comms PUBLIC newton_config os600_user memory utility hal_host_sockets)
target_include_directories(comms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/host)

# The NewtonScript side: the frame translators (values and option frames
# to and from bytes and option arrays) and the NewtonScript endpoint.
add_library(comms_script STATIC
    Translators.cpp
    NewScriptEndpoint.cpp
)
# newt: ExceptionNotify and CheckForDeferredActions; views: the root view
# brought up to date after an endpoint event
target_link_libraries(comms_script PUBLIC comms frames views newt)

add_subdirectory(tests)
