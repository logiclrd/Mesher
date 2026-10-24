#pragma once
#include "mesher/repair_step_snapshot.hpp"
#include "mesher/partition_render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <vector>

namespace mesher {

// CPU-only M16D1: an intermediate open/private stage is NOT a valid
// publishable RegionTopologyIndex. Render effective source faces plus staged
// faces directly, without reusing stale R03 triangles under an override.
struct RepairStepScene {
    std::vector<PartitionRenderVertex> triangles;
    std::size_t survivorFaces=0,generatedFaces=0;
    std::size_t highlightedFaces=0,boundaryEdges=0;
    bool showingPrevious=false;
    bool warningOnly=false;
};
inline RepairStepScene buildRepairStepScene(
    const RepairStepGeometry& selected,
    const RepairStepGeometry* previous=nullptr,
    const RepairStepGeometry* current=nullptr,
    bool showPrevious=false,
    std::size_t maxFaces=200000) {
    if(!selected.source||!maxFaces)
        throw std::invalid_argument("R06-M16D1 missing source or finite face limit");
    RepairStepScene scene;
    scene.showingPrevious=showPrevious;
    scene.warningOnly=selected.arguments.find(
        "UNTRUSTED_QUALITY_WARNING_PREVIEW")!=std::string::npos;
    std::set<FaceId> modified;
    std::set<RepairStepFaceKey> changedGenerated;
    std::set<VertexId> moved;
    if(previous&&current) {
        const auto delta=diffRepairStepGeometry(*previous,*current);
        modified.insert(delta.modifiedSurvivorFaces.begin(),
                        delta.modifiedSurvivorFaces.end());
        const auto& changed=showPrevious?delta.removedFaces:delta.addedFaces;
        changedGenerated.insert(changed.begin(),changed.end());
        moved.insert(delta.movedVertices.begin(),delta.movedVertices.end());
        const auto& vertices=showPrevious?delta.removedVertices:delta.addedVertices;
        moved.insert(vertices.begin(),vertices.end());
    }
    auto addTriangle=[&](PrivateRepairTriangle v,float material) {
        const Vec3 a=selected.position(v[0]);
        const Vec3 b=selected.position(v[1]);
        const Vec3 c=selected.position(v[2]);
        const auto n=cross(b-a,c-a);
        const auto magnitude=length(n);
        const Vec3 normal=magnitude>1e-20?n/magnitude:Vec3{};
        for(const auto p:{a,b,c})
            scene.triangles.push_back({float(p.x),float(p.y),float(p.z),
                 float(normal.x),float(normal.y),float(normal.z),material});
    };
    std::size_t total=selected.generatedFaces.size();
    if(total>maxFaces)throw std::length_error(
        "R06-M16D1 generated-face limit");
    selected.source->revision().forEachPartition([&](
        PartitionId,const MeshPartition& partition) {
        if(partition.faces.size()>maxFaces-total)
            throw std::length_error("R06-M16D1 finite effective face limit");
        total+=partition.faces.size();
    });
    scene.triangles.reserve(total*3+selected.boundaryLoops.size()*36);
    selected.source->revision().forEachPartition([&](
        PartitionId,const MeshPartition& partition) {
        for(const auto& f:partition.faces) {
            const auto v=selected.effectiveSourceFace(f.id);
            const bool highlight=modified.contains(f.id)||
                std::any_of(v.begin(),v.end(),[&](VertexId id){
                    return moved.contains(id);
                });
            addTriangle(v,highlight?(showPrevious?14.0f:15.0f):0.0f);
            scene.highlightedFaces+=highlight?1:0;
            ++scene.survivorFaces;
        }
    });
    for(const auto& face:selected.generatedFaces) {
        const bool highlight=changedGenerated.contains(repairStepFaceKey(face))||
            std::any_of(face.vertices.begin(),face.vertices.end(),
                [&](VertexId id){return moved.contains(id);});
        const float material=highlight?(showPrevious?14.0f:15.0f):
            face.kind==MixedClosureFaceKind::Cap?9.0f:
            face.kind==MixedClosureFaceKind::Zipper?8.0f:0.0f;
        addTriangle(face.vertices,material);
        scene.highlightedFaces+=highlight?1:0;
        ++scene.generatedFaces;
    }
    // Geometric boundary rods are purely diagnostic and never form part of
    // the staged topology; all endpoint positions come from THIS frame.
    const auto rod=[&](VertexId va,VertexId vb) {
        const Vec3 a=selected.position(va),b=selected.position(vb);
        const auto axis=b-a;
        if(length(axis)<=1e-15)return;
        const Vec3 forward=normalized(axis);
        const Vec3 ref=std::abs(forward.z)<.9?Vec3{0,0,1}:Vec3{0,1,0};
        const Vec3 u=normalized(cross(forward,ref))*.00035;
        const Vec3 v=normalized(cross(forward,u))*.00035;
        const std::array<Vec3,4> ring{{u,v,u*-1.,v*-1.}};
        const auto push=[&](Vec3 p) {
            scene.triangles.push_back({float(p.x),float(p.y),float(p.z),
                                       0,0,1,6.0f});
        };
        for(std::size_t i=0;i<4;++i) {
            const auto j=(i+1)%4;
            push(a+ring[i]);push(a+ring[j]);push(b+ring[i]);
            push(b+ring[i]);push(a+ring[j]);push(b+ring[j]);
        }
        ++scene.boundaryEdges;
    };
    if(selected.boundaryDiagnostic.empty())
        for(const auto& loop:selected.boundaryLoops)
            for(std::size_t i=0;i<loop.size();++i)
                rod(loop[i],loop[(i+1)%loop.size()]);
    return scene;
}
} // namespace mesher
