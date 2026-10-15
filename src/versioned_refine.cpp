#include "mesher/versioned_refine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
using Index=RegionTopologyIndex;
Edge key(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
Vec3 reflect(Vec3 p) {return {-p.x,p.y,p.z};}
struct DirectedEdge {VertexId a{},b{},c{};};
bool directed(std::array<VertexId,3> triangle,VertexId a,VertexId b,
              DirectedEdge& out) {
    for(unsigned i=0;i<3;++i) {
        const auto u=triangle[i],v=triangle[(i+1)%3];
        if((u==a&&v==b)||(u==b&&v==a)) {
            out={u,v,triangle[(i+2)%3]};
            return true;
        }
    }
    return false;
}
std::vector<FaceId> incident(const Index& index,Edge e) {
    const auto sides=index.maybeEdgeSides(e);
    if(!sides || sides->size()!=2)return {};
    return {(*sides)[0].face,(*sides)[1].face};
}
std::array<FaceId,2> splitOne(const Index& index,RegionEdit& edit,
                              Edge selected,VertexId midpoint) {
    const auto touching=incident(index,selected);
    if(touching.size()!=2)
        throw std::logic_error("Split requires two incident faces");
    std::array<FaceId,2> inserted{};
    unsigned count=0;
    for(const auto face:touching) {
        DirectedEdge e{};
        const auto original=index.face(face).vertices;
        if(!directed(original,selected.a,selected.b,e))
            throw std::logic_error("Split lost its oriented incident edge");
        edit.replaceFace(face,{e.a,midpoint,e.c});
        inserted[count++]=edit.appendLocalFace(
            index.face(face).owner,{midpoint,e.b,e.c});
    }
    return inserted;
}
double radial(const Index& mesh,std::array<VertexId,3> t) {
    const auto a=mesh.vertex(t[0]).position;
    const auto b=mesh.vertex(t[1]).position;
    const auto c=mesh.vertex(t[2]).position;
    return dot(cross(b-a,c-a),a+b+c);
}
Vec3 normal(const Index& mesh,std::array<VertexId,3> t) {
    const auto a=mesh.vertex(t[0]).position;
    const auto b=mesh.vertex(t[1]).position;
    const auto c=mesh.vertex(t[2]).position;
    const Vec3 n=cross(b-a,c-a);
    const double magnitude=length(n);
    return magnitude>1e-14?n/magnitude:Vec3{};
}
double quality(const Index& mesh,std::array<VertexId,3> t) {
    const auto a=mesh.vertex(t[0]).position;
    const auto b=mesh.vertex(t[1]).position;
    const auto c=mesh.vertex(t[2]).position;
    const double squares=dot(b-a,b-a)+dot(c-b,c-b)+dot(a-c,a-c);
    if(squares<1e-20)return 0;
    constexpr double root3=1.7320508075688772935;
    return 2.0*root3*length(cross(b-a,c-a))/squares;
}
struct ProposedFlip {
    std::array<FaceId,2> faces{};
    std::array<std::array<VertexId,3>,2> replacement{};
    std::array<VertexId,4> vertices{};
};
std::optional<ProposedFlip> proposeFlip(const Index& mesh,Edge e) {
    const auto faces=incident(mesh,e);
    if(faces.size()!=2)return std::nullopt;
    DirectedEdge u{},v{};
    if(!directed(mesh.face(faces[0]).vertices,e.a,e.b,u)||
       !directed(mesh.face(faces[1]).vertices,e.a,e.b,v)||
       u.a!=v.b||u.b!=v.a||u.c==v.c||
       mesh.maybeEdgeSides(key(u.c,v.c)))return std::nullopt;
    const auto oldA=mesh.face(faces[0]).vertices;
    const auto oldB=mesh.face(faces[1]).vertices;
    const std::array<VertexId,3> freshA{u.c,v.c,u.b};
    const std::array<VertexId,3> freshB{v.c,u.c,u.a};
    if(radial(mesh,oldA)<=1e-12||radial(mesh,oldB)<=1e-12||
       radial(mesh,freshA)<=1e-12||radial(mesh,freshB)<=1e-12)
        return std::nullopt;
    const auto oldNormalA=normal(mesh,oldA),oldNormalB=normal(mesh,oldB);
    const auto newNormalA=normal(mesh,freshA),newNormalB=normal(mesh,freshB);
    if(dot(oldNormalA,oldNormalB)<.95||
       dot(oldNormalA,newNormalA)<.90||
       dot(oldNormalB,newNormalB)<.90)return std::nullopt;
    const double qOldA=quality(mesh,oldA),qOldB=quality(mesh,oldB);
    const double qNewA=quality(mesh,freshA),qNewB=quality(mesh,freshB);
    const double minOld=std::min(qOldA,qOldB),minNew=std::min(qNewA,qNewB);
    const int va=static_cast<int>(mesh.vertex(u.a).incidentFaces.size());
    const int vb=static_cast<int>(mesh.vertex(u.b).incidentFaces.size());
    const int vc=static_cast<int>(mesh.vertex(u.c).incidentFaces.size());
    const int vd=static_cast<int>(mesh.vertex(v.c).incidentFaces.size());
    const auto cost=[](int n){return (n-6)*(n-6);};
    const int oldCost=cost(va)+cost(vb)+cost(vc)+cost(vd);
    const int newCost=cost(va-1)+cost(vb-1)+cost(vc+1)+cost(vd+1);
    const bool betterAngles=minNew>minOld*1.03 &&
                            qNewA+qNewB>.98*(qOldA+qOldB);
    const bool betterValence=newCost<oldCost &&
                             minNew>=.98*minOld &&
                             qNewA+qNewB>=.99*(qOldA+qOldB);
    if(!betterAngles&&!betterValence)return std::nullopt;
    return ProposedFlip{{faces[0],faces[1]},{freshA,freshB},
                        {u.a,u.b,u.c,v.c}};
}
}

VersionedLocalRefiner::VersionedLocalRefiner(
    std::size_t initialVertexCount,RefineSettings settings,
    std::vector<std::pair<VertexId,VertexId>> mirrors,
    std::size_t maximumElementsPerPartition)
    :settings_(settings),partitionCapacity_(maximumElementsPerPartition) {
    if(initialVertexCount==0 || !std::isfinite(settings.maximumEdgeRatio)||
       settings.maximumEdgeRatio<=0 || !partitionCapacity_)
        throw std::invalid_argument("Invalid versioned refinement configuration");
    constexpr double pi=3.14159265358979323846;
    maximumEdge_=settings.maximumEdgeRatio*
       std::sqrt(8*pi/(static_cast<double>(initialVertexCount)*std::sqrt(3.0)));
    for(const auto& [a,b]:mirrors) {
        if(a==0||b==0)throw std::invalid_argument("Mirror IDs must be nonzero");
        const auto [i,inserted]=mirrors_.emplace(a,b);
        const auto [j,insertedOther]=mirrors_.emplace(b,a);
        if((!inserted&&i->second!=b)||
           (!insertedOther&&j->second!=a))
            throw std::invalid_argument("Mirror pairs must be reciprocal");
    }
}

VersionedRefineResult VersionedLocalRefiner::apply(
    std::shared_ptr<const RegionTopologyIndex> postInflate,
    const LocalStampBaseline& baseline) {
    if(!postInflate||!baseline.before)
        throw std::invalid_argument("Refinement needs two immutable stamp revisions");
    VersionedRefineResult result;
    result.index=postInflate;
    if(!settings_.enabled)return result;

    struct Candidate {Edge edge;double length;};
    std::set<Edge> seen;
    std::vector<Candidate> candidates;
    for(const auto id:baseline.affectedFaces) {
        const auto face=postInflate->face(id).vertices;
        for(unsigned j=0;j<3;++j) {
            const auto edge=key(face[j],face[(j+1)%3]);
            if(!seen.insert(edge).second)continue;
            const double afterLength=length(postInflate->vertex(edge.a).position-
                                            postInflate->vertex(edge.b).position);
            const double beforeLength=baseline.edgeLengthBefore(edge.a,edge.b);
            if(afterLength<=maximumEdge_||afterLength<=beforeLength+1e-12)
                continue;
            if(!mirrors_.empty()) {
                const auto a=mirrors_.at(edge.a),b=mirrors_.at(edge.b);
                if(key(a,b)<edge)continue;
            }
            ++result.statistics.longGrowingEdges;
            candidates.push_back({edge,afterLength});
        }
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,
                                                       const Candidate& b) {
        return a.length==b.length?a.edge<b.edge:a.length>b.length;
    });
    std::unordered_set<VertexId> touched;
    for(const auto candidate:candidates) {
        if(result.statistics.splitPairs>=settings_.maximumSplitPairsPerStamp)break;
        const auto e=candidate.edge;
        const auto current=result.index;
        if(incident(*current,e).size()!=2)continue;
        const bool symmetric=!mirrors_.empty();
        const auto mirrored=symmetric?key(mirrors_.at(e.a),mirrors_.at(e.b)):e;
        const bool paired=symmetric&&mirrored!=e;
        if(paired&&incident(*current,mirrored).size()!=2)continue;
        const auto originalFaces=incident(*current,e);
        const auto mirrorFaces=paired?incident(*current,mirrored):std::vector<FaceId>{};
        if(paired) {
            bool overlap=false;
            for(const auto face:originalFaces)
                if(std::find(mirrorFaces.begin(),mirrorFaces.end(),face)!=
                   mirrorFaces.end())overlap=true;
            if(overlap)continue;
        }
        Vec3 midpoint=(current->vertex(e.a).position+
                       current->vertex(e.b).position)*.5;
        if(symmetric&&!paired) {
            if(std::abs(midpoint.x)>1e-9)continue;
            midpoint.x=0;
        }
        std::vector<VertexId> seeds{e.a,e.b};
        if(paired) {seeds.push_back(mirrored.a);seeds.push_back(mirrored.b);}
        try {
            // Both halves of a mirrored split are staged in one *unsymmetric*
            // region transaction, then atomically validated together. The
            // involutive mirror map is installed only after successful commit.
            RegionEdit edit(current,seeds,{},partitionCapacity_);
            const auto mid=edit.appendLocalVertex(current->vertex(e.a).owner,
                                                  midpoint);
            const auto addedFaces=splitOne(*current,edit,e,mid);
            VertexId other=mid;
            std::array<FaceId,2> mirrorAdded{};
            if(paired) {
                other=edit.appendLocalVertex(current->vertex(mirrored.a).owner,
                                             reflect(midpoint));
                mirrorAdded=splitOne(*current,edit,mirrored,other);
            }
            const auto committed=edit.commit();
            result.index=edit.updatedIndex();
            result.modifiedPartitions+=committed.statistics.modifiedPartitions;
            result.copiedDirectoryNodes+=committed.statistics.copiedDirectoryNodes;
            result.insertedVertices.push_back(mid);
            if(paired)result.insertedVertices.push_back(other);
            for(const auto face:originalFaces)result.changedFaces.push_back(face);
            for(const auto face:mirrorFaces)result.changedFaces.push_back(face);
            for(const auto face:addedFaces)result.changedFaces.push_back(face);
            if(paired)for(const auto face:mirrorAdded)
                result.changedFaces.push_back(face);
            if(symmetric) {
                mirrors_.emplace(mid,other);
                if(paired)mirrors_.emplace(other,mid);
                result.insertedMirrorPairs.push_back({mid,other});
            }
            ++result.statistics.splitPairs;
            result.statistics.newVertices+=paired?2u:1u;
            for(const auto id:{e.a,e.b,mid})touched.insert(id);
            if(paired)for(const auto id:{mirrored.a,mirrored.b,other})
                touched.insert(id);
        }catch(const std::logic_error&) {
            // A rejected local edit has never been published. The next
            // candidate still sees the previous valid indexed revision.
            continue;
        }
    }
    const auto uniqueChangedFaces=[&] {
        std::sort(result.changedFaces.begin(),result.changedFaces.end());
        result.changedFaces.erase(std::unique(result.changedFaces.begin(),
                                              result.changedFaces.end()),
                                  result.changedFaces.end());
    };
    if(!settings_.optimizeFlips||!result.statistics.splitPairs||
       settings_.maximumFlipPairsPerStamp==0) {
        uniqueChangedFaces();
        return result;
    }

    std::unordered_set<VertexId> local=touched;
    for(const auto vertex:touched)
        for(const auto face:result.index->vertex(vertex).incidentFaces)
            for(const auto neighbor:result.index->face(face).vertices)
                local.insert(neighbor);
    std::set<Edge> flipCandidates;
    for(const auto vertex:local)
        for(const auto face:result.index->vertex(vertex).incidentFaces) {
            const auto t=result.index->face(face).vertices;
            for(unsigned i=0;i<3;++i)
                if(local.contains(t[i])&&local.contains(t[(i+1)%3]))
                    flipCandidates.insert(key(t[i],t[(i+1)%3]));
        }
    std::size_t attempts=0;
    for(const auto e:flipCandidates) {
        if(++attempts>std::size_t(settings_.maximumFlipPairsPerStamp)*8 ||
           result.statistics.flipPairs>=settings_.maximumFlipPairsPerStamp)break;
        const auto current=result.index;
        const auto mirror=mirrors_.empty()?e:key(mirrors_.at(e.a),
                                                  mirrors_.at(e.b));
        if(!mirrors_.empty()&&mirror<=e)continue;
        const auto first=proposeFlip(*current,e);
        if(!first)continue;
        std::optional<ProposedFlip> second;
        if(mirror!=e) {
            second=proposeFlip(*current,mirror);
            if(!second)continue;
            bool overlap=false;
            for(const auto a:first->faces)
                for(const auto b:second->faces)
                    if(a==b)overlap=true;
            if(overlap)continue;
        }
        std::vector<VertexId> seeds(first->vertices.begin(),
                                    first->vertices.end());
        if(second)seeds.insert(seeds.end(),second->vertices.begin(),
                               second->vertices.end());
        try {
            RegionEdit edit(current,seeds,{},partitionCapacity_);
            for(unsigned i=0;i<2;++i)
                edit.replaceFace(first->faces[i],first->replacement[i]);
            if(second)for(unsigned i=0;i<2;++i)
                edit.replaceFace(second->faces[i],second->replacement[i]);
            const auto committed=edit.commit();
            result.index=edit.updatedIndex();
            result.modifiedPartitions+=committed.statistics.modifiedPartitions;
            result.copiedDirectoryNodes+=committed.statistics.copiedDirectoryNodes;
            for(const auto face:first->faces)result.changedFaces.push_back(face);
            if(second)for(const auto face:second->faces)
                result.changedFaces.push_back(face);
            ++result.statistics.flipPairs;
        }catch(const std::logic_error&) {
            continue;
        }
    }
    uniqueChangedFaces();
    return result;
}

} // namespace mesher
