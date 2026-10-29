# M62 calibrated smoothstep/pixel stroke must survive M69 cutover.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M62 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/include/mesher/wire_appearance.hpp" ink)
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" view)
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.frag" frag)
foreach(needle "kWireOuterHalfWidthPixels=1.5f" "t*t*(3.0f-2.0f*t)")
    string(FIND "${ink}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M62 CPU physical-pixel coverage changed")
    endif()
endforeach()
foreach(needle
    "result.wireRaster={float(width),float(height),kWireOuterHalfWidthPixels,0.0f};"
    "constants.wireRaster[3]=wireVisible_?1.0f:0.0f;"
    "depth.depthWriteEnable=(hudOverlay||paintOverlay)?VK_FALSE:VK_TRUE;"
    "depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;")
    string(FIND "${view}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M62 surface color/depth W invariant missing: ${needle}")
    endif()
endforeach()
foreach(needle "smoothstep(0.0,1.0,"
               "constants.wireRaster.z-distancePx"
               "color=mix(color,vec3(0.073239),coverage);")
    string(FIND "${frag}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M62 physical smooth coverage no longer in real shader")
    endif()
endforeach()
message(STATUS "M62 physical AA ink preserved on solid-owner mesh and floor")
