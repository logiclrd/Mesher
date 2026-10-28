# The classification regression deliberately carries an archived
# successful cap with an operator note "prefer zipper". It is not allowed
# to pass merely because the original result was AUTO_CLOSED.
if(NOT DEFINED MESHER_EXE OR NOT DEFINED R06_CASE_FILE)
    message(FATAL_ERROR "R06 classifier regression requires executable and fixture")
endif()
execute_process(
    COMMAND "${MESHER_EXE}" --repair-replay-test "${R06_CASE_FILE}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT "${status}" STREQUAL "3" OR
   NOT stdout MATCHES "REPLAY_TEST CHOICE_MISMATCH" OR
   NOT stdout MATCHES "expected_choice=zipper actual_choice=caps")
    message(FATAL_ERROR
        "Expected honest classifier mismatch, status=${status}, stdout=${stdout}, stderr=${stderr}")
endif()
message(STATUS "R06-V2 completed-but-wrong classifier case correctly reports CHOICE_MISMATCH")
