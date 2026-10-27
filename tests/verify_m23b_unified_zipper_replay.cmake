# M23B opt-in CLI proof on the exact immutable refusal-2 archive.
# Its historical expected refusal remains unchanged. Exit code 2 is
# intentional when the new certified repair improves that outcome.
if(NOT DEFINED MESHER_EXE OR NOT DEFINED R06_CASE_FILE)
    message(FATAL_ERROR "M23B replay needs executable and archived case")
endif()
execute_process(
    COMMAND "${MESHER_EXE}" --repair-replay-test "${R06_CASE_FILE}"
            --repair-unified-zipper
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${code}" STREQUAL "2" OR
   NOT output MATCHES "REPLAY_TEST OUTCOME_MISMATCH" OR
   NOT output MATCHES "AUTO_CLOSED private_closed_mesh=1" OR
   NOT output MATCHES "zipper_pairs=1" OR
   NOT output MATCHES "worker_history_checkpoint_unchanged=1" OR
   NOT output MATCHES "repair-r06-zipper-unified" OR
   NOT output MATCHES "repair-r06-m23b-placement" OR
   NOT output MATCHES "repair-r06-m23b-face-map")
    message(FATAL_ERROR
        "M23B archived private zipper CLI changed/missing: code=${code}, stdout=${output}, stderr=${err}")
endif()
string(REGEX MATCH "min_zipper_quality=([0-9]+[.][0-9]+)" found "${output}")
set(q "${CMAKE_MATCH_1}")
if(NOT found OR q LESS 0.10)
    message(FATAL_ERROR "M23B certified zipper q floor not met: q=${q}")
endif()
message(STATUS "M23B archived refusal-2 unified zipper PRIVATE certified: q=${q}")
