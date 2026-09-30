add_executable(test_Flash test_Flash.cpp)
target_link_libraries(test_Flash PRIVATE flash os600_kernel os600_user)
add_test(NAME stores.Flash COMMAND test_Flash)
set_tests_properties(stores.Flash PROPERTIES TIMEOUT 60)

add_executable(test_FlashStore test_FlashStore.cpp)
target_link_libraries(test_FlashStore PRIVATE flash os600_kernel os600_user)
add_test(NAME stores.FlashStore COMMAND test_FlashStore)
set_tests_properties(stores.FlashStore PROPERTIES TIMEOUT 120)

add_executable(test_HostFlash test_HostFlash.cpp)
target_link_libraries(test_HostFlash PRIVATE flash os600_kernel os600_user)
add_test(NAME stores.HostFlash COMMAND test_HostFlash)
set_tests_properties(stores.HostFlash PROPERTIES TIMEOUT 300)
