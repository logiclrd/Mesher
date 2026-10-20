#include "mesher/async_sculpt.hpp"
#include "mesher/desktop_merge_fixture.hpp"
#include "mesher/desktop_merge_input.hpp"
#include "mesher/partition_render.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace mesher;
namespace {
void require(bool valid,const std::string& detail) {
    if(!valid)throw std::runtime_error("M54 structural audit: "+detail);
}
using Edge=std::pair<VertexId,VertexId>;
using Coordinate=std::array<float,3>;
using DrawEdge=std::pair<Coordinate,Coordinate>;
struct EdgeUse {FaceId face{}; bool forward{};};

Edge canonicalEdge(VertexId a,VertexId b) {
    return std::minmax(a,b);
}
DrawEdge drawEdge(Vec3 a,Vec3 b) {
    Coordinate ca{float(a.x),float(a.y),float(a.z)};
    Coordinate cb{float(b.x),float(b.y),float(b.z)};
    if(cb<ca)std::swap(ca,cb);
    return {ca,cb};
}
DrawEdge drawEdge(PartitionRenderVertex a,PartitionRenderVertex b) {
    Coordinate ca{a.px,a.py,a.pz},cb{b.px,b.py,b.pz};
    if(cb<ca)std::swap(ca,cb);
    return {ca,cb};
}
struct Counts {
    std::size_t vertices{},faces{},edges{},components{},crossings{},ambiguous{};
    std::size_t openEdges{},nonmanifoldEdges{},sameDirectionEdges{};
    std::size_t degenerateFaces{},duplicateFaces{},renderDuplicateEdges{},ghostRenderEdges{};
    std::vector<double> volumes;
};
Counts audit(const RegionTopologyIndex& indexed,const char* tag,
             bool initial,bool symmetric,std::size_t expectedComponents) {
    Counts counts;
    std::set<VertexId> vertices;
    std::set<FaceId> faces;
    std::set<std::array<VertexId,3>> sortedFaces;
    std::map<Edge,std::vector<EdgeUse>> edges;
    std::map<FaceId,std::vector<FaceId>> neighbors;
    std::map<FaceId,std::array<VertexId,3>> faceCorners;
    std::map<FaceId,double> faceSignedVolumes;
    std::set<DrawEdge> expectedDrawEdges;
    indexed.revision().forEachPartition([&](
            PartitionId,const MeshPartition& p) {
        for(const auto& v:p.vertices) {
            require(vertices.insert(v.id).second,
                    "duplicate partition-owned stable vertex ID");
            const Vec3 pos=indexed.vertex(v.id).position;
            require(std::isfinite(pos.x)&&std::isfinite(pos.y)&&
                    std::isfinite(pos.z),"nonfinite indexed vertex");
        }
        for(const auto& f:p.faces) {
            ++counts.faces;
            require(faces.insert(f.id).second,"duplicate stable FaceId");
            auto unique=f.vertices;
            std::sort(unique.begin(),unique.end());
            if(!sortedFaces.insert(unique).second)++counts.duplicateFaces;
            const Vec3 a=indexed.vertex(f.vertices[0]).position;
            const Vec3 b=indexed.vertex(f.vertices[1]).position;
            const Vec3 c=indexed.vertex(f.vertices[2]).position;
            const Vec3 crossProduct=cross(b-a,c-a);
            if(length(crossProduct)<=1e-15)++counts.degenerateFaces;
            faceSignedVolumes[f.id]=dot(a,cross(b,c))/6.0;
            faceCorners.emplace(f.id,f.vertices);
            neighbors[f.id];
            for(unsigned i=0;i<3;++i) {
                const auto va=f.vertices[i],vb=f.vertices[(i+1)%3];
                edges[canonicalEdge(va,vb)].push_back({f.id,va<vb});
                expectedDrawEdges.insert(drawEdge(
                    indexed.vertex(va).position,indexed.vertex(vb).position));
            }
        }
    });
    counts.vertices=vertices.size();
    counts.edges=edges.size();
    for(const auto& [key,sides]:edges) {
        const auto canonical=indexed.edgeSides({key.first,key.second});
        require(canonical.size()==sides.size(),
                "indexed edge incidence disagrees with independently reconstructed original faces");
        if(sides.size()==1)++counts.openEdges;
        if(sides.size()!=2)++counts.nonmanifoldEdges;
        if(sides.size()==2) {
            if(sides[0].forward==sides[1].forward)
                ++counts.sameDirectionEdges;
            neighbors[sides[0].face].push_back(sides[1].face);
            neighbors[sides[1].face].push_back(sides[0].face);
        }
    }
    std::set<FaceId> remaining=faces;
    std::vector<std::size_t> componentVertices;
    std::vector<std::size_t> componentEdges;
    while(!remaining.empty()) {
        ++counts.components;
        std::set<FaceId> component;
        std::vector<FaceId> todo{*remaining.begin()};
        remaining.erase(todo.front());
        double volume=0.0;
        for(std::size_t i=0;i<todo.size();++i) {
            const auto id=todo[i];
            component.insert(id);
            volume+=faceSignedVolumes.at(id);
            for(const auto other:neighbors.at(id)) {
                if(remaining.erase(other))todo.push_back(other);
            }
        }
        std::set<VertexId> inComponent;
        std::set<Edge> eComponent;
        for(const auto id:component) {
            const auto corners=faceCorners.at(id);
            for(unsigned i=0;i<3;++i) {
                inComponent.insert(corners[i]);
                eComponent.insert(canonicalEdge(corners[i],corners[(i+1)%3]));
            }
        }
        require(inComponent.size()+component.size()==eComponent.size()+2,
                "surface component Euler characteristic is not 2");
        counts.volumes.push_back(volume);
    }
    // Check this exact root's staging geometry, NOT only topology. Every
    // submitted line must correspond to exactly one live indexed edge;
    // stale/cross-root ghost wires are a different rendering defect.
    std::set<DrawEdge> renderedEdges;
    std::size_t renderedSolidFaces=0;
    indexed.revision().forEachPartition([&](
            PartitionId owner,const MeshPartition& p) {
        const auto render=buildPartitionRenderGeometry(indexed,owner);
        require(render.solid.size()==p.faces.size()*3,
                "per-partition solid triangles do not match live source faces");
        require(render.wire.size()%2==0,
                "an odd number of wire vertices leaves an unpaired segment");
        renderedSolidFaces+=render.solid.size()/3;
        for(std::size_t i=0;i<render.wire.size();i+=2) {
            const auto line=drawEdge(render.wire[i],render.wire[i+1]);
            if(!expectedDrawEdges.contains(line))++counts.ghostRenderEdges;
            if(!renderedEdges.insert(line).second)++counts.renderDuplicateEdges;
        }
    });
    require(renderedSolidFaces==counts.faces,
            "rendered staging emitted missing/extra solid faces");
    require(renderedEdges.size()==expectedDrawEdges.size()&&
            counts.ghostRenderEdges==0&&counts.renderDuplicateEdges==0,
            "render staging has ghost, duplicate or missing visual wire segments");
    // This is a diagnostic test of the real accepted root; it may be
    // deliberately more comprehensive than the finite live worker query.
    CollisionQueryLimits limit;
    limit.maxSeedFaces=20000;
    limit.maxBoxTests=2000000;
    limit.maxPartitionTests=2000000;
    limit.maxCandidateTriangles=2000000;
    limit.maxNarrowPhaseTests=2000000;
    limit.maxReportedContacts=200000;
    const std::vector<FaceId> all(faces.begin(),faces.end());
    const auto contacts=indexed.triangleIntersections({},all,nullptr,limit);
    counts.crossings=contacts.size();
    for(const auto& contact:contacts)
        if(contact.ambiguous)++counts.ambiguous;
    std::cout<<"[M54] "<<tag
             <<" V="<<counts.vertices<<" E="<<counts.edges
             <<" F="<<counts.faces<<" components="<<counts.components
             <<" volume=[";
    for(double volume:counts.volumes)std::cout<<volume<<",";
    std::cout<<"] open="<<counts.openEdges
             <<" nonmanifold="<<counts.nonmanifoldEdges
             <<" same-direction="<<counts.sameDirectionEdges
             <<" degenerate="<<counts.degenerateFaces
             <<" duplicate-face="<<counts.duplicateFaces
             <<" crossings="<<counts.crossings
             <<" ambiguous="<<counts.ambiguous
             <<" ghost-wire="<<counts.ghostRenderEdges
             <<" duplicate-wire="<<counts.renderDuplicateEdges
             <<" render-wire="<<renderedEdges.size()<<'\n';
    require(counts.openEdges==0&&counts.nonmanifoldEdges==0&&
            counts.sameDirectionEdges==0&&counts.duplicateFaces==0&&
            counts.degenerateFaces==0,
            "true source mesh has an open/nonmanifold/reversed/duplicate/degenerate surface");
    require(counts.components==expectedComponents,
            "unexpected number of closed connected original/merged components");
    for(double v:counts.volumes)
        require(std::abs(v)>1e-8,"closed shell has nearly zero enclosed volume");
    if(symmetric) {
        require(counts.volumes.size()%2==0,
                "reflected source always has even number of shells");
        const auto sorted=counts.volumes;
        for(const auto v:sorted) {
            const auto matches=std::count_if(sorted.begin(),sorted.end(),
                          [v](double other){return std::abs(v-other)<1e-6;});
            require(matches>=2,"reflected shells must have equal signed volume partner");
        }
    }
    if(initial)
        require(counts.crossings>0,
                "the initial source fixture must actually intersect: audit positive control");
    else
        require(counts.crossings==0,
                "the accepted finite Boolean root retains nonadjacent/ambiguous triangle intersections");
    return counts;
}

std::shared_ptr<const RegionTopologyIndex> waitAccepted(
        AsyncSculptWorker& worker,
        const std::shared_ptr<const RegionTopologyIndex>& original) {
    std::shared_ptr<const RegionTopologyIndex> displayed;
    bool dirty=false;
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
    while(std::chrono::steady_clock::now()<deadline) {
        if(const auto frame=worker.takeSnapshot()) {
            if(frame->index&&frame->index!=original) {
                displayed=frame->index;
                dirty=!frame->dirtyPartitions.empty();
            }
        }
        const auto status=worker.status();
        if(displayed&&dirty&&displayed==worker.initialRevision()&&
           status.liveMergeAccepted==1&&status.liveMergeRefusals==0&&
           status.pendingLiveMergeRequests==0&&status.history.undoDepth==1)
            return displayed;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto status=worker.status();
    throw std::runtime_error("M54 worker did not produce exactly one "
        "published dirty accepted original-root union (accepted="+
        std::to_string(status.liveMergeAccepted)+", refused="+
        std::to_string(status.liveMergeRefusals)+
        ", refusal="+status.liveMergeRefusal+")");
}
void waitHistory(AsyncSculptWorker& worker,
                 const std::shared_ptr<const RegionTopologyIndex>& expected,
                 std::size_t undoDepth,std::size_t redoDepth) {
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(25);
    while(std::chrono::steady_clock::now()<deadline) {
        const auto frame=worker.takeSnapshot();
        const auto s=worker.status();
        if(frame&&frame->index==expected&&
           worker.initialRevision()==expected&&
           s.history.undoDepth==undoDepth&&
           s.history.redoDepth==redoDepth)return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("M54 undo/redo did not publish expected exact immutable root");
}
void reflected() {
    auto source=desktopMergeReflectedFirstOriginal();
    auto originals=source.firstFaces;
    SculptCollisionSettings policy;
    policy.physicalExact=true;
    policy.originalTerminal.maximumProtectedDeviationInEdges=0;
    AsyncSculptWorker worker(std::move(source.checkpoint),
        .025,.006,.2,{},{},{},{},policy,originals);
    const auto initial=worker.initialRevision();
    audit(*initial,"reflected before",true,true,4);
    // The actual M51 desktop controller supplies exactly the same stable
    // original source token and complete named 16-FaceId request.
    DesktopMergeInput ui;
    require(ui.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn,
            "reflected GUI starts OFF until deliberately turned ON");
    require(ui.nominateFixtureOriginals(initial,initial,originals,true)==
                DesktopMergeInput::Result::Selected,
            "reflected UI A-key explicitly nominates original faces");
    auto submission=ui.submit(initial,true,true);
    require(submission.result==DesktopMergeInput::Result::Queued,
            "reflected UI explicitly submits first-original expected root");
    worker.mergeDiscoveredContacts(std::move(submission.expected),
                                   std::move(submission.originals),
                                   desktopMergeExactLimits(true));
    const auto after=waitAccepted(worker,initial);
    audit(*after,"reflected accepted",false,true,2);
    require(after!=initial,"accepted Boolean must allocate new immutable root");
    worker.undo();
    waitHistory(worker,initial,0,1);
    audit(*worker.initialRevision(),"reflected undo",true,true,4);
    worker.redo();
    waitHistory(worker,after,1,0);
    audit(*worker.initialRevision(),"reflected redo",false,true,2);
}
void asymmetric() {
    Mesh mesh=desktopMergeAsymmetricTetraFixture();
    const auto imported=partitionFlatMesh(mesh,256);
    const auto originalFaces=imported.flatIndex.faceIds;
    SculptCollisionSettings policy;
    policy.physicalExact=true;
    policy.originalTerminal.maximumProtectedDeviationInEdges=0;
    AsyncSculptWorker worker(mesh,.025,.006,.2,{},{},{},false,{},policy);
    const auto initial=worker.initialRevision();
    audit(*initial,"asymmetric before",true,false,2);
    DesktopMergeInput ui;
    require(ui.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn,
            "asymmetric UI starts OFF until deliberately ON");
    require(ui.nominateFixtureOriginals(initial,initial,originalFaces,true)==
                DesktopMergeInput::Result::Selected,
            "asymmetric A action selects exact eight original faces");
    auto submission=ui.submit(initial,true,true);
    require(submission.result==DesktopMergeInput::Result::Queued,
            "asymmetric desktop source IDs and immutable expected root");
    worker.mergeDiscoveredContacts(std::move(submission.expected),
                                   std::move(submission.originals),
                                   desktopMergeExactLimits(false));
    auto after=waitAccepted(worker,initial);
    audit(*after,"asymmetric accepted",false,false,1);
}
} // namespace

int main() {
    try {
        asymmetric();
        reflected();
        std::cout<<"M54 independently audited accepted desktop unions and "
                    "source-consistent render staging\n";
    }catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
