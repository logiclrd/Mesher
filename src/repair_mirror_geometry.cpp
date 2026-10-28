#include "mesher/repair_mirror_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Position=std::tuple<double,double,double>;
using Triple=std::array<VertexId,3>;
Position positionKey(Vec3 p) {
    // Canonicalize signed zero; no tolerance-based welding is ever allowed.
    return {p.x==0?0:p.x,p.y==0?0:p.y,p.z==0?0:p.z};
}
Triple ordered(Triple t) {
    std::sort(t.begin(),t.end());
    return t;
}
bool cyclic(Triple t,Triple expected) {
    for(unsigned shift=0;shift<3;++shift)
        if(t[0]==expected[shift]&&
           t[1]==expected[(shift+1)%3]&&
           t[2]==expected[(shift+2)%3])
            return true;
    return false;
}
}

GeometricMirrorEvidence auditPrivateReflectedGeometry(
    const RegionTopologyIndex& mesh,
    std::size_t maxVertices,std::size_t maxFaces) {
    if(!maxVertices||!maxFaces)
        throw std::invalid_argument(
            "R06-M06C mirror audit requires positive finite limits");
    GeometricMirrorEvidence out;
    std::map<Position,VertexId> at;
    std::map<VertexId,Vec3> position;
    std::map<Triple,Triple> faces;
    mesh.revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& v:part.vertices) {
            if(++out.vertices>maxVertices)
                throw std::length_error("R06-M06C mirrored vertex audit budget");
            const auto p=v.position;
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
                throw std::logic_error("R06-M06C nonfinite reflected vertex");
            if(!at.emplace(positionKey(p),v.id).second)
                throw std::logic_error(
                    "R06-M06C mirrored coordinates are ambiguous; independent coincident vertices must not be welded");
            position.emplace(v.id,p);
            if(p.x==0)++out.seamVertices;
        }
        for(const auto& f:part.faces) {
            if(++out.faces>maxFaces)
                throw std::length_error("R06-M06C mirrored face audit budget");
            const auto key=ordered(f.vertices);
            if(!faces.emplace(key,f.vertices).second)
                throw std::logic_error("R06-M06C duplicate mirror-audit triangle");
        }
    });
    std::map<VertexId,VertexId> partner;
    for(const auto& [id,p]:position) {
        const auto it=at.find(positionKey({-p.x,p.y,p.z}));
        if(it==at.end()) {
            // Diagnostic only: never use near matches to weld, snap, or
            // certify a reflected orbit. A failure on a live sculpt should
            // identify the first unmatched stable ID and its nearest
            // would-be counterpart, including sub-ulp discrepancies.
            const Vec3 wanted{-p.x,p.y,p.z};
            VertexId closestId{};
            double closestDistance=std::numeric_limits<double>::infinity();
            Vec3 closestPosition{};
            for(const auto& [other,q]:position) {
                const auto distance=std::hypot(
                    q.x-wanted.x,q.y-wanted.y,q.z-wanted.z);
                if(distance<closestDistance) {
                    closestDistance=distance;
                    closestId=other;
                    closestPosition=q;
                }
            }
            std::ostringstream reason;
            reason<<std::setprecision(17)
                  <<"R06-M06C private cap cannot preserve exact reflected vertex orbits"
                  <<" id="<<id
                  <<" xyz="<<p.x<<","<<p.y<<","<<p.z
                  <<" wanted="<<wanted.x<<","<<wanted.y<<","<<wanted.z
                  <<" nearest_id="<<closestId
                  <<" nearest_xyz="<<closestPosition.x<<","
                     <<closestPosition.y<<","<<closestPosition.z
                  <<" nearest_distance="<<closestDistance;
            throw std::logic_error(reason.str());
        }
        partner.emplace(id,it->second);
        if(p.x!=0)++out.pairedVertices;
        else if(it->second!=id)
            throw std::logic_error("R06-M06C seam vertex cannot have a distinct mirrored ID");
    }
    for(const auto& [id,mate]:partner)
        if(partner.at(mate)!=id)
            throw std::logic_error("R06-M06C mirrored partner IDs are noninvolutive");
    for(const auto& [key,t]:faces) {
        (void)key;
        const auto x0=position.at(t[0]).x;
        const auto x1=position.at(t[1]).x;
        const auto x2=position.at(t[2]).x;
        if(std::min({x0,x1,x2})<0&&std::max({x0,x1,x2})>0)
            throw std::logic_error(
                "R06-M06C reflected cap has a triangle crossing X=0 without a seam vertex");
        const Triple reflected{
            partner.at(t[0]),partner.at(t[2]),partner.at(t[1])};
        const auto found=faces.find(ordered(reflected));
        if(found==faces.end()||!cyclic(found->second,reflected))
            throw std::logic_error(
                "R06-M06C cap has no oppositely wound mirrored face");
        ++out.pairedFaces;
    }
    return out;
}
} // namespace mesher
