# UtilityClasses: the container and helper classes (CDynamicArray, CList,
# CSortedList, the iterators, CItemTester/CItemComparer, the NArray family), the timer queue,
# the task frameworks (TForkWorld, TAppWorld with TAEventHandler) that
# the OS services and the higher layers are built on, the Unicode
# string functions and conversions, and the buffers and pipes
# (CBufferSegment, CPipe, CBufferPipe) the streams flow through.
add_library(utility STATIC
    DynamicArray.cpp
    ArrayIterator.cpp
    List.cpp
    ListIterator.cpp
    SortedList.cpp
    ItemTester.cpp
    NArray.cpp
    TimerQueue.cpp
    AEventHandler.cpp
    ForkWorld.cpp
    AppWorld.cpp
    Unicode.cpp
    BufferSegment.cpp
    Pipes.cpp
)
target_link_libraries(utility PUBLIC newton_config toolbox os600_user memory)
target_include_directories(utility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
