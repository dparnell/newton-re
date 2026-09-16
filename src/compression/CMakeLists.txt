# Compression: the TCompressor/TDecompressor/TCallback* protocols and the
# ROM's implementations of them (LZ, Zippy, arithmetic and Unicode text),
# with the tables read from the ROM by tools/newton-rom/analysis/romtable.py.
add_library(compression STATIC
    Compression.cpp
    Pushpopper.cpp
    LZCompression.cpp
    LZTables.cpp
    ZippyCompression.cpp
    ArithmeticCompression.cpp
    UnicodeCompression.cpp
    UnicodeTables.cpp
)
target_link_libraries(compression PUBLIC newton_config protocols memory toolbox)
target_include_directories(compression PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
