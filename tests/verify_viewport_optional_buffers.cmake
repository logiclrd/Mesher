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
    "if(displayedIndex_)initializePartitions();"
    "constants.wireRaster[3]=wireVisible_?1.0f:0.0f;"
    "if(!geometry->solid.empty())dst.solid=upload(geometry->solid);"
    "struct PartitionBuffers {Buffer solid;};"
    "if(vertices.empty())throw std::runtime_error(\"Empty vertex buffer\");")
    string(FIND "${viewport}" "${required}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M59 guarded optional legacy/COW vertex contract missing: ${required}")
    endif()
endforeach()
# An empty COW legacy buffer must never be masked with a dummy triangle:
# the renderer must still derive its visible model from indexed partitions.
# Legacy flat vertices remain guard-protected in BOTH normal drawing and
# M16D's exact-staging replacement scene. A private staged draw must suppress
# the stale candidate's old solid/partition buffers, not upload dummy faces.
string(FIND "${viewport}" "if(solid.vertexCount) {" classicSolid)
string(FIND "${viewport}" "if(!inspectionStepReplaceMesh_&&solid.vertexCount) {" steppedSolid)
if(classicSolid EQUAL -1 AND steppedSolid EQUAL -1)
    message(FATAL_ERROR "M59 optional empty solid must never be bound")
endif()
string(FIND "${viewport}" "if(displayedIndex_)for(const auto& [id,buffers]:partitionBuffers_)" classicPartition)
string(FIND "${viewport}" "if(!inspectionStepReplaceMesh_&&displayedIndex_)" steppedPartition)
string(FIND "${viewport}" "for(const auto& [id,buffers]:partitionBuffers_)" partitionLoop)
if(classicPartition EQUAL -1 AND
   (steppedPartition EQUAL -1 OR partitionLoop EQUAL -1))
    message(FATAL_ERROR "M59 immutable partition draw path has disappeared")
endif()
message(STATUS "M59 COW empty legacy arrays never trigger upload or null VkBuffer draw")
