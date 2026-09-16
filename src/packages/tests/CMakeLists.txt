add_executable(test_PackageIterator test_PackageIterator.cpp)
target_link_libraries(test_PackageIterator PRIVATE packages)
target_compile_definitions(test_PackageIterator PRIVATE "NEWTON_ROM_BIN=\"${CMAKE_SOURCE_DIR}/../build/MP2100D/rom.bin\"")
add_test(NAME packages.PackageIterator COMMAND test_PackageIterator)
