# the serial chip registry and the host's serial port over a TCP socket
add_executable(test_HostSerialChip test_HostSerialChip.cpp)
target_link_libraries(test_HostSerialChip PRIVATE hal_host_serial os600_kernel os600_user)
add_test(NAME hal.HostSerialChip COMMAND test_HostSerialChip)
set_tests_properties(hal.HostSerialChip PROPERTIES TIMEOUT 60)

add_executable(test_FIQTimer test_FIQTimer.cpp)
target_link_libraries(test_FIQTimer PRIVATE hal_timers os600_kernel os600_user)
add_test(NAME hal.FIQTimer COMMAND test_FIQTimer)
set_tests_properties(hal.FIQTimer PROPERTIES TIMEOUT 60)

# the host's IR port: two chips over a socket, the modulation and half duplex
add_executable(test_HostIRChip test_HostIRChip.cpp)
target_link_libraries(test_HostIRChip PRIVATE hal_host_serial os600_kernel os600_user)
add_test(NAME hal.HostIRChip COMMAND test_HostIRChip)
set_tests_properties(hal.HostIRChip PROPERTIES TIMEOUT 60)

# the host's PC card and socket: the image, the CIS through the attribute
# window, the flash rules, the card-detect interrupt
add_executable(test_HostCard test_HostCard.cpp)
target_link_libraries(test_HostCard PRIVATE hal_host_card os600_kernel os600_user)
add_test(NAME hal.HostCard COMMAND test_HostCard)
set_tests_properties(hal.HostCard PROPERTIES TIMEOUT 60)
