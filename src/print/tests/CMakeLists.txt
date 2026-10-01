add_executable(test_DotPrinter test_DotPrinter.cpp)
target_link_libraries(test_DotPrinter PRIVATE print os600_kernel os600_user)
add_test(NAME print.DotPrinter COMMAND test_DotPrinter)
set_tests_properties(print.DotPrinter PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "test_DotPrinter: all passed")

add_executable(test_PSPrinter test_PSPrinter.cpp)
target_link_libraries(test_PSPrinter PRIVATE print os600_kernel os600_user)
target_compile_definitions(test_PSPrinter PRIVATE "NEWTON_OBJECTS=\"${NEWTON_OBJECTS_FILE}\"")
add_test(NAME print.PSPrinter COMMAND test_PSPrinter)
set_tests_properties(print.PSPrinter PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "test_PSPrinter: all passed")

add_executable(test_HPPCL test_HPPCL.cpp)
target_link_libraries(test_HPPCL PRIVATE print)
add_test(NAME print.HPPCL COMMAND test_HPPCL)
set_tests_properties(print.HPPCL PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "test_HPPCL: all passed")

add_executable(test_HostIPP test_HostIPP.cpp)
target_link_libraries(test_HostIPP PRIVATE print)
add_test(NAME print.HostIPP COMMAND test_HostIPP)
set_tests_properties(print.HostIPP PROPERTIES TIMEOUT 30 PASS_REGULAR_EXPRESSION "test_HostIPP: all passed")

add_executable(test_HostDNSSD test_HostDNSSD.cpp)
target_link_libraries(test_HostDNSSD PRIVATE print_host_dnssd)
add_test(NAME print.HostDNSSD COMMAND test_HostDNSSD)
set_tests_properties(print.HostDNSSD PROPERTIES TIMEOUT 30 PASS_REGULAR_EXPRESSION "test_HostDNSSD: all passed")

# the host's TLS against the fake printer over ipps (its certificate the
# test one, tools/print/testcerts/printer-a.pem)
find_package(Python3 COMPONENTS Interpreter)
add_executable(test_HostTLS test_HostTLS.cpp)
target_link_libraries(test_HostTLS PRIVATE print_host_tls)
if(WIN32)
    target_link_libraries(test_HostTLS PRIVATE ws2_32)
endif()
if(Python3_FOUND)
    add_test(NAME print.HostTLS
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/print/ippprinter.py --port 0
                     --tls ${CMAKE_SOURCE_DIR}/../tools/print/testcerts/printer-a.pem --out ${CMAKE_CURRENT_BINARY_DIR}/tls
                     -- $<TARGET_FILE:test_HostTLS>)
    set_tests_properties(print.HostTLS PROPERTIES TIMEOUT 60
                         PASS_REGULAR_EXPRESSION "the certificate.s SHA-256 8D:85:C4:67:83:4A:B8:F0:41:34:79:B6:12:DC:EF:96:8D:2C:C6:FC:4D:C0:F4:31:F8:50:17:CA:95:7B:D1:56.*test_HostTLS: all passed")
endif()
