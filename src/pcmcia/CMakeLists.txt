# PCMCIA: the card sockets and what is in them - a card's CIS read into a
# TCardPCMCIA (CardCISIterator, PCMCIA20Parser, CardPCMCIA) over the socket
# the port provides (hal/CardSocket.h; the host's is hal_host_card), and the
# script side (FGetCardInfo, which the NewtonScript boot asks for before
# anything else can run).  NOT YET: the card server (TCardServer, the socket
# states, the card handlers).
add_library(pcmcia STATIC
    CardInfo.cpp
    CardPartHandler.cpp
    CardPCMCIA.cpp
    CardCISIterator.cpp
    PCMCIA20Parser.cpp
    CISTables.cpp
)
target_link_libraries(pcmcia PUBLIC newton_config frames package_manager hal_host_card)
target_include_directories(pcmcia PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
