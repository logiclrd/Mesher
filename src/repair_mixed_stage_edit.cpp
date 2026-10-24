#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_mixed_edges.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
using Edge=MixedClosureEdge;
Edge edge(VertexId a,VertexId b) {return canonicalMixedEdge(a,b);}
bool cyclic(PrivateRepairTriangle a,PrivateRepairTriangle b) {
    return (a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2])||
           (a[0]==b[1]&&a[1]==b[2]&&a[2]==b[0])||
           (a[0]==b[2]&&a[1]==b[0]&&a[2]==b[1]);
}
bool sameCorners(PrivateRepairTriangle a,PrivateRepairTriangle b) {
    std::sort(a.begin(),a.end());
    std::sort(b.begin(),b.end());
    return a==b;
}
} // namespace

MixedClosureStaging::MixedClosureStaging(
    std::shared_ptr<const RegionTopologyIndex> source,
    const std::vector<RepairContour>& contours) {
    sourceView=std::make_shared<MixedClosureSourceView>(std::move(source));
    positions.bind(sourceView);
    for(const auto& loop:contours) {
        if(loop.vertexIds.size()<3)
            throw std::logic_error("R06-M15A initial R03 rim has fewer than three vertices");
        initialRimPaths.push_back(loop.vertexIds);
    }
}
VertexId MixedClosureStaging::addVertex(
    MutableMeshView& privateView,Vec3 position,PartitionId owner,
    std::size_t maxGeneratedVertices) {
    if(!sourceView)throw std::logic_error("R06-M15A unbound staging source");
    if(!maxGeneratedVertices||
       positions.newVertexPositions()>=maxGeneratedVertices)
        throw std::length_error("R06-M15A private generated-vertex budget");
    // The private view allocates a monotonically unique VertexId. No
    // coordinate search and no arbitrary source/provisional ID alias.
    const auto id=privateView.addVertex(owner,position);
    if(positions.contains(id))
        throw std::logic_error("R06-M15A private allocator reused a vertex ID");
    positions.emplace(id,position);
    vertexOwners.emplace(id,owner);
    return id;
}
std::size_t MixedClosureStaging::trackSurvivor(
    FaceId id,std::size_t maxFaces) {
    if(!sourceView)throw std::logic_error("R06-M15A unbound staging source");
    for(std::size_t i=0;i<faces.size();++i)
        if(faces[i].stableId==id)return i;
    if(faces.size()>=maxFaces)
        throw std::length_error("R06-M15A finite survivor tracking limit");
    const auto& sourceFace=sourceView->face(id);
    for(const auto v:sourceFace.vertices) {
        positions.emplace(v,sourceView->position(v));
        vertexOwners.emplace(v,sourceView->owner(v));
    }
    survivingFaceIds.push_back(id);
    faces.push_back({id,sourceFace.owner,{},
                     MixedClosureFaceKind::Survivor,0,sourceView});
    return faces.size()-1;
}
std::size_t MixedClosureStaging::ensureFace(
    VertexId a,VertexId b,VertexId c,MixedClosureFaceKind kind,
    PartitionId owner,double targetEdge,std::size_t maxTrackedFaces) {
    if(!sourceView)throw std::logic_error("R06-M15A unbound staging source");
    if(!a||!b||!c||a==b||b==c||c==a||
       !positions.contains(a)||!positions.contains(b)||!positions.contains(c))
        throw std::logic_error("R06-M15A face requires three registered distinct VertexIds");
    const PrivateRepairTriangle wanted{a,b,c};
    for(std::size_t i=0;i<faces.size();++i) {
        const auto existing=faces[i].triangle();
        if(!sameCorners(existing,wanted))continue;
        if(!cyclic(existing,wanted))
            throw std::logic_error("R06-M15A reversed existing triangle winding");
        return i;
    }
    // A remote source face may not yet be loaded into the write halo.
    // Do not create an indexed duplicate of that surviving source face.
    if(sourceView->source().maybeVertex(a)) {
        for(const auto id:sourceView->source().vertex(a).incidentFaces) {
            const auto existing=sourceView->face(id).vertices;
            if(!sameCorners(existing,wanted))continue;
            if(!cyclic(existing,wanted))
                throw std::logic_error("R06-M15A reversed original survivor winding");
            return trackSurvivor(id,maxTrackedFaces);
        }
    }
    if(faces.size()>=maxTrackedFaces)
        throw std::length_error("R06-M15A finite generated-face tracking limit");
    // All three directed edges must have at most one already existing
    // effective incident side, and that side must be reverse oriented.
    // No face or boundary state is written until all three checks pass.
    for(const auto [u,v]:{std::pair{a,b},std::pair{b,c},std::pair{c,a}}) {
        const auto key=edge(u,v);
        std::size_t count=0;
        bool previousForward=false;
        for(const auto& side:sourceView->edgeSides({key.first,key.second})) {
            previousForward=side.forward;
            ++count;
        }
        for(const auto& face:faces) {
            if(face.stableId)continue; // source side already counted above
            bool forward=false;
            if(mixedFaceContainsEdge(face.triangle(),key,&forward)) {
                previousForward=forward;
                ++count;
            }
        }
        if(count>1)
            throw std::logic_error("R06-M15A face would overfill effective edge");
        if(count==1&&previousForward==(u==key.first))
            throw std::logic_error("R06-M15A new face duplicates directed edge side");
    }
    faces.push_back({0,owner,wanted,kind,targetEdge});
    if(kind==MixedClosureFaceKind::Cap)++capTriangles;
    if(kind==MixedClosureFaceKind::Zipper)++zipperTriangles;
    return faces.size()-1;
}
bool MixedClosureStaging::hasEffectiveEdge(VertexId a,VertexId b) const {
    if(!sourceView||!a||!b||a==b)return false;
    const auto key=edge(a,b);
    if(!sourceView->edgeSides({key.first,key.second}).empty())return true;
    for(const auto& face:faces)
        if(!face.stableId&&mixedFaceContainsEdge(face.triangle(),key))
            return true;
    return false;
}
std::vector<std::vector<VertexId>> MixedClosureStaging::currentBoundaryLoops() const {
    if(!sourceView)throw std::logic_error("R06-M15A unbound staging source");
    std::set<Edge> candidates;
    for(const auto& loop:initialRimPaths)
        for(std::size_t i=0;i<loop.size();++i)
            candidates.insert(edge(loop[i],loop[(i+1)%loop.size()]));
    for(const auto& face:faces) {
        const auto t=face.triangle();
        for(unsigned i=0;i<3;++i)
            candidates.insert(edge(t[i],t[(i+1)%3]));
    }
    std::map<VertexId,VertexId> next,previous;
    for(const auto& e:candidates) {
        std::vector<bool> orientations;
        for(const auto& side:sourceView->edgeSides({e.first,e.second}))
            orientations.push_back(side.forward);
        for(const auto& face:faces) {
            if(face.stableId)continue;
            bool forward=false;
            if(mixedFaceContainsEdge(face.triangle(),e,&forward))
                orientations.push_back(forward);
        }
        if(orientations.size()>2||
           (orientations.size()==2&&orientations[0]==orientations[1]))
            throw std::logic_error("R06-M15A working boundary has nonmanifold edge incidence");
        if(orientations.size()!=1)continue;
        const auto a=orientations[0]?e.first:e.second;
        const auto b=orientations[0]?e.second:e.first;
        if(!next.emplace(a,b).second||!previous.emplace(b,a).second)
            throw std::logic_error("R06-M15A working boundary has forked vertex");
    }
    if(next.size()!=previous.size())
        throw std::logic_error("R06-M15A working boundary is an open chain");
    std::vector<std::vector<VertexId>> result;
    std::set<VertexId> visited;
    for(const auto& [start,_]:next) {
        if(visited.contains(start))continue;
        std::vector<VertexId> loop;
        VertexId current=start;
        do {
            const auto it=next.find(current);
            if(it==next.end()||!previous.contains(current)||
               !visited.insert(current).second)
                throw std::logic_error("R06-M15A working boundary is not a closed directed walk");
            loop.push_back(current);
            current=it->second;
        }while(current!=start);
        if(loop.size()<3)
            throw std::logic_error("R06-M15A working boundary degenerate loop");
        result.push_back(std::move(loop));
    }
    return result;
}
} // namespace mesher
