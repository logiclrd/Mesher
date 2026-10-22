#include "mesher/repair_inspection.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
RegionTopologyIndex::Edge canonical(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
PartitionRenderVertex marker(Vec3 p,float material) {
    return {float(p.x),float(p.y),float(p.z),0,0,1,material};
}
void triangle(std::vector<PartitionRenderVertex>& out,
              Vec3 a,Vec3 b,Vec3 c,float material) {
    out.push_back(marker(a,material));
    out.push_back(marker(b,material));
    out.push_back(marker(c,material));
}
void dot(std::vector<PartitionRenderVertex>& out,Vec3 c,double r,float material=2.0f) {
    const std::array<Vec3,6> points{{
        c+Vec3{r,0,0},c+Vec3{-r,0,0},
        c+Vec3{0,r,0},c+Vec3{0,-r,0},
        c+Vec3{0,0,r},c+Vec3{0,0,-r}
    }};
    for(unsigned i=0;i<4;++i) {
        const unsigned j=(i+1)%4;
        const auto a=points[i==0?0:i==1?2:i==2?1:3];
        const auto b=points[j==0?0:j==1?2:j==2?1:3];
        triangle(out,points[4],a,b,material);
        triangle(out,points[5],b,a,material);
    }
}
void rod(std::vector<PartitionRenderVertex>& out,Vec3 a,Vec3 b,double radius,float material=3.0f) {
    const auto delta=b-a;
    if(length(delta)<=1e-15)return;
    const auto axis=normalized(delta);
    const auto ref=std::abs(axis.z)<0.9?Vec3{0,0,1}:Vec3{0,1,0};
    const auto u=normalized(cross(axis,ref))*radius;
    const auto v=normalized(cross(axis,u))*radius;
    const std::array<Vec3,4> ring{{u,v,u*-1.0,v*-1.0}};
    for(unsigned i=0;i<4;++i) {
        const auto j=(i+1)%4;
        triangle(out,a+ring[i],a+ring[j],b+ring[i],material);
        triangle(out,b+ring[i],a+ring[j],b+ring[j],material);
    }
}
}

std::string RepairInspectionSession::inspect(const ProposedSculptStep& proposed) {
    if(frozen())return "R01 diagnostic view is frozen; relaunch to sculpt again";
    if(!proposed.candidate||!proposed.before)
        return "R01 diagnostic requires valid immutable before/candidate roots";
    if(proposed.candidate==proposed.before)return {};
    auto result=std::make_shared<RepairInspectionSnapshot>();
    result->candidate=proposed.candidate;
    const std::vector<VertexId> vertices(
        proposed.changedVertices.begin(),proposed.changedVertices.end());
    const std::vector<FaceId> faces(
        proposed.changedFaces.begin(),proposed.changedFaces.end());
    // Finite speculative work, NEVER a partial collision list.
    RegionTopologyIndex::CollisionQueryLimits limits;
    limits.maxSeedFaces=16384;
    // Each changed face performs its OWN traversal of the persistent
    // partition directory. A normal 5000-vertex symmetric sphere stamp
    // can seed ~900 triangles, exceeding 131072 *directory* visits
    // even when only a few thousand pairs reach narrow phase.
    // Keep a hard limit, but size it for the actual two-level indexed
    // query rather than confusing directory work with contact volume.
    // R01 is an explicitly opt-in debug inspector, not a real-time
    // collision solver. Candidate and baseline both use these limits.
    limits.maxBoxTests=4194304;
    limits.maxPartitionTests=1048576;
    limits.maxCandidateTriangles=524288;
    limits.maxNarrowPhaseTests=262144;
    limits.maxReportedContacts=512; // diagnostic overlay memory is deliberately finite
    if(vertices.size()>65536||faces.size()>65536)
        result->diagnostic="R01 changed-ID input budget exhausted";
    else {
        try {
            result->contacts=proposed.candidate->triangleIntersections(
                vertices,faces,&result->work,limits);
        }catch(const std::length_error& e) {
            result->diagnostic=std::string("R01 collision query budget exhausted: ")+e.what();
        }
    }
    // R01 should freeze on NEW changed-neighborhood evidence, not on a
    // historically imperfect but still editable mesh. Compare the same
    // supported finite query on the previous immutable root. Restrict
    // sources to IDs that actually exist there: new refinement vertices
    // and faces have no historical counterpart. This is face-pair
    // provenance only; R07 will introduce geometric worsening metrics.
    if(!result->contacts.empty()&&result->diagnostic.empty()) {
        std::vector<VertexId> oldVertices;
        std::vector<FaceId> oldFaces;
        oldVertices.reserve(vertices.size());
        oldFaces.reserve(faces.size());
        for(const auto id:vertices)
            if(proposed.before->maybeVertex(id))oldVertices.push_back(id);
        for(const auto id:faces)
            if(proposed.before->maybeFace(id))oldFaces.push_back(id);
        try {
            const auto previous=proposed.before->triangleIntersections(
                oldVertices,oldFaces,&result->baselineWork,limits);
            std::set<std::pair<FaceId,FaceId>> existing;
            for(const auto& p:previous)
                if(!p.ambiguous)existing.emplace(p.first,p.second);
            std::erase_if(result->contacts,[&](const auto& p) {
                if(existing.contains({p.first,p.second})) {
                    ++result->preexistingPairs;
                    return true;
                }
                return false;
            });
        }catch(const std::length_error& e) {
            result->diagnostic=std::string("R01 baseline collision query budget exhausted: ")+e.what();
            // No partial result may be presented as newly introduced:
            // the query's evidential comparison did not complete.
            result->contacts.clear();
        }
    }
    if(result->contacts.empty()&&result->diagnostic.empty())return {};
    std::set<FaceId> markedFaces;
    std::set<VertexId> markedVertices;
    std::set<RegionTopologyIndex::Edge> markedEdges;
    for(const auto& contact:result->contacts) {
        markedFaces.insert(contact.first);
        markedFaces.insert(contact.second);
    }
    for(const auto face:markedFaces) {
        const auto f=proposed.candidate->maybeFace(face);
        if(!f)throw std::logic_error("R01 contact references missing candidate FaceId");
        for(unsigned i=0;i<3;++i) {
            markedVertices.insert(f->vertices[i]);
            markedEdges.insert(canonical(f->vertices[i],f->vertices[(i+1)%3]));
        }
    }
    result->affectedFaces.assign(markedFaces.begin(),markedFaces.end());
    result->incidentVertices.assign(markedVertices.begin(),markedVertices.end());
    result->incidentEdges.assign(markedEdges.begin(),markedEdges.end());
    result->previewEnabled=previewEnabled_;
    result->excisionEnabled=excisionEnabled_;
    result->zipperEnabled=zipperEnabled_;
    result->capEnabled=capEnabled_;
    result->autoEnabled=autoEnabled_;
    if(previewEnabled_&&!result->contacts.empty()) {
        try {
            result->excision=previewApproximateExcision(
                *proposed.candidate,result->contacts);
        }catch(const std::exception& e) {
            result->excision.complete=false;
            result->excision.diagnostic=std::string("R02 preview could not complete: ")+e.what();
        }
    }
    if(excisionEnabled_) {
        if(!result->diagnostic.empty()||!result->excision.complete) {
            // Never lose the actual cause in a generic refusal banner.
            const auto reason=!result->diagnostic.empty()?result->diagnostic:
                (!result->excision.diagnostic.empty()?result->excision.diagnostic:
                 "R02 cut plan was not certified complete");
            result->excisionDiagnostic="R03 refuses incomplete contact or R02 cut plan: "+reason;
        }
        else {
            try {
                result->privateCut=applyPrivateExcision(
                    proposed.candidate,result->excision);
            }catch(const std::exception& e) {
                result->excisionDiagnostic=std::string("R03 private cut refused: ")+e.what();
            }
        }
    }
    if(zipperEnabled_) {
        if(!result->privateCut) {
            result->zipperDiagnostic="R04 requires a successful R03 private excision: "+
                result->excisionDiagnostic;
        }else {
            try {
                result->privateZipper=applyForcedPrivateZipper(
                    *result->privateCut,result->excision);
            }catch(const std::exception& e) {
                result->zipperDiagnostic=std::string("R04 forced zipper refused: ")+e.what();
            }
        }
    }
    if(capEnabled_) {
        if(!result->privateCut)
            result->capDiagnostic="R05 requires complete R03 excision: "+
                result->excisionDiagnostic;
        else {
            try {
                result->privateCap=applyForcedPrivateCap(
                    *result->privateCut,result->excision);
            }catch(const std::exception& e) {
                result->capDiagnostic=std::string("R05 forced cap refused: ")+e.what();
            }
        }
    }
    if(autoEnabled_) {
        if(!result->privateCut) {
            result->autoDiagnostic="R06 requires successful complete R03 excision: "+
                result->excisionDiagnostic;
        }else {
            try {
                result->autoPlan=planPrivateAutomaticClosure(
                    *proposed.candidate,*result->privateCut,
                    result->excision,result->contacts);
                if(!result->autoPlan->ready())
                    result->autoDiagnostic="R06 automatic closure plan refused: "+
                        result->autoPlan->diagnostic;
                else {
                    CertifiedPrivateAutoLimits policy;
                    policy.closure.requireReflectedGeometry=symmetricSource_;
                    result->privateAuto=applyCertifiedPrivateAutomaticClosure(
                        proposed.candidate,*result->privateCut,
                        result->excision,*result->autoPlan,result->contacts,
                        policy);
                }
            }catch(const std::exception& e) {
                result->autoDiagnostic=std::string(
                    "R06 private automatic closure refused: ")+e.what();
            }
        }
    }
    {
        std::lock_guard lock(mutex_);
        if(frozen_)return "R01 diagnostic view is frozen; relaunch to sculpt again";
        snapshot_=std::move(result);
        frozen_.store(true,std::memory_order_release);
    }
    return "R01 diagnostic intersection detected; private candidate retained for read-only inspection";
}

std::shared_ptr<const RepairInspectionSnapshot>
RepairInspectionSession::frozenSnapshot() const {
    if(!frozen())return {};
    std::lock_guard lock(mutex_);
    return snapshot_;
}

std::vector<PartitionRenderVertex> buildRepairInspectionOverlay(
    const RepairInspectionSnapshot& snapshot) {
    std::vector<PartitionRenderVertex> overlay;
    if(!snapshot.candidate)return overlay;
    if(snapshot.autoEnabled&&snapshot.privateAuto) {
        // Draw only actual surviving new faces from M04's FINAL closed
        // private root. An inner-cavity face retired by M04C is invisible:
        // there are no diagnostic ghost caps or invented seam triangles.
        const auto& closed=*snapshot.privateAuto->index;
        const auto draw=[&](const std::vector<FaceId>& ids,float material) {
            for(const auto id:ids) {
                const auto& face=closed.face(id).vertices;
                const auto a=closed.vertex(face[0]).position;
                const auto b=closed.vertex(face[1]).position;
                const auto c=closed.vertex(face[2]).position;
                const auto n=cross(b-a,c-a);
                const auto norm=length(n);
                if(norm<=1e-15)continue;
                const auto offset=n/norm*
                    (.003*std::min({length(a-b),length(b-c),length(c-a)}));
                triangle(overlay,a+offset,b+offset,c+offset,material);
                triangle(overlay,c-offset,b-offset,a-offset,material);
            }
        };
        draw(snapshot.privateAuto->greenFaces,9.0f);
        draw(snapshot.privateAuto->blueFaces,8.0f);
        return overlay;
    }
    if(snapshot.capEnabled&&snapshot.privateCap) {
        // The real new faces are in the private COW mesh, highlighted from
        // BOTH sides. There must be no green surface left over in a hole
        // when the private cap refused.
        const auto& capped=*snapshot.privateCap->index;
        for(const auto id:snapshot.privateCap->greenFaces) {
            const auto& f=capped.face(id).vertices;
            const auto a=capped.vertex(f[0]).position;
            const auto b=capped.vertex(f[1]).position;
            const auto c=capped.vertex(f[2]).position;
            const auto n=cross(b-a,c-a);
            if(length(n)<=1e-15)continue;
            const auto offset=n/length(n)*
                (.003*std::min({length(a-b),length(b-c),length(c-a)}));
            triangle(overlay,a+offset,b+offset,c+offset,9.0f);
            triangle(overlay,c-offset,b-offset,a-offset,9.0f);
        }
        return overlay;
    }
    if(snapshot.zipperEnabled&&snapshot.privateZipper) {
        // Actual scar triangles are in the private mesh. Draw their blue
        // surfaces from BOTH sides, with small opposed normal offsets;
        // unlike R02's one-sided orange overlay, an interior camera
        // can inspect every produced triangle.
        const auto& zipped=*snapshot.privateZipper->index;
        for(const auto id:snapshot.privateZipper->blueFaces) {
            const auto& tri=zipped.face(id).vertices;
            const auto a=zipped.vertex(tri[0]).position;
            const auto b=zipped.vertex(tri[1]).position;
            const auto c=zipped.vertex(tri[2]).position;
            const auto crossNormal=cross(b-a,c-a);
            const auto norm=length(crossNormal);
            if(norm<=1e-15)continue;
            const auto scale=.003*std::min({length(a-b),length(b-c),length(c-a)});
            const auto offset=crossNormal/norm*scale;
            triangle(overlay,a+offset,b+offset,c+offset,8.0f);
            triangle(overlay,c-offset,b-offset,a-offset,8.0f);
        }
        return overlay;
    }
    if(snapshot.excisionEnabled&&snapshot.privateCut) {
        // Only real SURVIVING boundary rims are shown on the privately
        // cut mesh. Removed faces/vertices must not reappear as red/orange
        // "ghosts", which would obscure the actual holes.
        for(const auto& loop:snapshot.excision.contours) {
            for(std::size_t i=0;i<loop.vertexIds.size();++i) {
                const auto a=loop.vertexIds[i];
                const auto b=loop.vertexIds[(i+1)%loop.vertexIds.size()];
                rod(overlay,snapshot.privateCut->index->vertex(a).position,
                    snapshot.privateCut->index->vertex(b).position,.0025,6.0f);
            }
        }
        return overlay;
    }
    // Real incident edge lengths keep markers visible on both coarse
    // fixture geometry and ordinary higher-density sphere meshes.
    std::map<VertexId,double> radius;
    for(const auto vertex:snapshot.incidentVertices)
        radius.emplace(vertex,std::numeric_limits<double>::infinity());
    for(const auto edge:snapshot.incidentEdges) {
        const auto a=snapshot.candidate->vertex(edge.a).position;
        const auto b=snapshot.candidate->vertex(edge.b).position;
        const double distance=length(b-a);
        if(auto it=radius.find(edge.a);it!=radius.end())
            it->second=std::min(it->second,distance);
        if(auto it=radius.find(edge.b);it!=radius.end())
            it->second=std::min(it->second,distance);
    }
    const auto markerRadius=[&](VertexId id) {
        const auto it=radius.find(id);
        if(it==radius.end()||!std::isfinite(it->second))return .005;
        return std::clamp(it->second*.10,.004,.045);
    };
    overlay.reserve(snapshot.incidentVertices.size()*24+
                    snapshot.incidentEdges.size()*24);
    for(const auto edge:snapshot.incidentEdges)
        rod(overlay,snapshot.candidate->vertex(edge.a).position,
            snapshot.candidate->vertex(edge.b).position,
            std::min(markerRadius(edge.a),markerRadius(edge.b))*.34);
    for(const auto id:snapshot.incidentVertices)
        dot(overlay,snapshot.candidate->vertex(id).position,markerRadius(id));
    if(snapshot.previewEnabled&&!snapshot.excisionEnabled) {
        // The face inset exposes underlying wireframe and makes the
        // diagnostic region distinguishable from the red R01 contact halo.
        const auto highlight=[&](FaceId id,float material) {
            const auto& f=snapshot.candidate->face(id).vertices;
            const auto a=snapshot.candidate->vertex(f[0]).position;
            const auto b=snapshot.candidate->vertex(f[1]).position;
            const auto c=snapshot.candidate->vertex(f[2]).position;
            const auto center=(a+b+c)/3.0;
            const auto normal=cross(b-a,c-a);
            const auto scale=length(normal);
            const auto offset=scale>0?
                normal/scale*(.003*std::min({length(a-b),length(b-c),length(c-a)})):
                Vec3{};
            triangle(overlay,center+(a-center)*.89+offset,
                             center+(b-center)*.89+offset,
                             center+(c-center)*.89+offset,material);
        };
        for(const auto id:snapshot.excision.proposedFaces)highlight(id,4.0f);
        for(const auto id:snapshot.excision.uncertainFaces)highlight(id,5.0f);
        // Cyan dots mark vertices absorbed to repair branching cut rims.
        for(const auto id:snapshot.excision.expandedBoundaryVertices)
            dot(overlay,snapshot.candidate->vertex(id).position,.008,7.0f);
        for(const auto& loop:snapshot.excision.contours) {
            for(std::size_t i=0;i<loop.vertexIds.size();++i) {
                const auto a=loop.vertexIds[i];
                const auto b=loop.vertexIds[(i+1)%loop.vertexIds.size()];
                rod(overlay,snapshot.candidate->vertex(a).position,
                    snapshot.candidate->vertex(b).position,
                    std::max(.0025,std::min(markerRadius(a),markerRadius(b))*.53),6.0f);
            }
        }
    }
    return overlay;
}

} // namespace mesher
