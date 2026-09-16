add_executable(test_Regions test_Regions.cpp)
target_link_libraries(test_Regions PRIVATE qd)
add_test(NAME qd.Regions COMMAND test_Regions)
set_tests_properties(qd.Regions PROPERTIES TIMEOUT 60)
