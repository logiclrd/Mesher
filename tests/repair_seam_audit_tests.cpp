#include "mesher/repair_seam_audit.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
struct TestMesh {
    std::shared_ptr<const RegionTopologyIndex> index;
    FaceId newFace{};
};
TestMesh testFixture(bool tJunction=false,bool crossing=false) {
    Mesh m;
    m.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    m.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    if(tJunction) {
        // Distinct gray vertex in the INTERIOR of green edge 0-1.
        // The gray triangle is deliberately disconnected and invalid
        // to prove the audit detects geometrically false sewing.
        m.vertices.push_back({1,0,0});
        m.vertices.push_back({1,-.6,0});
        m.vertices.push_back({1.1,-.7,0});
        m.triangles.push_back({4,5,6});
    }
    if(crossing) {
        // Gray triangle has an edge crossing the generated (0,2)-(2,0)
        // cap edge at (1.6,0.4,0), with NO common VertexIds.
        const auto off=static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({.25,.4,0});
        m.vertices.push_back({1.9,.4,0});
        m.vertices.push_back({1.7,.7,0});
        m.triangles.push_back({off,off+1,off+2});
    }
    const auto imported=partitionFlatMesh(m,64);
    return {RegionTopologyIndex::build(imported.revision),
            imported.flatIndex.faceIds.front()};
}
void testClosedTetra() {
    auto t=testFixture();
    auto audit=auditPrivateRepairSeam(*t.index,{t.newFace});
    require(audit.complete&&audit.interfaceEdges.size()==3,
            "M13 tetra must have three exact generated-survivor interface edges");
    require(audit.interfaceIncidenceErrors==0&&
            audit.reversedOrientationErrors==0&&
            audit.tJunctionCount==0&&audit.crossingEdgePairs==0&&
            audit.disconnectedCoincidentVertices==0,
            "M13 clean manifold seam must have no topological or geometric edge issues");
    for(const auto& seam:audit.interfaceEdges) {
        require(seam.generatedFace==t.newFace&&
                seam.survivorFace!=t.newFace&&
                seam.generatedForward!=seam.survivorForward,
                "M13 must preserve both distinct stable FaceIds and opposite edge orientation");
    }
    std::ostringstream out;
    printRepairSeamAudit(out,*t.index,audit,"SYNTHETIC");
    require(out.str().find("full_triangle_collision_certificate=0")!=std::string::npos&&
            out.str().find("xyz_a=")!=std::string::npos&&
            out.str().find("new_face=")!=std::string::npos,
            "M13 output must identify limits, coordinates and both FaceIds");
}
void testFalseSewing() {
    auto t=testFixture(true,true);
    auto audit=auditPrivateRepairSeam(*t.index,{t.newFace});
    require(audit.complete&&audit.tJunctionCount>=1,
            "M13 geom T-junction missed gray vertex interior to new cap edge");
    require(audit.crossingEdgePairs>=1,
            "M13 geom crossing missed non-incident survivor/cap edges");
    const auto bounded=auditPrivateRepairSeam(
        *t.index,{t.newFace},
        RepairSeamAuditLimits{.maxFaceEdges=250000,
                             .maxGeometricPairs=1,
                             .maxStoredIssues=2,
                             .relativeTolerance=1e-9});
    require(!bounded.complete&&bounded.examinedGeometricPairs>1,
            "M13 finite work exhaustion must not claim complete investigation");
}
}
int main(){
    try {
        testClosedTetra();testFalseSewing();
        std::cout<<"[r06-m13] exact seam FaceId/winding, geometric T and crossing, bounded evidence PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06 M13 seam audit FAILED: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
