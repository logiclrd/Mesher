#include "mesher/repair_mixed_closure.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=std::pair<VertexId,VertexId>;
Edge edge(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const double sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
double edgeSpan(const std::map<VertexId,Vec3>& pos,Edge e) {
    return length(pos.at(e.first)-pos.at(e.second));
}
struct QualityInfo {
    double threshold{},edgeFactor{};
};
QualityInfo accepted(MixedClosureFaceKind kind,
                     const MixedPrivateClosureLimits& limits) {
    if(kind==MixedClosureFaceKind::Cap)
        return {limits.minCapQuality,limits.maxCapEdgeFactor};
    if(kind==MixedClosureFaceKind::Zipper)
        return {limits.minZipperQuality,limits.maxZipperEdgeFactor};
    return {limits.minSurvivorQuality,
            std::numeric_limits<double>::infinity()};
}
void validate(const MixedPrivateClosureLimits& settings) {
    if(!settings.maxInsertedVertices||!settings.maxNewFaces||
       !settings.maxRefinementSteps||!settings.maxAuditedFaces||
       !std::isfinite(settings.minSurvivorQuality)||
       !std::isfinite(settings.minCapQuality)||
       !std::isfinite(settings.minZipperQuality)||
       !std::isfinite(settings.maxCapEdgeFactor)||
       !std::isfinite(settings.maxZipperEdgeFactor)||
       !std::isfinite(settings.maxZipperBoundaryEdgeFactor)||
       settings.maxZipperBoundaryEdgeFactor<=0||
       settings.maxZipperBoundaryEdgeFactor>1||
       settings.minSurvivorQuality<=0||
       settings.minCapQuality<=0||settings.minCapQuality>=1||
       settings.minZipperQuality<=0||settings.minZipperQuality>=1||
       settings.maxCapEdgeFactor<=1||settings.maxZipperEdgeFactor<=1)
        throw std::invalid_argument("R06-M04B invalid finite quality/refinement limits");
}
} // namespace

MixedPrivateClosureResult applyPrivateMixedClosure(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const MixedPrivateClosureLimits& limits) {
    validate(limits);
    auto staged=stagePrivateMixedClosure(cut,proposal,plan,limits.staging);
    MixedPrivateClosureResult out;
    out.cappedLoops=staged.capLoops;
    out.zipperPairs=staged.zipperPairs;
    auto view=cut.index->revision().createMutableView();
    auto& faces=staged.faces;
    auto& positions=staged.positions;
    auto& vertexOwners=staged.vertexOwners;
    std::set<PartitionId> owners;

    const auto incident=[&](Edge e) {
        std::vector<std::size_t> result;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& v=faces[i].vertices;
            for(unsigned k=0;k<3;++k) {
                if(edge(v[k],v[(k+1)%3])==e) {
                    result.push_back(i);
                    break;
                }
            }
        }
        return result;
    };
    const auto splitQuality=[&](const MixedClosureFace& f,Edge candidate) {
        const auto& v=f.vertices;
        for(unsigned k=0;k<3;++k) {
            const auto a=v[k],b=v[(k+1)%3],c=v[(k+2)%3];
            if(edge(a,b)!=candidate)continue;
            const auto p=(positions.at(a)+positions.at(b))*.5;
            return std::min(quality(positions.at(a),p,positions.at(c)),
                            quality(p,positions.at(b),positions.at(c)));
        }
        throw std::logic_error("R06-M04B tracked face does not contain chosen edge");
    };
    const auto splitEdge=[&](Edge chosen) {
        const auto adjacent=incident(chosen);
        if(adjacent.size()!=2)
            throw std::logic_error("R06-M04B cannot split an edge without exactly two tracked faces");
        if(out.insertedVertices.size()>=limits.maxInsertedVertices)
            throw std::length_error("R06-M04B inserted vertex limit exceeded");
        const auto p=(positions.at(chosen.first)+positions.at(chosen.second))*.5;
        const auto owner=vertexOwners.at(chosen.first);
        const auto middle=view.addVertex(owner,p);
        out.insertedVertices.push_back(middle);
        positions.emplace(middle,p);
        vertexOwners.emplace(middle,owner);
        owners.insert(owner);
        for(const auto index:adjacent) {
            const auto parent=faces.at(index);
            bool matched=false;
            for(unsigned k=0;k<3;++k) {
                const auto a=parent.vertices[k];
                const auto b=parent.vertices[(k+1)%3];
                if(edge(a,b)!=chosen)continue;
                const auto c=parent.vertices[(k+2)%3];
                faces[index].vertices={a,middle,c};
                if(faces.size()>=limits.maxAuditedFaces+limits.maxNewFaces)
                    throw std::length_error("R06-M04B tracked face work limit");
                faces.push_back({0,parent.owner,{middle,b,c},
                                 parent.kind,parent.targetEdge});
                matched=true;
                break;
            }
            if(!matched)
                throw std::logic_error("R06-M04B lost one side of conforming edge split");
        }
    };

    // R04-like conforming PRE-subdivision along the true old-boundary /
    // new-zipper seam. A narrow zipper across sparsely sampled loops cannot
    // be fixed reliably by repeatedly bisecting skinny interior needles:
    // first shorten BOTH incident surface sides on the same actual edge.
    // This shares the single transaction and the same finite vertex/step
    // budget with later adaptive refinement (no private half-publication).
    while(true) {
        Edge worst{};
        double largestRatio=1;
        for(const auto& face:faces) {
            if(face.kind!=MixedClosureFaceKind::Zipper)continue;
            const auto& v=face.vertices;
            for(unsigned k=0;k<3;++k) {
                const auto candidate=edge(v[k],v[(k+1)%3]);
                const auto adjacent=incident(candidate);
                if(adjacent.size()!=2)continue;
                const auto first=faces[adjacent[0]].kind;
                const auto second=faces[adjacent[1]].kind;
                if(!((first==MixedClosureFaceKind::Survivor&&
                       second==MixedClosureFaceKind::Zipper)||
                      (first==MixedClosureFaceKind::Zipper&&
                       second==MixedClosureFaceKind::Survivor)))
                    continue;
                const auto ratio=edgeSpan(positions,candidate)/
                    (limits.maxZipperBoundaryEdgeFactor*face.targetEdge);
                if(ratio>largestRatio) {
                    largestRatio=ratio;
                    worst=candidate;
                }
            }
        }
        if(largestRatio<=1+1e-8)break;
        if(out.refinementSteps>=limits.maxRefinementSteps)
            throw std::length_error("R06-M04B zipper boundary density requires too many subdivisions");
        splitEdge(worst);
        ++out.refinementSteps;
    }

    std::size_t stagnant=0;
    for(std::size_t step=out.refinementSteps;
        step<limits.maxRefinementSteps;++step) {
        Edge longest{};
        double largestRatio=1;
        std::size_t lowest=faces.size();
        double smallest=1;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& f=faces[i];
            if(f.kind==MixedClosureFaceKind::Survivor)continue;
            const auto score=accepted(f.kind,limits);
            const auto& v=f.vertices;
            const auto q=quality(positions.at(v[0]),positions.at(v[1]),
                                 positions.at(v[2]));
            if(q<score.threshold&&q<smallest) {
                smallest=q;lowest=i;
            }
            for(unsigned k=0;k<3;++k) {
                const auto e=edge(v[k],v[(k+1)%3]);
                const auto ratio=edgeSpan(positions,e)/
                    (score.edgeFactor*f.targetEdge);
                if(ratio>largestRatio) {
                    largestRatio=ratio;
                    longest=e;
                }
            }
        }
        if(largestRatio<=1&&lowest==faces.size())break;
        Edge chosen=longest;
        if(largestRatio<=1) {
            double best=-1;
            const auto& candidate=faces[lowest];
            const auto v=candidate.vertices;
            for(unsigned k=0;k<3;++k) {
                const auto e=edge(v[k],v[(k+1)%3]);
                const auto adjacent=incident(e);
                if(adjacent.size()!=2)continue;
                double score=1;
                for(const auto index:adjacent)
                    score=std::min(score,splitQuality(faces[index],e));
                score+=1e-7*edgeSpan(positions,e)/candidate.targetEdge;
                if(score>best){best=score;chosen=e;}
            }
            if(best<0)
                throw std::logic_error("R06-M04B skinny cap/seam has no conforming two-sided split");
            if(best<=smallest*(1+1e-6))++stagnant;
            else stagnant=0;
        }else stagnant=0;
        if(stagnant>64)
            throw std::logic_error("R06-M04B refinement stagnation");
        splitEdge(chosen);
        ++out.refinementSteps;
    }
    for(const auto& f:faces) {
        const auto& v=f.vertices;
        const auto q=quality(positions.at(v[0]),positions.at(v[1]),
                             positions.at(v[2]));
        const auto requirements=accepted(f.kind,limits);
        if(f.kind==MixedClosureFaceKind::Survivor) {
            if(q<requirements.threshold)
                throw std::logic_error("R06-M04B conforming split left skinny original survivor");
            continue;
        }
        const auto largest=std::max({
            edgeSpan(positions,edge(v[0],v[1])),
            edgeSpan(positions,edge(v[1],v[2])),
            edgeSpan(positions,edge(v[2],v[0]))
        });
        if(q<requirements.threshold ||
           largest>f.targetEdge*requirements.edgeFactor*(1+1e-8))
            throw std::logic_error(
                "R06-M04B mixed closure remains skinny/elongated after finite refinement: kind="+
                std::string(f.kind==MixedClosureFaceKind::Cap?"CAP":"ZIPPER")+
                " vertices="+std::to_string(v[0])+","+
                              std::to_string(v[1])+","+
                              std::to_string(v[2])+
                " q="+std::to_string(q)+
                " required="+std::to_string(requirements.threshold)+
                " length_ratio="+
                std::to_string(largest/(f.targetEdge*requirements.edgeFactor))+
                " refinements="+std::to_string(out.refinementSteps)+
                " inserted_vertices="+std::to_string(out.insertedVertices.size()));
        if(f.kind==MixedClosureFaceKind::Cap)
            out.minimumCapQuality=std::min(out.minimumCapQuality,q);
        else
            out.minimumZipperQuality=std::min(out.minimumZipperQuality,q);
    }
    for(const auto& face:faces) {
        owners.insert(face.owner);
        if(face.stableId) {
            auto& partition=view.editPartition(face.owner);
            const auto it=std::find_if(partition.faces.begin(),
                                       partition.faces.end(),
                [&](const PartitionFace& old){return old.id==face.stableId;});
            if(it==partition.faces.end())
                throw std::logic_error("R06-M04B stable original FaceId missing");
            it->vertices=face.vertices;
        }else {
            if(out.newFaces>=limits.maxNewFaces)
                throw std::length_error("R06-M04B finite new-face limit exceeded");
            const auto id=view.addFace(face.owner,face.vertices);
            ++out.newFaces;
            if(face.kind==MixedClosureFaceKind::Cap)
                out.greenFaces.push_back(id);
            else if(face.kind==MixedClosureFaceKind::Zipper)
                out.blueFaces.push_back(id);
        }
    }
    out.touchedPartitions.assign(owners.begin(),owners.end());
    const auto revision=view.commit().revision;
    auto closed=RegionTopologyIndex::restore(
        revision,cut.index->retiredVertexIds(),cut.index->retiredFaceIds());
    validatePrivateClosedManifold(*closed,limits.maxAuditedFaces);
    for(const auto id:proposal.proposedFaces)
        if(closed->maybeFace(id)||!closed->retiredFace(id))
            throw std::logic_error("R06-M04B resurrected a retired R03 FaceId");
    out.index=std::move(closed);
    return out;
}
} // namespace mesher
