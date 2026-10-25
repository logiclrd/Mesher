#pragma once

#include "mesher/repair_cap_feasibility.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace mesher {

// M19C diagnostic ONLY. Tests diagonal changes in existing strip quads;
// returns metrics, not topology. The original patch and source rim are
// immutable and no trial is admitted to worker/history/checkpoints.
struct SpatialCapDiagonalProbe {
    double baselineWorstSeamRadians{},bestWorstSeamRadians{};
    double baselineWorstCapRimRadians{},bestWorstCapRimRadians{};
    double baselineMinimumQuality{},bestMinimumQuality{};
    double baselineMeanScore{},bestMeanScore{};
    std::size_t candidates{},admissible{},acceptedRounds{},qualityRejected{},
                orientationRejected{},scoreRejected{},contactRejected{};
    bool budgetExhausted{},improved{};
    // One accepted representative and its exact reflected strip quad
    // per round. In an ordinary cap the second ordinal equals the first.
    // M19D1 is pure provenance, not authorization for full-mesh staging.
    std::vector<std::array<std::size_t,2>> acceptedQuadOrbits;
};
struct SpatialCapDiagonalProbeLimits {
    std::size_t maxRounds=2,maxCandidates=48;
    double minimumQuality=.1;
};

namespace diagonal_detail {
inline bool flipOne(SpatialCapPatch& patch,std::size_t i) {
    const auto n=patch.boundaryCount;
    if(i>=n||patch.faces.size()!=3*n)return false;
    const auto a=static_cast<std::uint32_t>(i);
    const auto b=static_cast<std::uint32_t>((i+1)%n);
    const auto c=static_cast<std::uint32_t>(n)+b;
    const auto d=static_cast<std::uint32_t>(n)+a;
    const std::array<std::uint32_t,3> first{a,b,d},second{b,c,d};
    const std::array<std::uint32_t,3> altFirst{a,b,c},altSecond{a,c,d};
    if(patch.faces[3*i]==first&&patch.faces[3*i+1]==second) {
        patch.faces[3*i]=altFirst;
        patch.faces[3*i+1]=altSecond;
        return true;
    }
    if(patch.faces[3*i]==altFirst&&patch.faces[3*i+1]==altSecond) {
        patch.faces[3*i]=first;
        patch.faces[3*i+1]=second;
        return true;
    }
    return false;
}
inline bool pairedFlip(SpatialCapPatch& patch,std::size_t i,
                       const std::vector<std::size_t>& partner) {
    const auto n=patch.boundaryCount;
    if(!flipOne(patch,i))return false;
    if(partner.empty())return true;
    const auto j=(i+1)%n;
    const auto mirroredStart=partner.at(j);
    const auto mirroredEnd=partner.at(i);
    if((mirroredStart+1)%n!=mirroredEnd)return false;
    if(mirroredStart==i)return true;
    return flipOne(patch,mirroredStart);
}
}

// Tries every original strip quad in stable ordinal order and its EXACT
// reflected counterpart, if any. Independent one-/two-round candidate
// exploration keeps contact work finite. A missing admissible flip is
// evidence about this bounded search, NOT mathematical impossibility.
template<class ValidateCap>
SpatialCapDiagonalProbe probeSpatialCapDiagonalFeasibility(
    const SpatialCapPatch& original,
    const std::vector<Vec3>& incomingNormals,
    const std::vector<std::size_t>& mirrorOrbits,
    ValidateCap&& validateCap,
    const SpatialCapDiagonalProbeLimits& limits={},
    SpatialCapPatch* bestCandidate=nullptr) {
    const auto n=original.boundaryCount;
    if(n<3||original.faces.size()!=3*n||
       incomingNormals.size()!=n||
       (!mirrorOrbits.empty()&&mirrorOrbits.size()!=original.positions.size())||
       !limits.maxRounds||!limits.maxCandidates||
       !(limits.minimumQuality>0&&limits.minimumQuality<1))
        throw std::invalid_argument("R06-M19C requires indexed spatial-strip cap");
    SpatialCapDiagonalProbe out;
    const auto start=feasibility_detail::assess(original,incomingNormals);
    out.baselineWorstSeamRadians=start.seam;
    out.baselineWorstCapRimRadians=start.capRim;
    out.baselineMeanScore=start.mean;
    out.baselineMinimumQuality=start.minimumQuality;
    auto best=original;
    auto metrics=start;
    for(std::size_t round=0;round<limits.maxRounds;++round) {
        auto roundPatch=best;
        auto roundMetric=metrics;
        std::array<std::size_t,2> roundChoice{n,n};
        bool stopped=false;
        for(std::size_t i=0;i<n;++i) {
            if(out.candidates>=limits.maxCandidates) {
                stopped=true;break;
            }
            if(!mirrorOrbits.empty()&&mirrorOrbits[(i+1)%n]<i)
                continue; // only one deterministic representative per orbit
            ++out.candidates;
            auto candidate=best;
            if(!diagonal_detail::pairedFlip(candidate,i,mirrorOrbits)) {
                ++out.orientationRejected;continue;
            }
            bool valid=true;
            for(std::size_t f=0;f<candidate.faces.size();++f) {
                const auto& tri=candidate.faces[f];
                const auto a=candidate.positions[tri[0]];
                const auto b=candidate.positions[tri[1]];
                const auto c=candidate.positions[tri[2]];
                if(feasibility_detail::triangleQuality(a,b,c)+1e-12<
                   limits.minimumQuality) {
                    ++out.qualityRejected;valid=false;break;
                }
                const auto& old=best.faces[f];
                const auto previous=cross(
                    best.positions[old[1]]-best.positions[old[0]],
                    best.positions[old[2]]-best.positions[old[0]]);
                const auto proposed=cross(b-a,c-a);
                if(dot(previous,proposed)<=0) {
                    ++out.orientationRejected;valid=false;break;
                }
            }
            if(!valid)continue;
            const auto candidateMetric=feasibility_detail::assess(
                candidate,incomingNormals);
            if(!feasibility_detail::better(candidateMetric,roundMetric)) {
                ++out.scoreRejected;continue;
            }
            candidate.minimumQuality=1.;
            try {
                if(!validateCap(candidate)) {
                    ++out.contactRejected;continue;
                }
            }catch(const std::length_error&) {
                out.budgetExhausted=true;
                stopped=true;break;
            }catch(const std::logic_error&) {
                ++out.contactRejected;continue;
            }
            ++out.admissible;
            roundMetric=candidateMetric;
            roundChoice={i,mirrorOrbits.empty()?i:
                mirrorOrbits[(i+1)%n]};
            roundPatch=std::move(candidate);
        }
        if(!feasibility_detail::better(roundMetric,metrics))break;
        best=std::move(roundPatch);
        metrics=roundMetric;
        ++out.acceptedRounds;
        out.acceptedQuadOrbits.push_back(roundChoice);
        if(stopped)break;
    }
    out.bestWorstSeamRadians=metrics.seam;
    out.bestWorstCapRimRadians=metrics.capRim;
    out.bestMeanScore=metrics.mean;
    out.bestMinimumQuality=metrics.minimumQuality;
    out.improved=out.acceptedRounds>0;
    // Optional output is local counterfactual geometry only. Every accepted
    // candidate has passed the caller's existing cap-local validator;
    // original and whole-survivor mesh remain untouched and uncertified.
    if(bestCandidate)*bestCandidate=std::move(best);
    return out;
}
} // namespace mesher
