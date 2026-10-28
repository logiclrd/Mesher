# AUTO is the only normal interactive Inflate repair policy. This
# GPU-free parser/entry-point contract tests the DESIRED semantics, not
# historical disabled- or forced-mode behavior.
if(NOT DEFINED MESHER_EXE OR NOT EXISTS "${MESHER_EXE}" OR
   NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "AUTO CLI test requires Mesher executable and source root")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/main.cpp" source)
foreach(expected
    "const bool liveCertifiedAuto=inflate&&!demoMode"
    "!diagnosticRepair&&"
    "repairReplayPath.empty()&&!allowMergeGeometry&&!avoidCollisions"
    "desktopPolicy.certifiedAutoClosure=liveCertifiedAuto;"
    "desktopPolicy.certifiedAutoUnifiedZipper=true;"
    "repairPreview,repairExcise,false,false,"
    "repairAuto,options.symmetric,repairClassifier"
    "const bool diagnosticRepair=repairInspect||repairPreview||repairExcise;")
    string(FIND "${source}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Default certified AUTO, protected-mode isolation or inspector contract missing: ${expected}")
    endif()
endforeach()
foreach(removed
    "arg==\"--repair\""
    "arg==\"--repair-auto\""
    "arg==\"--repair-cap\""
    "arg==\"--repair-zipper\"")
    string(FIND "${source}" "${removed}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "Removed repair mode must not be parsed: ${removed}")
    endif()
endforeach()
execute_process(
    COMMAND "${MESHER_EXE}" --help
    RESULT_VARIABLE helpStatus OUTPUT_VARIABLE helpText ERROR_VARIABLE helpError)
if(NOT "${helpStatus}" STREQUAL "0" OR
   NOT helpText MATCHES "Certified R07 AUTO repair is enabled by default" OR
   helpText MATCHES "\\[--repair (auto|cap|zipper)" OR
   NOT helpText MATCHES "--repair-inspect")
    message(FATAL_ERROR "Default AUTO help contract: ${helpStatus} ${helpText} ${helpError}")
endif()
# A removed argument must NOT be accepted as an alias, silently ignored,
# or interpreted as an alternative uncertified geometry mode.
foreach(flag --repair --repair-auto --repair-cap --repair-zipper)
    execute_process(
        COMMAND "${MESHER_EXE}" "${flag}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if("${status}" STREQUAL "0" OR
       NOT "${stdout}${stderr}" MATCHES "Unknown argument: ${flag}")
        message(FATAL_ERROR "Retired selector ${flag} must fail before Vulkan: ${status} ${stdout} ${stderr}")
    endif()
endforeach()
# These standalone diagnostic switches remain intentionally opt-in,
# rather than automatically modifying live AUTO's publication policy.
execute_process(
    COMMAND "${MESHER_EXE}" --repair-quality-warn
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if("${status}" STREQUAL "0" OR
   NOT "${stdout}${stderr}" MATCHES "require --repair-inspect")
    message(FATAL_ERROR "Private inspection diagnostic must not enter live AUTO: ${status} ${stdout} ${stderr}")
endif()
message(STATUS "Default certified R07 AUTO, retired mode selectors and private inspector separation PASS")
