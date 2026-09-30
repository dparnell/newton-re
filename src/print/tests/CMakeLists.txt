add_executable(test_DotPrinter test_DotPrinter.cpp)
target_link_libraries(test_DotPrinter PRIVATE print os600_kernel os600_user)
add_test(NAME print.DotPrinter COMMAND test_DotPrinter)
set_tests_properties(print.DotPrinter PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "test_DotPrinter: all passed")
