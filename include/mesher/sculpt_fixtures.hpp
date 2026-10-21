#pragma once
#include "mesher/mesh.hpp"

#include <cstdint>

namespace mesher {

// Explicit developer geometry for testing topology-changing sculpt edits.
// Both meshes are generated as complete closed manifold surfaces, independently
// of any Merge or collision inspection feature.
Mesh generateTorusFixture(std::uint32_t vertices=5000,
                          std::uint64_t seed=1,
                          std::uint32_t relaxationIterations=65);
Mesh generateHollowSphereFixture(std::uint32_t sphereVertices=5000,
                                 std::uint64_t seed=1,
                                 std::uint32_t relaxationIterations=65);

// Validate the oriented triangulation and the expected topological type.
// Checks edge incidences, connected vertex links, components and V-E+F.
struct FixtureTopology {
    std::size_t vertices{};
    std::size_t edges{};
    std::size_t faces{};
    std::size_t components{};
    std::int64_t eulerCharacteristic{};
};
FixtureTopology auditFixtureTopology(const Mesh& mesh);
}
