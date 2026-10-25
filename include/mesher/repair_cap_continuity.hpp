#pragma once

#include "mesher/repair_surface_profile.hpp"
#include "mesher/repair_survivor_curvature.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {

// M18C: pure, read-only first-order and directional second-order comparisons.
// Numbers are diagnostics and candidate-ranking *evidence*, never a new
// acceptance/refusal gate. The original q floor remains entirely separate.
struct SpatialCapContinuityEdge {
    SpatialRimProfileEdge rim;
    SurvivorDirectionalCurvature survivor,cap;
    double boundaryNormalJumpRadians{};
    double capInteriorFairnessRadians{};
    double deltaSeamScaled{},deltaMixedScaled{},deltaTransverseScaled{};
    double directionalMismatchScaled{};
    // True ONLY for two independently reliable curvature estimates with
    // confidence >= threshold and moderate (<60deg) normal disagreement.
    bool directionalQualified{};
    bool directionalMeasured{};
    // Boundary-only number is useful even when the curvature fit is poor.
    // Do not compare two totals with different directional-qualified masks.
    double boundaryAndFairnessScore{};
    double qualifiedTotalScore{};
};
struct SpatialCapContinuity {
    std::vector<SpatialCapContinuityEdge> edges;
    std::size_t measuredBoundaryEdges{};
    std::size_t directionalMeasuredEdges{};
    std::size_t directionalQualifiedEdges{};
    std::size_t lowConfidenceEdges{};
    double maximumBoundaryJumpRadians{},meanBoundaryJumpRadians{};
    double meanBoundaryAndFairnessScore{};
    // Mean only over qualified edges: not comparable when mask differs.
    double meanQualifiedDirectionalMismatch{};
};
struct SpatialCapContinuityLimits {
    SurvivorCurvatureLimits curvature{};
    std::size_t maxPatchFaces=4096;
    double minimumDirectionalConfidence=0.5;
    double maximumNormalJumpForTensorComparison=1.0471975511965976;
    // Pure diagnostic composite: split out component values for independent
    // inspection; weights do not certify a geometry quality improvement.
    double fairnessWeight=0.25;
};

// Collect a deterministic, bounded cap-only triangle neighborhood, by
// actual LOCAL indexed edges. No geometric proximity lookup is allowed.
// Returns M18B-compatible centroid/normal observations in a separate
// neighborhood on the CAP side; the survivor reference is passed separately.
inline SurvivorCurvatureNeighborhood collectCapCurvatureNeighborhood(
    const SpatialCapPatch& patch,std::size_t seed,
    const SpatialCapContinuityLimits& limits={}) {
    SurvivorCurvatureNeighborhood out;
    if(seed>=patch.faces.size()) {
        out.missingSeed=true;
        return out;
    }
    if(patch.faces.size()>limits.maxPatchFaces)
        throw std::length_error("R06-M18C bounded cap face inventory exceeded");
    if(!limits.curvature.maxFaces) {
        out.truncated=true;
        return out;
    }
    using Edge=std::pair<std::uint32_t,std::uint32_t>;
    std::map<Edge,std::vector<std::size_t>> incidence;
    for(std::size_t f=0;f<patch.faces.size();++f) {
        const auto& triangle=patch.faces[f];
        for(const auto v:triangle)
            if(v>=patch.positions.size())
                throw std::logic_error("R06-M18C cap face has missing indexed vertex");
        for(unsigned j=0;j<3;++j) {
            const auto a=triangle[j],b=triangle[(j+1)%3];
            if(a==b)throw std::logic_error("R06-M18C cap edge degeneracy");
            incidence[{std::min(a,b),std::max(a,b)}].push_back(f);
        }
    }
    std::set<std::size_t> visited{seed};
    std::deque<std::pair<std::size_t,unsigned>> queue{{seed,0}};
    while(!queue.empty()) {
        if(out.faces.size()>=limits.curvature.maxFaces) {
            out.truncated=true;
            break;
        }
        const auto [index,depth]=queue.front();
        queue.pop_front();
        const auto& triangle=patch.faces[index];
        const auto a=patch.positions[triangle[0]];
        const auto b=patch.positions[triangle[1]];
        const auto c=patch.positions[triangle[2]];
        out.faces.push_back({static_cast<FaceId>(index+1),
                             (a+b+c)/3.,cross(b-a,c-a)});
        if(depth>=limits.curvature.maxFaceRings)continue;
        std::set<std::size_t> neighbors;
        for(unsigned j=0;j<3;++j) {
            const auto x=triangle[j],y=triangle[(j+1)%3];
            for(const auto other:incidence.at({std::min(x,y),std::max(x,y)}))
                if(other!=index)neighbors.insert(other);
        }
        for(const auto next:neighbors)
            if(visited.insert(next).second)
                queue.emplace_back(next,depth+1);
    }
    return out;
}

// Maximum instantaneous face-normal change on a CAP-to-CAP edge adjacent
// to the first-ring cap triangle. Read-only one-ring fairness indicator.
inline double spatialCapLocalFairness(
    const SpatialCapPatch& patch,std::size_t firstFace) {
    if(firstFace>=patch.faces.size())
        throw std::out_of_range("R06-M18C first cap face missing");
    const auto tri=patch.faces[firstFace];
    const auto a=patch.positions[tri[0]];
    const auto b=patch.positions[tri[1]];
    const auto c=patch.positions[tri[2]];
    const auto n=cross(b-a,c-a);
    const auto s=length(n);
    if(!(s>1.e-12))return 0;
    double largest=0;
    for(std::size_t f=0;f<patch.faces.size();++f) {
        if(f==firstFace)continue;
        const auto other=patch.faces[f];
        unsigned shared=0;
        for(const auto x:tri)
            for(const auto y:other)
                if(x==y)++shared;
        if(shared!=2)continue;
        const auto p=patch.positions[other[0]];
        const auto q=patch.positions[other[1]];
        const auto r=patch.positions[other[2]];
        const auto m=cross(q-p,r-p);
        const auto lengthM=length(m);
        if(!(lengthM>1.e-12))continue;
        largest=std::max(largest,std::acos(
            std::clamp(dot(n,m)/(s*lengthM),-1.,1.)));
    }
    return largest;
}

// Both curvature estimates refer to the *same outgoing seam tangent*.
// Each uses u=normal x tangent on its OWN side; comparison is conceptual
// normal-frame transport about the edge tangent. At large normal jumps it
// is NOT trusted, even if both fitting routines returned numeric tensors.
// Guidance and survivor curvature arrays are indexed to INCOMING cap edges
// (the reversed R03 contour convention); outgoing edge i->i+1 uses i+1.
inline SpatialCapContinuity measureSpatialCapContinuity(
    const SpatialCapPatch& patch,
    const std::vector<Vec3>& incomingSurvivorNormals,
    const std::vector<SurvivorDirectionalCurvature>& incomingSurvivorCurvature,
    const SpatialCapContinuityLimits& limits={}) {
    const auto n=patch.boundaryCount;
    if(n<3||incomingSurvivorCurvature.size()!=n||
       !std::isfinite(limits.minimumDirectionalConfidence)||
       !(limits.minimumDirectionalConfidence>=0&&
         limits.minimumDirectionalConfidence<=1)||
       !std::isfinite(limits.maximumNormalJumpForTensorComparison)||
       !(limits.maximumNormalJumpForTensorComparison>0&&
         limits.maximumNormalJumpForTensorComparison<=3.141592653589793)||
       !std::isfinite(limits.fairnessWeight)||limits.fairnessWeight<0)
        throw std::invalid_argument("R06-M18C invalid curvature-continuity inputs");
    if(patch.faces.size()>limits.maxPatchFaces)
        throw std::length_error("R06-M18C bounded cap face inventory exceeded");
    const auto profile=measureSpatialCapRimProfile(patch,incomingSurvivorNormals);
    SpatialCapContinuity result;
    result.edges.reserve(n);
    for(const auto& rim:profile.edges) {
        SpatialCapContinuityEdge sample;
        sample.rim=rim;
        const auto j=rim.rimEnd;
        sample.survivor=incomingSurvivorCurvature[j];
        sample.boundaryNormalJumpRadians=rim.absoluteDihedralRadians;
        sample.capInteriorFairnessRadians=
            spatialCapLocalFairness(patch,rim.capFaceIndex);
        sample.boundaryAndFairnessScore=
            sample.boundaryNormalJumpRadians+
            limits.fairnessWeight*sample.capInteriorFairnessRadians;
        sample.qualifiedTotalScore=sample.boundaryAndFairnessScore;
        if(rim.hasSurvivorNormal&&rim.hasCapNormal) {
            ++result.measuredBoundaryEdges;
            result.maximumBoundaryJumpRadians=std::max(
                result.maximumBoundaryJumpRadians,
                sample.boundaryNormalJumpRadians);
            result.meanBoundaryJumpRadians+=sample.boundaryNormalJumpRadians;
            result.meanBoundaryAndFairnessScore+=sample.boundaryAndFairnessScore;
        }
        const auto capNeighborhood=collectCapCurvatureNeighborhood(
            patch,rim.capFaceIndex,limits);
        sample.cap=estimateSurvivorDirectionalCurvature(
            patch.positions[rim.rimStart],patch.positions[rim.rimEnd],
            capNeighborhood,limits.curvature);
        sample.directionalMeasured=
            sample.survivor.hasEstimate&&sample.cap.hasEstimate;
        if(sample.directionalMeasured) {
            ++result.directionalMeasuredEdges;
            const auto h=rim.edgeLength;
            sample.deltaSeamScaled=h*(sample.cap.seam-sample.survivor.seam);
            sample.deltaMixedScaled=h*(sample.cap.mixed-sample.survivor.mixed);
            sample.deltaTransverseScaled=
                h*(sample.cap.transverse-sample.survivor.transverse);
            sample.directionalMismatchScaled=std::sqrt(
                sample.deltaSeamScaled*sample.deltaSeamScaled+
                2*sample.deltaMixedScaled*sample.deltaMixedScaled+
                sample.deltaTransverseScaled*sample.deltaTransverseScaled);
        }
        const auto eligible=[&](const SurvivorDirectionalCurvature& v) {
            return v.hasEstimate&&
                v.confidence==SurvivorCurvatureConfidence::Reliable&&
                v.confidenceScore>=limits.minimumDirectionalConfidence;
        };
        sample.directionalQualified=sample.directionalMeasured&&
            eligible(sample.survivor)&&eligible(sample.cap)&&
            rim.hasSurvivorNormal&&rim.hasCapNormal&&
            rim.absoluteDihedralRadians<=
                limits.maximumNormalJumpForTensorComparison;
        if(sample.directionalQualified) {
            ++result.directionalQualifiedEdges;
            result.meanQualifiedDirectionalMismatch+=
                sample.directionalMismatchScaled;
            sample.qualifiedTotalScore+=sample.directionalMismatchScaled;
        }else ++result.lowConfidenceEdges;
        result.edges.push_back(sample);
    }
    if(result.measuredBoundaryEdges) {
        const auto count=static_cast<double>(result.measuredBoundaryEdges);
        result.meanBoundaryJumpRadians/=count;
        result.meanBoundaryAndFairnessScore/=count;
    }
    if(result.directionalQualifiedEdges)
        result.meanQualifiedDirectionalMismatch/=
            static_cast<double>(result.directionalQualifiedEdges);
    return result;
}
} // namespace mesher
