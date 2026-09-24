# The text engine (TXArray.h): the document engine behind protoTXView -
# styled text with rulers, tabs and page breaks, written in ordinary C++
# over its own storage rather than over the object heap.  What is here
# so far is its foundation: the growable arrays everything else is built
# on, the ranges that record which run of characters each style, line
# and paragraph covers, the attributes a run carries (TXAttributes.h),
# the chunked character storage (TXChars.h) and the byte streams text
# and chunk tables travel through (TXStream.h), the rulers a paragraph
# is laid out against (TXRuler.h) and the object ranges that say which
# run of the text points at which of them (TXObjectRange.h).
add_library(text STATIC
    TXArray.cpp
    TXAttributes.cpp
    TXChars.cpp
    TXObjectRange.cpp
    TXRuler.cpp
    TXStream.cpp
)
target_link_libraries(text PUBLIC newton_config utility toolbox frames)
target_include_directories(text PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
