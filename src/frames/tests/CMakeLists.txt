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
