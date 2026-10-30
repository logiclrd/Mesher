# M67 actual Vulkan mesh coverage is emitted only by depth-visible solid.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M67 MESHER_SOURCE_ROOT required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.vert" vert)
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.frag" frag)
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" view)
file(READ "${MESHER_SOURCE_ROOT}/src/partition_render.cpp" staging)
foreach(needle
    "layout(location=3) noperspective out vec3 edgeBarycentric;"
    "gl_VertexIndex%3"
    "edgeBarycentric=corner==0?")
    string(FIND "${vert}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M67 mesh vertex barycentric missing: ${needle}")
    endif()
endforeach()
foreach(needle
    "layout(location=3) noperspective in vec3 edgeBarycentric;"
    "dFdx(edgeBarycentric)" "dFdy(edgeBarycentric)"
    "abs(edgeBarycentric)/perPixel"
    "if(constants.wireRaster.w>0.5)"
    "if(surfaceMaterial<0.5)"
    "smoothstep(0.0,1.0,"
    "color=mix(color,vec3(0.073239),coverage*wireOpacity);")
    string(FIND "${frag}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M67 owning face shader missing: ${needle}")
    endif()
endforeach()
foreach(needle
    "constants.wireRaster[3]=wireVisible_?1.0f:0.0f;"
    "constants.wireRaster[3]=wireVisible_?1.0f:0.0f;"
    "depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;"
    "raster.cullMode=VK_CULL_MODE_NONE;")
    string(FIND "${view}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M67 Vulkan draw/depth route missing: ${needle}")
    endif()
endforeach()
foreach(needle
    "out.solid.push_back(vertex(a-renderOrigin,normal,colorOf(t[0])));"
    "out.solid.push_back(vertex(b-renderOrigin,normal,colorOf(t[1])));"
    "out.solid.push_back(vertex(c-renderOrigin,normal,colorOf(t[2])));")
    string(FIND "${staging}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M67 COW nonindexed source faces changed: ${needle}")
    endif()
endforeach()
string(FIND "${view}" "vkCmdBindVertexBuffers(commandBuffer,0,1,&lines.buffer,&offset);" old)
string(FIND "${view}" "vkCmdBindVertexBuffers(commandBuffer,0,1,&buffers.wire.buffer,&offset);" indexed)
if(NOT old EQUAL -1 OR NOT indexed EQUAL -1)
    message(FATAL_ERROR "M67 old mesh edge quads must NOT draw")
endif()
message(STATUS "M67 mesh AA resides on actual visible fragment; M68 floor grid is independent")
