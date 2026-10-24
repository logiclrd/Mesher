#pragma once

#include "mesher/repair_mixed_stage.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mesher {

// An owned M16C2b diagnostic packet. The complete R03 index remains an
// immutable shared root: only modified original faces, changed original
// positions, generated vertices/faces and current boundary are copied.
// An unpublished/incomplete stage is NEVER a RegionTopologyIndex or a
// revision that the ordinary sculpt worker could publish.
struct RepairStepGeneratedFace {
    PrivateRepairTriangle vertices{};
    MixedClosureFaceKind kind{MixedClosureFaceKind::Survivor};
    PartitionId owner{};
    std::size_t stageOrdinal{};
};
struct RepairStepFaceKey {
    MixedClosureFaceKind kind{MixedClosureFaceKind::Survivor};
    PartitionId owner{};
    PrivateRepairTriangle oriented{};

    // Rotations are equivalent, reverse winding is NOT equivalent. New
    // staged faces have no permanent FaceId until patch materialization;
    // their oriented ID triple is the unambiguous geometry-diff key.
    friend bool operator<(const RepairStepFaceKey& a,
                          const RepairStepFaceKey& b) {
        return std::tie(a.kind,a.owner,a.oriented)<
               std::tie(b.kind,b.owner,b.oriented);
    }
};
inline RepairStepFaceKey repairStepFaceKey(const RepairStepGeneratedFace& face) {
    const auto v=face.vertices;
    const PrivateRepairTriangle b{v[1],v[2],v[0]};
    const PrivateRepairTriangle c{v[2],v[0],v[1]};
    return {face.kind,face.owner,std::min({v,b,c})};
}

struct RepairStepGeometry {
    std::shared_ptr<const RegionTopologyIndex> source;
    std::map<FaceId,RegionTopologyIndex::Face> survivorOverrides;
    std::map<VertexId,Vec3> originalPositionOverrides;
    std::map<VertexId,Vec3> generatedPositions;
    std::vector<RepairStepGeneratedFace> generatedFaces;
    std::vector<std::vector<VertexId>> boundaryLoops;
    std::string boundaryDiagnostic;
    std::string operation;
    std::string arguments;
    std::size_t accountedBytes{};

    PrivateRepairTriangle effectiveSourceFace(FaceId id) const {
        const auto it=survivorOverrides.find(id);
        return it!=survivorOverrides.end()?it->second.vertices:
            source->face(id).vertices;
    }
    Vec3 position(VertexId id) const {
        if(const auto it=generatedPositions.find(id);
           it!=generatedPositions.end())return it->second;
        if(const auto it=originalPositionOverrides.find(id);
           it!=originalPositionOverrides.end())return it->second;
        return source->vertex(id).position;
    }
};
struct RepairStepGeometryDelta {
    std::vector<FaceId> modifiedSurvivorFaces;
    std::vector<VertexId> addedVertices,removedVertices,movedVertices;
    std::vector<RepairStepFaceKey> addedFaces,removedFaces;
    std::vector<RegionTopologyIndex::Edge> addedEdges,removedEdges;
};

inline RepairStepGeometry captureRepairStepGeometry(
    const MixedClosureStaging& staged,std::string operation,
    std::string arguments,std::size_t maxBytes=16*1024*1024) {
    auto source=staged.sourceView();
    if(!source||!maxBytes)
        throw std::invalid_argument("M16C2b requires source and nonzero frame budget");
    if(operation.size()>4096||arguments.size()>4096)
        throw std::length_error("M16C2b unbounded operation metadata");
    RepairStepGeometry result;
    result.source=source->sourceRoot();
    result.operation=std::move(operation);
    result.arguments=std::move(arguments);
    // Conservative per-node accounting, not merely sizeof(map::value_type).
    // Every added container element is charged before copying; no full R03
    // topology scan or reconstruction is needed for snapshot capture.
    const auto charge=[&](std::size_t count,std::size_t unit) {
        if(unit&&count>(maxBytes-result.accountedBytes)/unit)
            throw std::length_error("M16C2b finite immutable frame byte budget");
        result.accountedBytes+=count*unit;
    };
    charge(1,sizeof(RepairStepGeometry));
    charge(result.operation.size()+result.arguments.size(),1);
    charge(source->faceOverrides().size(),
           sizeof(std::map<FaceId,RegionTopologyIndex::Face>::value_type)+
           4*sizeof(void*));
    result.survivorOverrides=source->faceOverrides();
    charge(source->positionOverrides().size(),
           sizeof(std::map<VertexId,Vec3>::value_type)+4*sizeof(void*));
    result.originalPositionOverrides=source->positionOverrides();
    charge(staged.positions().newVertexPositions(),
           sizeof(std::map<VertexId,Vec3>::value_type)+4*sizeof(void*));
    result.generatedPositions=staged.positions().createdPositions();
    std::size_t generatedCount=0;
    for(const auto& face:staged.faces())
        if(!face.stableId)++generatedCount;
    charge(generatedCount,sizeof(RepairStepGeneratedFace));
    result.generatedFaces.reserve(generatedCount);
    for(std::size_t i=0;i<staged.faces().size();++i) {
        const auto& face=staged.faces()[i];
        if(face.stableId)continue;
        const auto vertices=face.triangle();
        for(const auto id:vertices)
            if(!result.source->maybeVertex(id)&&
               !result.generatedPositions.contains(id))
                throw std::logic_error(
                    "M16C2b generated face references absent effective vertex");
        result.generatedFaces.push_back(
            {vertices,face.kind,face.owner,i});
    }
    // While the initial triangles of a zipper/cap are being installed,
    // an open chain is legitimate. Preserve evidence instead of throwing
    // away the stage, and never display the original stale yellow outline.
    try {
        const auto loops=staged.currentBoundaryLoops();
        charge(loops.size(),sizeof(std::vector<VertexId>));
        for(const auto& loop:loops)
            charge(loop.size(),sizeof(VertexId));
        result.boundaryLoops=loops;
    }catch(const std::length_error&) {
        throw;
    }catch(const std::exception& e) {
        result.boundaryDiagnostic=std::string(e.what()).substr(0,512);
        charge(result.boundaryDiagnostic.size(),1);
    }
    return result;
}

inline RepairStepGeometryDelta diffRepairStepGeometry(
    const RepairStepGeometry& before,const RepairStepGeometry& after) {
    if(!before.source||before.source!=after.source)
        throw std::invalid_argument(
            "M16C2b may compare only frames with the same immutable R03 root");
    RepairStepGeometryDelta delta;
    std::set<FaceId> changedSource;
    for(const auto& [id,_]:before.survivorOverrides)changedSource.insert(id);
    for(const auto& [id,_]:after.survivorOverrides)changedSource.insert(id);
    for(const auto id:changedSource)
        if(before.effectiveSourceFace(id)!=after.effectiveSourceFace(id))
            delta.modifiedSurvivorFaces.push_back(id);

    std::set<VertexId> originalIds;
    for(const auto& [id,_]:before.originalPositionOverrides)
        originalIds.insert(id);
    for(const auto& [id,_]:after.originalPositionOverrides)
        originalIds.insert(id);
    const auto samePosition=[](Vec3 a,Vec3 b) {
        return a.x==b.x&&a.y==b.y&&a.z==b.z;
    };
    for(const auto id:originalIds)
        if(!samePosition(before.position(id),after.position(id)))
            delta.movedVertices.push_back(id);
    for(const auto& [id,p]:before.generatedPositions) {
        const auto it=after.generatedPositions.find(id);
        if(it==after.generatedPositions.end())
            delta.removedVertices.push_back(id);
        else if(!samePosition(p,it->second))
            delta.movedVertices.push_back(id);
    }
    for(const auto& [id,_]:after.generatedPositions)
        if(!before.generatedPositions.contains(id))
            delta.addedVertices.push_back(id);

    std::set<RepairStepFaceKey> left,right;
    for(const auto& face:before.generatedFaces)
        left.insert(repairStepFaceKey(face));
    for(const auto& face:after.generatedFaces)
        right.insert(repairStepFaceKey(face));
    std::set_difference(right.begin(),right.end(),left.begin(),left.end(),
                        std::back_inserter(delta.addedFaces));
    std::set_difference(left.begin(),left.end(),right.begin(),right.end(),
                        std::back_inserter(delta.removedFaces));

    using Edge=RegionTopologyIndex::Edge;
    const auto edge=[](VertexId a,VertexId b)->Edge {
        return {std::min(a,b),std::max(a,b)};
    };
    const auto insertEdges=[&](std::set<Edge>& set,PrivateRepairTriangle f) {
        for(unsigned i=0;i<3;++i)
            set.insert(edge(f[i],f[(i+1)%3]));
    };
    std::set<Edge> possible,oldPatchEdges,newPatchEdges;
    const auto fillEdges=[&](const RepairStepGeometry& frame,
                             std::set<Edge>& patchEdges) {
        for(const auto& [id,original]:frame.survivorOverrides) {
            insertEdges(patchEdges,original.vertices);
            insertEdges(possible,frame.source->face(id).vertices);
        }
        for(const auto& face:frame.generatedFaces)
            insertEdges(patchEdges,face.vertices);
        possible.insert(patchEdges.begin(),patchEdges.end());
    };
    fillEdges(before,oldPatchEdges);
    fillEdges(after,newPatchEdges);
    // An unchanged source face can still own an edge incident to a
    // changed cap face. Only report removal when NO effective incident
    // face still owns that exact indexed edge, never by XYZ proximity.
    const auto exists=[&](const RepairStepGeometry& frame,
                          const std::set<Edge>& patchEdges,const Edge& e) {
        if(patchEdges.contains(e))return true;
        if(const auto sides=frame.source->maybeEdgeSides(e))
            for(const auto& side:*sides)
                if(!frame.survivorOverrides.contains(side.face))return true;
        return false;
    };
    for(const auto& e:possible) {
        const bool oldExists=exists(before,oldPatchEdges,e);
        const bool newExists=exists(after,newPatchEdges,e);
        if(!oldExists&&newExists)delta.addedEdges.push_back(e);
        if(oldExists&&!newExists)delta.removedEdges.push_back(e);
    }
    return delta;
}

} // namespace mesher
