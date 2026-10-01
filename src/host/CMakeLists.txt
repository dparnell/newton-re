# Host programs over the reconstructed OS: newtonscript, the NewtonScript
# compiler, interpreter and REP on the host (newtonscript.cpp), with the
# view system over a host display on request (HostViews.cpp); every
# reconstructed native bound in one place (HostNatives.cpp), and the store
# the host boots with (HostStores.cpp).
# the view system on the host (HostViews.h), for newtonscript and the newt
# world's boot
add_library(host_views STATIC HostViews.cpp HostNatives.cpp HostStores.cpp HostCards.cpp HostInterconnect.cpp FactorySoups.cpp HostObjectsFile.cpp)
target_link_libraries(host_views PUBLIC flash frames comms_script comms_dock comms_fax thirdparty_nie views recognition pcmcia sound system stores packages intl intl_meetings assist hal_host_display hal_host_tablet)
target_include_directories(host_views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(newtonscript newtonscript.cpp NSRoundTrip.cpp IMATool.cpp)
target_link_libraries(newtonscript PRIVATE host_views)
target_compile_definitions(newtonscript PRIVATE "NEWTON_DEFAULT_OBJECTS=\"${NEWTON_OBJECTS_FILE}\"")

# What the OS boots from: the object file built from the committed ROM
# source tree (romsrc/README.md) by romsrc.py and the host's own
# newtonscript, which compiles the tree's functions - no ROM image and no
# build/${NEWTON_ROM_BUILD} anywhere.  It is part of the default build and
# is made again only when a file of romsrc/, the builder's Python or
# newtonscript changes.  newton and newtonscript find it beside themselves
# (installed), in the build directory (NEWTON_DEFAULT_OBJECTS), or where
# NEWTON_OBJECTS says (host/HostObjectsFile.h).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/../romsrc/layout.tsv)
    file(GLOB_RECURSE romsrc_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/../romsrc/*)
    file(GLOB romsrc_tools CONFIGURE_DEPENDS
         ${CMAKE_SOURCE_DIR}/../tools/newton-rom/analysis/*.py
         ${CMAKE_SOURCE_DIR}/../tools/newton-rom/newtonrom/*.py
         ${CMAKE_SOURCE_DIR}/../tools/fonts/*.py
         ${CMAKE_SOURCE_DIR}/../tools/imaging/*.py)
    add_custom_command(OUTPUT ${NEWTON_OBJECTS_FILE}
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/newton-rom/analysis/romsrc.py
                     build ${CMAKE_SOURCE_DIR}/../romsrc --relayout -o ${NEWTON_OBJECTS_FILE}
                     --newtonscript $<TARGET_FILE:newtonscript>
             DEPENDS ${romsrc_sources} ${romsrc_tools} newtonscript
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..
             COMMENT "Building ${NEWTON_OBJECTS_FILE} from romsrc/"
             VERBATIM)
    # `cmake --build <dir> --target romsrc` builds it alone
    add_custom_target(romsrc ALL DEPENDS ${NEWTON_OBJECTS_FILE})
    install(FILES ${NEWTON_OBJECTS_FILE} DESTINATION bin)
else()
    message(WARNING "no Python 3 (or no romsrc/): the object file the OS boots from is not built - newton needs --rom <image>")
endif()
install(TARGETS newtonscript DESTINATION bin)

# the demo: a view hierarchy drawn into build/views-demo.pgm (needs the ROM image)
add_test(NAME host.ViewsDemo COMMAND newtonscript --display 320x480 ${CMAKE_CURRENT_SOURCE_DIR}/demo/views.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.ViewsDemo PROPERTIES TIMEOUT 120)
# drawing under a transform that scales: the world map at half size, shapes and text stretched
add_test(NAME host.ScaledMapDemo COMMAND newtonscript --display 320x480 ${CMAKE_CURRENT_SOURCE_DIR}/demo/scaledmap.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.ScaledMapDemo PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "the map is 360 by 179.*scaledmap: written")
# NewtonScript tracing: vars.trace 'functions, true and a one-slot frame (demo/trace.ns)
add_test(NAME host.NewtonScriptTrace COMMAND newtonscript ${CMAKE_CURRENT_SOURCE_DIR}/demo/trace.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonScriptTrace PROPERTIES TIMEOUT 60
                     PASS_REGULAR_EXPRESSION "trace: functions.*call with..3.*=> 8.*trace: everything.*<= ..[0-9A-F]+...trTwice.*=> 10.*trace: one slot.*0 <= ..[0-9A-F]+...counter.*counter := 1.*trace: done")

# newton: the OS booted and the NewtonScript world run over a window on the
# host display, the mouse the pen and the keys the keyboard
# (HostKeyboard.cpp, the keyboard tool's stand-in)
# (the window is a library of its own, built without the Newton include
# paths: src/intl/Locale.h would be found for the C library's <locale.h>
# on a case-insensitive file system, and the platform's headers do not mix
# with the DDK's - it gets src/host alone, for HostWindow.h and HostAudio.h)
# (and the loudspeaker beside it, the same reasons)
#
# One window and one sound implementation per host, chosen here:
#   Windows   win32/HostWindow.cpp (the message loop), win32/HostAudio.cpp (waveOut/waveIn)
#   Linux/BSD x11/HostWindow.cpp (X11, and Wayland through XWayland), alsa/HostAudio.cpp
# Where there is neither, the win32 files' own #else answers the calls and
# says there is no window and no device, so the world runs headless - which
# is what a host the port has not reached yet gets (macOS today).
if(WIN32)
    set(host_window_source win32/HostWindow.cpp)
    set(host_audio_source win32/HostAudio.cpp)
else()
    find_package(X11)
    find_path(ALSA_INCLUDE_DIR alsa/asoundlib.h)
    find_library(ALSA_LIBRARY asound)
    if(X11_FOUND)
        set(host_window_source x11/HostWindow.cpp)
    else()
        set(host_window_source win32/HostWindow.cpp)   # its #else: no window
        message(STATUS "no X11: newton runs headless (install the X11 development headers for a window)")
    endif()
    if(ALSA_INCLUDE_DIR AND ALSA_LIBRARY)
        set(host_audio_source alsa/HostAudio.cpp)
    else()
        set(host_audio_source win32/HostAudio.cpp)     # its #else: no device
        message(STATUS "no ALSA: newton runs silent (install the ALSA development headers for sound)")
    endif()
endif()
add_library(host_window STATIC ${host_window_source} ${host_audio_source})
target_include_directories(host_window PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_link_libraries(host_window PUBLIC user32 gdi32 shell32 winmm)
else()
    if(X11_FOUND)
        target_include_directories(host_window PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(host_window PUBLIC ${X11_LIBRARIES})
    endif()
    if(ALSA_INCLUDE_DIR AND ALSA_LIBRARY)
        target_include_directories(host_window PRIVATE ${ALSA_INCLUDE_DIR})
        target_link_libraries(host_window PUBLIC ${ALSA_LIBRARY})
    endif()
endif()
add_executable(newton newton.cpp HostKeyboard.cpp HostPackages.cpp HostHeapCheck.cpp)
target_link_libraries(newton PRIVATE newt host_views host_window hal_host_sound hal_timers hal_host_serial)
target_compile_definitions(newton PRIVATE "NEWTON_DEFAULT_OBJECTS=\"${NEWTON_OBJECTS_FILE}\"")
install(TARGETS newton DESTINATION bin)

# the newt world booted headless for two seconds with the views demo as its
# boot script (needs the ROM image)
add_test(NAME host.Newton COMMAND newton --rom ${NEWTON_ROM_BIN} --headless 2 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/newton.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.Newton PROPERTIES TIMEOUT 120)
# RepeatInfoToText's phrases, MeasuredNumberStr's cuts, DrawOriginal's box,
# LoadFontCache, an expando drawn (DrawExpando), QuickLookDone
add_test(NAME host.NewtonNatives COMMAND newton --headless 30 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/natives.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonNatives PROPERTIES TIMEOUT 120
    PASS_REGULAR_EXPRESSION "Every Monday and Thursday.*Every day.*The second and last Friday of every month.*The 15th of every month.*Every March 4.*Every other Saturday.*The first Monday of every September.*kind 5., ...*fits., .3.14159.*cut., .3.14.\].*whole., NIL.*too narrow., NIL.*left: 29, top: 14, right: 69, bottom: 34.*left: 4, top: 39, right: 44, bottom: 59.*bad bundle., .bad strokeBounds.*fonts loaded.*expando asked.*Name., .Phone.*debugger., -10005, 0, 0, -10005, 0.*dispatch., -10005.*dispatch marshal., .evt.ex.marshal.type.*natives: done")
# a script's own gestalt registered, read back and replaced; UnmountCard's
# argument checks; BootSucceeded's file (made in the build directory)
add_test(NAME host.NewtonGestalt COMMAND newton --headless 30 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/gestalt.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonGestalt PROPERTIES TIMEOUT 120
    PASS_REGULAR_EXPRESSION "12, -34, TRUE.*replace., TRUE.*56, 78, NIL.*a system selector., NIL.*not a selector., NIL.*not a function., -10006.*empty socket., NIL.*card slot stores., NIL.*gestalt: done")
# the docker's natives (comms/Docker.h) on the Connection application's
# protocol frame, with no desktop
add_test(NAME host.NewtonDocker COMMAND newton --serial-port none --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/docker.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonDocker PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "docker: made yes.*version 9, state 1.*desktop type 2.*state now 4.*after abort gone.*docker: done")
# the desktop connection end to end: the Connection application's autodock
# over the host serial port (TCP) to tools/dock/dock.py, which answers
# 'rtdk' with a package to load ('lpkg') and says 'disc' after the 'dres'
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/../fixtures/packages/fonts/monaco.pkg)
    add_test(NAME host.NewtonDock
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/dock.py
                     --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/fonts/monaco.pkg
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --headless 150
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/dock.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonDock PROPERTIES TIMEOUT 240 PASS_REGULAR_EXPRESSION "the Newton's protocol version is 9.*monaco.pkg: dres 0.*dock: installed Monaco.*dock: done")
    # the same with a script walking the package list every tick meanwhile
    # (GetPackages forks the world and waits on gPackageSemaphore while the
    # docker's world takes and gives up the mutex): the semaphore stub's
    # retry and the serial interrupt's system call (docs/host-runtime.md)
    add_test(NAME host.NewtonDockGetPackages
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/dock.py
                     --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/fonts/monaco.pkg
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --headless 150
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/dock-getpackages.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonDockGetPackages PROPERTIES TIMEOUT 240 PASS_REGULAR_EXPRESSION "monaco.pkg: dres 0.*dock: installed Monaco.*dock: done" FAIL_REGULAR_EXPRESSION "panic|refused|fell over|waited in vain")
    # the same inside a docking session: 'dock', the Newton's name, the
    # desktop's and the Newton's info, and the password exchange first
    add_test(NAME host.NewtonDockSession
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/dock.py --session
                     --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/fonts/monaco.pkg
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --headless 150
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/dock.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonDockSession PROPERTIES TIMEOUT 240 PASS_REGULAR_EXPRESSION "the Newton speaks protocol 11.*the Newton knows the password.*docked.*stores: Internal.*the default store is Internal.*soups on Internal, the System soup among them.*the System soup made current: 0.*the System soup.s info.*added entry.*the new one among them.*reads .written by dock.py.*after .cent. it reads .changed by dock.py.*a 1.x entry added as [0-9].*entries all right.*soup .dock.py test. made: 0.*the backup sent ids ..0, -2, -32768...*the soup sent entries .0, 1, 2..*backup all right.*.test. echoed b.echo.*after a stray b.newtdockrtst.*classes in the inheritance: .*Max.3, 7. called on the Newton: 7.*the system patches: 0 bytes.*the slip was answered with button 1.*requests all right.*monaco.pkg: dres 0.*packages on the store: Monaco .17064 bytes.*Monaco removed .0., 0 left.*monaco.pkg loaded again: dres 0.*the keyboard passthrough started: True.*the Newton disconnected after the keyboard.*dock: the desktop.s slip says Tap the first button.*dock: installed Monaco.*dock: typed from the desktop: hello!.*dock: done")
    # beaming: two newtons, their IR ports facing each other over a socket
    # (--ir-peer), one beams a note (Sharp IR) and the other receives it
    # into its In Box (tools/host/twonewtons.py runs both)
    add_test(NAME host.NewtonBeam
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/twonewtons.py $<TARGET_FILE:newton>
 --seconds 120
                     --first ${CMAKE_CURRENT_SOURCE_DIR}/demo/beam-receive.ns
                     --expect-first "eceived: \"Beamed note\".*text: \"hello from the other Newton\".*beam-receive: the note is in the In Box"
                     --second ${CMAKE_CURRENT_SOURCE_DIR}/demo/beam-send.ns
                     --expect-second "queued: \"Beamed note\".*beam-send: the Out Box is empty"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonBeam PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "twonewtons.py: both ended as expected")
    # Newt's Cape's RoutBook (rtbk10c2.pkg) used as its user would: the help
    # book NC_HELP.pkg opened from Extras, its Action button pressed twice -
    # the book's own routing, then RoutBook's text - and Beam Book picked;
    # the page arrives on the other newton as text (demo/rtbk-send.ns,
    # rtbk-receive.ns)
    if(EXISTS ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/rtbk10c2.pkg)
        add_test(NAME host.NewtonAppRoutBook
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/twonewtons.py $<TARGET_FILE:newton>
                         --seconds 150
                         --newton-arg=--package --newton-arg=${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/NC_HELP.pkg
                         --newton-arg=--package --newton-arg=${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/rtbk10c2.pkg
                         --first ${CMAKE_CURRENT_SOURCE_DIR}/demo/rtbk-receive.ns
                         --expect-first "rtbk-receive: the page came as text"
                         --second ${CMAKE_CURRENT_SOURCE_DIR}/demo/rtbk-send.ns
                         --expect-second "first press: Print Book.*second press: Beam Book.*rtbk-send: the Out Box is empty.*rtbk-send: done, 0 failed"
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
        set_tests_properties(host.NewtonAppRoutBook PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "twonewtons.py: both ended as expected"
                             FAIL_REGULAR_EXPRESSION "waited in vain|FAILED")
    endif()
    # the same over the default path: no zapCommToolId, so the probe and then IrDA
    add_test(NAME host.NewtonBeamIrDA
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/twonewtons.py $<TARGET_FILE:newton>
                     --seconds 300
                     --first ${CMAKE_CURRENT_SOURCE_DIR}/demo/beam-irda-receive.ns
                     --expect-first "eceived: \"Beamed note\".*text: \"hello from the other Newton\".*beam-receive: the note is in the In Box"
                     --second ${CMAKE_CURRENT_SOURCE_DIR}/demo/beam-irda-send.ns
                     --expect-second "queued: \"Beamed note\".*beam-send: the Out Box is empty"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonBeamIrDA PROPERTIES TIMEOUT 400 PASS_REGULAR_EXPRESSION "twonewtons.py: both ended as expected"
                         FAIL_REGULAR_EXPRESSION "waited in vain|was not sent")
    # the Newton Toolkit's inspector (comms/NTK.h): ntkListener over MNP on
    # the serial port to tools/ntk/inspector.py, which sets the timeout,
    # installs and deletes a package, evaluates expressions (one sending an
    # object back with NTKSend), sends lines to the REP (their output, and
    # three exceptions: a ref, a message and an error) and tells the script
    # to close the listener; then ntkDownload, answered with the package
    add_test(NAME host.NewtonNTK
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/ntk/inspector.py
                     --timeout 45
                     --install ${CMAKE_SOURCE_DIR}/../fixtures/packages/fonts/monaco.pkg --delete Monaco
                     --eval "1 + 2" --eval "NTKSend({answer: 42}); 'sent" --eval "foreach p in GetPackages() collect p.title"
                     --line "Print(\"hello from the inspector\")" --line "Throw(x, 1)"
                     --line "Throw('|evt.ex.msg|, \"oops\")" --line "Throw('|evt.ex.foo|, -8007)"
                     --finish "vars.ntkInspected := true"
                     --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/fonts/monaco.pkg
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --headless 150
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/ntk.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonNTK PROPERTIES TIMEOUT 240
                         PASS_REGULAR_EXPRESSION "connected .'cnnt' answered 'okln'.*timeout set: rslt 0.*monaco.pkg installed: rslt 0.*Monaco deleted: rslt 0.*1 . 2 => 3.*<- fobj {'answer: 42}.*=> 'Sent.*=> \\['Cardfile'.*<- text '\"hello from the inspector\"'.*<- eref .'evt.ex.fr.intrp;type.ref.frame'.*<- estr .'evt.ex.msg', 'oops'.*<- eerr .'evt.ex.foo', -8007.*listener: 0, alive: TRUE.*monaco.pkg: rslt 0.*ntk: download 0.*ntk: installed Monaco.*ntk: done.*exit status 0"
                         FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
    # the modem tool (comms/ModemTool.h, serv 'mods') on the serial port,
    # tools/modem/fakemodem.py the Hayes modem at its other end: dialing
    # out to the echo server --tcp-echo runs (the modem's phone book maps
    # the number to it), and answering a call it rings the Newton with
    # the docking loader: a device on the serial port (tools/dock/scpdevice.py)
    # says who it is and sends its package, which is stored and installed
    add_test(NAME host.NewtonSCPLoad
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/scpdevice.py
                     --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/fonts/monaco.pkg
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --headless 150
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/scpload.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonSCPLoad PROPERTIES TIMEOUT 240
                         PASS_REGULAR_EXPRESSION "scpdevice: heard n_id sire csre.*scpdevice: heard rese.*the Newton said abrt 1.*scpload: the device is tdev by appl.*scpload: installed Monaco.*scpload: done.*the program answered 0"
                         FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
    add_test(NAME host.NewtonModemDial
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/fakemodem.py
                     --number 5551212=echo
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --tcp-echo 0 --headless 120
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/modem.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonModemDial PROPERTIES TIMEOUT 200
                         PASS_REGULAR_EXPRESSION "<- ATI4.*<- ATDT5551212.*-> CONNECT 19200.*escaped to commands.*<- ATH0.*modem: received hello.*modem: line of 19 characters.*modem: done.*the program answered 0"
                         FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
    add_test(NAME host.NewtonModemAnswer
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/fakemodem.py
                     --incoming echo
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --tcp-echo 0 --headless 120
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/modem-answer.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonModemAnswer PROPERTIES TIMEOUT 200
                         PASS_REGULAR_EXPRESSION "<- ATS1.*-> RING.*-> 002.*<- ATA.*-> CONNECT 19200.*<- ATH0.*modem-answer: received hello.*modem-answer: line of 23 characters.*modem-answer: done.*the program answered 0"
                         FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
    # a fax received: fakemodem.py's Class 1 fax call (--fax-call, the page
    # t4.py's test page) answered by the ROM's fax transport over the fax
    # tool (comms/fax/FaxTool.h) and the modem tool; the page is confirmed
    # (MCF), lands in the In Box, is shown by the FaxViewer and turned right
    # (qd/Tile.h) - build/fax-received.pgm, build/fax-rotated.pgm
    add_test(NAME host.FaxPage
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/t4.py --test-page
                     --pbm ${CMAKE_CURRENT_BINARY_DIR}/faxpage.pbm)
    set_tests_properties(host.FaxPage PROPERTIES FIXTURES_SETUP faxpage)
    add_test(NAME host.NewtonFaxReceive
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/fakemodem.py
                     --fax-call ${CMAKE_CURRENT_BINARY_DIR}/faxpage.pbm
                     --spawn $<TARGET_FILE:newton> --display 320x480 --serial-port 0 --headless 200
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/fax-receive.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonFaxReceive PROPERTIES TIMEOUT 300 FIXTURES_REQUIRED faxpage
                         PASS_REGULAR_EXPRESSION "<- ATA.*answered a fax call.*fax: <- DIS.*fax: -> DCS.*fax: <- CFR.*fax: -> the page, 200 lines.*fax: -> EOP.*fax: the page confirmed MCF.*fax: DCN sent.*eceived: \"From .*fakemodem fax.*fax-receive: shown, the pixels 247564 bytes.*fax-receive: turned right, the pixels 248860 bytes.*fax-receive: done.*the program answered 0"
                         FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
    # the same fax received over a Class 2 modem (--fax-class 2: the modem
    # runs T.30, the fax tool asks for the page with +FDR and reverses its
    # bits - comms/fax/FaxToolClass2.cpp, FaxToolPhases.cpp's ReverseBits)
    add_test(NAME host.NewtonFaxReceiveClass2
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/fakemodem.py
                     --fax-class 2 --fax-call ${CMAKE_CURRENT_BINARY_DIR}/faxpage.pbm
                     --spawn $<TARGET_FILE:newton> --display 320x480 --serial-port 0 --headless 200
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/fax-receive.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonFaxReceiveClass2 PROPERTIES TIMEOUT 300 FIXTURES_REQUIRED faxpage
                         PASS_REGULAR_EXPRESSION "<- ATA.*answered a fax call \\(Class 2\\).*<- AT\\+FDR.*fax: -> the page, 200 lines.*fax: -> EOP.*<- AT\\+FPTS=1.*fax: -> DCN.*eceived: \"From .*fakemodem fax.*fax-receive: shown, the pixels 247564 bytes.*fax-receive: turned right, the pixels 248860 bytes.*fax-receive: done.*the program answered 0"
                         FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
    # a fax sent: a note through the fax routing slip and the ROM's fax
    # transport, printed by the print view (views/PrintView.h), the dot
    # printer and the fax driver (print/FaxDriver.h) over the fax tool, to
    # fakemodem.py answering as a Class 1 fax machine (--fax-answer); then
    # tools/modem/faxcheck.py checks the two pages (the cover page's title
    # and rule where the Sending slip lies on the screen, the note's text)
    # and makes PNGs of them - build/fax-sent.png, build/fax-sent-2.png
    add_test(NAME host.NewtonFaxSend
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/fakemodem.py
                     --fax-answer build/fax-sent.pbm
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --headless 200
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/fax-send.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonFaxSend PROPERTIES TIMEOUT 300 FIXTURES_SETUP faxsent
                         PASS_REGULAR_EXPRESSION "fax: <- DCS.*fax: -> CFR.*fax: <- page 1, [0-9]+ bytes, 2148 lines.*fax: <- MPS.*fax: <- page 2, [0-9]+ bytes, 2148 lines.*fax: <- EOP.*fax: <- DCN.*2 page\\(s\\) received.*fax-send: the Out Box is empty.*the program answered 0"
                         FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
    add_test(NAME host.NewtonFaxSend.check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/faxcheck.py build/fax-sent.pbm
                     --pages 2 --width 1728
                     --inked 1:190,180,750,300:1000 --inked 1:190,320,750,340:2500
                     --inked 2:180,95,700,160:1000
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonFaxSend.check PROPERTIES FIXTURES_REQUIRED faxsent
                         PASS_REGULAR_EXPRESSION "faxcheck: passed")
    # the same over a Class 2 modem (--fax-class 2: the modem runs T.30 and
    # the fax tool drives it with +FDT/+FET - comms/fax/FaxToolClass2.cpp)
    add_test(NAME host.NewtonFaxSendClass2
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/fakemodem.py
                     --fax-class 2 --fax-answer build/fax-sent-class2.pbm
                     --spawn $<TARGET_FILE:newton> --serial-port 0 --headless 200
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/fax-send.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonFaxSendClass2 PROPERTIES TIMEOUT 300 FIXTURES_SETUP faxsentclass2
                         PASS_REGULAR_EXPRESSION "-> 0,2.*<- AT\\+FCLASS=2.*a fax machine answers \\(Class 2\\).*<- AT\\+FDT.*fax: <- page 1, [0-9]+ bytes, 2148 lines.*fax: <- MPS.*fax: <- page 2, [0-9]+ bytes, 2148 lines.*fax: <- EOP.*2 page\\(s\\) received.*fax-send: the Out Box is empty.*the program answered 0"
                         FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
    add_test(NAME host.NewtonFaxSendClass2.check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/faxcheck.py build/fax-sent-class2.pbm
                     --pages 2 --width 1728
                     --inked 1:190,180,750,300:1000 --inked 1:190,320,750,340:2500
                     --inked 2:180,95,700,160:1000
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonFaxSendClass2.check PROPERTIES FIXTURES_REQUIRED faxsentclass2
                         PASS_REGULAR_EXPRESSION "faxcheck: passed")
endif()

# Beam with nobody in front of the IR port (newton always has one): the
# status slip looks for a receiver, then the alert says there was no response
add_test(NAME host.NewtonBeamNobody COMMAND newton --display 320x480 --headless 300 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/beam-nobody.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonBeamNobody PROPERTIES TIMEOUT 400
                     PASS_REGULAR_EXPRESSION "beam: the status slip says Looking for receiver.*beam: the alert says .*No response.*beam: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain|fell over")

# the machine's power (power/PowerManager.h): the batteries through the
# power manager and a battery gauge, the backlight on and off by its
# button, the machine asleep by the idle timer and by the power switch and
# woken by a tap each time, the power-off and power-on functions called
add_test(NAME host.NewtonPower COMMAND newton --display 320x480 --headless 200 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/power.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonPower PROPERTIES TIMEOUT 300
                     PASS_REGULAR_EXPRESSION "power: 1 battery, [0-9]+% .*power: the battery gauge shows [0-9]+ .*power: backlight on.*power: the backlight button turned it off.*power: the idle timer put it to sleep, a tap woke it.*power: the switch put it to sleep, a tap woke it; 2 power-off calls, 2 power-on calls.*power: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain|fell over")

# the tablet's calibration (recognition/Inker.h, hal/host/HostTablet.h;
# demo/alignpen.ns): on a panel put askew, the Setup assistant's
# calibration screen tapped through it, a tap then reading true, off again
# under the factory calibration and right after Align Pen, a sleep and a
# wake; then a restart on the same store with the calibration read back
add_test(NAME host.NewtonAlignPen
         COMMAND newton --display 320x480 --erase
                 --store ${CMAKE_CURRENT_BINARY_DIR}/alignpen.store --headless 200
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/alignpen.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonAlignPen PROPERTIES TIMEOUT 300 FIXTURES_SETUP host_alignpen_store
                     PASS_REGULAR_EXPRESSION "alignpen: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")
add_test(NAME host.NewtonAlignPen.restart
         COMMAND newton --display 320x480
                 --store ${CMAKE_CURRENT_BINARY_DIR}/alignpen.store --headless 120
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/alignpen.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonAlignPen.restart PROPERTIES TIMEOUT 180
                     FIXTURES_REQUIRED host_alignpen_store DEPENDS host.NewtonAlignPen
                     PASS_REGULAR_EXPRESSION "alignpen: restart done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# the inker's live ink and busy box (recognition/Inker.h, LiveInker.h;
# demo/liveink.ns): a stroke with the pen held down inked on the display,
# the busy box shown and taken away
add_test(NAME host.NewtonLiveInk COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/liveink.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonLiveInk PROPERTIES TIMEOUT 150
                     PASS_REGULAR_EXPRESSION "liveink: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# the host's printer (print/host/HostPrinter.h): a note and a Names card
# printed from their Action buttons, the printer chosen in the Print slip's
# Choose Other Printer; each page a PNG in build/print, which
# tools/imaging/pagecheck.py checks (the note's text, the card's frame and
# its address inked)
add_test(NAME host.NewtonHostPrinter.clean COMMAND ${CMAKE_COMMAND} -E rm -rf build/print WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonHostPrinter.clean PROPERTIES FIXTURES_SETUP hostprintclean)
add_test(NAME host.NewtonHostPrinter.mkdir COMMAND ${CMAKE_COMMAND} -E make_directory build/print WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonHostPrinter.mkdir PROPERTIES FIXTURES_SETUP hostprintclean DEPENDS host.NewtonHostPrinter.clean)
add_test(NAME host.NewtonHostPrinter COMMAND newton --display 320x480 --headless 200 --print-dir build/print --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/print.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonHostPrinter PROPERTIES TIMEOUT 300 FIXTURES_REQUIRED hostprintclean FIXTURES_SETUP hostprinted
                     PASS_REGULAR_EXPRESSION "note: chose Host printer \\(PNG files\\).*printed page 1 to build/print/print-001.png.*card: the printer is Host printer \\(PNG files\\).*printed page 2 to build/print/print-002.png.*print: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
if(Python3_FOUND)
    add_test(NAME host.NewtonHostPrinter.check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/imaging/pagecheck.py build/print
                     --pages 2 --size 2550x3300
                     --inked 1:320,240,1450,312:2000
                     --inked 2:768,1220,1776,1892:3000 --inked 2:1080,1430,1530,1520:1000
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonHostPrinter.check PROPERTIES FIXTURES_REQUIRED hostprinted
                         PASS_REGULAR_EXPRESSION "pagecheck: passed")
endif()

# the pen's clicks (demo/clicks.ns): a tap on a note's text, a button and a
# pick in its menu each give the host's sound driver something to play
add_test(NAME host.NewtonClicks COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/clicks.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonClicks PROPERTIES TIMEOUT 150
                     PASS_REGULAR_EXPRESSION "text clicks: ok.*Action button clicks: ok.*menu clicks: ok.*clicks: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# the boot's splash screen (demo/splash.ns): the logo and the version and
# copyright lines TNotebook::DrawSplashScreen draws, still on the display
# while the boot script runs
add_test(NAME host.NewtonSplash COMMAND newton --display 320x480 --headless 60
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/splash.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonSplash PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "splash: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED")

# dates, times and numbers read from text (demo/dateparse.ns): the natives
# over the locale's lexical dictionaries (intl/LexParse.h, ParseString),
# and the Assistant's "lunch at 1 pm tomorrow" opening its meeting slip at
# one o'clock tomorrow
add_test(NAME host.NewtonDateParse COMMAND newton --display 320x480 --headless 120
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/dateparse.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonDateParse PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "lunch at 1 pm tomorrow: ok.*dateparse: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# the word hints (demo/hints.ns): the WorldData package's 753 entries,
# written with their hint chunks by Apple's tools, copied onto the internal
# store - the chunks the host writes must be the same bytes - and words
# queries over the copies finding what the package's soups find
add_test(NAME host.NewtonWordHints COMMAND newton --display 320x480 --headless 120
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/hints.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonWordHints PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "hints: 753 entries, 759 chunks, 0 differ.*Zzyzx: 0 / 0: ok.*hints: done"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain|hints: differ")

# a store the host wrote before it wrote word hints (demo/hintsrepair.ns):
# written with NEWTON_NO_WORD_HINTS - no chunks, handler 0, missed by a
# words query - then mounted again, its entries' hints rewritten
# (stores/Soups.cpp RepairWordHints, DEVIATION) and found
add_test(NAME host.NewtonWordHintsOld
         COMMAND newton --display 320x480 --erase
                 --store ${CMAKE_CURRENT_BINARY_DIR}/hintsrepair.store --headless 60
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/hintsrepair.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonWordHintsOld PROPERTIES TIMEOUT 120 FIXTURES_SETUP host_hintsrepair_store
                     ENVIRONMENT "NEWTON_NO_WORD_HINTS=1"
                     PASS_REGULAR_EXPRESSION "hintsrepair: written"
                     FAIL_REGULAR_EXPRESSION "FAILED")
add_test(NAME host.NewtonWordHintsRepair
         COMMAND newton --display 320x480
                 --store ${CMAKE_CURRENT_BINARY_DIR}/hintsrepair.store --headless 60
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/hintsrepair.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonWordHintsRepair PROPERTIES TIMEOUT 120
                     FIXTURES_REQUIRED host_hintsrepair_store DEPENDS host.NewtonWordHintsOld
                     PASS_REGULAR_EXPRESSION "rewrote word hints for [0-9]+ entries.*hintsrepair: repaired"
                     FAIL_REGULAR_EXPRESSION "FAILED")

# a store an older host wrote, its reals and its text in large binaries in
# the host's byte order (demo/byteorder.ns): written with
# NEWTON_OLD_BYTE_ORDER (the store file unmarked, hal/host/HostFlash.h),
# then mounted again and turned round (stores/Soups.cpp
# RepairHostByteOrder, DEVIATION), then mounted a third time and left alone
add_test(NAME host.NewtonByteOrderOld
         COMMAND newton --display 320x480 --erase
                 --store ${CMAKE_CURRENT_BINARY_DIR}/byteorder.store --headless 60
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/byteorder.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonByteOrderOld PROPERTIES TIMEOUT 120 FIXTURES_SETUP host_byteorder_store
                     ENVIRONMENT "NEWTON_OLD_BYTE_ORDER=1"
                     PASS_REGULAR_EXPRESSION "byteorder: written"
                     FAIL_REGULAR_EXPRESSION "FAILED")
add_test(NAME host.NewtonByteOrderRepair
         COMMAND newton --display 320x480
                 --store ${CMAKE_CURRENT_BINARY_DIR}/byteorder.store --headless 60
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/byteorder.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonByteOrderRepair PROPERTIES TIMEOUT 120
                     FIXTURES_REQUIRED host_byteorder_store DEPENDS host.NewtonByteOrderOld
                     PASS_REGULAR_EXPRESSION "turned the reals and text of [0-9]+ entries.*byteorder: repaired"
                     FAIL_REGULAR_EXPRESSION "FAILED")
add_test(NAME host.NewtonByteOrderRepair.again
         COMMAND newton --display 320x480
                 --store ${CMAKE_CURRENT_BINARY_DIR}/byteorder.store --headless 60
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/byteorder.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonByteOrderRepair.again PROPERTIES TIMEOUT 120
                     FIXTURES_REQUIRED host_byteorder_store DEPENDS host.NewtonByteOrderRepair
                     PASS_REGULAR_EXPRESSION "byteorder: repaired"
                     FAIL_REGULAR_EXPRESSION "FAILED|turned the reals")

# ink read again (demo/rereadink.ns): an ink word and two shapes of ink
# lassoed with the pen and double tapped - the page's aeDoubleTap on its
# selection (views/EditView.cpp RereadSelectedInk), the shapes sorted into
# reading order (views/SortInk.cpp)
add_test(NAME host.NewtonRereadInk COMMAND newton --display 320x480 --headless 120
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/rereadink.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonRereadInk PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "in reading order: ok.*rereadink: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# two ink words joined with the pen (demo/joinink.ns): the join caret
# drawn upside down between them - TParagraphView::CheckAndDoJoin's ink half
add_test(NAME host.NewtonJoinInk COMMAND newton --display 320x480 --headless 120
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/joinink.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonJoinInk PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "joined into one ink word: ok.*joinink: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# the caret gestures on a line of writing, drawn with the pen
# (demo/caretspace.ns): the join closes a space, a caret opens one, and a
# caret with a tail opens an insert area that a word is written into, the
# white space left over taken out once the pen rests
# (TParagraphView::CheckAndDoJoin, InsertHorizontalSpace, Idle reason 1)
add_test(NAME host.NewtonCaretSpace COMMAND newton --display 320x480 --headless 120
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/caretspace.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonCaretSpace PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "the spaces left over go: \"ton to to\": ok.*caretspace: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# ink dropped on a paragraph with the pen (demo/dropink.ns): a shape of ink
# lassoed and dragged onto a paragraph of text goes in as an ink word
# (TParagraphView::Drop, InkConvert)
add_test(NAME host.NewtonDropInk COMMAND newton --display 320x480 --headless 120
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/dropink.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonDropInk PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "as an ink word: ok.*dropink: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# a word selected with the hilite stroke and dragged with a paced pen - notices a paced pen that arrives all at once (demo/drag.ns)
add_test(NAME host.NewtonDrag COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/drag.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonDrag PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "dragged 80 pixels down: ok.*drag: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# "ton" and "to" written with the pen and read by Rosetta, the second at the first's caret (demo/write.ns)
add_test(NAME host.NewtonWrite COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/write.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonWrite PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "reads \"ton to\": ok.*write: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# a word scrubbed out with the pen (demo/scrub.ns)
add_test(NAME host.NewtonScrub COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/scrub.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonScrub PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "nothing in its place: ok.*scrub: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# the corrector opened by a double tap and a reading picked from it (demo/correct.ns)
add_test(NAME host.NewtonCorrect COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/correct.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonCorrect PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "reads \"tor\": ok.*correct: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# a line, a box, a triangle and a circle drawn and recognised as shapes (demo/shapes.ns)
add_test(NAME host.NewtonShapes COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/shapes.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonShapes PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "four shapes on the page: ok.*shapes: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# shapes snapped to the shapes already on the page (demo/snapping.ns)
add_test(NAME host.NewtonSnapping COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/snapping.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonSnapping PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "from the box.s corner: ok.*snapping: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# ModalConfirm answered by a tap on OK in the fork (demo/modal.ns)
add_test(NAME host.NewtonModal COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/modal.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonModal PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "modal: asking.*modal: tapping OK.*answered TRUE.*still running.*modal: done"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# "call Daniel" through the Assistant to the Call slip (demo/assist.ns)
add_test(NAME host.NewtonAssist COMMAND newton --display 320x480 --headless 90
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/assist.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonAssist PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "addressed to Daniel: ok.*assist: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# every built-in application opened and closed (a smoke test) (demo/open-apps.ns)
add_test(NAME host.NewtonOpenApps COMMAND newton --display 320x480 --headless 300
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/open-apps.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonOpenApps PROPERTIES TIMEOUT 360
                     PASS_REGULAR_EXPRESSION "open-apps: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# each of the Assistant's tasks carried out (a smoke test) (demo/assist-tasks.ns)
add_test(NAME host.NewtonAssistTasks COMMAND newton --display 320x480 --headless 300
         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/assist-tasks.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonAssistTasks PROPERTIES TIMEOUT 360
                     PASS_REGULAR_EXPRESSION "assist-tasks: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# a long note printed across pages (demo/printlong.ns): the Notepad's print
# format reflows the note's paragraph into each page and cuts it between
# two lines where the page runs out (views/Reflow.cpp ReflowText, the
# paragraph's lines kept only inside its bounds with text flag 0x800);
# pagecheck.py counts the pages and the lines, none cut in half
add_test(NAME host.NewtonPrintLong.clean COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/printlong)
set_tests_properties(host.NewtonPrintLong.clean PROPERTIES FIXTURES_SETUP printlongclean)
add_test(NAME host.NewtonPrintLong.mkdir COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/printlong)
set_tests_properties(host.NewtonPrintLong.mkdir PROPERTIES FIXTURES_SETUP printlongclean DEPENDS host.NewtonPrintLong.clean)
add_test(NAME host.NewtonPrintLong COMMAND newton --display 320x480 --headless 240
         --print-dir ${CMAKE_CURRENT_BINARY_DIR}/printlong --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/printlong.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonPrintLong PROPERTIES TIMEOUT 300 FIXTURES_REQUIRED printlongclean FIXTURES_SETUP printlongprinted
                     PASS_REGULAR_EXPRESSION "calculatesBounds: TRUE.*printlong: 3 page.s. printed.*printlong: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain|fell over")
if(Python3_FOUND)
    add_test(NAME host.NewtonPrintLong.check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/imaging/pagecheck.py ${CMAKE_CURRENT_BINARY_DIR}/printlong
                     --pages 3 --size 2550x3300 --lines 80
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPrintLong.check PROPERTIES FIXTURES_REQUIRED printlongprinted
                         PASS_REGULAR_EXPRESSION "pagecheck: passed")
endif()

# deferred recognition (views/Rerecognize.h): "ton" written, and the
# same strokes read again through Recognize, RecognizeInkWord,
# RecognizeTextInStyles and RecognizePara (needs the ROM image)
add_test(NAME host.NewtonRecognize COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/recognize.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonRecognize PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "Recognize read ton.*unread strokes came back as nothing.*RecognizeInkWord read ton.*RecognizeTextInStyles made \"an ton\".*RecognizePara made \"ton ton\", the range ending at 7.*ink came back as a piece of 4 strokes.*ink came back as a piece of 4 strokes.*recognize: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the correction and word-unit methods on a paragraph, the correction info
# and a view of its own (GetAlternatives, Extract, insert, HandleRawInk,
# VoteOnWordUnit, HandleUnit)
add_test(NAME host.NewtonAlternatives COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/alternatives.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonAlternatives PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "GetAlternatives offered 6 words, the first \"ton\", with strokes.*Extract took 1 word info.*Extract past the text took nothing.*insert put the range back.*HandleRawInk answered nil.*VoteOnWordUnit answered an integer.*HandleUnit answered true.*alternatives: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the cursive recogniser: the letter set made cursive and "ton" and "to"
# written, the word segmenter handing each word to the reader
# (GCTryToRecognize), its readings made into word units and the page
# typing them - "ton to" ("For to" until FillSHR's bracketing xrs were
# put in the ROM's order); then "ton" read again and learnt, with the
# orthographic learning on - its learn array made and its three letters
# trained into the letter-shape database (the script's own lines are on
# stdout, which may come out before the reader's stderr or after it);
# this also watches for the heap damage a too-small trigram header once did
# here, which locked the machine up one run in three (needs the ROM image)
add_test(NAME host.NewtonCursive COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/cursive.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonCursive PROPERTIES TIMEOUT 180 ENVIRONMENT "NEWTON_TRACE_CURSIVE=1"
                     PASS_REGULAR_EXPRESSION "low_level: 1[0-9] xrs:.*xrw_algs: graph of [0-9]+ symbols:[A-Za-z0-9| ]*on.*answers: \"ton\".*word of strokes 0-3 [^:]*: 0.*low_level: [0-9]+ xrs:.*xrw_algs: graph of [0-9]+ symbols:to .*answers: \"to\".*word of strokes 0-2 [^:]*: 0(.*orthographic learn array: [0-9]+ entries.*trained into the orthographic database: 3 classes.*the page reads \"ton to\".*the letter weights were the defaults, after learning learnt|.*the page reads \"ton to\".*orthographic learn array: [0-9]+ entries.*trained into the orthographic database: 3 classes.*the letter weights were the defaults, after learning learnt).*cursive: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# numbers (src/host/demo/numbers.ns): "42", "10" and "217" written with the
# cursive letter set on the Notepad, which allows numbers - the digit
# reader finds each a number alone (no xrs left for the xr reader), the
# merge makes the digits the reading, and the page shows them typed; "217"
# read again is offered as the date "2/7" too
add_test(NAME host.NewtonNumbers COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/numbers.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonNumbers PROPERTIES TIMEOUT 180 ENVIRONMENT "NEWTON_TRACE_CURSIVE=1"
                     PASS_REGULAR_EXPRESSION "low_level: 0 xrs:.*answers: \"42\"/100.*answers: \"10\"/100.*answers: \"217\"/100 \"2/7\"/90.*the page reads \"42 10 217\".*217 read again as 217 2/7.*numbers: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# joined-up cursive (src/host/demo/cursive-joined.ns): five words each
# written in one stroke with the cursive letter set; each must reach the
# reader and be answered (what they read as is recorded in
# docs/recognition/README.md, "Joined-up writing") - "on", "no", "to"
# and "nun" first, "mum" as "Mom" - and "mum" and "nun" lose their
# arbitration to the scrub gesture (their synthetic stems are retraced
# exactly, a zig-zag TestScrub takes) and go down as ink words: the page
# reads "on no ~ to ~" (0x1a being the paragraph's ink-word character)
add_test(NAME host.NewtonCursiveJoined COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/cursive-joined.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonCursiveJoined PROPERTIES TIMEOUT 240 ENVIRONMENT "NEWTON_TRACE_CURSIVE=1"
                     PASS_REGULAR_EXPRESSION "answers: \"on\"/.*answers: \"no\"/.*answers: \"Mom\"/.*answers: \"to\"/.*answers: \"nun\"/.*the page reads \"on no . to . / \".*cursive-joined: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the journal (testing/Journal.h): "ton" written while recording, the four
# strokes played back into the tablet buffer and read again (needs the
# ROM image)
add_test(NAME host.NewtonJournal COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/journal.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonJournal PROPERTIES TIMEOUT 200
                     PASS_REGULAR_EXPRESSION "journal: recording started.*journal: 4 strokes recorded.*before the replay the page reads \"ton\".*after the replay the page reads \"ton tor\".*journal: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the test agent with the script as its test manager: reports queued and
# read back, the parameters split, debug() (testing/TestAgent.h; needs the
# ROM image)
add_test(NAME host.NewtonTestAgent COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/testagent.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonTestAgent PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "queue before activating is empty.*queued \"Test Case MSG: hello from the test\".*queued \"Test Case ERR: \tsomething went wrong\".*test a frame started in newt.*TotalSystemFree.*parameters 4:.*\"a frame\".*\"one\".*\"two three\".*\"four\".*debug\\(0\\) is a view.*is nil.*printed, not queued.*testagent: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the units the ROM's own packages export, as the unit natives see them
# after the boot (packages/Units.h), the built-in NIE's among them
add_test(NAME host.NewtonUnits COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/units.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonUnits PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "units: 12 exports.*units: cardfile 1.0 62 objects, 0 clients, 0 importers.*units: ConnectionInternal 1.1 39 objects.*units: Inet Protos:NIE 1.0 4 objects.*units: setup 1.0 19 objects.*units: SetupProtos:NIE 1.0 18 objects.*units: 0 import tables, 0 pending.*FulfillImportTable\\(nil\\) is true.*units: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the cursive recogniser's letter styles asked from NewtonScript
# (recognition/LetterShapes.h, WordRecognizer.h; needs the ROM image)
add_test(NAME host.NewtonLetterStyles COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/letterstyles.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonLetterStyles PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "letterstyles: 320 pictures, 'a' from 162, first letter !, weights default, after reset default.*letterstyles: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# a package loaded from a file (newton --package): the ROM's Formulas
# extracted as a loadable package under another name, so the package
# manager does not refuse it as the one it already has, and installed
# once the machine is up (needs Python)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    set(HOST_PACKAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/packages)
    # (out of the object file the OS boots from, which carries the ROM
    # extension: no ROM image needed)
    add_test(NAME host.NewtonPackage.extract
             COMMAND ${Python3_EXECUTABLE} tools/newton-rom/analysis/packages.py ${NEWTON_OBJECTS_FILE}
                     --extract ${HOST_PACKAGE_DIR} --rename Formulas=Formulas2
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackage.extract PROPERTIES FIXTURES_SETUP host_package)
    add_test(NAME host.NewtonPackage
             COMMAND newton --headless 60 --package ${HOST_PACKAGE_DIR}/Formulas2.pkg
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/installed.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackage PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package
                         PASS_REGULAR_EXPRESSION "Formulas2.pkg: installed as package"
                         FAIL_REGULAR_EXPRESSION "waited in vain")
    # a package kept on the internal store survives a restart: installed
    # (store:SuckPackageFromBinary) on a fresh store file, then found in the
    # store's "Packages" soup and activated at the next boot
    # (packages/StorePackages.h, demo/packagestore.ns)
    add_test(NAME host.NewtonPackageStore.install
             COMMAND newton --erase --store ${CMAKE_CURRENT_BINARY_DIR}/packagestore.store
                     --headless 60 --package ${HOST_PACKAGE_DIR}/Formulas2.pkg
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/installed.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackageStore.install PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package
                         FIXTURES_SETUP host_package_store
                         PASS_REGULAR_EXPRESSION "Formulas2.pkg: installed as package"
                         FAIL_REGULAR_EXPRESSION "waited in vain")
    add_test(NAME host.NewtonPackageStore
             COMMAND newton --store ${CMAKE_CURRENT_BINARY_DIR}/packagestore.store
                     --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/packagestore.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackageStore PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package_store
                         PASS_REGULAR_EXPRESSION "afterRestart: TRUE, pssid: [0-9]+, onInternal: TRUE}.*title: \"Formulas2\".*deactivated: TRUE.*activatedAgain: TRUE.*packagestore: done"
                         FAIL_REGULAR_EXPRESSION "FALSE|NIL,")
    # a memory card (docs/stores/README.md, "Memory cards"): a blank card
    # made and put in, formatted at the ROM's prompts, a soup written on
    # it, pulled out and put back (demo/card.ns); the built-in NIE's card
    # handler (thirdparty/nie's TLanternCardHandler, newtdev.pkg's part's
    # stand-in) is asked first and declines it
    add_test(NAME host.NewtonCard
             COMMAND newton --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/card.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonCard PROPERTIES TIMEOUT 120
                         PASS_REGULAR_EXPRESSION "RecognizeCardID: .Host card.*mounted: TRUE.*[Cc]ardSlot: 0, cardType: flsh.*written: TRUE.*removed: TRUE, stores: 1.*remounted: TRUE.*found: \"written on a card\".*cards: 1.*card: done"
                         FAIL_REGULAR_EXPRESSION "FALSE|the card server is NOT YET|no host stand-in")
    # the card reinsert alert: a card whose store is locked pulled out, the
    # alert drawn over the screen (ScreenPixel), the card put back, the alert
    # gone and the store as it was (demo/card-alert.ns); then the snapshot
    # taken while it was up made a PNG, build/card-alert.png
    add_test(NAME host.NewtonCardAlert
             COMMAND newton --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/card-alert.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonCardAlert PROPERTIES TIMEOUT 120 FIXTURES_SETUP host_card_alert
                         PASS_REGULAR_EXPRESSION "alertUp: TRUE.*sameStore: TRUE, found: \"kept through the alert\", alertDown: TRUE.*card-alert: done")
    if(Python3_FOUND)
        add_test(NAME host.NewtonCardAlert.png
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/imaging/pgm2png.py build/card-alert.pgm build/card-alert.png
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
        set_tests_properties(host.NewtonCardAlert.png PROPERTIES FIXTURES_REQUIRED host_card_alert)
    endif()
    # a card like Einstein's default one (tools/cards/linearcard.py makes
    # it: 28F008SA chips), recognised, formatted and written (demo/card-einstein.ns)
    if(Python3_FOUND)
        add_test(NAME host.NewtonCardEinstein.make
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/cards/linearcard.py make
                         ${CMAKE_CURRENT_BINARY_DIR}/einstein.card --size 2
                         --cis "01 03 52 06 ff 1e 06 02 11 01 01 03 01 18 02 89 a2 ff" --name Einstein)
        set_tests_properties(host.NewtonCardEinstein.make PROPERTIES FIXTURES_SETUP host_einstein_card)
        add_test(NAME host.NewtonCardEinstein
                 COMMAND newton --headless 60 --card ${CMAKE_CURRENT_BINARY_DIR}/einstein.card
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/card-einstein.ns
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
        set_tests_properties(host.NewtonCardEinstein PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_einstein_card
                             PASS_REGULAR_EXPRESSION "mounted: TRUE.*written: TRUE.*card-einstein: done"
                             FAIL_REGULAR_EXPRESSION "FALSE")
    endif()
    # a package a card carries in its attribute memory (pcmcia/CardPipe.h):
    # an 'auto part streamed as NSOF (tools/cards/streamedpkg.py) put after
    # the card's CIS and named by Apple's 0x8e tuple (linearcard.py make
    # --attr-package); the card server loads it through a TCardPipe when
    # the card goes in, its InstallScript runs, the card out takes it away
    # (its removeScript runs) and back in loads it again (demo/card-package.ns)
    if(Python3_FOUND)
        add_test(NAME host.NewtonCardPackage.pkg
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/cards/streamedpkg.py
                         ${CMAKE_CURRENT_BINARY_DIR}/cardpackage.pkg --name "Card Hello" --text "Card Hello")
        set_tests_properties(host.NewtonCardPackage.pkg PROPERTIES FIXTURES_SETUP host_card_package_pkg)
        add_test(NAME host.NewtonCardPackage.make
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/cards/linearcard.py make
                         ${CMAKE_CURRENT_BINARY_DIR}/cardpackage.card --size 2
                         --cis "01 03 52 06 ff 1e 06 02 11 01 01 03 01 18 02 89 a2 ff"
                         --attr-package ${CMAKE_CURRENT_BINARY_DIR}/cardpackage.pkg --package-name "Card Hello")
        set_tests_properties(host.NewtonCardPackage.make PROPERTIES FIXTURES_REQUIRED host_card_package_pkg
                             FIXTURES_SETUP host_card_package)
        add_test(NAME host.NewtonCardPackage
                 COMMAND newton --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/card-package.ns
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
        set_tests_properties(host.NewtonCardPackage PROPERTIES TIMEOUT 180 FIXTURES_REQUIRED host_card_package
                             ENVIRONMENT "CARD_PACKAGE_CARD=${CMAKE_CURRENT_BINARY_DIR}/cardpackage.card"
                             PASS_REGULAR_EXPRESSION "its InstallScript ran: ok.*its removeScript ran: ok.*the package gone with the card: ok.*loaded again with the card back: ok.*card-package: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "waited in vain|FAILED")
    endif()
    # a package cut into a soup of chunk entries and restored with
    # store:RestoreSegmentedPackage (packages/PackageArchivalPipe.h), and
    # stored with SuckPackageFromBinary's progress callback, on a store of
    # its own holding Formulas2
    add_test(NAME host.NewtonSegmented.install
             COMMAND newton --erase --store ${CMAKE_CURRENT_BINARY_DIR}/segmented.store
                     --headless 60 --package ${HOST_PACKAGE_DIR}/Formulas2.pkg
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/installed.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonSegmented.install PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package
                         FIXTURES_SETUP host_segmented_store
                         PASS_REGULAR_EXPRESSION "Formulas2.pkg: installed as package"
                         FAIL_REGULAR_EXPRESSION "waited in vain")
    add_test(NAME host.NewtonSegmented
             COMMAND newton --store ${CMAKE_CURRENT_BINARY_DIR}/segmented.store
                     --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/segmented.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonSegmented PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_segmented_store
                         PASS_REGULAR_EXPRESSION "removed: TRUE}.*sucked: TRUE, calls: [1-9][0-9]*, callbacksCalled: TRUE, infoSize: TRUE, infoName: \"Formulas2\", infoParts: 1, amountRead: TRUE}.*chunks: [0-9]+, restored: TRUE, sameBytes: TRUE, title: \"Formulas2\"}.*segmented: done"
                         FAIL_REGULAR_EXPRESSION "sameBytes: NIL")
    # store:SuckPackageFromEndPoint (packages/EndpointPackages.cpp): Formulas2's
    # bytes sent to the echo server over a TCP endpoint and sucked back off it
    add_test(NAME host.NewtonEndpointPackage.install
             COMMAND newton --erase --store ${CMAKE_CURRENT_BINARY_DIR}/endpointpkg.store
                     --headless 60 --package ${HOST_PACKAGE_DIR}/Formulas2.pkg
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/installed.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonEndpointPackage.install PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package
                         FIXTURES_SETUP host_endpointpkg_store
                         PASS_REGULAR_EXPRESSION "Formulas2.pkg: installed as package"
                         FAIL_REGULAR_EXPRESSION "waited in vain")
    add_test(NAME host.NewtonEndpointPackage
             COMMAND newton --store ${CMAKE_CURRENT_BINARY_DIR}/endpointpkg.store
                     --tcp-echo 0 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/endpointpkg.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonEndpointPackage PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_endpointpkg_store
                         PASS_REGULAR_EXPRESSION "removed: TRUE.*sucked: TRUE, title: \"Formulas2\", sameSize: TRUE}.*endpointpkg: done")
    # the same boot slowed by the heap checker's hooks: a task switched out on
    # its way back from a system call once lost the call's answer
    # (os600/user/host/SWI.cpp's ExitWithResult), and the store package
    # did not activate
    add_test(NAME host.NewtonPackageStoreSlow
             COMMAND newton --store ${CMAKE_CURRENT_BINARY_DIR}/packagestore.store
                     --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/packagestore.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackageStoreSlow PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package_store
                         DEPENDS host.NewtonPackageStore
                         ENVIRONMENT "NEWTON_HEAPCHECK=100000"
                         PASS_REGULAR_EXPRESSION "afterRestart: TRUE, pssid: [0-9]+, onInternal: TRUE}.*activatedAgain: TRUE.*packagestore: done"
                         FAIL_REGULAR_EXPRESSION "FALSE|NIL,")
    # the 1.x package natives over the same store's package (run last: it
    # removes the package)
    add_test(NAME host.NewtonOneX
             COMMAND newton --store ${CMAKE_CURRENT_BINARY_DIR}/packagestore.store
                     --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/onex.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonOneX PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package_store
                         DEPENDS "host.NewtonPackageStore;host.NewtonPackageStoreSlow"
                         PASS_REGULAR_EXPRESSION "found: TRUE.*deactivated: TRUE.*activated: TRUE, positive: TRUE.*same: TRUE.*removed: TRUE.*directory: TRUE, empty: TRUE.*onex: done"
                         FAIL_REGULAR_EXPRESSION "FALSE")
endif()

# Third-party packages (fixtures/packages: RPNcalc, games, fonts, Apple's
# Newton Internet Enabler) stored and installed on a fresh store, their
# Extras drawer entries checked, RPNcalc tapped, then all removed; then a
# restart on the same store finds none of them (demo/thirdparty.ns,
# demo/thirdparty-restart.ns).  MDaleks is the same package as Daleks under
# another file name, which the ROM's RegisterNewPackage refuses.  Only
# when the fixtures are there.
set(THIRDPARTY_DIR ${CMAKE_SOURCE_DIR}/../fixtures/packages)
if(EXISTS ${THIRDPARTY_DIR}/apps/RPNcalc/rpncalc.pkg)
    set(THIRDPARTY_ARGS)
    foreach(pkg apps/RPNcalc/rpncalc.pkg games/Daleks/Daleks1.pkg games/Daleks/MDaleks1.pkg
                games/Mahjongg2.1/Mahjongg.pkg games/NewtHack1_1/newthack.pkg games/NewtHack1_1/nhsounds.pkg
                games/NewtHack1_1/register.pkg fonts/monaco.pkg fonts/KLINGON_.PKG "fonts/times font.pkg"
                "fonts/zapf chancery font.pkg" "fonts/zapf dingbats font.pkg"
                apple/NIE2/ENETSUP/enetsup.pkg
                apple/NIE2/LOCTSUP/loctsup.pkg apple/NIE2/MODMSER/modmsup.pkg apple/NIE2/MODMSER/isptmplt.pkg)
        list(APPEND THIRDPARTY_ARGS --package ${THIRDPARTY_DIR}/${pkg})
    endforeach()
    add_test(NAME host.NewtonThirdPartyPackages
             COMMAND newton --display 320x480 --erase
                     --store ${CMAKE_CURRENT_BINARY_DIR}/thirdparty.store --headless 240
                     ${THIRDPARTY_ARGS} --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/thirdparty.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonThirdPartyPackages PROPERTIES TIMEOUT 300
                         FIXTURES_SETUP host_thirdparty_store
                         PASS_REGULAR_EXPRESSION "MDaleks1.pkg: not installed.*entries: 15,[^}]*wrong: \\[\\],[^}]*missing: \\[\\]}.*fonts: TRUE.*rpnOpened: TRUE.*removedPackages: 15, stored: 0, entriesLeft: 0, active: 0, fontsGone: TRUE, declawed: TRUE, freed: TRUE.*thirdparty: done"
                         FAIL_REGULAR_EXPRESSION "fell over")
    add_test(NAME host.NewtonThirdPartyPackages.restart
             COMMAND newton --display 320x480
                     --store ${CMAKE_CURRENT_BINARY_DIR}/thirdparty.store --headless 60
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/thirdparty-restart.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonThirdPartyPackages.restart PROPERTIES TIMEOUT 120
                         FIXTURES_REQUIRED host_thirdparty_store DEPENDS host.NewtonThirdPartyPackages
                         PASS_REGULAR_EXPRESSION "restartStored: 0, restartEntries: 0, restartActive: 0, fontsGone: TRUE.*thirdparty-restart: done")
    # the applications among them used for real (demo/thirdparty-apps.ns):
    # each opened from the Extras drawer, tapped a little (RPNcalc sums,
    # the Dalek-dodger steps, Mahjongg turns the screen and takes a pair
    # off the board - its native code on armcpu - NewtHack walks, the
    # Register picks a program), snapshot as tmp/thirdparty-apps-<n>.pgm
    add_test(NAME host.NewtonThirdPartyApps
             COMMAND newton --display 320x480 --erase
                     --store ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-apps.store --headless 280
                     --package ${THIRDPARTY_DIR}/apps/RPNcalc/rpncalc.pkg
                     --package ${THIRDPARTY_DIR}/games/Daleks/Daleks1.pkg
                     --package ${THIRDPARTY_DIR}/games/Mahjongg2.1/Mahjongg.pkg
                     --package ${THIRDPARTY_DIR}/games/NewtHack1_1/newthack.pkg
                     --package ${THIRDPARTY_DIR}/games/NewtHack1_1/nhsounds.pkg
                     --package ${THIRDPARTY_DIR}/games/NewtHack1_1/register.pkg
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/thirdparty-apps.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonThirdPartyApps PROPERTIES TIMEOUT 340
                         PASS_REGULAR_EXPRESSION "RPNcalc:NMad open.*daleksMoved: TRUE.*mahjonggTilesRemaining: 142.*Mahjongg:Scrawl open.*NewtHack:ChrisDopp open.*Register:FlaSheridn open.*thirdparty-apps: done, 0 failed"
                         FAIL_REGULAR_EXPRESSION "waited in vain|is not answered|stopped \\(|fell over")
    # each fixture application used for what it is for, its results checked
    # (src/host/demo/apps-*.ns over apphelpers.ns): the demo's name, its
    # script, then the packages it wants
    function(newton_app_demo name script)
        set(packages)
        foreach(pkg ${ARGN})
            list(APPEND packages --package ${THIRDPARTY_DIR}/${pkg})
        endforeach()
        add_test(NAME host.NewtonApp${name}
                 COMMAND newton --display 320x480 --erase
                         --store ${CMAKE_CURRENT_BINARY_DIR}/app-${script}.store --headless 280
                         ${packages}
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/apps-${script}.ns
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
        set_tests_properties(host.NewtonApp${name} PROPERTIES TIMEOUT 340
                             PASS_REGULAR_EXPRESSION "${script}: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "FAILED|waited in vain|is not answered|stopped \\(|fell over")
    endfunction()
    newton_app_demo(RPNcalc rpncalc apps/RPNcalc/rpncalc.pkg)
    newton_app_demo(Daleks daleks games/Daleks/Daleks1.pkg)
    newton_app_demo(Mahjongg mahjongg games/Mahjongg2.1/Mahjongg.pkg)
    newton_app_demo(NewtHack newthack games/NewtHack1_1/newthack.pkg games/NewtHack1_1/nhsounds.pkg games/NewtHack1_1/register.pkg)
    newton_app_demo(Note2Net note2net apps/NetHopper3.2/nethopper.pkg apps/NetHopper3.2/note2net.pkg)
    newton_app_demo(Fonts fonts "fonts/times font.pkg" fonts/monaco.pkg "fonts/zapf chancery font.pkg" "fonts/zapf dingbats font.pkg")
else()
    message(STATUS "fixtures/packages not there: host.NewtonThirdPartyPackages is not run")
endif()

# a large binary (VBO) made on the booted machine's internal store
# (stores/LargeBinaries.h, qd/Pictures.h's GetBitmapInfo; needs the ROM image)
add_test(NAME host.NewtonVBO COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/vbo.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonVBO PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "bitsVBO: TRUE,[^}]*compander: \"TPixelMapCompander\",[^}]*onStore: TRUE,[^}]*rowbytes: 8.*namedCompander: \"TLZStoreCompander\".*vbo: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the key-help slip (views/KeyHelpSlip.cpp): opened over the boot's first
# screen, its groups laid out and drawn, the display photographed into the
# build directory (keyhelp.pgm; needs the ROM image)
add_test(NAME host.NewtonKeyHelp COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/keyhelp.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonKeyHelp PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "keyhelp: open.*numcols 2, lineHeight 13.*groups 4.*keyhelp: Editing.*keyhelp: snapshot.*keyhelp: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# a page's views laid out again for the printer (views/Reflow.cpp ReFlow,
# ReflowPreflight): the groups made and what is in each (needs the ROM image)
add_test(NAME host.NewtonReflow COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/reflow.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonReflow PROPERTIES TIMEOUT 150
                     PASS_REGULAR_EXPRESSION "reflow: families 1, sizes 1, faces 2.*reflow: 3 groups.*group 0 top 0, 1 children .Shopping list.*group 1 top 20, 2 children box 0,0 box 80,10.*group 2 top 20, 1 children .Bread, milk, eggs, apples, cheese, coffee, rice, tea, butter and a newspaper for Sunday.*reflow: snapshot.*reflow: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# a roll over a soup cursor scrolled (views/View.cpp TView::SyncScrollSoup):
# the cursor and viewOriginY after each step (needs the ROM image)
add_test(NAME host.NewtonSoupScroll COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/soupscroll.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonSoupScroll PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "down -> 1, answer nil.*tall one -> 1, origin 34.*past it -> 2, origin 0.*up into the tall one -> 1, origin 68.*up a step -> 1, origin 34.*past the last -> 0.*up at the first -> 0.*up from the third -> 1, origin 68.*soupscroll: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the About slip's credits: MakePict records the lines as text shapes into one
# picture, which the credits view draws with CopyBits (needs the ROM image)
add_test(NAME host.NewtonCredits COMMAND newton --display 320x480 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/credits.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonCredits PROPERTIES TIMEOUT 150
                     PASS_REGULAR_EXPRESSION "credits: creditPict 184 by 216, creditHeight 218.*credits: written.*credits: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# a sound played: the ROM's click through PlaySoundSync and through a
# protoSoundChannel with a callback, headless over the null sound backend
# (hal/host/HostSoundDriver.h), which counts what was played
add_test(NAME host.NewtonSound COMMAND newton --display 320x480 --headless 30 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/sound.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonSound PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "sound: 520 samples played.*sound: played.*sound: started, active.*sound: callback state 0 error 0.*sound: closed")

# a protoTXView (text/TXView.h): text put in, typed into, restyled,
# scrolled, cut and pasted and scrubbed, pictures in build/txview-1.pgm to
# -4.pgm (needs the ROM image)
add_test(NAME host.NewtonTXView COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/txview.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonTXView PROPERTIES TIMEOUT 120
                     FAIL_REGULAR_EXPRESSION "waited in vain|FAILED|Exception"
                     PASS_REGULAR_EXPRESSION "txview: 176 characters, 142-176 the last line, scrolled from [1-9][0-9]* to 0.*txview: scrubbed to 167 characters.*txview: 2100 characters on the store, 2100 read back.*txview: replaced undoably, 4 characters left.*txview: the ruler.s justification is now left.*txview: done")

# a paginated protoTXView: the text poured through pages and a page break put
# in (src/text/TXFrames.h's TXPageFrames, TXFrameFormatter.h's TXPageFormatter)
add_test(NAME host.NewtonTXPages COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/txpages.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonTXPages PROPERTIES TIMEOUT 120
                     FAIL_REGULAR_EXPRESSION "waited in vain|FAILED|Exception"
                     PASS_REGULAR_EXPRESSION "txpages: 2 pages, 3 with the page break, 655 pixels tall.*txpages: done")

# the Sound Recorder driven through its Rec, Stop and Play buttons, headless:
# the null microphone hears a 1000 Hz test tone (newton --microphone-tone)
# and newton says how much of what was played back was that tone (the
# recorder records through TGSMCodec, GSM 06.10, which is lossy; the rest
# is the pen's clicks)
add_test(NAME host.NewtonRecorder COMMAND newton --display 320x480 --headless 120 --microphone-tone 1000 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/recorder.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonRecorder PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "test tone makes up (8[0-9]|9[0-9]|100)% of what was played.*recorder: opened: Ready to record.*recorder: recording: Recording.*recorder: stopped: Ready to play or record.*recorder: played: Playing.*recorder: finished: Ready to play or record.*recorder: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# a protoBasicEndpoint over TCP (comms/NewScriptEndpoint.h): the NIE's
# 'inet service and 'itrs remote socket, to the echo server newton runs
# (--tcp-echo), written and read back synchronously and asynchronously
add_test(NAME host.NewtonEcho COMMAND newton --display 320x480 --headless 60 --tcp-echo 0 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/echo.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonEcho PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "echo: connected, state 5.*echo: received hello .byteCount 5.*echo: sent asynchronously, done.*echo: line 15 characters, ended by endsequence.*echo: frame bonjour 4 Array.*echo: disconnected, state 2.*echo: unbound, state 1.*echo: done")

# protoEndpoint, the 1.x NewtonScript endpoint (comms/ScriptEndpoint.h):
# 1.x option frames, Connect, output and OutputFrame, input specs to a byte
# count, an end character and the 'frame form, against the echo server
add_test(NAME host.NewtonProtoEndpoint COMMAND newton --display 320x480 --headless 60 --tcp-echo 0 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/protoendpoint.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonProtoEndpoint PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "Instantiate: NIL, state: 1.*Connect: NIL, state: 5.*protoendpoint: received hello.*protoendpoint: line of 15 characters.*protoendpoint: frame bonjour 4.*outputDone: TRUE.*Disconnect: NIL, state: 0.*protoendpoint: done"
                     FAIL_REGULAR_EXPRESSION "waited in vain")

# the communications' trace frame (@661: cfinstantiate, cfrecord, Dispose
# over a THistoryCollector) and translate (comms/CommTrace.cpp)
add_test(NAME host.NewtonCommTrace COMMAND newton --display 320x480 --headless 30 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/commtrace.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonCommTrace PROPERTIES TIMEOUT 90
                     PASS_REGULAR_EXPRESSION "collector: TRUE.*events: TRUE.*Connect: 0, byte: 1.*disposed: TRUE.*flattened: binary, length: 48.*unflattened: \"bonjour\", second: 1.*unflattenedNoCode: \"bonjour\".*wrongTranslator: -54001.*commtrace: done")

# the NIE's DNS protocol ('sid ' naming 'dnst, a 'dnsq query and 'rrcd
# records) answered by the host's DNS service over the host's resolver
# (comms/host/HostDNSTool.h): localhost, a reverse lookup, a name that
# cannot exist - no network needed
add_test(NAME host.NewtonDNS COMMAND newton --display 320x480 --headless 60 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/dns.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonDNS PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "dns: instantiated, state 1.*dns: localhost type 1: 127.0.0.1.*dns:  type 12: 0.0.0.0 .. -> ..[A-Za-z].*dns: no-such-host.invalid not found, -60791.*dns: done")

# a protoStreamingEndpoint (comms/StreamingEndpoint.h): a frame streamed
# out as NSOF and read back over TCP through a TEndpointPipe
add_test(NAME host.NewtonStream COMMAND newton --display 320x480 --headless 60 --tcp-echo 0 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/stream.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonStream PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "stream: connected, state 5.*stream: sent, progress 1536 of 1854.*stream: received a streamed frame 1996 300 89401 .*progress 1854.*stream: done")

# the Newton Internet Enabler end to end through its own API (demo/inet.ns):
# InetGrabLink over the host's link (comms/host/HostLink.h - the NIE's link
# state machine, its protoFSM run by the re-expressions of
# src/thirdparty/nie), DNSGetAddressFromName("localhost"), a TCP echo to
# the address it answered, the link released and taken down.  The NIE is
# built into the ROM extension of the object file the default boot runs on
# (romsrc/rex, romsrc/README.md), so none of these installs it.
set(NIE_DIR ${CMAKE_SOURCE_DIR}/../fixtures/packages/apple/NIE2)
if(EXISTS ${CMAKE_SOURCE_DIR}/../romsrc/rex/inetenbl.pkg)
    # the NIE built in (demo/nie-builtin.ns): its three packages active on no
    # store, Internet Setup in the Extras drawer's Setup folder as a
    # built-in, the link grabbed and localhost looked up by its domain
    # manager; .OverBuiltIn installs the fixtures' copies on the internal
    # store as well, which the ROM keeps there but does not activate ("a
    # package by the same name (on store "Built-In") is already in use")
    add_test(NAME host.NewtonNIEBuiltIn
             COMMAND newton --display 320x480 --erase
                     --store ${CMAKE_CURRENT_BINARY_DIR}/nie-builtin.store --headless 90
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/nie-builtin.ns
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(host.NewtonNIEBuiltIn PROPERTIES TIMEOUT 150
                         PASS_REGULAR_EXPRESSION "nie-builtin: stored 0, active 0.*nie-builtin: localhost resolved: ok.*nie-builtin: done, 0 failed"
                         FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")
    if(EXISTS ${NIE_DIR}/REGPKGS/inetenbl.pkg)
        add_test(NAME host.NewtonNIEOverBuiltIn
                 COMMAND newton --display 320x480 --erase
                         --store ${CMAKE_CURRENT_BINARY_DIR}/nie-overbuiltin.store --headless 90
                         --package ${NIE_DIR}/ENETSUP/newtdev.pkg --package ${NIE_DIR}/REGPKGS/inetenbl.pkg
                         --package ${NIE_DIR}/REGPKGS/inetstup.pkg
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/nie-builtin.ns
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(host.NewtonNIEOverBuiltIn PROPERTIES TIMEOUT 150
                             PASS_REGULAR_EXPRESSION "nie-builtin: stored 3, active 0.*nie-builtin: localhost resolved: ok.*nie-builtin: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")
    endif()
    add_test(NAME host.NewtonInet
             COMMAND newton --display 320x480 --erase
                     --store ${CMAKE_CURRENT_BINARY_DIR}/inet.store --tcp-echo 0 --headless 120
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/inet.ns
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(host.NewtonInet PROPERTIES TIMEOUT 180
                         PASS_REGULAR_EXPRESSION "inet: default link Host network.*inet: link connected, connected.*inet: localhost is 127.0.0.1.*inet: connected, state 5.*inet: echoed hello.*inet: endpoint disposed.*inet: link released.*inet: link now idle.*inet: done"
                         FAIL_REGULAR_EXPRESSION "waited in vain")
    # Internet Setup's own UI with the host's link (demo/inetsetup.ns): the
    # Host network setup listed and opened, and a new one made with Host
    # network picked in New Internet Setup's picker (snapshots in
    # inetsetup-N.pgm)
    add_test(NAME host.NewtonInetSetup
             COMMAND newton --display 320x480 --erase
                     --store ${CMAKE_CURRENT_BINARY_DIR}/inetsetup.store --headless 120
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/inetsetup.ns
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(host.NewtonInetSetup PROPERTIES TIMEOUT 180
                         PASS_REGULAR_EXPRESSION "inetsetup: opened yes.*inetsetup: listed Host network.*inetsetup: host setups 2.*inetsetup: done")
    # Internet Setup's editor over the Host network setup (demo/inethostsetup.ns):
    # its domain name, configuration, local address and DNS server written
    # in their slips and closed (a setup without Ethernet's slots threw
    # type.ref.frame in ValidateTarget), kept when the application is
    # opened again; .restart finds them after a restart on the same store
    # and looks up a name with no dot, which the host's DNS tool asks for
    # again with the setup's domain after it
    add_test(NAME host.NewtonInetHostSetup
             COMMAND newton --display 320x480 --erase
                     --store ${CMAKE_CURRENT_BINARY_DIR}/inethostsetup.store --headless 150
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/inethostsetup.ns
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(host.NewtonInetHostSetup PROPERTIES TIMEOUT 240
                         FIXTURES_SETUP host_inethostsetup_store
                         PASS_REGULAR_EXPRESSION "inethostsetup: a new machine.*inethostsetup: done, 0 failed"
                         FAIL_REGULAR_EXPRESSION "FAILED|waited in vain|exception in a unit handler")
    add_test(NAME host.NewtonInetHostSetup.restart
             COMMAND newton --display 320x480
                     --store ${CMAKE_CURRENT_BINARY_DIR}/inethostsetup.store --headless 150
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/inethostsetup.ns
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(host.NewtonInetHostSetup.restart PROPERTIES TIMEOUT 240
                         FIXTURES_REQUIRED host_inethostsetup_store DEPENDS host.NewtonInetHostSetup
                         ENVIRONMENT NEWTON_TRACE_DNS=1
                         PASS_REGULAR_EXPRESSION "default domain \"example.invalid\".*nosuchnewtonhost.example.invalid: 0 address.*inethostsetup: restart done, 0 failed"
                         FAIL_REGULAR_EXPRESSION "FAILED|waited in vain|exception in a unit handler")
    # the NIE's protoEndpointFSM (its 'Inet Protos:NIE unit) as a TCP client
    # over the host's link (demo/inetfsm.ns): connected through its own
    # states, "hello" echoed, disconnected
    add_test(NAME host.NewtonInetFSM
             COMMAND newton --display 320x480 --erase
                     --store ${CMAKE_CURRENT_BINARY_DIR}/inetfsm.store --tcp-echo 0 --headless 22
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/inetfsm.ns
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(host.NewtonInetFSM PROPERTIES TIMEOUT 180
                         PASS_REGULAR_EXPRESSION "inetfsm: proto protoEndpointFSM.*inetfsm: connected.*inetfsm: echoed hello.*inetfsm: states Connecting Connected Disconnecting Disconnected .*inetfsm: done")
    # NetHopper 3.2 browsing (demo/nethopper.ns): its help page parsed by its
    # own native code (Newton C++ Tools BinCFunctions on src/armcpu), a URL
    # typed into Open a Location, the NIE's connection slip answered, the
    # page fetched from tools/host/httpserve.py (serving demo/www), laid out
    # and drawn, its GIF fetched and decoded (natively too) and drawn
    # (snapshots in nethopper-N.pgm)
    set(NETHOPPER_DIR ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NetHopper3.2)
    if(EXISTS ${NETHOPPER_DIR}/nethopper.pkg AND Python3_EXECUTABLE)
        add_test(NAME host.NewtonNetHopper
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/httpserve.py
                         --dir ${CMAKE_CURRENT_SOURCE_DIR}/demo/www --port 0 --
                         $<TARGET_FILE:newton> --display 320x480 --erase
                         --store ${CMAKE_CURRENT_BINARY_DIR}/nethopper.store --headless 240
                         --package ${NETHOPPER_DIR}/nethopper.pkg
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/nethopper.ns
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(host.NewtonNetHopper PROPERTIES TIMEOUT 300
                             PASS_REGULAR_EXPRESSION "nethopper: the help page drawn: ok.*nethopper: the URL typed: ok.*nethopper: loaded Host Test Page.*nethopper: the page's heading drawn: ok.*nethopper: the image decoded and drawn: ok.*nethopper: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "waited in vain|is not answered|stopped \\(|FAILED")
        # the NetHopper Scheduler used for what it is for (apps-netsched.ns):
        # a URL added in its URL Selector, Load Now, the page fetched into
        # NetHopper's cache
        add_test(NAME host.NewtonAppNetSched
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/httpserve.py
                         --dir ${CMAKE_CURRENT_SOURCE_DIR}/demo/www --port 0 --
                         $<TARGET_FILE:newton> --display 320x480 --erase
                         --store ${CMAKE_CURRENT_BINARY_DIR}/app-netsched.store --headless 240
                         --package ${NETHOPPER_DIR}/nethopper.pkg --package ${NETHOPPER_DIR}/nhscheduler.pkg
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/apps-netsched.ns
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
        set_tests_properties(host.NewtonAppNetSched PROPERTIES TIMEOUT 300
                             PASS_REGULAR_EXPRESSION "GET /nethopper.html 200.*netsched: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "waited in vain|is not answered|stopped \\(|FAILED")
        # Newt's Cape's companions used for what they are for
        # (apps-newtscape.ns): HTMList's Save as HTML, and a package
        # downloaded from the host's web server and installed
        add_test(NAME host.NewtonAppNewtsCape
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/httpserve.py
                         --dir ${CMAKE_CURRENT_SOURCE_DIR}/demo/www
                         --file monaco.pkg=${CMAKE_SOURCE_DIR}/../fixtures/packages/fonts/monaco.pkg --port 0 --
                         $<TARGET_FILE:newton> --display 320x480 --erase
                         --store ${CMAKE_CURRENT_BINARY_DIR}/app-newtscape.store --headless 240
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/nwcp20r2.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/hlst10k2.pkg
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/apps-newtscape.ns
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
        set_tests_properties(host.NewtonAppNewtsCape PROPERTIES TIMEOUT 300
                             PASS_REGULAR_EXPRESSION "GET /monaco.pkg 200.*newtscape: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "waited in vain|is not answered|stopped \\(|FAILED")
        # Newt's Cape's helper applications (demo/apps-newtscape-helpers.ns):
        # a WAV sound taken into the In Box by audx, a PalmDoc e-text decoded
        # by pdoc, a Latin-1 page read through encd's ISO-8859-1, a MOD tune
        # made a package by mdsv and NewtPack, the help book, Pkg Info, and
        # the trial's expiry (demo/www/beep.wav, story.pdb, tune.mod:
        # tools/host/tonewav.py, palmdoc.py, tinymod.py), each file served
        # with the MIME type its helper asks for
        add_test(NAME host.NewtonAppNewtsCapeHelpers
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/httpserve.py
                         --dir ${CMAKE_CURRENT_SOURCE_DIR}/demo/www --port 0
                         --type .wav=audio/x-wav --type .pdb=application/x-pilot-pdb --type .mod=www/unknown --
                         $<TARGET_FILE:newton> --display 320x480 --erase
                         --store ${CMAKE_CURRENT_BINARY_DIR}/app-newtscape-helpers.store --headless 240
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/nwcp20r2.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/audx10c2.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/pdoc10d2.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/encd10c2.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/ISO-8859-1.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/NC_HELP.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/mdsv10a2.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/pack34.pkg
                         --package ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape/pkgi10g2.pkg
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/apps-newtscape-helpers.ns
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
        set_tests_properties(host.NewtonAppNewtsCapeHelpers PROPERTIES TIMEOUT 300
                             PASS_REGULAR_EXPRESSION "GET /beep.wav 200.*GET /tune.mod 200.*the accented letters: ok.*NewtPack made the tune a package: ok.*the help book's first page: ok.*the package's version and parts: ok.*expired after 46 days: ok.*helpers: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "waited in vain|is not answered|stopped \\(|FAILED")
        # NetHopper's JPEG viewer (NetHopperJPEG.pkg) with no NewtsCape to
        # decode for it (demo/nethopper-jpeg.ns): the JPEG fetched, the
        # system's "Sorry, a problem has occurred. (-48200)" alert, the
        # picture left as its placeholder
        add_test(NAME host.NewtonNetHopperJPEG
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/httpserve.py
                         --dir ${CMAKE_CURRENT_SOURCE_DIR}/demo/www --port 0 --
                         $<TARGET_FILE:newton> --display 320x480 --erase
                         --store ${CMAKE_CURRENT_BINARY_DIR}/nethopper-jpeg.store --headless 240
                         --package ${NETHOPPER_DIR}/nethopper.pkg
                         --package ${NETHOPPER_DIR}/NetHopperJPEG.pkg
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/nethopper-jpeg.ns
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(host.NewtonNetHopperJPEG PROPERTIES TIMEOUT 300
                             PASS_REGULAR_EXPRESSION "nethopper-jpeg: loaded Host JPEG Page.*the alert names the error -48200: ok.*the picture left as its placeholder: ok.*nethopper-jpeg: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "waited in vain|is not answered|stopped \\(|FAILED")
        # the same page with NewtsCape 2.0 and its JPEG converter installed:
        # the converter's libjpeg (native ARM with floating point, on
        # src/armcpu) decodes the JPEG into a 'pixels bitmap, NewtsCape makes
        # it an icon and the page shows it - the frame and the bar checked
        # on the screen (nethopper-newtscape.pgm)
        set(NEWTSCAPE_DIR ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape)
        if(EXISTS ${NEWTSCAPE_DIR}/jpeg10e2.pkg)
            add_test(NAME host.NewtonNetHopperNewtsCape
                     COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/httpserve.py
                             --dir ${CMAKE_CURRENT_SOURCE_DIR}/demo/www --port 0 --
                             $<TARGET_FILE:newton> --display 320x480 --erase
                             --store ${CMAKE_CURRENT_BINARY_DIR}/nethopper-newtscape.store --headless 240
                             --package ${NETHOPPER_DIR}/nethopper.pkg
                             --package ${NETHOPPER_DIR}/NetHopperJPEG.pkg
                             --package ${NEWTSCAPE_DIR}/nwcp20r2.pkg --package ${NEWTSCAPE_DIR}/jpeg10e2.pkg
                             --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/nethopper-jpeg.ns
                     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
            set_tests_properties(host.NewtonNetHopperNewtsCape PROPERTIES TIMEOUT 300
                                 PASS_REGULAR_EXPRESSION "nethopper-jpeg: loaded Host JPEG Page.*nethopper-jpeg: the icon 56x28, 4 bits deep.*the frame's top edge: ok.*the bar: ok.*white below the bar: ok.*nethopper-jpeg: done, 0 failed"
                                 FAIL_REGULAR_EXPRESSION "waited in vain|is not answered|stopped \\(|FAILED")
        endif()
    endif()
    # Newt's Cape 2.0 as a browser (demo/newtscape.ns): the page fetched
    # over the built-in NIE and made a book, its picture first as ALT text,
    # then View > Load with Images..., the picture ticked and loaded - the
    # GIF decoded by the package's own native decoder (on src/armcpu) and
    # checked on the screen (newtscape-1.pgm, newtscape-2.pgm)
    set(NEWTSCAPE_DIR ${CMAKE_SOURCE_DIR}/../fixtures/packages/apps/NewtScape)
    if(EXISTS ${NEWTSCAPE_DIR}/nwcp20r2.pkg AND Python3_EXECUTABLE)
        add_test(NAME host.NewtonNewtsCape
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/httpserve.py
                         --dir ${CMAKE_CURRENT_SOURCE_DIR}/demo/www --port 0 --
                         $<TARGET_FILE:newton> --display 320x480 --erase
                         --store ${CMAKE_CURRENT_BINARY_DIR}/newtscape.store --headless 240
                         --package ${NEWTSCAPE_DIR}/nwcp20r2.pkg
                         --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/newtscape.ns
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(host.NewtonNewtsCape PROPERTIES TIMEOUT 300
                             PASS_REGULAR_EXPRESSION "newtscape: loaded Host Test Page.*the heading: ok.*the picture as its ALT text: ok.*the picture in the image cache: ok.*the frame's top edge: ok.*the bar: ok.*newtscape: done, 0 failed"
                             FAIL_REGULAR_EXPRESSION "waited in vain|is not answered|stopped \\(|FAILED")
    endif()
endif()

# the NewtonScript decompiler's round trip over a sample of the ROM's
# functions (tools/newton-rom/analysis/nsdecompile.py): each decompiled,
# compiled again by newtonscript (host/NSRoundTrip.cpp) and compared with
# the ROM's own; the percentage that round-trip must not fall
find_package(Python3 COMPONENTS Interpreter)

# the pure-Python PNG reader and writer the ROM's bitmaps are extracted with
# (tools/imaging/png.py)
if(Python3_FOUND)
    add_test(NAME tools.PNG
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/imaging/test_png.py
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
endif()

if(Python3_FOUND AND EXISTS ${NEWTON_ROM_BUILD_DIR}/symbols.json)
    add_test(NAME host.NSDecompileRoundTrip
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/newton-rom/analysis/nsdecompile.py
                     ${NEWTON_ROM_BUILD_DIR} --roundtrip --sample 600
                     --newtonscript $<TARGET_FILE:newtonscript> --rom ${NEWTON_ROM_BIN} --min-percent 100
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NSDecompileRoundTrip PROPERTIES TIMEOUT 300)

    # the extractor itself (the ROM-free track's step 2,
    # docs/rom-free/README.md): the ROM written as source afresh, into the
    # build directory - never over the committed tree - and built back, byte
    # for byte the ROM's
    add_test(NAME host.ROMSourceRoundTrip
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/newton-rom/analysis/romsrc.py
                     roundtrip ${NEWTON_ROM_BUILD_DIR} -o ${CMAKE_BINARY_DIR}/romsrc-extracted
                     --newtonscript $<TARGET_FILE:newtonscript>
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.ROMSourceRoundTrip PROPERTIES TIMEOUT 300)

    # whether the committed tree (romsrc/), less what was added to it on
    # purpose (--original: the Newton Internet Enabler in its extension,
    # romsrc/README.md), still builds byte for byte the ROM's - so an
    # unintended change to the rest is still caught
    add_test(NAME host.ROMSourceCommitted
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/newton-rom/analysis/romsrc.py
                     build ${CMAKE_SOURCE_DIR}/../romsrc --original --check ${NEWTON_ROM_BUILD_DIR}
                     --newtonscript $<TARGET_FILE:newtonscript>
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.ROMSourceCommitted PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "identical to the ROM's")

    # the OS booted from the committed tree draws exactly what the boot on the
    # ROM image draws (the Setup assistant's Welcome, out of the ROM extension
    # the object file carries), pixel for pixel (tools/host/samescreen.py)
    add_test(NAME host.NewtonNoROMSameScreen
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/samescreen.py
                     --newton $<TARGET_FILE:newton> --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/newton.ns
                     --snapshot build/newton-demo.pgm
                     --run "--rom ${NEWTON_ROM_BIN}" --run "--objects ${CMAKE_BINARY_DIR}/romsrc-objects.bin"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonNoROMSameScreen PROPERTIES TIMEOUT 120
                         ENVIRONMENT "NEWTON_ROM=${CMAKE_BINARY_DIR}/no-rom-image")
    add_test(NAME host.NewtonEditedSameScreen
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/host/samescreen.py
                     --newton $<TARGET_FILE:newton> --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/newton.ns
                     --snapshot build/newton-demo.pgm
                     --run "--rom ${NEWTON_ROM_BIN}" --run "--objects ${CMAKE_BINARY_DIR}/romsrc-edited.bin"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonEditedSameScreen PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED romsrc_edited
                         ENVIRONMENT "NEWTON_ROM=${CMAKE_BINARY_DIR}/no-rom-image")

    # `cmake --build <dir> --target romsrc-extract`: the ROM written as source
    # afresh into the build directory (the extractor is not run over romsrc/)
    add_custom_target(romsrc-extract
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/newton-rom/analysis/romsrc.py
                     extract ${NEWTON_ROM_BUILD_DIR} -o ${CMAKE_BINARY_DIR}/romsrc-extracted
                     --newtonscript $<TARGET_FILE:newtonscript>
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..
             COMMENT "Writing the ROM as source afresh into ${CMAKE_BINARY_DIR}/romsrc-extracted")
endif()

# The ROM source tree committed as romsrc/ (romsrc/README.md): what the OS
# boots from with no ROM image.  None of this needs the ROM or its
# extraction - only Python and the host's newtonscript, which compiles the
# tree's functions.
if(Python3_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/../romsrc/layout.tsv)
    # the ROM-free track's step 3: the OS booted as it boots by default - on
    # the object file built from the committed tree - with no ROM image
    # anywhere it could be found
    add_test(NAME host.NewtonNoROM
             COMMAND newton --headless 3
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/newton.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonNoROM PROPERTIES TIMEOUT 120
                         ENVIRONMENT "NEWTON_ROM=${CMAKE_BINARY_DIR}/no-rom-image"
                         PASS_REGULAR_EXPRESSION "the world is up.*written build/newton-demo.pgm")

    # the test of editability: a copy of the committed tree edited (a string
    # near the area's start and one in the first package lengthened, a slot
    # holding a new frame added), built laid out afresh, the edit read back
    # and the OS booted on it (host.NewtonEditedSameScreen, above)
    add_test(NAME host.ROMSourceEdit
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/newton-rom/analysis/romsrc.py
                     edit-test ${CMAKE_SOURCE_DIR}/../romsrc -o ${CMAKE_BINARY_DIR}/romsrc-edited
                     --objects ${CMAKE_BINARY_DIR}/romsrc-edited.bin --newtonscript $<TARGET_FILE:newtonscript>
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.ROMSourceEdit PROPERTIES TIMEOUT 300
                         FIXTURES_SETUP romsrc_edited PASS_REGULAR_EXPRESSION "edited .*moved")
    add_test(NAME host.ROMSourceEditValue
             COMMAND newtonscript --objects ${CMAKE_BINARY_DIR}/romsrc-edited.bin
                     -e "Print(ROMConstant(\"canonicalInkShape\").romsrcEdited.romsrcNote)"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.ROMSourceEditValue PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED romsrc_edited
                         ENVIRONMENT "NEWTON_ROM=${CMAKE_BINARY_DIR}/no-rom-image"
                         PASS_REGULAR_EXPRESSION "added by edit-test")
    # ... and the objects edit-test moved within their frame and array, with
    # the layout untouched
    add_test(NAME host.ROMSourceEditMoved
             COMMAND newtonscript --objects ${CMAKE_BINARY_DIR}/romsrc-edited.bin
                     -e "Print(@113); Print(@271.sram); Print(@271.flsh)"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.ROMSourceEditMoved PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED romsrc_edited
                         ENVIRONMENT "NEWTON_ROM=${CMAKE_BINARY_DIR}/no-rom-image"
                         PASS_REGULAR_EXPRESSION "edit-test.*may.*june.*april.*jan.*SRAM Card.*Flash Card")

    # the 13 fonts' text form (tools/fonts/newtonsfnt.py: BDF strikes, a text
    # file per table): each packed, unpacked and packed again byte for byte,
    # the committed directories what the unpacker writes, and edits change
    # only what they should
    add_test(NAME tools.NewtonFonts
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/fonts/test_newtonsfnt.py
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    # a font edited: one pixel of the System font's 9-point A flipped in its
    # BDF file, the tree built, the OS booted on it and on the unedited tree
    # (demo/fontedit.ns draws an A), and exactly that pixel differs on screen
    add_test(NAME host.ROMSourceFontEdit
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/fonts/glyph_edit_test.py
                     --tree ${CMAKE_SOURCE_DIR}/../romsrc -o ${CMAKE_BINARY_DIR}/romsrc-fontedit
                     --newton $<TARGET_FILE:newton> --newtonscript $<TARGET_FILE:newtonscript>
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/fontedit.ns
                     --original ${CMAKE_BINARY_DIR}/romsrc-objects.bin
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.ROMSourceFontEdit PROPERTIES TIMEOUT 300
                         PASS_REGULAR_EXPRESSION "the glyph edit reached the screen")
endif()

# a user's first hour (demo/walkthrough.ns): the Setup assistant, a note
# written and read back, a Names card typed in and found with Find, a
# meeting, a to-do made and scrubbed out, the Assistant, the note faxed
# later into the Out Box and filed, the Calculator, the volume turned down
# - each step checked and snapshotted (build/walkthrough-<n>-<step>.pgm) -
# then a restart on the same store that finds all of it still there
add_test(NAME host.NewtonWalkthrough
         COMMAND newton --display 320x480 --erase
                 --store ${CMAKE_CURRENT_BINARY_DIR}/walkthrough.store --headless 300
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/walkthrough.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonWalkthrough PROPERTIES TIMEOUT 360 FIXTURES_SETUP host_walkthrough_store
                     PASS_REGULAR_EXPRESSION "walkthrough: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")
add_test(NAME host.NewtonWalkthrough.restart
         COMMAND newton --display 320x480
                 --store ${CMAKE_CURRENT_BINARY_DIR}/walkthrough.store --headless 120
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/walkthrough.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonWalkthrough.restart PROPERTIES TIMEOUT 180
                     FIXTURES_REQUIRED host_walkthrough_store DEPENDS host.NewtonWalkthrough
                     PASS_REGULAR_EXPRESSION "walkthrough: restart done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# the year-2010 fix with the host's own clock (demo/year2010.ns,
# docs/intl/year-2010.md): after the Setup walk a note dated this year, a
# note made now dated today, Dates titled with today, an alarm a minute
# ahead ringing once; with NEWTON_ROM_2010_BUG=1 the ROM's own arithmetic,
# the Setup walk putting the clock back to 1992
add_test(NAME host.NewtonYear2010
         COMMAND newton --display 320x480 --headless 200
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/year2010.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonYear2010 PROPERTIES TIMEOUT 240
                     PASS_REGULAR_EXPRESSION "year2010: first note: ok.*note made now: ok.*dates: ok.*alarm: ok.*year2010: done"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")
add_test(NAME host.NewtonYear2010.romBug
         COMMAND newton --display 320x480 --headless 200
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/year2010.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonYear2010.romBug PROPERTIES TIMEOUT 240 ENVIRONMENT NEWTON_ROM_2010_BUG=1
                     PASS_REGULAR_EXPRESSION "firstNote: \"[0-9/]+92 .*first note: FAILED dated 1992.*year2010: done")

# the rest of a first day (demo/walkthrough2.ns): every Prefs panel opened
# and three settings changed, a bold style, a scrub undone, the on-screen
# keyboard, Seattle made the home city, Formulas, the Clock's minute timer
# ringing, the Dock's connection picker, the help book, a memory card
# formatted and a duplicated note filed on it - each step checked and
# snapshotted (build/walkthrough2-<n>-<step>.pgm) - then a restart on the
# same store and card that finds the settings, the city and the note
add_test(NAME host.NewtonWalkthrough2
         COMMAND newton --display 320x480 --erase
                 --store ${CMAKE_CURRENT_BINARY_DIR}/walkthrough2.store --headless 500
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/walkthrough2.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonWalkthrough2 PROPERTIES TIMEOUT 600 FIXTURES_SETUP host_walkthrough2_store
                     PASS_REGULAR_EXPRESSION "walkthrough: done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")
add_test(NAME host.NewtonWalkthrough2.restart
         COMMAND newton --display 320x480
                 --store ${CMAKE_CURRENT_BINARY_DIR}/walkthrough2.store --headless 120
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/walkthrough2.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonWalkthrough2.restart PROPERTIES TIMEOUT 180
                     FIXTURES_REQUIRED host_walkthrough2_store DEPENDS host.NewtonWalkthrough2
                     PASS_REGULAR_EXPRESSION "walkthrough: restart done, 0 failed"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# the Dates app's month overview (demo/monthoverview.ns): two meetings on
# the 15th drawn as bars where the ROM's arithmetic puts them, pixel-checked
add_test(NAME host.NewtonMonthOverview
         COMMAND newton --display 320x480 --headless 120
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/monthoverview.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonMonthOverview PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "9 am bar: ok.*1 pm bar: ok.*between them: ok.*monthoverview: done"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# part of a shape selected by tracing along it (views/PolygonTraced.cpp,
# HiliteTraced), a traced stretch scrubbed out (RemovePoints, command 0x44)
# and put back by Undo (demo/traced.ns)
add_test(NAME host.NewtonTraced
         COMMAND newton --display 320x480 --headless 150
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/traced.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonTraced PROPERTIES TIMEOUT 200
                     PASS_REGULAR_EXPRESSION "stretch is selected: ok.*cut in two: ok.*box stays: ok.*back whole: ok.*traced: done"
                     FAIL_REGULAR_EXPRESSION "FAILED|waited in vain")

# a bigger internal flash (docs/stores/README.md, "Bigger flash"): a new
# 128 MB sparse store, over 8 MB of soup entries written, read back after a
# restart on the same file, and the file on disk the size of what was
# written, not of the flash (demo/bigstore.ns, tools/stores/flashimage.py)
add_test(NAME host.NewtonBigStore.write
         COMMAND newton --erase --store ${CMAKE_CURRENT_BINARY_DIR}/bigstore.store --flash-size 128
                 --headless 300 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/bigstore.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonBigStore.write PROPERTIES TIMEOUT 400 FIXTURES_SETUP host_big_store
                     PASS_REGULAR_EXPRESSION "a new 128 MB sparse flash.*flash: TRUE.*overEightMB: TRUE.*bigstore: written"
                     FAIL_REGULAR_EXPRESSION "FALSE|not a flash file|would not mount")
add_test(NAME host.NewtonBigStore
         COMMAND newton --store ${CMAKE_CURRENT_BINARY_DIR}/bigstore.store
                 --headless 300 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/bigstore.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonBigStore PROPERTIES TIMEOUT 400 FIXTURES_REQUIRED host_big_store
                     PASS_REGULAR_EXPRESSION "flash: TRUE.*readBack: 600, all: TRUE, bad: 0.*bigstore: done"
                     FAIL_REGULAR_EXPRESSION "FALSE|not a flash file|would not mount")
if(Python3_FOUND)
    add_test(NAME host.NewtonBigStore.file
             COMMAND ${Python3_EXECUTABLE} tools/stores/flashimage.py info
                     ${CMAKE_CURRENT_BINARY_DIR}/bigstore.store --expect-stored 9:16
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonBigStore.file PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED host_big_store
                         DEPENDS host.NewtonBigStore
                         PASS_REGULAR_EXPRESSION "sparse image of a 128 MB flash.*file size as expected")
    # every task world answers its own size (tools/newton-rom/analysis/
    # worldsizes.py): a world copied short into its task writes past its
    # stack block (TInker's missing GetSizeOf, the TUPort::Receive crash)
    add_test(NAME tools.WorldSizes
             COMMAND ${Python3_EXECUTABLE} tools/newton-rom/analysis/worldsizes.py
                     ${NEWTON_ROM_BUILD_DIR}
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    add_test(NAME tools.FlashImage
             COMMAND ${Python3_EXECUTABLE} tools/stores/test_flashimage.py
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
endif()

# a fresh store has every soup the ROM's first boot makes, and mounting it
# throws nothing (demo/freshstore.ns): on a new flash file, and in memory
add_test(NAME host.NewtonFreshStore
         COMMAND newton --erase --store ${CMAKE_CURRENT_BINARY_DIR}/freshstore.store
                 --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/freshstore.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonFreshStore PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "freshstore: 15 soups, all there.*freshstore: done"
                     FAIL_REGULAR_EXPRESSION "while mounting|not a flash file|would not mount|NOT all there")
add_test(NAME host.NewtonFreshStore.memory
         COMMAND newton --headless 120 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/freshstore.ns
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonFreshStore.memory PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "freshstore: 15 soups, all there.*freshstore: done"
                     FAIL_REGULAR_EXPRESSION "while mounting|NOT all there")
