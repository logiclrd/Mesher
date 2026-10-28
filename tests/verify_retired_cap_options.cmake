# Once ring-and-hub is retired, its opt-in experiments must not silently
# become no-ops on the AFM production path. This test exercises the actual
# desktop CLI before any viewport or GPU initialization.
if(NOT DEFINED MESHER_EXE)
    message(FATAL_ERROR "Missing Mesher executable")
endif()
foreach(flag
    --repair-front-optimize
    --repair-curvature-optimize
    --repair-curvature-poisson
    --repair-feasibility-probe
    --repair-feasibility-preview
    --repair-diagonal-probe
    --repair-diagonal-preview
    --repair-whole-cap-fairing-preview)
    execute_process(
        COMMAND "${MESHER_EXE}" "${flag}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if("${status}" STREQUAL "0" OR
       NOT "${stdout}${stderr}" MATCHES "retired")
        message(FATAL_ERROR
            "Retired flag ${flag} must fail explicitly (status=${status}): "
            "${stdout} ${stderr}")
    endif()
endforeach()
message(STATUS "Retired ring optimizer flags explicitly refused")
