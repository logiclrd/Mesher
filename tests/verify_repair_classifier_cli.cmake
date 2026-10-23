# Compare BOTH R06-M03 classifiers against the *same* committed
# physical 5K candidate, without requiring a graphics device.
if(NOT DEFINED MESHER_EXE OR NOT DEFINED R06_CASE_FILE)
    message(FATAL_ERROR "R06 M08 needs executable and exact archive")
endif()
foreach(classifier IN ITEMS area proximity)
    execute_process(
        COMMAND "${MESHER_EXE}" --repair-classifier "${classifier}"
                --repair-replay-test "${R06_CASE_FILE}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    # Recorded original AUTO_REFUSED might become a new legitimate success
    # under the experimental topology; that is a *reported* exit 2.
    if(NOT ("${status}" STREQUAL "0" OR "${status}" STREQUAL "2") OR
       NOT stdout MATCHES "classifier=${classifier}" OR
       NOT stdout MATCHES "REPLAY_TEST (PASS|OUTCOME_MISMATCH)")
        message(FATAL_ERROR
            "M08 headless ${classifier} failed: exit=${status}, stdout=${stdout}, stderr=${stderr}")
    endif()
    if("${classifier}" STREQUAL "proximity" AND
       NOT stdout MATCHES "M08 proximity: area1=.* average_term=.* difference_term=.* distance_term=.* score=")
        message(FATAL_ERROR
            "M08 proximity output omits the individual fitness terms on the physical two-rim case: ${stdout}")
    endif()
endforeach()
message(STATUS "M08 exact physical two-rim archive was evaluated under BOTH classifiers")
