# Host programs over the reconstructed OS: newtonscript, the NewtonScript
# compiler, interpreter and REP on the host (newtonscript.cpp), with the
# view system over a host display on request (HostViews.cpp); every
# reconstructed native bound in one place (HostNatives.cpp), and the store
# the host boots with (HostStores.cpp).
# the view system on the host (HostViews.h), for newtonscript and the newt
# world's boot
add_library(host_views STATIC HostViews.cpp HostNatives.cpp HostStores.cpp FactorySoups.cpp)
target_link_libraries(host_views PUBLIC frames comms_script views recognition pcmcia sound system stores packages intl intl_meetings assist hal_host_display hal_host_tablet)
target_include_directories(host_views PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(newtonscript newtonscript.cpp)
target_link_libraries(newtonscript PRIVATE host_views)
target_compile_definitions(newtonscript PRIVATE "NEWTON_DEFAULT_ROM_IMAGE=\"${NEWTON_ROM_IMAGE}\"")

# the demo: a view hierarchy drawn into build/views-demo.pgm (needs the ROM image)
add_test(NAME host.ViewsDemo COMMAND newtonscript --rom ${NEWTON_ROM_BIN} --display 320x480 ${CMAKE_CURRENT_SOURCE_DIR}/demo/views.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.ViewsDemo PROPERTIES TIMEOUT 120)
# drawing under a transform that scales: the world map at half size, shapes and text stretched
add_test(NAME host.ScaledMapDemo COMMAND newtonscript --rom ${NEWTON_ROM_BIN} --display 320x480 ${CMAKE_CURRENT_SOURCE_DIR}/demo/scaledmap.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.ScaledMapDemo PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "the map is 360 by 179.*scaledmap: written")

# newton: the OS booted and the NewtonScript world run over a window on the
# host display (Windows: host/win32/HostWindow.cpp; elsewhere headless),
# the mouse the pen and the keys the keyboard (HostKeyboard.cpp, the
# keyboard tool's stand-in)
# (the window is a library of its own, built without the Newton include
# paths: src/intl/Locale.h would be found for the C library's <locale.h>
# on a case-insensitive file system, and the Windows headers do not mix
# with the DDK's)
# (and the loudspeaker, win32/HostAudio.cpp: waveOut, the same reasons)
add_library(host_window STATIC win32/HostWindow.cpp win32/HostAudio.cpp)
if(WIN32)
    target_link_libraries(host_window PUBLIC user32 gdi32 shell32 winmm)
endif()
add_executable(newton newton.cpp HostKeyboard.cpp HostPackages.cpp HostHeapCheck.cpp)
target_link_libraries(newton PRIVATE newt host_views host_window hal_host_sound)
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

# the correction and word-unit methods on a paragraph, the correction info
# and a view of its own (GetAlternatives, Extract, insert, HandleRawInk,
# VoteOnWordUnit, HandleUnit)
add_test(NAME host.NewtonAlternatives COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 50 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/alternatives.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonAlternatives PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "GetAlternatives offered 2 words, the first \"ton\".*Extract took 1 word info.*Extract past the text took nothing.*insert put the range back.*HandleRawInk answered nil.*VoteOnWordUnit answered an integer.*HandleUnit answered true")

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
    # a package cut into a soup of chunk entries and restored with
    # store:RestoreSegmentedPackage (packages/PackageArchivalPipe.h), and
    # stored with SuckPackageFromBinary's progress callback, on a store of
    # its own holding Formulas2
    add_test(NAME host.NewtonSegmented.install
             COMMAND newton --rom ${NEWTON_ROM_BIN} --erase --store ${CMAKE_CURRENT_BINARY_DIR}/segmented.store
                     --headless 4 --package ${HOST_PACKAGE_DIR}/Formulas2.pkg
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonSegmented.install PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_package
                         FIXTURES_SETUP host_segmented_store
                         PASS_REGULAR_EXPRESSION "Formulas2.pkg: installed as package")
    add_test(NAME host.NewtonSegmented
             COMMAND newton --rom ${NEWTON_ROM_BIN} --store ${CMAKE_CURRENT_BINARY_DIR}/segmented.store
                     --headless 5 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/segmented.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonSegmented PROPERTIES TIMEOUT 120 FIXTURES_REQUIRED host_segmented_store
                         PASS_REGULAR_EXPRESSION "removed: TRUE}.*sucked: TRUE, calls: [1-9][0-9]*, callbacksCalled: TRUE, infoSize: TRUE, infoName: \"Formulas2\", infoParts: 1, amountRead: TRUE}.*chunks: [0-9]+, restored: TRUE, sameBytes: TRUE, title: \"Formulas2\"}.*segmented: done"
                         FAIL_REGULAR_EXPRESSION "sameBytes: NIL")
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
                apple/NIE2/ENETSUP/newtdev.pkg apple/NIE2/REGPKGS/inetenbl.pkg apple/NIE2/ENETSUP/enetsup.pkg
                apple/NIE2/LOCTSUP/loctsup.pkg apple/NIE2/MODMSER/modmsup.pkg apple/NIE2/MODMSER/isptmplt.pkg
                apple/NIE2/REGPKGS/inetstup.pkg)
        list(APPEND THIRDPARTY_ARGS --package ${THIRDPARTY_DIR}/${pkg})
    endforeach()
    add_test(NAME host.NewtonThirdPartyPackages
             COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --erase
                     --store ${CMAKE_CURRENT_BINARY_DIR}/thirdparty.store --headless 40
                     ${THIRDPARTY_ARGS} --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/thirdparty.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonThirdPartyPackages PROPERTIES TIMEOUT 240
                         FIXTURES_SETUP host_thirdparty_store
                         PASS_REGULAR_EXPRESSION "MDaleks1.pkg: not installed.*entries: 18,[^}]*wrong: \\[\\],[^}]*missing: \\[\\]}.*fonts: TRUE.*rpnOpened: TRUE.*removedPackages: 18, stored: 0, entriesLeft: 0, active: 0, fontsGone: TRUE, declawed: TRUE, freed: TRUE.*thirdparty: done"
                         FAIL_REGULAR_EXPRESSION "fell over")
    add_test(NAME host.NewtonThirdPartyPackages.restart
             COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480
                     --store ${CMAKE_CURRENT_BINARY_DIR}/thirdparty.store --headless 10
                     --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/thirdparty-restart.ns
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
    set_tests_properties(host.NewtonThirdPartyPackages.restart PROPERTIES TIMEOUT 120
                         FIXTURES_REQUIRED host_thirdparty_store DEPENDS host.NewtonThirdPartyPackages
                         PASS_REGULAR_EXPRESSION "restartStored: 0, restartEntries: 0, restartActive: 0, fontsGone: TRUE.*thirdparty-restart: done")
else()
    message(STATUS "fixtures/packages not there: host.NewtonThirdPartyPackages is not run")
endif()

# a large binary (VBO) made on the booted machine's internal store
# (stores/LargeBinaries.h, qd/Pictures.h's GetBitmapInfo; needs the ROM image)
add_test(NAME host.NewtonVBO COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 5 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/vbo.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonVBO PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "bitsVBO: TRUE,[^}]*compander: \"TPixelMapCompander\",[^}]*onStore: TRUE,[^}]*rowbytes: 8.*namedCompander: \"TLZStoreCompander\".*vbo: done")

# the key-help slip (views/KeyHelpSlip.cpp): opened over the boot's first
# screen, its groups laid out and drawn, the display photographed into the
# build directory (keyhelp.pgm; needs the ROM image)
add_test(NAME host.NewtonKeyHelp COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 20 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/keyhelp.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonKeyHelp PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "keyhelp: open.*numcols 2, lineHeight 13.*groups 4.*keyhelp: Editing.*keyhelp: snapshot")

# a page's views laid out again for the printer (views/Reflow.cpp ReFlow,
# ReflowPreflight): the groups made and what is in each (needs the ROM image)
add_test(NAME host.NewtonReflow COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 35 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/reflow.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonReflow PROPERTIES TIMEOUT 150
                     PASS_REGULAR_EXPRESSION "reflow: families 1, sizes 1, faces 2.*reflow: 5 groups.*group 0 top 0, 1 children .Shopping list.*group 1 top 20, 2 children box 0,0 box 80,10.*group 2 top 20, 1 children .Bread.*group 3 top 0.*group 4 top 0, 1 children .newspaper for Sunday.*reflow: snapshot")

# a roll over a soup cursor scrolled (views/View.cpp TView::SyncScrollSoup):
# the cursor and viewOriginY after each step (needs the ROM image)
add_test(NAME host.NewtonSoupScroll COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 20 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/soupscroll.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonSoupScroll PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "down -> 1, answer nil.*tall one -> 1, origin 34.*past it -> 2, origin 0.*up into the tall one -> 1, origin 68.*up a step -> 1, origin 34.*past the last -> 0.*up at the first -> 0.*up from the third -> 1, origin 68.*soupscroll: done")

# the About slip's credits: MakePict records the lines as text shapes into one
# picture, which the credits view draws with CopyBits (needs the ROM image)
add_test(NAME host.NewtonCredits COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 50 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/credits.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonCredits PROPERTIES TIMEOUT 150
                     PASS_REGULAR_EXPRESSION "credits: creditPict 184 by 216, creditHeight 218.*credits: written")

# a sound played: the ROM's click through PlaySoundSync and through a
# protoSoundChannel with a callback, headless over the null sound backend
# (hal/host/HostSoundDriver.h), which counts what was played
add_test(NAME host.NewtonSound COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 3 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/sound.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonSound PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "sound: 520 samples played.*sound: played.*sound: started, active.*sound: callback state 0 error 0.*sound: closed")

# a protoTXView (text/TXView.h): text put in, typed into, restyled,
# scrolled, cut and pasted and scrubbed, pictures in build/txview-1.pgm to
# -4.pgm (needs the ROM image)
add_test(NAME host.NewtonTXView COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 16 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/txview.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonTXView PROPERTIES TIMEOUT 120
                     FAIL_REGULAR_EXPRESSION "FAILED|Exception"
                     PASS_REGULAR_EXPRESSION "txview: 176 characters, 142-176 the last line, scrolled from [1-9][0-9]* to 0.*txview: scrubbed to 167 characters.*txview: 2100 characters on the store, 2100 read back.*txview: replaced undoably, 4 characters left.*txview: the ruler.s justification is now left")

# a paginated protoTXView: the text poured through pages and a page break put
# in (src/text/TXFrames.h's TXPageFrames, TXFrameFormatter.h's TXPageFormatter)
add_test(NAME host.NewtonTXPages COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 12 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/txpages.ns WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(host.NewtonTXPages PROPERTIES TIMEOUT 120
                     FAIL_REGULAR_EXPRESSION "FAILED|Exception"
                     PASS_REGULAR_EXPRESSION "txpages: 2 pages, 3 with the page break, 655 pixels tall")

# the Sound Recorder driven through its Rec, Stop and Play buttons, headless:
# the null microphone hears a 1000 Hz test tone (newton --microphone-tone)
# and newton says how much of what was played back was that tone (the
# recorder records through TGSMCodec, GSM 06.10, which is lossy; the rest
# is the pen's clicks)
add_test(NAME host.NewtonRecorder COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 45 --microphone-tone 1000 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/recorder.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonRecorder PROPERTIES TIMEOUT 180
                     PASS_REGULAR_EXPRESSION "test tone makes up (8[0-9]|9[0-9]|100)% of what was played.*recorder: opened: Ready to record.*recorder: recording: Recording.*recorder: stopped: Ready to play or record.*recorder: played: Playing.*recorder: done")

# a protoBasicEndpoint over TCP (comms/NewScriptEndpoint.h): the NIE's
# 'inet service and 'itrs remote socket, to the echo server newton runs
# (--tcp-echo), written and read back synchronously and asynchronously
add_test(NAME host.NewtonEcho COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 8 --tcp-echo 52371 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/echo.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonEcho PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "echo: connected, state 5.*echo: received hello .byteCount 5.*echo: sent asynchronously, done.*echo: line 15 characters, ended by endsequence.*echo: frame bonjour 4 Array.*echo: disconnected, state 2.*echo: unbound, state 1.*echo: done")

# the NIE's DNS protocol ('sid ' naming 'dnst, a 'dnsq query and 'rrcd
# records) answered by the host's DNS service over the host's resolver
# (comms/host/HostDNSTool.h): localhost, a reverse lookup, a name that
# cannot exist - no network needed
add_test(NAME host.NewtonDNS COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 6 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/dns.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonDNS PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "dns: instantiated, state 1.*dns: localhost type 1: 127.0.0.1.*dns:  type 12: 0.0.0.0 .. -> ..[A-Za-z].*dns: no-such-host.invalid not found, -60791.*dns: done")

# a protoStreamingEndpoint (comms/StreamingEndpoint.h): a frame streamed
# out as NSOF and read back over TCP through a TEndpointPipe
add_test(NAME host.NewtonStream COMMAND newton --rom ${NEWTON_ROM_BIN} --display 320x480 --headless 8 --tcp-echo 52372 --script ${CMAKE_CURRENT_SOURCE_DIR}/demo/stream.ns WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(host.NewtonStream PROPERTIES TIMEOUT 120
                     PASS_REGULAR_EXPRESSION "stream: connected, state 5.*stream: sent, progress 1536 of 1854.*stream: received a streamed frame 1996 300 89401 .*progress 1854.*stream: done")
