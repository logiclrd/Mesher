#pragma once

#include "mesher/region_edit.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mesher {

// V18-M01: read-only candidate-vs-before contact ledger. This is NOT
// the final V18 clearance certificate or a live sculpt publication gate.
// Compare the complete set of relevant post-state BVH contacts, not each
// sheet against the other sheet's stale geometry. Never infer owner IDs
// from equal world positions or authorize a Boolean weld.
enum class OffModeContactStatus {
    Clean,                 // no query-visible candidate contact
    UnchangedPreexisting,  // only proven exactly unchanged old contacts
    NewContact,            // at least one new definite contact
    Uncertain              // ambiguous, changed old contact, bad input/budget
};

struct OffModeContactLimits {
    // Each query (before and candidate) must complete independently
    // under these six finite constraints; limits never mean truncation.
    CollisionQueryLimits query{
        4096,131072,32768,65536,32768,4096
    };
    std::size_t maxChangedInputRecords=16384;
};

struct OffModeContactReport {
    OffModeContactStatus status{OffModeContactStatus::Uncertain};
    RegionTopologyIndex::CollisionStatistics beforeWork{},candidateWork{};
    // Canonical stable FaceId pairs. "Retained" means BOTH actual faces
    // and all six endpoint positions are byte-value equal across roots.
    std::vector<RegionTopologyIndex::CollisionPair> introduced;
    std::vector<RegionTopologyIndex::CollisionPair> retained;
    std::vector<RegionTopologyIndex::CollisionPair> uncertain;
    std::size_t resolvedOldContacts{};
    std::string reason;
    bool conservativelyContactFree() const noexcept {
        // A later V18 gate must ALSO check mesh-local clearance,
        // complete dirty coverage and valid tangential/normal motion.
        return status==OffModeContactStatus::Clean||
               status==OffModeContactStatus::UnchangedPreexisting;
    }
};

// "changedVertices"/"changedFaces" are the complete net local dirty
// evidence across the stage, INCLUDING original retired IDs and new IDs.
// Unknown IDs, a missing scope for distinct roots, ambiguous results and
// exhausted work are reported as Uncertain, never as accepted Clean.
// The function never edits either immutable root and never publishes.
// A pre-existing contact is grandfathered ONLY when both entire faces
// have unchanged stable corner IDs and exact coordinates. Changed
// pre-existing contact is Uncertain until later V18 peers establish a
// separate no-worsening/clearance proof. Unrelated old crossings outside
// the changed neighborhood are intentionally not globally inspected.
OffModeContactReport assessOffModePostStateContacts(
    const std::shared_ptr<const RegionTopologyIndex>& before,
    const std::shared_ptr<const RegionTopologyIndex>& candidate,
    std::span<const VertexId> changedVertices,
    std::span<const FaceId> changedFaces,
    const OffModeContactLimits& limits={});

} // namespace mesher
