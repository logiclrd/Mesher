#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_inflate.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool condition,const char* reason) {
    if(!condition)throw std::runtime_error(reason);
}
Mesh sphere(std::uint32_t count=500) {
    SphereOptions options;
    options.vertexCount=count;
    options.relaxationIterations=8;
    return generateSphere(options);
}
void compare(const Mesh& a,const Mesh& b,double tolerance) {
    require(a.vertices.size()==b.vertices.size() &&
            a.triangles.size()==b.triangles.size(),
            "Versioned Inflate must preserve flat topology");
    for(std::size_t i=0;i<a.vertices.size();++i) {
        const auto difference=length(a.vertices[i]-b.vertices[i]);
        require(difference<=tolerance,
                "Versioned Inflate differs from original simultaneous normals");
    }
    for(std::size_t i=0;i<a.triangles.size();++i) {
        const auto x=a.triangles[i],y=b.triangles[i];
        require(x.a==y.a&&x.b==y.b&&x.c==y.c,
                "Versioned Inflate must preserve face winding/identity");
    }
}
struct AreaRecorder final : Brush {
    std::vector<std::vector<BrushArea>> stamps;
    void stamp(Mesh&,std::span<const BrushArea> areas) override {
        stamps.emplace_back(areas.begin(),areas.end());
    }
};
std::vector<std::vector<BrushArea>> exactTimedStamps(bool paired) {
    Mesh dummy;
    AreaRecorder recorder;
    BrushStroke stroke(dummy,recorder,.02);
    const Vec3 first{-.22,-.65,.69};
    auto centers=[paired](Vec3 p) {
        if(paired)return std::vector<Vec3>{p,{-p.x,p.y,p.z}};
        return std::vector<Vec3>{p};
    };
    std::vector<BrushArea> start{{first,.27,.3}};
    if(paired)start.push_back({{-first.x,first.y,first.z},.27,.3});
    stroke.startTimed(start,.2);
    stroke.updateTimed(centers({-.23,-.67,.68}),.16);
    stroke.updateTimed(centers({-.23,-.67,.68}),.32);
    stroke.finishTimed(centers({-.26,-.70,.64}),.43);
    require(recorder.stamps.size()>=5,
            "Timed recording must exercise moving and stationary 4D stamps");
    return recorder.stamps;
}
void testVersionedMatchesReference(CurvatureSettings curvature) {
    Mesh flat=sphere();
    const auto imported=partitionFlatMesh(flat,32);
    auto current=RegionTopologyIndex::build(imported.revision);
    InflateBrush original(.0025,curvature);
    original.begin(flat);
    VersionedInflateBrush versioned(.0025,curvature);
    auto frames=exactTimedStamps(false);
    std::size_t changedStamps=0;
    for(const auto& areas:frames) {
        const auto old=current;
        const auto stamp=versioned.stamp(current,areas,32);
        original.stamp(flat,areas);
        current=stamp.index;
        if(stamp.moved==0) {
            require(current==old,"Empty stamp must not allocate a revision");
        }else {
            ++changedStamps;
            require(stamp.baseline.before==old,
                    "Pre-stamp baseline must retain old immutable index");
            require(stamp.baseline.movedVertices.size()==stamp.moved,
                    "Affected baseline contains only actual moved vertex IDs");
            require(&current->revision()!=&old->revision(),
                    "Edited stamp produces a new immutable mesh revision");
            require(current->updateStatistics().fullMeshTraversals==0,
                    "Stamp cannot rebuild topology index");
            require(current->updateStatistics().changedFaceRecords==0,
                    "Inflate deformation must not change face indices");
            require(current->updateStatistics().changedVertexRecords==stamp.moved,
                    "Only moved vertex index records may change");
            require(stamp.commitStatistics.modifiedPartitions<
                    old->revision().partitionCount(),
                    "Small brush must not clone whole mesh");
            const auto id=stamp.baseline.movedVertices.front();
            require(length(stamp.baseline.positionBefore(id)-
                           old->vertex(id).position)==0,
                    "Baseline reads old pre-stamp coordinates");
            const auto face=stamp.baseline.affectedFaces.front();
            const auto tri=old->face(face).vertices;
            const double manual=length(old->vertex(tri[0]).position-
                                       old->vertex(tri[1]).position);
            require(stamp.baseline.edgeLengthBefore(tri[0],tri[1])==manual,
                    "Directional remeshing may query exact old edge lengths");
            const double newLength=length(current->vertex(tri[0]).position-
                                          current->vertex(tri[1]).position);
            require(stamp.baseline.edgeWasGrowing(*current,tri[0],tri[1])==
                    (newLength>manual+1e-12),
                    "Directional stretch test agrees with old/new positions");
            require(stamp.baseline.edgeWasShrinking(*current,tri[0],tri[1])==
                    (newLength+1e-12<manual),
                    "Directional shrink test agrees with old/new positions");
        }
        const auto rebuilt=reconstructFlatMesh(current->revision(),
                                               imported.flatIndex);
        compare(flat,rebuilt,1e-10);
    }
    require(changedStamps>=3,"Test requires several non-empty brush stamps");
    original.end(flat);
}
void testSymmetricInflate() {
    Mesh flat=sphere(800);
    const auto imported=partitionFlatMesh(flat,32);
    auto current=RegionTopologyIndex::build(imported.revision);
    const auto mirrorIndices=symmetricSphereMirrors(
        static_cast<std::uint32_t>(flat.vertices.size()));
    std::vector<std::pair<VertexId,VertexId>> mirrors;
    for(std::size_t i=0;i<mirrorIndices.size();++i)
        mirrors.push_back({imported.flatIndex.vertexIds[i],
                           imported.flatIndex.vertexIds[mirrorIndices[i]]});
    VersionedInflateBrush brush(.0015,CurvatureSettings{true,.25},
                                std::move(mirrors));
    InflateBrush original(.0015,CurvatureSettings{true,.25});
    original.begin(flat);
    const auto frames=exactTimedStamps(true);
    for(const auto& areas:frames) {
        original.stamp(flat,areas);
        auto result=brush.stamp(current,areas,32);
        current=result.index;
        if(result.moved) {
            const auto& changed=result.baseline.movedVertices;
            require(std::is_sorted(changed.begin(),changed.end()),
                    "Baseline moved vertices must be stable ID ordered");
        }
        const auto output=reconstructFlatMesh(current->revision(),
                                              imported.flatIndex);
        compare(flat,output,2e-9);
        for(std::size_t i=0;i<flat.vertices.size();++i) {
            const auto m=mirrorIndices[i];
            const auto a=output.vertices[i],b=output.vertices[m];
            require(std::abs(a.x+b.x)<1e-10 &&
                    std::abs(a.y-b.y)<1e-10 &&
                    std::abs(a.z-b.z)<1e-10,
                    "Versioned symmetrical Inflate must stay mirrored");
            if(i==m)require(a.x==0,
                            "Shared seam must remain precisely on X=0");
        }
    }
    original.end(flat);
}
void testSignedDeflateUsesSameNormals() {
    const auto source=sphere(800);
    const auto imported=partitionFlatMesh(source,32);
    const auto before=RegionTopologyIndex::build(imported.revision);
    const auto mirrorIndices=symmetricSphereMirrors(
        static_cast<std::uint32_t>(source.vertices.size()));
    std::vector<std::pair<VertexId,VertexId>> pairs;
    for(std::size_t i=0;i<mirrorIndices.size();++i)
        pairs.push_back({imported.flatIndex.vertexIds[i],
                         imported.flatIndex.vertexIds[mirrorIndices[i]]});
    VersionedInflateBrush brush(.012,{},std::move(pairs));
    const std::vector<BrushArea> areas{
        {{-.24,-.65,.69},.36,.3},{{.24,-.65,.69},.36,.3}};
    const auto up=brush.stamp(before,areas,32);
    brush.setStrengthScale(-2.0/3.0);
    const auto down=brush.stamp(before,areas,32);
    require(up.moved>0&&up.moved==down.moved&&
            up.baseline.movedVertices==down.baseline.movedVertices,
            "Inverse brush must affect identical source vertices");
    require(up.baseline.before==before&&down.baseline.before==before,
            "Both directions must share frozen pre-stamp normals");
    for(const auto id:up.baseline.movedVertices) {
        const Vec3 p=before->vertex(id).position;
        const Vec3 outward=up.index->vertex(id).position-p;
        const Vec3 inward=down.index->vertex(id).position-p;
        require(length(inward+outward*(2.0/3.0))<1e-12,
                "Deflate must be exactly -2/3 of same-normal Inflate");
    }
    const auto flat=reconstructFlatMesh(down.index->revision(),
                                       imported.flatIndex);
    for(std::size_t i=0;i<flat.vertices.size();++i) {
        const Vec3 a=flat.vertices[i],b=flat.vertices[mirrorIndices[i]];
        require(std::abs(a.x+b.x)<1e-10&&
                std::abs(a.y-b.y)<1e-10&&
                std::abs(a.z-b.z)<1e-10,
                "Inverse stamp must preserve exact mirrored symmetry");
        if(i==mirrorIndices[i])require(a.x==0,
                                      "On-plane IDs stay on X=0");
    }
    brush.setStrengthScale(1.0);
    const auto reset=brush.stamp(before,areas,32);
    for(const auto id:up.baseline.movedVertices)
        require(length(reset.index->vertex(id).position-
                       up.index->vertex(id).position)<1e-12,
                "Normal Inflate after inverse must restore initial strength");
    bool rejected=false;
    try{brush.setStrengthScale(0);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Zero strength scale must be rejected");
}

void testCurvatureFeedbackSmoothsBothSignedDirections() {
    // On an ordinary convex sphere, positive mean-curvature flow should
    // accelerate inward erosion relative to uncorrected Deflate. At the
    // bottom of a deliberate, reflected indentation, it must instead slow
    // erosion: the old outward-only multiplier would deepen that pocket.
    auto source=sphere(800);
    const auto mirrorIndices=symmetricSphereMirrors(
        static_cast<std::uint32_t>(source.vertices.size()));
    const Vec3 target=normalized(Vec3{-.24,-.65,.69});
    std::size_t center=0;
    for(std::size_t i=1;i<source.vertices.size();++i)
        if(dot(normalized(source.vertices[i]),target)>
           dot(normalized(source.vertices[center]),target))
            center=i;
    const auto partner=mirrorIndices[center];
    require(center!=partner,
            "Curvature test must use a real off-plane mirrored brush pair");

    const auto setup=[&](const Mesh& input) {
        const auto imported=partitionFlatMesh(input,32);
        auto before=RegionTopologyIndex::build(imported.revision);
        std::vector<std::pair<VertexId,VertexId>> pairs;
        for(std::size_t i=0;i<mirrorIndices.size();++i)
            pairs.push_back({imported.flatIndex.vertexIds[i],
                             imported.flatIndex.vertexIds[mirrorIndices[i]]});
        VersionedInflateBrush curved(.012,{true,1.0},pairs);
        VersionedInflateBrush plain(.012,{},std::move(pairs));
        curved.setStrengthScale(-2.0/3.0);
        plain.setStrengthScale(-2.0/3.0);
        const std::vector<BrushArea> areas{
            {input.vertices[center],.35,.3},
            {input.vertices[partner],.35,.3}};
        const auto smoothed=curved.stamp(before,areas,32);
        const auto uncorrected=plain.stamp(before,areas,32);
        const auto id=imported.flatIndex.vertexIds[center];
        const Vec3 point=before->vertex(id).position;
        const double smoothAmount=length(smoothed.index->vertex(id).position-point);
        const double plainAmount=length(uncorrected.index->vertex(id).position-point);
        require(smoothed.moved>0&&uncorrected.moved>0&&plainAmount>0,
                "Curvature regression must exercise an influenced vertex");
        return smoothAmount/plainAmount;
    };
    const double convexRatio=setup(source);
    require(convexRatio>1.01,
            "Convex peak must shrink faster under smoothing Deflate");
    // Push both corresponding vertices beneath their immediate ring.
    // This creates a genuine local concave trough without changing faces.
    source.vertices[center]=source.vertices[center]*.88;
    source.vertices[partner]=source.vertices[partner]*.88;
    const double concaveRatio=setup(source);
    require(concaveRatio<.99,
            "Concave trough must shrink more slowly, not deepen into honeycomb walls");
}

void testNoopAndFailure() {
    const auto original=sphere();
    const auto imported=partitionFlatMesh(original,32);
    auto index=RegionTopologyIndex::build(imported.revision);
    VersionedInflateBrush brush(.01);
    auto noHit=brush.stamp(index,std::vector<BrushArea>{
        {{100,100,100},.01,.3}},32);
    require(noHit.index==index && noHit.moved==0 &&
            noHit.commitStatistics.copiedDirectoryNodes==0,
            "Empty local stamp must not commit or copy anything");
    bool failed=false;
    try{(void)brush.stamp(index,std::vector<BrushArea>{
            {{0,0,0},-1,.3}},32);}
    catch(const std::invalid_argument&){failed=true;}
    require(failed,"Invalid areas rejected before mutation");
    require(index->revision().partitionCount()==
            imported.revision->partitionCount(),"Rejected edit has no side effects");
}
void testLegacyDefaultAvoidsFullBaseline() {
    auto source=sphere();
    auto adaptiveMesh=source;
    InflateBrush direct(.003,CurvatureSettings{true,.25});
    AdaptiveInflateBrush adaptive(adaptiveMesh,.003,
                                  CurvatureSettings{true,.25},{},{},false);
    direct.begin(source);
    adaptive.begin(adaptiveMesh);
    const std::vector<BrushArea> areas{{{-.25,-.65,.67},.30,.30}};
    for(unsigned stamp=0;stamp<12;++stamp) {
        direct.stamp(source,areas);
        adaptive.stamp(adaptiveMesh,areas);
        require(adaptive.lastDetails().preStampCopiedVertices==0,
                "Default interactive worker must never copy the pre-stamp array");
        compare(source,adaptiveMesh,0);
    }
    direct.end(source);
    adaptive.end(adaptiveMesh);
    auto legacy=sphere();
    RefineSettings refine{};
    refine.enabled=true;
    refine.maximumSplitPairsPerStamp=0;
    refine.optimizeFlips=false;
    AdaptiveInflateBrush withRemesh(legacy,.0001,{},{},refine,false);
    withRemesh.begin(legacy);
    withRemesh.stamp(legacy,areas);
    require(withRemesh.lastDetails().preStampCopiedVertices==
            legacy.vertices.size(),
            "Legacy directional remeshing retains full baseline until V6/V7");
    withRemesh.end(legacy);
}
}
int main() {
    try {
        testVersionedMatchesReference({});
        testVersionedMatchesReference({true,.25});
        testSymmetricInflate();
        testSignedDeflateUsesSameNormals();
        testCurvatureFeedbackSmoothsBothSignedDirections();
        testNoopAndFailure();
        testLegacyDefaultAvoidsFullBaseline();
        std::cout<<"Versioned Inflate local baseline tests passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Versioned Inflate regression failure: "<<error.what()<<'\n';
        return 1;
    }
    return 0;
}
