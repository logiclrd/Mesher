# R06-M09 pure source contract: the R06 comparison must be rendered
# on the frozen 2-line upper-left HUD, distinct from the FPS/stamps box.
# Actual RGB and GUI placement still need physical Vulkan acceptance.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M09 HUD test requires source root")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" viewport)
file(READ "${MESHER_SOURCE_ROOT}/shaders/hud.frag" fragment)
file(READ "${MESHER_SOURCE_ROOT}/src/repair_inspection.cpp" repair)
foreach(required IN ITEMS
        "AREA: "
        "PROXIMITY: "
        "classifierComparable_"
        "classifiersAgree_"
        "classifiersAgree?2.0f:3.0f"
        "activeClassifier==0?\" <---\":\"\""
        "activeClassifier==1?\" <---\":\"\""
        "const int textX=margin+padding,textY=margin+padding")
    string(FIND "${viewport}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "M09 missing frozen top-left HUD contract: ${required}")
    endif()
endforeach()
foreach(required IN ITEMS
    "kind > 2.5" "kind > 1.5"
    "vec4(1.0, 0.31, 0.31, 1.0)"
    "vec4(0.37, 1.0, 0.50, 1.0)")
    string(FIND "${fragment}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "M09 missing distinct red/green glyph material: ${required}")
    endif()
endforeach()
string(FIND "${repair}" "compareClassifierPlans(*result,classifier);" found)
if(found EQUAL -1)
    message(FATAL_ERROR "M09 does not evaluate independent alternate M03 plan")
endif()
message(STATUS "M09 independent green/red comparison HUD and chosen arrow source contracts PASS")
