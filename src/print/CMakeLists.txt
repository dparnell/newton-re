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
    FaxDriverTables.cpp
    DitherTables.cpp
)
target_link_libraries(print PUBLIC newton_config qd protocols frames comms comms_fax utility)

add_subdirectory(tests)
