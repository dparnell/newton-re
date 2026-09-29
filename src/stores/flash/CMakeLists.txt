# The flash the flash store keeps its data on (Flash.h): the TFlash and
# TFlashDriver protocols, the ranges of chips and the internal flash, with
# the driver of the port's chips (host/: the host's, over
# hal/host/HostFlash.h).  packages: the ROM extensions' configuration
# entries ('flsa, 'fdrv) and where the extensions are.
add_library(flash STATIC
    FlashRange.cpp
    InternalFlash.cpp
    FlashStore.cpp
    FlashStoreObjects.cpp
    FlashStoreParts.cpp
    FlashStoreTables.cpp
    FlashBlock.cpp
    FlashIterator.cpp
    host/HostFlashDriver.cpp
)
target_link_libraries(flash PUBLIC newton_config protocols memory os600_user os600_kernel hal_host hal_timers packages stores toolbox utility)
target_include_directories(flash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/hal/host)

add_subdirectory(tests)
