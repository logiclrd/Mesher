#pragma once
#include "mesher/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {

// M17A, pure/read-only. Scores candidate edge split TEMPLATES but makes NO
// topology edits. A later stage-editor milestone must re-preflight both R03
// survivor sides, reflection partners, geometric collisions and all IDs.
enum class RepairSplitTemplateKind {Midpoint,OffCenter,Hybrid};
struct RepairSplitTemplateSide {
    Vec3 opposite{};
    double qualityFloor=0.1;
};
struct RepairSplitTemplatePolicy {
    double targetLength=1.0;
    double coarseScaleRatio=2.0; // provisional policy, not R06 admission
    double minSegmentFraction=0.12;
    double minimumGainFraction=1e-5;
    std::size_t remainingOperations=2;
    std::size_t maximumDepth=2;
};
struct RepairSplitTemplateInput {
    std::uint64_t firstId{},secondId{};
    Vec3 first{},second{};
    std::vector<RepairSplitTemplateSide> sides;
    RepairSplitTemplatePolicy policy{};
};
struct RepairSplitTemplateCandidate {
    RepairSplitTemplateKind kind{RepairSplitTemplateKind::Midpoint};
    // Canonical endpoints: firstId < secondId. With a reversed caller
    // ordering, the physical point is identical and t changes to 1-t.
    double firstT=0.5;
    double secondT=0.; // only used by Hybrid; the first is ALWAYS .5
    std::size_t operations=1;
    std::size_t resultingFacesPerSide=2;
    double worstBefore=1.;
    double worstAfter=1.;
    // Positive ONLY when the second split genuinely improves the affected
    // first-level child. This is not a claim that global worst q improved.
    double correctedChildGain=0.;
    double shortestBaseFraction=1.;
    double longestRemainingBaseFraction=1.;
    bool admissible=false;
    std::string rejection;
};
struct RepairSplitTemplateDecision {
    bool coarse=false;
    double edgeToTargetRatio=0.;
    std::vector<RepairSplitTemplateCandidate> candidates;
    std::size_t selected=std::numeric_limits<std::size_t>::max();
    std::string reason;
    bool hasSelection() const noexcept {
        return selected<candidates.size();
    }
};

inline RepairSplitTemplateDecision evaluateRepairSplitTemplates(
    RepairSplitTemplateInput input) {
    using Kind=RepairSplitTemplateKind;
    const auto finite=[](Vec3 p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    };
    const auto& policy=input.policy;
    if(!input.firstId||!input.secondId||
       input.firstId==input.secondId||
       !finite(input.first)||!finite(input.second)||
       input.sides.empty()||input.sides.size()>2||
       !std::isfinite(policy.targetLength)||policy.targetLength<=0||
       !std::isfinite(policy.coarseScaleRatio)||policy.coarseScaleRatio<=1||
       !std::isfinite(policy.minSegmentFraction)||
       policy.minSegmentFraction<=0||policy.minSegmentFraction>=.25||
       !std::isfinite(policy.minimumGainFraction)||
       policy.minimumGainFraction<0||policy.minimumGainFraction>.25)
        throw std::invalid_argument("R06-M17A invalid finite split-template input");
    for(const auto& side:input.sides)
        if(!finite(side.opposite)||!std::isfinite(side.qualityFloor)||
           side.qualityFloor<=0||side.qualityFloor>=1)
            throw std::invalid_argument("R06-M17A invalid incident triangle or floor");
    if(input.firstId>input.secondId) {
        std::swap(input.firstId,input.secondId);
        std::swap(input.first,input.second);
    }
    const Vec3 edge=input.second-input.first;
    const double lengthSquared=dot(edge,edge);
    if(!std::isfinite(lengthSquared)||lengthSquared<=0)
        throw std::invalid_argument("R06-M17A degenerate long edge");
    const auto baseLength=std::sqrt(lengthSquared);
    const auto quality=[](Vec3 a,Vec3 b,Vec3 c) {
        const double sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
        return sum>0?2*std::sqrt(3.)*
            std::sqrt(dot(cross(b-a,c-a),cross(b-a,c-a)))/sum:0.;
    };
    const auto onEdge=[&](double t) {
        return input.first+edge*t;
    };
    RepairSplitTemplateDecision output;
    output.edgeToTargetRatio=baseLength/policy.targetLength;
    output.coarse=output.edgeToTargetRatio>policy.coarseScaleRatio;
    double worstBefore=1.;
    bool underQuality=false;
    for(const auto& side:input.sides) {
        const double q=quality(input.first,input.second,side.opposite);
        if(!std::isfinite(q))
            throw std::invalid_argument("R06-M17A invalid triangle quality");
        worstBefore=std::min(worstBefore,q);
        underQuality|=q<side.qualityFloor;
    }
    const auto addCandidate=[&](Kind kind,double t,double second) {
        RepairSplitTemplateCandidate candidate;
        candidate.kind=kind;
        candidate.firstT=t;
        candidate.secondT=second;
        candidate.operations=kind==Kind::Hybrid?2:1;
        candidate.resultingFacesPerSide=candidate.operations+1;
        candidate.worstBefore=worstBefore;
        output.candidates.push_back(candidate);
        auto& result=output.candidates.back();
        if(candidate.operations>policy.remainingOperations||
           candidate.operations>policy.maximumDepth) {
            result.rejection="finite operation/depth budget";
            return;
        }
        std::vector<double> knots{0.,1.,t};
        if(kind==Kind::Hybrid)knots.push_back(second);
        std::sort(knots.begin(),knots.end());
        for(std::size_t i=1;i<knots.size();++i) {
            const auto fraction=knots[i]-knots[i-1];
            result.shortestBaseFraction=std::min(
                result.shortestBaseFraction,fraction);
            result.longestRemainingBaseFraction=std::min(1.,
                std::max(result.longestRemainingBaseFraction==1.?
                    0.:result.longestRemainingBaseFraction,fraction));
            if(!std::isfinite(fraction)||
               fraction+1e-12<policy.minSegmentFraction) {
                result.rejection="near-endpoint/repeated off-center cut";
                return;
            }
        }
        // Check the SECOND hybrid point against its own midpoint child
        // interval, not just the original edge; stops endpoint chasing.
        if(kind==Kind::Hybrid) {
            const double local=second<.5?second/.5:(second-.5)/.5;
            if(local+1e-12<policy.minSegmentFraction||
               local>1.-policy.minSegmentFraction+1e-12) {
                result.rejection="corrective cut too close to child endpoint";
                return;
            }
        }
        double after=1.;
        for(const auto& side:input.sides) {
            const double oldQuality=quality(
                input.first,input.second,side.opposite);
            double sideWorst=1.;
            for(std::size_t i=1;i<knots.size();++i)
                sideWorst=std::min(sideWorst,quality(
                    onEdge(knots[i-1]),onEdge(knots[i]),side.opposite));
            after=std::min(after,sideWorst);
            if(kind==Kind::Hybrid) {
                const double lo=second<.5?0.:.5;
                const double hi=second<.5?.5:1.;
                const double oldChild=quality(
                    onEdge(lo),onEdge(hi),side.opposite);
                const double fixedWorst=std::min(
                    quality(onEdge(lo),onEdge(second),side.opposite),
                    quality(onEdge(second),onEdge(hi),side.opposite));
                if(oldChild>0)
                    result.correctedChildGain=std::max(
                        result.correctedChildGain,
                        fixedWorst/oldChild-1.);
            }
            // A healthy survivor must meet its hard floor. An already
            // deficient face in coarse geometry must at least IMPROVE,
            // never consume a budget while propagating a worse needle.
            const double required=oldQuality>=side.qualityFloor?
                side.qualityFloor:
                output.coarse?
                    oldQuality*(1.+policy.minimumGainFraction):
                    side.qualityFloor;
            if(!std::isfinite(sideWorst)||sideWorst+1e-12<required) {
                result.worstAfter=after;
                result.rejection="child quality regresses or needs shape repair";
                return;
            }
        }
        result.worstAfter=after;
        result.admissible=true;
    };
    addCandidate(Kind::Midpoint,.5,0.);

    // Opposite vertices can prefer DIFFERENT perpendicular feet. Generate
    // a finite unique union; test each point against EVERY incident side.
    std::vector<double> singlePoints;
    std::vector<double> correctionPoints;
    for(const auto& side:input.sides) {
        const double foot=dot(side.opposite-input.first,edge)/lengthSquared;
        if(!std::isfinite(foot)||foot<=0||foot>=1)continue;
        singlePoints.push_back(foot);
        singlePoints.push_back((foot+.5)*.5);
        correctionPoints.push_back(foot);
        // The corrective foot can move halfway toward its child midpoint
        // when the raw foot is too close to an end.
        const double childCenter=foot<.5?.25:.75;
        correctionPoints.push_back((foot+childCenter)*.5);
    }
    const auto unique=[](std::vector<double>& ts) {
        std::sort(ts.begin(),ts.end());
        ts.erase(std::unique(ts.begin(),ts.end(),
            [](double a,double b){return std::abs(a-b)<1e-10;}),ts.end());
    };
    unique(singlePoints);
    unique(correctionPoints);
    for(const auto t:singlePoints)
        if(std::abs(t-.5)>1e-10)
            addCandidate(Kind::OffCenter,t,0.);
    for(const auto t:correctionPoints)
        if(std::abs(t-.5)>1e-10)
            addCandidate(Kind::Hybrid,.5,t);

    // Selection is intentionally conservative: during coarse size
    // reduction, the balanced midpoint is preferred unless a bounded
    // hybrid provides a MATERIAL quality gain. An off-center fallback
    // is allowed only if midpoint cannot preserve existing quality.
    const auto better=[&](std::size_t i,std::size_t j) {
        const auto& a=output.candidates[i];
        const auto& b=output.candidates[j];
        if(a.worstAfter!=b.worstAfter)
            return a.worstAfter>b.worstAfter;
        if(a.operations!=b.operations)
            return a.operations<b.operations;
        if(a.shortestBaseFraction!=b.shortestBaseFraction)
            return a.shortestBaseFraction>b.shortestBaseFraction;
        return a.firstT<b.firstT;
    };
    const auto bestOf=[&](Kind kind) {
        std::size_t best=output.candidates.size();
        for(std::size_t i=0;i<output.candidates.size();++i)
            if(output.candidates[i].admissible&&
               output.candidates[i].kind==kind&&
               (best==output.candidates.size()||better(i,best)))
                best=i;
        return best;
    };
    const auto midpoint=bestOf(Kind::Midpoint);
    const auto offCenter=bestOf(Kind::OffCenter);
    const auto hybrid=bestOf(Kind::Hybrid);
    const std::size_t missing=output.candidates.size();
    if(underQuality&&!output.coarse) {
        output.reason="shape repair required at target scale; splitting is not a substitute";
        return output;
    }
    if(!underQuality&&output.edgeToTargetRatio<=1.) {
        output.reason="healthy edge already meets local target density";
        return output;
    }
    if(output.coarse) {
        output.selected=midpoint;
        if(midpoint!=missing&&hybrid!=missing&&
           (output.candidates[hybrid].worstAfter>
               output.candidates[midpoint].worstAfter*
                   (1.05+policy.minimumGainFraction)||
            // A bounded hybrid is valuable for very coarse geometry if
            // its SECOND split improves the selected half substantially,
            // even when the unchanged OTHER half remains the global
            // minimum. Do not apply this density/trajectory preference to
            // an already under-quality parent or a smaller-scale sliver.
            (!underQuality&&output.edgeToTargetRatio>=3.0&&
             output.candidates[hybrid].correctedChildGain>=.10&&
             output.candidates[hybrid].worstAfter>=
                 .90*output.candidates[midpoint].worstAfter)))
            output.selected=hybrid;
        if(output.selected==missing)output.selected=offCenter;
    }else {
        output.selected=midpoint;
        if(offCenter!=missing&&(output.selected==missing||
           (output.candidates[offCenter].worstAfter>
            output.candidates[output.selected].worstAfter*
                (1.01+policy.minimumGainFraction))))
            output.selected=offCenter;
        if(hybrid!=missing&&(output.selected==missing||
           output.candidates[hybrid].worstAfter>
               output.candidates[output.selected].worstAfter*
                   (1.05+policy.minimumGainFraction)))
            output.selected=hybrid;
    }
    output.reason=output.hasSelection()?
        "read-only template proposal; real incident/mirror topology must be re-preflighted":
        "no quality-safe bounded split; hand off to shape repair or refuse";
    return output;
}
} // namespace mesher
