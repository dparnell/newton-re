add_executable(test_RecObject test_RecObject.cpp)
target_link_libraries(test_RecObject PRIVATE recognition)
add_test(NAME recognition.RecObject COMMAND test_RecObject)
