# M59: GPU-less regression of indexed-COW startup with NO flat legacy
# vertices. An optional empty Buffer must never be uploaded or bound;
# actual immutable partition buffers still initialize.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M59 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" viewport)
foreach(required
    "auto drawing=buildDrawing(displayedIndex_?empty:mesh_);"
    "if(!drawing.solid.empty())solid=upload(drawing.solid);"
    "if(!drawing.lines.empty())lines=upload(drawing.lines);"
    "if(displayedIndex_)initializePartitions();"
    "if(solid.vertexCount) {"
    "if(lines.vertexCount) {"
    "if(!geometry->solid.empty())dst.solid=upload(geometry->solid);"
    "if(!geometry->wire.empty())dst.wire=upload(geometry->wire);"
    "if(vertices.empty())throw std::runtime_error(\"Empty vertex buffer\");")
    string(FIND "${viewport}" "${required}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M59 guarded optional legacy/COW vertex contract missing: ${required}")
    endif()
endforeach()
# An empty COW legacy buffer must never be masked with a dummy triangle:
# the renderer must still derive its visible model from indexed partitions.
string(FIND "${viewport}" "if(displayedIndex_)for(const auto& [id,buffers]:partitionBuffers_)" partitionDraw)
if(partitionDraw EQUAL -1)
    message(FATAL_ERROR "M59 immutable partition draw path has disappeared")
endif()
message(STATUS "M59 COW empty legacy arrays never trigger upload or null VkBuffer draw")
