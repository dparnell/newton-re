# Communications: the option arrays the framework is configured through
# (docs/comms/README.md for the plan: the comm tools, the comm manager,
# the endpoints and the NewtonScript endpoint over them, and the host's
# network behind a host comm tool).
add_library(comms STATIC
    Options.cpp
    CommTools.cpp
    CommManager.cpp
    host/HostTCPTool.cpp
    host/HostServices.cpp
)
# hal_host_sockets: the host's network, which host/HostTCPTool.cpp (the
# stand-in for the NIE's TCP tool) works over
target_link_libraries(comms PUBLIC newton_config os600_user memory utility hal_host_sockets)
target_include_directories(comms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/host)

add_subdirectory(tests)
