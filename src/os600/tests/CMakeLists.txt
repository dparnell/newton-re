# Tests that span the kernel, the user side and the host task runtime.
add_executable(test_HostRuntime test_HostRuntime.cpp)
target_link_libraries(test_HostRuntime PRIVATE os600_kernel os600_user)
add_test(NAME os600.HostRuntime COMMAND test_HostRuntime)
set_tests_properties(os600.HostRuntime PROPERTIES TIMEOUT 30)
