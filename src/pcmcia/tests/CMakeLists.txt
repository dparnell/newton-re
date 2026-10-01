# a card's CIS read from the host's card: the blank flash card, a CIS with
# every tuple the parser keeps, a checksum, a blank card
add_executable(test_CardCIS test_CardCIS.cpp)
target_link_libraries(test_CardCIS PRIVATE pcmcia os600_kernel os600_user)
add_test(NAME pcmcia.CardCIS COMMAND test_CardCIS)
set_tests_properties(pcmcia.CardCIS PROPERTIES TIMEOUT 60)

# a memory card from the socket to its store: the handler, the card's
# TFlash, a TFlashStore formatted on it and read back after a reinsertion
add_executable(test_MemoryCard test_MemoryCard.cpp)
target_link_libraries(test_MemoryCard PRIVATE pcmcia flash os600_kernel os600_user)
add_test(NAME pcmcia.MemoryCard COMMAND test_MemoryCard)
set_tests_properties(pcmcia.MemoryCard PROPERTIES TIMEOUT 60)

# the card server: a card put in, recognised and announced ('card' system
# event), taken out (the application told, the handler removed), put back
add_executable(test_CardServer test_CardServer.cpp)
target_link_libraries(test_CardServer PRIVATE pcmcia flash os600_kernel os600_user)
target_compile_definitions(test_CardServer PRIVATE "NEWTON_OBJECTS=\"${NEWTON_OBJECTS_FILE}\"")
add_test(NAME pcmcia.CardServer COMMAND test_CardServer)
set_tests_properties(pcmcia.CardServer PROPERTIES TIMEOUT 60)

# an ATA card over the host's model of one: TATASimple reading, writing
# and identifying the drive, the loader's partition map (plain, and in a
# PC partition); the images made by tools/cards/atacard.py
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_executable(test_ATACard test_ATACard.cpp)
    target_link_libraries(test_ATACard PRIVATE pcmcia os600_kernel os600_user)
    add_test(NAME pcmcia.ATACard.make
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/cards/atacard.py make
                     ${CMAKE_CURRENT_BINARY_DIR}/ata-plain.card --size 4)
    add_test(NAME pcmcia.ATACard.makembr
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/cards/atacard.py make
                     ${CMAKE_CURRENT_BINARY_DIR}/ata-mbr.card --size 4 --mbr)
    set_tests_properties(pcmcia.ATACard.make pcmcia.ATACard.makembr PROPERTIES FIXTURES_SETUP pcmcia_ata_cards)
    add_test(NAME pcmcia.ATACard
             COMMAND test_ATACard ${CMAKE_CURRENT_BINARY_DIR}/ata-plain.card ${CMAKE_CURRENT_BINARY_DIR}/ata-mbr.card)
    set_tests_properties(pcmcia.ATACard PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED pcmcia_ata_cards
                         PASS_REGULAR_EXPRESSION "test_ATACard: all passed")
endif()
