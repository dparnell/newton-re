add_library(toolbox STATIC
    CompMath.cpp
)
target_link_libraries(toolbox PUBLIC newton_config)
target_include_directories(toolbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
