#include "mesher/versioned_collapse.hpp"
#include "mesher/vertex_paint.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
Edge edge(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
bool used(std::array<VertexId,3> face,VertexId id) {
    return face[0]==id||face[1]==id||face[2]==id;
}
double radial(Vec3 a,Vec3 b,Vec3 c) {
    return dot(cross(b-a,c-a),a+b+c);
}
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
// Exact local equivalent of the old full-triangle-scan link condition.
// Its two common neighbors must be exactly the two opposite vertices of the
// incident edge faces, with no extra common neighbor or nonmanifold pinch.
bool linkCondition(const RegionTopologyIndex& index,VertexId a,VertexId b) {
    if(a==b || !index.maybeVertex(a) || !index.maybeVertex(b))return false;
    const auto sides=index.maybeEdgeSides(edge(a,b));
    if(!sides || sides->size()!=2)return false;
    std::unordered_set<VertexId> an,bn,opposite;
    for(const auto f:index.vertex(a).incidentFaces)
        for(const auto v:index.face(f).vertices)
            if(v!=a)an.insert(v);
    for(const auto f:index.vertex(b).incidentFaces)
        for(const auto v:index.face(f).vertices)
            if(v!=b)bn.insert(v);
    for(const auto side:*sides)
        for(const auto v:index.face(side.face).vertices)
            if(v!=a&&v!=b)opposite.insert(v);
    if(opposite.size()!=2)return false;
    std::size_t intersection=0;
    for(const auto v:an)if(bn.contains(v)) {
        if(!opposite.contains(v))return false;
        ++intersection;
    }
    return intersection==2;
}
struct ModifiedFace {
    FaceId id{};
    std::array<VertexId,3> original{};
    std::array<VertexId,3> next{};
    bool deleted{};
};
// Pair insertions must not silently add a repeated "mirror" mapping.
void addMirror(std::unordered_map<VertexId,VertexId>& map,
               VertexId a,VertexId b) {
    if(a==0||b==0)throw std::invalid_argument("Mirror ID cannot be zero");
    const auto i=map.find(a),j=map.find(b);
    if((i!=map.end()&&i->second!=b)||
       (j!=map.end()&&j->second!=a))
        throw std::invalid_argument("Mirror map is not an involution");
    map.emplace(a,b);
    map.emplace(b,a);
}
}

VersionedDirectionalCollapser::VersionedDirectionalCollapser(
    std::size_t initialVertexCount,CollapseSettings settings,
    std::vector<std::pair<VertexId,VertexId>> mirrors,
    std::size_t maximumElementsPerPartition)
    :settings_(settings),partitionCapacity_(maximumElementsPerPartition) {
    if(!initialVertexCount||!std::isfinite(settings.minimumEdgeRatio)||
       settings.minimumEdgeRatio<=0||!partitionCapacity_)
        throw std::invalid_argument("Invalid versioned collapse configuration");
    constexpr double pi=3.1415926535897932384626433832795;
    minimumEdge_=settings.minimumEdgeRatio*
        std::sqrt(8*pi/(static_cast<double>(initialVertexCount)*
                        std::sqrt(3.0)));
    for(const auto& [a,b]:mirrors)addMirror(mirrors_,a,b);
}

void VersionedDirectionalCollapser::addMirrorPair(
    VertexId first,VertexId second) {
    if(mirrors_.empty())return; // asymmetric worker has no mirror map
    // Validate BEFORE recording an undo entry: an invalid correspondence
    // must not leave a journal entry for a mutation that never occurred.
    const auto a=mirrors_.find(first),b=mirrors_.find(second);
    if(!first||!second||
       (a!=mirrors_.end()&&a->second!=second)||
       (b!=mirrors_.end()&&b->second!=first))
        throw std::invalid_argument("Conflicting collapse mirror pair");
    if(stepOpen_&&a==mirrors_.end())
        pendingMirrors_.push_back({true,first,second});
    addMirror(mirrors_,first,second);
}

void VersionedDirectionalCollapser::removeMirrorPair(
    VertexId first,VertexId second) {
    const auto a=mirrors_.find(first),b=mirrors_.find(second);
    if(a==mirrors_.end()||b==mirrors_.end()||
       a->second!=second||b->second!=first)
        throw std::invalid_argument("Cannot remove nonexistent collapse mirror pair");
    if(stepOpen_)pendingMirrors_.push_back({false,first,second});
    mirrors_.erase(first);
    if(second!=first)mirrors_.erase(second);
}
void VersionedDirectionalCollapser::beginStep() {
    if(stepOpen_)throw std::logic_error("Collapse Step already open");
    pendingMirrors_.clear();
    stepOpen_=true;
}
void VersionedDirectionalCollapser::commitStep() noexcept {
    pendingMirrors_.clear();
    stepOpen_=false;
}
void VersionedDirectionalCollapser::rollbackStep() {
    stepOpen_=false;
    for(auto it=pendingMirrors_.rbegin();it!=pendingMirrors_.rend();++it) {
        if(it->added)removeMirrorPair(it->first,it->second);
        else addMirrorPair(it->first,it->second);
    }
    pendingMirrors_.clear();
}

bool VersionedDirectionalCollapser::attempt(
    VersionedCollapseResult& result,VertexId a,VertexId b) {
    const auto current=result.index;
    if(!linkCondition(*current,a,b))return false;
    const bool symmetric=!mirrors_.empty();
    if(symmetric) {
        const auto i=mirrors_.find(a),j=mirrors_.find(b);
        if(i==mirrors_.end()||j==mirrors_.end())
            throw std::logic_error("Active vertex lacks a mirror correspondence");
        // Prefer to retain the seam vertex rather than pull it off X=0.
        if(j->second==b&&i->second!=a)std::swap(a,b);
    }
    const auto ma=symmetric?mirrors_.at(a):a;
    const auto mb=symmetric?mirrors_.at(b):b;
    const bool paired=symmetric&&edge(ma,mb)!=edge(a,b);
    if(symmetric&&!paired&&ma!=a)return false; // edge crossing reflection
    if(paired && (!linkCondition(*current,ma,mb) ||
                  b==ma || b==mb || a==mb))return false;

    std::set<FaceId> neighbors;
    for(const auto id:{a,b})for(const auto face:current->vertex(id).incidentFaces)
        neighbors.insert(face);
    if(paired)for(const auto id:{ma,mb})
        for(const auto face:current->vertex(id).incidentFaces)
            neighbors.insert(face);

    const Vec3 initialA=current->vertex(a).position;
    const Vec3 initialB=current->vertex(b).position;
    const Vec3 midpoint=(initialA+initialB)*.5;
    const bool retainSeam=symmetric&&ma==a&&mb!=b;
    const Vec3 newA=retainSeam?initialA:midpoint;
    const Vec3 newMa=paired&&ma!=a?
        (current->vertex(ma).position+current->vertex(mb).position)*.5:
        newA;
    if(!finite(newA)||!finite(newMa))return false;

    const auto redirected=[&](VertexId id)->VertexId {
        if(id==b)return a;
        if(paired&&id==mb)return ma;
        return id;
    };
    const auto position=[&](VertexId id)->Vec3 {
        if(id==a)return newA;
        if(paired&&id==ma)return newMa;
        return current->vertex(id).position;
    };
    std::vector<ModifiedFace> changes;
    changes.reserve(neighbors.size());
    for(const auto faceId:neighbors) {
        const auto original=current->face(faceId).vertices;
        auto updated=original;
        for(auto& id:updated)id=redirected(id);
        const bool gone=updated[0]==updated[1]||
                        updated[1]==updated[2]||updated[0]==updated[2];
        if(!gone) {
            const auto pa=position(updated[0]);
            const auto pb=position(updated[1]);
            const auto pc=position(updated[2]);
            if(length(cross(pb-pa,pc-pa))<1e-12)return false;
            const double oldRadial=radial(
                current->vertex(original[0]).position,
                current->vertex(original[1]).position,
                current->vertex(original[2]).position);
            if(oldRadial>1e-13&&radial(pa,pb,pc)<=1e-13)return false;
        }
        const bool changed=gone||updated!=original||
            used(original,a)|| (paired&&used(original,ma));
        if(changed)changes.push_back({faceId,original,updated,gone});
    }

    std::vector<VertexId> seeds{a,b};
    if(paired) {seeds.push_back(ma);seeds.push_back(mb);}
    // Stage ALL mirrored edits in a single unsymmetric RegionEdit, since
    // the symmetry-specific V3 API cannot remove vertices by itself.
    try {
        RegionEdit transaction(current,seeds,{},partitionCapacity_);
        if(length(newA-initialA)>0)transaction.moveVertex(a,newA);
        const auto ca=vertexColorAt(*current,a);
        const auto cb=vertexColorAt(*current,b);
        transaction.setVertexColor(a,{(ca.r+cb.r)*.5f,(ca.g+cb.g)*.5f,
                                      (ca.b+cb.b)*.5f,(ca.a+cb.a)*.5f});
        if(paired&&ma!=a &&
           length(newMa-current->vertex(ma).position)>0)
            transaction.moveVertex(ma,newMa);
        if(paired&&ma!=a) {
            const auto ca=vertexColorAt(*current,ma);
            const auto cb=vertexColorAt(*current,mb);
            transaction.setVertexColor(ma,{(ca.r+cb.r)*.5f,(ca.g+cb.g)*.5f,
                                           (ca.b+cb.b)*.5f,(ca.a+cb.a)*.5f});
        }
        for(const auto& f:changes) {
            if(f.deleted)transaction.removeFace(f.id);
            else if(f.original!=f.next)transaction.replaceFace(f.id,f.next);
        }
        transaction.removeVertex(b);
        if(paired&&mb!=b)transaction.removeVertex(mb);
        const auto committed=transaction.commit();
        result.index=transaction.updatedIndex();
        result.modifiedPartitions+=committed.statistics.modifiedPartitions;
        result.copiedDirectoryNodes+=committed.statistics.copiedDirectoryNodes;
        result.clonedPartitionBytes+=committed.statistics.clonedPartitionBytes;
        for(const auto& f:changes) {
            if(f.deleted)result.removedFaces.push_back(f.id);
            else if(f.original!=f.next)result.rewrittenFaces.push_back(f.id);
        }
        result.removedVertices.push_back(b);
        if(paired&&mb!=b)result.removedVertices.push_back(mb);
        // The complete private RegionEdit has already committed. Record
        // its exact original/root edge endpoints and every changed FaceId
        // in ONE event (including its reflected side when paired).
        CollapseSourceEvidence evidence;
        evidence.before=current;
        evidence.after=result.index;
        evidence.survivor=a;
        evidence.retired=b;
        evidence.mirrorSurvivor=ma;
        evidence.mirrorRetired=mb;
        evidence.symmetric=symmetric;
        evidence.paired=paired;
        for(const auto& changed:changes) {
            if(changed.deleted)evidence.retiredFaces.push_back(changed.id);
            else if(changed.original!=changed.next)
                evidence.rewrittenFaces.push_back(changed.id);
        }
        result.collapseSources.push_back(std::move(evidence));
        if(symmetric) {
            // The previous geometry root preserves retired IDs. This map
            // describes *only* living pairs, without broken dangling links.
            result.removedMirrorPairs.push_back({b,mb});
            removeMirrorPair(b,mb);
            if(ma==a)mirrors_[a]=a;
            else {
                mirrors_[a]=ma;
                mirrors_[ma]=a;
            }
        }
        ++result.statistics.accepted;
        result.statistics.removedVertices+=paired&&mb!=b?2u:1u;
        return true;
    }catch(const std::logic_error&) {
        // Invalid link, valence, orientation, or boundary fan: no published
        // revision and no state mutation. The next candidate starts clean.
        return false;
    }
}

VersionedCollapseResult VersionedDirectionalCollapser::tryEdge(
    std::shared_ptr<const RegionTopologyIndex> postEdit,
    const LocalStampBaseline& baseline,VertexId a,VertexId b) {
    if(!postEdit||!baseline.before)
        throw std::invalid_argument("Collapse requires an immutable stamp baseline");
    VersionedCollapseResult result;
    result.index=std::move(postEdit);
    result.sourceIndex=result.index;
    result.stampOriginal=baseline.before;
    if(!settings_.enabled||settings_.maximumPairsPerStamp==0)return result;
    if(!result.index->maybeVertex(a)||!result.index->maybeVertex(b)||
       !baseline.before->maybeVertex(a)||!baseline.before->maybeVertex(b))
        return result;
    if(!result.index->maybeEdgeSides(edge(a,b)))return result;
    const auto after=length(result.index->vertex(a).position-
                            result.index->vertex(b).position);
    if(after>=minimumEdge_||
       after+1e-12>=baseline.edgeLengthBefore(a,b))return result;
    ++result.statistics.attempted;
    (void)attempt(result,a,b);
    return result;
}

VersionedCollapseResult VersionedDirectionalCollapser::apply(
    std::shared_ptr<const RegionTopologyIndex> postEdit,
    const LocalStampBaseline& baseline) {
    if(!postEdit||!baseline.before)
        throw std::invalid_argument("Collapse requires an immutable stamp baseline");
    VersionedCollapseResult result;
    result.index=std::move(postEdit);
    result.sourceIndex=result.index;
    result.stampOriginal=baseline.before;
    if(!settings_.enabled||settings_.maximumPairsPerStamp==0)return result;
    struct Candidate {Edge id;double length;};
    std::set<Edge> seen;
    std::vector<Candidate> candidates;
    // The existing affected-face halo bounds this pass to the edited area.
    // New faces inserted by V6 have no old position, so only consider edges
    // whose *both* endpoints predate the stamp's immutable baseline.
    for(const auto face:baseline.affectedFaces) {
        // The same baseline might have passed through an earlier local edit.
        // An erased face is a valid tombstone, not a candidate.
        if(result.index->retiredFace(face))continue;
        const auto t=result.index->face(face).vertices;
        for(unsigned i=0;i<3;++i) {
            const auto e=edge(t[i],t[(i+1)%3]);
            if(!seen.insert(e).second)continue;
            if(!baseline.before->maybeVertex(e.a)||
               !baseline.before->maybeVertex(e.b))continue;
            const auto after=length(result.index->vertex(e.a).position-
                                    result.index->vertex(e.b).position);
            if(after>=minimumEdge_||
               after+1e-12>=baseline.edgeLengthBefore(e.a,e.b))continue;
            if(!mirrors_.empty()&&
               edge(mirrors_.at(e.a),mirrors_.at(e.b))<e)continue;
            candidates.push_back({e,after});
        }
    }
    std::sort(candidates.begin(),candidates.end(),
              [](const Candidate& a,const Candidate& b) {
        return a.length==b.length?a.id<b.id:a.length<b.length;
    });
    for(const auto& candidate:candidates) {
        if(result.statistics.accepted>=settings_.maximumPairsPerStamp)break;
        if(!result.index->maybeVertex(candidate.id.a)||
           !result.index->maybeVertex(candidate.id.b)||
           !result.index->maybeEdgeSides(candidate.id))continue;
        ++result.statistics.attempted;
        (void)attempt(result,candidate.id.a,candidate.id.b);
    }
    std::sort(result.removedFaces.begin(),result.removedFaces.end());
    std::sort(result.rewrittenFaces.begin(),result.rewrittenFaces.end());
    return result;
}

} // namespace mesher
