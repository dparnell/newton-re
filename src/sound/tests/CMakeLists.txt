add_executable(test_IMACodec test_IMACodec.cpp)
target_link_libraries(test_IMACodec PRIVATE sound)
add_test(NAME sound.IMACodec COMMAND test_IMACodec)
set_tests_properties(sound.IMACodec PROPERTIES TIMEOUT 60)
