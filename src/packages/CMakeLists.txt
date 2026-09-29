# Packages: the package directory and its readers (TPackageIterator), the
# package manager task with the events it takes and the part handlers'
# side of it (package_manager: beside the kernel services, which start the
# manager - it needs nothing of the object system), then the frames part
# handlers and the packages built into the ROM extension, loaded through
# the manager at boot (packages).
add_library(package_manager STATIC
    PackageIterator.cpp
    PackageEvents.cpp
    PackageManager.cpp
    PartHandlers.cpp
    PackagePipe.cpp
    PartPipe.cpp
    PackageLoader.cpp
)
# stores: the ROM domain manager a package on a store is told to (the two
# static libraries depend on each other)
target_link_libraries(package_manager PUBLIC newton_config utility os600_user protocols toolbox)
target_link_libraries(package_manager PRIVATE stores)
target_include_directories(package_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(packages STATIC
    FramePartHandler.cpp
    ROMPackages.cpp
    Units.cpp
    StorePackages.cpp
    StorePackageNatives.cpp
    PackageArchivalPipe.cpp
)
# stores: a streamed frames part is read with a TObjectReader, and the
# package natives answer the package stores
target_link_libraries(packages PUBLIC newton_config package_manager utility os600_user toolbox frames stores)
target_include_directories(packages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
