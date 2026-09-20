add_executable(test_CompMath test_CompMath.cpp)
target_link_libraries(test_CompMath PRIVATE toolbox)
add_test(NAME toolbox.CompMath COMMAND test_CompMath)

add_executable(test_FixedMath test_FixedMath.cpp)
target_link_libraries(test_FixedMath PRIVATE toolbox)
add_test(NAME toolbox.FixedMath COMMAND test_FixedMath)

add_executable(test_Angles test_Angles.cpp)
target_link_libraries(test_Angles PRIVATE toolbox)
add_test(NAME toolbox.Angles COMMAND test_Angles)
