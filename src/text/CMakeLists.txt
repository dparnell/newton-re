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
    TXBinaryChars.cpp
    TXChars.cpp
    TXCommand.cpp
    TXContainer.cpp
    TXDisplay.cpp
    TXFrameFormatter.cpp
    TXFormatter.cpp
    TXFrames.cpp
    TXGraphicsRun.cpp
    TXGraphicsRunChar.cpp
    TXHilite.cpp
    TXLine.cpp
    TXLinesHeights.cpp
    TXNewtContainer.cpp
    TXNewtTextRun.cpp
    TXObjectRange.cpp
    TXOffset.cpp
    TXRuler.cpp
    TXRulerRange.cpp
    TXRulerUI.cpp
    TXRun.cpp
    TXStyledText.cpp
    TXStream.cpp
    TXUtilities.cpp
    TXVBOChars.cpp
    TXView.cpp
    TXViewNatives.cpp
    Textension.cpp
)
# (qd for the clipping helpers; views for the root view they dirty)
target_link_libraries(text PUBLIC newton_config utility toolbox frames qd views)
target_include_directories(text PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
