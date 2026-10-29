#include "mesher/repair_intersection.hpp"
#include "mesher/vertex_paint.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {

// The cut planner supplies exact source-edge or source-face provenance.
// Project the new point onto that source simplex and interpolate its paint;
// proximity to a separate intersecting sheet is never a colour authority.
VertexRGBA plannedCutPointColor(const RegionTopologyIndex& baseline,
                                const ConformingCutPoint& point) {
    if(point.existingVertex)
        return vertexColorAt(baseline,*point.existingVertex);
    const auto combine=[](const std::array<VertexRGBA,3>& c,
                          std::array<double,3> w) {
        for(auto& value:w)value=std::clamp(value,0.0,1.0);
        const double sum=w[0]+w[1]+w[2];
        if(!(sum>0)||!std::isfinite(sum))
            throw std::logic_error("Cut color interpolation has invalid weights");
        for(auto& value:w)value/=sum;
        const auto channel=[&](float VertexRGBA::*member) {
            return static_cast<float>(
                double(c[0].*member)*w[0]+double(c[1].*member)*w[1]+
                double(c[2].*member)*w[2]);
        };
        return VertexRGBA{channel(&VertexRGBA::r),channel(&VertexRGBA::g),
                          channel(&VertexRGBA::b),channel(&VertexRGBA::a)};
    };
    if(point.boundaryEdge) {
        const auto edge=*point.boundaryEdge;
        const auto a=baseline.vertex(edge.a).position;
        const auto b=baseline.vertex(edge.b).position;
        const auto ab=b-a;
        const double lengthSquared=dot(ab,ab);
        if(!(lengthSquared>0)||!std::isfinite(lengthSquared))
            throw std::logic_error("Cut color interpolation has degenerate edge");
        const double t=dot(point.position-a,ab)/lengthSquared;
        if(!std::isfinite(t))throw std::logic_error("Non-finite cut color weight");
        return combine({vertexColorAt(baseline,edge.a),
                        vertexColorAt(baseline,edge.b),VertexRGBA{}},
                       {1.0-t,t,0.0});
    }
    const auto triangle=baseline.face(point.owningFace).vertices;
    const auto a=baseline.vertex(triangle[0]).position;
    const auto b=baseline.vertex(triangle[1]).position;
    const auto c=baseline.vertex(triangle[2]).position;
    const auto ab=b-a,ac=c-a,ap=point.position-a;
    const double d00=dot(ab,ab),d01=dot(ab,ac),d11=dot(ac,ac);
    const double d20=dot(ap,ab),d21=dot(ap,ac);
    const double denominator=d00*d11-d01*d01;
    if(!(denominator>0)||!std::isfinite(denominator))
        throw std::logic_error("Cut color interpolation has degenerate face");
    const double u=(d11*d20-d01*d21)/denominator;
    const double v=(d00*d21-d01*d20)/denominator;
    if(!std::isfinite(u)||!std::isfinite(v))
        throw std::logic_error("Non-finite interior cut color weight");
    return combine({vertexColorAt(baseline,triangle[0]),
                    vertexColorAt(baseline,triangle[1]),
                    vertexColorAt(baseline,triangle[2])},{1.0-u-v,u,v});
}

ConformingCutCandidate materializeMirroredTransverseCutCandidate(
    std::shared_ptr<const RegionTopologyIndex>,
    const std::vector<RegionTopologyIndex::CollisionPair>&,
    const ConformingCutLimits&,std::size_t,
    const std::unordered_map<VertexId,VertexId>&);

ConformingCutCandidate materializeTransverseCutCandidate(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ConformingCutLimits& limits,
    std::size_t partitionCapacity,
    const std::unordered_map<VertexId,VertexId>* mirrors) {
    if(!baseline||contacts.empty()||partitionCapacity==0)
        throw std::invalid_argument(
            "Conforming revision needs a baseline, contacts and positive capacity");
    if(mirrors)
        return materializeMirroredTransverseCutCandidate(
            baseline,contacts,limits,partitionCapacity,*mirrors);

    // Never accept a caller-forged local tessellation as an authorization
    // to edit permanent IDs. The read-only planner re-checks V13 geometry,
    // local area/winding, every incident edge, and all bounded budgets.
    const auto plan=buildConformingCutPlan(*baseline,contacts,limits);
    if(plan.status!=IntersectionSectionStatus::TransverseSegment||
       plan.faces.empty()||plan.segments.empty())
        throw std::logic_error("Conforming candidate refused: "+plan.refusal);

    // RegionEdit's complete writable halo includes the owners of all
    // selected vertices and their incident faces. Seeds here cover every
    // affected source face AND its shared-edge neighbors.
    std::set<VertexId> selected;
    for(const auto& part:plan.faces) {
        if(part.triangles.empty())
            throw std::logic_error("Conforming plan contains empty changed face");
        for(const auto id:baseline->face(part.sourceFace).vertices)
            selected.insert(id);
    }
    if(selected.empty())
        throw std::logic_error("Conforming candidate has no affected vertices");

    RegionEdit edit(baseline,{selected.begin(),selected.end()}, {},
                    partitionCapacity);
    ConformingCutCandidate output;
    output.originalIndex=baseline;
    output.sourceContacts=contacts;
    std::vector<VertexId> assigned;
    assigned.reserve(plan.points.size());
    for(const auto& point:plan.points) {
        if(point.existingVertex) {
            const auto vertex=baseline->maybeVertex(*point.existingVertex);
            if(!vertex||vertex->position.x!=point.position.x||
               vertex->position.y!=point.position.y||
               vertex->position.z!=point.position.z)
                throw std::logic_error("Conforming original vertex changed beneath plan");
            assigned.push_back(*point.existingVertex);
            continue;
        }
        PartitionId owner{};
        if(point.boundaryEdge) {
            const auto& edge=*point.boundaryEdge;
            if(!baseline->maybeEdgeSides(edge))
                throw std::logic_error("Cut edge is absent from immutable baseline");
            owner=baseline->vertex(edge.a).owner;
        }else {
            if(!baseline->maybeFace(point.owningFace))
                throw std::logic_error("Interior cut source is not a live face");
            owner=baseline->face(point.owningFace).owner;
        }
        const auto id=edit.appendLocalVertex(owner,point.position);
        edit.setVertexColor(id,plannedCutPointColor(*baseline,point));
        assigned.push_back(id);
        output.insertedVertices.push_back(id);
    }

    // Source FaceIds remain stable; additional child triangles receive
    // brand-new IDs within the same privately staged RegionEdit.
    for(const auto& patch:plan.faces) {
        const auto& original=baseline->face(patch.sourceFace);
        const auto transform=[&](std::array<std::size_t,3> triangle) {
            for(auto& index:triangle)
                if(index>=assigned.size())
                    throw std::logic_error("Conforming triangle index out of range");
            return std::array<VertexId,3>{
                assigned[triangle[0]],assigned[triangle[1]],
                assigned[triangle[2]]};
        };
        edit.replaceFace(patch.sourceFace,transform(patch.triangles.front()));
        output.rewrittenFaces.push_back(patch.sourceFace);
        output.childSources.emplace_back(patch.sourceFace,patch.sourceFace);
        for(std::size_t i=1;i<patch.triangles.size();++i) {
            const auto id=edit.appendLocalFace(
                original.owner,transform(patch.triangles[i]));
            output.insertedFaces.push_back(id);
            output.childSources.emplace_back(id,patch.sourceFace);
        }
    }
    for(const auto& segment:plan.segments) {
        std::array<VertexId,2> first{},second{};
        for(unsigned i=0;i<2;++i) {
            if(segment.firstSide[i]>=assigned.size()||
               segment.secondSide[i]>=assigned.size())
                throw std::logic_error("Conforming contact references unknown cut");
            first[i]=assigned[segment.firstSide[i]];
            second[i]=assigned[segment.secondSide[i]];
            if(first[i]==second[i])
                throw std::logic_error("Independent collision sheets were pre-welded");
        }
        output.sectionFirst.push_back(first);
        output.sectionSecond.push_back(second);
    }

    // This enforces complete 2-manifold links of every changed vertex and
    // opposing directions of every affected edge. Even a failed commit
    // cannot publish edits: the only mutable view is owned by this scope.
    output.commit=edit.commit();
    output.index=edit.updatedIndex();
    if(!output.index||output.index==baseline)
        throw std::logic_error("Private cut commit omitted revised immutable root");
    return output;
}

} // namespace mesher
