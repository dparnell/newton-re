# testing: the Newton's own test facilities - the journal (strokes
# recorded and played back into the tablet buffer) and the tablet's bypass
# natives.  The test agent (TTestAgent) is NOT YET.  The stroke world
# records into the journal, so this and recognition depend on each other.
add_library(testing STATIC
    Journal.cpp
)
target_include_directories(testing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(testing PUBLIC newton_config frames recognition os600_user)
