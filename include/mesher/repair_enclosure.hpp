#pragma once

#include "mesher/repair_survivors.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace mesher {

// R06-M02 is read-only. A closed inward shell which is provably enclosed
// by an exterior shell is redundant: Mesher has NO intentional sealed voids.
// A free-standing reversed shell is NOT proof of an enclosed cavity.
enum class EnclosedComponentKind {Exterior,RedundantEnclosedCavity,Ambiguous};

struct EnclosedComponentLimits {
    std::size_t maxFaces=1000000;
    std::size_t maxEdges=3000000;
    std::size_t maxComponents=128;
    std::size_t maxWindingTriangleVisits=250000;
    std::size_t maxCollisionSeedFaces=16384;
    std::size_t maxCollisionBoxTests=16777216;
    std::size_t maxCollisionPartitionTests=2097152;
    std::size_t maxCollisionCandidateTriangles=2000000;
    std::size_t maxCollisionNarrowPhaseTests=1000000;
    std::size_t maxCollisionReportedContacts=1024;
    double minimumRelativeVolume=1e-9;
    double windingTolerance=0.02;
};

struct EnclosedComponentEvidence {
    EnclosedComponentKind kind{EnclosedComponentKind::Ambiguous};
    // Index into the matching RepairSurvivorInventory::components vector.
    std::size_t component{};
    std::size_t enclosingComponent{std::numeric_limits<std::size_t>::max()};
    double signedVolume{};
    std::size_t witnessPoints{};
    std::string reason;
};
struct EnclosedComponentReport {
    RepairSurvivorInventory inventory;
    std::vector<EnclosedComponentEvidence> components;
    std::size_t windingTriangleVisits{};
    RegionTopologyIndex::CollisionStatistics collisionWork{};
};

// Requires an immutable fully CLOSED orientable revision. Any component
// with an incomplete vertex fan, reversed edge pair, open rim, insufficient
// work budget or uncertain material classification fails closed: callers
// must NOT retire such a component. The original root/IDs remain untouched.
// RedundantEnclosedCavity is a read-only deletion CANDIDATE, not a mutation.
EnclosedComponentReport inspectEnclosedComponents(
    std::shared_ptr<const RegionTopologyIndex> closed,
    const EnclosedComponentLimits& limits={});

} // namespace mesher
