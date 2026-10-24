#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_step_snapshot.hpp"
#include "mesher/mesh_partitioning.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace mesher;
namespace {
void require(bool b,const char* why) {
    if(!b)throw std::runtime_error(why);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    std::vector<RepairContour> rims;
    std::vector<std::array<VertexId,4>> vertices;
};
Fixture fixture(bool mirrored=false,bool remoteDuplicate=false) {
    Mesh mesh;
    mesh.vertices={{2,0,0},{4,0,0},{2,2,0},{2,0,2}};
    mesh.triangles={{0,1,3},{0,3,2},{1,2,3}};
    if(mirrored) {
        // Deliberately put reflected B before reflected A in stable-ID
        // order so partner of canonical A is the SECOND mirrored endpoint.
        mesh.vertices.insert(mesh.vertices.end(),
            {{-4,0,0},{-2,0,0},{-2,2,0},{-2,0,2}});
        mesh.triangles.insert(mesh.triangles.end(),
            {{5,7,4},{5,6,7},{4,7,6}});
    }
    if(remoteDuplicate) {
        const auto start=static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(),
            {{2.7,0,0},{9,0,0},{9,1,0}});
        mesh.triangles.push_back({start,start+1,start+2});
    }
    const auto partitions=partitionFlatMesh(mesh,64);
    Fixture f;
    f.root=RegionTopologyIndex::build(partitions.revision);
    const auto& ids=partitions.flatIndex.vertexIds;
    f.vertices.push_back({ids[0],ids[1],ids[2],ids[3]});
    RepairContour first;
    first.vertexIds={ids[0],ids[1],ids[2]};
    f.rims.push_back(first);
    if(mirrored) {
        f.vertices.push_back({ids[5],ids[4],ids[6],ids[7]});
        RepairContour second;
        second.vertexIds={ids[5],ids[6],ids[4]};
        f.rims.push_back(second);
    }
    return f;
}
void makeCap(MixedClosureStaging& stage,const Fixture& f) {
    for(const auto& v:f.vertices) {
        const auto owner=f.root->vertex(v[0]).owner;
        const bool negative=f.root->vertex(v[0]).position.x<0;
        if(negative)
            (void)stage.ensureFace(v[0],v[1],v[2],
                MixedClosureFaceKind::Cap,owner,2);
        else
            (void)stage.ensureFace(v[0],v[2],v[1],
                MixedClosureFaceKind::Cap,owner,2);
    }
}
bool same(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}
MixedClosureSplitSettings policy(bool mirrored=false) {
    MixedClosureSplitSettings settings;
    settings.requireReflectedGeometry=mirrored;
    settings.minSurvivorQuality=.012;
    settings.minCapQuality=.1;
    settings.maxRefinementSteps=20;
    settings.maxInsertedVertices=20;
    return settings;
}
void exactCapSurvivorAndReversedCalls() {
    const auto f=fixture();
    MixedClosureStaging stage(f.root,f.rims);
    makeCap(stage,f);
    auto view=f.root->revision().createMutableView();
    const auto a=f.vertices[0][0],b=f.vertices[0][1];
    const auto original=stage.sourceView()->face(
        f.root->vertex(a).incidentFaces.front()).vertices;
    std::vector<RepairStepGeometry> events;
    stage.setOperationObserver([&](const auto& s,const auto& op,const auto& args) {
        if(op=="splitEdge")
            events.push_back(captureRepairStepGeometry(s,op,args));
    });
    const auto mids=stage.splitEdge(view,b,a,policy(),0,0,false,.35);
    require(mids.size()==1&&stage.splitEvents().size()==1&&
        events.size()==1,"reversed call must commit one complete single-edge edit");
    const auto m=mids[0],p=stage.positions().at(m);
    const auto va=stage.positions().at(std::min(a,b));
    const auto vb=stage.positions().at(std::max(a,b));
    const auto expected=va+(vb-va)*.35;
    require(same(p,expected)&&
        same(stage.splitEvents()[0].position,expected)&&
        stage.splitEvents()[0].canonicalT==.35,
        "canonical t must define exact new private VertexId XYZ");
    require(stage.splitEvents()[0].incidentSurvivorSides==1&&
        stage.splitEvents()[0].incidentCapSides==1&&
        !stage.hasEffectiveEdge(a,b)&&stage.hasEffectiveEdge(a,m)&&
        stage.hasEffectiveEdge(m,b),
        "one operation must update both original survivor and cap side");
    require(stage.sourceView()->pendingFaceEdits()>=1&&
        events.front().arguments.find("canonical_t=0.34999999999999998")!=
            std::string::npos&&
        events.front().arguments.find(" xyz=")!=std::string::npos,
        "observer must report exact t and new private position after full edit");
    require(f.root->face(f.root->vertex(a).incidentFaces.front()).vertices==
            original&&f.root->retiredFaceCount()==0,
        "indexed R03 source must remain immutable and unpublished");
    // Same geometry from normal endpoint orientation in a separate stage.
    MixedClosureStaging ordered(f.root,f.rims);
    makeCap(ordered,f);
    auto otherView=f.root->revision().createMutableView();
    const auto sameId=ordered.splitEdge(otherView,a,b,policy(),0,0,false,.35);
    require(sameId.size()==1&&same(ordered.positions().at(sameId[0]),p),
        "caller argument reversal must not reverse stable-ID canonical t");
    std::cout<<"[r06-m17b] two-sided source/cap override, exact t/XYZ, reversed endpoints PASS\n";
}
void rejectedRequestsDoNotMutate() {
    const auto f=fixture();
    MixedClosureStaging stage(f.root,f.rims);
    makeCap(stage,f);
    auto view=f.root->revision().createMutableView();
    const auto a=f.vertices[0][0],b=f.vertices[0][1];
    int notifications=0;
    stage.setOperationObserver([&](const auto&,const auto& op,const auto&) {
        if(op=="splitEdge")++notifications;
    });
    const auto facesBefore=stage.faces().size();
    const auto base=stage.sourceView()->pendingFaceEdits();
    for(const double t:{0.01,0.0,1.0,
        std::numeric_limits<double>::quiet_NaN()}) {
        bool invalid=false;
        try {(void)stage.splitEdge(view,a,b,policy(),0,0,false,t);}
        catch(const std::invalid_argument&) {invalid=true;}
        require(invalid&&stage.faces().size()==facesBefore&&
           stage.sourceView()->pendingFaceEdits()==base&&
           stage.splitEvents().empty()&&notifications==0,
           "near-end, zero, one or nonfinite t cannot partially mutate stage");
    }
    auto strict=policy();
    strict.minCapQuality=.85;
    bool refused=false;
    try {(void)stage.splitEdge(view,a,b,strict,0,0,false,.35);}
    catch(const std::logic_error&) {refused=true;}
    require(refused&&stage.splitEvents().empty()&&
        stage.faces().size()==facesBefore&&notifications==0,
        "underquality proposed cap children fail before any mutation");
    auto budget=policy();
    budget.maxInsertedVertices=1;
    const auto extra=stage.addVertex(view,{2.7,0,0},f.root->vertex(a).owner);
    (void)extra;
    const auto afterVertex=stage.positions().newVertexPositions();
    refused=false;
    try {(void)stage.splitEdge(view,a,b,budget,0,0,false,.35);}
    catch(const std::logic_error&) {refused=true;}
    require(refused&&stage.positions().newVertexPositions()==afterVertex&&
            stage.splitEvents().empty()&&notifications==0,
            "existing distinct vertex at split XYZ cannot be welded by coincidence");
    std::cout<<"[r06-m17b] invalid/fraction/quality/collision refusal is mutation-free PASS\n";
}
void mirrorOrbitAndFiniteBudget() {
    const auto f=fixture(true);
    const auto a=f.vertices[0][0],b=f.vertices[0][1];
    MixedClosureStaging insufficient(f.root,f.rims);
    makeCap(insufficient,f);
    auto blockedView=f.root->revision().createMutableView();
    auto bounds=policy(true);
    bounds.maxRefinementSteps=1;
    bool declined=false;
    try {(void)insufficient.splitEdge(
        blockedView,a,b,bounds,0,0,false,.3);}
    catch(const std::length_error&) {declined=true;}
    require(declined&&insufficient.splitEvents().empty()&&
        insufficient.sourceView()->pendingFaceEdits()==0,
        "entire paired orbit budget must be checked before any source override");
    MixedClosureStaging stage(f.root,f.rims);
    makeCap(stage,f);
    auto view=f.root->revision().createMutableView();
    int splitNotifications=0;
    std::string args;
    stage.setOperationObserver([&](const auto& s,const auto& op,const auto& text) {
        if(op=="splitEdge") {
            ++splitNotifications;
            args=text;
            require(s.splitEvents().size()==2,
                    "observer cannot see half-updated mirror orbit");
        }
    });
    const auto ids=stage.splitEdge(view,a,b,policy(true),0,0,false,.3);
    require(ids.size()==2&&splitNotifications==1&&
        stage.splitEvents().size()==2&&
        args.find("mirror_orbit=2")!=std::string::npos,
        "complete reflected conforming split must notify only once");
    const auto& one=stage.splitEvents()[0];
    const auto& two=stage.splitEvents()[1];
    const auto p=stage.positions().at(ids[0]),q=stage.positions().at(ids[1]);
    require(one.midpoint==ids[0]&&two.midpoint==ids[1]&&
        same(p,one.position)&&same(q,two.position)&&
        p.x==-q.x&&p.y==q.y&&p.z==q.z&&
        (one.canonicalT==.3||one.canonicalT==.7)&&
        (two.canonicalT==.3||two.canonicalT==.7)&&
        one.canonicalT!=two.canonicalT,
        "mirrored XYZ must be exact despite reversed partner endpoint ordering");
    require(one.incidentCapSides==1&&two.incidentCapSides==1&&
        one.incidentSurvivorSides==1&&two.incidentSurvivorSides==1&&
        f.root->retiredFaceCount()==0,
        "both incident sides and immutable roots must survive mirror orbit");
    std::cout<<"[r06-m17b] one atomic two-edge reflected transaction, t/1-t and exact XYZ PASS\n";
}
void missingPartnerRefused() {
    const auto f=fixture();
    MixedClosureStaging stage(f.root,f.rims);
    makeCap(stage,f);
    auto view=f.root->revision().createMutableView();
    const auto a=f.vertices[0][0],b=f.vertices[0][1];
    bool refused=false;
    try {(void)stage.splitEdge(view,a,b,policy(true),0,0,false,.3);}
    catch(const std::logic_error&){refused=true;}
    require(refused&&stage.splitEvents().empty()&&
       stage.sourceView()->pendingFaceEdits()==0,
       "incomplete mirror provenance must refuse before mutation");
    std::cout<<"[r06-m17b] missing symmetry orbit preflight refusal PASS\n";
}
} // namespace
int main() {
    try {
        exactCapSurvivorAndReversedCalls();
        rejectedRequestsDoNotMutate();
        mirrorOrbitAndFiniteBudget();
        missingPartnerRefused();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"[r06-m17b] FAILED: "<<e.what()<<"\n";
        return 1;
    }
}
