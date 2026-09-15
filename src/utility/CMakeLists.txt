# UtilityClasses: the container and helper classes (CDynamicArray, CList,
# CSortedList, the iterators, CItemTester/CItemComparer) that the OS
# services and the higher layers are built on.  Over the memory manager's
# pointer API (NewPtr etc., a host stand-in in toolbox/host for now).
add_library(utility STATIC
    DynamicArray.cpp
    ArrayIterator.cpp
    List.cpp
    ListIterator.cpp
    SortedList.cpp
    ItemTester.cpp
    TimerQueue.cpp
)
target_link_libraries(utility PUBLIC newton_config toolbox os600_user)
target_include_directories(utility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
