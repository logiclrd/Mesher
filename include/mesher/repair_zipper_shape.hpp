#pragma once

#include "mesher/mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace mesher {

// Read-only zipper shape template. Index triplets refer to the incoming
// oriented triangle, never to mutable/stable mesh identities.
struct ZipperShapeProposal {
    enum class Kind {ApexToBase, TwoEdge};
    Kind kind{Kind::ApexToBase};
    std::array<unsigned,3> corners{};
    double baseT{};
    bool useAlternateDiagonal{};
    double parentQuality{};
    double minimumChildQuality{};
    double baseToHeight{};
};

inline double zipperShapeQuality(Vec3 a,Vec3 b,Vec3 c) {
    const auto sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.)*length(cross(b-a,c-a))/sum:0.;
}

// The greatest candidate base length must be at least 80% of the zipper
// target edge. This is an eligibility rule, NOT a closure-quality waiver.
// Explore all three oriented base edges: with an overhanging apex the
// visually useful base need not be the mathematically longest side.
inline std::optional<ZipperShapeProposal> chooseZipperShape(
    const std::array<Vec3,3>& p,double targetEdge,double qualityFloor) {
    if(!(targetEdge>0)&&!std::isfinite(targetEdge))return std::nullopt;
    if(!(targetEdge>0)||!std::isfinite(targetEdge)||
       !(qualityFloor>0)||!std::isfinite(qualityFloor))
        return std::nullopt;
    const auto parent=zipperShapeQuality(p[0],p[1],p[2]);
    const auto distance=[](Vec3 a,Vec3 b){return length(a-b);};
    const auto longest=std::max({distance(p[0],p[1]),
        distance(p[1],p[2]),distance(p[2],p[0])});
    if(!(longest>=.8*targetEdge))return std::nullopt;

    std::optional<ZipperShapeProposal> best;
    const auto consider=[&](ZipperShapeProposal candidate) {
        if(!std::isfinite(candidate.minimumChildQuality)||
           candidate.minimumChildQuality+1e-12<qualityFloor)return;
        // A bounded loss of worst-child quality is acceptable if the
        // elongation is reduced, but a large regression is not.
        if(candidate.minimumChildQuality+1e-12<parent*.95)return;
        if(!best||candidate.minimumChildQuality>
                    best->minimumChildQuality+1e-12||
           (std::abs(candidate.minimumChildQuality-
                     best->minimumChildQuality)<=1e-12&&
            candidate.baseToHeight>best->baseToHeight))
            best=candidate;
    };
    for(unsigned i=0;i<3;++i) {
        unsigned ai=i,bi=(i+1)%3,ci=(i+2)%3;
        auto a=p[ai],b=p[bi],c=p[ci];
        auto ab=b-a;
        const auto len=length(ab);
        if(!(len>0))continue;
        const auto height=length(cross(ab,c-a))/len;
        if(!(height>0&&len>2.5*height))continue;
        const auto angleWide=[&](Vec3 origin,Vec3 along,Vec3 opposite) {
            const auto u=along-origin,v=opposite-origin;
            return dot(u,v)<std::sqrt(.5)*length(u)*length(v);
        };
        bool wideA=angleWide(a,b,c);
        bool wideB=angleWide(b,a,c);
        if(!wideA&&!wideB) {
            const double foot=dot(c-a,ab)/dot(ab,ab);
            const double t=(foot+.5)*.5;
            if(!(t>0&&t<1))continue;
            const auto d=a+ab*t;
            consider({ZipperShapeProposal::Kind::ApexToBase,
                {ai,bi,ci},t,false,parent,
                std::min(zipperShapeQuality(a,d,c),
                         zipperShapeQuality(d,b,c)),len/height});
            continue;
        }
        // The wide base end is A; reverse the edge when it is B.
        if(!wideA&&wideB) {
            std::swap(ai,bi);std::swap(a,b);
        }
        // The split is at D=2/3 A + 1/3 B, E=(B+C)/2.
        const auto d=a+(b-a)/3.;
        const auto e=(b+c)*.5;
        const auto residual=zipperShapeQuality(b,d,e);
        const auto cd=std::min({
            zipperShapeQuality(a,c,d),
            zipperShapeQuality(c,d,e),residual});
        const auto ae=std::min({
            zipperShapeQuality(a,c,e),
            zipperShapeQuality(a,d,e),residual});
        // Ties prefer CD: it produces three equal-area children.
        const bool alternate=ae>cd+1e-12;
        consider({ZipperShapeProposal::Kind::TwoEdge,
            {ai,bi,ci},1./3.,alternate,parent,
            alternate?ae:cd,len/height});
    }
    return best;
}
} // namespace mesher
