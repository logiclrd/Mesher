#pragma once

#include "mesher/offmode_collision.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>

namespace mesher {

// V18-M03: local-mesh-scale *static endpoint* clearance, not CCD,
// Boolean repair, or V18-M04's constrained normal displacement.
enum class OffModeClearanceStatus {
    Clear,            // candidate has no near independent source pair
    TangentialSlide,  // changed stable independent sheets preserve prior gap
    UnchangedOld,     // strictly unchanged original contact/proximity only
    TooClose,         // new/worsened clearance shortfall
    Contact,          // M01 proves new improper nonadjacent intersection
    Uncertain         // ambiguous contact, topology, numeric or finite work
};

struct OffModeClearanceLimits {
    OffModeContactLimits contacts{};
    // Fraction of min(shortest triangle edge on EACH face). Never brush
    // radius, global mesh bbox, or max coarse face edge.
    double minimumSpacingRatio=0.05;
    RegionTopologyIndex::CollisionQueryLimits neighborhood{
        4096,131072,32768,65536,32768,4096
    };
    std::size_t maxDistanceTests=8192;
};

struct OffModeClearanceReport {
    OffModeClearanceStatus status{OffModeClearanceStatus::Uncertain};
    OffModeContactReport contact{};
    RegionTopologyIndex::CollisionStatistics neighborhoodWork{};
    std::size_t distanceTests{};
    std::size_t examinedNearPairs{};
    std::size_t unchangedHistoricalPairs{};
    std::size_t tangentialPairs{};
    double smallestDistance=std::numeric_limits<double>::infinity();
    double largestRequiredSpacing=0;
    std::string reason;
    bool allowed() const noexcept {
        return status==OffModeClearanceStatus::Clear||
               status==OffModeClearanceStatus::TangentialSlide||
               status==OffModeClearanceStatus::UnchangedOld;
    }
};

// Reads both immutable roots and only the caller's complete local dirty
// VertexId/FaceId scope. Its M01 step must first be fully admissible.
// For previously too-close pairs, motion is permitted ONLY on unchanged
// stable face topology, with nondecreasing exact triangle distance and
// both sheets' relative translation normal components provably zero.
// An unrelated old crossing outside the dirty neighborhood is neither
// enumerated nor cleaned. Extremely near, degenerate, zero-gap,
// nonplanar tangencies and unsupported topology are conservative.
OffModeClearanceReport assessOffModeMeshClearance(
    const std::shared_ptr<const RegionTopologyIndex>& before,
    const std::shared_ptr<const RegionTopologyIndex>& candidate,
    std::span<const VertexId> changedVertices,
    std::span<const FaceId> changedFaces,
    const OffModeClearanceLimits& limits={});

} // namespace mesher
