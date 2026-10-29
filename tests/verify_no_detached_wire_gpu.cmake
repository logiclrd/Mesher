# M69 no old detached GPU wire line resources. CPU canonical topology
# wire geometry remains available for source-certified manifold audits.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M69 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" view)
file(READ "${MESHER_SOURCE_ROOT}/CMakeLists.txt" cmake)
file(READ "${MESHER_SOURCE_ROOT}/src/partition_render.cpp" source)
foreach(needle
    "struct PartitionBuffers {Buffer solid;};"
    "Buffer solid{},floorSolid{},hud{};"
    "solidPipeline=makePipeline();"
    "hudPipeline=makePipeline(true);"
    "if(!drawing.solid.empty())solid=upload(drawing.solid);"
    "if(!geometry->solid.empty())dst.solid=upload(geometry->solid);"
    "replaceBuffer(dst.solid,found->second->solid);"
    "retireGpuBuffer(old->second.solid);"
    "destroyBuffer(floorSolid);"
    "depth.depthWriteEnable=(hudOverlay||paintOverlay)?VK_FALSE:VK_TRUE;")
    string(FIND "${view}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M69 missing exact GPU solid-only cache/lifetime contract: ${needle}")
    endif()
endforeach()
foreach(dead "wirePipeline" "floorLines" "uploadWire(" "replaceWire("
             "buffers.wire" "dst.wire" "drawing.lines" "scenery.lines"
             "expandWireQuads(" "VK_CULL_MODE_BACK_BIT")
    string(FIND "${view}" "${dead}" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "M69 retired GPU edge resources/culling reappeared: ${dead}")
    endif()
endforeach()
string(FIND "${cmake}" "set(SHADERS mesh.vert mesh.frag hud.vert hud.frag)" compiled)
string(FIND "${source}" "out.wire.push_back" cpu)
if(compiled EQUAL -1 OR cpu EQUAL -1)
    message(FATAL_ERROR "M69 GPU-only removal cannot retire immutable CPU wire auditors")
endif()
message(STATUS "M69 no old GPU wire allocations/pipeline; original CPU edge audits preserved")
