#pragma once
#include "mesher/repair_preview.hpp"
#include <memory>
#include <cstddef>

namespace mesher {

// R03 deliberately creates a private OPEN mesh. Do not use RegionEdit's
// production commit: its closed-two-manifold invariant must remain intact.
// This independent debug revision NEVER participates in CAS, history, save,
// checkpoints, or the editable worker state.
struct PrivateExcisionResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::size_t removedVertices{};
    std::size_t removedFaces{};
    std::size_t modifiedPartitions{};
    std::size_t survivingFaces{};
    std::size_t boundaryEdges{};
    std::size_t boundaryLoops{};
};

struct PrivateExcisionLimits {
    std::size_t maxVertices=768;
    std::size_t maxFaces=4096;
    std::size_t maxModifiedPartitions=1024;
    std::size_t maxSurvivingFaces=1000000; // one debug-only topology audit
};

// Fail closed if the plan is incomplete, stale, not vertex-star complete,
// has orphan references, or creates ANY boundary other than R02's loops.
// The original index and private post-sculpt candidate are never changed.
PrivateExcisionResult applyPrivateExcision(
    std::shared_ptr<const RegionTopologyIndex> candidate,
    const RepairExcisionPreview& proposal,
    const PrivateExcisionLimits& limits={});

} // namespace mesher
