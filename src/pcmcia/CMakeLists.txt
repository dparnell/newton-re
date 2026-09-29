# PCMCIA: the card sockets and what is in them.  Only the script side of it
# so far - FGetCardInfo, which the NewtonScript boot asks for before anything
# else can run - because the card server itself (TCardServer, the socket
# states, the CIS tuples) is not reconstructed and a host has no card
# hardware anyway.
add_library(pcmcia STATIC
    CardInfo.cpp
    CardPartHandler.cpp
)
target_link_libraries(pcmcia PUBLIC newton_config frames package_manager)
target_include_directories(pcmcia PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
