#pragma once
#include "mesher/partition_render.hpp"
#include "mesher/repair_preview.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mesher {

struct ProposedSculptStep;

// A read-only preview of a candidate that was NEVER published. The red
// topology is incident to intersecting FACES; original vertices are not
// necessarily located on the geometric triangle/triangle intersection.
struct RepairInspectionSnapshot {
    std::shared_ptr<const RegionTopologyIndex> candidate;
    std::vector<RegionTopologyIndex::CollisionPair> contacts;
    std::vector<FaceId> affectedFaces;
    std::vector<VertexId> incidentVertices;
    std::vector<RegionTopologyIndex::Edge> incidentEdges;
    RegionTopologyIndex::CollisionStatistics work{};
    RegionTopologyIndex::CollisionStatistics baselineWork{};
    std::size_t preexistingPairs=0; // not automatically a new-crossing freeze
    std::string diagnostic; // nonempty on bounded-query/incomplete evidence
    bool previewEnabled=false;
    RepairExcisionPreview excision;
};

// Thread-safe one-shot observer, called by the existing PRE-CAS sculpt
// validator. Returns a refusal only on a freeze; no worker root is published.
class RepairInspectionSession {
public:
    explicit RepairInspectionSession(bool preview=false):previewEnabled_(preview) {}
    std::string inspect(const ProposedSculptStep& proposed);
    std::shared_ptr<const RepairInspectionSnapshot> frozenSnapshot() const;
    bool frozen() const noexcept {return frozen_.load(std::memory_order_acquire);}
private:
    bool previewEnabled_=false;
    std::atomic<bool> frozen_{false};
    mutable std::mutex mutex_;
    std::shared_ptr<const RepairInspectionSnapshot> snapshot_;
};

// CPU-generated unlit colored triangular prisms/octahedra. A viewport may
// upload this once, and orbit inside/outside the frozen candidate freely.
std::vector<PartitionRenderVertex> buildRepairInspectionOverlay(
    const RepairInspectionSnapshot& snapshot);

} // namespace mesher
