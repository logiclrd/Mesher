#pragma once

#include "mesher/region_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace mesher {

// R06-M18B: diagnostic-only, bounded survivor-side shape-operator estimate.
// All samples are from ORIGINAL, still surviving, indexed faces. No cap
// triangles may train the reference curvature and no mesh is ever mutated.
struct SurvivorCurvatureFaceSample {
    FaceId face{};
    Vec3 centroid{};
    Vec3 normal{}; // arbitrary nonzero magnitude; normalized by estimator
};

struct SurvivorCurvatureNeighborhood {
    std::vector<SurvivorCurvatureFaceSample> faces; // seed face first
    bool truncated{};
    bool missingSeed{};
};

struct SurvivorCurvatureLimits {
    unsigned maxFaceRings=3;
    std::size_t maxFaces=48;
    std::size_t minimumNeighbors=5;
    // Diagnostics/uncertainty signals, NOT global curvature-acceptance gates.
    double creaseAngleRadians=0.55;
    double maximumRmsNormalResidual=0.12;
    double maximumPivotRatio=1.e7;
};

enum class SurvivorCurvatureConfidence {
    Reliable,
    MissingSeed,
    InsufficientSamples,
    RankDeficient,
    CreaseSuspected,
    InconsistentNormals,
    HighResidual,
    Truncated
};

struct SurvivorDirectionalCurvature {
    double seam=0;       // S_tt
    double mixed=0;      // S_tu, twist (sign depends on seam direction)
    double transverse=0; // S_uu, survivor-side continuation
    std::size_t contributingFaces{};
    std::size_t usableNeighbors{};
    std::size_t skippedFaces{};
    double edgeLength{};
    double maximumNormalAngle{};
    double rmsNormalResidual{};
    double pivotRatio{std::numeric_limits<double>::infinity()};
    double confidenceScore{}; // diagnostic only, 0..1
    SurvivorCurvatureConfidence confidence{
        SurvivorCurvatureConfidence::MissingSeed};
    bool hasEstimate{};
};

inline const char* survivorCurvatureConfidenceName(
    SurvivorCurvatureConfidence confidence) {
    switch(confidence) {
    case SurvivorCurvatureConfidence::Reliable:return "reliable";
    case SurvivorCurvatureConfidence::MissingSeed:return "missing_seed";
    case SurvivorCurvatureConfidence::InsufficientSamples:return "insufficient_samples";
    case SurvivorCurvatureConfidence::RankDeficient:return "rank_deficient";
    case SurvivorCurvatureConfidence::CreaseSuspected:return "crease_suspected";
    case SurvivorCurvatureConfidence::InconsistentNormals:return "inconsistent_normals";
    case SurvivorCurvatureConfidence::HighResidual:return "high_residual";
    case SurvivorCurvatureConfidence::Truncated:return "truncated";
    }
    return "unknown";
}

// Exact face-adjacency BFS, bounded by depth and count. In an R03 private
// open cut, deleted faces have NO side in this index, so the search cannot
// accidentally sample geometry from the excised region. Order is deterministic
// by stable FaceId; there are no geometric nearest-neighbor guesses.
inline SurvivorCurvatureNeighborhood collectSurvivorCurvatureNeighborhood(
    const RegionTopologyIndex& survivor,FaceId seed,
    const SurvivorCurvatureLimits& limits={}) {
    SurvivorCurvatureNeighborhood out;
    if(!survivor.maybeFace(seed)) {
        out.missingSeed=true;
        return out;
    }
    if(!limits.maxFaces) {
        out.truncated=true;
        return out;
    }
    std::set<FaceId> visited{seed};
    std::deque<std::pair<FaceId,unsigned>> queue{{seed,0}};
    while(!queue.empty()) {
        if(out.faces.size()>=limits.maxFaces) {
            out.truncated=true;
            break;
        }
        const auto [id,depth]=queue.front();
        queue.pop_front();
        const auto* face=survivor.maybeFace(id).get();
        if(!face)continue; // an explicitly missing source remains missing
        const auto& ids=face->vertices;
        const auto a=survivor.vertex(ids[0]).position;
        const auto b=survivor.vertex(ids[1]).position;
        const auto c=survivor.vertex(ids[2]).position;
        out.faces.push_back({id,(a+b+c)/3.,cross(b-a,c-a)});
        if(depth>=limits.maxFaceRings)continue;
        std::set<FaceId> neighbors;
        for(std::size_t e=0;e<3;++e) {
            const auto v=ids[e],w=ids[(e+1)%3];
            const RegionTopologyIndex::Edge key{
                std::min(v,w),std::max(v,w)};
            const auto sides=survivor.maybeEdgeSides(key);
            if(!sides)continue;
            for(const auto& side:*sides)
                if(side.face!=id&&survivor.maybeFace(side.face))
                    neighbors.insert(side.face);
        }
        for(const auto next:neighbors)
            if(visited.insert(next).second)
                queue.emplace_back(next,depth+1);
    }
    return out;
}

namespace survivor_curvature_detail {
inline bool finite(Vec3 v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
}
inline bool solve3(double m[3][3],double b[3],double x[3],
                   double& pivotRatio) {
    double largest=0,smallest=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<3;++i) {
        std::size_t pivot=i;
        for(std::size_t j=i+1;j<3;++j)
            if(std::abs(m[j][i])>std::abs(m[pivot][i]))pivot=j;
        double scale=0;
        for(std::size_t j=i;j<3;++j)
            for(std::size_t k=i;k<3;++k)
                scale=std::max(scale,std::abs(m[j][k]));
        if(!(scale>0)||std::abs(m[pivot][i])<=1.e-11*scale)
            return false;
        if(pivot!=i) {
            for(std::size_t k=0;k<3;++k)std::swap(m[pivot][k],m[i][k]);
            std::swap(b[pivot],b[i]);
        }
        const auto magnitude=std::abs(m[i][i]);
        largest=std::max(largest,magnitude);
        smallest=std::min(smallest,magnitude);
        for(std::size_t j=i+1;j<3;++j) {
            const auto factor=m[j][i]/m[i][i];
            for(std::size_t k=i;k<3;++k)m[j][k]-=factor*m[i][k];
            b[j]-=factor*b[i];
        }
    }
    pivotRatio=largest/smallest;
    for(int i=2;i>=0;--i) {
        double v=b[i];
        for(int j=i+1;j<3;++j)v-=m[i][j]*x[j];
        x[i]=v/m[i][i];
        if(!std::isfinite(x[i]))return false;
    }
    return std::isfinite(pivotRatio);
}
} // namespace survivor_curvature_detail

// Fit the symmetric 2x2 shape operator S=-d(normal)/d(position) using
// normal differences among a bounded set of connected surviving face
// centroids. The seam frame is t=normalize(B-A),
// u=normalize(n_survivor x t), and t x u=n_survivor.
// For each neighbor with tangent delta (x,y), the least-squares equations
// are -dn_t=S_tt*x+S_tu*y, -dn_u=S_tu*x+S_uu*y.
// Three-pass bounded Huber IRLS limits single-face outliers. A suspicious
// crease, high residual or ill conditioning LOWERS CONFIDENCE; it does not
// reject a valid cap or rewrite the curvature toward zero.
inline SurvivorDirectionalCurvature estimateSurvivorDirectionalCurvature(
    Vec3 edgeA,Vec3 edgeB,
    const SurvivorCurvatureNeighborhood& neighborhood,
    const SurvivorCurvatureLimits& limits={}) {
    using namespace survivor_curvature_detail;
    SurvivorDirectionalCurvature out;
    if(neighborhood.missingSeed||neighborhood.faces.empty())return out;
    const auto& seed=neighborhood.faces.front();
    const auto span=edgeB-edgeA;
    const auto h=length(span),normalLength=length(seed.normal);
    out.edgeLength=h;
    if(!finite(edgeA)||!finite(edgeB)||!finite(seed.centroid)||
       !finite(seed.normal)||!(h>1.e-12)||!(normalLength>1.e-12))
        return out;
    const auto t=span/h;
    const auto n=seed.normal/normalLength;
    const auto u=cross(n,t);
    if(!(length(u)>1.e-10))return out;
    struct Observation {
        double x{},y{},nt{},nu{},weight{},robust=1.;
    };
    std::vector<Observation> observations;
    observations.reserve(std::min(limits.maxFaces,neighborhood.faces.size()));
    bool inconsistent=false,crease=false;
    for(std::size_t i=1;i<neighborhood.faces.size();++i) {
        const auto& sample=neighborhood.faces[i];
        const auto m=length(sample.normal);
        if(!finite(sample.centroid)||!finite(sample.normal)||
           !(m>1.e-12)) {++out.skippedFaces;continue;}
        const auto sn=sample.normal/m;
        const auto cosine=std::clamp(dot(n,sn),-1.,1.);
        const auto angle=std::acos(cosine);
        out.maximumNormalAngle=std::max(out.maximumNormalAngle,angle);
        if(cosine<0)inconsistent=true;
        if(angle>limits.creaseAngleRadians)crease=true;
        const auto shift=sample.centroid-seed.centroid;
        const auto x=dot(shift,t),y=dot(shift,u);
        const auto distance2=x*x+y*y;
        if(!(distance2>h*h*1.e-12)) {
            ++out.skippedFaces;
            continue;
        }
        const auto difference=sn-n;
        const double scaled=distance2/(h*h);
        observations.push_back({x,y,-dot(difference,t),
            -dot(difference,u),1./(1.+scaled),1.});
    }
    out.usableNeighbors=observations.size();
    out.contributingFaces=out.usableNeighbors+1;
    if(observations.size()<limits.minimumNeighbors) {
        out.confidence=SurvivorCurvatureConfidence::InsufficientSamples;
        return out;
    }

    double coefficients[3]{};
    double pivotRatio=0;
    for(unsigned iteration=0;iteration<3;++iteration) {
        double matrix[3][3]{},rhs[3]{};
        const auto add=[&](std::array<double,3> row,double value,double w) {
            for(std::size_t a=0;a<3;++a) {
                rhs[a]+=w*row[a]*value;
                for(std::size_t b=0;b<3;++b)
                    matrix[a][b]+=w*row[a]*row[b];
            }
        };
        for(const auto& o:observations) {
            const auto w=o.weight*o.robust;
            add({o.x,o.y,0.},o.nt,w);
            add({0.,o.x,o.y},o.nu,w);
        }
        if(!solve3(matrix,rhs,coefficients,pivotRatio)) {
            out.confidence=SurvivorCurvatureConfidence::RankDeficient;
            return out;
        }
        if(iteration<2) {
            std::vector<double> residuals;
            residuals.reserve(observations.size());
            for(const auto& o:observations) {
                const auto dt=coefficients[0]*o.x+coefficients[1]*o.y-o.nt;
                const auto du=coefficients[1]*o.x+coefficients[2]*o.y-o.nu;
                residuals.push_back(std::sqrt(dt*dt+du*du));
            }
            auto sorted=residuals;
            std::sort(sorted.begin(),sorted.end());
            const auto huber=std::max(.02,2.5*sorted[sorted.size()/2]);
            for(std::size_t i=0;i<observations.size();++i)
                observations[i].robust=residuals[i]>huber?
                    huber/residuals[i]:1.;
        }
    }
    out.seam=coefficients[0];
    out.mixed=coefficients[1];
    out.transverse=coefficients[2];
    out.pivotRatio=pivotRatio;
    out.hasEstimate=true;
    double total=0;
    for(const auto& o:observations) {
        const auto dt=coefficients[0]*o.x+coefficients[1]*o.y-o.nt;
        const auto du=coefficients[1]*o.x+coefficients[2]*o.y-o.nu;
        total+=dt*dt+du*du;
    }
    out.rmsNormalResidual=std::sqrt(total/observations.size());
    out.confidenceScore=std::clamp(
        1.-out.rmsNormalResidual/std::max(.001,limits.maximumRmsNormalResidual),
        0.,1.);
    if(inconsistent)
        out.confidence=SurvivorCurvatureConfidence::InconsistentNormals;
    else if(crease)
        out.confidence=SurvivorCurvatureConfidence::CreaseSuspected;
    else if(pivotRatio>limits.maximumPivotRatio)
        out.confidence=SurvivorCurvatureConfidence::RankDeficient;
    else if(out.rmsNormalResidual>limits.maximumRmsNormalResidual)
        out.confidence=SurvivorCurvatureConfidence::HighResidual;
    else if(neighborhood.truncated)
        out.confidence=SurvivorCurvatureConfidence::Truncated;
    else out.confidence=SurvivorCurvatureConfidence::Reliable;
    if(out.confidence!=SurvivorCurvatureConfidence::Reliable)
        out.confidenceScore=std::min(out.confidenceScore,.25);
    return out;
}

} // namespace mesher
