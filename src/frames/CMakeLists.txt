# Frames: the NewtonScript object system - the object heap and its collector,
# refs, symbols, arrays, frames and their maps, paths, clones and classes
# (the DDK's objects.h; Frames.h for clients), and the importer that reads
# the ROM's own objects out of a ROM image (ROMImport.h).  RSSymbols.h,
# RSSymbolTable.cpp, ROMConstants.h and ROMConstants.cpp are generated from
# the ROM's constants by tools/newton-rom/analysis/romconstants.py.
add_library(frames STATIC
    ObjectHeap.cpp
    GC.cpp
    Objects.cpp
    Symbols.cpp
    RSSymbolTable.cpp
    ROMConstants.cpp
    ROMImport.cpp
    Interpreter.cpp
    VariableLookup.cpp
    NativeFunctions.cpp
    ROMNatives.cpp
    Builtins.cpp
    Munger.cpp
    Printer.cpp
    PrintLiterals.cpp
    REPTranslators.cpp
)
target_link_libraries(frames PUBLIC newton_config memory os600_user hal_host toolbox utility protocols)
target_include_directories(frames PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
