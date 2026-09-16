add_executable(test_Frames test_Frames.cpp)
target_link_libraries(test_Frames PRIVATE frames)
add_test(NAME frames.Objects COMMAND test_Frames)
set_tests_properties(frames.Objects PROPERTIES TIMEOUT 120)

# the ROM image the importer test reads: the MP2100 D debug ROM in the repository
add_executable(test_ROMImport test_ROMImport.cpp)
target_link_libraries(test_ROMImport PRIVATE frames)
target_compile_definitions(test_ROMImport PRIVATE "NEWTON_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../DebugRom/MP2100 D/Senior DCirrusNoDebug image\"")
add_test(NAME frames.ROMImport COMMAND test_ROMImport)
set_tests_properties(frames.ROMImport PROPERTIES TIMEOUT 120)

add_executable(test_Interpreter test_Interpreter.cpp)
target_link_libraries(test_Interpreter PRIVATE frames)
target_compile_definitions(test_Interpreter PRIVATE "NEWTON_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../DebugRom/MP2100 D/Senior DCirrusNoDebug image\"")
add_test(NAME frames.Interpreter COMMAND test_Interpreter)

add_executable(test_Printer test_Printer.cpp)
target_link_libraries(test_Printer PRIVATE frames)
target_compile_definitions(test_Printer PRIVATE "NEWTON_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../DebugRom/MP2100 D/Senior DCirrusNoDebug image\"")
add_test(NAME frames.Printer COMMAND test_Printer)

add_executable(test_Compiler test_Compiler.cpp)
target_link_libraries(test_Compiler PRIVATE frames)
target_compile_definitions(test_Compiler PRIVATE "NEWTON_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../DebugRom/MP2100 D/Senior DCirrusNoDebug image\"")
add_test(NAME frames.Compiler COMMAND test_Compiler)

add_executable(test_Strings test_Strings.cpp)
target_link_libraries(test_Strings PRIVATE frames)
target_compile_definitions(test_Strings PRIVATE "NEWTON_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../DebugRom/MP2100 D/Senior DCirrusNoDebug image\"")
add_test(NAME frames.Strings COMMAND test_Strings)
