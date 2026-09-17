# The recognition system: the recogniser's object classes (RecObject.h:
# TRecObject, TArray, TDArray), the strokes (Stroke.h: TStroke and its
# public face TStrokePublic) and the units the recognisers hand the views
# (Unit.h: TUnit, TSIUnit, TStrokeUnit, TClickUnit, TUnitPublic).  The
# recognisers themselves (the domains, the controller, the arbiter) are
# NOT YET.  Over frames (the units and strokes are handed to NewtonScript
# as address refs) and qd.
add_library(recognition STATIC
    RecObject.cpp
    Stroke.cpp
)
target_include_directories(recognition PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# (the strokes tell the root view what to redraw; the views track strokes:
# the two static libraries depend on each other)
target_link_libraries(recognition PUBLIC frames qd views)
add_subdirectory(tests)
