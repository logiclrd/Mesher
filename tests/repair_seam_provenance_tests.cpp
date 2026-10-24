#include "mesher/repair_seam_provenance.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace mesher;
namespace {
void require(bool yes,const char* what) {
    if(!yes)throw std::runtime_error(what);
}
std::shared_ptr<const RegionTopologyIndex> build(const Mesh& mesh) {
    return RegionTopologyIndex::build(partitionFlatMesh(mesh,64).revision);
}
Mesh tetra() {
    Mesh m;
    m.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    m.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    return m;
}
void testClosedBaselineAndNewContact() {
    auto sourceMesh=tetra();
    const auto source=build(sourceMesh);
    // New patch vertex occupies the interior of the SOURCE edge 0-1;
    // the source edge has two incident faces, neither of which is
    // subdivided. These are disjoint geometric interfaces, not
    // permission to weld distinct IDs.
    Mesh candidateMesh=sourceMesh;
    candidateMesh.vertices.push_back({1,0,0});
    candidateMesh.vertices.push_back({1,-.5,.2});
    candidateMesh.vertices.push_back({1.2,-.7,.2});
    candidateMesh.triangles.push_back({4,5,6});
    const auto imported=partitionFlatMesh(candidateMesh,64);
    const auto candidate=RegionTopologyIndex::build(imported.revision);
    const auto newVertex=imported.flatIndex.vertexIds[4];
    const auto addedFace=imported.flatIndex.faceIds.back();
    const auto seam=auditPrivateRepairSeam(*candidate,{addedFace});
    require(seam.tJunctionCount>=1,"M14D fixture must contain M13 point-on-survivor-edge witness");
    const auto explanation=explainRepairSeamWitnesses(*source,*candidate,seam);
    bool found=false;
    for(const auto& witness:explanation.witnesses)
        if(witness.kind=="NEW_VERTEX_INSIDE_SURVIVOR_EDGE"&&
           witness.vertex==newVertex) {
            found=true;
            require(!witness.vertexInSource&&witness.vertexIntroduced,
                    "M14D must distinguish transaction-new vertex from inherited rim VertexId");
            require(witness.survivorEdge.sourceSides==2&&
                    witness.survivorEdge.candidateSides==2&&
                    witness.survivorEdge.sourceFaces.size()==2,
                    "M14D must record full original interior-edge incident FaceId provenance");
            require(!witness.indexedTopologyWitness,
                    "M14D geometric T candidate must not assert indexed topology failure");
        }
    require(found,"M14D source-backed witness must preserve M13 generated vertex ID");
    std::ostringstream log;
    printRepairSeamProvenance(log,explanation);
    require(log.str().find("validation_gate=FINAL_MANIFOLD_NOT_GEOMETRIC_CONTACT")
             !=std::string::npos&&
            log.str().find("intended_attachment=UNKNOWN")!=std::string::npos&&
            log.str().find("mandatory_refusal=0")!=std::string::npos,
            "M14D log must not invent intent or require zero geometric intersections");
    std::cout<<"[r06-m14d] source/new IDs, surviving interior edge and advisory witness PASS\n";
}
void testOriginalRimAndDedup() {
    Mesh flat;
    flat.vertices={{0,0,0},{2,0,0},{0,2,0}};
    flat.triangles={{0,1,2}};
    const auto imported=partitionFlatMesh(flat,64);
    const auto source=RegionTopologyIndex::build(imported.revision);
    const auto a=imported.flatIndex.vertexIds[0];
    const auto b=imported.flatIndex.vertexIds[1];
    const auto v=imported.flatIndex.vertexIds[2];
    RepairSeamAudit audit;
    RepairSeamIssue issue;
    issue.kind="NEW_VERTEX_INSIDE_SURVIVOR_EDGE";
    issue.vertex=v;
    issue.survivorA=a;
    issue.survivorB=b;
    audit.issues={issue,issue};
    audit.totalIssues=2;
    auto result=explainRepairSeamWitnesses(*source,*source,audit);
    require(result.complete&&result.inputStoredIssues==2&&
            result.uniqueWitnesses==1&&result.repeatedWitnesses==1&&
            result.witnesses[0].repeats==2,
            "M14D exact ID witnesses must deduplicate deterministically");
    require(result.witnesses[0].vertexInSource&&
            !result.witnesses[0].vertexIntroduced&&
            result.witnesses[0].survivorEdge.sourceSides==1&&
            result.witnesses[0].survivorEdge.sourceFaces.size()==1,
            "M14D original R03 one-sided rim must not be labeled a generated vertex");
    audit.issueListTruncated=true;
    result=explainRepairSeamWitnesses(*source,*source,audit);
    require(!result.complete&&result.storedOnly,
            "M14D bounded/truncated witness set must not claim global completeness");
    std::cout<<"[r06-m14d] original rim, inherited ID and bounded witness dedup PASS\n";
}
void testZeroWitnessesAreNotIntersectionProof() {
    const auto source=build(tetra());
    const auto result=explainRepairSeamWitnesses(*source,*source,RepairSeamAudit{});
    require(result.complete&&result.uniqueWitnesses==0&&result.storedOnly,
            "M14D no M13 witnesses is not a 3D intersection-free certificate");
}
}
int main() {
    try {
        testClosedBaselineAndNewContact();
        testOriginalRimAndDedup();
        testZeroWitnessesAreNotIntersectionProof();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M14D source witness provenance FAILED: "<<e.what()<<"\n";
        return 1;
    }
}
