add_executable(test_Store test_Store.cpp)
target_link_libraries(test_Store PRIVATE stores os600_kernel os600_user)
add_test(NAME stores.Store COMMAND test_Store)
set_tests_properties(stores.Store PROPERTIES TIMEOUT 60)

add_executable(test_StoreWrapper test_StoreWrapper.cpp)
target_link_libraries(test_StoreWrapper PRIVATE stores)
add_test(NAME stores.StoreWrapper COMMAND test_StoreWrapper)
set_tests_properties(stores.StoreWrapper PROPERTIES TIMEOUT 60)

add_executable(test_SoupIndex test_SoupIndex.cpp)
target_link_libraries(test_SoupIndex PRIVATE stores)
add_test(NAME stores.SoupIndex COMMAND test_SoupIndex)
set_tests_properties(stores.SoupIndex PROPERTIES TIMEOUT 120)

add_executable(test_Soups test_Soups.cpp)
target_link_libraries(test_Soups PRIVATE stores)
add_test(NAME stores.Soups COMMAND test_Soups)
set_tests_properties(stores.Soups PROPERTIES TIMEOUT 120)

add_executable(test_ObjectStreamer test_ObjectStreamer.cpp)
target_link_libraries(test_ObjectStreamer PRIVATE stores)
add_test(NAME stores.ObjectStreamer COMMAND test_ObjectStreamer)
set_tests_properties(stores.ObjectStreamer PROPERTIES TIMEOUT 60)

add_executable(test_StoreCompander test_StoreCompander.cpp)
target_link_libraries(test_StoreCompander PRIVATE stores os600_kernel os600_user)
add_test(NAME stores.StoreCompander COMMAND test_StoreCompander)
set_tests_properties(stores.StoreCompander PROPERTIES TIMEOUT 60)
add_executable(test_LargeObjects test_LargeObjects.cpp)
target_link_libraries(test_LargeObjects PRIVATE stores os600_kernel os600_user)
add_test(NAME stores.LargeObjects COMMAND test_LargeObjects)
set_tests_properties(stores.LargeObjects PROPERTIES TIMEOUT 60)

