#pragma once

#include "mesher/repair_cap_spatial.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mesher {

// M18A diagnostic ONLY. Input normals use the same incoming-edge ordering
// as buildSpatialCapFrontPoisson: normals[k] belongs to the ORIGINAL R03
// survivor edge ending at oriented CAP vertex k (because the rim is reversed).
// For the outgoing CAP edge k -> (k+1)%n, its ORIGINAL survivor face normal
// is therefore normals[(k+1)%n]. These are true indexed mates, NOT proximity
// matches. No geometry, quality gate or acceptance status is modified.
struct SpatialRimProfileEdge {
    std::size_t rimStart{},rimEnd{},capFaceIndex{};
    std::uint32_t capOppositeVertex{};
    double edgeLength{};
    bool hasSurvivorNormal{};
    bool hasCapNormal{};
    double signedDihedralRadians{};
    double absoluteDihedralRadians{};
    // Relative to the matched original survivor face normal.
    double signedFirstRingNormalOffset{};
    double normalizedFirstRingNormalOffset{};
    // Projection of opposite cap corner along outgoing original rim edge.
    // 0 and 1 project to its endpoints; outside [0,1] flags tangent overshoot.
    double oppositeFootParameter{};
};

struct SpatialRimProfile {
    std::vector<SpatialRimProfileEdge> edges;
    std::size_t measuredEdges{},unavailableEdges{};
    double maximumAbsoluteDihedralRadians{};
    double meanAbsoluteDihedralRadians{};
    double maximumAbsoluteNormalizedOffset{};
};

// Pure, bounded by the patch's existing face/rim limits. The first-order
// signed dihedral is a profile diagnostic, NOT the curvature tensor (M18B).
// A zero/absent survivor normal gives an explicit unavailable measurement.
inline SpatialRimProfile measureSpatialCapRimProfile(
    const SpatialCapPatch& patch,
    const std::vector<Vec3>& incomingSurvivorFaceNormals) {
    const auto n=patch.boundaryCount;
    if(n<3||patch.positions.size()<n||
       (!incomingSurvivorFaceNormals.empty()&&
        incomingSurvivorFaceNormals.size()!=n))
        throw std::invalid_argument("R06-M18A invalid rim profile inputs");

    SpatialRimProfile out;
    out.edges.reserve(n);
    for(std::size_t i=0;i<n;++i) {
        const auto j=(i+1)%n;
        const auto a=patch.positions[i],b=patch.positions[j];
        const auto axis=b-a;
        const auto edgeLength=length(axis);
        if(!(edgeLength>1e-12)||!std::isfinite(edgeLength))
            throw std::logic_error("R06-M18A invalid cap rim edge length");

        // Require the exact directed boundary edge, not a coincident edge
        // introduced elsewhere in the patch.
        std::size_t capFace=patch.faces.size();
        std::uint32_t opposite=0;
        for(std::size_t f=0;f<patch.faces.size();++f) {
            const auto tri=patch.faces[f];
            for(std::size_t k=0;k<3;++k) {
                if(tri[k]!=i||tri[(k+1)%3]!=j)continue;
                if(capFace!=patch.faces.size())
                    throw std::logic_error("R06-M18A cap rim edge covered twice");
                capFace=f;
                opposite=tri[(k+2)%3];
            }
        }
        if(capFace==patch.faces.size()||opposite>=patch.positions.size())
            throw std::logic_error("R06-M18A cap rim edge missing indexed cap face");

        SpatialRimProfileEdge sample;
        sample.rimStart=i;
        sample.rimEnd=j;
        sample.capFaceIndex=capFace;
        sample.capOppositeVertex=opposite;
        sample.edgeLength=edgeLength;

        const auto c=patch.positions[opposite];
        const auto normal=cross(b-a,c-a);
        const auto capSize=length(normal);
        sample.hasCapNormal=capSize>1e-12&&std::isfinite(capSize);
        const auto source=incomingSurvivorFaceNormals.empty()?
            Vec3{}:incomingSurvivorFaceNormals[j];
        const auto sourceSize=length(source);
        sample.hasSurvivorNormal=sourceSize>1e-12&&std::isfinite(sourceSize);
        if(sample.hasCapNormal&&sample.hasSurvivorNormal) {
            const auto ns=source/sourceSize;
            const auto nc=normal/capSize;
            const auto t=axis/edgeLength;
            const auto signedSin=dot(t,cross(ns,nc));
            const auto cosine=std::clamp(dot(ns,nc),-1.,1.);
            sample.signedDihedralRadians=std::atan2(signedSin,cosine);
            sample.absoluteDihedralRadians=std::abs(sample.signedDihedralRadians);
            sample.signedFirstRingNormalOffset=dot(c-a,ns);
            sample.normalizedFirstRingNormalOffset=
                sample.signedFirstRingNormalOffset/edgeLength;
            sample.oppositeFootParameter=dot(c-a,axis)/(edgeLength*edgeLength);
            out.maximumAbsoluteDihedralRadians=std::max(
                out.maximumAbsoluteDihedralRadians,
                sample.absoluteDihedralRadians);
            out.maximumAbsoluteNormalizedOffset=std::max(
                out.maximumAbsoluteNormalizedOffset,
                std::abs(sample.normalizedFirstRingNormalOffset));
            out.meanAbsoluteDihedralRadians+=sample.absoluteDihedralRadians;
            ++out.measuredEdges;
        }else {
            ++out.unavailableEdges;
        }
        out.edges.push_back(sample);
    }
    if(out.measuredEdges)
        out.meanAbsoluteDihedralRadians/=
            static_cast<double>(out.measuredEdges);
    return out;
}
} // namespace mesher
