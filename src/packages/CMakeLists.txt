# Packages: the package directory and its readers (TPackageIterator), to be
# followed by the package manager task and the part handlers.
# ROMPackages.h loads the packages built into the ROM extension - the
# applications a MessagePad boots with - by installing their frames parts
# directly, the package manager being NOT YET.
add_library(packages STATIC
    PackageIterator.cpp
    ROMPackages.cpp
)
# stores: a package may carry a store part of its own (the WorldData
# package's cities), which is mounted as a TPackageStore
target_link_libraries(packages PUBLIC newton_config utility os600_user toolbox frames stores)
target_include_directories(packages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
