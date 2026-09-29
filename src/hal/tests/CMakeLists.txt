# the serial chip registry and the host's serial port over a TCP socket
add_executable(test_HostSerialChip test_HostSerialChip.cpp)
target_link_libraries(test_HostSerialChip PRIVATE hal_host_serial os600_kernel os600_user)
add_test(NAME hal.HostSerialChip COMMAND test_HostSerialChip)
set_tests_properties(hal.HostSerialChip PROPERTIES TIMEOUT 60)

add_executable(test_FIQTimer test_FIQTimer.cpp)
target_link_libraries(test_FIQTimer PRIVATE hal_timers os600_kernel os600_user)
add_test(NAME hal.FIQTimer COMMAND test_FIQTimer)
set_tests_properties(hal.FIQTimer PROPERTIES TIMEOUT 60)
