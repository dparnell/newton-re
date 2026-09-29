add_executable(test_Options test_Options.cpp)
target_link_libraries(test_Options PRIVATE comms os600_kernel os600_user)
add_test(NAME comms.Options COMMAND test_Options)
set_tests_properties(comms.Options PROPERTIES TIMEOUT 30)
