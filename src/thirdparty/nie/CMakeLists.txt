# The Newton Internet Enabler's native-compiled NewtonScript (inetenbl.pkg's
# protoFSM engine), re-expressed and registered through frames/PackageNatives.h.
add_library(thirdparty_nie STATIC
    NIERuntime.cpp
    ProtoFSMQueue.cpp
    ProtoFSMEngine.cpp
    ProtoFSMEvents.cpp
    ProtoFSMPeriodic.cpp
    ProtoFSMClone.cpp
    ProtoFSMLoop.cpp
    ProtoFSMObjectToString.cpp
    ProtoFSMPrinter.cpp
    NIENatives.cpp
    LanternCardHandler.cpp
)
# pcmcia: TLanternCardHandler (newtdev.pkg's card handler); package_manager:
# the protocol stand-ins it is registered with
target_link_libraries(thirdparty_nie PUBLIC frames pcmcia package_manager)
target_include_directories(thirdparty_nie PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(tests)
