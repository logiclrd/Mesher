#include "repair_closure_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
constexpr double pi=std::numbers::pi_v<double>;
constexpr std::size_t samples=32;
struct RingGeometry {
    Vec3 centroid{},normal{};
    double area{},radius{},isoperimetric{};
};
struct Match {
    RepairContourPair pair;
    double meanSquare{};
};
struct PairOption {
    Match match;
    RepairClosureChoice score;
    bool zipperAllowed{};
};
RepairPlan fail(RepairPlan plan,RepairPlanStatus status,const char* why) {
    plan.status=status;
    plan.reason=why;
    return plan;
}
std::vector<Vec3> resample(const RegionTopologyIndex& index,
                           const RepairContour& contour) {
    std::vector<Vec3> v;
    v.reserve(contour.vertexIds.size());
    for(const auto id:contour.vertexIds)v.push_back(index.vertex(id).position);
    std::vector<double> distances(v.size()+1,0);
    for(std::size_t i=0;i<v.size();++i)
        distances[i+1]=distances[i]+length(v[(i+1)%v.size()]-v[i]);
    std::vector<Vec3> result;
    result.reserve(samples);
    std::size_t edge=0;
    for(std::size_t i=0;i<samples;++i) {
        const double location=contour.perimeter*
                              static_cast<double>(i)/samples;
        while(edge+1<v.size()&&distances[edge+1]<location)++edge;
        const double width=distances[edge+1]-distances[edge];
        const double factor=width>0?(location-distances[edge])/width:0;
        result.push_back(v[edge]*(1-factor)+
                         v[(edge+1)%v.size()]*factor);
    }
    return result;
}
Match correspondence(const std::vector<Vec3>& a,
                     const std::vector<Vec3>& b,
                     double localEdge,std::size_t first,std::size_t second) {
    Match best;
    best.pair.first=first;
    best.pair.second=second;
    best.pair.maxSampleDistance=std::numeric_limits<double>::infinity();
    double minimumSquare=std::numeric_limits<double>::infinity();
    for(unsigned reversed=0;reversed<2;++reversed)
        for(std::size_t shift=0;shift<samples;++shift) {
            double furthest=0,sum=0;
            for(std::size_t i=0;i<samples;++i) {
                const auto j=reversed?(shift+samples-i)%samples:
                                       (shift+i)%samples;
                const auto delta=a[i]-b[j];
                const double squared=dot(delta,delta);
                furthest=std::max(furthest,std::sqrt(squared));
                sum+=squared;
            }
            // Max-distance pairing avoids one stretched region even when
            // the average correspondence looks deceptively good.
            if(furthest<best.pair.maxSampleDistance||
               (furthest==best.pair.maxSampleDistance&&
                sum<minimumSquare)) {
                best.pair.reverseSecond=(reversed!=0);
                best.pair.sampleOffset=shift;
                best.pair.maxSampleDistance=furthest;
                minimumSquare=sum;
            }
        }
    best.meanSquare=minimumSquare/samples;
    best.pair.gapInEdges=localEdge>0?
        best.pair.maxSampleDistance/localEdge:
        std::numeric_limits<double>::infinity();
    return best;
}
bool ringGeometry(const RegionTopologyIndex& index,
                  const RepairContour& contour,
                  const RepairPlanSettings& settings,RingGeometry& result) {
    const auto& ids=contour.vertexIds;
    if(ids.size()<3)return false;
    const auto origin=index.vertex(ids.front()).position;
    Vec3 areaVector{};
    for(std::size_t i=0;i<ids.size();++i) {
        const auto a=index.vertex(ids[i]).position-origin;
        const auto b=index.vertex(ids[(i+1)%ids.size()]).position-origin;
        areaVector=areaVector+cross(a,b);
    }
    const double magnitude=length(areaVector);
    if(!std::isfinite(magnitude)||magnitude<=0)return false;
    result.normal=areaVector/magnitude;
    result.area=magnitude*.5;
    result.radius=std::sqrt(result.area/pi);
    if(!std::isfinite(result.radius)||result.radius<=0)return false;
    result.isoperimetric=
        contour.perimeter*contour.perimeter/(4*pi*result.area);
    if(!std::isfinite(result.isoperimetric)||
       result.isoperimetric>settings.maximumIsoperimetricRatio)return false;

    double total=0;
    Vec3 weighted{};
    for(std::size_t i=0;i<ids.size();++i) {
        const auto a=index.vertex(ids[i]).position;
        const auto b=index.vertex(ids[(i+1)%ids.size()]).position;
        const double signedArea=.5*dot(result.normal,
                                        cross(a-origin,b-origin));
        weighted=weighted+(origin+a+b)*(signedArea/3);
        total+=signedArea;
    }
    if(!std::isfinite(total)||total<=0)return false;
    result.centroid=weighted/total;
    double maximumOffset=0;
    for(const auto id:ids) {
        const auto delta=index.vertex(id).position-result.centroid;
        maximumOffset=std::max(maximumOffset,
                               std::abs(dot(delta,result.normal)));
    }
    return maximumOffset<=settings.maximumPlanarityInRadii*result.radius;
}
bool compatibleFrustum(const RingGeometry& a,const RingGeometry& b,
                       const RepairPlanSettings& settings) {
    if(std::abs(dot(a.normal,b.normal))<
       settings.minimumNormalAlignment)return false;
    const auto delta=b.centroid-a.centroid;
    const double along=dot(delta,a.normal);
    const double sideways=length(delta-a.normal*along);
    // Circular coaxial model is not reliable for large sideways offset.
    return sideways<=.5*std::max(a.radius,b.radius);
}
bool better(double candidate,double incumbent,double relative) {
    return candidate<incumbent-relative*
        std::max(std::abs(candidate),std::abs(incumbent));
}
} // namespace

RepairPlan selectRepairClosures(const RegionTopologyIndex& index,
                               RepairPlan plan,
                               const RepairPlanSettings& settings) {
    const std::size_t n=plan.contours.size();
    if(n==0) {plan.status=RepairPlanStatus::Ready;return plan;}
    if(n>16)return fail(std::move(plan),RepairPlanStatus::BudgetExceeded,
                        "More than 16 closure rings");
    std::vector<RingGeometry> geometry(n);
    std::vector<std::vector<Vec3>> points(n);
    RepairPlanSettings ringBounds=settings;
    if(n==1) {
        // With only one contour there is no annular partner and no
        // frustum choice. A nonplanar but simple disk can be tested by
        // the real ear clip and post-state intersection validator.
        ringBounds.maximumPlanarityInRadii=
            settings.maximumSingleCapPlanarityInRadii;
        ringBounds.maximumIsoperimetricRatio=
            settings.maximumSingleCapIsoperimetricRatio;
    }
    for(std::size_t i=0;i<n;++i) {
        if(!ringGeometry(index,plan.contours[i],ringBounds,geometry[i]))
            return fail(std::move(plan),RepairPlanStatus::AmbiguousClosure,
                        "Contour nonplanar, degenerate or too elongated for fast area estimate");
        points[i]=resample(index,plan.contours[i]);
    }
    std::map<std::pair<std::size_t,std::size_t>,PairOption> pairs;
    bool unsupportedLikelyZip=false;
    for(std::size_t i=0;i<n;++i) {
        for(std::size_t j=i+1;j<n;++j) {
            const auto& a=geometry[i],b=geometry[j];
            const double ratio=std::max(plan.contours[i].perimeter,
                                        plan.contours[j].perimeter)/
                               std::min(plan.contours[i].perimeter,
                                        plan.contours[j].perimeter);
            if(ratio>settings.maximumPerimeterRatio)continue;
            const auto match=correspondence(points[i],points[j],
                std::min(plan.contours[i].meanEdgeLength,
                         plan.contours[j].meanEdgeLength),i,j);
            if(!std::isfinite(match.meanSquare))continue;
            RepairClosureChoice choice;
            choice.first=i;choice.second=j;
            choice.ringAreaFirst=a.area;choice.ringAreaSecond=b.area;
            choice.centroidSeparation=length(b.centroid-a.centroid);
            choice.effectiveRadiusFirst=a.radius;
            choice.effectiveRadiusSecond=b.radius;
            choice.capArea=a.area+b.area;
            choice.zipperArea=pi*(a.radius+b.radius)*std::hypot(
                choice.centroidSeparation,a.radius-b.radius);
            choice.meanSquaredCorrespondenceDistance=match.meanSquare;
            choice.relativeAreaAdvantage=std::abs(choice.capArea-
                choice.zipperArea)/std::max(choice.capArea,choice.zipperArea);
            const bool supported=compatibleFrustum(a,b,settings);
            const bool cheapZipper=choice.zipperArea<choice.capArea*
                (1-settings.minimumRelativeAreaAdvantage);
            if(!supported) {
                // If two very nearby contours are not approximately
                // coaxial, we cannot trust a circular frustum decision.
                if(match.pair.gapInEdges<=settings.maximumGapInEdges)
                    unsupportedLikelyZip=true;
                continue;
            }
            if(cheapZipper&&match.pair.gapInEdges>
               settings.maximumGapInEdges) {
                unsupportedLikelyZip=true;
                continue;
            }
            pairs.emplace(std::pair{i,j},PairOption{
                match,choice,
                match.pair.gapInEdges<=settings.maximumGapInEdges});
        }
    }
    if(unsupportedLikelyZip)
        return fail(std::move(plan),RepairPlanStatus::AmbiguousClosure,
                    "Promising zipper requires unsupported alignment or excessive span");

    // 2^n bounded exact subset DP permits single independent caps OR
    // paired annular zipper connections. No odd-ring special case.
    const std::size_t total=std::size_t{1}<<n,full=total-1;
    const double infinity=std::numeric_limits<double>::infinity();
    std::vector<double> dp(total,infinity);
    std::vector<std::size_t> selected(total,n);
    std::vector<bool> tie(total,false);
    dp[full]=0;
    for(std::size_t mask=full;mask-->0;) {
        std::size_t first=0;
        while(first<n&&(mask&(std::size_t{1}<<first)))++first;
        if(first==n)continue;
        const auto capMask=mask|(std::size_t{1}<<first);
        dp[mask]=geometry[first].area+dp[capMask];
        selected[mask]=n; // cap first ring
        tie[mask]=tie[capMask];
        for(std::size_t second=first+1;second<n;++second) {
            if(mask&(std::size_t{1}<<second))continue;
            const auto candidate=pairs.find({first,second});
            if(candidate==pairs.end()||!candidate->second.zipperAllowed)
                continue;
            const auto nextMask=capMask|(std::size_t{1}<<second);
            const auto cost=candidate->second.score.zipperArea+dp[nextMask];
            if(better(cost,dp[mask],settings.minimumRelativeAreaAdvantage)) {
                dp[mask]=cost;
                selected[mask]=second;
                tie[mask]=tie[nextMask];
            }else if(!better(dp[mask],cost,
                             settings.minimumRelativeAreaAdvantage)) {
                // Costs within the relative margin: do not guess topology.
                if(cost<dp[mask]) {
                    dp[mask]=cost;
                    selected[mask]=second;
                }
                tie[mask]=true;
            }
        }
    }
    if(tie[0])
        return fail(std::move(plan),RepairPlanStatus::AmbiguousClosure,
                    "Cap and zipper alternatives have indistinguishable added areas");
    plan.estimatedAddedArea=dp[0];
    std::size_t mask=0;
    while(mask!=full) {
        std::size_t first=0;
        while(mask&(std::size_t{1}<<first))++first;
        const auto second=selected[mask];
        mask|=std::size_t{1}<<first;
        if(second==n) {
            plan.cappedContours.push_back(first);
            RepairClosureChoice cap;
            cap.first=first;
            cap.second=n; // explicit sentinel: a single capped loop
            cap.kind=ClosureKind::IndependentCaps;
            cap.ringAreaFirst=geometry[first].area;
            cap.effectiveRadiusFirst=geometry[first].radius;
            cap.capArea=geometry[first].area;
            plan.closureChoices.push_back(cap);
            continue;
        }
        const auto& match=pairs.at({first,second});
        plan.pairs.push_back(match.match.pair);
        plan.maximumSeamGapInEdges=std::max(plan.maximumSeamGapInEdges,
                                          match.match.pair.gapInEdges);
        auto zipper=match.score;
        zipper.kind=ClosureKind::Zipper;
        plan.closureChoices.push_back(zipper);
        mask|=std::size_t{1}<<second;
    }
    plan.status=RepairPlanStatus::Ready;
    return plan;
}
} // namespace mesher
