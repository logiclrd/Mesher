#pragma once

#include "mesher/triangle_collision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {

// PRIVATE local-indexed advancing-front workspace, not the source-backed
// MixedClosureStaging or a published mesh. The first boundaryCount vertices
// retain immutable R03 positional identity. All faces here are cap faces.
struct AdvancingFrontWorkspace {
    std::size_t boundaryCount{};
    std::vector<Vec3> positions;
    std::vector<std::uint32_t> front;
    std::vector<std::array<std::uint32_t,3>> faces;
};

enum class MirroredBridgeOperation {
    AcuteSeamSplit, ThreeCornerConnection, WideSeamTriangle
};

struct MirroredBridgeAdvance {
    AdvancingFrontWorkspace workspace;
    MirroredBridgeOperation operation{};
    std::size_t addedVertices{};
    std::size_t addedFaces{};
};

// M22A: a PURE, atomic local-front operation for the special case
//   ..., P, A, mirror(A), mirror(P), ...
// with the selected front vertex A adjacent to its reflection.
// This is an independent kernel, NOT the complete AFM cap generator.
// A future M22B caller will own minimum-angle orbit selection, the other
// advancing-front corner cases, collision checks and source-backed staging.
//
// A crossing FRONT edge is split at its precise seam midpoint S. The
// bisector Q (where needed) is also X=0. This converts the user's
// self-mirrored crossing face into two exact reflected half-plane faces.
// A crossing ORIGINAL R03 edge is not legal; such a split requires M15
// source-backed editing and is deliberately refused here.
inline MirroredBridgeAdvance proposeMirroredBridgeFrontAdvance(
    const AdvancingFrontWorkspace& original,
    std::size_t selectedIndex,
    std::size_t maxPositions,
    std::size_t maxFaces,
    double minQuality) {
    const auto count=original.front.size();
    if(count<4||selectedIndex>=count||
       original.boundaryCount>original.positions.size()||
       original.positions.size()>maxPositions||
       original.faces.size()>maxFaces||
       !(minQuality>0&&minQuality<1)||!std::isfinite(minQuality))
        throw std::invalid_argument("M22A invalid bridge workspace or work limits");
    for(const auto id:original.front)
        if(id>=original.positions.size())
            throw std::logic_error("M22A front has invalid local vertex ID");
    const auto prev=(selectedIndex+count-1)%count;
    const auto next=(selectedIndex+1)%count;
    const auto after=(selectedIndex+2)%count;
    const auto pId=original.front[prev];
    const auto aId=original.front[selectedIndex];
    const auto bId=original.front[next];
    const auto rId=original.front[after];
    if(pId==aId||aId==bId||bId==rId||rId==pId||
       pId==bId||aId==rId)
        throw std::logic_error("M22A bridge has repeated local front IDs");
    const auto p=original.positions[pId];
    const auto a=original.positions[aId];
    const auto b=original.positions[bId];
    const auto r=original.positions[rId];
    const auto finite=[](Vec3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    };
    if(!finite(p)||!finite(a)||!finite(b)||!finite(r)||
       a.x==0||p.x*a.x<=0||
       b.x!=-a.x||b.y!=a.y||b.z!=a.z||
       r.x!=-p.x||r.y!=p.y||r.z!=p.z)
        throw std::logic_error("M22A expects an exact reflected front bridge");
    // Never treat an ORIGINAL boundary edge as a disposable temporary front
    // edge, regardless of its geometric proximity to the symmetry plane.
    const auto n=original.boundaryCount;
    if(aId<n&&bId<n&&
       ((aId+1)%n==bId||(bId+1)%n==aId))
        throw std::logic_error("M22A cannot split an original R03 boundary edge");

    const auto u=p-a;
    const auto v=b-a;
    const auto lu=length(u),lv=length(v);
    if(!(lu>1e-12&&lv>1e-12))
        throw std::logic_error("M22A degenerate front angle");
    const auto unitU=u/lu,unitV=v/lv;
    const auto radians=std::acos(std::clamp(dot(unitU,unitV),-1.,1.));
    const auto fortyFive=std::acos(-1.)/4.;
    const auto ninety=std::acos(-1.)/2.;
    const auto op=radians<fortyFive?
        MirroredBridgeOperation::AcuteSeamSplit:
        (radians<ninety?MirroredBridgeOperation::ThreeCornerConnection:
                        MirroredBridgeOperation::WideSeamTriangle);

    // Strong transaction exception guarantee: the input is never mutated.
    auto candidate=original;
    const Vec3 s{0,a.y,a.z};
    if(!finite(s)||candidate.positions.size()>=maxPositions)
        throw std::length_error("M22A seam midpoint vertex budget");
    for(std::size_t k=0;k<candidate.positions.size();++k) {
        // A distinct seam ID already at the same exact point would create
        // a disconnected vertex fan. No tolerance-based ID welding.
        const auto old=candidate.positions[k];
        if(old.x==0&&old.y==s.y&&old.z==s.z)
            throw std::logic_error("M22A coincident pre-existing seam vertex");
    }
    const auto sId=static_cast<std::uint32_t>(candidate.positions.size());
    candidate.positions.push_back(s);
    std::uint32_t qId=0;
    if(op!=MirroredBridgeOperation::AcuteSeamSplit) {
        const auto bisector=unitU+unitV;
        if(!(std::abs(bisector.x)>1e-12)||
           candidate.positions.size()>=maxPositions)
            throw std::length_error("M22A bisector unavailable or vertex budget");
        const auto factor=-a.x/bisector.x;
        if(!(factor>0&&std::isfinite(factor)))
            throw std::logic_error("M22A bisector does not hit forward seam");
        auto q=a+bisector*factor;
        q.x=0;
        if(!finite(q)||length(q-s)<=1e-9*lv)
            throw std::logic_error("M22A bisector makes degenerate seam triangle");
        for(std::size_t k=0;k<candidate.positions.size();++k) {
            const auto old=candidate.positions[k];
            if(old.x==0&&old.y==q.y&&old.z==q.z)
                throw std::logic_error("M22A duplicate exact seam position");
        }
        qId=static_cast<std::uint32_t>(candidate.positions.size());
        candidate.positions.push_back(q);
    }

    // A crossing front edge may already have one generated incident face.
    // Split that face at exactly the same S, but ONLY when the third vertex
    // itself lies on the seam; otherwise it would make new crossing faces.
    std::vector<std::array<std::uint32_t,3>> prior;
    prior.reserve(candidate.faces.size()+1);
    std::size_t adjacentFaces=0;
    for(const auto& f:candidate.faces) {
        unsigned side=3;
        for(unsigned k=0;k<3;++k)
            if((f[k]==aId&&f[(k+1)%3]==bId)||
               (f[k]==bId&&f[(k+1)%3]==aId))side=k;
        if(side==3) {
            prior.push_back(f);
            continue;
        }
        if(++adjacentFaces>1||
           candidate.positions[f[(side+2)%3]].x!=0)
            throw std::logic_error("M22A cannot split crossing generated face");
        const auto first=f[side],second=f[(side+1)%3],third=f[(side+2)%3];
        prior.push_back({first,sId,third});
        prior.push_back({sId,second,third});
    }
    candidate.faces=std::move(prior);

    const auto add=[&](std::uint32_t x,std::uint32_t y,std::uint32_t z) {
        candidate.faces.push_back({x,y,z});
    };
    if(op==MirroredBridgeOperation::AcuteSeamSplit) {
        add(pId,aId,sId);
        add(sId,bId,rId);
    }else if(op==MirroredBridgeOperation::ThreeCornerConnection) {
        add(pId,aId,qId);
        add(aId,sId,qId);
        add(sId,bId,qId);
        add(bId,rId,qId);
    }else {
        add(aId,sId,qId);
        add(sId,bId,qId);
    }
    if(candidate.faces.size()>maxFaces)
        throw std::length_error("M22A new face budget");

    // Each newly constructed face must stay on a closed half-space and
    // satisfy at least the requested quality, not merely finite area.
    for(const auto& f:candidate.faces) {
        for(const auto id:f)
            if(id>=candidate.positions.size())
                throw std::logic_error("M22A invalid staged face ID");
        const auto x=candidate.positions[f[0]];
        const auto y=candidate.positions[f[1]];
        const auto z=candidate.positions[f[2]];
        if(std::min({x.x,y.x,z.x})<0&&std::max({x.x,y.x,z.x})>0)
            throw std::logic_error("M22A crossing-plane triangle");
        const auto sum=dot(x-y,x-y)+dot(y-z,y-z)+dot(z-x,z-x);
        const auto q=sum>0?2*std::sqrt(3.)*length(cross(y-x,z-x))/sum:0.;
        if(!std::isfinite(q)||q<minQuality)
            throw std::logic_error("M22A under-quality bridge triangle");
    }

    // Rotate to P as the first member so insertion works even when the
    // selected pair straddles the cyclic array's last/first slots.
    std::vector<std::uint32_t> front;
    front.reserve(count+2);
    front.push_back(pId);
    if(op==MirroredBridgeOperation::AcuteSeamSplit)
        front.push_back(sId);
    else if(op==MirroredBridgeOperation::ThreeCornerConnection)
        front.push_back(qId);
    else {
        front.push_back(aId);
        front.push_back(qId);
        front.push_back(bId);
    }
    for(std::size_t i=3;i<count;++i)
        front.push_back(original.front[(prev+i)%count]);
    candidate.front=std::move(front);
    MirroredBridgeAdvance answer;
    answer.operation=op;
    answer.addedVertices=candidate.positions.size()-original.positions.size();
    answer.addedFaces=candidate.faces.size()-original.faces.size();
    answer.workspace=std::move(candidate);
    return answer;
}

} // namespace mesher
