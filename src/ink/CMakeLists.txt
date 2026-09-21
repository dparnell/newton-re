# Ink: the strokes the pen leaves, kept as they were written - the three
# classes of ink binary, and the eight bytes an ink word carries about
# itself so that it can be laid out among real characters.  The stroke
# compression underneath it (the CIC encoder and decoder) is NOT YET.
add_library(ink STATIC
    Ink.cpp
    InkCodec.cpp
    CICDecoder.cpp
    CICCodeBook.cpp
)
target_link_libraries(ink PUBLIC newton_config frames qd)
target_include_directories(ink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
