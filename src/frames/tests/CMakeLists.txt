add_executable(test_Frames test_Frames.cpp)
target_link_libraries(test_Frames PRIVATE frames)
add_test(NAME frames.Objects COMMAND test_Frames)
set_tests_properties(frames.Objects PROPERTIES TIMEOUT 120)

# the ROM image the importer test reads: the debug ROM in the repository
add_executable(test_ROMImport test_ROMImport.cpp)
target_link_libraries(test_ROMImport PRIVATE frames)
target_compile_definitions(test_ROMImport PRIVATE "NEWTON_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\"")
add_test(NAME frames.ROMImport COMMAND test_ROMImport)
set_tests_properties(frames.ROMImport PROPERTIES TIMEOUT 120)

add_executable(test_Interpreter test_Interpreter.cpp)
target_link_libraries(test_Interpreter PRIVATE frames)
target_compile_definitions(test_Interpreter PRIVATE "NEWTON_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\"")
add_test(NAME frames.Interpreter COMMAND test_Interpreter)

add_executable(test_Printer test_Printer.cpp)
target_link_libraries(test_Printer PRIVATE frames)
target_compile_definitions(test_Printer PRIVATE "NEWTON_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\"")
add_test(NAME frames.Printer COMMAND test_Printer)

add_executable(test_Compiler test_Compiler.cpp)
target_link_libraries(test_Compiler PRIVATE frames)
target_compile_definitions(test_Compiler PRIVATE "NEWTON_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\"")
add_test(NAME frames.Compiler COMMAND test_Compiler)

add_executable(test_Strings test_Strings.cpp)
target_link_libraries(test_Strings PRIVATE frames)
target_compile_definitions(test_Strings PRIVATE "NEWTON_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\"")
add_test(NAME frames.Strings COMMAND test_Strings)

add_executable(test_FramesPart test_FramesPart.cpp)
target_link_libraries(test_FramesPart PRIVATE frames)
target_compile_definitions(test_FramesPart PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME frames.FramesPart COMMAND test_FramesPart)
set_tests_properties(frames.FramesPart PROPERTIES TIMEOUT 120)

add_executable(test_SortTables test_SortTables.cpp)
target_link_libraries(test_SortTables PRIVATE frames)
target_compile_definitions(test_SortTables PRIVATE "NEWTON_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\"")
add_test(NAME frames.SortTables COMMAND test_SortTables)
set_tests_properties(frames.SortTables PROPERTIES TIMEOUT 120)

add_executable(test_Marshalling test_Marshalling.cpp)
target_link_libraries(test_Marshalling PRIVATE frames)
target_compile_definitions(test_Marshalling PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME frames.Marshalling COMMAND test_Marshalling)
