add_executable(test_ARMCPU test_ARMCPU.cpp)
target_link_libraries(test_ARMCPU PRIVATE armcpu)
add_test(NAME armcpu.ARMCPU COMMAND test_ARMCPU)

# a package's native functions run through the fallback: Mahjongg deals
add_test(NAME armcpu.Mahjongg
    COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --erase
            --store ${CMAKE_CURRENT_BINARY_DIR}/mahjongg.store --headless 28
            --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/games/Mahjongg2.1/Mahjongg.pkg
            --script ${CMAKE_CURRENT_SOURCE_DIR}/mahjongg.ns
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(armcpu.Mahjongg PROPERTIES TIMEOUT 180
    ENVIRONMENT "NEWTON_TRACE_ARMCPU=1"
    PASS_REGULAR_EXPRESSION "native [+]0x2d8c: [0-9]+ instructions, stop 1.*mahjongg: open.*mahjongg: done"
    FAIL_REGULAR_EXPRESSION "[[]armcpu[]] (the ROM's|native [+]0x[0-9a-f]+ stopped|an object|[0-9a-f]+ is not)")
