# the serial chip registry and the host's serial port over a TCP socket
add_executable(test_HostSerialChip test_HostSerialChip.cpp)
target_link_libraries(test_HostSerialChip PRIVATE hal_host_serial os600_kernel os600_user)
add_test(NAME hal.HostSerialChip COMMAND test_HostSerialChip)
set_tests_properties(hal.HostSerialChip PROPERTIES TIMEOUT 60)
