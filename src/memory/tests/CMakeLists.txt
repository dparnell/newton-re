add_executable(test_SkiaHeap test_SkiaHeap.cpp)
target_link_libraries(test_SkiaHeap PRIVATE memory)
add_test(NAME memory.SkiaHeap COMMAND test_SkiaHeap)
