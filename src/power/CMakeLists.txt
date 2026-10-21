# The power manager and the machine's sleep (PowerManager.h): the 'pg&e
# world the power switch and the batteries' readings go to, CyclePower
# over the platform's power (hal/Power.h), and the battery driver's seam
# (BatteryDriver.h) - with the host's battery driver and power switch
# (host/: the host's own battery - HostBattery.cpp: a board's gauge, or
# Windows', macOS's or Linux's - and newton's F12/F11 keys) standing in
# for the MP2x00's Cirrus driver and GPIO switch.
# The host's battery reading (host/HostBattery.h): a board's gauge, or
# Windows', macOS's or Linux's. Built without the Newton include paths:
# std::mutex and macOS's CoreFoundation include <locale.h>, which
# src/intl/Locale.h shadows on a case-insensitive file system, and MacTypes.h
# clashes with the DDK's NewtonTypes.h.
add_library(host_battery STATIC
    host/HostBattery.cpp
)
target_link_libraries(host_battery PUBLIC newton_config)
target_include_directories(host_battery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(APPLE)
    target_link_libraries(host_battery PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()

add_library(power STATIC
    PowerManager.cpp
    BatteryDriver.cpp
    host/HostBatteryDriver.cpp
    host/HostPowerSwitch.cpp
)
target_link_libraries(power PUBLIC newton_config protocols utility os600_user qd recognition stores frames hal_host host_battery)
target_include_directories(power PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
