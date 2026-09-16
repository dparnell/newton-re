# Host programs over the reconstructed OS: newtonscript, the NewtonScript
# compiler, interpreter and REP on the host (newtonscript.cpp).
add_executable(newtonscript newtonscript.cpp)
target_link_libraries(newtonscript PRIVATE frames)
target_compile_definitions(newtonscript PRIVATE "NEWTON_DEFAULT_ROM_IMAGE=\"${CMAKE_SOURCE_DIR}/../DebugRom/MP2100 D/Senior DCirrusNoDebug image\"")
