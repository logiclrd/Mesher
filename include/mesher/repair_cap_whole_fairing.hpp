#pragma once

#include "mesher/repair_surface_profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {

// M21A1: entirely LOCAL speculative fairing of an already valid cap.
// Never edits R03 rim, the input patch, source mesh, or staging. Scores
// the *whole* cap: neither a largest-dihedral heuristic nor a face mask.
struct SpatialCapFairingLimits {
    std::size_t maxRounds=3,maxTrials=512,maxFaces=4096;
    double minimumQuality=.10;
    double maximumVertexDisplacementEdges=.55;
    double minimumMeaningfulScoreReduction=1e-6;
    double maximumWorstRegressionRadians=1e-8;
};
struct SpatialCapFairingScore {
    double meanInteriorSquaredRadians{},meanSeamSquaredRadians{};
    double worstInteriorRadians{},worstSeamRadians{};
    double total{},minimumQuality{1.};
    std::size_t interiorEdges{},seamEdges{};
};
struct SpatialCapFairingReport {
    SpatialCapFairingScore baseline{},selected{};
    std::size_t trials{},scoreImprovingTrials{},legallyAdmissibleTrials{},
                acceptedRounds{},qualityRejected{},orientationRejected{},
                shapeRejected{},contactRejected{};
    bool contactBudgetExhausted=false,improved=false;
};

// This is deliberately a measurable but modest first-order objective.
// A smooth curved surface has nonzero dihedrals. The source-backed rim
// gets its own component; neither component may worsen at its maximum.
inline SpatialCapFairingScore measureSpatialCapFairing(
    const SpatialCapPatch& patch,
    const std::vector<Vec3>& incomingNormals,
    std::size_t maxFaces=4096) {
    if(patch.boundaryCount<3||patch.faces.empty()||
       patch.faces.size()>maxFaces||
       incomingNormals.size()!=patch.boundaryCount)
        throw std::invalid_argument("R06-M21A malformed finite fairing metric");
    SpatialCapFairingScore out;
    const auto n=patch.faces.size();
    using Edge=std::pair<std::uint32_t,std::uint32_t>;
    std::map<Edge,std::vector<std::size_t>> edges;
    std::vector<Vec3> normals(n);
    for(std::size_t f=0;f<n;++f) {
        const auto t=patch.faces[f];
        if(t[0]>=patch.positions.size()||t[1]>=patch.positions.size()||
           t[2]>=patch.positions.size()||t[0]==t[1]||
           t[1]==t[2]||t[0]==t[2])
            throw std::logic_error("R06-M21A invalid cap face indices");
        const auto a=patch.positions[t[0]],b=patch.positions[t[1]],
                   c=patch.positions[t[2]];
        const auto d=cross(b-a,c-a),len=length(d);
        if(!(len>1e-14)||!std::isfinite(len))
            throw std::logic_error("R06-M21A degenerate cap face");
        normals[f]=d/len;
        const double length2=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
        const double q=2.*std::sqrt(3.)*len/length2;
        out.minimumQuality=std::min(out.minimumQuality,q);
        for(unsigned i=0;i<3;++i) {
            const auto x=t[i],y=t[(i+1)%3];
            edges[{std::min(x,y),std::max(x,y)}].push_back(f);
        }
    }
    for(const auto& [_,side]:edges) {
        if(side.size()>2)
            throw std::logic_error("R06-M21A nonmanifold indexed cap edge");
        if(side.size()!=2)continue;
        const auto cosine=std::clamp(
            dot(normals[side[0]],normals[side[1]]),-1.,1.);
        const double angle=cosine>1.-1e-12?0.:std::acos(cosine);
        out.worstInteriorRadians=std::max(out.worstInteriorRadians,angle);
        out.meanInteriorSquaredRadians+=angle*angle;
        ++out.interiorEdges;
    }
    if(out.interiorEdges)
        out.meanInteriorSquaredRadians/=out.interiorEdges;
    const auto rim=measureSpatialCapRimProfile(patch,incomingNormals);
    for(const auto& e:rim.edges) {
        if(!e.hasCapNormal||!e.hasSurvivorNormal)
            throw std::logic_error("R06-M21A missing genuine survivor seam normal");
        out.worstSeamRadians=std::max(
            out.worstSeamRadians,e.absoluteDihedralRadians);
        out.meanSeamSquaredRadians+=
            e.absoluteDihedralRadians*e.absoluteDihedralRadians;
        ++out.seamEdges;
    }
    if(out.seamEdges)out.meanSeamSquaredRadians/=out.seamEdges;
    out.total=out.meanInteriorSquaredRadians+
              .5*out.meanSeamSquaredRadians;
    return out;
}

template<class ValidateCap>
SpatialCapFairingReport proposeSpatialCapWholeFairing(
    const SpatialCapPatch& original,
    const std::vector<Vec3>& incomingNormals,
    const std::vector<std::size_t>& mirroredOrbits,
    double medianRimEdge,ValidateCap&& validateCap,
    const SpatialCapFairingLimits& limits={},
    SpatialCapPatch* selectedPatch=nullptr) {
    if(!limits.maxRounds||!limits.maxTrials||!limits.maxFaces||
       original.faces.size()>limits.maxFaces||
       !(medianRimEdge>0&&std::isfinite(medianRimEdge))||
       !(limits.minimumQuality>0&&limits.minimumQuality<1)||
       !(limits.maximumVertexDisplacementEdges>0&&
         std::isfinite(limits.maximumVertexDisplacementEdges))||
       !(limits.minimumMeaningfulScoreReduction>0)||
       (!mirroredOrbits.empty()&&
        mirroredOrbits.size()!=original.positions.size()))
        throw std::invalid_argument("R06-M21A invalid finite fairing limits");
    SpatialCapFairingReport report;
    report.baseline=measureSpatialCapFairing(
        original,incomingNormals,limits.maxFaces);
    report.selected=report.baseline;
    // Baseline must already satisfy mandatory geometry floor.
    // Otherwise this optional operation refuses to *claim* a baseline.
    if(report.baseline.minimumQuality+1e-12<limits.minimumQuality) {
        if(selectedPatch)*selectedPatch=original;
        return report;
    }
    const auto count=original.positions.size(),rim=original.boundaryCount;
    std::vector<std::set<std::size_t>> adjacency(count);
    for(const auto& face:original.faces)
        for(unsigned j=0;j<3;++j) {
            adjacency[face[j]].insert(face[(j+1)%3]);
            adjacency[face[(j+1)%3]].insert(face[j]);
        }
    const auto reflect=[](Vec3 v)->Vec3{return {-v.x,v.y,v.z};};
    for(std::size_t v=0;v<count;++v)if(!mirroredOrbits.empty()) {
        const auto mate=mirroredOrbits[v];
        if(mate>=count||mirroredOrbits[mate]!=v)
            throw std::invalid_argument("R06-M21A broken exact mirrored orbit");
        const auto a=original.positions[v],b=original.positions[mate];
        if((mate==v&&a.x!=0)||
           (mate!=v&&(a.x!=-b.x||a.y!=b.y||a.z!=b.z)))
            throw std::invalid_argument("R06-M21A inexact baseline mirror positions");
    }
    SpatialCapPatch current=original;
    // Entire-patch objective is always computed on full proposed geometry;
    // work is bounded and every source rim position stays bit-identical.
    for(std::size_t round=0;round<limits.maxRounds;++round) {
        auto best=current;
        auto bestScore=report.selected;
        bool exhausted=false;
        const auto attempt=[&](SpatialCapPatch candidate) {
            if(report.trials>=limits.maxTrials) {
                exhausted=true;return;
            }
            ++report.trials;
            double totalMove2=0;
            for(std::size_t i=0;i<count;++i) {
                if(i<rim) {
                    const auto a=candidate.positions[i],b=original.positions[i];
                    if(a.x!=b.x||a.y!=b.y||a.z!=b.z) {
                        ++report.shapeRejected;return;
                    }
                    continue;
                }
                const double distance=length(
                    candidate.positions[i]-original.positions[i]);
                if(distance>limits.maximumVertexDisplacementEdges*
                            medianRimEdge) {
                    ++report.shapeRejected;return;
                }
                totalMove2+=distance*distance;
            }
            for(std::size_t f=0;f<original.faces.size();++f) {
                const auto t=original.faces[f];
                const auto a=original.positions[t[0]],
                           b=original.positions[t[1]],
                           c=original.positions[t[2]];
                const auto x=candidate.positions[t[0]],
                           y=candidate.positions[t[1]],
                           z=candidate.positions[t[2]];
                if(dot(cross(b-a,c-a),cross(y-x,z-x))<=0) {
                    ++report.orientationRejected;return;
                }
            }
            SpatialCapFairingScore scored;
            try {scored=measureSpatialCapFairing(
                    candidate,incomingNormals,limits.maxFaces);}
            catch(const std::exception&){++report.qualityRejected;return;}
            if(scored.minimumQuality+1e-12<limits.minimumQuality) {
                ++report.qualityRejected;return;
            }
            if(scored.worstSeamRadians>
                   report.baseline.worstSeamRadians+
                   limits.maximumWorstRegressionRadians||
               scored.worstInteriorRadians>
                   report.baseline.worstInteriorRadians+
                   limits.maximumWorstRegressionRadians)
                return;
            const double displacementPenalty=
                .01*totalMove2/(medianRimEdge*medianRimEdge*
                                 std::max(std::size_t{1},count-rim));
            if(scored.total+displacementPenalty+
               limits.minimumMeaningfulScoreReduction>=bestScore.total)
                return;
            ++report.scoreImprovingTrials;
            candidate.minimumQuality=1.;
            bool legal=false;
            try {legal=validateCap(candidate);}
            catch(const std::length_error&) {
                report.contactBudgetExhausted=true;
                exhausted=true;return;
            }catch(const std::exception&) {
                ++report.contactRejected;return;
            }
            if(!legal) {++report.contactRejected;return;}
            ++report.legallyAdmissibleTrials;
            best=std::move(candidate);
            bestScore=scored;
        };
        // Joint Jacobi sweeps may improve broad regions at once. They
        // retain the M19 positions as close anchors, not extrapolated
        // guesses based on a separate face anomaly mask.
        for(const double alpha:{.10,.25,.45}) {
            if(exhausted)break;
            auto candidate=current;
            for(std::size_t v=rim;v<count;++v) {
                if(!mirroredOrbits.empty()&&mirroredOrbits[v]<v)continue;
                if(adjacency[v].empty())continue;
                Vec3 mean{};
                for(const auto a:adjacency[v])
                    mean=mean+current.positions[a];
                mean=mean/static_cast<double>(adjacency[v].size());
                auto moved=current.positions[v]+
                           (mean-current.positions[v])*alpha;
                const auto mate=mirroredOrbits.empty()?v:mirroredOrbits[v];
                if(!mirroredOrbits.empty()&&mate==v)moved.x=0;
                candidate.positions[v]=moved;
                if(mate!=v)candidate.positions[mate]=reflect(moved);
            }
            attempt(std::move(candidate));
        }
        // Local coordinate probes optimize the same *global* objective;
        // unlike M19, no single seam vertex is privileged or forced.
        for(std::size_t v=rim;v<count&&!exhausted;++v) {
            if(!mirroredOrbits.empty()&&mirroredOrbits[v]<v)continue;
            if(adjacency[v].empty())continue;
            Vec3 mean{};
            for(const auto a:adjacency[v])
                mean=mean+current.positions[a];
            mean=mean/static_cast<double>(adjacency[v].size());
            for(const double alpha:{.20,.50}) {
                auto candidate=current;
                auto moved=current.positions[v]+
                          (mean-current.positions[v])*alpha;
                const auto mate=mirroredOrbits.empty()?v:mirroredOrbits[v];
                if(!mirroredOrbits.empty()&&mate==v)moved.x=0;
                candidate.positions[v]=moved;
                if(v!=mate)candidate.positions[mate]=reflect(moved);
                attempt(std::move(candidate));
                if(exhausted)break;
            }
        }
        if(bestScore.total+limits.minimumMeaningfulScoreReduction>=
           report.selected.total)break;
        current=std::move(best);
        report.selected=bestScore;
        ++report.acceptedRounds;
        if(exhausted)break;
    }
    report.improved=report.acceptedRounds>0;
    if(selectedPatch)*selectedPatch=report.improved?current:original;
    return report;
}

} // namespace mesher
