# M58's detached quads were superseded by M67/M68 surface-attached
# AA. M69 ensures NO old quad pipeline is compiled or drawn.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M69 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" view)
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.vert" vert)
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.frag" frag)
file(READ "${MESHER_SOURCE_ROOT}/CMakeLists.txt" cmake)
foreach(needle "gl_VertexIndex%3" "noperspective out vec3 edgeBarycentric")
    string(FIND "${vert}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M67 source face AA vertex identity missing: ${needle}")
    endif()
endforeach()
foreach(needle "noperspective in vec3 edgeBarycentric"
               "dFdx(edgeBarycentric)" "dFdy(edgeBarycentric)"
               "nearest=abs(worldPosition.xy-round(worldPosition.xy));"
               "vec3 altitudesPx=1.0/perPixel;"
               "float typicalAltitudePx=altitudesPx.x+altitudesPx.y+altitudesPx.z-"
               "float densityFade=mix(0.30,1.0,density);"
               "wireHalfwidth=mix(0.42,constants.wireRaster.z,density*density);"
               "coverage=1.0-smoothstep(wireHalfwidth-0.45,"
               "wireOpacity=densityFade;"
               "color=mix(color,vec3(0.073239),coverage*wireOpacity);")
    string(FIND "${frag}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M67/M68 real physical pixel coverage missing: ${needle}")
    endif()
endforeach()
string(FIND "${cmake}" "set(SHADERS mesh.vert mesh.frag hud.vert hud.frag)" shaders)
if(shaders EQUAL -1)
    message(FATAL_ERROR "M69 detached edge shader remains active")
endif()
foreach(old "uploadWire(" "replaceWire(" "wirePipeline" "floorLines")
    string(FIND "${view}" "${old}" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "M69 dead wire GPU resource remains: ${old}")
    endif()
endforeach()
message(STATUS "M58 physical AA goal now implemented on depth-owning solid primitives")
