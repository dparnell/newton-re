# the NIE's re-expressions called as the package's own function objects
# would be: over the NIE's code binary, read out of the package in fixtures/;
# the same checks are run first on the package's own ARM code (src/armcpu's
# interpreter), the oracle the re-expressions must agree with: both count
add_executable(test_NIEProtoFSM test_NIEProtoFSM.cpp)
target_link_libraries(test_NIEProtoFSM PRIVATE thirdparty_nie armcpu_frames stores frames)
target_compile_definitions(test_NIEProtoFSM PRIVATE "NEWTON_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\""
    "NIE_PACKAGE=\"${CMAKE_SOURCE_DIR}/../fixtures/packages/apple/NIE2/REGPKGS/inetenbl.pkg\"")
add_test(NAME thirdparty.NIEProtoFSM COMMAND test_NIEProtoFSM)
