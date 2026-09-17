# The recognition system: the recogniser's object classes (RecObject.h:
# TRecObject, TArray, TDArray), the strokes (Stroke.h: TStroke and its
# public face TStrokePublic), the units the recognisers make (Unit.h:
# TUnit, TSIUnit, TStrokeUnit, TClickUnit, TClickEventUnit) and the face
# the views see (UnitPublic.h), the areas (Areas.h), the domains' base
# (Domain.h) and the recognisers' front (Recognizer.h: TRecognizer, the
# click recognisers, TRecognitionManager).  The recognisers themselves
# (the stroke world, the controller, the arbiter, the domains) are NOT
# YET.  Over frames (the units and strokes are handed to NewtonScript as
# address refs) and qd.
add_library(recognition STATIC
    RecObject.cpp
    Stroke.cpp
    Unit.cpp
    UnitPublic.cpp
    Areas.cpp
    Domain.cpp
    Recognizer.cpp
)
target_include_directories(recognition PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# (the strokes tell the root view what to redraw; the views track strokes:
# the two static libraries depend on each other)
target_link_libraries(recognition PUBLIC frames qd views)
add_subdirectory(tests)
