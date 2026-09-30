# a card's CIS read from the host's card: the blank flash card, a CIS with
# every tuple the parser keeps, a checksum, a blank card
add_executable(test_CardCIS test_CardCIS.cpp)
target_link_libraries(test_CardCIS PRIVATE pcmcia os600_kernel os600_user)
add_test(NAME pcmcia.CardCIS COMMAND test_CardCIS)
set_tests_properties(pcmcia.CardCIS PROPERTIES TIMEOUT 60)
