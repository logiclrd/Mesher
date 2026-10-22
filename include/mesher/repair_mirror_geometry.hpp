#pragma once
#include "mesher/versioned_mesh.hpp"

#include <cstddef>
#include <memory>

namespace mesher {

// STRICT diagnostic mirror-geometry audit of an already closed private
// revision. This is NOT a substitute for V17/R07's authoritative living
// mirror-ID journal. Never perform coordinate welding or infer topology
// when reflected positions are ambiguous; refuse instead.
struct GeometricMirrorEvidence {
    std::size_t vertices{};
    std::size_t faces{};
    std::size_t seamVertices{};
    std::size_t pairedVertices{};
    std::size_t pairedFaces{};
};
GeometricMirrorEvidence auditPrivateReflectedGeometry(
    const RegionTopologyIndex& mesh,
    std::size_t maxVertices=1000000,
    std::size_t maxFaces=1000000);
} // namespace mesher
