# Communications: the option arrays the framework is configured through
# (docs/comms/README.md for the plan: the comm tools, the comm manager,
# the endpoints and the NewtonScript endpoint over them, and the host's
# network behind a host comm tool).
add_library(comms STATIC
    Options.cpp
    SerialOptions.cpp
    CommTools.cpp
    CommManager.cpp
    Endpoint.cpp
    SerialEndpoint.cpp
    EndpointPipe.cpp
    host/HostTCPTool.cpp
    host/HostServices.cpp
    host/HostEchoServer.cpp
    host/HostDNSTool.cpp
    host/HostLinkTool.cpp
)
# hal_host_sockets: the host's network, which host/HostTCPTool.cpp (the
# stand-in for the NIE's TCP tool) works over
target_link_libraries(comms PUBLIC newton_config os600_user memory utility hal_host_sockets)
target_include_directories(comms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/host)

# The serial tools (SerialTool.h): TSerTool, TAsyncSerTool and the 'aser
# service, over a serial chip from the registry (hal_serial) and the
# machine's timers (hal_timers); MNP; the modem tool (ModemTool.h: a Hayes
# modem, 'mods'); and the IR tools (SharpIRTool.h: Sharp
# IR, 'slir'; IrProbeTool.h: the probe, 'pkir', over IrSIR.h's SIR framing).
add_library(comms_serial STATIC
    SerialTool.cpp
    AsyncSerialTool.cpp
    FramedSerialTool.cpp
    MNP.cpp
    MNPClass5.cpp
    MNPClass5Tables.cpp
    V42bis.cpp
    ModemTool.cpp
    ModemToolCommands.cpp
    ModemToolConnect.cpp
    ModemToolFax.cpp
    ModemToolOptions.cpp
    ModemStrings.cpp
    fax/T4FaxLine.cpp
    fax/T4Tables.cpp
    SharpIRTool.cpp
    IrSIR.cpp
    IrProbeTool.cpp
    IrDATables.cpp
    irda/IrQOS.cpp
    irda/IrDscInfo.cpp
    irda/IrIASService.cpp
    irda/IrStream.cpp
    irda/IrGlue.cpp
    irda/IrIAS.cpp
    irda/IrLMP.cpp
    irda/IrLAPConn.cpp
    irda/IrLAP.cpp
    irda/IrDATool.cpp
)
target_link_libraries(comms_serial PUBLIC comms hal_serial hal_timers)
# the IrDA stack ('irda': docs/comms/README.md, "Beaming - the plan")
target_include_directories(comms_serial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/irda ${CMAKE_CURRENT_SOURCE_DIR}/fax)

# The NewtonScript side: the frame translators (values and option frames
# to and from bytes and option arrays) and the NewtonScript endpoint.
# host/HostLink.ns, the host's link for the NIE, is compiled in as a string
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostLinkSource.cpp
    COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/host/HostLink.ns
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/HostLinkSource.cpp -DNAME=gHostLinkSource
            -P ${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/host/HostLink.ns ${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake
    VERBATIM)
add_library(comms_script STATIC
    Translators.cpp
    CommTrace.cpp
    HostOptionLayouts.cpp
    NewScriptEndpoint.cpp
    StreamingEndpoint.cpp
    Beamer.cpp
    host/HostLink.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/HostLinkSource.cpp
)
# newt: ExceptionNotify and CheckForDeferredActions; views: the root view
# brought up to date after an endpoint event
target_link_libraries(comms_script PUBLIC comms frames stores views newt)

# The dock (docs/comms/README.md, "The desktop connection"): the easy
# endpoint pipe the docker talks through, made from the Connection
# application's options frame (so over comms_script's translators), the
# modem navigator every such endpoint goes through, and the docker - the
# 'dock' protocol's session and its natives (over packages, for a package
# the desktop loads, and newt, whose world it forks); and protoEndpoint, the
# 1.x NewtonScript endpoint (ScriptEndpoint.h), which runs the modem
# navigator too.
add_library(comms_dock STATIC
    EzEndpointPipe.cpp
    ModemNavigator.cpp
    Docker.cpp
    ModemOptions.cpp
    ScriptEndpoint.cpp
)
target_link_libraries(comms_dock PUBLIC comms_serial comms_script packages stores newt intl qd utility)

# The fax tool (fax/FaxTool.h: T.30 over the modem tool, 'faxs'); a page
# received into a large binary is flushed through the stores' large objects.
add_library(comms_fax STATIC
    fax/FaxTool.cpp
    fax/FaxToolPhases.cpp
    fax/FaxToolClass2.cpp
    fax/FaxToolInterface.cpp
)
# (TFaxToolInterface runs the modem navigator and takes the dialling
# preferences: comms_dock)
target_link_libraries(comms_fax PUBLIC comms_serial comms_dock stores)

# The Newton Toolkit's inspector connection (NTK.h): the debugger nub the
# desktop's NTK talks to, its task and endpoint client (over the dock's easy
# options), the REP translators it swaps in, and the tethered listener.
add_library(comms_ntk STATIC
    NTKTranslators.cpp
    NTKNub.cpp
)
target_link_libraries(comms_ntk PUBLIC comms_dock comms_script packages stores views newt frames utility)

add_subdirectory(tests)
