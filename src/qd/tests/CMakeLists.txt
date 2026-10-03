add_executable(test_Regions test_Regions.cpp)
target_link_libraries(test_Regions PRIVATE qd)
add_test(NAME qd.Regions COMMAND test_Regions)
set_tests_properties(qd.Regions PROPERTIES TIMEOUT 60)

add_executable(test_Draw test_Draw.cpp)
target_link_libraries(test_Draw PRIVATE qd)
add_test(NAME qd.Draw COMMAND test_Draw)
set_tests_properties(qd.Draw PROPERTIES TIMEOUT 60)

add_executable(test_Pictures test_Pictures.cpp)
target_link_libraries(test_Pictures PRIVATE qd views)
add_test(NAME qd.Pictures COMMAND test_Pictures)
set_tests_properties(qd.Pictures PROPERTIES TIMEOUT 60)

add_executable(test_Shapes test_Shapes.cpp)
target_link_libraries(test_Shapes PRIVATE qd)
add_test(NAME qd.Shapes COMMAND test_Shapes)
set_tests_properties(qd.Shapes PROPERTIES TIMEOUT 60)

add_executable(test_Text test_Text.cpp)
target_link_libraries(test_Text PRIVATE qd)
target_compile_definitions(test_Text PRIVATE "NEWTON_OBJECTS=\"${NEWTON_OBJECTS_FILE}\"")
add_test(NAME qd.Text COMMAND test_Text)
set_tests_properties(qd.Text PROPERTIES TIMEOUT 120)

add_executable(test_Screen test_Screen.cpp)
target_link_libraries(test_Screen PRIVATE qd hal_host_display)
add_test(NAME qd.Screen COMMAND test_Screen ${CMAKE_BINARY_DIR}/test_Screen.pgm)
set_tests_properties(qd.Screen PROPERTIES TIMEOUT 60)

add_executable(test_PicPlay test_PicPlay.cpp)
target_link_libraries(test_PicPlay PRIVATE qd)
target_compile_definitions(test_PicPlay PRIVATE "NEWTON_OBJECTS=\"${NEWTON_OBJECTS_FILE}\"")
add_test(NAME qd.PicPlay COMMAND test_PicPlay)

add_executable(test_TextObject test_TextObject.cpp)
target_link_libraries(test_TextObject PRIVATE qd)
target_compile_definitions(test_TextObject PRIVATE "NEWTON_OBJECTS=\"${NEWTON_OBJECTS_FILE}\"")
add_test(NAME qd.TextObject COMMAND test_TextObject)
set_tests_properties(qd.TextObject PROPERTIES TIMEOUT 120)

add_executable(test_ScrollRect test_ScrollRect.cpp)
target_link_libraries(test_ScrollRect PRIVATE qd)
add_test(NAME qd.ScrollRect COMMAND test_ScrollRect)
set_tests_properties(qd.ScrollRect PROPERTIES TIMEOUT 120)

# the fast blitter against the slow one it replaced (qd/Draw.h:
# SetQDSlowBlitter): random scenes in every verb, mode and depth, drawn
# both ways and compared byte for byte
add_executable(test_Blitter test_Blitter.cpp)
target_link_libraries(test_Blitter PRIVATE qd)
add_test(NAME qd.Blitter COMMAND test_Blitter)
set_tests_properties(qd.Blitter PROPERTIES TIMEOUT 300)

# StretchBits onto eight-bit maps (a host extension: the ROM draws nothing
# onto one) against the same calls onto four-bit maps, widened
add_executable(test_Stretch8 test_Stretch8.cpp)
target_link_libraries(test_Stretch8 PRIVATE qd)
add_test(NAME qd.Stretch8 COMMAND test_Stretch8)
set_tests_properties(qd.Stretch8 PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "test_Stretch8: all passed")

# a fax page turned a quarter each way a tile at a time (qd/Tile.h), every
# pixel checked
add_executable(test_Tile test_Tile.cpp)
target_link_libraries(test_Tile PRIVATE qd views)
add_test(NAME qd.Tile COMMAND test_Tile)
set_tests_properties(qd.Tile PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "test_Tile: ok")
