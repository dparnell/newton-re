# the NIE's re-expressions called as the package's own function objects
# would be: over the NIE's code binary, read out of the package in fixtures/;
# the same checks are run first on the package's own ARM code (src/armcpu's
# interpreter), the oracle the re-expressions must agree with: both count
add_executable(test_NIEProtoFSM test_NIEProtoFSM.cpp)
target_link_libraries(test_NIEProtoFSM PRIVATE thirdparty_nie armcpu_frames stores frames)
target_compile_definitions(test_NIEProtoFSM PRIVATE "NEWTON_OBJECTS=\"${NEWTON_OBJECTS_FILE}\""
    "NIE_PACKAGE=\"${CMAKE_SOURCE_DIR}/../fixtures/packages/apple/NIE2/REGPKGS/inetenbl.pkg\"")
add_test(NAME thirdparty.NIEProtoFSM COMMAND test_NIEProtoFSM)

# the NIE's card handler (newtdev.pkg part 4, re-expressed): made by name as
# the card server makes it, a memory card declined, a card registered under
# "Lantern" taken
add_executable(test_LanternCardHandler test_LanternCardHandler.cpp)
target_link_libraries(test_LanternCardHandler PRIVATE thirdparty_nie pcmcia os600_kernel os600_user)
add_test(NAME thirdparty.LanternCardHandler COMMAND test_LanternCardHandler)
set_tests_properties(thirdparty.LanternCardHandler PROPERTIES TIMEOUT 60)
