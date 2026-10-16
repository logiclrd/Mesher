#include "mesher/repair_plan.hpp"

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
struct CandidateMatch {
    RepairContourPair pair;
    double squaredError{};
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
           settings.maximumPerimeterRatio>=1;
}
std::vector<Vec3> resample(const RegionTopologyIndex& index,
                           const RepairContour& contour) {
    constexpr std::size_t count=32;
    std::vector<Vec3> vertices;
    vertices.reserve(contour.vertexIds.size());
    for(auto id:contour.vertexIds)vertices.push_back(index.vertex(id).position);
    std::vector<double> accumulated(vertices.size()+1,0);
    for(std::size_t i=0;i<vertices.size();++i)
        accumulated[i+1]=accumulated[i]+
            length(vertices[(i+1)%vertices.size()]-vertices[i]);
    std::vector<Vec3> output;
    output.reserve(count);
    std::size_t edge=0;
    for(std::size_t i=0;i<count;++i) {
        const double target=contour.perimeter*static_cast<double>(i)/count;
        while(edge+1<vertices.size()&&accumulated[edge+1]<target)++edge;
        const auto span=accumulated[edge+1]-accumulated[edge];
        const double t=span>0?(target-accumulated[edge])/span:0;
        output.push_back(vertices[edge]*(1-t)+
                         vertices[(edge+1)%vertices.size()]*t);
    }
    return output;
}
CandidateMatch bestMatch(const RegionTopologyIndex& index,
                         const RepairContour& a,const RepairContour& b,
                         std::size_t first,std::size_t second) {
    const auto x=resample(index,a),y=resample(index,b);
    CandidateMatch best{};
    best.pair.first=first;
    best.pair.second=second;
    best.pair.maxSampleDistance=std::numeric_limits<double>::infinity();
    best.squaredError=std::numeric_limits<double>::infinity();
    for(unsigned reversed=0;reversed<2;++reversed) {
        for(std::size_t shift=0;shift<y.size();++shift) {
            double maxDistance=0,squared=0;
            for(std::size_t k=0;k<x.size();++k) {
                const std::size_t position=
                    reversed?(shift+y.size()-k)%y.size():
                             (shift+k)%y.size();
                const auto difference=x[k]-y[position];
                const double d=length(difference);
                maxDistance=std::max(maxDistance,d);
                squared+=dot(difference,difference);
            }
            if(maxDistance<best.pair.maxSampleDistance||
               (maxDistance==best.pair.maxSampleDistance&&
                squared<best.squaredError)) {
                best.pair.reverseSecond=reversed!=0;
                best.pair.sampleOffset=shift;
                best.pair.maxSampleDistance=maxDistance;
                best.squaredError=squared;
            }
        }
    }
    const double h=std::min(a.meanEdgeLength,b.meanEdgeLength);
    best.pair.gapInEdges=h>0?best.pair.maxSampleDistance/h:
                          std::numeric_limits<double>::infinity();
    return best;
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
    if(plan.contours.size()%2!=0)
        return failure(std::move(plan),Status::UnmatchedContours,
                       "An odd number of seam contours cannot be paired");

    std::vector<CandidateMatch> candidates;
    for(std::size_t i=0;i<plan.contours.size();++i) {
        const auto& a=plan.contours[i];
        for(std::size_t j=i+1;j<plan.contours.size();++j) {
            const auto& b=plan.contours[j];
            const double ratio=std::max(a.perimeter,b.perimeter)/
                               std::min(a.perimeter,b.perimeter);
            if(ratio>settings.maximumPerimeterRatio)continue;
            const auto candidate=bestMatch(index,a,b,i,j);
            if(candidate.pair.gapInEdges<=settings.maximumGapInEdges)
                candidates.push_back(candidate);
        }
    }
    std::sort(candidates.begin(),candidates.end(),
              [](const auto& a,const auto& b) {
        if(a.pair.gapInEdges!=b.pair.gapInEdges)
            return a.pair.gapInEdges<b.pair.gapInEdges;
        if(a.squaredError!=b.squaredError)
            return a.squaredError<b.squaredError;
        return std::pair{a.pair.first,a.pair.second}<
               std::pair{b.pair.first,b.pair.second};
    });
    std::vector<bool> assigned(plan.contours.size(),false);
    for(const auto& candidate:candidates) {
        if(assigned[candidate.pair.first]||assigned[candidate.pair.second])
            continue;
        assigned[candidate.pair.first]=assigned[candidate.pair.second]=true;
        plan.maximumSeamGapInEdges=std::max(plan.maximumSeamGapInEdges,
                                            candidate.pair.gapInEdges);
        plan.pairs.push_back(candidate.pair);
    }
    if(plan.pairs.size()*2!=plan.contours.size())
        return failure(std::move(plan),Status::UnmatchedContours,
                       "No compatible nearby partner for every contour");
    plan.status=Status::Ready;
    return plan;
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
            seed,settings.maxRemovedFaces);
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
        const auto samples=index.classifyFaceFragments(id,settings.sampleDepth);
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
