# QuickDraw: Newton's graphics library, Apple's QuickDraw rewritten in C for
# the ARM.  Rectangles and points (Rects.h), regions (Regions.h), ports,
# pens, patterns and pixel maps (Ports.h), drawing rectangles and regions
# and the blitter (Draw.h); lines, shapes, pictures and text to come.
add_library(qd STATIC
    Rects.cpp
    Regions.cpp
    Ports.cpp
    Draw.cpp
)
target_link_libraries(qd PUBLIC newton_config memory)
target_include_directories(qd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
