#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_mixed_edges.hpp"
#include "mesher/repair_survivor_split.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
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
    PartitionId owner,double targetEdge,std::size_t maxTrackedFaces,
    std::size_t maxGeneratedFaces) {
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
    if(capTriangles+zipperTriangles>=maxGeneratedFaces &&
       kind!=MixedClosureFaceKind::Survivor)
        throw std::length_error("R06-M15C finite generated closure-face budget");
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

std::vector<VertexId> MixedClosureStaging::splitEdge(
    MutableMeshView& privateView,VertexId a,VertexId b,
    const MixedClosureSplitSettings& settings,
    std::size_t alreadyInserted,std::size_t alreadyRefined,
    bool optionalQualityOnly) {
    if(!sourceView)throw std::logic_error("R06-M15B unbound staging source");
    const Edge firstEdge=edge(a,b);
    if(!a||!b||a==b||!hasEffectiveEdge(a,b))
        throw std::logic_error("R06-M15B requested split edge does not exist in current topology");
    if(!settings.maxTrackedFaces||!settings.maxInsertedVertices||
       !settings.maxRefinementSteps||settings.minSurvivorQuality<=0||
       settings.minCapQuality<=0||settings.minZipperQuality<=0)
        throw std::invalid_argument("R06-M15B invalid split policy");

    const auto partner=[&](VertexId id)->std::optional<VertexId> {
        const auto p=positions.at(id);
        std::optional<VertexId> candidate;
        for(const auto& [other,q]:positions) {
            if(q.x!=-p.x||q.y!=p.y||q.z!=p.z)continue;
            if(candidate)return std::nullopt;
            candidate=other;
        }
        return candidate;
    };
    Edge otherEdge=firstEdge;
    if(settings.requireReflectedGeometry) {
        const auto left=partner(firstEdge.first);
        const auto right=partner(firstEdge.second);
        if(!left||!right) {
            if(optionalQualityOnly)return {};
            throw std::logic_error("R06-M15B reflected split edge partner missing");
        }
        otherEdge=edge(*left,*right);
    }
    const bool two=otherEdge!=firstEdge;
    const std::vector<Edge> orbit=two?
        std::vector<Edge>{firstEdge,otherEdge}:
        std::vector<Edge>{firstEdge};

    struct Candidate {
        Edge key;
        MixedClosureEdgeNeighborhood neighborhood;
        std::vector<MixedClosureFace> effective;
        Vec3 midpoint{};
        std::size_t originalSides{};
    };
    std::vector<Candidate> work;
    work.reserve(orbit.size());
    std::set<FaceId> remote;
    std::set<FaceId> originalOwners;
    std::size_t children=0;
    for(const auto& item:orbit) {
        auto n=inspectMixedClosureEdge(faces,*sourceView,item);
        if(n.sides()==0)
            throw std::logic_error("R06-M15B requested reflected edge no longer exists");
        if(n.sides()>2||(n.sides()==2&&!n.conformingPair)) {
            if(optionalQualityOnly)return {};
            throw std::logic_error("R06-M15B invalid current incident edge orientation");
        }
        Candidate c;
        c.key=item;
        c.neighborhood=std::move(n);
        c.originalSides=c.neighborhood.sides();
        children+=c.originalSides;
        for(const auto index:c.neighborhood.localFaceIndexes)
            c.effective.push_back(faces.at(index));
        for(const auto id:c.neighborhood.missingSurvivorFaces) {
            const auto& f=sourceView->face(id);
            c.effective.push_back({id,f.owner,{},
                MixedClosureFaceKind::Survivor,0,sourceView});
            remote.insert(id);
        }
        for(const auto& f:c.effective)
            if(f.stableId&&!originalOwners.insert(f.stableId).second) {
                if(optionalQualityOnly)return {};
                throw std::logic_error("R06-M15B reflected edge orbit shares an original face");
            }
        const auto safe=assessSurvivorSplit(
            c.effective,positions,item,settings.minSurvivorQuality);
        if(!safe.safe) {
            if(optionalQualityOnly)return {};
            throw std::logic_error(
                "R06-M15B survivor child fails split quality face="+
                std::to_string(safe.originalFace)+
                " child_q="+std::to_string(safe.worstChildQuality));
        }
        const auto midpoint=assessUniqueConformingMidpoint(positions,item);
        if(!midpoint.unique) {
            if(optionalQualityOnly)return {};
            throw std::logic_error("R06-M15B split midpoint duplicates a registered vertex");
        }
        c.midpoint=midpoint.point;
        // Original R03 vertices outside the tracked halo must also be
        // checked, but identical XYZ does NOT grant identity or welding.
        for(const auto existing:sourceView->source().querySphere(
              c.midpoint,std::numeric_limits<double>::denorm_min())) {
            const auto q=sourceView->position(existing);
            if(q.x==c.midpoint.x&&q.y==c.midpoint.y&&q.z==c.midpoint.z) {
                if(optionalQualityOnly)return {};
                throw std::logic_error("R06-M15B split midpoint duplicates a remote source vertex");
            }
        }
        if(optionalQualityOnly) {
            for(const auto& f:c.effective) {
                const auto tri=f.triangle();
                bool found=false;
                for(unsigned k=0;k<3;++k) {
                    const auto x=tri[k],y=tri[(k+1)%3],z=tri[(k+2)%3];
                    if(edge(x,y)!=item)continue;
                    const auto p=positions.at(x),q=positions.at(y),
                               r=positions.at(z),m=c.midpoint;
                    const auto quality=[](Vec3 u,Vec3 v,Vec3 w) {
                        const auto sum=dot(u-v,u-v)+dot(v-w,v-w)+dot(w-u,w-u);
                        return sum>0?2*std::sqrt(3.)*
                            length(cross(v-u,w-u))/sum:0.;
                    };
                    const auto floor=f.kind==MixedClosureFaceKind::Survivor?
                        settings.minSurvivorQuality:
                        f.kind==MixedClosureFaceKind::Cap?
                        settings.minCapQuality:settings.minZipperQuality;
                    if(std::min(quality(p,m,r),quality(m,q,r))+1e-12<floor)
                        return {};
                    found=true;
                    break;
                }
                if(!found)
                    throw std::logic_error("R06-M15B expected incident face missing chosen edge");
            }
        }
        work.push_back(std::move(c));
    }
    if(two) {
        const auto& p=work.front().midpoint;
        const auto& q=work.back().midpoint;
        if(p.x==q.x&&p.y==q.y&&p.z==q.z) {
            if(optionalQualityOnly)return {};
            throw std::logic_error("R06-M15B reflected split orbit has one coincident midpoint");
        }
    }
    const auto count=orbit.size();
    if(alreadyInserted+count>settings.maxInsertedVertices||
       alreadyRefined+count>settings.maxRefinementSteps||
       faces.size()+remote.size()+children>settings.maxTrackedFaces) {
        if(optionalQualityOnly)return {};
        throw std::length_error("R06-M15B complete edge split exceeds finite budget");
    }
    // All semantic checks are complete before the first mutation.
    // Failure after allocation aborts the ENTIRE private stage; none
    // of these writes reach the authoritative mesh or history.
    for(const auto id:remote)(void)trackSurvivor(id,settings.maxTrackedFaces);
    faces.reserve(faces.size()+children);
    std::vector<VertexId> midpoints;
    midpoints.reserve(count);
    for(const auto& c:work) {
        const auto owner=sourceView->source().maybeVertex(c.key.first)?
            sourceView->owner(c.key.first):vertexOwners.at(c.key.first);
        const auto middle=addVertex(
            privateView,c.midpoint,owner,settings.maxInsertedVertices);
        midpoints.push_back(middle);
        std::vector<std::size_t> adjacent;
        for(std::size_t i=0;i<faces.size();++i)
            if(mixedFaceContainsEdge(faces[i].triangle(),c.key))
                adjacent.push_back(i);
        if(adjacent.size()!=c.originalSides)
            throw std::logic_error("R06-M15B preflight incidence drift");
        for(const auto index:adjacent) {
            const auto parent=faces.at(index);
            const auto tri=parent.triangle();
            bool found=false;
            for(unsigned k=0;k<3;++k) {
                const auto x=tri[k],y=tri[(k+1)%3],z=tri[(k+2)%3];
                if(edge(x,y)!=c.key)continue;
                faces[index].setTriangle({x,middle,z});
                faces.push_back({0,parent.owner,{middle,y,z},
                    parent.kind,parent.targetEdge});
                if(parent.kind==MixedClosureFaceKind::Cap)++capTriangles;
                if(parent.kind==MixedClosureFaceKind::Zipper)++zipperTriangles;
                found=true;
                break;
            }
            if(!found)
                throw std::logic_error("R06-M15B chosen edge disappeared during split");
        }
    }
    // currentBoundaryLoops() derives all yellow paths from these exact
    // effective edges; the original AB is now absent and AM/MB are real.
    return midpoints;
}
} // namespace mesher
