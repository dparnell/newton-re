add_executable(test_Protocols test_Protocols.cpp)
target_link_libraries(test_Protocols PRIVATE protocols os600_kernel os600_user)
add_test(NAME protocols.Registry COMMAND test_Protocols)
set_tests_properties(protocols.Registry PROPERTIES TIMEOUT 30)
