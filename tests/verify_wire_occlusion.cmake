# M56 stronger condition: the wire is NOT a separately displaced
# fragment. Its owning visible solid face keeps its true clip depth.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M56 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.vert" vert)
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.frag" frag)
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" view)
string(FIND "${vert}" "gl_Position=constants.viewProjection*vec4(position,1.0);" projection)
string(REGEX MATCH "gl_Position[ \\t]*[.][ \\t]*z[ \\t]*[-+*/]?=" bias "${vert}")
if(projection EQUAL -1 OR bias)
    message(FATAL_ERROR "M56 original owning face depth biased or replaced")
endif()
foreach(needle "depth.depthTestEnable=hudOverlay?VK_FALSE:VK_TRUE;"
               "depth.depthWriteEnable=(hudOverlay||paintOverlay)?VK_FALSE:VK_TRUE;"
               "depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;"
               "raster.cullMode=VK_CULL_MODE_NONE;"
               "constants.wireRaster[3]=wireVisible_?1.0f:0.0f;")
    string(FIND "${view}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M56 true solid depth and W guard missing: ${needle}")
    endif()
endforeach()
# Hover is translucent only in a separate pipeline, never by weakening
# the source mesh's depth ownership or its W-controlled wire coverage.
foreach(needle
    "paintPreviewPipeline=makePipeline(false,true);"
    "if(hudOverlay||paintOverlay)"
    "depth.depthTestEnable=hudOverlay?VK_FALSE:VK_TRUE;")
    string(FIND "${view}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "UX04 preview cannot replace opaque owning mesh depth: ${needle}")
    endif()
endforeach()
foreach(needle "if(constants.wireRaster.w>0.5)" "if(surfaceMaterial<0.5)"
               "nearest=abs(worldPosition.xy-round(worldPosition.xy));")
    string(FIND "${frag}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M56 wire ink no longer on actual depth-visible surface")
    endif()
endforeach()
message(STATUS "M56 no leak from hidden edges: ink belongs to depth-winning solid")
