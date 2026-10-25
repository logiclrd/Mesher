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
    // Normal sweep counts include geometry rejected later by q/embedding.
    // They establish whether BOTH sides of the survivor reference surface
    // were explicitly explored (not merely projected onto one plane).
    std::size_t normalOutwardProposals{},normalInwardProposals{},
                normalOutwardAdmissible{},normalInwardAdmissible{},
                sampledPositivePlaneSide{},sampledNegativePlaneSide{};
    double averageNormalInitialOffsetOverEdge{},normalSweepMaxOverEdge{},
           minSampledNormalDeltaOverEdge{std::numeric_limits<double>::infinity()},
           maxSampledNormalDeltaOverEdge{-std::numeric_limits<double>::infinity()};
    std::size_t proposed{},normalCandidates{},locallyAdmissible{},
                acceptedRounds{},qualityRejected{},orientationRejected{},
                scoreRejected{},contactRejected{};
    bool contactBudgetExhausted{};
    bool improved{};
};

struct SpatialCapFeasibilityLimits {
    std::size_t maxRounds=4,maxProposals=512;
    double minimumQuality=.1;
    double maximumDisplacementInEdgeScales=3.0; // double original cap so expanded normal sweep is reachable
    double normalSweepEdgeScales=1.6; // M19A5: double original ±0.8 median-rim-edge sweep
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
    const SpatialCapFeasibilityLimits& limits={},
    SpatialCapPatch* bestCandidate=nullptr) {
    const auto n=original.boundaryCount;
    if(n<3||original.positions.size()<2*n||
       incomingNormals.size()!=n||incomingCurvature.size()!=n||
       (!mirroredOrbits.empty()&&mirroredOrbits.size()!=original.positions.size())||
       !limits.maxRounds||!limits.maxProposals||
       !(limits.minimumQuality>0&&limits.minimumQuality<1)||
       !(limits.maximumDisplacementInEdgeScales>0)||
       !(limits.normalSweepEdgeScales>0&&
         std::isfinite(limits.normalSweepEdgeScales))||
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
    // The survivor *set* normal is an independent third basis axis:
    // the old M19A interpolations terminated at one plane and could
    // neither cross it nor search along its normal in both directions.
    // In symmetric cases the orbit-invariant direction lives in X=0.
    Vec3 averageNormal{};
    for(const auto source:incomingNormals) {
        const auto magnitude=length(source);
        if(magnitude>1e-12&&std::isfinite(magnitude))
            averageNormal=averageNormal+source/magnitude;
    }
    if(!mirroredOrbits.empty())averageNormal.x=0;
    auto averageLength=length(averageNormal);
    if(!(averageLength>1e-10)) {
        // Degenerate/cancelling survivor normals: use the first reliable
        // directed original face normal, still mirrored if requested.
        for(const auto source:incomingNormals) {
            Vec3 direction=source;
            if(!mirroredOrbits.empty())direction.x=0;
            const auto magnitude=length(direction);
            if(magnitude>1e-10&&std::isfinite(magnitude)){
                averageNormal=direction/magnitude;
                averageLength=1;
                break;
            }
        }
    }
    if(!(averageLength>1e-10))
        throw std::invalid_argument(
            "R06-M19A no finite survivor-set normal for two-sided sweep");
    averageNormal=averageNormal/averageLength;
    report.normalSweepMaxOverEdge=limits.normalSweepEdgeScales;
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
            // The NORMAL coordinate is independent of the in-plane
            // choice. Search BOTH signs about (a) the existing offset,
            // (b) the first-order tangent plane, (c) the high-confidence
            // curvature surface. A projection/interpolation alone can
            // never pass *through* the plane to its other side.
            // Keep signed offsets symmetric, with small and larger
            // excursions, and retain zero-offset plane candidates.
            // Each proposal (even if q-refused) counts toward the finite
            // budget. The deterministic first tranche samples signed
            // moves from the CURRENT vertex for every visited edge.
            const std::array<double,5> normalSteps{
                -1.,-.5,0.,.5,1.};
            for(unsigned baseChoice=0;baseChoice<3;++baseChoice) {
                if(baseChoice==2&&!credible)continue;
                const auto baseTarget=baseChoice==0?c:
                    baseChoice==1?planeTarget:tangentTarget;
                for(const auto fraction:{.5,1.})
                for(const auto offset:normalSteps) {
                if(baseChoice==0&&(fraction!=1.||offset==0.))continue;
                if(report.proposed>=limits.maxProposals) {
                    exhausted=true;break;
                }
                ++report.proposed;
                if(offset<0)++report.normalInwardProposals;
                if(offset>0)++report.normalOutwardProposals;
                const auto target=baseTarget+
                    averageNormal*(offset*limits.normalSweepEdgeScales*
                                   medianRimEdge);
                auto moved=c+(target-c)*fraction;
                if(!mirroredOrbits.empty()&&mate==vertex)
                    moved.x=0;
                if(length(moved-c)>
                    limits.maximumDisplacementInEdgeScales*medianRimEdge)
                    continue;
                const auto sampledDelta=dot(moved-c,averageNormal)/
                    medianRimEdge;
                report.minSampledNormalDeltaOverEdge=std::min(
                    report.minSampledNormalDeltaOverEdge,sampledDelta);
                report.maxSampledNormalDeltaOverEdge=std::max(
                    report.maxSampledNormalDeltaOverEdge,sampledDelta);
                const auto localPlaneSign=dot(moved-planeTarget,normal);
                if(localPlaneSign>1e-12)++report.sampledPositivePlaneSide;
                if(localPlaneSign<-1e-12)++report.sampledNegativePlaneSide;
                ++report.normalCandidates;
                auto candidate=best;
                candidate.positions[vertex]=moved;
                // No mirror plane means NO reflection: M19A previously
                // assigned reflect(moved) to the same vertex here.
                if(!mirroredOrbits.empty()&&mate!=vertex)
                    candidate.positions[mate]=reflect(moved);
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
                if(offset<0)++report.normalInwardAdmissible;
                if(offset>0)++report.normalOutwardAdmissible;
                roundMetric=assessment;
                roundPatch=std::move(candidate);
                }
                if(exhausted)break;
            }
            if(exhausted)break;
        }
        if(!better(roundMetric,metrics))break;
        best=std::move(roundPatch);
        metrics=roundMetric;
        ++report.acceptedRounds;
        if(exhausted)break;
    }
    // Sign indicates which side of the survivor-set tangent orientation
    // the original cap offset occupies. This does not assume either sign
    // is preferred or that the center plane is an actual tangent plane.
    const auto profile=measureSpatialCapRimProfile(original,incomingNormals);
    const auto& worst=*std::max_element(profile.edges.begin(),
        profile.edges.end(),[](const auto& a,const auto& b) {
            return a.absoluteDihedralRadians<b.absoluteDihedralRadians;
        });
    report.averageNormalInitialOffsetOverEdge=dot(
        original.positions[worst.capOppositeVertex]-
            original.positions[worst.rimStart],averageNormal)/
        medianRimEdge;
    if(report.normalCandidates==0){
        report.minSampledNormalDeltaOverEdge=0;
        report.maxSampledNormalDeltaOverEdge=0;
    }
    report.bestWorstSeamRadians=metrics.seam;
    report.bestWorstCapRimRadians=metrics.capRim;
    report.bestMeanScore=metrics.mean;
    report.bestMinimumQuality=metrics.minimumQuality;
    report.improved=report.acceptedRounds>0;
    // Caller opts in explicitly. The ordinary diagnostic still has NO
    // output patch, so it remains strictly read-only.
    if(bestCandidate)*bestCandidate=best;
    return report;
}
} // namespace mesher
