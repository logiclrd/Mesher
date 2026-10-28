#pragma once

#include "mesher/repair_excision.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mesher {

// R06-M01: read-only combinatorial evidence from the ACTUAL R03 cut.
// This inventory is deliberately not a cap/zipper or cavity deletion policy.
struct RepairSurvivorInventoryLimits {
    std::size_t maxFaces=1000000;
    std::size_t maxEdges=3000000;
    std::size_t maxBoundaryLoops=64;
};

struct RepairSurvivorComponent {
    std::vector<FaceId> faces;
    std::vector<VertexId> vertices;
    std::vector<std::size_t> contourIndices;
    std::size_t edges{};
    std::size_t boundaryEdges{};
    std::int64_t eulerCharacteristic{};
    bool closed{};
};

struct RepairSurvivorInventory {
    std::vector<RepairSurvivorComponent> components;
    std::size_t faces{};
    std::size_t edges{};
    std::size_t boundaryEdges{};
    std::size_t boundaryLoops{};
};

// A component is connected through triangle edges, never by XYZ coincidence.
// Every directed contour edge must occur ONCE as a real, correctly wound,
// one-sided R03 edge owned by precisely one component and surviving FaceId.
// A disconnected vertex fan, misleading loop association or finite-limit
// exhaustion refuses the whole analysis. This changes no revision or ledger.
RepairSurvivorInventory inventoryPrivateExcision(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const RepairSurvivorInventoryLimits& limits={});

} // namespace mesher
