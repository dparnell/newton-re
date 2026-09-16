add_executable(test_Frames test_Frames.cpp)
target_link_libraries(test_Frames PRIVATE frames)
add_test(NAME frames.Objects COMMAND test_Frames)
set_tests_properties(frames.Objects PROPERTIES TIMEOUT 120)
