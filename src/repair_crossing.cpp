#include "mesher/repair_surgery.hpp"
#include "repair_plane_internal.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
Edge canonical(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
void accumulate(RevisionCommitStatistics& total,
                const RevisionCommitStatistics& next) {
    total.modifiedPartitions+=next.modifiedPartitions;
    total.clonedPartitions+=next.clonedPartitions;
    total.clonedPartitionBytes+=next.clonedPartitionBytes;
    total.copiedDirectoryNodes+=next.copiedDirectoryNodes;
}
bool sameInitialPlan(const RepairPlan& actual,const RepairPlan& requested) {
    if(!actual.ready()||!requested.ready()||
       actual.removedFaces!=requested.removedFaces||
       actual.contours.size()!=requested.contours.size()||
       actual.cappedContours!=requested.cappedContours||
       actual.pairs.size()!=requested.pairs.size())
        return false;
    for(std::size_t i=0;i<actual.contours.size();++i)
        if(actual.contours[i].vertexIds!=requested.contours[i].vertexIds||
           actual.contours[i].survivingFaces!=
               requested.contours[i].survivingFaces)
            return false;
    for(std::size_t i=0;i<actual.pairs.size();++i)
        if(actual.pairs[i].first!=requested.pairs[i].first||
           actual.pairs[i].second!=requested.pairs[i].second)
            return false;
    return true;
}
bool preservedCycle(const std::vector<VertexId>& before,
                    const std::vector<VertexId>& after) {
    if(after.size()<before.size()||before.empty())return false;
    const auto start=std::find(after.begin(),after.end(),before[0]);
    if(start==after.end())return false;
    const std::size_t offset=static_cast<std::size_t>(start-after.begin());
    std::size_t matched=0;
    for(std::size_t i=0;i<after.size();++i)
        if(after[(offset+i)%after.size()]==before[matched])
            if(++matched==before.size())return true;
    return false;
}
void requireSameClosureTopology(const RepairPlan& initial,
                                const RepairPlan& replanned) {
    if(!replanned.ready()||
       replanned.contours.size()!=initial.contours.size()||
       replanned.pairs.size()!=initial.pairs.size()||
       replanned.cappedContours.size()!=initial.cappedContours.size())
        throw std::logic_error(
            "Crossing normalization changed closure topology or made it ambiguous");

    std::vector<std::size_t> oldToNew(initial.contours.size(),
                                      replanned.contours.size());
    std::set<std::size_t> taken;
    for(std::size_t i=0;i<initial.contours.size();++i) {
        for(std::size_t j=0;j<replanned.contours.size();++j) {
            if(!preservedCycle(initial.contours[i].vertexIds,
                               replanned.contours[j].vertexIds))
                continue;
            if(oldToNew[i]!=replanned.contours.size()||
               !taken.insert(j).second)
                throw std::logic_error("Crossing normalization merged two distinct rings");
            oldToNew[i]=j;
        }
        if(oldToNew[i]==replanned.contours.size())
            throw std::logic_error(
                "Crossing normalization lost an original ordered contour");
    }
    std::set<std::size_t> oldCaps,newCaps(
        replanned.cappedContours.begin(),replanned.cappedContours.end());
    for(const auto id:initial.cappedContours)
        oldCaps.insert(oldToNew.at(id));
    if(oldCaps!=newCaps)
        throw std::logic_error("Crossing normalization unexpectedly changed cap choices");
    auto pairKey=[](std::size_t a,std::size_t b) {
        return std::pair<std::size_t,std::size_t>{std::min(a,b),std::max(a,b)};
    };
    std::set<std::pair<std::size_t,std::size_t>> oldPairs,newPairs;
    for(const auto& pair:initial.pairs)
        oldPairs.emplace(pairKey(oldToNew.at(pair.first),
                                 oldToNew.at(pair.second)));
    for(const auto& pair:replanned.pairs)
        newPairs.emplace(pairKey(pair.first,pair.second));
    if(oldPairs!=newPairs)
        throw std::logic_error(
            "Crossing normalization changed zipper partner topology");
}
} // namespace

MirroredCrossingRepair repairMirroredContoursAcrossPlane(
    std::shared_ptr<const RegionTopologyIndex> original,
    const RepairPlan& originalPlan,
    const std::vector<std::pair<VertexId,VertexId>>& vertexMirrors,
    const RepairSurgerySettings& settings,
    std::size_t maximumCrossings) {
    if(!original||!originalPlan.ready()||vertexMirrors.empty()||
       maximumCrossings==0)
        throw std::invalid_argument(
            "Crossing repair needs a ready original plan, mirror map and budget");
    // Verify the exact original snapshot and original topology choice BEFORE
    // inserting anything. Callers cannot supply a plan from another root.
    const auto current=planRemovalContours(
        *original,originalPlan.removedFaces,settings.planning);
    if(!sameInitialPlan(current,originalPlan))
        throw std::logic_error("Crossing repair plan is stale or incompatible");

    std::map<VertexId,VertexId> mirror;
    for(const auto& [a,b]:vertexMirrors) {
        const auto p=original->vertex(a).position;
        const auto q=original->vertex(b).position;
        if(p.x!=-q.x||p.y!=q.y||p.z!=q.z)
            throw std::logic_error("Crossing repair mirror coordinates are inconsistent");
        const auto [it,inserted]=mirror.emplace(a,b);
        if(!inserted&&it->second!=b)
            throw std::logic_error("Crossing repair has conflicting mirror IDs");
        const auto [jt,added]=mirror.emplace(b,a);
        if(!added&&jt->second!=a)
            throw std::logic_error("Crossing repair mirror map is not involutive");
    }
    std::set<Edge> crossingEdges;
    for(const auto& contour:current.contours) {
        std::vector<Vec3> points;
        points.reserve(contour.vertexIds.size());
        for(const auto id:contour.vertexIds)
            points.push_back(original->vertex(id).position);
        const auto report=detail::inspectPlaneContour(
            points,contour.meanEdgeLength);
        if(report.onPlaneEdges||report.nearlyOnPlaneVertices)
            throw std::logic_error(
                "Unclassified on-plane or near-plane contour cannot be normalized");
        if(report.seamVertices+report.crossingEdges!=0&&
           report.seamVertices+report.crossingEdges!=2)
            throw std::logic_error(
                "Contour must have exactly two combined plane contacts");
        for(std::size_t i=0;i<contour.vertexIds.size();++i) {
            const auto a=contour.vertexIds[i];
            const auto b=contour.vertexIds[(i+1)%contour.vertexIds.size()];
            const auto p=original->vertex(a).position;
            const auto q=original->vertex(b).position;
            if(!((p.x<0&&q.x>0)||(p.x>0&&q.x<0)))continue;
            const auto it=mirror.find(a);
            if(it==mirror.end()||it->second!=b||p.x!=-q.x||
               p.y!=q.y||p.z!=q.z)
                throw std::logic_error(
                    "Crossing boundary edge is not an exact reflected endpoint pair");
            crossingEdges.insert(canonical(a,b));
            if(crossingEdges.size()>maximumCrossings)
                throw std::length_error("Crossing-edge normalization budget exceeded");
        }
    }
    MirroredCrossingRepair result;
    // Existing fully conformed rings use the unchanged symmetric surgeon.
    if(crossingEdges.empty()) {
        result.normalizedPlan=current;
        result.surgery=applyMirroredRepairSurgery(
            original,current,vertexMirrors,settings);
        accumulate(result.accumulatedStatistics,result.surgery.commit.statistics);
        return result;
    }
    // Normalizations do NOT publish between edges. Every new face is
    // tracked: if its preserved half was marked for excision, its newly
    // introduced mirrored half MUST also be removed after re-planning.
    // Without this expansion, the surgery would leave a hanging sheet.
    if(crossingEdges.size()>=settings.maxNewVertices||
       crossingEdges.size()*2>=settings.maxNewFaces)
        throw std::length_error("Insufficient global surgery budget after normalization");
    auto normalized=std::move(original);
    std::set<FaceId> removal(current.removedFaces.begin(),
                             current.removedFaces.end());
    auto pairs=vertexMirrors;
    for(const auto edge:crossingEdges) {
        const auto sides=normalized->maybeEdgeSides(edge);
        if(!sides||sides->size()!=2)
            throw std::logic_error("Crossing edge vanished during normalization");
        const bool removeFirst=removal.contains(sides->at(0).face);
        const bool removeSecond=removal.contains(sides->at(1).face);
        if(removeFirst==removeSecond)
            throw std::logic_error(
                "A normalized boundary edge needs one retained and one removed side");
        const auto step=normalizeSelfMirroredPlaneEdge(
            normalized,edge.a,edge.b,pairs,settings.partitionCapacity);
        for(std::size_t i=0;i<2;++i) {
            if(removal.contains(step.preservedFaces[i]))
                removal.insert(step.insertedFaces[i]);
            result.normalizationInsertedFaces.push_back(step.insertedFaces[i]);
        }
        result.planeSeamVertices.push_back(step.seamVertex);
        pairs.emplace_back(step.seamVertex,step.seamVertex);
        accumulate(result.accumulatedStatistics,step.commit.statistics);
        normalized=step.index;
    }
    result.normalizedCrossingEdges=crossingEdges.size();
    // Budget for the *subsequent* scar must account for work already
    // spent normalizing the exact shared plane. Fail conservatively
    // if the remainder does not have meaningful space for new faces.
    auto remaining=settings;
    remaining.maxNewVertices-=crossingEdges.size();
    remaining.maxNewFaces-=2*crossingEdges.size();
    result.normalizedPlan=planRemovalContours(
        *normalized,{removal.begin(),removal.end()},settings.planning);
    requireSameClosureTopology(current,result.normalizedPlan);
    result.surgery=applyMirroredRepairSurgery(
        normalized,result.normalizedPlan,pairs,remaining);
    accumulate(result.accumulatedStatistics,result.surgery.commit.statistics);
    // Result.surgery.index is a descendant of ORIGINAL, but callers must
    // publish with publishIfCurrent(originalSnapshot,result.surgery.index)
    // exactly once. Intermediate normalization root identities never
    // escape through a repository publish.
    return result;
}
} // namespace mesher
