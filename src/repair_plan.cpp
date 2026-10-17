#include "mesher/repair_plan.hpp"
#include "repair_closure_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
using Status=RepairPlanStatus;
using Kind=RegionTopologyIndex::FragmentKind;
struct BoundaryEdge {
    VertexId from{},to{};
    FaceId survivor{};
};
RepairPlan failure(RepairPlan plan,Status status,const char* reason) {
    plan.status=status;
    plan.reason=reason;
    return plan;
}
bool finiteSettings(const RepairPlanSettings& settings) {
    return settings.maxClassifiedFaces>0&&settings.maxRemovedFaces>0&&
           settings.maxBoundaryEdges>0&&settings.maxContours>0&&
           settings.sampleDepth<=4&&
           std::isfinite(settings.minimumHiddenFraction)&&
           settings.minimumHiddenFraction>0.5&&
           settings.minimumHiddenFraction<=1&&
           std::isfinite(settings.maximumGapInEdges)&&
           settings.maximumGapInEdges>0&&
           std::isfinite(settings.maximumPerimeterRatio)&&
           settings.maximumPerimeterRatio>=1&&
           std::isfinite(settings.minimumRelativeAreaAdvantage)&&
           settings.minimumRelativeAreaAdvantage>=0&&
           settings.minimumRelativeAreaAdvantage<0.5&&
           std::isfinite(settings.maximumPlanarityInRadii)&&
           settings.maximumPlanarityInRadii>=0&&
           std::isfinite(settings.maximumIsoperimetricRatio)&&
           settings.maximumIsoperimetricRatio>=1&&
           std::isfinite(settings.maximumSingleCapPlanarityInRadii)&&
           settings.maximumSingleCapPlanarityInRadii>=settings.maximumPlanarityInRadii&&
           std::isfinite(settings.maximumSingleCapIsoperimetricRatio)&&
           settings.maximumSingleCapIsoperimetricRatio>=
               settings.maximumIsoperimetricRatio&&
           std::isfinite(settings.minimumNormalAlignment)&&
           settings.minimumNormalAlignment>=0&&
           settings.minimumNormalAlignment<=1;
}
RegionTopologyIndex::Edge edgeKey(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
void appendComponent(const RegionTopologyIndex& index,FaceId seed,
                     std::size_t budget,std::set<FaceId>& removed) {
    std::queue<FaceId> pending;
    std::set<FaceId> visited{seed};
    pending.push(seed);
    while(!pending.empty()) {
        if(visited.size()>budget)return; // caller will reject over budget
        const auto face=pending.front();pending.pop();
        removed.insert(face);
        const auto& v=index.face(face).vertices;
        for(unsigned i=0;i<3;++i) {
            const auto sides=index.edgeSides(
                edgeKey(v[i],v[(i+1)%3]));
            for(const auto& side:sides)
                if(visited.insert(side.face).second)
                    pending.push(side.face);
        }
    }
}
} // namespace

RepairPlan planRemovalContours(const RegionTopologyIndex& index,
    const std::vector<FaceId>& proposedRemoval,
    const RepairPlanSettings& settings) {
    if(!finiteSettings(settings))
        throw std::invalid_argument("Invalid repair-plan bounds/thresholds");
    RepairPlan plan;
    std::set<FaceId> removed(proposedRemoval.begin(),proposedRemoval.end());
    if(removed.empty())return plan;
    if(removed.size()>settings.maxRemovedFaces)
        return failure(std::move(plan),Status::BudgetExceeded,
                       "Too many faces in proposed removal set");
    plan.removedFaces.assign(removed.begin(),removed.end());
    std::map<VertexId,BoundaryEdge> leaving;
    std::map<VertexId,VertexId> entering;
    for(const auto id:removed) {
        const auto& face=index.face(id);
        const auto& tri=face.vertices;
        for(unsigned i=0;i<3;++i) {
            const auto a=tri[i],b=tri[(i+1)%3];
            plan.maximumRemovedEdgeLength=std::max(
                plan.maximumRemovedEdgeLength,
                length(index.vertex(a).position-index.vertex(b).position));
            const auto sides=index.maybeEdgeSides(edgeKey(a,b));
            if(!sides||sides->size()!=2)
                return failure(std::move(plan),Status::InvalidBoundary,
                               "Open or nonmanifold edge at proposed cut");
            FaceId retained=0;
            for(const auto& side:*sides)
                if(!removed.contains(side.face))retained=side.face;
            if(!retained)continue;
            // Retained orientation owns the directed border; a zipper must
            // use the reverse edge to preserve manifold face orientation.
            const auto& survivor=index.face(retained).vertices;
            bool found=false;
            for(unsigned j=0;j<3;++j) {
                if(survivor[j]!=b||survivor[(j+1)%3]!=a)continue;
                const BoundaryEdge candidate{b,a,retained};
                if(!leaving.emplace(b,candidate).second||
                   !entering.emplace(a,b).second)
                    return failure(std::move(plan),Status::InvalidBoundary,
                                   "Cut boundary branches at a vertex");
                found=true;
                ++plan.boundaryEdges;
                break;
            }
            if(!found)
                return failure(std::move(plan),Status::InvalidBoundary,
                               "Surviving and deleted faces have inconsistent winding");
            if(plan.boundaryEdges>settings.maxBoundaryEdges)
                return failure(std::move(plan),Status::BudgetExceeded,
                               "Boundary edge budget exceeded");
        }
    }
    if(leaving.size()!=entering.size())
        return failure(std::move(plan),Status::InvalidBoundary,
                       "Open contour endpoints are not allowed for zipper loops");
    for(const auto& [vertex,_]:leaving) {
        if(!entering.contains(vertex))
            return failure(std::move(plan),Status::InvalidBoundary,
                           "Cut boundary has a dangling endpoint");
    }
    std::set<VertexId> visited;
    for(const auto& [seed,_]:leaving) {
        if(visited.contains(seed))continue;
        RepairContour contour;
        auto current=seed;
        do {
            if(!visited.insert(current).second)
                return failure(std::move(plan),Status::InvalidBoundary,
                               "Boundary loop revisits an internal vertex");
            const auto it=leaving.find(current);
            if(it==leaving.end())
                return failure(std::move(plan),Status::InvalidBoundary,
                               "Boundary loop has a missing successor");
            contour.vertexIds.push_back(current);
            contour.survivingFaces.push_back(it->second.survivor);
            const auto next=it->second.to;
            const double segment=length(index.vertex(current).position-
                                        index.vertex(next).position);
            if(!std::isfinite(segment)||segment<=0)
                return failure(std::move(plan),Status::InvalidBoundary,
                               "Zero-length or nonfinite contour segment");
            contour.perimeter+=segment;
            current=next;
        }while(current!=seed);
        if(contour.vertexIds.size()<3)
            return failure(std::move(plan),Status::InvalidBoundary,
                           "A seam ring needs at least three vertices");
        contour.meanEdgeLength=contour.perimeter/contour.vertexIds.size();
        plan.contours.push_back(std::move(contour));
        if(plan.contours.size()>settings.maxContours)
            return failure(std::move(plan),Status::BudgetExceeded,
                           "Too many boundary loops");
    }
    if(visited.size()!=leaving.size())
        return failure(std::move(plan),Status::InvalidBoundary,
                       "Boundary construction left unvisited directed edges");
    // Fully removed isolated closed shells have no cut edges and no seam.
    if(plan.contours.empty()) {
        plan.status=Status::Ready;
        return plan;
    }
    return selectRepairClosures(index,std::move(plan),settings);
}

RepairPlan planApproximateExcision(const RegionTopologyIndex& index,
    const std::vector<VertexId>& changedVertices,
    const std::vector<FaceId>& changedFaces,
    const std::vector<FaceId>& componentSeeds,
    const RepairPlanSettings& settings) {
    if(!finiteSettings(settings))
        throw std::invalid_argument("Invalid repair-plan bounds/thresholds");
    RepairPlan summary;
    const auto intersections=index.triangleIntersections(
        changedVertices,changedFaces);
    summary.intersections=intersections.size();
    std::set<FaceId> removed;
    // Explicitly nominated new disconnected components may be discarded
    // only when V14 identifies the WHOLE closed component as redundant.
    for(const auto seed:componentSeeds) {
        const auto component=index.classifyClosedComponent(
            seed,settings.maxRemovedFaces,
            settings.allowReferenceWindingFallback);
        if(!component.complete)
            return failure(std::move(summary),Status::BudgetExceeded,
                           "Closed-component traversal exceeded budget");
        if(component.kind==RegionTopologyIndex::ComponentKind::Ambiguous)
            return failure(std::move(summary),Status::AmbiguousClassification,
                           "Isolated component cannot be classified safely");
        if(component.kind!=RegionTopologyIndex::ComponentKind::Redundant)
            continue; // preserve genuine shell/cavity
        appendComponent(index,seed,settings.maxRemovedFaces,removed);
        if(removed.size()>settings.maxRemovedFaces)
            return failure(std::move(summary),Status::BudgetExceeded,
                           "Redundant isolated components exceed removal budget");
    }
    std::queue<FaceId> pending;
    std::set<FaceId> discovered;
    for(const auto& pair:intersections) {
        if(pair.ambiguous)
            return failure(std::move(summary),Status::AmbiguousClassification,
                           "V13 reported an ambiguous triangle contact");
        for(const auto id:{pair.first,pair.second})
            if(discovered.insert(id).second)pending.push(id);
    }
    while(!pending.empty()) {
        if(summary.classifiedFaces>=settings.maxClassifiedFaces)
            return failure(std::move(summary),Status::BudgetExceeded,
                           "Classification face budget exceeded");
        const auto id=pending.front();pending.pop();
        ++summary.classifiedFaces;
        const auto samples=index.classifyFaceFragments(
            id,settings.sampleDepth,settings.allowReferenceWindingFallback);
        std::size_t hidden=0,exposed=0,ambiguous=0;
        for(const auto& sample:samples) {
            if(sample.kind==Kind::Buried||sample.kind==Kind::Outside)
                ++hidden;
            else if(sample.kind==Kind::Ambiguous)++ambiguous;
            else ++exposed;
        }
        if(ambiguous) {
            ++summary.uncertainFaces;
            return failure(std::move(summary),Status::AmbiguousClassification,
                           "A candidate face has uncertain winding patches");
        }
        if(hidden&&exposed)++summary.mixedFaces;
        if(hidden==0||static_cast<double>(hidden)/samples.size()<
                      settings.minimumHiddenFraction)continue;
        removed.insert(id);
        if(removed.size()>settings.maxRemovedFaces)
            return failure(std::move(summary),Status::BudgetExceeded,
                           "Too many removable faces");
        const auto& vertices=index.face(id).vertices;
        for(unsigned i=0;i<3;++i) {
            const auto sides=index.maybeEdgeSides(
                edgeKey(vertices[i],vertices[(i+1)%3]));
            if(!sides||sides->size()!=2)
                return failure(std::move(summary),Status::InvalidBoundary,
                               "Collision neighborhood has nonmanifold edges");
            for(const auto& side:*sides)
                if(discovered.insert(side.face).second)pending.push(side.face);
        }
    }
    if(!intersections.empty()&&removed.empty())
        return failure(std::move(summary),Status::AmbiguousClassification,
                       "Intersecting sheets had no sufficiently hidden faces");
    auto plan=planRemovalContours(index,
        std::vector<FaceId>(removed.begin(),removed.end()),settings);
    plan.intersections=summary.intersections;
    plan.classifiedFaces=summary.classifiedFaces;
    plan.uncertainFaces=summary.uncertainFaces;
    plan.mixedFaces=summary.mixedFaces;
    return plan;
}
} // namespace mesher
