add_executable(test_TXArray test_TXArray.cpp)
target_link_libraries(test_TXArray PRIVATE text memory)
add_test(NAME text.TXArray COMMAND test_TXArray)
set_tests_properties(text.TXArray PROPERTIES TIMEOUT 120)

add_executable(test_TXStream test_TXStream.cpp)
target_link_libraries(test_TXStream PRIVATE text memory)
add_test(NAME text.TXStream COMMAND test_TXStream)
set_tests_properties(text.TXStream PROPERTIES TIMEOUT 120)
