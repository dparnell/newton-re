add_executable(test_DoubleQ test_DoubleQ.cpp)
target_link_libraries(test_DoubleQ PRIVATE os600_kernel)
add_test(NAME os600.kernel.DoubleQ COMMAND test_DoubleQ)

add_executable(test_ObjectTable test_ObjectTable.cpp)
target_link_libraries(test_ObjectTable PRIVATE os600_kernel)
add_test(NAME os600.kernel.ObjectTable COMMAND test_ObjectTable)
