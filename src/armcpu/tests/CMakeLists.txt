add_executable(test_ARMCPU test_ARMCPU.cpp)
target_link_libraries(test_ARMCPU PRIVATE armcpu)
add_test(NAME armcpu.ARMCPU COMMAND test_ARMCPU)
