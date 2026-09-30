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
