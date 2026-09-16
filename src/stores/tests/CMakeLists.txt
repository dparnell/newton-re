add_executable(test_Store test_Store.cpp)
target_link_libraries(test_Store PRIVATE stores os600_kernel os600_user)
add_test(NAME stores.Store COMMAND test_Store)
set_tests_properties(stores.Store PROPERTIES TIMEOUT 60)

add_executable(test_StoreWrapper test_StoreWrapper.cpp)
target_link_libraries(test_StoreWrapper PRIVATE stores)
add_test(NAME stores.StoreWrapper COMMAND test_StoreWrapper)
set_tests_properties(stores.StoreWrapper PROPERTIES TIMEOUT 60)
