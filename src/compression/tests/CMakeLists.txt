add_executable(test_Compression test_Compression.cpp)
target_link_libraries(test_Compression PRIVATE compression os600_kernel os600_user)
add_test(NAME compression.LZ COMMAND test_Compression)
set_tests_properties(compression.LZ PROPERTIES TIMEOUT 60)

# the ROM's own LZ coder run on the ARM interpreter as the oracle (needs
# the ROM image): the reconstruction must compress to the same bytes and
# decompress to the same bytes and the same length, including where the
# ROM's own length is wrong
add_executable(test_LZOracle test_LZOracle.cpp)
target_link_libraries(test_LZOracle PRIVATE compression armcpu os600_kernel os600_user)
target_compile_definitions(test_LZOracle PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME compression.LZOracle COMMAND test_LZOracle)
set_tests_properties(compression.LZOracle PROPERTIES TIMEOUT 300)
