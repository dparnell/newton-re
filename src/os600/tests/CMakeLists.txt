# Tests that span the kernel, the user side and the host task runtime.
add_executable(test_HostRuntime test_HostRuntime.cpp)
target_link_libraries(test_HostRuntime PRIVATE os600_kernel os600_user)
add_test(NAME os600.HostRuntime COMMAND test_HostRuntime)
set_tests_properties(os600.HostRuntime PROPERTIES TIMEOUT 30)

add_executable(test_HostInterruptSources test_HostInterruptSources.cpp)
target_link_libraries(test_HostInterruptSources PRIVATE os600_kernel os600_user)
add_test(NAME os600.HostInterruptSources COMMAND test_HostInterruptSources)
set_tests_properties(os600.HostInterruptSources PROPERTIES TIMEOUT 30)

add_executable(test_Boot test_Boot.cpp)
target_link_libraries(test_Boot PRIVATE os600_kernel os600_user)
add_test(NAME os600.Boot COMMAND test_Boot)
set_tests_properties(os600.Boot PROPERTIES TIMEOUT 30)
