# PCMCIA: the card sockets and what is in them - a card's CIS read into a
# TCardPCMCIA (CardCISIterator, PCMCIA20Parser, CardPCMCIA) over the socket
# the port provides (hal/CardSocket.h; the host's is hal_host_card), and the
# script side (FGetCardInfo, which the NewtonScript boot asks for before
# anything else can run), and the card server (CardServer.h: 'cdsv' and its
# card processor 'cdpr', the socket states, the handlers offered each card).
add_library(pcmcia STATIC
    CardInfo.cpp
    CardPartHandler.cpp
    CardPCMCIA.cpp
    CardCISIterator.cpp
    PCMCIA20Parser.cpp
    CISTables.cpp
    CardMessage.cpp
    CardPower.cpp
    CardServerGlobals.cpp
    CardServer.cpp
    CardProcessor.cpp
    NewtCardEvents.cpp
    CardAlerts.cpp
    CHMemModem.cpp
)
target_link_libraries(pcmcia PUBLIC newton_config frames package_manager hal_host_card flash alert)
target_include_directories(pcmcia PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
