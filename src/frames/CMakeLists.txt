# Frames: the NewtonScript object system - the object heap and its collector,
# refs, symbols, arrays, frames and their maps, paths, clones and classes
# (the DDK's objects.h; Frames.h for clients).  RSSymbols.h/RSSymbolTable.cpp
# are generated from the ROM's RSSYM constants by
# tools/newton-rom/analysis/rssymbols.py.
add_library(frames STATIC
    ObjectHeap.cpp
    GC.cpp
    Objects.cpp
    Symbols.cpp
    RSSymbolTable.cpp
)
target_link_libraries(frames PUBLIC newton_config memory os600_user hal_host)
target_include_directories(frames PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
