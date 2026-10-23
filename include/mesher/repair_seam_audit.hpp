#pragma once
#include "mesher/repair_preview.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace mesher {

// This is a diagnostic of the actual frozen, unpublished R06 geometry.
// It is NOT a replacement for exact manifold or triangle-contact validation.
struct RepairSeamEdge {
    VertexId a{},b{};
    FaceId generatedFace{},survivorFace{};
    bool generatedForward=false,survivorForward=false;
};
struct RepairSeamIssue {
    std::string kind;
    VertexId vertex{},otherVertex{};
    VertexId generatedA{},generatedB{},survivorA{},survivorB{};
    Vec3 where{};
    double distance{};
};
struct RepairSeamAuditLimits {
    std::size_t maxFaceEdges=250000;
    std::size_t maxGeometricPairs=30000000;
    std::size_t maxStoredIssues=256;
    double relativeTolerance=1e-9;
};
struct RepairSeamAudit {
    std::vector<RepairSeamEdge> interfaceEdges;
    std::vector<RepairSeamIssue> issues;
    std::size_t interfaceIncidenceErrors=0;
    std::size_t reversedOrientationErrors=0;
    std::size_t tJunctionCount=0;
    std::size_t disconnectedCoincidentVertices=0;
    std::size_t crossingEdgePairs=0;
    std::size_t examinedGeometricPairs=0;
    std::size_t candidateGeometricPairs=0;
    std::size_t totalIssues=0;
    bool complete=true;
    bool issueListTruncated=false;
    // A COMPLETE audit exhausts its specified vertex/edge and edge/edge
    // tests only. It is never a global triangle-triangle collision certificate.
};

RepairSeamAudit auditPrivateRepairSeam(
    const RegionTopologyIndex& mesh,
    const std::vector<FaceId>& generatedFaces,
    RepairSeamAuditLimits limits={});

void printRepairSeamAudit(
    std::ostream& out,const RegionTopologyIndex& mesh,
    const RepairSeamAudit& audit,
    const char* mode="UNTRUSTED_PRIVATE");
} // namespace mesher
