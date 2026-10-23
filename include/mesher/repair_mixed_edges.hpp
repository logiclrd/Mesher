#pragma once

#include "mesher/repair_mixed_stage.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {

// M14C: an edge is identified by its exact two VertexIds. Existing source
// geometry is discovered through the effective R03 index; pending original
// face overrides and new patch faces are considered in the same ledger.
// No approximate positions are used to invent incidence.
using MixedClosureEdge=std::pair<VertexId,VertexId>;
inline MixedClosureEdge canonicalMixedEdge(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
inline bool mixedFaceContainsEdge(PrivateRepairTriangle v,
                                  MixedClosureEdge e,bool* forward=nullptr) {
    for(unsigned k=0;k<3;++k) {
        const auto a=v[k],b=v[(k+1)%3];
        if(canonicalMixedEdge(a,b)==e) {
            if(forward)*forward=a==e.first;
            return true;
        }
    }
    return false;
}
struct MixedClosureEdgeNeighborhood {
    std::vector<std::size_t> localFaceIndexes;
    std::vector<FaceId> missingSurvivorFaces;
    bool conformingPair=false;
    std::size_t sides() const noexcept {
        return localFaceIndexes.size()+missingSurvivorFaces.size();
    }
};

// No write, no halo expansion. The original source edge's sides may include
// an original FaceId already present in the tracked local list. Deduplicate
// by stable ID; honor transaction overrides of original face connectivity.
inline MixedClosureEdgeNeighborhood inspectMixedClosureEdge(
    const std::vector<MixedClosureFace>& tracked,
    const MixedClosureSourceView& source,MixedClosureEdge e) {
    e=canonicalMixedEdge(e.first,e.second);
    if(e.first==e.second)throw std::logic_error("M14C degenerate edge");
    MixedClosureEdgeNeighborhood result;
    std::set<FaceId> localOriginalIds;
    std::vector<bool> orientations;
    for(std::size_t i=0;i<tracked.size();++i) {
        const auto& f=tracked[i];
        if(f.stableId) {
            if(!localOriginalIds.insert(f.stableId).second)
                throw std::logic_error("M14C duplicate tracked survivor FaceId");
        }
        bool forward=false;
        if(mixedFaceContainsEdge(f.triangle(),e,&forward)) {
            result.localFaceIndexes.push_back(i);
            orientations.push_back(forward);
        }
    }
    for(const auto& side:source.edgeSides({e.first,e.second})) {
        if(localOriginalIds.contains(side.face))continue;
        result.missingSurvivorFaces.push_back(side.face);
        orientations.push_back(side.forward);
    }
    result.conformingPair=result.sides()==2&&
        orientations[0]!=orientations[1];
    return result;
}

// Expand ONLY the exact source FaceIds participating in this edge. A read
// never silently adds triangles to the working refinement list. Both
// reflected edge neighborhoods must be inspected and preflighted BEFORE
// calling this function. Source ownership and positions are read-through.
inline void importMixedEdgeSurvivors(
    MixedClosureStaging& staged,const MixedClosureEdgeNeighborhood& candidate,
    std::size_t maxTrackedFaces) {
    if(staged.faces.size()+candidate.missingSurvivorFaces.size()>maxTrackedFaces)
        throw std::length_error("M14C finite source face halo expansion budget");
    for(const auto id:candidate.missingSurvivorFaces) {
        const auto& f=staged.sourceView->face(id);
        for(const auto v:f.vertices) {
            staged.positions.emplace(v,staged.sourceView->position(v));
            staged.vertexOwners.emplace(v,staged.sourceView->owner(v));
        }
        staged.survivingFaceIds.push_back(id);
        staged.faces.push_back({id,f.owner,{},MixedClosureFaceKind::Survivor,
                                0,staged.sourceView});
    }
}

// The topology portion of a split, after the caller has preflighted quality,
// mirrored-orbit ownership and budgets and registered ONE new midpoint ID.
// Both effective incident faces must already be in the tracked worklist;
// preserve each original face's stable ID and add its child as a new FaceId.
inline void applyMixedConformingEdgeSplit(
    std::vector<MixedClosureFace>& faces,MixedClosureEdge e,VertexId middle,
    std::size_t maxTrackedFaces) {
    e=canonicalMixedEdge(e.first,e.second);
    std::vector<std::size_t> adjacent;
    for(std::size_t i=0;i<faces.size();++i)
        if(mixedFaceContainsEdge(faces[i].triangle(),e))
            adjacent.push_back(i);
    if(adjacent.size()!=2||faces.size()+2>maxTrackedFaces)
        throw std::logic_error("M14C shared edge requires two tracked sides and room for children");
    bool leftForward=false,rightForward=false;
    (void)mixedFaceContainsEdge(faces[adjacent[0]].triangle(),e,&leftForward);
    (void)mixedFaceContainsEdge(faces[adjacent[1]].triangle(),e,&rightForward);
    if(leftForward==rightForward)
        throw std::logic_error("M14C shared edge orientation mismatch");
    for(const auto index:adjacent) {
        const auto parent=faces.at(index);
        const auto v=parent.triangle();
        for(unsigned k=0;k<3;++k) {
            const auto a=v[k],b=v[(k+1)%3];
            if(canonicalMixedEdge(a,b)!=e)continue;
            const auto c=v[(k+2)%3];
            faces[index].setTriangle({a,middle,c});
            faces.push_back({0,parent.owner,{middle,b,c},
                             parent.kind,parent.targetEdge});
            break;
        }
    }
}

} // namespace mesher
