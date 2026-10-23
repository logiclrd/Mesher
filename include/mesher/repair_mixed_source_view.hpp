#pragma once

#include "mesher/region_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {

// M14A: authoritative *read-through* access to the EXACT immutable R03 cut.
// Modified survivor records live ONLY as pending overrides. This intentionally
// does not commit or publish, and does not infer identity from equal XYZ.
// M14B will migrate the legacy flattened refinement arrays to this view.
class MixedClosureSourceView {
public:
    explicit MixedClosureSourceView(
        std::shared_ptr<const RegionTopologyIndex> source,
        std::size_t maxOverrides=1000000)
        : source_(std::move(source)),maxOverrides_(maxOverrides) {
        if(!source_||!maxOverrides_)
            throw std::invalid_argument("M14A requires an R03 source and finite edit budget");
    }

    const RegionTopologyIndex& source() const noexcept {return *source_;}
    std::shared_ptr<const RegionTopologyIndex> sourceRoot() const noexcept {
        return source_;
    }

    // Stable source FaceIds, including faces outside the initially tracked
    // contour halo. Only explicitly edited faces are represented locally.
    const RegionTopologyIndex::Face& face(FaceId id) const {
        const auto it=faceOverrides_.find(id);
        return it==faceOverrides_.end()?source_->face(id):it->second;
    }

    Vec3 position(VertexId id) const {
        const auto it=positionOverrides_.find(id);
        return it==positionOverrides_.end()?
            source_->vertex(id).position:it->second;
    }
    PartitionId owner(VertexId id) const {
        return source_->vertex(id).owner;
    }

    // Proposal only; the immutable source root is never mutated. This first
    // milestone supports existing source IDs; M14B introduces new faces and
    // transaction-local generated vertices into the same effective model.
    void overrideFace(FaceId id,std::array<VertexId,3> vertices) {
        const auto& baseline=source_->face(id);
        if(vertices[0]==vertices[1]||vertices[1]==vertices[2]||
           vertices[2]==vertices[0])
            throw std::logic_error("M14A refuses repeated face vertex IDs");
        for(const auto v:vertices)
            if(!source_->maybeVertex(v))
                throw std::logic_error("M14A face override references missing source vertex");
        if(!faceOverrides_.contains(id)&&
           faceOverrides_.size()+positionOverrides_.size()>=maxOverrides_)
            throw std::length_error("M14A finite survivor override budget exceeded");
        faceOverrides_.insert_or_assign(id,
            RegionTopologyIndex::Face{baseline.owner,vertices});
    }
    void overridePosition(VertexId id,Vec3 p) {
        (void)source_->vertex(id);
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            throw std::logic_error("M14A nonfinite survivor vertex override");
        if(!positionOverrides_.contains(id)&&
           faceOverrides_.size()+positionOverrides_.size()>=maxOverrides_)
            throw std::length_error("M14A finite survivor override budget exceeded");
        positionOverrides_.insert_or_assign(id,p);
    }

    // Effective incidence: pull unchanged original sides directly from the
    // immutable index, discard obsolete sides of edited original faces, and
    // add newly created incidences of edited original faces. No full-mesh
    // source face copy is necessary.
    std::vector<RegionTopologyIndex::EdgeSide> edgeSides(
        RegionTopologyIndex::Edge e) const {
        if(e.b<e.a)std::swap(e.a,e.b);
        std::vector<RegionTopologyIndex::EdgeSide> effective;
        const auto visit=[&](FaceId id) {
            const auto& v=face(id).vertices;
            for(unsigned k=0;k<3;++k) {
                const auto a=v[k],b=v[(k+1)%3];
                if((a==e.a&&b==e.b)||(a==e.b&&b==e.a)) {
                    effective.push_back({id,a==e.a});
                    return;
                }
            }
        };
        if(const auto old=source_->maybeEdgeSides(e))
            for(const auto& side:*old)visit(side.face);
        for(const auto& [id,changed]:faceOverrides_) {
            (void)changed;
            const auto old=source_->face(id).vertices;
            bool onOriginal=false;
            for(unsigned k=0;k<3;++k) {
                const auto a=old[k],b=old[(k+1)%3];
                if((a==e.a&&b==e.b)||(a==e.b&&b==e.a)) {
                    onOriginal=true;
                    break;
                }
            }
            if(!onOriginal)visit(id);
        }
        std::sort(effective.begin(),effective.end(),
            [](const auto& a,const auto& b) {
                return a.face<b.face;
            });
        return effective;
    }

    std::size_t pendingFaceEdits() const noexcept {
        return faceOverrides_.size();
    }
    std::size_t pendingVertexEdits() const noexcept {
        return positionOverrides_.size();
    }

private:
    std::shared_ptr<const RegionTopologyIndex> source_;
    std::size_t maxOverrides_;
    std::map<FaceId,RegionTopologyIndex::Face> faceOverrides_;
    std::map<VertexId,Vec3> positionOverrides_;
};

} // namespace mesher
