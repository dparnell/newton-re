add_library(toolbox STATIC
    Angles.cpp
    AngleTables.cpp
    CompMath.cpp
    FixedGeometry.cpp
    FixedMath.cpp
)
target_link_libraries(toolbox PUBLIC newton_config)
target_include_directories(toolbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
