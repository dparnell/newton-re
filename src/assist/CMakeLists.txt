# The Intelligent Assistant's C++ side (Assistant.h): the task templates
# and the list operations the "Please ..." slip is built with.
add_library(assist STATIC
    Assistant.cpp
)
target_link_libraries(assist PUBLIC newton_config frames)
target_include_directories(assist PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
