# An archived V1 outcome remains REFUSED, but the current R06 cap is
# certified and must NOT be demoted to untrusted warning geometry merely
# because --repair-quality-warn is present.
if(NOT DEFINED MESHER_EXE OR NOT DEFINED R06_CASE_FILE)
    message(FATAL_ERROR "R06 quality-warning safety check lacks inputs")
endif()
execute_process(
    COMMAND "${MESHER_EXE}" --repair-replay-test "${R06_CASE_FILE}"
        --repair-quality-warn
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE err)
if(NOT "${status}" STREQUAL "2" OR
   NOT output MATCHES "REPLAY_TEST OUTCOME_MISMATCH" OR
   NOT output MATCHES "AUTO_CLOSED private_closed_mesh=1" OR
   output MATCHES "QUALITY_WARNING_PREVIEW untrusted=1" OR
   NOT output MATCHES "worker_history_checkpoint_unchanged=1")
    message(FATAL_ERROR
        "Certified R06 must not be demoted to warning-only geometry: "
        "status=${status}, stdout=${output}, stderr=${err}")
endif()
message(STATUS "R06 certified refusal-1 recovery stays certified with warning flag")
