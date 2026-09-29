# The Newton Internet Enabler's native-compiled NewtonScript (inetenbl.pkg's
# protoFSM engine), re-expressed and registered through frames/PackageNatives.h.
add_library(thirdparty_nie STATIC
    NIERuntime.cpp
    ProtoFSMQueue.cpp
    ProtoFSMEngine.cpp
    ProtoFSMEvents.cpp
    NIENatives.cpp
)
target_link_libraries(thirdparty_nie PUBLIC frames)
target_include_directories(thirdparty_nie PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
