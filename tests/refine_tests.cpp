#include "mesher/refine.hpp"
#include "mesher/remesh.hpp"
#include "mesher/brush.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
void validateReflection(const Mesh& mesh,const std::vector<std::uint32_t>& mirrors) {
    check(mirrors.size()==mesh.vertices.size(),"Mirror count matches vertices");
    for(std::uint32_t i=0;i<mirrors.size();++i) {
        const auto other=mirrors[i];
        check(other<mirrors.size() && mirrors[other]==i,"Mirror map reciprocal");
        const auto a=mesh.vertices[i],b=mesh.vertices[other];
        check(std::abs(a.x+b.x)<1e-9 && std::abs(a.y-b.y)<1e-9 &&
              std::abs(a.z-b.z)<1e-9,"Reflected vertices stay paired");
    }
    // Reflection must preserve triangle connectivity as well as positions;
    // both sides must share the same seam vertices by index.
    std::set<std::array<std::uint32_t,3>> faces;
    auto sorted=[](std::uint32_t a,std::uint32_t b,std::uint32_t c) {
        std::array<std::uint32_t,3> key{a,b,c};
        std::sort(key.begin(),key.end());
        return key;
    };
    for(const auto t:mesh.triangles) faces.insert(sorted(t.a,t.b,t.c));
    for(const auto t:mesh.triangles)
        check(faces.contains(sorted(mirrors[t.a],mirrors[t.b],mirrors[t.c])),
              "Mirrored triangle must exist after local topology changes");
}
void testSelectiveSplitting() {
    SphereOptions opts;opts.vertexCount=800;opts.relaxationIterations=20;
    auto mesh=generateSphere(opts);
    auto before=mesh.vertices;
    RefineSettings settings;
    settings.enabled=true;settings.maximumEdgeRatio=.5;
    settings.maximumSplitPairsPerStamp=3;settings.optimizeFlips=false;
    auto mirror=symmetricSphereMirrors(opts.vertexCount);
    LocalRefiner refiner(mesh,settings,mirror);
    auto none=refiner.apply(mesh,before);
    check(none.splitPairs==0 && mesh.vertices.size()==opts.vertexCount,
          "Edges already over limit must not be split unless they stretch");

    // Stretch an edge on both hemispheres, keeping exact positional symmetry.
    const auto face=mesh.triangles.front();
    std::uint32_t vertex=face.a;
    for(const auto i:{face.a,face.b,face.c})
        if(i<mirror[i])vertex=i;
    const auto opposite=mirror[vertex];
    check(vertex!=opposite,"Use a hemisphere vertex, not a seam vertex");
    mesh.vertices[vertex]=mesh.vertices[vertex]*1.12;
    mesh.vertices[opposite]={-mesh.vertices[vertex].x,
                             mesh.vertices[vertex].y,mesh.vertices[vertex].z};

    const auto result=refiner.apply(mesh,before);
    check(result.splitPairs>0 && result.newVertices>0,"Stretch triggers subdivision");
    check(result.splitPairs<=3 && result.newVertices<=6,"Split stamp budget is respected");
    check(before.size()==mesh.vertices.size(),"Interpolated original positions kept");
    check(validateClosedSphereTopology(mesh),"Each split is conforming, manifold");
    validateReflection(mesh,refiner.mirrors());
}
void testAffectedFaceCandidateEquivalence() {
    SphereOptions options;
    options.vertexCount=800;
    auto original=generateSphere(options);
    auto full=original,local=original;
    const auto mirror=symmetricSphereMirrors(options.vertexCount);
    const std::uint32_t index=10;
    const auto partner=mirror[index];
    full.vertices[index]=full.vertices[index]*1.18;
    full.vertices[partner]={-full.vertices[index].x,
                            full.vertices[index].y,full.vertices[index].z};
    local.vertices=full.vertices;
    std::vector<std::uint32_t> faces;
    for(std::uint32_t face=0;face<full.triangles.size();++face) {
        const auto t=full.triangles[face];
        if(t.a==index||t.b==index||t.c==index||
           t.a==partner||t.b==partner||t.c==partner)
            faces.push_back(face);
    }
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.5;
    settings.maximumSplitPairsPerStamp=4;
    settings.optimizeFlips=true;
    LocalRefiner allRefiner(full,settings,mirror);
    LocalRefiner localRefiner(local,settings,mirror);
    auto beforeFull=original.vertices,beforeLocal=original.vertices;
    const auto allStats=allRefiner.apply(full,beforeFull);
    const auto localStats=localRefiner.apply(local,beforeLocal,faces,true);
    check(allStats.splitPairs==localStats.splitPairs &&
          allStats.flipPairs==localStats.flipPairs &&
          full.vertices.size()==local.vertices.size() &&
          full.triangles.size()==local.triangles.size(),
          "Restricted growing-edge search matches full scan");
    for(std::size_t i=0;i<full.vertices.size();++i)
        check(length(full.vertices[i]-local.vertices[i])<1e-12,
              "Restricted refinement vertex positions agree");
    for(std::size_t i=0;i<full.triangles.size();++i) {
        const auto a=full.triangles[i],b=local.triangles[i];
        check(a.a==b.a&&a.b==b.b&&a.c==b.c,
              "Restricted refinement topology agrees");
    }
}
// Rebuilding the index before every stamp is the reference implementation.
// Persistent local connectivity must produce exactly the same vertex indices,
// oriented triangles, geometry and flip/split decisions across many updates.
void testPersistentConnectivityEquivalence() {
    SphereOptions opts;
    opts.vertexCount=800;
    opts.relaxationIterations=20;
    auto meshA=generateSphere(opts);
    auto meshB=meshA;
    const auto initialMirror=symmetricSphereMirrors(opts.vertexCount);
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.55;
    settings.maximumSplitPairsPerStamp=4;
    settings.maximumFlipPairsPerStamp=4;
    LocalRefiner persistent(meshA,settings,initialMirror);
    LocalRefiner rebuilt(meshB,settings,initialMirror);
    std::size_t totalSplits=0;
    for(int pass=0;pass<16;++pass) {
        const auto beforeA=meshA.vertices;
        const auto beforeB=meshB.vertices;
        const auto mirror=persistent.mirrors();
        const std::uint32_t target=static_cast<std::uint32_t>(10+pass*3);
        const auto partner=mirror.at(target);
        check(target!=partner,"Picked vertex must have a distinct mirror");
        auto push=[&](Mesh& m) {
            m.vertices[target]=m.vertices[target]*1.06;
            m.vertices[partner]={-m.vertices[target].x,
                                  m.vertices[target].y,m.vertices[target].z};
        };
        push(meshA);
        push(meshB);
        std::vector<std::uint32_t> faces;
        for(std::uint32_t i=0;i<meshA.triangles.size();++i) {
            const auto t=meshA.triangles[i];
            if(t.a==target||t.b==target||t.c==target||
               t.a==partner||t.b==partner||t.c==partner)
                faces.push_back(i);
        }
        // Force only the reference to rebuild its connectivity while keeping
        // the original nominal edge target and mirror correspondence.
        rebuilt.setMirrors(rebuilt.mirrors());
        auto previousA=beforeA,previousB=beforeB;
        const auto a=persistent.apply(meshA,previousA,faces,true);
        const auto b=rebuilt.apply(meshB,previousB,faces,true);
        totalSplits+=a.splitPairs;
        check(a.splitPairs==b.splitPairs && a.flipPairs==b.flipPairs &&
              a.newVertices==b.newVertices && a.longGrowingEdges==b.longGrowingEdges,
              "Persistent and rebuilt adjacency must select identical edits");
        check(meshA.vertices.size()==meshB.vertices.size() &&
              meshA.triangles.size()==meshB.triangles.size(),
              "Local edits must preserve identical counts");
        for(std::size_t i=0;i<meshA.vertices.size();++i)
            check(length(meshA.vertices[i]-meshB.vertices[i])<1e-12,
                  "Local edits must preserve identical geometry");
        for(std::size_t i=0;i<meshA.triangles.size();++i) {
            const auto x=meshA.triangles[i],y=meshB.triangles[i];
            check(x.a==y.a&&x.b==y.b&&x.c==y.c,
                  "Local edits must preserve face indices and orientation");
        }
        check(validateClosedSphereTopology(meshA),
              "Persistent edits preserve closed manifold topology");
        check(persistent.mirrors()==rebuilt.mirrors(),
              "Persistent edits preserve symmetric vertex mapping");
    }
    check(totalSplits>0,"Test must exercise actual repeated splitting");
}
void testAnimationSymmetryAndReplay() {
    SphereOptions opts;opts.vertexCount=800;opts.relaxationIterations=20;
    auto mesh=generateSphere(opts),original=mesh;
    CollapseSettings collapse;collapse.enabled=true;collapse.maximumPairsPerStamp=2;
    RefineSettings refine;refine.enabled=true;refine.maximumEdgeRatio=1.05;
    refine.maximumSplitPairsPerStamp=3;refine.maximumFlipPairsPerStamp=3;
    CurvatureSettings curvature;curvature.enabled=true;curvature.strength=.25;
    InflateDemoAnimation animation(mesh,.02,.012,128,.4,collapse,true,curvature,refine);
    std::size_t added=0,flips=0,removed=0;
    for(std::size_t step=0;step<animation.total();++step) {
        check(animation.advance(),"Incremental topology-changing stamp");
        check(validateClosedSphereTopology(mesh),"Every step keeps closed manifold");
        if(step%15==0 || step+1==animation.total())
            validateReflection(mesh,animation.mirrors());
        added+=animation.lastRefine().newVertices;
        flips+=animation.lastRefine().flipPairs;
        removed+=animation.lastCollapse().removedVertices;
    }
    check(added>0,"Demo must perform actual stretching-driven splits");
    check(animation.totalAdded()==added && animation.totalFlips()==flips &&
          animation.totalRemoved()==removed,"Per-stamp counters accumulate");
    const auto finalMesh=mesh;
    const auto finalAdded=animation.totalAdded(),finalFlips=animation.totalFlips();
    animation.reset();
    check(mesh.vertices.size()==original.vertices.size() &&
          mesh.triangles.size()==original.triangles.size(),
          "Replay restores pre-sculpt topology");
    while(animation.advance()) {}
    check(animation.totalAdded()==finalAdded && animation.totalFlips()==finalFlips,
          "Topology operation counts replay deterministically");
    check(mesh.vertices.size()==finalMesh.vertices.size() &&
          mesh.triangles.size()==finalMesh.triangles.size(),
          "Replayed mesh topology has same sizes");
    for(std::size_t i=0;i<mesh.vertices.size();++i)
        check(length(mesh.vertices[i]-finalMesh.vertices[i])<1e-12,
              "Replayed vertex geometry is deterministic");
    for(std::size_t i=0;i<mesh.triangles.size();++i) {
        const auto a=mesh.triangles[i],b=finalMesh.triangles[i];
        check(a.a==b.a && a.b==b.b && a.c==b.c,
              "Replayed face connectivity identical");
    }
    std::cout<<"Remeshed 800-vertex demo: added "<<added<<", flipped "
             <<flips<<", removed "<<removed<<", inverted "
             <<animation.diagnostics().invertedFaces<<"\n";
}
}
int main() {
    try {testSelectiveSplitting();testAffectedFaceCandidateEquivalence();testPersistentConnectivityEquivalence();testAnimationSymmetryAndReplay();}
    catch(const std::exception& e) {
        std::cerr<<"Refinement test failed: "<<e.what()<<'\n';
        return 1;
    }
    std::cout<<"Local refinement tests passed\n";
}
