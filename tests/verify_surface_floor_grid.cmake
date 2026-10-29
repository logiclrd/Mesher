# M68: GLSL unit-world-XY lines must be sampled on real floor fragment,
# never depth-flat line quads or the floor triangulation's fake diagonal.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M68 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" view)
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.frag" frag)
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.vert" vert)
foreach(needle
    "nearest=abs(worldPosition.xy-round(worldPosition.xy));"
    "dFdx(worldPosition.x)" "dFdy(worldPosition.x)"
    "dFdx(worldPosition.y)" "dFdy(worldPosition.y)"
    "gridDistance=nearest/unitsPerPixel;"
    "distancePx=min(gridDistance.x,gridDistance.y);"
    "wireHalfwidth-distancePx"
    "wireOpacity=smoothstep(1.5,10.0,gridSpacingPx);"
    "color=mix(color,vec3(0.073239),coverage*wireOpacity);"
    "if(surfaceMaterial<0.5)")
    string(FIND "${frag}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M68 world-XY floor pixel-distance shader missing: ${needle}")
    endif()
endforeach()
foreach(needle
    "layout(location=0) out vec3 worldPosition;"
    "worldPosition=position;")
    string(FIND "${vert}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M68 floor world XY interpolant unavailable: ${needle}")
    endif()
endforeach()
foreach(needle
    "floorSolid=upload(scenery.solid);"
    "if(floorVisible_) {"
    "vkCmdBindVertexBuffers(commandBuffer,0,1,&floorSolid.buffer,&offset);"
    "constants.wireRaster[3]=wireVisible_?1.0f:0.0f;"
    "const Vec3 a{center.x-halfExtent,center.y-halfExtent,z};"
    "const Vec3 b{center.x+halfExtent,center.y-halfExtent,z};"
    "const Vec3 c{center.x+halfExtent,center.y+halfExtent,z};"
    "const Vec3 d{center.x-halfExtent,center.y+halfExtent,z};")
    string(FIND "${view}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M68 real floor G/W and source integer grid missing: ${needle}")
    endif()
endforeach()
string(FIND "${view}" "vkCmdBindVertexBuffers(commandBuffer,0,1,&floorLines.buffer,&offset);" oldFloor)
string(FIND "${view}" "vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,wirePipeline);" oldWire)
if(NOT oldFloor EQUAL -1 OR NOT oldWire EQUAL -1)
    message(FATAL_ERROR "M68 detached grid AA quads must NOT be drawn")
endif()
message(STATUS "M68 1-unit floor grid follows depth-visible floor fragments; no fake triangle diagonal")
