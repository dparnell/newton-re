# QuickDraw: Newton's graphics library, Apple's QuickDraw rewritten in C for
# the ARM.  Rectangles and points (Rects.h), regions (Regions.h), ports,
# pens, patterns and pixel maps (Ports.h), drawing rectangles and regions
# and the blitter (Draw.h), lines, ovals, round rectangles and arcs
# (Shapes.h), the region holders (RegionVars.h), fonts and styles (Fonts.h: the 'sfnt' bitmap fonts of the
# font family frames), text drawn and measured (Text.h); polygons and
# pictures to come.
add_library(qd STATIC
    Rects.cpp
    Regions.cpp
    RegionVars.cpp
    Ports.cpp
    Draw.cpp
    Shapes.cpp
    Fonts.cpp
    Text.cpp
    Pictures.cpp
)
target_link_libraries(qd PUBLIC newton_config memory toolbox frames)
target_include_directories(qd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
