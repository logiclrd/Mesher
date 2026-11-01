#include "mesher/brush.hpp"
#include "mesher/remesh.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

using namespace mesher;

namespace {
void check(bool value,const char* message) {
    if(!value) throw std::runtime_error(message);
}
void testTopologyAndMeasurements() {
    SphereOptions options;
    options.vertexCount=500;
    auto mesh=generateSphere(options);
    check(validateClosedSphereTopology(mesh),"Initial topology");
    auto d=measureMesh(mesh);
    check(d.minimumEdgeLength>0 && d.minimumTriangleAltitude>0 &&
          d.invertedFaces==0,"Initial sphere measurements");
    auto before=mesh.vertices;
    auto zero=measureMesh(mesh,before,10.);
    check(zero.shrinkingShortEdges==0,"Already short edges don't count as compression");
    CollapseSettings s;
    s.enabled=true; s.minimumEdgeRatio=10;
    DirectionalCollapser c(mesh,s,symmetricSphereMirrors(options.vertexCount));
    const auto result=c.apply(mesh,before);
    check(result.accepted==0 && result.removedVertices==0 &&
          mesh.vertices.size()==before.size(),"Never simplify a stationary mesh");
}
void testSymmetricCollapse() {
    SphereOptions options;
    options.vertexCount=500;
    auto mesh=generateSphere(options);
    auto before=mesh.vertices;
    auto mirrors=symmetricSphereMirrors(options.vertexCount);

    // Compress an ordinary hemisphere edge and its mirror. This is a
    // controlled, directional candidate, not an indiscriminate remesh.
    bool found=false;
    std::unordered_set<std::uint64_t> visited;
    for(const auto t:mesh.triangles) {
        for(const auto& pair:{std::pair{t.a,t.b},std::pair{t.b,t.c},
                             std::pair{t.c,t.a}}) {
            auto a=pair.first,b=pair.second;
            if(a>=mirrors[a] || b>=mirrors[b]) continue; // positive side only
            const std::uint64_t edge=(std::uint64_t(std::min(a,b))<<32)|std::max(a,b);
            if(!visited.insert(edge).second) continue;
            Mesh candidate=mesh;
            const auto midpoint=(candidate.vertices[a]+candidate.vertices[b])*.5;
            candidate.vertices[b]=midpoint;
            candidate.vertices[mirrors[b]]=midpoint-
                Vec3{2*midpoint.x,0,0};
            CollapseSettings settings;settings.enabled=true;
            settings.minimumEdgeRatio=2.;settings.maximumPairsPerStamp=1;
            DirectionalCollapser c(candidate,settings,mirrors);
            const auto result=c.apply(candidate,before);
            if(result.removedVertices==2) {
                mesh=std::move(candidate);
                mirrors=c.mirrors();
                found=true;
                break;
            }
        }
        if(found) break;
    }
    check(found,"Find one safe shrinking mirrored edge collapse");
    check(validateClosedSphereTopology(mesh),"Collapsed mesh remains closed");
    check(mesh.vertices.size()==options.vertexCount-2,
          "Two mirrored vertices removed atomically");
    for(std::uint32_t i=0;i<mirrors.size();++i) {
        const auto v=mesh.vertices[i],m=mesh.vertices[mirrors[i]];
        check(std::abs(v.x+m.x)<1e-10 &&
              std::abs(v.y-m.y)<1e-10 && std::abs(v.z-m.z)<1e-10,
              "Collapsed geometry preserves exact bilateral positions");
        check(mirrors[mirrors[i]]==i,"Mirror map remains involutive");
    }
}
void testIndependentCurvatureAndCollapseSettings() {
    SphereOptions options;
    options.vertexCount=800;
    const Mesh source=generateSphere(options);
    CollapseSettings collapse;collapse.enabled=true;
    CurvatureSettings curvature;curvature.enabled=true;curvature.strength=1.0;
    auto baselineMesh=source, correctedMesh=source, collapsedMesh=source, combinedMesh=source;
    InflateDemoAnimation baseline(baselineMesh,.02,.012,128,.4,{},true,{});
    InflateDemoAnimation corrected(correctedMesh,.02,.012,128,.4,{},true,curvature);
    InflateDemoAnimation collapsed(collapsedMesh,.02,.012,128,.4,collapse,true,{});
    InflateDemoAnimation combined(combinedMesh,.02,.012,128,.4,collapse,true,curvature);
    for(std::size_t stamp=0;stamp<baseline.total();++stamp) {
        check(baseline.advance()&&corrected.advance()&&
              collapsed.advance()&&combined.advance(),
              "All four modes must process the identical stamp trajectory");
        for(const auto* m:{&baselineMesh,&correctedMesh,&collapsedMesh,&combinedMesh})
            check(validateClosedSphereTopology(*m),
                  "No setting combination may break closed topology");
    }
    check(correctedMesh.vertices.size()==source.vertices.size() &&
          combinedMesh.vertices.size()<=source.vertices.size(),
          "Curvature itself must never remesh");
    double difference=0;
    for(std::size_t i=0;i<source.vertices.size();++i)
        difference=std::max(difference,
                            length(correctedMesh.vertices[i]-baselineMesh.vertices[i]));
    check(difference>1e-4,"Curvature option must have an observable effect");
    const auto result=combinedMesh;
    combined.reset();
    while(combined.advance()) {}
    check(result.vertices.size()==combinedMesh.vertices.size() &&
          result.triangles.size()==combinedMesh.triangles.size(),
          "Combined mode replay must preserve topology");
    for(std::size_t i=0;i<result.vertices.size();++i)
        check(length(result.vertices[i]-combinedMesh.vertices[i])<1e-12,
              "Combined mode replay must be deterministic");
    std::cout<<"Four modes inverted at 800 vertices: "
             <<baseline.diagnostics().invertedFaces<<", "
             <<corrected.diagnostics().invertedFaces<<", "
             <<collapsed.diagnostics().invertedFaces<<", "
             <<combined.diagnostics().invertedFaces<<"\n";
}
void testAnimationAndReplay() {
    SphereOptions options;
    options.vertexCount=800;
    auto source=generateSphere(options),baseline=source,withCollapse=source;
    InflateDemoAnimation without(baseline,.02,.012,128,.4,{},true);
    CollapseSettings settings;settings.enabled=true;
    InflateDemoAnimation with(withCollapse,.02,.012,128,.4,settings,true);
    std::size_t eligibleStamps=0;
    for(std::size_t i=0;i<with.total();++i) {
        check(without.advance() && with.advance(),"Both modes same sampling");
        if(with.preCollapseDiagnostics().shrinkingShortEdges>0) ++eligibleStamps;
        check(validateClosedSphereTopology(withCollapse),
              "Remesher must preserve closed manifold each stamp");
    }
    check(with.total()==without.total(),"Same stamp count");
    check(withCollapse.vertices.size()<=source.vertices.size(),
          "Collapse never increases vertex count");
    check(eligibleStamps>0,"Experiment sees compressing short edges");
    const auto result=withCollapse;
    const auto totalRemoved=with.totalRemoved();
    with.reset();
    check(with.completed()==0 && with.totalRemoved()==0,"Reset counts");
    check(withCollapse.vertices.size()==source.vertices.size() &&
          withCollapse.triangles.size()==source.triangles.size(),
          "Reset restores original topology");
    while(with.advance()) {}
    check(with.totalRemoved()==totalRemoved,"Replay collapse count deterministic");
    check(result.vertices.size()==withCollapse.vertices.size() &&
          result.triangles.size()==withCollapse.triangles.size(),
          "Replay topology deterministic");
    for(std::size_t i=0;i<result.vertices.size();++i)
        check(length(result.vertices[i]-withCollapse.vertices[i])<1e-12,
              "Replay vertex positions deterministic");
    std::cout<<"800-vertex demo: "<<eligibleStamps<<" compression stamps, "
             <<totalRemoved<<" vertices removed, "
             <<without.diagnostics().invertedFaces<<" baseline inversions, "
             <<with.diagnostics().invertedFaces<<" collapse inversions\n";
}
}
int main() {
    try {
        testTopologyAndMeasurements();
        testSymmetricCollapse();
        testAnimationAndReplay();
        testIndependentCurvatureAndCollapseSettings();
    } catch(const std::exception& e) {
        std::cerr<<"Remesh test failed: "<<e.what()<<'\n';
        return 1;
    }
    std::cout<<"Remesh tests passed\n";
    return 0;
}
