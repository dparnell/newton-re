add_executable(test_Containers test_Containers.cpp)
target_link_libraries(test_Containers PRIVATE utility)
add_test(NAME utility.Containers COMMAND test_Containers)

add_executable(test_Pipes test_Pipes.cpp)
target_link_libraries(test_Pipes PRIVATE utility)
add_test(NAME utility.Pipes COMMAND test_Pipes)
