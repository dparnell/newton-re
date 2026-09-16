# Stores: the persistent object store protocol TStore (Store.h) beneath the
# frames stores and soups, the frames layer's view of a store (StoreWrapper.h:
# symbol and map tables, the node cache), the ROM's read-only TPackageStore over a
# package's soup part, and the host's in-memory THostStore standing in for
# the flash store (host/).
add_library(stores STATIC
    Store.cpp
    PackageStore.cpp
    host/HostStore.cpp
    StoreWrapper.cpp
    NodeCache.cpp
)
target_link_libraries(stores PUBLIC newton_config protocols memory toolbox frames)
target_include_directories(stores PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
