add_executable(test_Options test_Options.cpp)
target_link_libraries(test_Options PRIVATE comms os600_kernel os600_user)
add_test(NAME comms.Options COMMAND test_Options)
set_tests_properties(comms.Options PROPERTIES TIMEOUT 30)

add_executable(test_CommTool test_CommTool.cpp)
target_link_libraries(test_CommTool PRIVATE comms os600_kernel os600_user)
add_test(NAME comms.CommTool COMMAND test_CommTool)
set_tests_properties(comms.CommTool PROPERTIES TIMEOUT 60)

add_executable(test_CommManager test_CommManager.cpp)
target_link_libraries(test_CommManager PRIVATE comms os600_kernel os600_user)
add_test(NAME comms.CommManager COMMAND test_CommManager)
set_tests_properties(comms.CommManager PROPERTIES TIMEOUT 60)

add_executable(test_Endpoint test_Endpoint.cpp)
target_link_libraries(test_Endpoint PRIVATE comms os600_kernel os600_user)
add_test(NAME comms.Endpoint COMMAND test_Endpoint)
set_tests_properties(comms.Endpoint PROPERTIES TIMEOUT 60)

add_executable(test_Translators test_Translators.cpp)
target_link_libraries(test_Translators PRIVATE comms_script os600_kernel os600_user)
target_compile_definitions(test_Translators PRIVATE "NEWTON_ROM_BIN=\"${NEWTON_ROM_BIN}\"")
add_test(NAME comms.Translators COMMAND test_Translators)
set_tests_properties(comms.Translators PROPERTIES TIMEOUT 60)

# every option class the host constructs is listed with its fields, for a
# script's options (comms/HostOptionLayouts.h)
add_executable(test_HostOptionLayouts test_HostOptionLayouts.cpp)
target_link_libraries(test_HostOptionLayouts PRIVATE comms_dock comms_fax os600_kernel os600_user)
add_test(NAME comms.HostOptionLayouts COMMAND test_HostOptionLayouts)
set_tests_properties(comms.HostOptionLayouts PROPERTIES TIMEOUT 30)

add_executable(test_SerialTool test_SerialTool.cpp)
target_link_libraries(test_SerialTool PRIVATE comms_serial hal_host_serial os600_kernel os600_user)
add_test(NAME comms.SerialTool COMMAND test_SerialTool)
set_tests_properties(comms.SerialTool PROPERTIES TIMEOUT 60)

# the ROM's V.42bis coder, checked against tools/dock/v42bis.py's
add_executable(test_V42bis test_V42bis.cpp)
target_link_libraries(test_V42bis PRIVATE comms_serial os600_kernel os600_user)
add_test(NAME comms.V42bisRoundTrip COMMAND test_V42bis)

# Sharp IR between two host IR chips over a socket (beaming layer 2)
add_executable(test_SharpIR test_SharpIR.cpp)
target_link_libraries(test_SharpIR PRIVATE comms_serial hal_host_serial os600_kernel os600_user)
add_test(NAME comms.SharpIR COMMAND test_SharpIR)
set_tests_properties(comms.SharpIR PROPERTIES TIMEOUT 120)

# the IrDA stack's self-contained parts: QOS, discovery info, the IAS database
add_executable(test_IrDAParts test_IrDAParts.cpp)
target_link_libraries(test_IrDAParts PRIVATE comms_serial)
add_test(NAME comms.IrDAParts COMMAND test_IrDAParts)

# the IR probe: probe against probe (IrDA), probe against Sharp IR
add_executable(test_IrProbe test_IrProbe.cpp)
target_link_libraries(test_IrProbe PRIVATE comms_serial hal_host_serial os600_kernel os600_user)
add_test(NAME comms.IrProbe COMMAND test_IrProbe)
set_tests_properties(comms.IrProbe PROPERTIES TIMEOUT 120)

# the IrDA stack between two host IR chips: discovery, the IAS lookup, a
# connection and data both ways
add_executable(test_IrDA test_IrDA.cpp)
target_link_libraries(test_IrDA PRIVATE comms_serial hal_host_serial os600_kernel os600_user)
add_test(NAME comms.IrDA COMMAND test_IrDA)
set_tests_properties(comms.IrDA PROPERTIES TIMEOUT 120)

# MNP against the desktop end in tools/dock/mnp.py (which runs the program)
add_executable(test_MNP test_MNP.cpp)
target_link_libraries(test_MNP PRIVATE comms_serial hal_host_serial os600_kernel os600_user)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    # the same link with V.42bis both ways (the ROM's coder against
    # tools/dock/v42bis.py's, in compressed mode)
    add_test(NAME comms.MNPV42bis
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/mnp.py --echo --v42bis --spawn $<TARGET_FILE:test_MNP>)
    set_tests_properties(comms.MNPV42bis PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "V.42bis .1024, 32.*came in compressed mode.*test_MNP: all passed")
    add_test(NAME comms.V42bis
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/v42bis.py --check $<TARGET_FILE:test_V42bis>)
    set_tests_properties(comms.V42bis PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "v42bis.py: all the same")
    add_test(NAME comms.MNP
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/mnp.py --echo --spawn $<TARGET_FILE:test_MNP>)
    set_tests_properties(comms.MNP PROPERTIES TIMEOUT 120)
    add_test(NAME comms.MNPLongHeaders
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/mnp.py --echo --no-class4 --spawn $<TARGET_FILE:test_MNP>)
    set_tests_properties(comms.MNPLongHeaders PROPERTIES TIMEOUT 120)
endif()

add_executable(test_MNPClass5 test_MNPClass5.cpp)
target_link_libraries(test_MNPClass5 PRIVATE comms_serial hal_host_serial os600_kernel os600_user)
add_test(NAME comms.MNPClass5 COMMAND test_MNPClass5)
set_tests_properties(comms.MNPClass5 PROPERTIES TIMEOUT 60)

add_executable(test_EzEndpointPipe test_EzEndpointPipe.cpp)
target_link_libraries(test_EzEndpointPipe PRIVATE comms_dock hal_host_serial os600_kernel os600_user)
if(Python3_FOUND)
    add_test(NAME comms.EzEndpointPipe
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/dock/mnp.py --echo --spawn $<TARGET_FILE:test_EzEndpointPipe>)
    set_tests_properties(comms.EzEndpointPipe PROPERTIES TIMEOUT 120)
endif()

# the modem tool's commands and answers (comms/ModemTool.h), without a modem
add_executable(test_ModemTool test_ModemTool.cpp)
target_link_libraries(test_ModemTool PRIVATE comms_serial os600_kernel os600_user)
add_test(NAME comms.ModemTool COMMAND test_ModemTool)
set_tests_properties(comms.ModemTool PROPERTIES TIMEOUT 30 PASS_REGULAR_EXPRESSION "test_ModemTool: all passed")

# the fax tool's MH decoder (comms/fax/T4FaxLine.h) against a page coded by
# tools/modem/t4.py, which is written from the T.4 recommendation alone
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_executable(test_T4FaxLine test_T4FaxLine.cpp)
    target_link_libraries(test_T4FaxLine PRIVATE comms_serial os600_kernel os600_user)
    add_test(NAME comms.T4Page
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/t4.py --test-page --fill 2
                     --pbm ${CMAKE_CURRENT_BINARY_DIR}/t4page.pbm -o ${CMAKE_CURRENT_BINARY_DIR}/t4page.t4)
    set_tests_properties(comms.T4Page PROPERTIES FIXTURES_SETUP t4page)
    add_test(NAME comms.T4FaxLine COMMAND test_T4FaxLine ${CMAKE_CURRENT_BINARY_DIR}/t4page.t4 ${CMAKE_CURRENT_BINARY_DIR}/t4page.pbm
                                          ${CMAKE_CURRENT_BINARY_DIR}/t4encoded.t4)
    set_tests_properties(comms.T4FaxLine PROPERTIES FIXTURES_REQUIRED t4page FIXTURES_SETUP t4encoded TIMEOUT 60
                         PASS_REGULAR_EXPRESSION "200 of 200 lines decoded.*200 lines coded.*test_T4FaxLine: all passed")
    # the fax tool's coder (EncodeT4) the other way: t4.py decodes what it
    # coded, and the page must come back
    add_test(NAME comms.T4Encode
             COMMAND ${CMAKE_COMMAND} -DPYTHON=${Python3_EXECUTABLE} -DT4=${CMAKE_SOURCE_DIR}/../tools/modem/t4.py
                     -DCODED=${CMAKE_CURRENT_BINARY_DIR}/t4encoded.t4 -DDECODED=${CMAKE_CURRENT_BINARY_DIR}/t4encoded.pbm
                     -DPAGE=${CMAKE_CURRENT_BINARY_DIR}/t4page.pbm -P ${CMAKE_CURRENT_SOURCE_DIR}/T4Encode.cmake)
    set_tests_properties(comms.T4Encode PROPERTIES FIXTURES_REQUIRED t4encoded TIMEOUT 60)
    # tools/modem/fakemodem.py's fax machine answering (--fax-answer), called
    # by a scripted Class 1 caller in place of the Newton
    add_test(NAME comms.FakemodemFaxAnswer COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/modem/fakemodem.py --self-test)
    set_tests_properties(comms.FakemodemFaxAnswer PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "fax answer self test passed")
endif()
