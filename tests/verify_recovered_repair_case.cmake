# M10: these archived V1 cases were originally REFUSED and MUST remain
# untouched historic evidence. The new algorithm legitimately closes them.
# A headless replay returns exit 2 (recorded REFUSED != computed CLOSED);
# that is a deliberate, reviewed improvement, not a CI failure.
if(NOT DEFINED MESHER_EXE OR NOT DEFINED R06_CASE_FILE OR
   NOT DEFINED CASE_ID)
    message(FATAL_ERROR "M10 recovered-case test needs exe, case and ID")
endif()
execute_process(
    COMMAND "${MESHER_EXE}" --repair-replay-test "${R06_CASE_FILE}"
            --repair-classifier area
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${code}" STREQUAL "2" OR
   NOT output MATCHES "REPLAY_TEST OUTCOME_MISMATCH" OR
   NOT output MATCHES "actual=AUTO_CLOSED" OR
   NOT output MATCHES "AUTO_CLOSED private_closed_mesh=1" OR
   NOT output MATCHES "interior_quality_flips=[1-9][0-9]*")
    message(FATAL_ERROR "M10 case ${CASE_ID} not recovered, status=${code}, stdout=${output}, err=${err}")
endif()
if("${CASE_ID}" STREQUAL "2")
    if(NOT output MATCHES "cap_loops=0 zipper_pairs=1" OR
       NOT output MATCHES "min_zipper_quality=0.1[0-9]*")
        message(FATAL_ERROR "M10 case 2 must close as zipper with unchanged q>=0.10: ${output}")
    endif()
elseif("${CASE_ID}" STREQUAL "4")
    if(NOT output MATCHES "cap_loops=1 zipper_pairs=0" OR
       NOT output MATCHES "min_cap_quality=0.10[0-9]*")
        message(FATAL_ERROR "M10 case 4 must close as reflected cap with unchanged q>=0.10: ${output}")
    endif()
else()
    message(FATAL_ERROR "No reviewed M10 recovery expectation for case ${CASE_ID}")
endif()
message(STATUS "M10 independently verified recovered case ${CASE_ID}: historic REFUSED preserved; new AUTO_CLOSED with finite flips and q>=0.10")
