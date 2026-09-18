# Host programs over the reconstructed OS: newtonscript, the NewtonScript
# compiler, interpreter and REP on the host (newtonscript.cpp), with the
# view system over a host display on request (HostViews.cpp); every
# reconstructed native bound in one place (HostNatives.cpp), and the store
# the host boots with (HostStores.cpp).
# the view system on the host (HostViews.h), for newtonscript and the newt
# world's boot
add_library(host_views STATIC HostViews.cpp HostNatives.cpp HostStores.cpp)
target_link_libraries(host_views PUBLIC frames views recognition pcmcia sound stores intl intl_meetings hal_host_display hal_host_tablet)
target_include_directories(host_views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(newtonscript newtonscript.cpp)
target_link_libraries(newtonscript PRIVATE host_views)
target_compile_definitions(newtonscript PRIVATE "NEWTON_DEFAULT_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../DebugRom/MP2100 D/Senior DCirrusNoDebug image\"")

# the demo: a view hierarchy drawn into build/views-demo.pgm (needs the ROM image)
add_test(NAME host.ViewsDemo COMMAND newtonscript --rom ${CMAKE_SOURCE_DIR}/../build/MP2100D/rom.bin --display 320x480 ${CMAKE_CURRENT_SOURCE_DIR}/demo/views.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.ViewsDemo PROPERTIES TIMEOUT 120)

# newton: the OS booted and the NewtonScript world run over a window on the
# host display (Windows: host/win32/HostWindow.cpp; elsewhere headless),
# the mouse the pen and the keys the keyboard (HostKeyboard.cpp, the
# keyboard tool's stand-in)
# (the window is a library of its own, built without the Newton include
# paths: src/intl/Locale.h would be found for the C library's <locale.h>
# on a case-insensitive file system, and the Windows headers do not mix
# with the DDK's)
add_library(host_window STATIC win32/HostWindow.cpp)
if(WIN32)
    target_link_libraries(host_window PUBLIC user32 gdi32)
endif()
add_executable(newton newton.cpp HostKeyboard.cpp)
target_link_libraries(newton PRIVATE newt host_views host_window)
target_compile_definitions(newton PRIVATE "NEWTON_DEFAULT_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../build/MP2100D/rom.bin\"")

# the newt world booted headless for two seconds with the views demo as its
# boot script (needs the ROM image)
add_test(NAME host.Newton COMMAND newton --rom ${CMAKE_SOURCE_DIR}/../build/MP2100D/rom.bin --headless 2 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/newton.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.Newton PROPERTIES TIMEOUT 120)
