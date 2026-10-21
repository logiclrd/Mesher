#include "mesher/offmode_mirror.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
using Corners=std::array<VertexId,3>;
Corners key(Corners c) {
    std::sort(c.begin(),c.end());
    return c;
}
bool cyclic(Corners a,Corners b) {
    for(unsigned shift=0;shift<3;++shift)
        if(a[0]==b[shift]&&a[1]==b[(shift+1)%3]&&
           a[2]==b[(shift+2)%3])return true;
    return false;
}
}

OffModeMirrorAudit auditOffModeMirrors(
    const std::shared_ptr<const RegionTopologyIndex>& root,
    const std::unordered_map<VertexId,VertexId>& mirrors,
    std::span<const VertexId> changedVertices,
    std::span<const FaceId> changedFaces,
    bool fullSource,
    const OffModeMirrorAuditLimits& limits) {
    OffModeMirrorAudit result;
    const auto fail=[&](std::string reason) {
        result.complete=false;
        result.refusal=std::move(reason);
        return result;
    };
    if(!root||mirrors.empty())
        return fail("V18 symmetric source requires explicit indexed root and reciprocal stable-ID mirror ownership");
    std::set<VertexId> vertices;
    std::set<FaceId> faces;
    const auto addVertex=[&](VertexId v) {
        if(!root->maybeVertex(v))return;
        if(!vertices.contains(v)&&vertices.size()>=limits.maxVertices)
            throw std::length_error("V18 mirrored vertex work budget exhausted");
        vertices.insert(v);
    };
    const auto addFace=[&](FaceId f) {
        if(!root->maybeFace(f))return;
        if(!faces.contains(f)&&faces.size()>=limits.maxFaces)
            throw std::length_error("V18 mirrored face work budget exhausted");
        faces.insert(f);
    };
    try {
        if(fullSource) {
            root->revision().forEachPartition([&](PartitionId,const MeshPartition& p) {
                for(const auto& [id,v]:p.vertices) {
                    (void)v;
                    addVertex(id);
                }
                for(const auto& [id,f]:p.faces) {
                    (void)f;
                    addFace(id);
                }
            });
            if(vertices.size()!=mirrors.size())
                return fail("V18 symmetric original mirror map is incomplete or includes retired sources");
        }else{
            for(const auto v:changedVertices)addVertex(v);
            for(const auto f:changedFaces) {
                addFace(f);
                const auto face=root->maybeFace(f);
                if(face)for(const auto v:face->vertices)addVertex(v);
            }
        }
        // Follow only *named* mirror ownership; never infer a mate from
        // equality of floating-point XYZ coordinates.
        const auto scope=vertices;
        for(const auto v:scope) {
            const auto it=mirrors.find(v);
            if(it==mirrors.end())
                return fail("V18 changed source vertex lacks explicit mirror ID");
            addVertex(it->second);
        }
        for(const auto v:vertices) {
            const auto mate=mirrors.find(v);
            if(mate==mirrors.end()||!root->maybeVertex(mate->second)||
               !mirrors.contains(mate->second)||
               mirrors.at(mate->second)!=v)
                return fail("V18 symmetric vertex orbit missing reciprocal live ID");
            const auto p=root->vertex(v).position;
            const auto q=root->vertex(mate->second).position;
            if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
               ((v==mate->second)!=(p.x==0)))
                return fail("V18 symmetric stable-ID pair is not exact reflected geometry or X=0 fixed point");
            if(v==mate->second)++result.fixedPlaneVertices;
            ++result.verticesChecked;
            // Every surviving incident face of both members belongs to
            // this source-local halo, even when a face ID was replaced.
            for(const auto f:root->vertex(v).incidentFaces)addFace(f);
        }
        std::map<Corners,FaceId> byCorners;
        for(const auto f:faces) {
            const auto corners=root->face(f).vertices;
            const auto [it,inserted]=byCorners.emplace(key(corners),f);
            (void)it;
            if(!inserted)
                return fail("V18 mirror face scope contains duplicate stable topology");
        }
        for(const auto f:faces) {
            const auto original=root->face(f).vertices;
            Corners reflected{};
            for(unsigned i=0;i<3;++i) {
                const auto it=mirrors.find(original[i]);
                if(it==mirrors.end())
                    return fail("V18 mirrored face has unknown stable corner mapping");
                reflected[i]=it->second;
            }
            const auto other=byCorners.find(key(reflected));
            if(other==byCorners.end())
                return fail("V18 mirrored source face lacks reflected stable-corner counterpart");
            const auto ordered=root->face(other->second).vertices;
            if(!cyclic(ordered,{reflected[0],reflected[2],reflected[1]}))
                return fail("V18 mirrored source face has incorrect reflected orientation");
            ++result.facesChecked;
        }
    }catch(const std::length_error& e) {
        return fail(e.what());
    }catch(const std::logic_error& e) {
        return fail(e.what());
    }catch(const std::out_of_range& e) {
        return fail(e.what());
    }
    result.complete=true;
    return result;
}

} // namespace mesher
