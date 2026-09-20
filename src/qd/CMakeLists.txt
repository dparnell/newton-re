# QuickDraw: Newton's graphics library, Apple's QuickDraw rewritten in C for
# the ARM.  Rectangles and points (Rects.h), regions (Regions.h), ports,
# pens, patterns and pixel maps (Ports.h), drawing rectangles and regions
# and the blitter (Draw.h), lines, ovals, round rectangles and arcs
# (Shapes.h), polygons and the recording of lines into regions
# (Polygons.h), the region holders (RegionVars.h), fonts and styles
# (Fonts.h: the 'sfnt' bitmap fonts of the font family frames), text drawn
# and measured (Text.h), bitmap frames (Pictures.h), the screen pixel map
# and its driver protocol (Screen.h); QuickDraw pictures to come.
add_library(qd STATIC
    Rects.cpp
    Regions.cpp
    RegionVars.cpp
    Ports.cpp
    Draw.cpp
    Shapes.cpp
    Polygons.cpp
    Fonts.cpp
    Text.cpp
    Pictures.cpp
    Transform.cpp
    Screen.cpp
    ScreenNatives.cpp
    RectNatives.cpp
)
target_link_libraries(qd PUBLIC newton_config memory toolbox frames protocols)
target_include_directories(qd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
