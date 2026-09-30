# ctest comms.T4Encode: tools/modem/t4.py decodes the page the fax tool's
# EncodeT4 coded (test_T4FaxLine wrote it), and what it makes of it must be
# the page itself.
execute_process(COMMAND ${PYTHON} ${T4} --decode ${CODED} --pbm ${DECODED} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "t4.py could not decode ${CODED}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${DECODED} ${PAGE} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the page t4.py decoded from EncodeT4's code differs from the page")
endif()
message(STATUS "EncodeT4's page decoded by t4.py: the same page")
