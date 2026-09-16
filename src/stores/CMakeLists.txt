# Stores: the persistent object store protocol TStore (Store.h) beneath the
# frames stores and soups, the ROM's read-only TPackageStore over a
# package's soup part, and the host's in-memory THostStore standing in for
# the flash store (host/).
add_library(stores STATIC
    Store.cpp
    PackageStore.cpp
    host/HostStore.cpp
)
target_link_libraries(stores PUBLIC newton_config protocols memory toolbox)
target_include_directories(stores PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
