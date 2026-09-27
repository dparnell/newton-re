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
add_executable(newton newton.cpp HostKeyboard.cpp HostPackages.cpp)
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
endif()
