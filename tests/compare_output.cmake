execute_process(COMMAND ${PROGRAM} ${ARGS}
    OUTPUT_VARIABLE actual RESULT_VARIABLE status)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "${PROGRAM} exited with ${status}")
endif()
file(READ "${GOLDEN}" expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "output of ${PROGRAM} differs from ${GOLDEN}")
endif()
