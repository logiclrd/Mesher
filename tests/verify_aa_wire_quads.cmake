if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M58 MESHER_SOURCE_ROOT required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" viewport)
file(READ "${MESHER_SOURCE_ROOT}/shaders/wire.vert" vert)
file(READ "${MESHER_SOURCE_ROOT}/shaders/wire.frag" frag)
file(READ "${MESHER_SOURCE_ROOT}/include/mesher/wire_quads.hpp" cpu)
foreach(needle
    "Buffer uploadWire("
    "return upload(expandWireQuads(edges));"
    "replaceBuffer(buffer,expandWireQuads(edges));"
    "lines=uploadWire(drawing.lines);"
    "floorLines=uploadWire(scenery.lines);"
    "if(!geometry->wire.empty())dst.wire=uploadWire(geometry->wire);"
    "replaceWire(dst.wire,found->second->wire);"
    "assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;"
    "depth.depthWriteEnable=(wire||hudOverlay)?VK_FALSE:VK_TRUE;"
    "depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;"
    "if(wire||hudOverlay) {"
    "blendAttachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;"
    "result.wireRaster={float(width),float(height),kWireOuterHalfWidthPixels,0.0f};"
    "if(wireVisible_) {")
    string(FIND "${viewport}" "${needle}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "M58 real Vulkan stage lacks AA/depth/dirty-buffer property: ${needle}")
    endif()
endforeach()
foreach(needle
    "layout(location=0) in vec3 position;"
    "layout(location=1) in vec3 otherEndpoint;"
    "layout(location=2) in float cornerIndex;"
    "noperspective out vec2 aaDistance"
    "constants.viewProjection*vec4(position,1.0)"
    "constants.viewProjection*vec4(otherEndpoint,1.0)"
    "projected.xy+=perpendicular"
    "gl_Position=projected;"
    "if(start.z<0.0 && end.z<0.0)")
    string(FIND "${vert}" "${needle}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "M58 wire quad vertex shader invariant missing: ${needle}")
    endif()
endforeach()
foreach(needle
    "noperspective in vec2 aaDistance"
    "smoothstep(0.0,1.0,aaDistance.y-abs(aaDistance.x))"
    "outColor=vec4(0.073239,0.073239,0.073239,coverage);")
    string(FIND "${frag}" "${needle}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "M58 smooth alpha coverage missing: ${needle}")
    endif()
endforeach()
string(FIND "${cpu}" "constexpr std::array<unsigned,6> corners{0,1,2,0,2,3};" corners)
if(corners EQUAL -1)
    message(FATAL_ERROR "M58 CPU and Vulkan shader six-corner agreement changed")
endif()
string(REGEX MATCH "gl_Position[ \t]*[.][ \t]*z[ \t]*[-+*/]?=" bias "${vert}")
if(bias)
    message(FATAL_ERROR "M58 must never reintroduce M56's hidden-edge depth-bias regression")
endif()
message(STATUS "M58 Vulkan depth-correct AA wire triangles / CPU expansion source contract verified")
