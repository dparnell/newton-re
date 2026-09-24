# The text engine (TXArray.h): the document engine behind protoTXView -
# styled text with rulers, tabs and page breaks, written in ordinary C++
# over its own storage rather than over the object heap.  What is here
# so far is its foundation: the growable arrays everything else is built
# on, and the ranges that record which run of characters each style,
# line and paragraph covers.
add_library(text STATIC
    TXArray.cpp
)
target_link_libraries(text PUBLIC newton_config utility)
target_include_directories(text PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
