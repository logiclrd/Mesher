#pragma once

#include "mesher/repair_mixed_stage.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {

// M14E: explicit, complete face/vertex patch ledger. The cap/zipper
// generator may construct nonmanifold temporary triangles, but every
// original survivor modification MUST be present before consolidation.
// Stable source IDs are never inferred from XYZ.
struct MixedClosurePatchFace {
    std::size_t ordinal{};
    FaceId original{};
    PartitionId owner{};
    PrivateRepairTriangle vertices{};
    MixedClosureFaceKind kind{MixedClosureFaceKind::Survivor};
};
struct MixedClosurePatchVertex {
    VertexId id{};
    PartitionId owner{};
    Vec3 position{};
};
struct MixedClosurePatch {
    std::shared_ptr<const RegionTopologyIndex> sourceR03;
    std::vector<MixedClosurePatchFace> faceWrites;
    std::vector<MixedClosurePatchVertex> vertexWrites;
    // Reserved/staged new VertexIds have already been allocated in this
    // private COW view during finite subdivision. Include them in the
    // ledger and certify exact owner/position before face materialization.
    std::vector<MixedClosurePatchVertex> generatedVertices;
    std::size_t newFaces{};
    std::size_t changedSurvivors{};
};
struct AppliedMixedClosurePatch {
    // Original FaceId remains stable; each new face gets ONE allocated ID
    // in the same ordinal as its original proposal (preserves color labels).
    std::vector<std::pair<std::size_t,FaceId>> createdFaces;
    std::vector<PartitionId> touchedPartitions;
};

// A read-only preflight of the ENTIRE set of source overrides and new
// triangles. No COW mutation can occur until all requested existing-face
// edits have been reconciled against the source-backed working set.
inline MixedClosurePatch planPrivateMixedClosurePatch(
    const MixedClosureStaging& stage,std::size_t maxTrackedFaces) {
    if(!stage.sourceView||!maxTrackedFaces||
       stage.faces.size()>maxTrackedFaces)
        throw std::length_error("R06-M14E invalid or oversized staged patch");
    MixedClosurePatch patch;
    patch.sourceR03=stage.sourceView->sourceRoot();
    std::set<FaceId> observed;
    for(std::size_t i=0;i<stage.faces.size();++i) {
        const auto& face=stage.faces[i];
        const auto triangle=face.triangle();
        if(triangle[0]==triangle[1]||triangle[1]==triangle[2]||
           triangle[0]==triangle[2])
            throw std::logic_error("R06-M14E invalid repeated triangle vertex in patch");
        for(const auto vertex:triangle)
            if(!stage.positions.contains(vertex))
                throw std::logic_error("R06-M14E patch face references unregistered vertex");
        if(face.stableId) {
            if(face.sourceView!=stage.sourceView||
               face.kind!=MixedClosureFaceKind::Survivor||
               !observed.insert(face.stableId).second)
                throw std::logic_error("R06-M14E duplicate/foreign original face handle");
            const auto& source=patch.sourceR03->face(face.stableId);
            if(source.owner!=face.owner)
                throw std::logic_error("R06-M14E source survivor owner changed");
            if(!stage.sourceView->hasFaceOverride(face.stableId))continue;
            ++patch.changedSurvivors;
        }else if(face.sourceView)
            throw std::logic_error("R06-M14E a new face must not bind a source FaceId handle");
        else ++patch.newFaces;
        patch.faceWrites.push_back({i,face.stableId,face.owner,
                                    triangle,face.kind});
    }
    // Catch an override of an original FaceId omitted from the patch's
    // survivor worklist. Never silently accept an incomplete proposal.
    if(stage.sourceView->pendingFaceEdits()!=patch.changedSurvivors)
        throw std::logic_error("R06-M14E incomplete patch: original survivor override omitted");
    for(const auto id:stage.sourceView->editedPositionIds()) {
        const auto original=patch.sourceR03->vertex(id);
        patch.vertexWrites.push_back({id,original.owner,
                                      stage.sourceView->position(id)});
    }
    for(const auto& [id,position]:stage.positions) {
        if(patch.sourceR03->maybeVertex(id))continue;
        const auto owner=stage.vertexOwners.find(id);
        if(owner==stage.vertexOwners.end())
            throw std::logic_error("R06-M14E generated vertex owner omitted from patch");
        patch.generatedVertices.push_back({id,owner->second,position});
    }
    return patch;
}

// Called on a PRIVATE COW view of sourceR03. Must not run on a publisher
// or renderer. The caller retains one final closed-manifold validation of
// the committed and reconstructed complete candidate BEFORE publication.
inline AppliedMixedClosurePatch applyPrivateMixedClosurePatch(
    MutableMeshView& view,const RegionTopologyIndex& expectedSource,
    const MixedClosurePatch& patch,std::size_t maxNewFaces) {
    if(!patch.sourceR03||patch.sourceR03.get()!=&expectedSource)
        throw std::logic_error("R06-M14E patch source does not match private COW base");
    if(patch.newFaces>maxNewFaces)
        throw std::length_error("R06-M14E finite new face patch budget exceeded");
    // An exported ledger is not an authority to bypass the staging
    // contract. Verify full write counts and unique source IDs before
    // touching the private copy-on-write view.
    std::set<FaceId> originalWrites;
    std::size_t sourceEdits=0,newWrites=0;
    for(const auto& f:patch.faceWrites) {
        if(f.original) {
            ++sourceEdits;
            if(!originalWrites.insert(f.original).second)
                throw std::logic_error("R06-M15C3 duplicate original FaceId in patch");
        }else ++newWrites;
    }
    if(sourceEdits!=patch.changedSurvivors||
       newWrites!=patch.newFaces)
        throw std::logic_error("R06-M15C3 incomplete or inconsistent exported patch ledger");
    AppliedMixedClosurePatch applied;
    std::set<PartitionId> owners;
    for(const auto& created:patch.generatedVertices) {
        if(expectedSource.maybeVertex(created.id))
            throw std::logic_error("R06-M14E generated vertex overlaps original source ID");
        const auto& part=view.editPartition(created.owner);
        const auto it=std::find_if(part.vertices.begin(),part.vertices.end(),
            [&](const PartitionVertex& v){return v.id==created.id;});
        if(it==part.vertices.end())
            throw std::logic_error("R06-M14E missing generated vertex in private COW view");
        const auto& p=it->position;
        if(p.x!=created.position.x||p.y!=created.position.y||
           p.z!=created.position.z)
            throw std::logic_error("R06-M14E generated vertex position disagrees with patch");
        owners.insert(created.owner);
    }
    for(const auto& edit:patch.vertexWrites) {
        const auto& original=expectedSource.vertex(edit.id);
        if(original.owner!=edit.owner)
            throw std::logic_error("R06-M14E source vertex owner mismatch");
        auto& part=view.editPartition(edit.owner);
        const auto it=std::find_if(part.vertices.begin(),part.vertices.end(),
            [&](const PartitionVertex& v){return v.id==edit.id;});
        if(it==part.vertices.end())
            throw std::logic_error("R06-M14E source vertex not present in private copy");
        it->position=edit.position;
        owners.insert(edit.owner);
    }
    for(const auto& face:patch.faceWrites) {
        owners.insert(face.owner);
        if(face.original) {
            const auto& original=expectedSource.face(face.original);
            if(original.owner!=face.owner)
                throw std::logic_error("R06-M14E source face owner mismatch");
            auto& part=view.editPartition(face.owner);
            const auto it=std::find_if(part.faces.begin(),part.faces.end(),
                [&](const PartitionFace& f){return f.id==face.original;});
            if(it==part.faces.end())
                throw std::logic_error("R06-M14E source face absent from private copy");
            it->vertices=face.vertices;
        }else {
            const auto id=view.addFace(face.owner,face.vertices);
            applied.createdFaces.emplace_back(face.ordinal,id);
        }
    }
    applied.touchedPartitions.assign(owners.begin(),owners.end());
    return applied;
}

} // namespace mesher
