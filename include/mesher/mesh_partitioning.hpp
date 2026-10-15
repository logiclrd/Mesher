#pragma once

#include "mesher/versioned_mesh.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace mesher {

// The flat ordering is a compatibility/export concern, never element identity.
struct FlatMeshIndex {
    std::vector<VertexId> vertexIds;
    std::vector<FaceId> faceIds;
};
struct PartitionedMeshImport {
    std::shared_ptr<const MeshRevision> revision;
    FlatMeshIndex flatIndex;
};

// One-time O(N log N) Morton sort. The vertex and face streams are grouped
// independently but use the same partition IDs: every partition holds at most
// maxElementsPerPartition vertices AND at most that many faces.
// Face vertices refer to global stable IDs, including across partitions.
PartitionedMeshImport partitionFlatMesh(
    const Mesh& flat, std::size_t maxElementsPerPartition = 256);

// Full-graph validation for import/export/tests; does NOT establish manifoldness.
void validatePartitionedMesh(const MeshRevision& revision);

// Exact flat positions, vertex/face ordering, orientation and shared indices.
// Rejects missing, duplicated, or extra elements instead of omitting edits.
Mesh reconstructFlatMesh(const MeshRevision& revision,
                         const FlatMeshIndex& originalOrder);

} // namespace mesher
