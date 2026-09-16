add_executable(test_Compression test_Compression.cpp)
target_link_libraries(test_Compression PRIVATE compression os600_kernel os600_user)
add_test(NAME compression.LZ COMMAND test_Compression)
set_tests_properties(compression.LZ PROPERTIES TIMEOUT 60)
