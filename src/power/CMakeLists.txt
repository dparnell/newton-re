# The power manager and the machine's sleep (PowerManager.h): the 'pg&e
# world the power switch and the batteries' readings go to, CyclePower
# over the platform's power (hal/Power.h), and the battery driver's seam
# (BatteryDriver.h) - with the host's battery driver and power switch
# (host/: the host's own battery - HostBattery.cpp: a board's gauge, or
# Windows', macOS's or Linux's - and newton's F12/F11 keys) standing in
# for the MP2x00's Cirrus driver and GPIO switch.
add_library(power STATIC
    PowerManager.cpp
    BatteryDriver.cpp
    host/HostBattery.cpp
    host/HostBatteryDriver.cpp
    host/HostPowerSwitch.cpp
)
target_link_libraries(power PUBLIC newton_config protocols utility os600_user qd recognition stores frames hal_host)
target_include_directories(power PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# (macOS's battery is IOKit's power sources, in a library of its own built
# without the Newton include paths: IOKit's MacTypes.h and the DDK's
# NewtonTypes.h name the same types - host/iokit/HostBatteryIOKit.cpp)
if(APPLE)
    add_library(power_host_iokit STATIC host/iokit/HostBatteryIOKit.cpp)
    target_include_directories(power_host_iokit PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(power_host_iokit PUBLIC "-framework IOKit" "-framework CoreFoundation")
    target_link_libraries(power PRIVATE power_host_iokit)
endif()

add_subdirectory(tests)
