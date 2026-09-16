add_executable(test_Regions test_Regions.cpp)
target_link_libraries(test_Regions PRIVATE qd)
add_test(NAME qd.Regions COMMAND test_Regions)
set_tests_properties(qd.Regions PROPERTIES TIMEOUT 60)

add_executable(test_Draw test_Draw.cpp)
target_link_libraries(test_Draw PRIVATE qd)
add_test(NAME qd.Draw COMMAND test_Draw)
set_tests_properties(qd.Draw PROPERTIES TIMEOUT 60)

add_executable(test_Shapes test_Shapes.cpp)
target_link_libraries(test_Shapes PRIVATE qd)
add_test(NAME qd.Shapes COMMAND test_Shapes)
set_tests_properties(qd.Shapes PROPERTIES TIMEOUT 60)
