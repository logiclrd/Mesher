#pragma once

#include "mesher/repair_cap_continuity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace mesher {

// M19A read-only COUNTERFACTUAL search. Local admissibility is NOT an
// entire survivor+cap manifold certificate: its callback checks the same
// cap self-contact budget as the ordinary spatial-front constructor.
// A finite candidate set cannot establish an absolute feasibility bound.
struct SpatialCapFeasibility {
    double baselineWorstSeamRadians{};
    double baselineWorstCapRimRadians{};
    double bestWorstSeamRadians{};
    double bestWorstCapRimRadians{};
    double baselineMeanScore{},bestMeanScore{};
    double baselineMinimumQuality{},bestMinimumQuality{};
    std::size_t proposed{},normalCandidates{},locallyAdmissible{},
                acceptedRounds{},qualityRejected{},orientationRejected{},
                scoreRejected{},contactRejected{};
    bool contactBudgetExhausted{};
    bool improved{};
};

struct SpatialCapFeasibilityLimits {
    std::size_t maxRounds=4,maxProposals=96;
    double minimumQuality=.1;
    double maximumDisplacementInEdgeScales=1.5;
    // Only first-ring indexed vertices may move, optionally as mirrored
    // orbits. The fixed original rim and the central hub NEVER move.
};

namespace feasibility_detail {
struct Assessment {
    double seam{},capRim{},mean{},minimumQuality{1.};
};
inline double triangleQuality(Vec3 a,Vec3 b,Vec3 c) {
    const auto squared=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return squared>0?2*std::sqrt(3.)*
        length(cross(b-a,c-a))/squared:0.;
}
inline Assessment assess(const SpatialCapPatch& patch,
                         const std::vector<Vec3>& incomingNormals) {
    const auto profile=measureSpatialCapRimProfile(patch,incomingNormals);
    Assessment out;
    for(const auto& e:profile.edges) {
        if(!e.hasCapNormal||!e.hasSurvivorNormal)
            throw std::invalid_argument("R06-M19A missing indexed survivor normal");
        const auto fair=spatialCapLocalFairness(patch,e.capFaceIndex);
        out.seam=std::max(out.seam,e.absoluteDihedralRadians);
        out.capRim=std::max(out.capRim,fair);
        out.mean+=e.absoluteDihedralRadians*e.absoluteDihedralRadians+
                  .25*fair*fair;
    }
    out.mean/=profile.edges.size();
    for(const auto& tri:patch.faces)
        out.minimumQuality=std::min(out.minimumQuality,triangleQuality(
            patch.positions[tri[0]],patch.positions[tri[1]],
            patch.positions[tri[2]]));
    return out;
}
inline bool better(const Assessment& a,const Assessment& b) {
    // Lexicographically favor reducing the ACTUAL WORST discontinuity,
    // not averaging the bad crease away. The seam-specific maximum is
    // the tie-breaker, then the cap-rim maximum and finally mean score.
    const auto worstA=std::max(a.seam,a.capRim);
    const auto worstB=std::max(b.seam,b.capRim);
    if(a.seam>b.seam+1e-8||a.capRim>b.capRim+1e-8)
        return false; // never trade an increased worst seam for less fairing
    if(worstA+1e-5<worstB)return true;
    if(worstB+1e-5<worstA)return false;
    if(a.seam+1e-5<b.seam)return true;
    if(b.seam+1e-5<a.seam)return false;
    return a.mean+1e-5<b.mean;
}
} // namespace feasibility_detail

template<class ValidateCap>
SpatialCapFeasibility probeSpatialCapFirstRingFeasibility(
    const SpatialCapPatch& original,
    const std::vector<Vec3>& incomingNormals,
    const std::vector<SurvivorDirectionalCurvature>& incomingCurvature,
    const std::vector<std::size_t>& mirroredOrbits,
    double medianRimEdge,
    ValidateCap&& validateCap,
    const SpatialCapFeasibilityLimits& limits={}) {
    const auto n=original.boundaryCount;
    if(n<3||original.positions.size()<2*n||
       incomingNormals.size()!=n||incomingCurvature.size()!=n||
       (!mirroredOrbits.empty()&&mirroredOrbits.size()!=original.positions.size())||
       !limits.maxRounds||!limits.maxProposals||
       !(limits.minimumQuality>0&&limits.minimumQuality<1)||
       !(limits.maximumDisplacementInEdgeScales>0)||
       !(medianRimEdge>0&&std::isfinite(medianRimEdge)))
        throw std::invalid_argument("R06-M19A malformed finite probe");
    using namespace feasibility_detail;
    SpatialCapFeasibility report;
    const auto start=assess(original,incomingNormals);
    report.baselineWorstSeamRadians=start.seam;
    report.baselineWorstCapRimRadians=start.capRim;
    report.baselineMeanScore=start.mean;
    report.baselineMinimumQuality=start.minimumQuality;
    auto best=original;
    auto metrics=start;
    auto reflect=[](Vec3 p){return Vec3{-p.x,p.y,p.z};};
    for(std::size_t round=0;round<limits.maxRounds;++round) {
        const auto profile=measureSpatialCapRimProfile(best,incomingNormals);
        std::vector<std::size_t> order(n);
        std::iota(order.begin(),order.end(),0);
        std::stable_sort(order.begin(),order.end(),
            [&](std::size_t a,std::size_t b){
                const auto& x=profile.edges[a],&y=profile.edges[b];
                return x.absoluteDihedralRadians+
                    .25*spatialCapLocalFairness(best,x.capFaceIndex)>
                    y.absoluteDihedralRadians+
                    .25*spatialCapLocalFairness(best,y.capFaceIndex);
            });
        auto roundPatch=best;
        auto roundMetric=metrics;
        bool exhausted=false;
        for(const auto ordinal:order) {
            const auto& edge=profile.edges[ordinal];
            const auto vertex=static_cast<std::size_t>(edge.capOppositeVertex);
            if(vertex<n||vertex>=2*n)continue;
            const auto mate=mirroredOrbits.empty()?
                vertex:mirroredOrbits.at(vertex);
            if(mate<vertex||mate>=2*n)continue;
            const auto a=best.positions[edge.rimStart];
            const auto b=best.positions[edge.rimEnd];
            const auto c=best.positions[vertex];
            const auto normalRaw=incomingNormals[edge.rimEnd];
            const auto normalLength=length(normalRaw);
            if(!(normalLength>1e-12))continue;
            const auto normal=normalRaw/normalLength;
            const auto t=(b-a)/edge.edgeLength;
            const auto crossTangent=cross(normal,t);
            const auto originalFoot=dot(c-a,t);
            const auto foot=std::clamp(originalFoot,
                edge.edgeLength*.10,edge.edgeLength*.90);
            const auto crossDistance=dot(c-a,crossTangent);
            const auto& tensor=incomingCurvature[edge.rimEnd];
            const bool credible=tensor.hasEstimate&&
                tensor.confidence==SurvivorCurvatureConfidence::Reliable&&
                tensor.confidenceScore>=.5;
            // First-order tangent continuation is ALWAYS available from
            // the original face normal. Add a bounded second-order height
            // only if the survivor curvature estimate is trustworthy.
            const auto k=[&](double x){
                return std::clamp(x,-.7/medianRimEdge,.7/medianRimEdge);
            };
            const auto height=credible?
                .5*k(tensor.seam)*foot*(foot-edge.edgeLength)+
                k(tensor.mixed)*(foot-.5*edge.edgeLength)*crossDistance+
                .5*k(tensor.transverse)*crossDistance*crossDistance:0.;
            const auto tangentTarget=a+t*foot+
                crossTangent*crossDistance+normal*height;
            const auto planeTarget=c-normal*dot(c-a,normal);
            for(const auto curvatureTarget:{false,true})
            for(const auto fraction:{.25,.5,.75,1.}) {
                if(report.proposed>=limits.maxProposals) {
                    exhausted=true;break;
                }
                if(curvatureTarget&&!credible)continue;
                ++report.proposed;
                const auto target=curvatureTarget?
                    tangentTarget:planeTarget;
                const auto moved=c+(target-c)*fraction;
                if(length(moved-c)>
                    limits.maximumDisplacementInEdgeScales*medianRimEdge)
                    continue;
                ++report.normalCandidates;
                auto candidate=best;
                candidate.positions[vertex]=moved;
                if(mate==vertex)candidate.positions[vertex].x=0.;
                else candidate.positions[mate]=reflect(moved);
                bool legal=true;
                for(const auto& tri:best.faces) {
                    const auto pa=candidate.positions[tri[0]];
                    const auto pb=candidate.positions[tri[1]];
                    const auto pc=candidate.positions[tri[2]];
                    if(triangleQuality(pa,pb,pc)+1e-12<
                       limits.minimumQuality) {
                        ++report.qualityRejected;legal=false;break;
                    }
                    const auto oldNormal=cross(
                        best.positions[tri[1]]-best.positions[tri[0]],
                        best.positions[tri[2]]-best.positions[tri[0]]);
                    const auto newNormal=cross(pb-pa,pc-pa);
                    if(dot(oldNormal,newNormal)<=0) {
                        ++report.orientationRejected;legal=false;break;
                    }
                }
                if(!legal)continue;
                const auto assessment=assess(candidate,incomingNormals);
                if(!better(assessment,roundMetric)) {
                    ++report.scoreRejected;continue;
                }
                candidate.minimumQuality=1.;
                try {
                    if(!validateCap(candidate)) {
                        ++report.contactRejected;continue;
                    }
                }catch(const std::length_error&) {
                    report.contactBudgetExhausted=true;
                    exhausted=true;break;
                }catch(const std::logic_error&) {
                    ++report.contactRejected;continue;
                }
                ++report.locallyAdmissible;
                roundMetric=assessment;
                roundPatch=std::move(candidate);
            }
            if(exhausted)break;
        }
        if(!better(roundMetric,metrics))break;
        best=std::move(roundPatch);
        metrics=roundMetric;
        ++report.acceptedRounds;
        if(exhausted)break;
    }
    report.bestWorstSeamRadians=metrics.seam;
    report.bestWorstCapRimRadians=metrics.capRim;
    report.bestMeanScore=metrics.mean;
    report.bestMinimumQuality=metrics.minimumQuality;
    report.improved=report.acceptedRounds>0;
    return report;
}
} // namespace mesher
