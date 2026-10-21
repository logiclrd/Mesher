# M73 CLI-only default-on and negative opt-out checks.
# This test is independent of Vulkan hardware or running a window.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M73 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/main.cpp" source)
foreach(expected
    "collapseSettings.enabled=true;"
    "curvatureSettings.enabled=true;"
    "refineSettings.enabled=true;"
    "else if(arg==\"--no-collapse\") collapseSettings.enabled=false;"
    "else if(arg==\"--no-refine\") refineSettings.enabled=false;"
    "else if(arg==\"--no-curvature-correction\") curvatureSettings.enabled=false;"
    "collapse/curvature/refine ON")
    string(FIND "${source}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "M73 default-on/explicit opt-out contract missing: ${expected}")
    endif()
endforeach()
foreach(legacy
    "else if(arg==\"--collapse\")"
    "else if(arg==\"--curvature-correction\")"
    "else if(arg==\"--refine\")")
    string(FIND "${source}" "${legacy}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "M73 legacy positive CLI flag remained accepted: ${legacy}")
    endif()
endforeach()
string(FIND "${source}" "curvatureSettings.strength=v;" strength)
string(FIND "${source}" "curvatureSettings.enabled=true;" enabled)
# The ONLY enabling assignment occurs in defaults, before CLI parsing,
# and not inside the --curvature-strength branch.
if(strength EQUAL -1 OR enabled EQUAL -1 OR NOT enabled LESS strength)
    message(FATAL_ERROR "M73 strength/default ordering invalid")
endif()
string(REGEX MATCH "curvatureSettings[.]strength=v;[ \t\r\n]*curvatureSettings[.]enabled=true;" inadvertent "${source}")
if(inadvertent)
    message(FATAL_ERROR "M73 numeric gain may not override an explicit disable")
endif()
message(STATUS "M73 CLI defaults and all three explicit disables verified")
