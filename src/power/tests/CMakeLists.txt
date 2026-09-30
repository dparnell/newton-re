add_executable(test_PowerManager test_PowerManager.cpp)
target_link_libraries(test_PowerManager PRIVATE power os600_kernel os600_user)
add_test(NAME power.PowerManager COMMAND test_PowerManager)
set_tests_properties(power.PowerManager PROPERTIES TIMEOUT 60)
