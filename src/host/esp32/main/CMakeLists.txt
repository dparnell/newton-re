# The reconstruction as one ESP-IDF component: the tree's sources
# (newton_sources.cmake, made by tools/tdeck/idf_sources.py) and the
# device's own host code beside this file.
get_filename_component(NEWTON_SRC ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
include(${CMAKE_CURRENT_LIST_DIR}/newton_sources.cmake)

set(sources)
foreach(s ${NEWTON_SOURCES})
    list(APPEND sources ${NEWTON_SRC}/${s})
endforeach()
list(APPEND sources ${NEWTON_FORWARDED})
# (the DDK's own directory as -I too: three of its headers include others
#  with <...>, and none of its file names is a system header's)
idf_component_register(SRCS esp32main.cpp newtonmain.cpp tdeck_board.cpp tdeck_audio.cpp tdeck_panel.cpp tdeck_input.cpp tdeck_store.cpp tdeck_profile.cpp tdeck_wifi.cpp tdeck_modem.cpp tdeck_lora.cpp tdeck_gps.cpp tdeck_battery.cpp tdeck_power.cpp ${sources}
                       INCLUDE_DIRS . ${NEWTON_SRC}/ddk
                       REQUIRES driver esp_partition spi_flash esp_timer esp_psram vfs freertos lwip nvs_flash esp_wifi esp_netif esp_event esp_pm)

# the flags the tree is built with everywhere (src/CMakeLists.txt): the
# DDK configured for the MessagePad, host_compat.h first, signed overflow
# wrapping, chars signed, every function four-byte aligned; C++17 as on
# the desktop; and quiet - the tree's warnings are the desktop build's
# business
# Each source's include directories, as the desktop build gives them and in
# its order (newton_sources.cmake's groups: headers of one name live in more
# than one directory).  As -iquote, searched for "..." includes only: as -I,
# src/intl/Locale.h would be found for the C library's <locale.h> on a
# case-insensitive file system.
math(EXPR last "${NEWTON_GROUP_COUNT} - 1")
foreach(n RANGE ${last})
    set(quoted)
    foreach(d ${NEWTON_GROUP_${n}_INCLUDES})
        list(APPEND quoted -iquote${NEWTON_SRC}/${d})
    endforeach()
    set(files)
    foreach(s ${NEWTON_GROUP_${n}_SOURCES})
        if(s MATCHES "^forwarded/")
            list(APPEND files ${CMAKE_CURRENT_LIST_DIR}/${s})
        else()
            list(APPEND files ${NEWTON_SRC}/${s})
        endif()
    endforeach()
    set_source_files_properties(${files} PROPERTIES COMPILE_OPTIONS "${quoted}")
endforeach()
# (the device's own Newton-side file: every directory, in the same order
#  PlatformIO would put them in anyway)
set(all_quoted)
math(EXPR last "${NEWTON_GROUP_COUNT} - 1")
foreach(n RANGE ${last})
    foreach(d ${NEWTON_GROUP_${n}_INCLUDES})
        list(APPEND all_quoted -iquote${NEWTON_SRC}/${d})
    endforeach()
endforeach()
list(REMOVE_DUPLICATES all_quoted)
set_source_files_properties(newtonmain.cpp PROPERTIES COMPILE_OPTIONS "${all_quoted};-iquote${NEWTON_SRC}/host")
# (the board's modem driver hands its UART to hal/host/HostCellular.h's engine,
#  which is plain C++ - none of the Newton's headers)
set_source_files_properties(tdeck_modem.cpp PROPERTIES COMPILE_OPTIONS "-iquote${NEWTON_SRC}/hal/host")
# (and its battery's gauge is given to power/host/HostBattery.h, plain C++ too)
set_source_files_properties(tdeck_battery.cpp PROPERTIES COMPILE_OPTIONS "-iquote${NEWTON_SRC}/power/host")
set_source_files_properties(tdeck_audio.cpp PROPERTIES COMPILE_OPTIONS "-iquote${NEWTON_SRC}/host")

# The LoRa mesh (docs/host-tdeck-pro-mesh.md): MeshCore and what it needs
# (thirdparty/meshcore, as its CMakeLists.txt lists them for the desktops),
# RadioLib's SX126x driver and MeshCore's wrapper for it, and the engine
# over them (hal/host/HostMesh.cpp) - left out of newton_sources.cmake
# (tools/tdeck/idf_sources.py) and compiled here with their own include
# directories, as -I: MeshCore includes its headers with <...>.  MeshCore's
# contact and channel counts change BaseChatMesh's layout, and RadioLib's
# GODMODE and STATIC_ONLY its classes', so every one of these files is
# built with the same definitions; the radio's (LORA_*, SX126X_*) are
# lilyphone1's build flags, except the TCXO's voltage and the RF switch,
# which are LilyGO's (its examples/test_lora_recv; Meshtastic's t-deck-pro
# variant the same): the module's DIO2 drives its antenna's TX/RX switch,
# and without SX126X_DIO2_AS_RF_SWITCH the switch was never turned to
# receive - the radio sent, and heard only what was very close
# (2026-10-14: 50 packets sent, 1 heard, overnight).  The boosted receive
# gain is MeshCore's own T-Deck variant's.
set(MESHCORE ${NEWTON_SRC}/thirdparty/meshcore)
set(mesh_sources
    ${MESHCORE}/MeshCore/src/Dispatcher.cpp
    ${MESHCORE}/MeshCore/src/Identity.cpp
    ${MESHCORE}/MeshCore/src/Mesh.cpp
    ${MESHCORE}/MeshCore/src/Packet.cpp
    ${MESHCORE}/MeshCore/src/Utils.cpp
    ${MESHCORE}/MeshCore/src/helpers/AdvertDataHelpers.cpp
    ${MESHCORE}/MeshCore/src/helpers/BaseChatMesh.cpp
    ${MESHCORE}/MeshCore/src/helpers/StaticPoolPacketManager.cpp
    ${MESHCORE}/MeshCore/src/helpers/TxtDataHelpers.cpp
    ${MESHCORE}/Crypto/src/AES128.cpp
    ${MESHCORE}/Crypto/src/AES192.cpp
    ${MESHCORE}/Crypto/src/AES256.cpp
    ${MESHCORE}/Crypto/src/AESCommon.cpp
    ${MESHCORE}/Crypto/src/BigNumberUtil.cpp
    ${MESHCORE}/Crypto/src/BlockCipher.cpp
    ${MESHCORE}/Crypto/src/Crypto.cpp
    ${MESHCORE}/Crypto/src/Curve25519.cpp
    ${MESHCORE}/Crypto/src/Ed25519.cpp
    ${MESHCORE}/Crypto/src/Hash.cpp
    ${MESHCORE}/Crypto/src/SHA256.cpp
    ${MESHCORE}/Crypto/src/SHA512.cpp
    ${MESHCORE}/ed25519/src/add_scalar.c
    ${MESHCORE}/ed25519/src/fe.c
    ${MESHCORE}/ed25519/src/ge.c
    ${MESHCORE}/ed25519/src/key_exchange.c
    ${MESHCORE}/ed25519/src/keypair.c
    ${MESHCORE}/ed25519/src/sc.c
    ${MESHCORE}/ed25519/src/sha512.c
    ${MESHCORE}/ed25519/src/sign.c
    ${MESHCORE}/ed25519/src/verify.c
    ${MESHCORE}/compat/CryptoRNG.cpp
    ${NEWTON_SRC}/hal/host/HostMesh.cpp)
file(GLOB radiolib_sources
    ${MESHCORE}/RadioLib/src/*.cpp
    ${MESHCORE}/RadioLib/src/utils/*.cpp
    ${MESHCORE}/RadioLib/src/protocols/PhysicalLayer/*.cpp
    ${MESHCORE}/RadioLib/src/modules/SX126x/*.cpp)
set(radio_wrapper ${MESHCORE}/MeshCore/src/helpers/radiolib/RadioLibWrappers.cpp)
# (PlatformIO names an object by its source's file name alone, and these
#  have names other sources have - ed25519's sha512.c and Crypto's
#  SHA512.cpp, on a case-insensitive file system - so each is compiled
#  through a file of its own name, made here, that includes it: as
#  idf_sources.py's forwarded/ does for the tree's)
function(mesh_forward out_var)
    set(forwarded)
    foreach(f ${ARGN})
        file(RELATIVE_PATH rel ${NEWTON_SRC} ${f})
        string(REPLACE "/" "_" name ${rel})
        set(out ${CMAKE_CURRENT_BINARY_DIR}/meshfwd/${name})
        set(content "// made by src/host/esp32/main/CMakeLists.txt: ${rel} under a name of its own\n#include \"${f}\"\n")
        set(old "")
        if(EXISTS ${out})
            file(READ ${out} old)
        endif()
        if(NOT old STREQUAL content)
            file(WRITE ${out} "${content}")
        endif()
        list(APPEND forwarded ${out})
    endforeach()
    set(${out_var} ${forwarded} PARENT_SCOPE)
endfunction()
mesh_forward(mesh_sources ${mesh_sources})
mesh_forward(radiolib_sources ${radiolib_sources})
mesh_forward(radio_wrapper ${radio_wrapper})
target_sources(${COMPONENT_LIB} PRIVATE ${mesh_sources} ${radiolib_sources} ${radio_wrapper})
set(mesh_includes -I${MESHCORE}/compat -I${MESHCORE}/MeshCore/src -I${MESHCORE}/Crypto/src -I${MESHCORE}/ed25519/src
                  -I${MESHCORE}/base64/src -I${MESHCORE}/RadioLib/src)
set(mesh_definitions MAX_CONTACTS=64 MAX_GROUP_CHANNELS=8 ED25519_NO_SEED=1 RADIOLIB_GODMODE=1 RADIOLIB_STATIC_ONLY=1
                     MESHCORE_RADIOLIB=1 LORA_FREQ=916.575 LORA_BW=62.5 LORA_SF=7 LORA_CR=8 LORA_TX_POWER=22
                     SX126X_DIO3_TCXO_VOLTAGE=2.4 SX126X_CURRENT_LIMIT=140
                     SX126X_DIO2_AS_RF_SWITCH=true SX126X_RX_BOOSTED_GAIN=1)
set_source_files_properties(${mesh_sources} ${radiolib_sources} PROPERTIES
                            COMPILE_OPTIONS "${mesh_includes};-iquote${NEWTON_SRC}/hal/host"
                            COMPILE_DEFINITIONS "${mesh_definitions}")
# (the wrapper and the board's radio want Arduino's few names: compat/Arduino.h
#  forced in)
set_source_files_properties(${radio_wrapper} tdeck_lora.cpp PROPERTIES
                            COMPILE_OPTIONS "${mesh_includes};-iquote${NEWTON_SRC}/hal/host;-include;${MESHCORE}/compat/Arduino.h"
                            COMPILE_DEFINITIONS "${mesh_definitions}")

# comms/host/HostLink.ns, the host's link for the NIE, compiled in as a
# string as the desktop build does it (src/comms/CMakeLists.txt) - made
# when CMake configures, not by a custom command: PlatformIO builds with
# SCons from CMake's description and runs none (a change to HostLink.ns
# reconfigures)
set(INPUT ${NEWTON_SRC}/comms/host/HostLink.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostLinkSource.cpp)
set(NAME gHostLinkSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostLinkSource.cpp)
# comms/host/HostCell.ns, the 4G module's telephone provider, the same way
set(INPUT ${NEWTON_SRC}/comms/host/HostCell.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostCellSource.cpp)
set(NAME gHostCellSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostCellSource.cpp)
# comms/host/HostText.ns, its text message transport
set(INPUT ${NEWTON_SRC}/comms/host/HostText.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostTextSource.cpp)
set(NAME gHostTextSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostTextSource.cpp)
# comms/host/HostMessages.ns, the Messages application
set(INPUT ${NEWTON_SRC}/comms/host/HostMessages.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostMessagesSource.cpp)
set(NAME gHostMessagesSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostMessagesSource.cpp)
# comms/host/HostMesh.ns, the Mesh application
set(INPUT ${NEWTON_SRC}/comms/host/HostMesh.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostMeshSource.cpp)
set(NAME gHostMeshSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostMeshSource.cpp)
# print/host/HostPrinters.ns, the Network Printers preferences panel (print/
# host/HostNetworkPrinters.cpp: the IPP printers the board's mDNS question
# finds, and the ROM's network printer chooser answered from it)
set(INPUT ${NEWTON_SRC}/print/host/HostPrinters.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostPrintersSource.cpp)
set(NAME gHostPrintersSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostPrintersSource.cpp)
# host/HostSettings.ns, the Host preferences panel (host/HostSettings.cpp,
# beside it: the panel's settings - tdeck_panel.cpp answers them)
set(INPUT ${NEWTON_SRC}/host/HostSettings.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostSettingsSource.cpp)
set(NAME gHostSettingsSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostSettingsSource.cpp ${NEWTON_SRC}/host/HostSettings.cpp)
set_source_files_properties(${NEWTON_SRC}/host/HostSettings.cpp PROPERTIES COMPILE_OPTIONS "${all_quoted};-iquote${NEWTON_SRC}/host")

# host/HostQuickSheet.ns, the quick sheet pulled down from the top edge and
# the lock screen (host/HostQuickSheet.cpp; tdeck_input.cpp sees the pull)
set(INPUT ${NEWTON_SRC}/host/HostQuickSheet.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostQuickSheetSource.cpp)
set(NAME gHostQuickSheetSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostQuickSheetSource.cpp ${NEWTON_SRC}/host/HostQuickSheet.cpp)
set_source_files_properties(${NEWTON_SRC}/host/HostQuickSheet.cpp PROPERTIES COMPILE_OPTIONS "${all_quoted};-iquote${NEWTON_SRC}/host")

# host/HostDeveloper.ns, the Developer preferences panel (host/HostDeveloper.cpp:
# no report file here, so the profile goes to the console)
set(INPUT ${NEWTON_SRC}/host/HostDeveloper.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostDeveloperSource.cpp)
set(NAME gHostDeveloperSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostDeveloperSource.cpp ${NEWTON_SRC}/host/HostDeveloper.cpp)
set_source_files_properties(${NEWTON_SRC}/host/HostDeveloper.cpp PROPERTIES COMPILE_OPTIONS "${all_quoted};-iquote${NEWTON_SRC}/host")

# host/HostWordEngines.ns, the host's handwriting engines in the Handwriting
# Recognition slip and Setup's handwriting page (host/HostWordEngines.cpp:
# HostWordEngines(), which the letter-set box asks; without it the unistroke
# engine is built and installed, but no button offers it)
set(INPUT ${NEWTON_SRC}/host/HostWordEngines.ns)
set(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/HostWordEnginesSource.cpp)
set(NAME gHostWordEnginesSource)
include(${NEWTON_SRC}/cmake/EmbedText.cmake)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${INPUT})
target_sources(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/HostWordEnginesSource.cpp ${NEWTON_SRC}/host/HostWordEngines.cpp)
set_source_files_properties(${NEWTON_SRC}/host/HostWordEngines.cpp PROPERTIES COMPILE_OPTIONS "${all_quoted};-iquote${NEWTON_SRC}/host")

target_compile_definitions(${COMPONENT_LIB} PRIVATE forQ=1 NDEBUG)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -include${NEWTON_SRC}/host/host_compat.h
    -fwrapv -fsigned-char -falign-functions=4 -w
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)

# malloc(0) answers a block, as the desktops' C libraries do and the tree
# assumes (newlib's on the ESP32-S3 answers nil: esp32main.cpp's
# __wrap_malloc)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=malloc")
