# M62 supplement: real Vulkan AA parameters agree with CPU reference.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M62 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" viewport)
file(READ "${MESHER_SOURCE_ROOT}/include/mesher/wire_appearance.hpp" ink)
file(READ "${MESHER_SOURCE_ROOT}/shaders/wire.frag" fragment)
file(READ "${MESHER_SOURCE_ROOT}/shaders/wire.vert" vertex)
foreach(needle
    "kWireOuterHalfWidthPixels=1.5f"
    "t*t*(3.0f-2.0f*t)")
    string(FIND "${ink}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "M62 CPU pixel ink contract missing: ${needle}")
    endif()
endforeach()
foreach(needle
    "include/mesher/wire_appearance.hpp"
    "result.wireRaster={float(width),float(height),kWireOuterHalfWidthPixels,0.0f};"
    "depth.depthWriteEnable=(wire||hudOverlay)?VK_FALSE:VK_TRUE;"
    "depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;"
    "if(wireVisible_&&floorVisible_) {"
    "constants.wireRaster[3]=wireVisible_?1.0f:0.0f;"
    "if(floorVisible_) {")
    # First include guard is in the actual source as \"mesher/wire_appearance.hpp\".
    if(needle STREQUAL "include/mesher/wire_appearance.hpp")
        set(needle "mesher/wire_appearance.hpp")
    endif()
    string(FIND "${viewport}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "M62 Vulkan pixel/depth visibility contract missing: ${needle}")
    endif()
endforeach()
string(FIND "${fragment}" "smoothstep(0.0,1.0,aaDistance.y-abs(aaDistance.x))" smooth)
string(FIND "${vertex}" "gl_Position=projected;" unbiased)
if(smooth EQUAL -1 OR unbiased EQUAL -1)
    message(FATAL_ERROR "M62 shader coverage/unaltered clip Z contract missing")
endif()
string(REGEX MATCH "gl_Position[ \\t]*[.][ \\t]*z[ \\t]*[-+*/]?=" bias "${vertex}")
if(bias)
    message(FATAL_ERROR "M62 may not reintroduce a hidden-rear-wire depth bias")
endif()
message(STATUS "M62 physical pixel stroke, symmetric ink and read-only LEQUAL depth verified")
