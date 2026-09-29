add_executable(test_Containers test_Containers.cpp)
target_link_libraries(test_Containers PRIVATE utility)
add_test(NAME utility.Containers COMMAND test_Containers)

add_executable(test_Pipes test_Pipes.cpp)
target_link_libraries(test_Pipes PRIVATE utility)
add_test(NAME utility.Pipes COMMAND test_Pipes)

# the ROM's own DES run on the ARM interpreter as the oracle (needs the
# ROM image)
add_executable(test_DES test_DES.cpp)
target_link_libraries(test_DES PRIVATE utility armcpu)
target_compile_definitions(test_DES PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME utility.DES COMMAND test_DES)

add_executable(test_CRC16 test_CRC16.cpp)
target_link_libraries(test_CRC16 PRIVATE utility)
add_test(NAME utility.CRC16 COMMAND test_CRC16)

add_executable(test_RingBuffer test_RingBuffer.cpp)
target_link_libraries(test_RingBuffer PRIVATE utility)
add_test(NAME utility.RingBuffer COMMAND test_RingBuffer)

add_executable(test_ShadowRingBuffer test_ShadowRingBuffer.cpp)
target_link_libraries(test_ShadowRingBuffer PRIVATE utility os600_kernel os600_user)
add_test(NAME utility.ShadowRingBuffer COMMAND test_ShadowRingBuffer)
set_tests_properties(utility.ShadowRingBuffer PROPERTIES TIMEOUT 30)

add_executable(test_Random test_Random.cpp)
target_link_libraries(test_Random PRIVATE utility)
add_test(NAME utility.Random COMMAND test_Random)

add_executable(test_BufferList test_BufferList.cpp)
target_link_libraries(test_BufferList PRIVATE utility os600_kernel os600_user)
add_test(NAME utility.BufferList COMMAND test_BufferList)
set_tests_properties(utility.BufferList PROPERTIES TIMEOUT 30)

add_executable(test_CircleBuf test_CircleBuf.cpp)
target_link_libraries(test_CircleBuf PRIVATE utility)
add_test(NAME utility.CircleBuf COMMAND test_CircleBuf)

# the ring buffer two tasks share (the NTK's nub and its task talk through two)
add_executable(test_TaskSafeRingBuffer test_TaskSafeRingBuffer.cpp)
target_link_libraries(test_TaskSafeRingBuffer PRIVATE utility os600_kernel os600_user)
add_test(NAME utility.TaskSafeRingBuffer COMMAND test_TaskSafeRingBuffer)
set_tests_properties(utility.TaskSafeRingBuffer PROPERTIES TIMEOUT 60)

add_executable(test_EventCollector test_EventCollector.cpp)
target_link_libraries(test_EventCollector PRIVATE utility os600_kernel os600_user)
add_test(NAME utility.EventCollector COMMAND test_EventCollector)
