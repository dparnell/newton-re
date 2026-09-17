# Host programs over the reconstructed OS: newtonscript, the NewtonScript
# compiler, interpreter and REP on the host (newtonscript.cpp), with the
# view system over a host display on request (HostViews.cpp).
add_executable(newtonscript newtonscript.cpp HostViews.cpp)
target_link_libraries(newtonscript PRIVATE frames views recognition hal_host_display hal_host_tablet)
target_compile_definitions(newtonscript PRIVATE "NEWTON_DEFAULT_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../DebugRom/MP2100 D/Senior DCirrusNoDebug image\"")

# the demo: a view hierarchy drawn into build/views-demo.pgm (needs the ROM image)
add_test(NAME host.ViewsDemo COMMAND newtonscript --rom ${CMAKE_SOURCE_DIR}/../build/MP2100D/rom.bin --display 320x480 ${CMAKE_CURRENT_SOURCE_DIR}/demo/views.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.ViewsDemo PROPERTIES TIMEOUT 120)
