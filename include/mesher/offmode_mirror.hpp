#pragma once

#include "mesher/region_edit.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>

namespace mesher {

// V18-M05: immutable stable-identity mirror audit for OFF-mode sculpt.
// NOT an M29 first-original authorization, not a way to enable V17 Merge.
struct OffModeMirrorAuditLimits {
    std::size_t maxVertices=32768;
    std::size_t maxFaces=65536;
};
struct OffModeMirrorAudit {
    bool complete=false;
    std::size_t verticesChecked{};
    std::size_t facesChecked{};
    std::size_t fixedPlaneVertices{};
    std::string refusal;
};

// A complete source admission (fullSource=true) checks all living
// vertices/faces at session construction, with finite upfront caps.
// Each terminal private candidate thereafter checks ONLY the changed
// stable-ID neighborhood and both mirror mates' incident face stars,
// never a global renderer or root-flatten scan. All matches use explicit
// reciprocal stable IDs, with reversed oriented triangle reflection.
// Coincident spatial coordinates confer no ownership.
OffModeMirrorAudit auditOffModeMirrors(
    const std::shared_ptr<const RegionTopologyIndex>& root,
    const std::unordered_map<VertexId,VertexId>& mirrors,
    std::span<const VertexId> changedVertices={},
    std::span<const FaceId> changedFaces={},
    bool fullSource=false,
    const OffModeMirrorAuditLimits& limits={});

} // namespace mesher
