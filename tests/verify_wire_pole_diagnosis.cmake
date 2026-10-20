# M64: certify the diagnosis targets the *real* pre-fix Vulkan path,
# and the staged render representation is not confused with floor grid.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M64 requires MESHER_SOURCE_ROOT")
endif()
file(READ "${MESHER_SOURCE_ROOT}/shaders/wire.vert" wire)
file(READ "${MESHER_SOURCE_ROOT}/shaders/wire.frag" frag)
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" viewport)
file(READ "${MESHER_SOURCE_ROOT}/src/partition_render.cpp" partition)
foreach(needle
    "projected.xy+=perpendicular"
    "gl_Position=projected;")
    string(FIND "${wire}" "${needle}" where)
    if(where EQUAL -1)
        message(FATAL_ERROR "M64 baseline depth-flat wire quad assumption invalid: ${needle}")
    endif()
endforeach()
foreach(needle
    "depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;"
    "depth.depthWriteEnable=(wire||hudOverlay)?VK_FALSE:VK_TRUE;"
    "const Vec3 a{-8,-8,z},b{8,-8,z},c{8,8,z},d{-8,8,z};"
    "for(const auto p:{a,b,c,a,c,d})"
    "for(int i=-8;i<=8;++i)"
    "out.lines.push_back(makeVertex({double(i),-8,z}));"
    "out.solid.push_back(makeVertex(a,normal));"
    "out.solid.push_back(makeVertex(b,normal));"
    "out.solid.push_back(makeVertex(c,normal));")
    string(FIND "${viewport}" "${needle}" inView)
    if(needle MATCHES "out[.]solid[.]push_back")
        # Legacy solid triples are explicitly represented in the viewport.
    elseif(inView EQUAL -1)
        message(FATAL_ERROR "M64 baseline solid/floor staging assumption invalid: ${needle}")
    endif()
endforeach()
string(FIND "${partition}" "out.solid.push_back(vertex(a,normal));" a)
string(FIND "${partition}" "out.solid.push_back(vertex(b,normal));" b)
string(FIND "${partition}" "out.solid.push_back(vertex(c,normal));" c)
if(a EQUAL -1 OR b EQUAL -1 OR c EQUAL -1)
    message(FATAL_ERROR "M64 source face triples not available for future surface shading")
endif()
string(FIND "${frag}" "smoothstep(0.0,1.0,aaDistance.y-abs(aaDistance.x))" aa)
if(aa EQUAL -1)
    message(FATAL_ERROR "M64 diagnosed baseline is not the calibrated M62 line")
endif()
message(STATUS "M64 depth-flat wire vs real solid-face and independent floor grid assumptions verified")
