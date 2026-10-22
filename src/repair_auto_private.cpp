#include "mesher/repair_auto_private.hpp"
#include "mesher/repair_mirror_geometry.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
bool identicalPairs(const std::vector<RepairContourPair>& a,
                    const std::vector<RepairContourPair>& b) {
    if(a.size()!=b.size())return false;
    std::multiset<std::pair<std::size_t,std::size_t>> left,right;
    for(const auto& p:a)left.insert({std::min(p.first,p.second),
                                     std::max(p.first,p.second)});
    for(const auto& p:b)right.insert({std::min(p.first,p.second),
                                      std::max(p.first,p.second)});
    return left==right;
}
void validateCutAgainstSource(
    const RegionTopologyIndex& source,const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,std::size_t maximum) {
    const std::set<FaceId> deleted(proposal.proposedFaces.begin(),
                                   proposal.proposedFaces.end());
    const std::set<VertexId> removed(proposal.proposedVertices.begin(),
                                      proposal.proposedVertices.end());
    std::size_t faces=0;
    cut.index->revision().forEachPartition([&](
        PartitionId,const MeshPartition& part) {
        for(const auto& face:part.faces) {
            if(++faces>maximum)
                throw std::length_error("R06-M04C stale-cut verification work limit");
            const auto old=source.maybeFace(face.id);
            if(!old||deleted.contains(face.id)||
               old->vertices!=face.vertices||
               old->owner!=cut.index->face(face.id).owner)
                throw std::logic_error(
                    "R06-M04C private R03 cut diverges from immutable candidate FaceIds");
            for(const auto vertex:face.vertices) {
                const auto a=source.maybeVertex(vertex);
                const auto b=cut.index->maybeVertex(vertex);
                if(!a||!b||removed.contains(vertex)||
                   a->owner!=b->owner||
                   a->position.x!=b->position.x||
                   a->position.y!=b->position.y||
                   a->position.z!=b->position.z)
                    throw std::logic_error(
                        "R06-M04C survivor coordinates differ from frozen source candidate");
            }
        }
    });
    if(faces!=cut.survivingFaces)
        throw std::logic_error("R06-M04C stale R03 surviving-face count");
}
} // namespace

CertifiedPrivateAutoResult applyCertifiedPrivateAutomaticClosure(
    std::shared_ptr<const RegionTopologyIndex> sourceCandidate,
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const std::vector<RegionTopologyIndex::CollisionPair>& trustedContacts,
    const CertifiedPrivateAutoLimits& limits) {
    if(!sourceCandidate||!cut.index||!plan.ready()||
       !limits.maxRetiredCavityFaces||!limits.maxRetiredCavityVertices||
       !limits.maxRetiredCavityComponents)
        throw std::invalid_argument("R06-M04C requires a valid trusted private closure and finite budgets");
    validateCutAgainstSource(*sourceCandidate,cut,proposal,
                             limits.closure.maxAuditedFaces);
    // Replan directly from immutable source and exactly the entrusted R01
    // contacts; a caller may not provide an arbitrary fabricated zipper.
    const auto recomputed=planPrivateAutomaticClosure(
        *sourceCandidate,cut,proposal,trustedContacts);
    if(!recomputed.ready() ||
       recomputed.cappedContours!=plan.cappedContours ||
       !identicalPairs(recomputed.zipperPairs,plan.zipperPairs))
        throw std::logic_error(
            "R06-M04C stale or forged M03 closure choice disagrees with trusted source");
    // Whole-component zero-rim R03 deletion is NOT automatically acceptable.
    // Require M02 evidence on the PRE-EXCISION candidate when such a
    // component has already disappeared from the R03 root.
    if(std::any_of(plan.regions.begin(),plan.regions.end(),
       [](const AutoClosureRegion& region) {
           return region.zeroLoopComponentRetirement;
       })) {
        const auto before=inspectEnclosedComponents(sourceCandidate,
                                                    limits.material);
        const std::set<FaceId> removed(proposal.proposedFaces.begin(),
                                       proposal.proposedFaces.end());
        std::size_t whollyRemoved=0;
        for(std::size_t c=0;c<before.inventory.components.size();++c) {
            const auto& component=before.inventory.components[c];
            if(std::all_of(component.faces.begin(),component.faces.end(),
                   [&](FaceId id){return removed.contains(id);})) {
                ++whollyRemoved;
                if(before.components[c].kind!=
                   EnclosedComponentKind::RedundantEnclosedCavity)
                    throw std::logic_error(
                        "R06-M04C zero-loop deletion lacks independent inward enclosed cavity proof");
            }
        }
        if(!whollyRemoved)
            throw std::logic_error(
                "R06-M04C zero-loop region lacks an entire original face component");
    }

    const auto staged=applyPrivateMixedClosure(cut,proposal,plan,
                                               limits.closure);
    const auto evidence=inspectEnclosedComponents(staged.index,
                                                 limits.material);
    CertifiedPrivateAutoResult out;
    out.postClosureFaces=evidence.inventory.faces;
    out.generatedFaces=staged.newFaces;
    out.insertedVertices=staged.insertedVertices.size();
    out.refinementSteps=staged.refinementSteps;
    out.minimumCapQuality=staged.minimumCapQuality;
    out.minimumZipperQuality=staged.minimumZipperQuality;
    out.spatialCapLoops=staged.spatialCapLoops;
    out.mirroredSeamZipperPairs=staged.mirroredSeamZipperPairs;
    out.mirroredSeamVertices=staged.mirroredSeamVertices;
    out.spatialPoissonIterations=staged.spatialPoissonIterations;
    out.minimumSpatialInitialQuality=staged.minimumSpatialInitialQuality;
    out.spatialDensitySplits=staged.spatialDensitySplits;
    out.spatialDensityRemainingLongEdges=staged.spatialDensityRemainingLongEdges;
    out.maximumFinalSpatialCapEdgeRatio=staged.maximumFinalSpatialCapEdgeRatio;
    out.windingTriangleVisits=evidence.windingTriangleVisits;
    out.collisionNarrowPhaseTests=evidence.collisionWork.narrowPhaseTests;
    std::set<FaceId> retiredFaces;
    std::set<VertexId> retiredVertices;
    std::set<PartitionId> owners;
    for(std::size_t i=0;i<evidence.components.size();++i) {
        const auto& component=evidence.components[i];
        if(component.kind==EnclosedComponentKind::Ambiguous)
            throw std::logic_error(
                "R06-M04C cannot publish an ambiguous closed material component: "+
                component.reason);
        if(component.kind==EnclosedComponentKind::Exterior) {
            ++out.acceptedExteriorComponents;
            continue;
        }
        if(++out.retiredCavityComponents>limits.maxRetiredCavityComponents)
            throw std::length_error("R06-M04C redundant cavity component budget exceeded");
        for(const auto id:evidence.inventory.components[i].faces) {
            if(!retiredFaces.insert(id).second||
               retiredFaces.size()>limits.maxRetiredCavityFaces)
                throw std::length_error("R06-M04C redundant cavity face budget exceeded");
            owners.insert(staged.index->face(id).owner);
        }
        for(const auto id:evidence.inventory.components[i].vertices) {
            if(!retiredVertices.insert(id).second||
               retiredVertices.size()>limits.maxRetiredCavityVertices)
                throw std::length_error("R06-M04C redundant cavity vertex budget exceeded");
            owners.insert(staged.index->vertex(id).owner);
        }
    }
    auto finalIndex=staged.index;
    if(!retiredFaces.empty()) {
        auto view=staged.index->revision().createMutableView();
        std::size_t facesErased=0,verticesErased=0;
        for(const auto owner:owners) {
            auto& part=view.editPartition(owner);
            const auto first=part.faces.size();
            std::erase_if(part.faces,[&](const PartitionFace& face) {
                return retiredFaces.contains(face.id);
            });
            facesErased+=first-part.faces.size();
            const auto count=part.vertices.size();
            std::erase_if(part.vertices,[&](const PartitionVertex& vertex) {
                return retiredVertices.contains(vertex.id);
            });
            verticesErased+=count-part.vertices.size();
        }
        if(facesErased!=retiredFaces.size() ||
           verticesErased!=retiredVertices.size())
            throw std::logic_error("R06-M04C cannot fully retire isolated cavity component");
        auto removedFaceIds=staged.index->retiredFaceIds();
        auto removedVertexIds=staged.index->retiredVertexIds();
        removedFaceIds.insert(removedFaceIds.end(),
                              retiredFaces.begin(),retiredFaces.end());
        removedVertexIds.insert(removedVertexIds.end(),
                                retiredVertices.begin(),retiredVertices.end());
        finalIndex=RegionTopologyIndex::restore(
            view.commit().revision,removedVertexIds,removedFaceIds);
        validatePrivateClosedManifold(*finalIndex,
                                       limits.closure.maxAuditedFaces);
    }
    out.retiredCavityFaces.assign(retiredFaces.begin(),retiredFaces.end());
    out.retiredCavityVertices.assign(retiredVertices.begin(),retiredVertices.end());
    for(const auto id:staged.greenFaces)
        if(!retiredFaces.contains(id))out.greenFaces.push_back(id);
    for(const auto id:staged.blueFaces)
        if(!retiredFaces.contains(id))out.blueFaces.push_back(id);
    if(out.acceptedExteriorComponents==0)
        throw std::logic_error("R06-M04C removed every outward exterior shell");
    for(const auto id:proposal.proposedFaces)
        if(finalIndex->maybeFace(id)||!finalIndex->retiredFace(id))
            throw std::logic_error("R06-M04C resurrected R03 removed geometry");
    for(const auto id:retiredFaces)
        if(finalIndex->maybeFace(id)||!finalIndex->retiredFace(id))
            throw std::logic_error("R06-M04C failed to tombstone an internal cavity face");
    for(const auto id:retiredVertices)
        if(finalIndex->maybeVertex(id)||!finalIndex->retiredVertex(id))
            throw std::logic_error("R06-M04C failed to tombstone an internal cavity vertex");
    if(limits.closure.requireReflectedGeometry) {
        // Private diagnostic only: the actual R07 publication path must
        // additionally update the authoritative reciprocal mirror-ID map.
        // No silent transition to asymmetric while the current sculpt
        // continues believing its mirror relationships are valid.
        (void)auditPrivateReflectedGeometry(
            *finalIndex,limits.closure.maxAuditedFaces*3,
            limits.closure.maxAuditedFaces);
    }
    out.postRetirementFaces=out.postClosureFaces-retiredFaces.size();
    out.index=std::move(finalIndex);
    return out;
}
} // namespace mesher
