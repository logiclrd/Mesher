# Captured V1 REFUSED outcomes remain immutable historical evidence.
# A newer certified AUTO_CLOSED must be treated as an explicit recovery,
# independent of which finite remeshing primitive obtained legal geometry.
if(NOT DEFINED MESHER_EXE OR NOT DEFINED R06_CASE_FILE OR
   NOT DEFINED CASE_ID)
    message(FATAL_ERROR "Reviewed recovery check needs executable, archive and case ID")
endif()
execute_process(
    COMMAND "${MESHER_EXE}" --repair-replay-test "${R06_CASE_FILE}"
            --repair-classifier area
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${code}" STREQUAL "2" OR
   NOT output MATCHES "REPLAY_TEST OUTCOME_MISMATCH" OR
   NOT output MATCHES "actual=AUTO_CLOSED" OR
   NOT output MATCHES "AUTO_CLOSED private_closed_mesh=1" OR
   NOT output MATCHES "worker_history_checkpoint_unchanged=1")
    message(FATAL_ERROR "Archive ${CASE_ID} no longer has certified private recovery, status=${code}, stdout=${output}, err=${err}")
endif()
if("${CASE_ID}" STREQUAL "2")
    set(expectedChoice "cap_loops=0 zipper_pairs=1")
    set(qualityLabel "min_zipper_quality")
elseif("${CASE_ID}" STREQUAL "4" OR "${CASE_ID}" STREQUAL "5")
    set(expectedChoice "cap_loops=1 zipper_pairs=0")
    set(qualityLabel "min_cap_quality")
else()
    message(FATAL_ERROR "No reviewed recovery expectation for case ${CASE_ID}")
endif()
if(NOT output MATCHES "${expectedChoice}")
    message(FATAL_ERROR "Case ${CASE_ID} chose wrong closure kind: ${output}")
endif()
string(REGEX MATCH "${qualityLabel}=([0-9]+[.][0-9]+)" found "${output}")
set(worstQuality "${CMAKE_MATCH_1}")
if(NOT found OR worstQuality LESS 0.10)
    message(FATAL_ERROR "Case ${CASE_ID} violated original strict 0.10 quality floor (${qualityLabel}=${worstQuality}): ${output}")
endif()
message(STATUS "Reviewed case ${CASE_ID}: immutable V1 expected REFUSED; new certified AUTO_CLOSED, q=${worstQuality} >= 0.10, no publication")
