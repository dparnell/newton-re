add_executable(test_CompMath test_CompMath.cpp)
target_link_libraries(test_CompMath PRIVATE toolbox)
add_test(NAME toolbox.CompMath COMMAND test_CompMath)
