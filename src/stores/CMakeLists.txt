# Stores: the persistent object store protocol TStore (Store.h) beneath the
# frames stores and soups, the frames layer's view of a store (StoreWrapper.h:
# symbol and map tables, the node cache, the store object format, the
# soup index B-tree), the ROM's read-only TPackageStore over a
# package's soup part, and the host's in-memory THostStore standing in for
# the flash store (host/).
add_library(stores STATIC
    Store.cpp
    PackageStore.cpp
    host/HostStore.cpp
    StoreWrapper.cpp
    NodeCache.cpp
    StorePipes.cpp
    StoreObject.cpp
    SoupIndex.cpp
    Entries.cpp
    Soups.cpp
    SoupNatives.cpp
    Cursors.cpp
	UnionSoups.cpp
	Tags.cpp
	CopyEntries.cpp
	ObjectStreamer.cpp
	StoreCompander.cpp
	PackageStorePartHandler.cpp
)
# package_manager: the package store's part handler (a TPartHandler)
target_link_libraries(stores PUBLIC newton_config protocols memory toolbox frames compression hal_host package_manager)
target_include_directories(stores PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
