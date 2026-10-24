#pragma once
#include "mesher/repair_seam_audit.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace mesher {

// R06-M14D: explanation of opt-in M13 witnesses ONLY. The definitive
// acceptance gate remains the consolidated revision's manifold validator.
// A crossing or coincident XYZ is never proof of an intended attachment.
struct RepairWitnessSourceEdge {
    VertexId a{},b{};
    // In the immutable post-excision R03 source, 1 = an open rim;
    // 2 = ordinary interior edge; 0 = edge absent from R03.
    std::size_t sourceSides{};
    std::size_t candidateSides{};
    std::vector<FaceId> sourceFaces;
};
struct RepairWitnessProvenance {
    std::string kind;
    VertexId vertex{},otherVertex{};
    // A vertex referenced by a new cap face may still be an ORIGINAL R03
    // rim vertex, rather than a newly allocated repair vertex.
    bool vertexInSource=false;
    bool vertexIntroduced=false;
    bool otherInSource=false;
    bool otherIntroduced=false;
    bool indexedTopologyWitness=false;
    RepairWitnessSourceEdge newEdge;
    RepairWitnessSourceEdge survivorEdge;
    std::size_t repeats=1; // same exact ID witness deduplicated
};
struct RepairSeamProvenanceReport {
    std::vector<RepairWitnessProvenance> witnesses;
    std::size_t inputStoredIssues{};
    std::size_t uniqueWitnesses{};
    std::size_t repeatedWitnesses{};
    bool complete=false; // only as complete as the bounded original M13 audit
    bool storedOnly=true; // never claims an exhaustive geometric certificate
};

RepairSeamProvenanceReport explainRepairSeamWitnesses(
    const RegionTopologyIndex& sourceR03,
    const RegionTopologyIndex& consolidatedPrivate,
    const RepairSeamAudit& audit,
    const std::vector<VertexId>& introducedVertices={});

void printRepairSeamProvenance(
    std::ostream& out,const RepairSeamProvenanceReport& report);

} // namespace mesher
