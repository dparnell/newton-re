# Packages: the package directory and its readers (TPackageIterator), to be
# followed by the package loader, the package manager task and the part
# handlers.
add_library(packages STATIC
    PackageIterator.cpp
)
target_link_libraries(packages PUBLIC newton_config utility os600_user toolbox)
target_include_directories(packages PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
