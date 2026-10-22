#pragma once

#include "mesher/repair_enclosure.hpp"
#include "mesher/repair_mixed_closure.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace mesher {

struct CertifiedPrivateAutoLimits {
    MixedPrivateClosureLimits closure{};
    EnclosedComponentLimits material{};
    std::size_t maxRetiredCavityFaces=20000;
    std::size_t maxRetiredCavityVertices=20000;
    std::size_t maxRetiredCavityComponents=16;
};

struct CertifiedPrivateAutoResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<FaceId> greenFaces; // surviving caps only
    std::vector<FaceId> blueFaces; // surviving seam only
    std::vector<FaceId> retiredCavityFaces;
    std::vector<VertexId> retiredCavityVertices;
    std::size_t retiredCavityComponents{};
    std::size_t acceptedExteriorComponents{};
    std::size_t postClosureFaces{};
    std::size_t postRetirementFaces{};
    // M05 only: forward real M04B topology-quality and M02 bounded
    // enclosure-work evidence to frozen diagnostics, no changes to policy.
    std::size_t generatedFaces{};
    std::size_t insertedVertices{};
    std::size_t refinementSteps{};
    double minimumCapQuality{1};
    double minimumZipperQuality{1};
    std::size_t spatialCapLoops{};
    std::size_t spatialPoissonIterations{};
    double minimumSpatialInitialQuality{1};
    std::size_t windingTriangleVisits{};
    std::size_t collisionNarrowPhaseTests{};
};

// Entire result is still PRIVATE and unpublishable by R06 diagnostics.
// Reverify M03's choice and an R03 exact-cut fingerprint, then:
// 1) privately stage+refine mixed geometry and audit closed manifold,
// 2) use M02 bounded material evidence on the actual closed result,
// 3) retire ONLY whole reliably enclosed inward components, re-audit,
// 4) reject zero-loop excision of a nonredundant original closed shell.
// All errors leave the worker, source candidate and private R03 root alone.
CertifiedPrivateAutoResult applyCertifiedPrivateAutomaticClosure(
    std::shared_ptr<const RegionTopologyIndex> sourceCandidate,
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const std::vector<RegionTopologyIndex::CollisionPair>& trustedContacts,
    const CertifiedPrivateAutoLimits& limits={});

} // namespace mesher
