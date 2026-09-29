# Host programs over the reconstructed OS: newtonscript, the NewtonScript
# compiler, interpreter and REP on the host (newtonscript.cpp), with the
# view system over a host display on request (HostViews.cpp); every
# reconstructed native bound in one place (HostNatives.cpp), and the store
# the host boots with (HostStores.cpp).
# the view system on the host (HostViews.h), for newtonscript and the newt
# world's boot
add_library(host_views STATIC HostViews.cpp HostNatives.cpp HostStores.cpp FactorySoups.cpp)
target_link_libraries(host_views PUBLIC frames views recognition pcmcia sound system stores packages intl intl_meetings assist hal_host_display hal_host_tablet)
target_include_directories(host_views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(newtonscript newtonscript.cpp)
target_link_libraries(newtonscript PRIVATE host_views)
target_compile_definitions(newtonscript PRIVATE "NEWTON_DEFAULT_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\"")

# the demo: a view hierarchy drawn into build/views-demo.pgm (needs the ROM image)
add_test(NAME host.ViewsDemo COMMAND newtonscript --rom ${NEWTON_ROM_BIN} --display 320x480 ${CMAKE_CURRENT_SOURCE_DIR}/demo/views.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
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
    target_link_libraries(host_window PUBLIC user32 gdi32 shell32)
endif()
add_executable(newton newton.cpp HostKeyboard.cpp HostPackages.cpp HostHeapCheck.cpp)
target_link_libraries(newton PRIVATE newt host_views host_window)
target_compile_definitions(newton PRIVATE "NEWTON_DEFAULT_ROM_IMAGE=\"${NEWTON_ROM_BIN}\"")

# the newt world booted headless for two seconds with the views demo as its
# boot script (needs the ROM image)
add_test(NAME host.Newton COMMAND newton --rom ${NEWTON_ROM_BIN} --headless 2 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/newton.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.Newton PROPERTIES TIMEOUT 120)

# deferred recognition (views/Rerecognize.h): "ton" written, and the
# same strokes read again through Recognize, RecognizeInkWord,
# RecognizeTextInStyles and RecognizePara (needs the ROM image)
add_test(NAME host.NewtonRecognize COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 50 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/recognize.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonRecognize PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "Recognize read ton.*unread strokes came back as nothing.*RecognizeInkWord read ton.*RecognizeTextInStyles made \"an ton\".*RecognizePara made \"ton ton\", the range ending at 7.*ink came back as a piece of 4 strokes.*ink came back as a piece of 4 strokes")

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
add_test(NAME host.NewtonCursive COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 50 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/cursive.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonCursive PROPERTIES TIMEOUT 180 ENVIRONMENT "NEWTON_TRACE_CURSIVE=1"
                     PASS_REGULAR_EXPRESSION "low_level: 1[0-9] xrs:.*xrw_algs: graph of [0-9]+ symbols:[A-Za-z0-9| ]*on.*answers: \"ton\".*word of strokes 0-3 [^:]*: 0.*low_level: [0-9]+ xrs:.*xrw_algs: graph of [0-9]+ symbols:to .*answers: \"to\".*word of strokes 0-2 [^:]*: 0(.*orthographic learn array: [0-9]+ entries.*trained into the orthographic database: 3 classes.*the page reads \"ton to\".*the letter weights were the defaults, after learning learnt|.*the page reads \"ton to\".*orthographic learn array: [0-9]+ entries.*trained into the orthographic database: 3 classes.*the letter weights were the defaults, after learning learnt)")

# numbers (src/host/demo/numbers.ns): "42", "10" and "217" written with the
# cursive letter set on the Notepad, which allows numbers - the digit
# reader finds each a number alone (no xrs left for the xr reader), the
# merge makes the digits the reading, and the page shows them typed; "217"
# read again is offered as the date "2/7" too
add_test(NAME host.NewtonNumbers COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 50 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/numbers.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonNumbers PROPERTIES TIMEOUT 180 ENVIRONMENT "NEWTON_TRACE_CURSIVE=1"
                     PASS_REGULAR_EXPRESSION "low_level: 0 xrs:.*answers: \"42\"/100.*answers: \"10\"/100.*answers: \"217\"/100 \"2/7\"/90.*the page reads \"42 10 217\".*217 read again as 217 2/7")

# joined-up cursive (src/host/demo/cursive-joined.ns): five words each
# written in one stroke with the cursive letter set; each must reach the
# reader and be answered (what they read as is recorded in
# docs/recognition/README.md, "Joined-up writing") - "on", "no", "to"
# and "nun" first, "mum" as "Mom" - and "mum" and "nun" lose their
# arbitration to the scrub gesture (their synthetic stems are retraced
# exactly, a zig-zag TestScrub takes) and go down as ink words: the page
# reads "on no ~ to ~" (0x1a being the paragraph's ink-word character)
add_test(NAME host.NewtonCursiveJoined COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 70 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/cursive-joined.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonCursiveJoined PROPERTIES TIMEOUT 240 ENVIRONMENT "NEWTON_TRACE_CURSIVE=1"
                     PASS_REGULAR_EXPRESSION "answers: \"on\"/.*answers: \"no\"/.*answers: \"Mom\"/.*answers: \"to\"/.*answers: \"nun\"/.*the page reads \"on no . to . / \"")

# the journal (testing/Journal.h): "ton" written while recording, the four
# strokes played back into the tablet buffer and read again (needs the
# ROM image)
add_test(NAME host.NewtonJournal COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 70 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/journal.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonJournal PROPERTIES TIMEOUT 200
                     PASS_REGULAR_EXPRESSION "journal: recording started.*journal: 4 strokes recorded.*before the replay the page reads \"ton\".*after the replay the page reads \"ton tor\"")

# the test agent with the script as its test manager: reports queued and
# read back, the parameters split, debug() (testing/TestAgent.h; needs the
# ROM image)
add_test(NAME host.NewtonTestAgent COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 12 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/testagent.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonTestAgent PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "queue before activating is empty.*queued \"Test Case MSG: hello from the test\".*queued \"Test Case ERR: \tsomething went wrong\".*test a frame started in newt.*TotalSystemFree.*parameters 4:.*\"a frame\".*\"one\".*\"two three\".*\"four\".*debug\\(0\\) is a view.*is nil.*printed, not queued.*testagent: done")

# the units the ROM's own packages export, as the unit natives see them
# after the boot (packages/Units.h; needs the ROM image)
add_test(NAME host.NewtonUnits COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 6 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/units.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonUnits PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "units: 7 exports.*units: cardfile 1.0 62 objects, 0 clients, 0 importers.*units: ConnectionInternal 1.1 39 objects.*units: setup 1.0 19 objects.*units: 0 import tables, 0 pending.*FulfillImportTable\\(nil\\) is true.*units: done")

# the cursive recogniser's letter styles asked from NewtonScript
# (recognition/LetterShapes.h, WordRecognizer.h; needs the ROM image)
add_test(NAME host.NewtonLetterStyles COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 5 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/letterstyles.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonLetterStyles PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "letterstyles: 320 pictures, 'a' from 162, first letter !, weights default, after reset default.*letterstyles: done")

# a package loaded from a file (newton --package): the ROM's Formulas
# extracted as a loadable package under another name, so the package
# manager does not refuse it as the one it already has, and installed
# once the machine is up (needs the ROM image and Python)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    set(HOST_PACKAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/packages)
    add_test(NAME host.NewtonPackage.extract
             COMMAND ${Python3_EXECUTABLE} tools/newton-rom/analysis/packages.py build/${NEWTON_ROM_BUILD}
                     --extract ${HOST_PACKAGE_DIR} --rename Formulas=Formulas2
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackage.extract PROPERTIES FIXTURES_SETUP host_package)
    add_test(NAME host.NewtonPackage
             COMMAND newton --rom ${NEWTON_ROM_BIN} --headless 4 --package ${HOST_PACKAGE_DIR}/Formulas2.pkg
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackage PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package
                         PASS_REGULAR_EXPRESSION "Formulas2.pkg: installed as package")
    # a package kept on the internal store survives a restart: installed
    # (store:SuckPackageFromBinary) on a fresh store file, then found in the
    # store's "Packages" soup and activated at the next boot
    # (packages/StorePackages.h, demo/packagestore.ns)
    add_test(NAME host.NewtonPackageStore.install
             COMMAND newton --rom ${NEWTON_ROM_BIN} --erase --store ${CMAKE_CURRENT_BINARY_DIR}/packagestore.store
                     --headless 4 --package ${HOST_PACKAGE_DIR}/Formulas2.pkg
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackageStore.install PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package
                         FIXTURES_SETUP host_package_store
                         PASS_REGULAR_EXPRESSION "Formulas2.pkg: installed as package")
    add_test(NAME host.NewtonPackageStore
             COMMAND newton --rom ${NEWTON_ROM_BIN} --store ${CMAKE_CURRENT_BINARY_DIR}/packagestore.store
                     --headless 5 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/packagestore.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackageStore PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package_store
                         PASS_REGULAR_EXPRESSION "afterRestart: TRUE, pssid: [0-9]+, onInternal: TRUE}.*title: \"Formulas2\".*deactivated: TRUE.*activatedAgain: TRUE.*packagestore: done"
                         FAIL_REGULAR_EXPRESSION "FALSE|NIL,")
    # the same boot slowed by the heap checker's hooks: a task switched out on
    # its way back from a system call once lost the call's answer
    # (os600/user/host/SWI.cpp's ExitWithResult), and the store package
    # did not activate
    add_test(NAME host.NewtonPackageStoreSlow
             COMMAND newton --rom ${NEWTON_ROM_BIN} --store ${CMAKE_CURRENT_BINARY_DIR}/packagestore.store
                     --headless 5 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/packagestore.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonPackageStoreSlow PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package_store
                         DEPENDS host.NewtonPackageStore
                         ENVIRONMENT "NEWTON_HEAPCHECK=100000"
                         PASS_REGULAR_EXPRESSION "afterRestart: TRUE, pssid: [0-9]+, onInternal: TRUE}.*activatedAgain: TRUE.*packagestore: done"
                         FAIL_REGULAR_EXPRESSION "FALSE|NIL,")
    # the 1.x package natives over the same store's package (run last: it
    # removes the package)
    add_test(NAME host.NewtonOneX
             COMMAND newton --rom ${NEWTON_ROM_BIN} --store ${CMAKE_CURRENT_BINARY_DIR}/packagestore.store
                     --headless 5 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/onex.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonOneX PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package_store
                         DEPENDS "host.NewtonPackageStore;host.NewtonPackageStoreSlow"
                         PASS_REGULAR_EXPRESSION "found: TRUE.*deactivated: TRUE.*activated: TRUE, positive: TRUE.*same: TRUE.*removed: TRUE.*directory: TRUE, empty: TRUE.*onex: done"
                         FAIL_REGULAR_EXPRESSION "FALSE")
endif()

# a large binary (VBO) made on the booted machine's internal store
# (stores/LargeBinaries.h, qd/Pictures.h's GetBitmapInfo; needs the ROM image)
add_test(NAME host.NewtonVBO COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 5 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/vbo.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonVBO PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "bitsVBO: TRUE,[^}]*compander: \"TLZStoreCompander\",[^}]*onStore: TRUE,[^}]*rowbytes: 8.*vbo: done")
