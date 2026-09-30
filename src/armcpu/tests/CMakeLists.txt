add_executable(test_ARMCPU test_ARMCPU.cpp)
target_link_libraries(test_ARMCPU PRIVATE armcpu)
add_test(NAME armcpu.ARMCPU COMMAND test_ARMCPU)

add_executable(test_PackageNativeCPU test_PackageNativeCPU.cpp)
target_link_libraries(test_PackageNativeCPU PRIVATE armcpu_frames frames)
target_compile_definitions(test_PackageNativeCPU PRIVATE "NEWTON_OBJECTS=\"${NEWTON_OBJECTS_FILE}\"")
add_test(NAME armcpu.PackageNativeCPU COMMAND test_PackageNativeCPU)

# a package's native functions run through the fallback: Mahjongg deals
add_test(NAME armcpu.Mahjongg
    COMMAND newton --display 320x480 --erase
            --store ${CMAKE_CURRENT_BINARY_DIR}/mahjongg.store --headless 28
            --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/games/Mahjongg2.1/Mahjongg.pkg
            --script ${CMAKE_CURRENT_SOURCE_DIR}/mahjongg.ns
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(armcpu.Mahjongg PROPERTIES TIMEOUT 180
    ENVIRONMENT "NEWTON_TRACE_ARMCPU=1"
    PASS_REGULAR_EXPRESSION "native [+]0x2d8c: [0-9]+ instructions, stop 1.*mahjongg: open.*mahjongg: done"
    FAIL_REGULAR_EXPRESSION "[[]armcpu[]] (the ROM's|native [+]0x[0-9a-f]+ stopped|an object|[0-9a-f]+ is not)")

# NewtHack's five-argument native function, run every turn
add_test(NAME armcpu.NewtHack
    COMMAND newton --display 320x480 --erase
            --store ${CMAKE_CURRENT_BINARY_DIR}/newthack.store --headless 32
            --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/games/NewtHack1_1/newthack.pkg
            --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/games/NewtHack1_1/nhsounds.pkg
            --script ${CMAKE_CURRENT_SOURCE_DIR}/newthack.ns
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(armcpu.NewtHack PROPERTIES TIMEOUT 180
    ENVIRONMENT "NEWTON_TRACE_ARMCPU=1"
    PASS_REGULAR_EXPRESSION "native [+]0x29ec: [0-9]+ instructions, stop 1.*newthack: done"
    FAIL_REGULAR_EXPRESSION "[[]armcpu[]] (the ROM's|native [+]0x[0-9a-f]+ stopped|an object|[0-9a-f]+ is not)")
