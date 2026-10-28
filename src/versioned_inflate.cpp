#include "mesher/versioned_inflate.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
bool finite(Vec3 value) {
    return std::isfinite(value.x)&&std::isfinite(value.y)&&
           std::isfinite(value.z);
}
void validateArea(const BrushArea& area) {
    if(!finite(area.center)||!std::isfinite(area.radius)||area.radius<=0||
       !std::isfinite(area.hardness)||area.hardness<0||area.hardness>1)
        throw std::invalid_argument("Invalid versioned Inflate brush area");
}
struct Movement {
    VertexId id{};
    Vec3 position{};
};
}

VersionedInflateBrush::VersionedInflateBrush(
    double strength,CurvatureSettings curvature,
    std::vector<std::pair<VertexId,VertexId>> mirrors)
    :strength_(strength),curvature_(curvature) {
    if(!std::isfinite(strength)||
       !std::isfinite(curvature.strength)||curvature.strength<0)
        throw std::invalid_argument("Invalid versioned Inflate parameters");
    for(const auto& pair:mirrors)addMirrorPair(pair.first,pair.second);
}

void VersionedInflateBrush::setStrengthScale(double scale) {
    if(!std::isfinite(scale)||scale==0)
        throw std::invalid_argument("Inflate strength scale must be finite and nonzero");
    strengthScale_=scale;
}

void VersionedInflateBrush::addMirrorPair(VertexId first,VertexId second) {
    if(first==0||second==0)
        throw std::invalid_argument("Mirror IDs must be nonzero");
    const auto a=mirrors_.find(first);
    const auto b=mirrors_.find(second);
    if((a!=mirrors_.end()&&a->second!=second)||
       (b!=mirrors_.end()&&b->second!=first))
        throw std::invalid_argument("Conflicting mirror correspondence");
    mirrors_.emplace(first,second);
    mirrors_.emplace(second,first);
}

void VersionedInflateBrush::removeMirrorPair(VertexId first,VertexId second) {
    const auto a=mirrors_.find(first),b=mirrors_.find(second);
    if(a==mirrors_.end()||b==mirrors_.end()||
       a->second!=second||b->second!=first)
        throw std::invalid_argument("Cannot retire a mismatched mirror pair");
    mirrors_.erase(first);
    if(second!=first)mirrors_.erase(second);
}

VersionedInflateStamp VersionedInflateBrush::stamp(
    std::shared_ptr<const RegionTopologyIndex> before,
    std::span<const BrushArea> areas,
    std::size_t maximumElementsPerPartition,double normalFraction) const {
    if(!before)
        throw std::invalid_argument("Inflate stamp needs an immutable revision");
    if(maximumElementsPerPartition==0)
        throw std::invalid_argument("Partition capacity must be positive");
    if(!std::isfinite(normalFraction)||normalFraction<=0||
       normalFraction>1)
        throw std::invalid_argument("V18 Inflate normal fraction must be finite and in (0,1]");
    VersionedInflateStamp result;
    result.index=before;
    result.baseline.before=before;
    std::vector<VertexId> candidates;
    for(const auto& area:areas) {
        validateArea(area);
        auto selected=before->querySphere(area.center,area.radius);
        candidates.insert(candidates.end(),selected.begin(),selected.end());
    }
    std::sort(candidates.begin(),candidates.end());
    candidates.erase(std::unique(candidates.begin(),candidates.end()),
                     candidates.end());
    result.candidates=candidates.size();
    const bool curvature=curvature_.enabled&&curvature_.strength>0;
    std::vector<Movement> movements;
    movements.reserve(candidates.size());

    // All vertices and incident faces are read from the *same* frozen
    // revision. This reproduces legacy Inflate's pre-stamp simultaneous
    // vertex normals and curvature, never a sequential in-place update.
    for(const auto id:candidates) {
        const auto& v=before->vertex(id);
        const double influence=brushInfluence(v.position,areas);
        if(influence<=0||v.incidentFaces.empty())continue;
        Vec3 sum{},cotangentSum{};
        double vertexArea=0,edgeLengthSum=0;
        std::uint32_t edgeCount=0;
        const Vec3 point=v.position;
        for(const auto faceId:v.incidentFaces) {
            const auto& tri=before->face(faceId).vertices;
            const Vec3 a=before->vertex(tri[0]).position;
            const Vec3 b=before->vertex(tri[1]).position;
            const Vec3 c=before->vertex(tri[2]).position;
            const Vec3 n=cross(b-a,c-a);
            const double twiceArea=length(n);
            if(twiceArea>1e-20)sum=sum+n/twiceArea;
            if(!curvature||twiceArea<1e-14)continue;
            const double cotA=std::clamp(dot(b-a,c-a)/twiceArea,-20.0,20.0);
            const double cotB=std::clamp(dot(a-b,c-b)/twiceArea,-20.0,20.0);
            const double cotC=std::clamp(dot(a-c,b-c)/twiceArea,-20.0,20.0);
            const double ab=length(b-a),bc=length(c-b),ca=length(a-c);
            if(id==tri[0]) {
                cotangentSum=cotangentSum+(b-a)*cotC+(c-a)*cotB;
                edgeLengthSum+=ab+ca;
            }else if(id==tri[1]) {
                cotangentSum=cotangentSum+(a-b)*cotC+(c-b)*cotA;
                edgeLengthSum+=ab+bc;
            }else {
                cotangentSum=cotangentSum+(a-c)*cotB+(b-c)*cotA;
                edgeLengthSum+=bc+ca;
            }
            vertexArea+=twiceArea/6.0;
            edgeCount+=2;
        }
        if(length(sum)<=1e-20)continue;
        const Vec3 direction=normalized(sum);
        double multiplier=1.0;
        if(curvature&&vertexArea>1e-14&&edgeCount>0) {
            const Vec3 laplacian=cotangentSum/(2*vertexArea);
            const double nominalLength=edgeLengthSum/edgeCount;
            const double signedCurvature=dot(laplacian,direction);
            multiplier=std::clamp(1.0+curvature_.strength*nominalLength*
                                  signedCurvature,0.10,3.0);
        }
        const Vec3 delta=direction*(strength_*strengthScale_*influence*multiplier*
                                      normalFraction);
        const Vec3 position=point+delta;
        if(!finite(position))
            throw std::overflow_error("Versioned Inflate displacement is not finite");
        movements.push_back({id,position});
    }
    if(movements.empty())return result;
    std::unordered_map<VertexId,Vec3> finalPositions;
    finalPositions.reserve(movements.size()*2);
    for(const auto& move:movements)finalPositions.emplace(move.id,move.position);

    if(!mirrors_.empty()) {
        // Choose the lower-ID member of each pair as the authoritative
        // calculation. Mirroring happens in the same region transaction.
        // The paired input is expected to supply symmetric brush areas.
        // Unlike the legacy per-half independent floating-point evaluation,
        // this enforces exact symmetry and clamps the shared X=0 seam.
        for(const auto& move:movements) {
            const auto found=mirrors_.find(move.id);
            if(found==mirrors_.end())
                throw std::logic_error("Influenced vertex has no mirror mapping");
        }
        for(const auto& move:movements) {
            const auto partner=mirrors_.at(move.id);
            if(partner==move.id) {
                // Small x floating point noise is projected back onto the seam.
                if(std::abs(move.position.x)>1e-9)
                    throw std::logic_error("Seam moved away from symmetry plane");
                finalPositions[move.id]={0,move.position.y,move.position.z};
                continue;
            }
            if(move.id>partner && finalPositions.contains(partner))continue;
            const auto reflected=Vec3{-move.position.x,move.position.y,
                                       move.position.z};
            finalPositions[partner]=reflected;
        }
    }

    std::vector<VertexId> seeds;
    seeds.reserve(finalPositions.size());
    for(const auto& [id,_]:finalPositions)seeds.push_back(id);
    std::sort(seeds.begin(),seeds.end());
    std::unordered_set<FaceId> affected;
    for(const auto id:seeds)
        for(const auto f:before->vertex(id).incidentFaces)affected.insert(f);
    result.baseline.movedVertices=seeds;
    result.baseline.affectedFaces.assign(affected.begin(),affected.end());
    std::sort(result.baseline.affectedFaces.begin(),
              result.baseline.affectedFaces.end());

    std::vector<std::pair<VertexId,VertexId>> activeMirrors;
    if(!mirrors_.empty()) {
        activeMirrors.reserve(seeds.size());
        for(const auto id:seeds) {
            const auto it=mirrors_.find(id);
            if(it==mirrors_.end())
                throw std::logic_error("Missing mirror for influenced vertex");
            activeMirrors.emplace_back(id,it->second);
        }
    }
    RegionEdit edit(before,seeds,std::move(activeMirrors),
                    maximumElementsPerPartition);
    for(const auto id:seeds) {
        if(!mirrors_.empty()&&id>mirrors_.at(id))continue;
        edit.moveVertex(id,finalPositions.at(id));
    }
    const auto commit=edit.commit();
    result.index=edit.updatedIndex();
    result.commitStatistics=commit.statistics;
    result.moved=seeds.size();
    return result;
}

} // namespace mesher
