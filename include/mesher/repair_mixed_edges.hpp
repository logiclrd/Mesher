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

} // namespace mesher
