add_executable(test_Regions test_Regions.cpp)
target_link_libraries(test_Regions PRIVATE qd)
add_test(NAME qd.Regions COMMAND test_Regions)
set_tests_properties(qd.Regions PROPERTIES TIMEOUT 60)

add_executable(test_Draw test_Draw.cpp)
target_link_libraries(test_Draw PRIVATE qd)
add_test(NAME qd.Draw COMMAND test_Draw)
set_tests_properties(qd.Draw PROPERTIES TIMEOUT 60)

add_executable(test_Pictures test_Pictures.cpp)
target_link_libraries(test_Pictures PRIVATE qd)
add_test(NAME qd.Pictures COMMAND test_Pictures)
set_tests_properties(qd.Pictures PROPERTIES TIMEOUT 60)

add_executable(test_Shapes test_Shapes.cpp)
target_link_libraries(test_Shapes PRIVATE qd)
add_test(NAME qd.Shapes COMMAND test_Shapes)
set_tests_properties(qd.Shapes PROPERTIES TIMEOUT 60)

add_executable(test_Text test_Text.cpp)
target_link_libraries(test_Text PRIVATE qd)
target_compile_definitions(test_Text PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME qd.Text COMMAND test_Text)
set_tests_properties(qd.Text PROPERTIES TIMEOUT 120)

add_executable(test_Screen test_Screen.cpp)
target_link_libraries(test_Screen PRIVATE qd hal_host_display)
add_test(NAME qd.Screen COMMAND test_Screen ${CMAKE_BINARY_DIR}/test_Screen.pgm)
set_tests_properties(qd.Screen PROPERTIES TIMEOUT 60)

add_executable(test_PicPlay test_PicPlay.cpp)
target_link_libraries(test_PicPlay PRIVATE qd)
add_test(NAME qd.PicPlay COMMAND test_PicPlay)
