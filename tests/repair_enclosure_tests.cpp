#include "mesher/repair_enclosure.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/sculpt_fixtures.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool passed,const char* message) {
    if(!passed)throw std::runtime_error(message);
}
Mesh spheres(std::vector<std::pair<Vec3,double>> descriptors,
             std::vector<bool> inverted) {
    Mesh combined;
    SphereOptions options;
    options.vertexCount=160;
    options.relaxationIterations=8;
    options.symmetric=false;
    const auto unit=generateSphere(options);
    require(descriptors.size()==inverted.size(),"Invalid sphere fixture specification");
    for(std::size_t i=0;i<descriptors.size();++i) {
        const auto offset=static_cast<std::uint32_t>(combined.vertices.size());
        for(const auto p:unit.vertices)
            combined.vertices.push_back(descriptors[i].first+
                                        p*descriptors[i].second);
        for(const auto t:unit.triangles) {
            if(inverted[i])
                combined.triangles.push_back({offset+t.a,offset+t.c,offset+t.b});
            else
                combined.triangles.push_back({offset+t.a,offset+t.b,offset+t.c});
        }
    }
    return combined;
}
std::shared_ptr<const RegionTopologyIndex> indexed(Mesh m) {
    auto imported=partitionFlatMesh(m,64);
    return RegionTopologyIndex::build(imported.revision);
}
void independentExteriors() {
    const auto mesh=indexed(spheres({{{-4,0,0},1},{{4,0,0},1}}, {false,false}));
    const auto root=&mesh->revision();
    const auto picker=mesh->pickingRootIdentity();
    const auto report=inspectEnclosedComponents(mesh);
    require(report.inventory.components.size()==2 &&
            report.components.size()==2,
            "R06-M02 disconnected exterior shells remain separate components");
    for(const auto& item:report.components)
        require(item.kind==EnclosedComponentKind::Exterior &&
                item.signedVolume>0,
                "R06-M02 must preserve independent outward-facing solids");
    require(&mesh->revision()==root &&
            mesh->pickingRootIdentity()==picker &&
            mesh->retiredFaceCount()==0,
            "R06-M02 must not mutate immutable source or picking authority");
    std::cout<<"[r06-m02] disjoint outward solids preserved: PASS\n";
}
void enclosedNegativeIsRedundant() {
    const auto mesh=indexed(spheres({{{0,0,0},1},{{0,0,0},.65}}, {false,true}));
    const auto report=inspectEnclosedComponents(mesh);
    require(report.inventory.components.size()==2,
            "R06-M02 nested sphere faces must not share stable IDs");
    std::size_t exterior=0,cavity=0;
    for(const auto& item:report.components) {
        if(item.kind==EnclosedComponentKind::Exterior) {
            ++exterior;
            require(item.signedVolume>0,"R06-M02 exterior orientation positive");
        }
        if(item.kind==EnclosedComponentKind::RedundantEnclosedCavity) {
            ++cavity;
            require(item.signedVolume<0 &&
                    item.enclosingComponent<report.components.size() &&
                    report.components[item.enclosingComponent].signedVolume>0 &&
                    item.witnessPoints>=3,
                    "R06-M02 deletion requires negative orientation plus positive enclosing shell");
        }
    }
    require(exterior==1&&cavity==1 &&
            mesh->retiredFaceCount()==0 &&
            mesh->retiredVertexCount()==0,
            "R06-M02 internal cavity must be classified, never deleted in M02");
    std::cout<<"[r06-m02] isolated inward shell enclosed by outward shell: PASS\n";

    auto tiny=EnclosedComponentLimits{};
    tiny.maxWindingTriangleVisits=1;
    bool refused=false;
    try{(void)inspectEnclosedComponents(mesh,tiny);}
    catch(const std::length_error&){refused=true;}
    require(refused,"R06-M02 must fail closed when winding evidence exceeds work limit");

    tiny=EnclosedComponentLimits{};
    tiny.maxCollisionSeedFaces=1;
    refused=false;
    try{(void)inspectEnclosedComponents(mesh,tiny);}
    catch(const std::length_error&){refused=true;}
    require(refused,"R06-M02 must fail closed on incomplete indexed contact evidence");

    tiny=EnclosedComponentLimits{};
    tiny.maxFaces=10;
    refused=false;
    try{(void)inspectEnclosedComponents(mesh,tiny);}
    catch(const std::length_error&){refused=true;}
    require(refused,"R06-M02 must fail closed on incomplete component inventory");
}
void negativeOutsideRefused() {
    const auto mesh=indexed(spheres({{{-4,0,0},1},{{4,0,0},1}},
                                    {false,true}));
    const auto report=inspectEnclosedComponents(mesh);
    std::size_t outside=0,uncertain=0;
    for(const auto& item:report.components) {
        outside+=item.kind==EnclosedComponentKind::Exterior?1u:0u;
        uncertain+=item.kind==EnclosedComponentKind::Ambiguous?1u:0u;
        require(item.kind!=EnclosedComponentKind::RedundantEnclosedCavity,
                "R06-M02 reversed uncontained exterior cannot be deleted");
    }
    require(outside==1&&uncertain==1,
            "R06-M02 inverted outside component requires explicit refusal");
    std::cout<<"[r06-m02] inverted uncontained exterior explicitly ambiguous: PASS\n";
}
void nestedOutwardRefused() {
    const auto mesh=indexed(spheres({{{0,0,0},1},{{0,0,0},.65}},
                                    {false,false}));
    const auto report=inspectEnclosedComponents(mesh);
    require(report.components.size()==2 &&
            std::any_of(report.components.begin(),report.components.end(),
                 [](const auto& c){return c.kind==EnclosedComponentKind::Ambiguous;}) &&
            std::none_of(report.components.begin(),report.components.end(),
                 [](const auto& c){
                     return c.kind==EnclosedComponentKind::RedundantEnclosedCavity;
                 }),
            "R06-M02 nested outward shells need separate material burial proof");
    std::cout<<"[r06-m02] nested outward shell refuses unsound cavity deletion: PASS\n";
}
void touchingNestedSurfacesRefused() {
    const auto mesh=indexed(spheres({{{0,0,0},1},{{0,0,0},1}},
                                    {false,true}));
    // Identical inverted shells can generate many legitimate coplanar
    // contacts, with a different count across floating-point platforms.
    // Give this classification test enough finite evidence to reach the
    // actual no-deletion decision, rather than testing a budget exception.
    auto limits=EnclosedComponentLimits{};
    limits.maxCollisionReportedContacts=32768;
    const auto report=inspectEnclosedComponents(mesh,limits);
    require(std::none_of(report.components.begin(),report.components.end(),
                 [](const auto& c){
                     return c.kind==EnclosedComponentKind::RedundantEnclosedCavity;
                 }),
            "R06-M02 exact coincident inverted surfaces cannot authorize deletion");
    std::cout<<"[r06-m02] coincident inverted independent vertices refuse: PASS\n";
}
void openMouthStillOneBoundary() {
    const auto mesh=indexed(generateHollowSphereFixture(5000,1,65));
    const auto report=inspectEnclosedComponents(mesh);
    require(report.components.size()==1 &&
            report.components.front().kind==EnclosedComponentKind::Exterior &&
            report.components.front().signedVolume>0,
            "R06-M02 accessible hollow fixture is one connected closed shell, not isolated cavity");
    std::cout<<"[r06-m02] open-mouth hollow has no isolated inner component: PASS\n";
}
}
int main() {
    try {
        independentExteriors();
        enclosedNegativeIsRedundant();
        negativeOutsideRefused();
        nestedOutwardRefused();
        touchingNestedSurfacesRefused();
        openMouthStillOneBoundary();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M02 enclosed component proof FAILED: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
