# Ink: the strokes the pen leaves, kept as they were written - the three
# classes of ink binary, and the eight bytes an ink word carries about
# itself so that it can be laid out among real characters.  The stroke
# compression underneath it (the CIC encoder and decoder) is NOT YET.
add_library(ink STATIC
    Ink.cpp
    InkStrokes.cpp
    InkCodec.cpp
    CICDecoder.cpp
    CICEncoder.cpp
    CICCodeBook.cpp
    CICTables.cpp
)
# (the glue between the strokes and the codec is the one part of this
# area that knows what a stroke is)
target_link_libraries(ink PUBLIC newton_config frames qd recognition)
target_include_directories(ink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
