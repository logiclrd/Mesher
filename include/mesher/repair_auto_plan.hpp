#pragma once

#include "mesher/repair_survivors.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mesher {

// R06-M03 only chooses closure topology. It NEVER edits the private R03 cut,
// publishes a worker revision, or certifies M04's actual cap/zipper geometry.
// Contact pairs are the complete trusted R01 pairs for the same ORIGINAL
// candidate from which R02/R03 excised the selected faces.
struct AutoClosurePlanSettings {
    std::size_t maxExcisionFaces=4096;
    std::size_t maxContours=16;
    std::size_t maxContourVertices=512;
    std::size_t maxContactPairs=8192;
    std::size_t maxRegions=32;
    std::size_t maxLoopsPerRegion=4; // no invented 3-vs-5 matching
    std::size_t maxAuditedFaces=1000000;
    std::size_t maxAuditedEdges=3000000;
    // A true zipper must be materially cheaper than two independent disks;
    // retaining/adding a handle on one survivor requires stronger evidence.
    double minimumZipperSavings=0.20;
    double minimumSameOwnerZipperSavings=0.55;
    RepairPlanSettings fitness{};
};

enum class AutoClosurePlanStatus {
    Ready, InvalidInput, BudgetExceeded, AmbiguousRegion, AmbiguousFitness
};

struct AutoClosureRegion {
    std::vector<std::size_t> contours; // global R02 contour indices
    std::vector<std::size_t> excisionPatches;
    std::vector<std::size_t> caps; // global contour indices
    std::vector<RepairContourPair> zippers; // global contour indices
    std::size_t contactLinks{};
    double estimatedAddedArea{};
    bool zeroLoopComponentRetirement{}; // NOT deletion authority: R06-M04
};

struct AutoClosurePlan {
    AutoClosurePlanStatus status{AutoClosurePlanStatus::InvalidInput};
    std::string diagnostic;
    RepairSurvivorInventory survivors;
    std::vector<AutoClosureRegion> regions;
    std::vector<std::size_t> cappedContours; // all-or-nothing flattened plan
    std::vector<RepairContourPair> zipperPairs;
    std::size_t excisionPatches{};
    std::size_t trustedContactLinks{};
    std::size_t geometryRegions{};
    double estimatedAddedArea{};
    bool ready() const noexcept {return status==AutoClosurePlanStatus::Ready;}
};

// Provenance contract: sourceCandidate is the precise pre-R03 immutable
// revision; cut and proposal must agree on all stable face/edge IDs.
// A trusted R01 contact connects excision patches only when BOTH of its
// actual face IDs were removed. No proximity-only nearest-loop pairing.
// Each R02 loop must have ONE R03 survivor owner and ONE excision-patch owner.
// Disjoint contact islands never compete globally for closure pairings.
// Unsupported many-to-many / fitness ties refuse the WHOLE plan.
AutoClosurePlan planPrivateAutomaticClosure(
    const RegionTopologyIndex& sourceCandidate,
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const std::vector<RegionTopologyIndex::CollisionPair>& trustedContacts,
    const AutoClosurePlanSettings& limits={});

} // namespace mesher
