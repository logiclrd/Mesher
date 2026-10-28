# V18-M06 headless tests of the actual desktop command-line parser.
# No Vulkan loader, GLFW display or GPU is required for these preflight paths.
if(NOT DEFINED MESHER_EXE OR NOT EXISTS "${MESHER_EXE}")
    message(FATAL_ERROR "M06 executable path missing")
endif()

execute_process(
    COMMAND "${MESHER_EXE}" --help
    RESULT_VARIABLE helpResult
    OUTPUT_VARIABLE helpOutput
    ERROR_VARIABLE helpError)
if(NOT helpResult EQUAL 0 OR
   NOT helpOutput MATCHES "--avoid-collisions" OR
   NOT helpOutput MATCHES "Merge OFF collision avoidance" OR
   NOT helpOutput MATCHES "Mouse Inflate by default")
    message(FATAL_ERROR "M06 default/help contract wrong: ${helpResult} ${helpOutput} ${helpError}")
endif()

execute_process(
    COMMAND "${MESHER_EXE}" --avoid-collisions --help
    RESULT_VARIABLE armedHelpResult
    OUTPUT_VARIABLE armedHelpOutput
    ERROR_VARIABLE armedHelpError)
if(NOT armedHelpResult EQUAL 0 OR
   NOT armedHelpOutput MATCHES "--avoid-collisions")
    message(FATAL_ERROR "M06 selected flag not parsed in real desktop help")
endif()

function(mustRefuse expected)
    execute_process(
        COMMAND "${MESHER_EXE}" ${ARGN}
        RESULT_VARIABLE code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(code EQUAL 0 OR NOT stdout MATCHES "${expected}")
        message(FATAL_ERROR "M06 preflight did not refuse [${ARGN}] with [${expected}]: exit=${code} stdout=${stdout} stderr=${stderr}")
    endif()
endfunction()

# Prove both orderings of protected V17 / unprotected V18 conflict,
# and refusal BEFORE unsupported checkpoint file I/O or Vulkan init.
mustRefuse("conflicts with --merge-geometry"
    --merge-geometry --avoid-collisions)
mustRefuse("conflicts with --merge-geometry"
    --avoid-collisions --merge-geometry --recover m06-does-not-exist.mshr)
mustRefuse("requires interactive mouse Inflate"
    --avoid-collisions --demo)
mustRefuse("requires interactive mouse Inflate"
    --avoid-collisions --no-inflate)
mustRefuse("requires interactive mouse Inflate"
    --avoid-collisions --compare-remesh)
mustRefuse("requires interactive mouse Inflate"
    --avoid-collisions --compare-local-remesh)
mustRefuse("requires interactive mouse Inflate"
    --avoid-collisions --compare-mitigations)
mustRefuse("--merge-test-tetra requires explicit --merge-geometry"
    --avoid-collisions --merge-test-tetra --asymmetric)
mustRefuse("requires --merge-geometry"
    --avoid-collisions --merge-test-reflected-tetra)
mustRefuse("Unknown argument"
    --avoid-collisions-extra --help)

mustRefuse("--avoid-test-sheets requires --avoid-collisions --asymmetric"
    --avoid-test-sheets)
mustRefuse("--avoid-test-sheets requires --avoid-collisions --asymmetric"
    --avoid-collisions --avoid-test-sheets)
mustRefuse("--avoid-test-sheets requires --avoid-collisions --asymmetric"
    --avoid-collisions --asymmetric --avoid-test-sheets --recover m07-nonexistent.mshr)
mustRefuse("--avoid-test-sheets requires --avoid-collisions --asymmetric"
    --merge-geometry --avoid-test-sheets --asymmetric)

message(STATUS "V18-M07 actual protected/independent fixture CLI contract: PASS")
