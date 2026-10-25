#pragma once

#include "mesher/repair_cap_spatial.hpp"
#include "mesher/repair_survivor_curvature.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {

// R06-M20A1 READ-ONLY proposal. Face/vertex ordinals are strictly local to
// this candidate SpatialCapPatch; never weld positions or touch the R03 root.
struct SpatialCapAnomalyReport {
    std::size_t measuredInteriorEdges{},suspectedInteriorEdges{};
    std::size_t unreliableSourceEdges{},sourceCreaseEdges{};
    double worstMeasuredJumpRadians{},worstUnexplainedJumpRadians{};
    std::vector<std::size_t> suspectFaces; // proposed excision INCLUDING halo
    std::vector<std::vector<std::uint32_t>> secondaryContours;
    bool touchesOriginalRim=false,mirrored=false;
    std::string refusal;
    bool hasProposal() const {return !secondaryContours.empty();}
};
struct SpatialCapAnomalyLimits {
    std::size_t maxFaces=4096,maxRegionFaces=256,maxContours=8;
    std::size_t haloRings=1; // larger growth is a separate milestone
    // Conservative heuristic when independent source tensor evidence is
    // sparse; not a curvature certificate and not a repair acceptance gate.
    double minimumNormalJumpRadians=.60; // ~34 degrees
    double minimumUnexplainedRadians=.30; // ~17 degrees beyond source trend
};

inline SpatialCapAnomalyReport diagnoseSpatialCapAnomaly(
    const SpatialCapPatch& patch,
    const std::vector<Vec3>& incomingSurvivorNormals,
    const std::vector<SurvivorDirectionalCurvature>& originalCurvature={},
    const std::vector<std::size_t>& reflectionOrbits={},
    const SpatialCapAnomalyLimits& limits={}) {
    SpatialCapAnomalyReport report;
    const auto n=patch.boundaryCount;
    if(n<3||patch.positions.size()<n||patch.faces.empty()||
       patch.faces.size()>limits.maxFaces||!limits.maxRegionFaces||
       !limits.maxContours||limits.haloRings>3||
       incomingSurvivorNormals.size()!=n||
       (!originalCurvature.empty()&&originalCurvature.size()!=n)||
       (!reflectionOrbits.empty()&&
        reflectionOrbits.size()!=patch.positions.size())||
       !std::isfinite(limits.minimumNormalJumpRadians)||
       !std::isfinite(limits.minimumUnexplainedRadians)||
       limits.minimumNormalJumpRadians<=0||
       limits.minimumUnexplainedRadians<=0)
        throw std::invalid_argument("R06-M20A1 malformed bounded local cap diagnosis");
    report.mirrored=!reflectionOrbits.empty();

    using Edge=std::pair<std::uint32_t,std::uint32_t>;
    const auto key=[](std::uint32_t a,std::uint32_t b)->Edge {
        return {std::min(a,b),std::max(a,b)};
    };
    std::map<Edge,std::vector<std::size_t>> incidence;
    std::map<std::array<std::uint32_t,3>,std::size_t> byTriangle;
    std::vector<Vec3> normals(patch.faces.size()),centroids(patch.faces.size());
    const auto canonical=[](std::array<std::uint32_t,3> v) {
        std::sort(v.begin(),v.end());return v;
    };
    for(std::size_t f=0;f<patch.faces.size();++f) {
        const auto tri=patch.faces[f];
        if(tri[0]>=patch.positions.size()||tri[1]>=patch.positions.size()||
           tri[2]>=patch.positions.size()||
           tri[0]==tri[1]||tri[1]==tri[2]||tri[2]==tri[0])
            throw std::logic_error("R06-M20A1 invalid local cap triangle");
        if(!byTriangle.emplace(canonical(tri),f).second)
            throw std::logic_error("R06-M20A1 duplicate local face");
        const auto a=patch.positions[tri[0]],b=patch.positions[tri[1]],
                   c=patch.positions[tri[2]];
        const auto normal=cross(b-a,c-a);
        const auto len=length(normal);
        if(!(len>1e-14)||!std::isfinite(len))
            throw std::logic_error("R06-M20A1 degenerate/nonfinite cap face");
        normals[f]=normal/len;
        centroids[f]=(a+b+c)/3.;
        for(unsigned j=0;j<3;++j)
            incidence[key(tri[j],tri[(j+1)%3])].push_back(f);
    }
    std::vector<std::set<std::size_t>> adjacent(patch.faces.size());
    for(const auto& [edge,sides]:incidence) {
        if(sides.size()>2)
            throw std::logic_error("R06-M20A1 nonmanifold local cap edge");
        if(sides.size()==2) {
            adjacent[sides[0]].insert(sides[1]);
            adjacent[sides[1]].insert(sides[0]);
        }
    }
    if(report.mirrored) {
        for(std::size_t v=0;v<reflectionOrbits.size();++v)
            if(reflectionOrbits[v]>=reflectionOrbits.size()||
               reflectionOrbits[reflectionOrbits[v]]!=v)
                throw std::logic_error("R06-M20A1 invalid exact reflected orbit");
    }
    // Independent original survivor observation: robust lower-quartile
    // normal difference, rather than allowing a single sharp crease to
    // train the cap target. Shape-operator values are used ONLY when
    // qualified and only to widen the diagnostic expectation, never to
    // silently trust the CAP itself as its reference surface.
    std::vector<double> survivorBends;
    for(std::size_t i=0;i<n;++i) {
        const auto a=incomingSurvivorNormals[i];
        const auto b=incomingSurvivorNormals[(i+1)%n];
        const auto la=length(a),lb=length(b);
        if(!(la>1e-12&&lb>1e-12))continue;
        const auto bend=std::acos(std::clamp(dot(a,b)/(la*lb),-1.,1.));
        if(std::isfinite(bend)) {
            survivorBends.push_back(bend);
            if(bend>.90)++report.sourceCreaseEdges;
        }
    }
    if(survivorBends.empty())
        throw std::invalid_argument("R06-M20A1 no independent source normals");
    std::sort(survivorBends.begin(),survivorBends.end());
    const auto sourceBend=survivorBends[survivorBends.size()/4];
    std::vector<double> rimEdges;
    for(std::size_t i=0;i<n;++i)
        rimEdges.push_back(length(patch.positions[i]-
            patch.positions[(i+1)%n]));
    std::sort(rimEdges.begin(),rimEdges.end());
    const double h=rimEdges[n/2];
    if(!(h>1e-12&&std::isfinite(h)))
        throw std::invalid_argument("R06-M20A1 invalid original rim scale");

    Edge worstEdge{};
    double worstResidual=0;
    for(const auto& [edge,sides]:incidence) {
        if(sides.size()!=2)continue;
        const auto f=sides[0],g=sides[1];
        ++report.measuredInteriorEdges;
        const auto cosine=std::clamp(dot(normals[f],normals[g]),-1.,1.);
        const double observed=std::acos(cosine);
        report.worstMeasuredJumpRadians=std::max(
            report.worstMeasuredJumpRadians,observed);
        const auto spacing=std::max(length(centroids[f]-centroids[g]),h*.25);
        double expected=std::min(.85,sourceBend*spacing/h*1.5+.12);
        bool reliable=false;
        if(!originalCurvature.empty()) {
            // Only rim vertices actually incident to these TWO source-
            // adjacent cap faces can supply qualified local reference.
            // Other cap interior edges remain explicitly low-confidence.
            // Never borrow an unrelated confident sample from elsewhere.
            std::set<std::size_t> anchors;
            for(const auto faceIndex:{f,g})
                for(const auto v:patch.faces[faceIndex])
                    if(v<n)anchors.insert(v);
            for(const auto i:anchors) {
                const auto& v=originalCurvature[i];
                if(v.hasEstimate&&
                   v.confidence==SurvivorCurvatureConfidence::Reliable&&
                   v.confidenceScore>=.5) {
                    const auto norm=std::sqrt(v.seam*v.seam+
                        2*v.mixed*v.mixed+v.transverse*v.transverse);
                    if(std::isfinite(norm)) {
                        expected=std::max(expected,
                            std::min(.85,norm*spacing));
                        reliable=true;
                    }
                }
            }
        }
        if(!reliable)++report.unreliableSourceEdges;
        expected=std::max(.20,expected);
        const double residual=std::max(0.,observed-expected);
        if(observed>=limits.minimumNormalJumpRadians&&
           residual>=limits.minimumUnexplainedRadians&&
           (residual>worstResidual+1e-12||
            (std::abs(residual-worstResidual)<=1e-12&&edge<worstEdge))) {
            worstResidual=residual;
            worstEdge=edge;
        }
        report.worstUnexplainedJumpRadians=std::max(
            report.worstUnexplainedJumpRadians,residual);
        if(observed>=limits.minimumNormalJumpRadians&&
           residual>=limits.minimumUnexplainedRadians)
            ++report.suspectedInteriorEdges;
    }
    if(report.suspectedInteriorEdges==0) {
        report.refusal="no unexplained CAP/CAP normal jump above diagnostic threshold";
        return report;
    }

    std::set<std::size_t> selected;
    for(const auto f:incidence.at(worstEdge))selected.insert(f);
    // Mirror the chosen anomalous face island by exact local *indices*.
    // No coordinate-based association or approximate position welding.
    const auto addMirrors=[&]() {
        if(!report.mirrored)return;
        const auto current=selected;
        for(const auto f:current) {
            const auto face=patch.faces[f];
            const auto mirror=canonical({
                static_cast<std::uint32_t>(reflectionOrbits[face[0]]),
                static_cast<std::uint32_t>(reflectionOrbits[face[1]]),
                static_cast<std::uint32_t>(reflectionOrbits[face[2]])});
            const auto it=byTriangle.find(mirror);
            if(it==byTriangle.end())
                throw std::logic_error("R06-M20A1 missing mirrored face");
            selected.insert(it->second);
        }
    };
    addMirrors();
    for(std::size_t ring=0;ring<limits.haloRings;++ring) {
        const auto current=selected;
        for(const auto f:current)
            selected.insert(adjacent[f].begin(),adjacent[f].end());
        addMirrors();
    }
    if(selected.size()>limits.maxRegionFaces||
       selected.size()==patch.faces.size()) {
        report.refusal="secondary face island exhausts region budget or whole cap";
        return report;
    }

    // Boundary of selected oriented triangles. A secondary hole contour
    // may touch the immutable original R03 rim, but this first milestone
    // does NOT delete its survivors or commit any generated face deletion.
    std::map<std::uint32_t,std::uint32_t> next,previous;
    for(const auto f:selected) {
        const auto t=patch.faces[f];
        for(unsigned j=0;j<3;++j) {
            const auto a=t[j],b=t[(j+1)%3];
            const auto& side=incidence.at(key(a,b));
            const bool selectedOther=side.size()==2&&
                selected.contains(side[0]==f?side[1]:side[0]);
            if(selectedOther)continue;
            if(side.size()==1)report.touchesOriginalRim=true;
            if(!next.emplace(a,b).second||
               !previous.emplace(b,a).second) {
                report.refusal="secondary contour forks at local vertex";
                return report;
            }
        }
    }
    if(next.empty()||next.size()!=previous.size()) {
        report.refusal="secondary contour is empty or non-closed";
        return report;
    }
    std::set<std::uint32_t> unvisited;
    for(const auto& [a,b]:next) {
        if(!previous.contains(a)) {
            report.refusal="secondary contour contains an open endpoint";
            return report;
        }
        unvisited.insert(a);
    }
    while(!unvisited.empty()) {
        if(report.secondaryContours.size()>=limits.maxContours) {
            report.secondaryContours.clear();
            report.refusal="secondary contour exceeds finite loop limit";
            return report;
        }
        std::vector<std::uint32_t> walk;
        const auto start=*unvisited.begin();
        auto cursor=start;
        do {
            if(!unvisited.erase(cursor)) {
                report.secondaryContours.clear();
                report.refusal="secondary contour has repeated/forked vertex";
                return report;
            }
            walk.push_back(cursor);
            cursor=next.at(cursor);
        }while(cursor!=start);
        if(walk.size()<3) {
            report.secondaryContours.clear();
            report.refusal="secondary contour has fewer than three vertices";
            return report;
        }
        report.secondaryContours.push_back(std::move(walk));
    }
    report.suspectFaces.assign(selected.begin(),selected.end());
    return report;
}
} // namespace mesher
