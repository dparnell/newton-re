# Printing: the TPrinter and TDotPrinterDriver
# protocols (Printer.h), the dot printer's imaging engine that draws a page
# a band at a time through its scaling bottlenecks (DotPrinter.cpp,
# ScalingBottlenecks.cpp), and the drivers behind it.
add_library(print STATIC
    Printer.cpp
    DotPrinter.cpp
    ScalingBottlenecks.cpp
    DriverCallbacks.cpp
    FaxDriver.cpp
    PSPrinter.cpp
    PSBottlenecks.cpp
    PSPrinterTables.cpp
    HPPCL.cpp
    FaxDriverTables.cpp
    DitherTables.cpp
    host/HostPrinter.cpp
    host/HostIPP.cpp
    host/HostIPPTool.cpp
    host/HostIPPSocket.cpp
    host/HostPAPDriver.cpp
    host/HostNetworkPrinters.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/HostPrintersSource.cpp
)
target_link_libraries(print PUBLIC newton_config qd protocols frames comms comms_fax utility ink print_host_dnssd print_host_tls)

# host/HostPrinters.ns, the Network Printers preferences panel, compiled in
# as a string
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostPrintersSource.cpp
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/host/HostPrinters.ns
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/HostPrintersSource.cpp -DNAME=gHostPrintersSource
            -P ${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/host/HostPrinters.ns ${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake
    VERBATIM)

# TLS for ipps:// printers (host/tls/HostTLS.h): the host's own library -
# Schannel on Windows, OpenSSL elsewhere when it is found (else no ipps) -
# built without the Newton include paths, a plain C interface.
add_library(print_host_tls STATIC
    host/tls/HostTLS.cpp
)
target_include_directories(print_host_tls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host/tls)
if(WIN32)
    target_link_libraries(print_host_tls PUBLIC secur32 crypt32)
else()
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        target_compile_definitions(print_host_tls PRIVATE HOST_TLS_OPENSSL)
        target_link_libraries(print_host_tls PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

# The host's DNS-SD browse for IPP printers (host/dnssd/HostDNSSD.h): the
# platform's own service browsing (dnsapi's DnsServiceBrowse on Windows,
# avahi-browse elsewhere), built without the Newton include paths - the
# platforms' headers and the DDK's names do not mix - so a plain C interface.
add_library(print_host_dnssd STATIC
    host/dnssd/HostDNSSD.cpp
)
target_include_directories(print_host_dnssd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/host/dnssd)
if(WIN32)
    target_link_libraries(print_host_dnssd PUBLIC dnsapi)
else()
    find_package(Threads)
    if(Threads_FOUND)
        target_link_libraries(print_host_dnssd PUBLIC Threads::Threads)
    endif()
endif()

add_subdirectory(tests)
