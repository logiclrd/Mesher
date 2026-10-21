#include "mesher/repair_preview.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
using Kind=RegionTopologyIndex::FragmentKind;
RegionTopologyIndex::Edge key(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
std::vector<FaceId> adjacent(const RegionTopologyIndex& candidate,FaceId id) {
    std::set<FaceId> ids;
    const auto& face=candidate.face(id).vertices;
    for(unsigned i=0;i<3;++i) {
        const auto sides=candidate.maybeEdgeSides(
            key(face[i],face[(i+1)%3]));
        if(!sides||sides->size()!=2)
            throw std::runtime_error("Non-manifold input at proposed excision boundary");
        for(const auto& side:*sides)if(side.face!=id)ids.insert(side.face);
    }
    return {ids.begin(),ids.end()};
}
std::size_t contactIslandCount(
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts) {
    // Canonical FaceIds are graph vertices; a contact pair is a graph edge.
    // Separate geometric contact clusters need not correspond to separate
    // proposed face deletions or to pairs of closure loops.
    std::map<FaceId,std::set<FaceId>> graph;
    for(const auto& pair:contacts) {
        graph[pair.first].insert(pair.second);
        graph[pair.second].insert(pair.first);
    }
    std::set<FaceId> seen;
    std::size_t count=0;
    for(const auto& [seed,_]:graph) {
        if(seen.contains(seed))continue;
        ++count;
        std::vector<FaceId> stack{seed};
        while(!stack.empty()) {
            const auto node=stack.back();stack.pop_back();
            if(!seen.insert(node).second)continue;
            for(const auto next:graph.at(node))stack.push_back(next);
        }
    }
    return count;
}
std::size_t excisionComponentCount(
    const RegionTopologyIndex& candidate,
    const std::vector<FaceId>& proposed) {
    std::set<FaceId> removed(proposed.begin(),proposed.end());
    std::set<FaceId> seen;
    std::size_t count=0;
    for(const auto seed:removed) {
        if(seen.contains(seed))continue;
        ++count;
        std::vector<FaceId> pending{seed};
        while(!pending.empty()) {
            const auto id=pending.back();pending.pop_back();
            if(!seen.insert(id).second)continue;
            for(const auto next:adjacent(candidate,id))
                if(removed.contains(next))pending.push_back(next);
        }
    }
    return count;
}
}
RepairExcisionPreview previewApproximateExcision(
    const RegionTopologyIndex& candidate,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const RepairPreviewLimits& limits) {
    if(!limits.maxClassifiedFaces||!limits.maxProposedFaces||
       !limits.maxContours||limits.sampleDepth>4||
       !std::isfinite(limits.minimumHiddenFraction)||
       limits.minimumHiddenFraction<=.5||limits.minimumHiddenFraction>1)
        throw std::invalid_argument("Invalid R02 classification budgets");
    RepairExcisionPreview view;
    view.contactIslands=contactIslandCount(contacts);
    view.complete=true;
    if(contacts.empty())return view;
    std::queue<FaceId> pending;
    std::set<FaceId> scheduled;
    for(const auto& pair:contacts) {
        if(pair.ambiguous) {
            view.complete=false;
            view.diagnostic="R02 triangle-contact classification is ambiguous";
        }
        for(const auto id:{pair.first,pair.second})
            if(scheduled.insert(id).second)pending.push(id);
    }
    std::set<FaceId> proposed,uncertain,mixed;
    while(!pending.empty()) {
        if(view.classifiedFaces>=limits.maxClassifiedFaces) {
            view.complete=false;
            view.diagnostic="R02 face-classification work limit exhausted";
            break;
        }
        const auto id=pending.front();pending.pop();
        ++view.classifiedFaces;
        try {
            const auto patches=candidate.classifyFaceFragments(
                id,limits.sampleDepth,false); // never perform O(all) fallback
            std::size_t hidden=0,visible=0,indeterminate=0;
            for(const auto& patch:patches) {
                if(patch.kind==Kind::Buried||patch.kind==Kind::Outside)
                    ++hidden;
                else if(patch.kind==Kind::Ambiguous)
                    ++indeterminate;
                else ++visible;
            }
            if(indeterminate) {
                uncertain.insert(id);
                view.complete=false;
                if(view.diagnostic.empty())
                    view.diagnostic="R02 V14 sample winding is uncertain; no deletion certified for purple faces";
                continue;
            }
            if(hidden&&visible)mixed.insert(id);
            if(!hidden||static_cast<double>(hidden)/patches.size()<
                        limits.minimumHiddenFraction)continue;
            if(proposed.size()>=limits.maxProposedFaces) {
                view.complete=false;
                view.diagnostic="R02 selected-face work limit exhausted";
                break;
            }
            proposed.insert(id);
            // Expand ONLY through confirmed removed triangles, as in
            // the V15 bounded approximate-excision planner.
            for(const auto neighbor:adjacent(candidate,id))
                if(scheduled.insert(neighbor).second)pending.push(neighbor);
        }catch(const std::length_error&) {
            view.complete=false;
            uncertain.insert(id);
            view.diagnostic="R02 winding query work limit exhausted";
            break;
        }catch(const std::runtime_error& e) {
            view.complete=false;
            uncertain.insert(id);
            view.diagnostic=std::string("R02 classification error: ")+e.what();
            break;
        }
    }
    view.proposedFaces.assign(proposed.begin(),proposed.end());
    view.uncertainFaces.assign(uncertain.begin(),uncertain.end());
    view.mixedFaces.assign(mixed.begin(),mixed.end());
    if(view.proposedFaces.empty()) {
        if(view.diagnostic.empty()) {
            view.complete=false;
            view.diagnostic="R02 contacts exist but no confidently buried candidate faces were identified";
        }
        return view;
    }
    // Planning is read-only. Closure failure is *not* a reason to erase
    // useful diagnostic loops or to authorize a guessed hole pairing.
    try {
        RepairPlanSettings settings;
        settings.maxRemovedFaces=limits.maxProposedFaces;
        settings.maxBoundaryEdges=limits.maxProposedFaces*3;
        settings.maxContours=limits.maxContours;
        const auto plan=planRemovalContours(candidate,view.proposedFaces,settings);
        view.contourStatus=plan.status;
        view.contours=plan.contours;
        view.boundaryEdges=plan.boundaryEdges;
        view.excisionComponents=excisionComponentCount(candidate,view.proposedFaces);
        if(plan.status!=RepairPlanStatus::Ready) {
            view.complete=false;
            if(view.diagnostic.empty())
                view.diagnostic="R02 boundary / closure evidence: "+plan.reason;
        }
    }catch(const std::exception& e) {
        view.complete=false;
        view.diagnostic=std::string("R02 oriented-contour planning failed: ")+e.what();
    }
    return view;
}
} // namespace mesher
