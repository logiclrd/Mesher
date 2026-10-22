#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_mixed_closure.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/sculpt_fixtures.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    FlatMeshIndex flat;
};
Fixture fixture(Mesh mesh) {
    auto imp=partitionFlatMesh(mesh,64);
    return {RegionTopologyIndex::build(imp.revision),imp.flatIndex};
}
FaceId nearest(const Fixture& f,Vec3 p) {
    FaceId best=0;
    double distance=1e99;
    for(const auto id:f.flat.faceIds) {
        const auto tri=f.root->face(id).vertices;
        const auto center=(f.root->vertex(tri[0]).position+
            f.root->vertex(tri[1]).position+
            f.root->vertex(tri[2]).position)/3;
        if(const auto d=length(center-p);d<distance) {
            best=id;distance=d;
        }
    }
    return best;
}
struct CapPosition {std::size_t left{},right{};};
CapPosition prism(Mesh& m,double x0,double x1,double radius) {
    constexpr std::array<std::array<double,2>,4> square{{
        {-1,-1},{1,-1},{1,1},{-1,1}
    }};
    const auto base=static_cast<std::uint32_t>(m.vertices.size());
    for(const auto x:{x0,x1})
        for(const auto yz:square)m.vertices.push_back({x,yz[0]*radius,yz[1]*radius});
    const auto lc=static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({x0,0,0});
    const auto rc=static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({x1,0,0});
    for(std::uint32_t k=0;k<4;++k) {
        const auto j=(k+1)%4;
        m.triangles.push_back({base+k,base+j,base+j+4});
        m.triangles.push_back({base+k,base+j+4,base+k+4});
    }
    const auto left=m.triangles.size();
    for(std::uint32_t k=0;k<4;++k)
        m.triangles.push_back({lc,base+(k+1)%4,base+k});
    const auto right=m.triangles.size();
    for(std::uint32_t k=0;k<4;++k)
        m.triangles.push_back({rc,base+k+4,base+(k+1)%4+4});
    return {left,right};
}
void addCapFaces(std::vector<std::size_t>& indices,CapPosition cap,bool right) {
    const auto start=right?cap.right:cap.left;
    for(std::size_t i=0;i<4;++i)indices.push_back(start+i);
}
RegionTopologyIndex::CollisionPair contact(FaceId a,FaceId b) {
    if(a>b)std::swap(a,b);
    return {a,b,false};
}
void mixedAndTampered() {
    Mesh m;
    const auto left=prism(m,-2,0,1),right=prism(m,.5,2.5,1);
    const auto remote=prism(m,6,8,.5);
    std::vector<std::size_t> removed;
    addCapFaces(removed,left,true);
    addCapFaces(removed,right,false);
    addCapFaces(removed,remote,false);
    const auto f=fixture(std::move(m));
    RepairExcisionPreview proposal;
    proposal.complete=true;
    for(const auto index:removed)
        proposal.proposedFaces.push_back(f.flat.faceIds[index]);
    std::sort(proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    const std::set<FaceId> deleted(proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    for(const auto id:f.flat.vertexIds) {
        const auto& incident=f.root->vertex(id).incidentFaces;
        if(!incident.empty()&&
           std::all_of(incident.begin(),incident.end(),
               [&](FaceId id){return deleted.contains(id);}))
            proposal.proposedVertices.push_back(id);
    }
    const auto rings=planRemovalContours(*f.root,proposal.proposedFaces);
    proposal.contours=rings.contours;
    for(const auto& loop:proposal.contours)
        proposal.boundaryEdges+=loop.vertexIds.size();
    const auto cut=applyPrivateExcision(f.root,proposal);
    const auto contacts=std::vector<RegionTopologyIndex::CollisionPair>{
        contact(f.flat.faceIds[left.right],f.flat.faceIds[right.left])};
    const auto plan=planPrivateAutomaticClosure(*f.root,cut,proposal,contacts);
    require(plan.ready()&&plan.cappedContours.size()==1&&
            plan.zipperPairs.size()==1,"R06-M04A mixed fixture requires an M03 cap+zipper choice");
    const auto staging=stagePrivateMixedClosure(cut,proposal,plan);
    require(staging.capLoops==1&&staging.zipperPairs==1&&
            staging.capTriangles>0&&staging.zipperTriangles>0&&
            staging.boundarySidesCovered==proposal.boundaryEdges&&
            cut.index->retiredFaceCount()==proposal.proposedFaces.size()&&
            f.root->retiredFaceCount()==0,
            "R06-M04A must stage all actual oriented rims without mutation");

    auto invalid=plan;
    invalid.cappedContours.push_back(plan.zipperPairs.front().first);
    bool refused=false;
    try{(void)stagePrivateMixedClosure(cut,proposal,invalid);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R06-M04A must refuse duplicated cap/zipper rim before execution");

    invalid=plan;
    invalid.regions.front().zippers.clear();
    refused=false;
    try{(void)stagePrivateMixedClosure(cut,proposal,invalid);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R06-M04A must refuse forged global cross-region seam");

    auto bounds=MixedClosureStageSettings{};
    bounds.maxNewTriangles=1;
    refused=false;
    try{(void)stagePrivateMixedClosure(cut,proposal,plan,bounds);}
    catch(const std::length_error&){refused=true;}
    require(refused,"R06-M04A must bound combined new geometry");
    const auto closed=applyPrivateMixedClosure(cut,proposal,plan);
    require(closed.index && closed.index!=cut.index &&
            !closed.greenFaces.empty() && !closed.blueFaces.empty() &&
            closed.cappedLoops==1 && closed.zipperPairs==1 &&
            closed.minimumCapQuality>=MixedPrivateClosureLimits{}.minCapQuality &&
            closed.minimumZipperQuality>=MixedPrivateClosureLimits{}.minZipperQuality &&
            f.root->retiredFaceCount()==0 && cut.index->retiredFaceCount()==
                proposal.proposedFaces.size(),
            "R06-M04B must finish a genuinely mixed closed manifold in one private root");
    require(closed.spatialCapLoops==0&&
            closed.spatialDensitySplits==0&&
            closed.maximumFinalSpatialCapEdgeRatio==0,
            "R06-M06C4a spatial density must not touch legacy mixed caps and zippers");
    auto legacyDensity=MixedPrivateClosureLimits{};
    legacyDensity.spatialCapDensityEdgeFactor=1.05;
    legacyDensity.maxOptionalSpatialDensitySplits=0;
    const auto unchanged=applyPrivateMixedClosure(cut,proposal,plan,legacyDensity);
    require(unchanged.index&&
            unchanged.spatialDensitySplits==0&&
            unchanged.greenFaces.size()==closed.greenFaces.size()&&
            unchanged.blueFaces.size()==closed.blueFaces.size()&&
            unchanged.minimumCapQuality>=legacyDensity.minCapQuality,
            "R06-M06C4a optional density settings must not perturb non-spatial repair");
    auto invalidDensity=MixedPrivateClosureLimits{};
    invalidDensity.spatialCapDensityEdgeFactor=0.95;
    bool badDensityRefused=false;
    try{(void)applyPrivateMixedClosure(cut,proposal,plan,invalidDensity);}
    catch(const std::invalid_argument&){badDensityRefused=true;}
    require(badDensityRefused&&f.root->retiredFaceCount()==0,
            "R06-M06C4a invalid optional spatial density limits must refuse safely");
    for(const auto id:closed.greenFaces)
        require(closed.index->maybeFace(id)&&!cut.index->maybeFace(id),
                "R06-M04B green cap faces need fresh IDs");
    for(const auto id:closed.blueFaces)
        require(closed.index->maybeFace(id)&&!cut.index->maybeFace(id),
                "R06-M04B blue seam faces need fresh IDs");
    auto bound=MixedPrivateClosureLimits{};
    bound.maxNewFaces=1;
    refused=false;
    try{(void)applyPrivateMixedClosure(cut,proposal,plan,bound);}
    catch(const std::length_error&){refused=true;}
    require(refused && f.root->retiredFaceCount()==0,
            "R06-M04B exhausted work must not alter original immutable revision");
    std::cout<<"[r06-m04ab] real mixed cap+zipper private closed, stable IDs preserved PASS\n";
}
void narrowZipperMustNeverReturnUnderQuality() {
    Mesh m;
    const auto left=prism(m,-2,0,1);
    const auto right=prism(m,.1,2.1,1);
    std::vector<std::size_t> removed;
    addCapFaces(removed,left,true);
    addCapFaces(removed,right,false);
    const auto f=fixture(std::move(m));
    RepairExcisionPreview proposal;
    proposal.complete=true;
    for(const auto index:removed)
        proposal.proposedFaces.push_back(f.flat.faceIds.at(index));
    std::sort(proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    const std::set<FaceId> deleted(proposal.proposedFaces.begin(),
                                   proposal.proposedFaces.end());
    for(const auto id:f.flat.vertexIds) {
        const auto& incident=f.root->vertex(id).incidentFaces;
        if(!incident.empty()&&std::all_of(
                incident.begin(),incident.end(),
                [&](FaceId id){return deleted.contains(id);}))
            proposal.proposedVertices.push_back(id);
    }
    const auto ring=planRemovalContours(*f.root,proposal.proposedFaces);
    proposal.contours=ring.contours;
    for(const auto& loop:proposal.contours)
        proposal.boundaryEdges+=loop.vertexIds.size();
    const auto cut=applyPrivateExcision(f.root,proposal);
    const auto links=std::vector<RegionTopologyIndex::CollisionPair>{
        contact(f.flat.faceIds[left.right],f.flat.faceIds[right.left])};
    const auto plan=planPrivateAutomaticClosure(*f.root,cut,proposal,links);
    require(plan.ready()&&plan.zipperPairs.size()==1,
            "R06-M04B narrow seam must enter actual mixed-refinement gate");
    const auto policy=MixedPrivateClosureLimits{};
    bool refused=false;
    try {
        const auto result=applyPrivateMixedClosure(cut,proposal,plan);
        require(result.index&&
                result.minimumZipperQuality>=policy.minZipperQuality &&
                !result.blueFaces.empty(),
                "R06-M04B narrow seam may succeed only with fully audited quality");
    }catch(const std::length_error&) {
        refused=true;
    }catch(const std::logic_error&) {
        refused=true;
    }
    require(f.root->retiredFaceCount()==0 &&
            cut.index->retiredFaceCount()==proposal.proposedFaces.size(),
            "R06-M04B narrow seam refusal cannot mutate either input root");
    std::cout<<"[r06-m04b] narrow seam "
             <<(refused?"refused safely":"passed audited quality")
             <<" without partial publication PASS\n";
}
void planarMirroredCapMustNotUseUnpairedEarDiagonal() {
    Mesh m;
    const std::vector<Vec3> rim{
        {0,1,0},{-.20,.8,0},{-.35,.3,0},
        {-.35,-.3,0},{-.20,-.8,0},{0,-1,0},
        {.20,-.8,0},{.35,-.3,0},{.35,.3,0},{.20,.8,0}
    };
    for(const auto p:rim)m.vertices.push_back(p);
    m.vertices.push_back({0,0,-.45});
    m.vertices.push_back({0,0,.45});
    std::vector<std::size_t> remove;
    for(std::uint32_t i=0;i<rim.size();++i) {
        const auto j=(i+1)%static_cast<std::uint32_t>(rim.size());
        m.triangles.push_back({10,i,j});
        remove.push_back(m.triangles.size());
        m.triangles.push_back({11,j,i});
    }
    const auto source=fixture(std::move(m));
    RepairExcisionPreview proposal;
    proposal.complete=true;
    for(const auto i:remove)
        proposal.proposedFaces.push_back(source.flat.faceIds[i]);
    std::sort(proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    for(const auto id:source.flat.vertexIds) {
        const auto& incident=source.root->vertex(id).incidentFaces;
        if(!incident.empty()&&std::all_of(incident.begin(),incident.end(),
            [&](FaceId v) {
                return std::binary_search(proposal.proposedFaces.begin(),
                                          proposal.proposedFaces.end(),v);
            }))proposal.proposedVertices.push_back(id);
    }
    const auto loops=planRemovalContours(*source.root,proposal.proposedFaces);
    proposal.contours=loops.contours;
    for(const auto& loop:proposal.contours)
        proposal.boundaryEdges+=loop.vertexIds.size();
    require(proposal.contours.size()==1&&proposal.boundaryEdges==rim.size(),
            "R06-M06C4c1 synthetic planar mirror disk must have one true rim");
    const auto cut=applyPrivateExcision(source.root,proposal);
    AutoClosurePlan plan;
    plan.status=AutoClosurePlanStatus::Ready;
    plan.cappedContours={0};
    AutoClosureRegion region;
    region.contours={0};
    region.caps={0};
    plan.regions.push_back(region);
    MixedClosureStageSettings bounds;
    bounds.requireReflectedGeometry=true;
    const auto staged=stagePrivateMixedClosure(cut,proposal,plan,bounds);
    require(staged.capLoops==1&&staged.spatialCapLoops==1&&
            staged.usedSpatialCapFallback&&
            staged.boundarySidesCovered==rim.size(),
            "R06-M06C4c1 even projected-simple mirrored caps require mirror-paired spatial triangulation");
    for(const auto& face:staged.faces) {
        if(face.kind!=MixedClosureFaceKind::Cap)continue;
        const auto a=staged.positions.at(face.vertices[0]).x;
        const auto b=staged.positions.at(face.vertices[1]).x;
        const auto c=staged.positions.at(face.vertices[2]).x;
        require(!(std::min({a,b,c})<0&&std::max({a,b,c})>0),
                "R06-M06C4c1 mirrored cap may not straddle plane without seam");
    }
    std::cout<<"[r06-m06c4c1] projected-simple symmetric single rim stages mirrored spatial patch PASS\\n";
}

void physicalMirroredZipperStaging() {
    // User's physical 2026-10-22 third run: one 15-vertex negative-X
    // contour plus its exactly reflected reverse-wound 15-vertex mate.
    // The cone walls below are ONLY a synthetic test harness; no
    // physical full-scene replay or source-face provenance is claimed.
    const std::vector<Vec3> left{
        {-0.031007713054145417,-1.1836744611350298,0.280200847381798},
        {-0.022457743732093041,-1.2317527007027365,0.32452651759714424},
        {-0.022314532795767769,-1.2597746200274036,0.37194167359900848},
        {-0.016718918648627897,-1.2434259568702559,0.40289958689434363},
        {-0.0079178477591305037,-1.2232021816141574,0.42835521840216018},
        {-0.026980549887205982,-1.2355194192123422,0.47076800879725617},
        {-0.021879171174721771,-1.1772709040242231,0.49948713115248411},
        {-0.033262887944756868,-1.1189238188370849,0.51852404125459983},
        {-0.023925537979033448,-1.0879203026713191,0.48471449738103473},
        {-0.020833989196607873,-1.0614813915793981,0.44958828940649598},
        {-0.007896840246730423,-1.0893901933528187,0.41869076523125487},
        {-0.012132250212578275,-1.0649160149680836,0.38024382853815925},
        {-0.012774357648999658,-1.0857760696177319,0.34485227991553014},
        {-0.017453788743390264,-1.1069933254809998,0.31003298589811173},
        {-0.034710167704590698,-1.1260278547422924,0.26737804206477189}
    };
    const auto n=static_cast<std::uint32_t>(left.size());
    Mesh m;
    std::vector<std::size_t> removed;
    for(unsigned half=0;half<2;++half) {
        const auto start=static_cast<std::uint32_t>(m.vertices.size());
        Vec3 centroid{};
        for(std::uint32_t i=0;i<n;++i) {
            auto v=left[half?(n-i)%n:i];
            if(half)v.x=-v.x;
            m.vertices.push_back(v);
            centroid=centroid+v;
        }
        centroid=centroid/static_cast<double>(n);
        const auto far=static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({half?.20:-.20,centroid.y,centroid.z});
        const auto near=static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({half?.002:-.002,centroid.y,centroid.z});
        for(std::uint32_t i=0;i<n;++i) {
            const auto j=(i+1)%n;
            m.triangles.push_back({far,start+i,start+j});
            removed.push_back(m.triangles.size());
            m.triangles.push_back({near,start+j,start+i});
        }
    }
    const auto source=fixture(std::move(m));
    RepairExcisionPreview proposal;
    proposal.complete=true;
    for(const auto id:removed)
        proposal.proposedFaces.push_back(source.flat.faceIds[id]);
    std::sort(proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    for(const auto id:source.flat.vertexIds) {
        const auto& incident=source.root->vertex(id).incidentFaces;
        if(!incident.empty()&&std::all_of(incident.begin(),incident.end(),
            [&](FaceId v) {
                return std::binary_search(proposal.proposedFaces.begin(),
                                          proposal.proposedFaces.end(),v);
            }))proposal.proposedVertices.push_back(id);
    }
    const auto rims=planRemovalContours(*source.root,proposal.proposedFaces);
    proposal.contours=rims.contours;
    for(const auto& rim:proposal.contours)
        proposal.boundaryEdges+=rim.vertexIds.size();
    require(proposal.contours.size()==2&&proposal.boundaryEdges==30,
            "R06-M06C4c2 physical mirrored pair fixture must produce two R03 rings");
    const auto cut=applyPrivateExcision(source.root,proposal);
    AutoClosurePlan plan;
    plan.status=AutoClosurePlanStatus::Ready;
    plan.zipperPairs={{0,1}};
    AutoClosureRegion region;
    region.contours={0,1};
    region.zippers={{0,1}};
    plan.regions.push_back(region);
    MixedClosureStageSettings bounds;
    bounds.requireReflectedGeometry=true;
    const auto staged=stagePrivateMixedClosure(cut,proposal,plan,bounds);
    require(staged.zipperPairs==1&&staged.usedMirroredSeamZipper&&
            staged.mirroredSeamZipperVertices==n&&
            staged.zipperTriangles==4*n&&
            staged.boundarySidesCovered==30,
            "R06-M06C4c2 must stage one exact mirror-paired zipper with real seam");
    for(const auto& f:staged.faces)
        if(f.kind==MixedClosureFaceKind::Zipper) {
            const auto x0=staged.positions.at(f.vertices[0]).x;
            const auto x1=staged.positions.at(f.vertices[1]).x;
            const auto x2=staged.positions.at(f.vertices[2]).x;
            require(!(std::min({x0,x1,x2})<0&&std::max({x0,x1,x2})>0),
                    "R06-M06C4c2 a staged zipper face may not cross X=0 without seam");
        }
    // Deliberate mismatch cannot be healed by approximate welding.
    auto corrupt=proposal;
    const auto old=corrupt.contours[1].vertexIds;
    std::rotate(corrupt.contours[1].vertexIds.begin(),
                corrupt.contours[1].vertexIds.begin()+1,
                corrupt.contours[1].vertexIds.end());
    bool refused=false;
    // Rotating an otherwise valid contour is legal, not a mismatch:
    // the algorithm must accept it with exact reflected matching.
    const auto rotated=stagePrivateMixedClosure(cut,corrupt,plan,bounds);
    require(rotated.zipperTriangles==4*n,
            "R06-M06C4c2 rotated start vertex must not defeat mirrored loop matching");
    corrupt.contours[1].vertexIds=old;
    std::swap(corrupt.contours[1].vertexIds[1],
              corrupt.contours[1].vertexIds[2]);
    try{(void)stagePrivateMixedClosure(cut,corrupt,plan,bounds);}
    catch(const std::logic_error&){refused=true;}
    require(refused,
            "R06-M06C4c2 twisted opposing loop must refuse rather than weld mismatched topology");
    require(source.root->retiredFaceCount()==0,
            "R06-M06C4c2 local staging cannot publish or mutate original source");
    std::cout<<"[r06-m06c4c2] physical 15+15 rim stages two reflected halves around real seam PASS\\n";
}

void tightTorus() {
    const auto f=fixture(generateTorusFixture(5000,1,5));
    const auto contacts=std::vector<RegionTopologyIndex::CollisionPair>{
        contact(nearest(f,{.12,0,0}),nearest(f,{-.12,0,0}))};
    const auto proposal=previewApproximateExcision(*f.root,contacts);
    require(proposal.complete&&proposal.contours.size()==2,
            "R06-M04A torus must produce two real cut rims");
    const auto cut=applyPrivateExcision(f.root,proposal);
    const auto plan=planPrivateAutomaticClosure(*f.root,cut,proposal,contacts);
    require(plan.ready()&&plan.cappedContours.size()==2,
            "R06-M04A tight torus must be capped by M03");
    const auto staged=stagePrivateMixedClosure(cut,proposal,plan);
    require(staged.capLoops==2&&staged.zipperPairs==0&&
            staged.boundarySidesCovered==cut.boundaryEdges,
            "R06-M04A torus must have complete oriented two-cap closure");
    const auto closed=applyPrivateMixedClosure(cut,proposal,plan);
    require(closed.index && closed.greenFaces.size()>2 &&
            closed.blueFaces.empty() && closed.cappedLoops==2,
            "R06-M04B tight torus must close with real caps and no zipper");
    std::set<std::pair<VertexId,VertexId>> edges;
    std::size_t vertices=0,faces=0;
    closed.index->revision().forEachPartition([&](
        PartitionId,const MeshPartition& part) {
        vertices+=part.vertices.size();
        faces+=part.faces.size();
        for(const auto& f:part.faces)
            for(unsigned k=0;k<3;++k) {
                auto a=f.vertices[k],b=f.vertices[(k+1)%3];
                if(a>b)std::swap(a,b);
                edges.insert({a,b});
            }
    });
    require(vertices+faces-edges.size()==0,
            "R06-M04B unmodified torus disk cuts must preserve its genus-one handle");
    std::cout<<"[r06-m04ab] unmodified torus local disk caps preserve Euler 0 PASS\n";
}
}
int main() {
    try{
        mixedAndTampered();
        narrowZipperMustNeverReturnUnderQuality();
        tightTorus();
        physicalMirroredZipperStaging();
        planarMirroredCapMustNotUseUnpairedEarDiagonal();
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"R06-M04A mixed staging FAILED: "<<error.what()<<"\n";
        return 1;
    }
}
