add_library(toolbox STATIC
    CompMath.cpp
    host/Memory.cpp
)
target_link_libraries(toolbox PUBLIC newton_config)

add_subdirectory(tests)
