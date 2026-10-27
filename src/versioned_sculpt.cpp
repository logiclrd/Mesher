#include "mesher/versioned_sculpt.hpp"
#include "mesher/partition_render.hpp"
#include "mesher/repair_auto_private.hpp"
#include "mesher/repair_auto_publication.hpp"
#include "mesher/repair_excision.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point a,Clock::time_point b) {
    return std::chrono::duration<double>(b-a).count();
}
// An exact physical Step may clone the same partition in multiple PRIVATE
// edits. All those copies cost work even though only ONE terminal CAS is
// published. Refuse overflow rather than letting a wrapped size evade a cap.
std::size_t checkedStepSum(std::initializer_list<std::size_t> values) {
    std::size_t sum=0;
    for(const auto value:values) {
        if(value>std::numeric_limits<std::size_t>::max()-sum)
            throw std::length_error(
                "Aggregate private Step resource counter overflow");
        sum+=value;
    }
    return sum;
}
std::string refuseExcessStepWork(
    const SculptStepResourceWork& work,
    const SculptStepResourceLimits& limit) {
    if(work.privateCopiedBytes>limit.maxPrivateCopiedBytes)
        return "Aggregate private Step COW payload byte budget exhausted";
    if(work.changedIdReferences>limit.maxChangedIdReferences)
        return "Aggregate private Step changed ID reference budget exhausted";
    if(work.finalDirtyPartitions>limit.maxFinalDirtyPartitions)
        return "Aggregate private Step final dirty partition budget exhausted";
    return {};
}
std::size_t elementCount(const RegionTopologyIndex& index,bool vertices) {
    std::size_t count=0;
    index.revision().forEachPartition([&](PartitionId,const MeshPartition& p) {
        count+=vertices?p.vertices.size():p.faces.size();
    });
    return count;
}
std::vector<std::pair<VertexId,VertexId>> makeMirrors(
    const FlatMeshIndex& order,std::size_t count,bool symmetric) {
    if(!symmetric)return {};
    const auto correspondences=symmetricSphereMirrors(
        static_cast<std::uint32_t>(count));
    std::vector<std::pair<VertexId,VertexId>> pairs;
    pairs.reserve(count);
    for(std::size_t i=0;i<count;++i)
        pairs.push_back({order.vertexIds.at(i),
                         order.vertexIds.at(correspondences[i])});
    return pairs;
}
}

SculptContactTracker::SculptContactTracker(std::size_t quietChecks)
    :quietChecks_(quietChecks) {
    if(quietChecks_==0)
        throw std::invalid_argument("Contact episode quiet-check count must be positive");
}
void SculptContactTracker::observe(std::optional<bool> contact) noexcept {
    if(!contact) {
        ++stats_.uncertainChecks;
        // M23: the closure threshold is CONSECUTIVE conclusive Clean
        // checks. Unknown, refusal, or exhaustion interrupts that
        // sequence without either ending or starting an episode.
        stats_.quietRun=0;
        stats_.contactRun=0;
        return;
    }
    if(*contact) {
        ++stats_.contactChecks;
        if(!stats_.active) {
            stats_.active=true;
            ++stats_.episodes;
            stats_.contactRun=0;
        }
        stats_.quietRun=0;
        ++stats_.contactRun;
        stats_.longestContactRun=std::max(stats_.longestContactRun,
                                         stats_.contactRun);
        return;
    }
    ++stats_.cleanChecks;
    stats_.contactRun=0;
    if(stats_.active&&++stats_.quietRun>=quietChecks_) {
        stats_.active=false;
        ++stats_.closedEpisodes;
        stats_.quietRun=0;
    }
}

SculptSourceContactAssessment assessPostSculptOriginalContacts(
    const ProposedSculptStep& proposed,bool symmetric,
    const SculptSourceContactLimits& limits,
    const std::unordered_map<VertexId,VertexId>* mirrors,
    std::size_t groupedSeeds,
    bool requireFiniteTransverseSections) {
    SculptSourceContactAssessment result;
    const auto refuse=[&](std::string reason) {
        result.refusal=std::move(reason);
        result.contacts.clear();
        result.originalCandidate.reset();
        result.status=SculptSourceContactStatus::Refused;
        return result;
    };
    if(!proposed.before||!proposed.candidate)
        return refuse("Post-sculpt V13 discovery requires two immutable index roots");
    std::vector<VertexId> vertices;
    std::size_t faces=0;
    bool bounded=true;
    proposed.candidate->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            if(!bounded)return;
            if(part.vertices.size()>limits.maxVertices-vertices.size()||
               part.faces.size()>limits.maxFaces-faces) {
                bounded=false;
                return;
            }
            faces+=part.faces.size();
            for(const auto& v:part.vertices)
                vertices.push_back(v.id);
        });
    if(!bounded)
        return refuse("Post-sculpt complete original V13 root exceeds live vertex/face budget");
    // Mirror correspondence is an EXTRA invariant, never a license to weld
    // two independent coincident IDs. Validate the entire post-remesh
    // involution before returning ANY source contacts.
    if(symmetric) {
        if(!mirrors||mirrors->size()!=vertices.size())
            return refuse("Post-sculpt complete original mirror map is absent or incomplete");
        for(const auto id:vertices) {
            const auto it=mirrors->find(id);
            if(it==mirrors->end()||
               !mirrors->contains(it->second)||
               mirrors->at(it->second)!=id||
               !proposed.candidate->maybeVertex(it->second))
                return refuse("Post-sculpt original mirror source ID is not reciprocal");
            const auto p=proposed.candidate->vertex(id).position;
            const auto q=proposed.candidate->vertex(it->second).position;
            if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
               ((id==it->second)!=(p.x==0)))
                return refuse("Post-sculpt original mirror source orbit is not exact XYZ");
        }
    }
    try {
        // A changed-face-only search is not the full ORIGINAL contact
        // graph. Every live face becomes a seed through its vertices;
        // the BVH still performs bounded local pair tests, not a global
        // O(F^2) triangle loop.
        auto all=proposed.candidate->triangleIntersections(
            vertices,{},&result.work,limits.query,groupedSeeds);
        result.discoveredPairs=all.size();
        if(all.empty()) {
            result.originalCandidate=proposed.candidate;
            result.status=SculptSourceContactStatus::Clean;
            return result;
        }
        if(all.size()>limits.maxSectionTests)
            return refuse("Post-sculpt original transverse section work budget exhausted");
        for(const auto& pair:all) {
            if(pair.ambiguous)
                return refuse("Post-sculpt original V13 face pair is ambiguous");
            if(!requireFiniteTransverseSections)continue;
            const auto a=proposed.candidate->face(pair.first).vertices;
            const auto b=proposed.candidate->face(pair.second).vertices;
            const auto points=[&](const std::array<VertexId,3>& ids) {
                return std::array<Vec3,3>{{
                    proposed.candidate->vertex(ids[0]).position,
                    proposed.candidate->vertex(ids[1]).position,
                    proposed.candidate->vertex(ids[2]).position
                }};
            };
            if(splitTransverseTriangleContact(
                   points(a),a,points(b),b).status!=
               IntersectionSectionStatus::TransverseSegment)
                return refuse("Post-sculpt original V13 pair has no supported finite transverse section");
        }
        result.contacts=std::move(all);
        result.originalCandidate=proposed.candidate;
        result.status=requireFiniteTransverseSections?
            SculptSourceContactStatus::CompleteTransverse:
            SculptSourceContactStatus::CompleteIntersecting;
    }catch(const std::length_error& error) {
        return refuse(std::string("Post-sculpt bounded original V13 discovery: ")+
                      error.what());
    }catch(const std::logic_error& error) {
        return refuse(std::string("Post-sculpt original V13 provenance refusal: ")+
                      error.what());
    }
    return result;
}

// R07: changed-neighborhood source discovery retains exact full-root mirror
// validation and queries all spatial neighbors of the changed face seeds.
// Protected original-source V13 whole-mesh discovery above is unchanged.
SculptSourceContactAssessment assessPostSculptRepairContacts(
    const ProposedSculptStep& proposed,bool symmetric,
    const SculptSourceContactLimits& limits,
    const std::unordered_map<VertexId,VertexId>* mirrors) {
    SculptSourceContactAssessment result;
    const auto refuse=[&](std::string reason) {
        result.refusal=std::move(reason);
        result.contacts.clear();
        result.originalCandidate.reset();
        result.status=SculptSourceContactStatus::Refused;
        return result;
    };
    if(!proposed.before||!proposed.candidate)
        return refuse("Live AUTO requires immutable before and current roots");
    std::vector<VertexId> vertices;
    std::size_t faces=0;
    bool bounded=true;
    proposed.candidate->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            if(!bounded)return;
            if(part.vertices.size()>limits.maxVertices-vertices.size()||
               part.faces.size()>limits.maxFaces-faces) {
                bounded=false;
                return;
            }
            faces+=part.faces.size();
            for(const auto& v:part.vertices)
                vertices.push_back(v.id);
        });
    if(!bounded)
        return refuse("Post-sculpt complete original V13 root exceeds live vertex/face budget");
    // Mirror correspondence is an EXTRA invariant, never a license to weld
    // two independent coincident IDs. Validate the entire post-remesh
    // involution before returning ANY source contacts.
    if(symmetric) {
        if(!mirrors||mirrors->size()!=vertices.size())
            return refuse("Post-sculpt complete original mirror map is absent or incomplete");
        for(const auto id:vertices) {
            const auto it=mirrors->find(id);
            if(it==mirrors->end()||
               !mirrors->contains(it->second)||
               mirrors->at(it->second)!=id||
               !proposed.candidate->maybeVertex(it->second))
                return refuse("Post-sculpt original mirror source ID is not reciprocal");
            const auto p=proposed.candidate->vertex(id).position;
            const auto q=proposed.candidate->vertex(it->second).position;
            if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
               ((id==it->second)!=(p.x==0)))
                return refuse("Post-sculpt original mirror source orbit is not exact XYZ");
        }
    }
    try {
        // Query the full current BVH only from faces affected by the
        // stamp. Neighbor candidates are never restricted to the stamp.
        std::vector<VertexId> dirtyVertices;
        std::vector<FaceId> dirtyFaces;
        dirtyVertices.reserve(proposed.changedVertices.size());
        dirtyFaces.reserve(proposed.changedFaces.size());
        for(const auto id:proposed.changedVertices) {
            if(proposed.candidate->maybeVertex(id))
                dirtyVertices.push_back(id);
            else if(!proposed.candidate->retiredVertex(id))
                return refuse("Live AUTO changed vertex has no source identity");
        }
        for(const auto id:proposed.changedFaces) {
            if(proposed.candidate->maybeFace(id))
                dirtyFaces.push_back(id);
            else if(!proposed.candidate->retiredFace(id))
                return refuse("Live AUTO changed face has no source identity");
        }
        if(dirtyVertices.empty()&&dirtyFaces.empty()&&
           proposed.before!=proposed.candidate)
            return refuse("Live AUTO missing changed-neighborhood evidence");
        auto all=proposed.candidate->triangleIntersections(
            dirtyVertices,dirtyFaces,&result.work,limits.query,16);
        result.discoveredPairs=all.size();
        if(!all.empty()) {
            // A pre-existing unchanged contact is not caused by this
            // stamp. Compare only valid stable source IDs on before.
            std::vector<VertexId> oldVertices;
            std::vector<FaceId> oldFaces;
            for(const auto id:dirtyVertices)
                if(proposed.before->maybeVertex(id))
                    oldVertices.push_back(id);
            for(const auto id:dirtyFaces)
                if(proposed.before->maybeFace(id))
                    oldFaces.push_back(id);
            const auto previously=proposed.before->triangleIntersections(
                oldVertices,oldFaces,nullptr,limits.query,16);
            std::set<std::pair<FaceId,FaceId>> preexisting;
            for(const auto& pair:previously)
                if(!pair.ambiguous)
                    preexisting.emplace(pair.first,pair.second);
            std::erase_if(all,[&](const auto& pair) {
                return !pair.ambiguous&&
                    preexisting.contains({pair.first,pair.second});
            });
        }
        if(all.empty()) {
            result.originalCandidate=proposed.candidate;
            result.status=SculptSourceContactStatus::Clean;
            return result;
        }
        if(all.size()>limits.maxSectionTests)
            return refuse("Post-sculpt original transverse section work budget exhausted");
        // R07's R02/R03 excision works from whole intersecting face
        // stars, not exact V13 section patches. A non-ambiguous contact
        // can therefore be admitted to private repair without requiring
        // local section triangulation that R02 never uses. Ambiguous
        // collision predicates remain a hard refusal.
        for(const auto& pair:all)
            if(pair.ambiguous)
                return refuse("R07 AUTO face pair has ambiguous contact");
        result.contacts=std::move(all);
        result.originalCandidate=proposed.candidate;
        result.status=SculptSourceContactStatus::CompleteIntersecting;
    }catch(const std::length_error& error) {
        return refuse(std::string("Post-sculpt bounded original V13 discovery: ")+
                      error.what());
    }catch(const std::logic_error& error) {
        return refuse(std::string("Post-sculpt original V13 provenance refusal: ")+
                      error.what());
    }
    return result;
}

// V17d2: every new and retired vertex from a mirrored private repair
// receives a canonical involutive mirror pair. Only the small set of
// surgery-touched IDs is examined for delta derivation. Existing pairs are
// checked by V16 when the surgeon is called; the caller supplies the
// collapser's already staged post-refinement/post-collapse map.
void deriveMirroredRepairDeltas(
    SculptCollisionEvaluation& outcome,
    const RegionTopologyIndex& candidate,
    const std::unordered_map<VertexId,VertexId>& oldMirrors) {
    if(!outcome.repair)return;
    const auto& repaired=*outcome.repair;
    const auto& final=*repaired.index;
    using Point=std::tuple<double,double,double>;
    const std::set<VertexId> added(repaired.insertedVertices.begin(),
                                   repaired.insertedVertices.end());
    const std::set<VertexId> removed(repaired.retiredVertices.begin(),
                                     repaired.retiredVertices.end());
    std::map<Point,VertexId> byPosition;
    for(const auto id:added) {
        if(candidate.maybeVertex(id)||!final.maybeVertex(id))
            throw std::logic_error("Surgery added mirror ID has invalid lifetime");
        const auto p=final.vertex(id).position;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
           !byPosition.emplace(Point{p.x,p.y,p.z},id).second)
            throw std::logic_error("Surgery added duplicate or nonfinite mirror vertex");
    }
    for(const auto id:added) {
        const auto p=final.vertex(id).position;
        const auto it=byPosition.find(Point{-p.x,p.y,p.z});
        if(it==byPosition.end())
            throw std::logic_error("Surgery new vertex has no exact reflected partner");
        const auto twin=it->second;
        if(id<=twin)outcome.insertedMirrors.emplace_back(id,twin);
    }
    for(const auto id:removed) {
        if(!candidate.maybeVertex(id)||final.maybeVertex(id))
            throw std::logic_error("Surgery retired mirror ID has invalid lifetime");
        const auto it=oldMirrors.find(id);
        if(it==oldMirrors.end()||!removed.contains(it->second)||
           !oldMirrors.contains(it->second)||
           oldMirrors.at(it->second)!=id)
            throw std::logic_error("Surgery retired only one side of a mirror orbit");
        if(id<=it->second)outcome.retiredMirrors.emplace_back(id,it->second);
    }
}

// V17d1: deliberately separate read-only classification and private surgery
// from the final published Step. The live viewport never enables this yet.
SculptCollisionEvaluation evaluateSculptCollisionCandidate(
    const ProposedSculptStep& proposed,bool symmetric,
    const SculptCollisionSettings& settings,
    const std::vector<FaceId>& isolatedComponentSeeds,
    const std::unordered_map<VertexId,VertexId>* vertexMirrors) {
    if(!proposed.before||!proposed.candidate)
        throw std::invalid_argument("Classified collision evaluation needs complete roots");
    SculptCollisionEvaluation outcome;
    if(!settings.enabled)return outcome;
    const auto verifyAnchors=[&](
        const std::shared_ptr<const RegionTopologyIndex>& evaluated) {
        if(!settings.anchoredSurface)return;
        const auto& guard=*settings.anchoredSurface;
        if(!std::isfinite(guard.maximumDistanceInEdges)||
           guard.maximumDistanceInEdges<0)
            throw std::invalid_argument("Invalid anchored-surface tolerance");
        outcome.anchoredSurfaceAudit=auditSurfaceWitnesses(
            *evaluated,guard.originalWitnesses,guard.candidateFaces,
            guard.originalEdgeLength,guard.maxDistanceTests);
        if(outcome.anchoredSurfaceAudit->maximumDistanceInEdges>
           guard.maximumDistanceInEdges) {
            outcome.refusal="Cumulative anchored original-surface deviation exceeded limit";
            outcome.repair.reset();
        }
    };
    // These finite limits apply even if the caller uses default V13 offline
    // settings with unlimited budgets. Lower caller limits remain respected.
    auto surgery=settings.surgery;
    auto& plan=surgery.planning;
    using Limits=RegionTopologyIndex::CollisionQueryLimits;
    const Limits hard{2048,131072,16384,131072,65536,2048};
    const auto tighten=[&](Limits& l) {
        l.maxSeedFaces=std::min(l.maxSeedFaces,hard.maxSeedFaces);
        l.maxBoxTests=std::min(l.maxBoxTests,hard.maxBoxTests);
        l.maxPartitionTests=std::min(l.maxPartitionTests,hard.maxPartitionTests);
        l.maxCandidateTriangles=std::min(l.maxCandidateTriangles,hard.maxCandidateTriangles);
        l.maxNarrowPhaseTests=std::min(l.maxNarrowPhaseTests,hard.maxNarrowPhaseTests);
        l.maxReportedContacts=std::min(l.maxReportedContacts,hard.maxReportedContacts);
    };
    tighten(plan.collisionLimits);
    tighten(surgery.residualCollisionLimits);
    plan.allowReferenceWindingFallback=false;
    // Explicitly preserve sampled exposed/reversed-boundary features.
    // The legacy all-face reverse check is incompatible with buried removal.
    surgery.requireReverseSurfaceFidelity=false;
    surgery.requireExposedFragmentFidelity=false;
    const std::vector<VertexId> vertices(
        proposed.changedVertices.begin(),proposed.changedVertices.end());
    const std::vector<FaceId> faces(
        proposed.changedFaces.begin(),proposed.changedFaces.end());
    if(symmetric) {
        outcome.plan=planApproximateExcision(
            *proposed.candidate,vertices,faces,isolatedComponentSeeds,plan);
        if(outcome.plan.status==RepairPlanStatus::NoChanges&&
           outcome.plan.intersections==0) {
            verifyAnchors(proposed.candidate);
            return outcome;
        }
        if(!outcome.plan.ready()) {
            outcome.refusal="Symmetric classified collision refused: "+
                outcome.plan.reason;
            return outcome;
        }
        // V16 accepts a compact canonical vector, whereas the V17c staged
        // map is mutable. Convert ONLY after classification discovers a
        // repair; collision-free symmetric Steps never copy all mirror IDs.
        if(!vertexMirrors||vertexMirrors->empty()) {
            outcome.refusal="Symmetric collision candidate has no staged mirror map";
            return outcome;
        }
        std::vector<MirrorPair> pairs;
        pairs.reserve(vertexMirrors->size()/2+1);
        for(const auto& [a,b]:*vertexMirrors)
            if(a<=b)pairs.emplace_back(a,b);
        surgery.requireExposedFragmentFidelity=true;
        auto across=repairMirroredContoursAcrossPlane(
            proposed.candidate,outcome.plan,pairs,surgery);
        auto normalized=std::move(across.surgery);
        normalized.commit.statistics=across.accumulatedStatistics;
        // Crossing normalization is a private precursor of surgery. Roll its
        // survivors into the SAME net Step; never count temporary IDs that
        // normalization created and the final surgery then retired.
        for(const auto id:across.planeSeamVertices)
            if(normalized.index->maybeVertex(id))
                normalized.insertedVertices.push_back(id);
        for(const auto id:across.normalizationInsertedFaces)
            if(normalized.index->maybeFace(id))
                normalized.insertedFaces.push_back(id);
        const auto netVertexAdded=[&](VertexId id) {
            return !proposed.candidate->maybeVertex(id)&&
                    normalized.index->maybeVertex(id);
        };
        const auto netVertexRemoved=[&](VertexId id) {
            return proposed.candidate->maybeVertex(id)&&
                   !normalized.index->maybeVertex(id);
        };
        const auto netFaceAdded=[&](FaceId id) {
            return !proposed.candidate->maybeFace(id)&&
                    normalized.index->maybeFace(id);
        };
        const auto netFaceRemoved=[&](FaceId id) {
            return proposed.candidate->maybeFace(id)&&
                   !normalized.index->maybeFace(id);
        };
        const auto retain=[&](auto& ids,const auto& test) {
            std::erase_if(ids,[&](auto id){return !test(id);});
            std::sort(ids.begin(),ids.end());
            ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        };
        retain(normalized.insertedVertices,netVertexAdded);
        retain(normalized.retiredVertices,netVertexRemoved);
        retain(normalized.insertedFaces,netFaceAdded);
        retain(normalized.retiredFaces,netFaceRemoved);
        outcome.repair=std::move(normalized);
        deriveMirroredRepairDeltas(outcome,*proposed.candidate,*vertexMirrors);
    }else {
        auto classified=applyClassifiedExcisionRepair(
            proposed.candidate,vertices,faces,isolatedComponentSeeds,
            ClassifiedFeaturePolicy::PreserveSampledExposedFeatures,surgery);
        outcome.plan=classified.plan;
        if(outcome.plan.status==RepairPlanStatus::NoChanges&&
           outcome.plan.intersections==0) {
            verifyAnchors(proposed.candidate);
            return outcome;
        }
        if(!classified.surgery) {
            outcome.refusal="Classified collision repair refused: "+
                (outcome.plan.reason.empty()?
                 std::string("ambiguous or unsupported surface"):outcome.plan.reason);
            return outcome;
        }
        outcome.repair=std::move(classified.surgery);
    }
    // A healed scar is NOT evidence that the entire changed stamp is clean.
    // Re-query every surviving moved/rewritten face and every added face
    // against the COMPLETE healed candidate before granting publication.
    std::vector<VertexId> finalVertices=vertices;
    finalVertices.insert(finalVertices.end(),
        outcome.repair->insertedVertices.begin(),
        outcome.repair->insertedVertices.end());
    finalVertices.insert(finalVertices.end(),
        outcome.repair->retiredVertices.begin(),
        outcome.repair->retiredVertices.end());
    std::vector<FaceId> finalFaces=faces;
    finalFaces.insert(finalFaces.end(),
        outcome.repair->insertedFaces.begin(),
        outcome.repair->insertedFaces.end());
    finalFaces.insert(finalFaces.end(),
        outcome.repair->retiredFaces.begin(),
        outcome.repair->retiredFaces.end());
    for(const auto& contour:outcome.plan.contours)
        finalFaces.insert(finalFaces.end(),
            contour.survivingFaces.begin(),contour.survivingFaces.end());
    RegionTopologyIndex::CollisionStatistics measured;
    const auto residual=outcome.repair->index->triangleIntersections(
        finalVertices,finalFaces,&measured,surgery.residualCollisionLimits);
    outcome.finalNarrowPhaseTests=measured.narrowPhaseTests;
    if(!residual.empty()) {
        outcome.refusal="Entire repaired Step retains a collision or ambiguous contact";
        outcome.repair.reset();
        return outcome;
    }
    verifyAnchors(outcome.repair->index);
    return outcome;
}

Mesh flattenRevisionForViewport(
    const RegionTopologyIndex& index,const FlatMeshIndex& originalOrder,
    ViewportFlattenStatistics* statistics) {
    const auto start=Clock::now();
    std::unordered_map<VertexId,Vec3> positions;
    std::unordered_map<FaceId,std::array<VertexId,3>> faces;
    index.revision().forEachPartition(
        [&](PartitionId,const MeshPartition& partition) {
            for(const auto& v:partition.vertices)
                if(!positions.emplace(v.id,v.position).second)
                    throw std::logic_error("Duplicate vertex in viewport adapter");
            for(const auto& f:partition.faces)
                if(!faces.emplace(f.id,f.vertices).second)
                    throw std::logic_error("Duplicate face in viewport adapter");
        });
    if(positions.size()>std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Flat Vulkan viewport exceeds 32-bit vertex index range");
    Mesh output;
    output.vertices.reserve(positions.size());
    output.triangles.reserve(faces.size());
    std::unordered_map<VertexId,std::uint32_t> indices;
    indices.reserve(positions.size());
    auto putVertex=[&](VertexId id,Vec3 value) {
        const auto indexValue=static_cast<std::uint32_t>(output.vertices.size());
        indices.emplace(id,indexValue);
        output.vertices.push_back(value);
    };
    // Preserve original flat indices for every surviving imported vertex.
    // Retired vertices are simply absent. Stable-ID additions are appended
    // after the surviving originals in increasing ID order.
    for(const auto id:originalOrder.vertexIds) {
        const auto it=positions.find(id);
        if(it==positions.end())continue;
        putVertex(it->first,it->second);
        positions.erase(it);
    }
    std::vector<std::pair<VertexId,Vec3>> extraVertices(
        positions.begin(),positions.end());
    std::sort(extraVertices.begin(),extraVertices.end(),
              [](const auto& a,const auto& b){return a.first<b.first;});
    for(const auto& [id,p]:extraVertices)putVertex(id,p);
    auto putFace=[&](std::array<VertexId,3> f) {
        output.triangles.push_back({
            indices.at(f[0]),indices.at(f[1]),indices.at(f[2])});
    };
    for(const auto id:originalOrder.faceIds) {
        const auto it=faces.find(id);
        if(it==faces.end())continue;
        putFace(it->second);
        faces.erase(it);
    }
    std::vector<std::pair<FaceId,std::array<VertexId,3>>> extraFaces(
        faces.begin(),faces.end());
    std::sort(extraFaces.begin(),extraFaces.end(),
              [](const auto& a,const auto& b){return a.first<b.first;});
    for(const auto& [_,vertices]:extraFaces)putFace(vertices);
    if(statistics) {
        statistics->milliseconds=seconds(start,Clock::now())*1000;
        statistics->vertices=output.vertices.size();
        statistics->triangles=output.triangles.size();
        statistics->bytes=output.vertices.size()*sizeof(Vec3)+
                          output.triangles.size()*sizeof(Triangle);
    }
    return output;
}

VersionedSculptBrush::VersionedSculptBrush(
    const Mesh& initial,double strength,CurvatureSettings curvature,
    CollapseSettings collapse,RefineSettings refine,bool symmetric,
    std::size_t partitionCapacity,SculptStepValidator validator,
    SculptCollisionSettings collision)
    :VersionedSculptBrush(partitionFlatMesh(initial,partitionCapacity),
                          initial.vertices.size(),strength,curvature,collapse,
                          refine,symmetric,partitionCapacity,std::move(validator),
                          std::move(collision)) {}

VersionedSculptBrush::VersionedSculptBrush(
    PartitionedMeshImport import,std::size_t initialCount,double strength,
    CurvatureSettings curvature,CollapseSettings collapse,RefineSettings refine,
    bool symmetric,std::size_t partitionCapacity,
    SculptStepValidator validator,SculptCollisionSettings collision)
    :flatOrder_(std::make_shared<const FlatMeshIndex>(
         std::move(import.flatIndex))),
published_(RegionTopologyIndex::build(std::move(import.revision))),
     firstOriginalAuthority_(collision.physicalExact?
         std::make_shared<const OriginalPatchLifetime>(
             beginOriginalPatchLifetime(captureOriginalSourceAnchors(
                 published_.snapshot(),flatOrder_->faceIds,
                 collision.originalIntentInput.maxOriginalWitnesses))):nullptr),
     history_(published_.snapshot(),64,32*1024*1024,&historyRetirement_,
              initialCount,flatOrder_->faceIds.size(),firstOriginalAuthority_),
     inflate_(strength,curvature,makeMirrors(*flatOrder_,initialCount,symmetric)),
     refiner_(initialCount,refine,
              makeMirrors(*flatOrder_,initialCount,symmetric),partitionCapacity),
     collapser_(initialCount,collapse,
                makeMirrors(*flatOrder_,initialCount,symmetric),partitionCapacity),
     partitionCapacity_(partitionCapacity),
     initialVertexCount_(initialCount),
     vertices_(initialCount),faces_(flatOrder_->faceIds.size()),
     validator_(std::move(validator)),collision_(std::move(collision)),
     symmetric_(symmetric),contact_(collision_.quietChecksToEndContact) {
    if(!partitionCapacity_)throw std::invalid_argument("Invalid partition capacity");
    if(collision_.enabled&&collision_.physicalExact)
        throw std::invalid_argument(
            "Physical exact Boolean and legacy approximate collision surgery are mutually exclusive");
    if(collision_.certifiedAutoClosure&&
       (collision_.physicalExact||collision_.enabled||
        collision_.offModeConstraints.enabled))
        throw std::invalid_argument(
            "Live certified R06 AUTO is exclusive with exact, legacy and OFF-mode collision policies");
    if(collision_.offModeConstraints.enabled&&
       (collision_.physicalExact||collision_.enabled))
        throw std::invalid_argument(
            "V18 OFF-mode contact guard is mutually exclusive with V17 Boolean surgery and physicalExact Merge");
    if(collision_.offModeConstraints.enabled&&symmetric_) {
        const auto checked=auditOffModeMirrors(
            published_.snapshot(),inflate_.mirrors(),{},{},true);
        if(!checked.complete)
            throw std::invalid_argument(
                "V18 OFF-mode initial stable mirror source refused: "+checked.refusal);
    }

    if(collision_.offModeConstraints.enabled&&
       collision_.offModeConstraints.backtrack.enabled&&
       (collision_.offModeConstraints.backtrack.maxTrials==0||
        collision_.offModeConstraints.backtrack.maxTrials>8||
        !std::isfinite(collision_.offModeConstraints.backtrack.maxNormalStepInLocalEdges)||
        collision_.offModeConstraints.backtrack.maxNormalStepInLocalEdges<=0||
        collision_.offModeConstraints.backtrack.maxNormalStepInLocalEdges>1))
        throw std::invalid_argument(
            "V18 backtracking requires 1..8 bounded private candidate trials");
    details_.vertices=vertices_;
    details_.triangles=faces_;
    if(collision_.physicalExact&&symmetric_)
        firstOriginalMirrors_=inflate_.mirrors();
}

VersionedSculptBrush::VersionedSculptBrush(
    DurableCheckpoint recovered,double strength,CurvatureSettings curvature,
    CollapseSettings collapse,RefineSettings refine,
    std::size_t partitionCapacity,SculptStepValidator validator,
    SculptCollisionSettings collision)
    :flatOrder_(std::make_shared<const FlatMeshIndex>()),
     published_(recovered.index),
     firstOriginalAuthority_(collision.physicalExact?
         recovered.verifiedOriginalFidelity:nullptr),
     history_(published_.snapshot(),64,32*1024*1024,&historyRetirement_,
              elementCount(*recovered.index,true),
              elementCount(*recovered.index,false),
              recovered.verifiedOriginalFidelity),
     inflate_(strength,curvature,recovered.mirrorPairs),
     refiner_(recovered.initialVertexCount?recovered.initialVertexCount:
              elementCount(*recovered.index,true),refine,
              recovered.mirrorPairs,partitionCapacity),
     collapser_(recovered.initialVertexCount?recovered.initialVertexCount:
                elementCount(*recovered.index,true),collapse,
                recovered.mirrorPairs,partitionCapacity),
     partitionCapacity_(partitionCapacity),
     initialVertexCount_(recovered.initialVertexCount?recovered.initialVertexCount:
                         elementCount(*recovered.index,true)),
     vertices_(elementCount(*recovered.index,true)),
     faces_(elementCount(*recovered.index,false)),
     validator_(std::move(validator)),collision_(std::move(collision)),
     symmetric_(!recovered.mirrorPairs.empty()),
     contact_(collision_.quietChecksToEndContact),
     originalFidelityRequired_(recovered.originalFidelityRequired||
         static_cast<bool>(recovered.verifiedOriginalFidelity)) {
    if(!partitionCapacity_)throw std::invalid_argument("Invalid partition capacity");
    if(originalFidelityRequired_&&collision_.physicalExact&&
       !recovered.verifiedOriginalFidelity)
        throw std::logic_error(
            "Recovered physicalExact root lacks authenticated FIRST-original source ancestry");
    if(collision_.enabled&&collision_.physicalExact)
        throw std::invalid_argument(
            "Physical exact Boolean and legacy approximate collision surgery are mutually exclusive");
    if(collision_.certifiedAutoClosure&&
       (collision_.physicalExact||collision_.enabled||
        collision_.offModeConstraints.enabled))
        throw std::invalid_argument(
            "Live certified R06 AUTO is exclusive with exact, legacy and OFF-mode collision policies");
    if(collision_.offModeConstraints.enabled&&
       (collision_.physicalExact||collision_.enabled))
        throw std::invalid_argument(
            "V18 OFF-mode contact guard is mutually exclusive with V17 Boolean surgery and physicalExact Merge");
    if(collision_.offModeConstraints.enabled&&symmetric_) {
        const auto checked=auditOffModeMirrors(
            published_.snapshot(),inflate_.mirrors(),{},{},true);
        if(!checked.complete)
            throw std::invalid_argument(
                "V18 OFF-mode initial stable mirror source refused: "+checked.refusal);
    }

    if(collision_.offModeConstraints.enabled&&
       collision_.offModeConstraints.backtrack.enabled&&
       (collision_.offModeConstraints.backtrack.maxTrials==0||
        collision_.offModeConstraints.backtrack.maxTrials>8||
        !std::isfinite(collision_.offModeConstraints.backtrack.maxNormalStepInLocalEdges)||
        collision_.offModeConstraints.backtrack.maxNormalStepInLocalEdges<=0||
        collision_.offModeConstraints.backtrack.maxNormalStepInLocalEdges>1))
        throw std::invalid_argument(
            "V18 backtracking requires 1..8 bounded private candidate trials");
    details_.vertices=vertices_;
    details_.triangles=faces_;
    if(collision_.physicalExact&&recovered.verifiedOriginalFidelity) {
        // A trusted recovery may retain opaque FIRST-root authority.
        // Never re-anchor a recovered later terminal, including V12/V13.
        auto originalPairs=recovered.verifiedFirstOriginalMirrorPairs;
        if(originalPairs.empty()&&firstOriginalAuthority_->acceptedTransitions==0&&
           firstOriginalAuthority_->anchors.original==published_.snapshot())
            originalPairs=recovered.mirrorPairs;
        for(const auto& [a,b]:originalPairs) {
            if(!a||!b||
               (firstOriginalMirrors_.contains(a)&&firstOriginalMirrors_.at(a)!=b)||
               (firstOriginalMirrors_.contains(b)&&firstOriginalMirrors_.at(b)!=a))
                throw std::logic_error("Conflicting original reflected-source authority");
            firstOriginalMirrors_[a]=b;
            firstOriginalMirrors_[b]=a;
        }
    }
}

void VersionedSculptBrush::recordStepDiagnostics() noexcept {
    auto& total=resourceTotals_;
    const auto& step=lastStepResourceWork_;
    const auto sum=[](std::size_t& dst,std::size_t n) {
        if(n>std::numeric_limits<std::size_t>::max()-dst)
            dst=std::numeric_limits<std::size_t>::max();
        else dst+=n;
    };
    sum(total.attempts,1);
    if(step.path==SculptStepResourcePath::PhysicalStamp)
        sum(total.physicalAttempts,1);
    else if(step.path==SculptStepResourcePath::QueuedExact)
        sum(total.queuedExactAttempts,1);
    sum(total.attemptedPrivateCopiedBytes,step.privateCopiedBytes);
    sum(total.attemptedChangedIdReferences,step.changedIdReferences);
    sum(total.attemptedDirtyPartitions,step.finalDirtyPartitions);
    total.attemptedExact.accumulate(step.exactStage);
    total.observedAttemptMilliseconds+=step.elapsedMilliseconds;
    if(step.refused) {
        sum(total.refused,1);
        switch(step.refusalCategory) {
        case SculptRefusalCategory::CallerOrProvenance:sum(total.provenanceRefusals,1);break;
        case SculptRefusalCategory::GeometryOrTopology:sum(total.geometryRefusals,1);break;
        case SculptRefusalCategory::FiniteWorkBudget:sum(total.budgetRefusals,1);break;
        case SculptRefusalCategory::StaleExpectedRoot:sum(total.staleRefusals,1);break;
        case SculptRefusalCategory::None:break;
        }
    }else if(step.postCasFailed) {
        sum(total.postCasFailures,1);
    }else if(step.published) {
        sum(total.published,1);
        sum(total.publishedPrivateCopiedBytes,step.privateCopiedBytes);
        sum(total.publishedChangedIdReferences,step.changedIdReferences);
        sum(total.publishedDirtyPartitions,step.finalDirtyPartitions);
        total.publishedExact.accumulate(step.exactStage);
        total.observedPublishedMilliseconds+=step.elapsedMilliseconds;
    }else sum(total.unchanged,1);
}

void VersionedSculptBrush::ensureOperational() const {
    if(terminalFailure())
        throw std::logic_error(
            "Terminal post-CAS failure; original-root transaction quarantined: "+
            terminalFailureReason_);
}
void VersionedSculptBrush::injectPostCasFault(
    SculptPostCasFaultPoint point) const {
    if(collision_.postCasFaultInjection)
        collision_.postCasFaultInjection(point);
}
void VersionedSculptBrush::markPostCasFailure(
    std::exception_ptr error) {
    std::string reason="unknown exception";
    try {
        if(error)std::rethrow_exception(error);
    }catch(const std::exception& e) {
        reason=e.what();
    }catch(...) {}
    terminalFailureReason_=
        "Indexed root CAS completed, auxiliary transaction incomplete: "+reason;
    lastRejection_=terminalFailureReason_;
    lastDirtyPartitions_.clear();
    lastStepResourceWork_.published=false;
    lastStepResourceWork_.refused=false;
    lastStepResourceWork_.postCasFailed=true;
    lastStepResourceWork_.refusalCategory=SculptRefusalCategory::None;
    lastStepResourceWork_.refusal=terminalFailureReason_;
    recordStepDiagnostics();
    terminalFailure_.store(true,std::memory_order_release);
}

std::string VersionedSculptBrush::validateOriginalIntentSources(
    const std::vector<FaceId>& originals) const {
    if(originals.empty())return {};
    if(!collision_.physicalExact)
        return "Explicit first-original FaceId intent requires physicalExact mode";
    if(!firstOriginalAuthority_)
        return "No authenticated first-original source for explicit intended FaceIds";
    try {
        // This establishes caller provenance only, NOT a final terminal
        // fidelity decision. M32/M33 will rebind to the DIRECT previous
        // published lifetime and evaluate the actual finished candidate.
        (void)declareOriginalPatchIntent(
            *firstOriginalAuthority_,originals,
            symmetric_?&firstOriginalMirrors_:nullptr,
            collision_.originalIntentInput.maxDeclaredSources);
    }catch(const std::exception& error) {
        return std::string("Invalid first-original intent: ")+error.what();
    }
    return {};
}
void VersionedSculptBrush::setOriginalPatchIntent(
    std::vector<FaceId> originals) {
    ensureOperational();
    physicalIntendedOriginals_=std::move(originals);
    physicalIntentRefusal_=validateOriginalIntentSources(
        physicalIntendedOriginals_);
}

VersionedSculptBrush::BacktrackChoice
VersionedSculptBrush::probeOffModeNormalFraction(
    const std::shared_ptr<const RegionTopologyIndex>& before,
    std::span<const BrushArea> areas) {
    BacktrackChoice choice;
    const auto& policy=collision_.offModeConstraints;
    if(!policy.enabled||!policy.backtrack.enabled)return choice;
    const auto& budget=policy.backtrack;
    if(!std::isfinite(budget.maxNormalStepInLocalEdges)||
       budget.maxNormalStepInLocalEdges<=0||
       budget.maxNormalStepInLocalEdges>1)
        throw std::invalid_argument(
            "V18 normal step must have finite local edge ratio in (0,1]");
    if(budget.maxTrials==0||budget.maxTrials>8)
        throw std::invalid_argument(
            "V18 bounded backtracking requires 1..8 private trials");
    auto limits=policy.clearanceLimits;
    limits.contacts=policy.contactLimits;
    // The first unsuccessful normal proposal has *no* authority to
    // change the repository or any speculative remesher mirror state.
    // Each complete candidate gets an independent source-baseline
    // Inflate, refine, collapse and *final* M01+M03 clearance check.
    for(std::size_t attempt=0;attempt<budget.maxTrials;++attempt) {
        const double fraction=std::ldexp(1.0,-static_cast<int>(attempt));
        ++choice.work.trials;
        const auto inflated=inflate_.stamp(
            before,areas,partitionCapacity_,fraction);
        refiner_.beginStep();
        try {collapser_.beginStep();}
        catch(...) {refiner_.rollbackStep();throw;}
        bool rollbackNeeded=true;
        OffModeClearanceReport assessment;
        bool noChange=false;
        bool tooLargeNormal=false;
        std::string error;
        try {
            const auto refined=refiner_.apply(
                inflated.index,inflated.baseline);
            for(const auto& pair:refined.insertedMirrorPairs)
                collapser_.addMirrorPair(pair.first,pair.second);
            const auto collapsed=collapser_.apply(
                refined.index,inflated.baseline);
            const auto updated=collapsed.index;
            std::vector<VertexId> changedVertices=inflated.baseline.movedVertices;
            changedVertices.insert(changedVertices.end(),
                refined.insertedVertices.begin(),refined.insertedVertices.end());
            changedVertices.insert(changedVertices.end(),
                collapsed.removedVertices.begin(),collapsed.removedVertices.end());
            std::vector<FaceId> changedFaces=inflated.baseline.affectedFaces;
            changedFaces.insert(changedFaces.end(),
                refined.changedFaces.begin(),refined.changedFaces.end());
            changedFaces.insert(changedFaces.end(),
                collapsed.removedFaces.begin(),collapsed.removedFaces.end());
            changedFaces.insert(changedFaces.end(),
                collapsed.rewrittenFaces.begin(),collapsed.rewrittenFaces.end());

            choice.work.privateCopiedBytes=checkedStepSum({
                choice.work.privateCopiedBytes,
                inflated.commitStatistics.clonedPartitionBytes,
                refined.clonedPartitionBytes,
                collapsed.clonedPartitionBytes
            });
            choice.work.changedIdReferences=checkedStepSum({
                choice.work.changedIdReferences,
                changedVertices.size(),changedFaces.size()
            });
            if(choice.work.privateCopiedBytes>budget.maxTrialCopiedBytes||
               choice.work.changedIdReferences>
                   budget.maxTrialChangedIdReferences)
                throw std::length_error(
                    "V18 bounded backtracking cumulative COW/changed-ID work budget exhausted");
            // The finite endpoint predicate alone cannot exclude
            // tunnelling through a thin untouched sheet. Bound each
            // original normal step by a fraction of the PRE-STAMP
            // shortest incident edge, never the brush radius.
            for(const auto id:inflated.baseline.movedVertices) {
                const auto& beforeVertex=before->vertex(id);
                const auto afterVertex=inflated.index->vertex(id).position;
                double shortest=std::numeric_limits<double>::infinity();
                for(const auto fid:beforeVertex.incidentFaces) {
                    const auto corners=before->face(fid).vertices;
                    for(unsigned k=0;k<3;++k) {
                        if(corners[k]!=id)continue;
                        for(unsigned j=1;j<=2;++j)
                            shortest=std::min(shortest,length(
                                beforeVertex.position-
                                before->vertex(corners[(k+j)%3]).position));
                    }
                }
                if(!std::isfinite(shortest)||shortest<=0)
                    throw std::logic_error(
                        "V18 cannot certify local edge scale for large normal step");
                if(length(afterVertex-beforeVertex.position)>
                   budget.maxNormalStepInLocalEdges*shortest)
                    tooLargeNormal=true;
            }
            noChange=updated==before;
            if(!noChange&&symmetric_) {
                const auto audited=auditOffModeMirrors(
                    updated,collapser_.mirrors(),changedVertices,changedFaces);
                if(!audited.complete)
                    throw std::logic_error(
                        "V18 private mirrored trial lacks exact stable-ID source: "+
                        audited.refusal);
                for(const auto v:changedVertices) {
                    if(!before->maybeVertex(v)||updated->maybeVertex(v))continue;
                    const auto original=inflate_.mirrors().find(v);
                    if(original==inflate_.mirrors().end()||
                       updated->maybeVertex(original->second))
                        throw std::logic_error(
                            "V18 reflected trial retired only one old source mirror");
                }
            }
            if(!noChange) {
                assessment=assessOffModeMeshClearance(
                    before,updated,changedVertices,changedFaces,limits);
                choice.work.distanceTests=checkedStepSum({
                    choice.work.distanceTests,assessment.distanceTests});
                if(choice.work.distanceTests>budget.maxTrialDistanceTests)
                    throw std::length_error(
                        "V18 bounded backtracking distance-work budget exhausted");
            }
            collapser_.rollbackStep();
            refiner_.rollbackStep();
            rollbackNeeded=false;
        }catch(const std::length_error& e) {
            error=std::string("V18 bounded backtracking budget refusal: ")+e.what();
        }catch(const std::logic_error& e) {
            error=std::string("V18 bounded backtracking geometry refusal: ")+e.what();
        }catch(...) {
            if(rollbackNeeded) {
                collapser_.rollbackStep();
                refiner_.rollbackStep();
            }
            throw;
        }
        if(rollbackNeeded) {
            collapser_.rollbackStep();
            refiner_.rollbackStep();
        }
        if(!error.empty()) {
            choice.refusal=std::move(error);
            break;
        }
        if(noChange&&attempt==0) {
            choice.work.chosenNormalFraction=1;
            return choice; // Genuine unchanged original Step
        }
        if(!noChange&&assessment.allowed()&&!tooLargeNormal) {
            choice.fraction=fraction;
            choice.work.chosenNormalFraction=fraction;
            return choice;
        }
        ++choice.work.declined;
        // Only conclusive TOO-CLOSE or definite new CONTACT can be
        // addressed by smaller normal displacement. No retry of an
        // UNKNOWN, bad source or exhausted finite certificate.
        if(noChange||
           (!assessment.allowed()&&
            assessment.status!=OffModeClearanceStatus::TooClose&&
            assessment.status!=OffModeClearanceStatus::Contact)) {
            choice.refusal="V18 bounded backtracking refuses uncertain private clearance";
            break;
        }
    }
    choice.work.refused=true;
    if(choice.refusal.empty())
        choice.refusal="V18 bounded backtracking exhausted without a certified normal displacement";
    // The normal full-strength pipeline runs once more, but is FORCED to
    // refuse pre-CAS. This retains the existing single rejected Step,
    // attempted-work accounting, diagnostic and FIFO rollback contracts.
    return choice;
}

void VersionedSculptBrush::stamp(
    Mesh&,std::span<const BrushArea> areas) {
    const auto old=published_.snapshot();
    ensureOperational();
    const auto start=Clock::now();
    lastStepResourceWork_={};
    lastStepResourceWork_.path=SculptStepResourcePath::PhysicalStamp;
    // M41: the guard remains active through the entire private V13,
    // c1/c2/c3/M03/d2 and M26/M21 source-replay work. Actual repeated
    // queries cost work again; an immutable cached result costs nothing.
    ScopedExactStageWork countExactGeometry(
        lastStepResourceWork_.exactStage);
    // Scope is resolved on the worker at Begin and is never filled from
    // candidate geometry. Bad source input must not terminate FIFO.
    if(!collision_.physicalExact&&history_.currentFidelity()) {
        // Trusted protected history must not be mutated through a legacy
        // proof-less stamp, which would otherwise fail M29 AFTER CAS.
        lastDirtyPartitions_.clear();
        lastRejection_="Authenticated source history needs a certified physicalExact Step";
        details_={};
        details_.areas.assign(areas.begin(),areas.end());
        details_.vertices=vertices_;
        details_.triangles=faces_;
        timings_={};
        ++rejected_;
        ++completed_;
        lastStepResourceWork_.refused=true;
        lastStepResourceWork_.refusalCategory=SculptRefusalCategory::CallerOrProvenance;
        lastStepResourceWork_.refusal=lastRejection_;
        lastStepResourceWork_.elapsedMilliseconds=
            seconds(start,Clock::now())*1000;
        recordStepDiagnostics();
        if(collision_.physicalExact||collision_.enabled)
            contact_.observe(std::nullopt);
        return;
    }
    if(!physicalIntentRefusal_.empty()) {
        lastDirtyPartitions_.clear();
        lastRejection_=physicalIntentRefusal_;
        details_={};
        details_.areas.assign(areas.begin(),areas.end());
        details_.vertices=vertices_;
        details_.triangles=faces_;
        timings_={};
        ++rejected_;
        ++completed_;
        lastStepResourceWork_.refused=true;
        lastStepResourceWork_.refusalCategory=SculptRefusalCategory::CallerOrProvenance;
        lastStepResourceWork_.refusal=lastRejection_;
        lastStepResourceWork_.elapsedMilliseconds=
            seconds(start,Clock::now())*1000;
        recordStepDiagnostics();
        // An invalid caller scope is not an accepted Clean V13 result.
        // It must interrupt the consecutive quiet sequence even though
        // the invalid request is declined before private deformation.
        if(collision_.physicalExact||collision_.enabled)
            contact_.observe(std::nullopt);
        return;
    }
    const auto trial=probeOffModeNormalFraction(old,areas);
    lastStepResourceWork_.backtracking=trial.work;
    const auto inflated=inflate_.stamp(
        old,areas,partitionCapacity_,trial.fraction);
    const auto afterInflate=Clock::now();
    refiner_.beginStep();
    try {collapser_.beginStep();}
    catch(...) {refiner_.rollbackStep();throw;}
    bool didPublish=false;
    try {
    const auto refined=refiner_.apply(inflated.index,inflated.baseline);
    // Collapse must see V6's newly inserted stable mirror IDs immediately.
    for(const auto& pair:refined.insertedMirrorPairs)
        collapser_.addMirrorPair(pair.first,pair.second);
    const auto afterRefine=Clock::now();
    const auto collapsed=collapser_.apply(refined.index,inflated.baseline);
    const auto afterCollapse=Clock::now();
    lastStepResourceWork_.stageWall.initialGeometryMs=
        seconds(start,afterCollapse)*1000;
    std::shared_ptr<const RegionTopologyIndex> updated=collapsed.index;

    // Identify every face-owner partition whose triangles, flat normals or
    // canonical edge lines may have changed, using only old/new local halos.
    // This set spans the entire stamp even when split/flip and collapse both
    // occur, so a rendered frame never receives a half-updated surface.
    std::vector<VertexId> changedVertices=inflated.baseline.movedVertices;
    changedVertices.insert(changedVertices.end(),
        refined.insertedVertices.begin(),refined.insertedVertices.end());
    changedVertices.insert(changedVertices.end(),
        collapsed.removedVertices.begin(),collapsed.removedVertices.end());
    std::vector<FaceId> changedFaces=inflated.baseline.affectedFaces;
    changedFaces.insert(changedFaces.end(),
        refined.changedFaces.begin(),refined.changedFaces.end());
    changedFaces.insert(changedFaces.end(),
        collapsed.removedFaces.begin(),collapsed.removedFaces.end());
    changedFaces.insert(changedFaces.end(),
        collapsed.rewrittenFaces.begin(),collapsed.rewrittenFaces.end());
    // M23: collect the V13/V16 full-candidate classification, but
    // defer recording a Clean until *all* subsequent validation of the
    // proposed physical Step has succeeded. Even a fully classified
    // Clean candidate can be vetoed later by M21 first-original policy.
    // A conclusively proven Contact is still a Contact if a later
    // source weld or intent validation refuses: it cannot end an
    // existing contact episode or authorize a cached Boolean.
    bool classifiedEpisodeStep=false;
    std::optional<bool> episodeClassification;
    // M18: a physical stamp may choose the STRICT exact Boolean path.
    // Both the physical displacement/remesh and the certified cut/join
    // remain private. A single original-root CAS occurs only below.
    std::optional<ExactCutStep> physicalExactUnion;
    std::vector<MirrorPair> physicalNetAdded,physicalNetRemoved;
    std::string physicalRefusal;
    if(collision_.physicalExact&&updated!=old) {
        ++collisionChecked_;
        try {
            const auto discovered=assessPostSculptOriginalContacts(
                {old,updated,changedVertices,changedFaces},symmetric_,
                collision_.physicalSourceLimits,
                symmetric_?&collapser_.mirrors():nullptr);
            classifiedEpisodeStep=true;
            if(discovered.status==SculptSourceContactStatus::Refused) {
                physicalRefusal="Physical exact source refusal: "+discovered.refusal;
            }else if(discovered.status==SculptSourceContactStatus::Clean) {
                episodeClassification=false;
            }else {
                episodeClassification=true;
                auto limits=collision_.physicalExactLimits;
                limits.requireMirrors=symmetric_;
                auto exact=prepareExactCutStep(
                    discovered.originalCandidate,discovered.contacts,limits,
                    symmetric_?&collapser_.mirrors():nullptr);
                if(!exact.complete||!exact.final||
                   exact.expected!=updated||!exact.commit.revision||
                   exact.commit.revision.get()!=&exact.final->revision()) {
                    physicalRefusal="Physical exact Boolean refused: "+exact.refusal;
                }else {
                    // The candidate may already contain new/refined or
                    // collapsed historical mirror IDs. Bring BOTH staging
                    // maps into candidate shape BEFORE applying the exact
                    // cut/join's additional orbits.
                    if(symmetric_) {
                        for(const auto& [a,b]:collapsed.removedMirrorPairs)
                            refiner_.removeMirrorPair(a,b);
                        for(const auto& [a,b]:exact.removedMirrors) {
                            refiner_.removeMirrorPair(a,b);
                            collapser_.removeMirrorPair(a,b);
                        }
                        for(const auto& [a,b]:exact.addedMirrors) {
                            refiner_.addMirrorPair(a,b);
                            collapser_.addMirrorPair(a,b);
                        }
                        const auto& now=collapser_.mirrors();
                        const auto& was=inflate_.mirrors();
                        if(now!=refiner_.mirrors()||
                           now.size()!=elementCount(*exact.final,true))
                            throw std::logic_error(
                                "Physical final mirrored source maps are incomplete or disagree");
                        // Verify every live orbit and its exact reflected
                        // XYZ, including a genuine (id,id) fixed point.
                        for(const auto& [a,b]:now) {
                            if(!now.contains(b)||now.at(b)!=a||
                               !exact.final->maybeVertex(a)||
                               !exact.final->maybeVertex(b))
                                throw std::logic_error(
                                    "Physical final mirror orbit is missing a live source ID");
                            const auto p=exact.final->vertex(a).position,
                                       q=exact.final->vertex(b).position;
                            if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
                               ((a==b)!=(p.x==0)))
                                throw std::logic_error(
                                    "Physical final mirror source XYZ is not exactly reflected");
                            // Historical original live IDs cannot silently
                            // switch correspondences in this transaction.
                            if(was.contains(a)&&was.at(a)!=b)
                                throw std::logic_error(
                                    "Physical union changed historical mirror correspondence");
                            if(a<=b&&!was.contains(a))
                                physicalNetAdded.emplace_back(a,b);
                        }
                        for(const auto& [a,b]:was) {
                            if(a>b)continue;
                            const bool liveA=exact.final->maybeVertex(a)!=nullptr,
                                       liveB=exact.final->maybeVertex(b)!=nullptr;
                            if(liveA!=liveB||
                               (liveA&&(!now.contains(a)||now.at(a)!=b)))
                                throw std::logic_error(
                                    "Physical union retired only part of a historical mirror orbit");
                            if(!liveA)physicalNetRemoved.emplace_back(a,b);
                        }
                        // All old live IDs must be present; all removed
                        // historical IDs have one net retirement journal,
                        // even if the proposed remesh already removed them.
                        std::sort(physicalNetAdded.begin(),physicalNetAdded.end());
                        std::sort(physicalNetRemoved.begin(),physicalNetRemoved.end());
                    }
                    changedVertices.insert(changedVertices.end(),
                        exact.addedVertices.begin(),exact.addedVertices.end());
                    changedVertices.insert(changedVertices.end(),
                        exact.retiredVertices.begin(),exact.retiredVertices.end());
                    changedFaces.insert(changedFaces.end(),
                        exact.addedFaces.begin(),exact.addedFaces.end());
                    changedFaces.insert(changedFaces.end(),
                        exact.rewrittenFaces.begin(),exact.rewrittenFaces.end());
                    changedFaces.insert(changedFaces.end(),
                        exact.retiredFaces.begin(),exact.retiredFaces.end());
                    updated=exact.final;
                    physicalExactUnion=std::move(exact);
                }
            }
        }catch(const std::length_error& error) {
            physicalRefusal=std::string("Physical exact work budget: ")+error.what();
        }catch(const std::logic_error& error) {
            physicalRefusal=std::string("Physical exact source/map refusal: ")+error.what();
        }
    }
    std::vector<MirrorPair> repairedAddedMirrors,repairedRemovedMirrors;
    // R07 production AUTO is deliberately independent of the M16 frozen
    // read-only inspector. Run the SAME trusted R01->R03->M03->M04C
    // certification on the worker's complete unpublished post-stamp root.
    // If anything refuses, rollback the ENTIRE brush Step below; never
    // publish a partial cut, a half-updated mirrored orbit or an open mesh.
    std::optional<CertifiedPrivateAutoResult> certifiedAutoRepair;
    std::optional<CertifiedAutoPublicationDelta> certifiedAutoDelta;
    std::string certifiedAutoRefusal;
    if(collision_.certifiedAutoClosure&&updated!=old) {
        ++collisionChecked_;
        try {
            const auto discovered=assessPostSculptRepairContacts(
                {old,updated,changedVertices,changedFaces},symmetric_,
                collision_.certifiedAutoSourceLimits,
                symmetric_?&collapser_.mirrors():nullptr);
            lastStepResourceWork_.autoDetectionWork=discovered.work;
            classifiedEpisodeStep=true;
            if(discovered.status==SculptSourceContactStatus::Refused) {
                certifiedAutoRefusal="R07 live AUTO source refusal: "+
                    discovered.refusal;
            }else if(discovered.status==SculptSourceContactStatus::Clean) {
                episodeClassification=false;
            }else {
                episodeClassification=true;
                // R01's new-contact detector is deliberately localized,
                // but R02/R03/M03 need COMPLETE original contact evidence.
                // Escalate only when it finds a genuine new transverse
                // intersection. No whole-mesh scan is done for Clean
                // stamps, and protected Merge keeps its own source policy.
                const auto complete=assessPostSculptOriginalContacts(
                    {old,updated,changedVertices,changedFaces},symmetric_,
                    collision_.certifiedAutoCompleteSourceLimits,
                    symmetric_?&collapser_.mirrors():nullptr,16,false);
                if(!complete.ready()||
                   complete.originalCandidate!=updated)
                    throw std::logic_error(
                        "R07 live AUTO complete original contact graph refused: "+
                        (complete.refusal.empty()?
                         std::string("local/full contact evidence disagrees"):
                         complete.refusal));
                std::cerr<<"[repair-r07] CONTACT local_pairs="
                         <<discovered.contacts.size()
                         <<" all_source_pairs="<<complete.contacts.size()
                         <<" complete_box_tests="<<complete.work.boxesTested
                         <<" (repair stages remain private)\n";
                const auto source=complete.originalCandidate;
                const auto preview=previewApproximateExcision(
                    *source,complete.contacts);
                if(!preview.complete)
                    throw std::logic_error(
                        "R07 live AUTO incomplete source-backed R03 preview: "+
                        preview.diagnostic);
                const auto cut=applyPrivateExcision(source,preview);
                auto plan=planPrivateAutomaticClosure(
                    *source,cut,preview,complete.contacts);
                if(!plan.ready())
                    throw std::logic_error(
                        "R07 live AUTO M03 closure plan refused: "+plan.diagnostic);
                CertifiedPrivateAutoLimits limits;
                limits.closure.requireReflectedGeometry=symmetric_;
                limits.closure.staging.useUnifiedZipper=
                    collision_.certifiedAutoUnifiedZipper;
                auto result=applyCertifiedPrivateAutomaticClosure(
                    source,cut,preview,plan,complete.contacts,limits);
                if(!result.index)
                    throw std::logic_error(
                        "R07 certified AUTO produced no final closed source root");
                auto delta=inspectCertifiedAutoPublication(
                    *source,*result.index,
                    symmetric_?&collapser_.mirrors():nullptr,
                    limits.closure.maxAuditedFaces);
                // The publication delta is a second, independent mirror-ID
                // gate. All edits remain inside the rollback-able private
                // maps until one complete root passes all Step validation.
                for(const auto& [a,b]:delta.removedMirrors) {
                    refiner_.removeMirrorPair(a,b);
                    collapser_.removeMirrorPair(a,b);
                }
                for(const auto& [a,b]:delta.addedMirrors) {
                    refiner_.addMirrorPair(a,b);
                    collapser_.addMirrorPair(a,b);
                }
                repairedAddedMirrors=delta.addedMirrors;
                repairedRemovedMirrors=delta.removedMirrors;
                changedVertices.insert(changedVertices.end(),
                    delta.changedVertices.begin(),delta.changedVertices.end());
                changedFaces.insert(changedFaces.end(),
                    delta.changedFaces.begin(),delta.changedFaces.end());
                updated=result.index;
                certifiedAutoDelta=std::move(delta);
                certifiedAutoRepair=std::move(result);
            }
        }catch(const std::length_error& error) {
            certifiedAutoRefusal=std::string(
                "R07 live AUTO finite work refusal: ")+error.what();
        }catch(const std::exception& error) {
            certifiedAutoRefusal=std::string(
                "R07 live AUTO geometry/provenance refusal: ")+error.what();
        }
    }
    // A selected collision policy first classifies the FULL post-remesh
    // proposal. When supported, it may supply a privately repaired root.
    // The V17c validator below observes that final root and expanded dirty
    // set, never a partly repaired intermediate revision.
    std::string collisionRefusal;
    std::optional<RepairSurgeryResult> collisionRepair;
    if(collision_.enabled&&updated!=old) {
        ++collisionChecked_;
        SculptCollisionEvaluation judged;
        try {
            std::vector<FaceId> seeds;
            for(const auto id:collision_.isolatedComponentSeeds)
                if(updated->maybeFace(id))seeds.push_back(id);
            judged=evaluateSculptCollisionCandidate(
                {old,updated,std::span<const VertexId>(changedVertices),
                 std::span<const FaceId>(changedFaces)},symmetric_,collision_,seeds,
                symmetric_?&collapser_.mirrors():nullptr);
        }catch(const std::length_error& error) {
            collisionRefusal=error.what();
        }catch(const std::logic_error& error) {
            collisionRefusal=error.what();
        }
        // A failed or ambiguous classifier cannot establish a clean interval.
        // Hysteresis is diagnostic: the full V13-V16 decision still runs on
        // EVERY proposed Step; no previous refusal/approval is reused.
        const bool conclusive=collisionRefusal.empty()&&
            judged.refusal.empty()&&
            (judged.plan.status==RepairPlanStatus::NoChanges||
             judged.plan.status==RepairPlanStatus::Ready);
        classifiedEpisodeStep=true;
        if(conclusive)
            episodeClassification=judged.plan.intersections>0;
        if(collisionRefusal.empty())collisionRefusal=judged.refusal;
        if(collisionRefusal.empty()&&judged.repair) {
            // All changes to both speculative mirror maps are journaled.
            // Refusal by the subsequent V17c validator reverses them with
            // no full-map copy and no changes to Inflate or the repository.
            repairedAddedMirrors=std::move(judged.insertedMirrors);
            repairedRemovedMirrors=std::move(judged.retiredMirrors);
            for(const auto& [a,b]:repairedRemovedMirrors) {
                collapser_.removeMirrorPair(a,b);
                refiner_.removeMirrorPair(a,b);
            }
            for(const auto& [a,b]:repairedAddedMirrors) {
                collapser_.addMirrorPair(a,b);
                refiner_.addMirrorPair(a,b);
            }
            collisionRepair=std::move(judged.repair);
            updated=collisionRepair->index;
            changedVertices.insert(changedVertices.end(),
                collisionRepair->insertedVertices.begin(),
                collisionRepair->insertedVertices.end());
            changedVertices.insert(changedVertices.end(),
                collisionRepair->retiredVertices.begin(),
                collisionRepair->retiredVertices.end());
            changedFaces.insert(changedFaces.end(),
                collisionRepair->insertedFaces.begin(),
                collisionRepair->insertedFaces.end());
            changedFaces.insert(changedFaces.end(),
                collisionRepair->retiredFaces.begin(),
                collisionRepair->retiredFaces.end());
            for(const auto& contour:judged.plan.contours)
                changedFaces.insert(changedFaces.end(),
                    contour.survivingFaces.begin(),
                    contour.survivingFaces.end());
        }
    }
    // The decision examines the complete unpublished post-refine/collapse
    // Step and its exact dirty face/vertex set. Classification budget and
    // geometric refusals raised by this opt-in callback are recoverable.
    std::string rejection=!physicalRefusal.empty()?
        std::move(physicalRefusal):
        !certifiedAutoRefusal.empty()?
            std::move(certifiedAutoRefusal):std::move(collisionRefusal);
    if(rejection.empty()&&validator_&&updated!=old) {
        try {
            rejection=validator_({old,updated,
                std::span<const VertexId>(changedVertices),
                std::span<const FaceId>(changedFaces)});
        }catch(const std::length_error& error) {
            rejection=error.what();
        }catch(const std::logic_error& error) {
            rejection=error.what();
        }
    }
    // M32: exact-final first-original policy. The old *PUBLISHED* root
    // and its matching M29 certificate are the ONLY permissible predecessor.
    // M28 validates every actual private source stage and seals ONE external
    // successor. No intermediate Inflate/cut root is ever audited or published.
    lastStepResourceWork_.stageWall.sourceGeometryMs=
        seconds(afterCollapse,Clock::now())*1000;
    const auto proofStarted=Clock::now();
    std::shared_ptr<const OriginalPatchLifetime> finalFidelity;
    if(rejection.empty()&&collision_.physicalExact&&updated!=old) {
        try {
            const auto previous=history_.currentFidelity();
            if(!previous||!firstOriginalAuthority_||
               previous->lineage.terminal!=old||
               previous->anchors.original!=firstOriginalAuthority_->anchors.original)
                throw std::logic_error(
                    "Physical source Step lacks current authenticated FIRST-original lifetime");
            const auto& budget=collision_.originalTerminal;
            verifyOriginalPatchPublicationRoot(
                *previous,old,budget.maxLivingDescendants);
            // A caller may continue naming an already intended original
            // across successive independent stamps. Only sources with an
            // M26-sealed WHOLE Buried/Outside retirement can be omitted;
            // all other names must still be live, original and explicit.
            // This never infers a new intention from XYZ or the brush area.
            std::set<FaceId> liveOrigins;
            for(const auto& [id,faces]:previous->lineage.living)
                if(!faces.empty())liveOrigins.insert(id);
            std::vector<FaceId> activeIntended;
            activeIntended.reserve(physicalIntendedOriginals_.size());
            for(const auto id:physicalIntendedOriginals_) {
                if(liveOrigins.contains(id))
                    activeIntended.push_back(id);
                else if(!std::binary_search(
                            previous->certifiedRetiredOriginals.begin(),
                            previous->certifiedRetiredOriginals.end(),id))
                    throw std::logic_error(
                        "Physical intended original is absent without certified Buried/Outside retirement");
            }
            const auto declaration=declareOriginalPatchIntent(
                *previous,activeIntended,
                symmetric_?&firstOriginalMirrors_:nullptr,
                collision_.originalIntentInput.maxDeclaredSources);
            auto candidate=advanceCertifiedPhysicalStampLifetime(
                *previous,inflated,refined,collapsed,
                physicalExactUnion?&*physicalExactUnion:nullptr,
                symmetric_?&firstOriginalMirrors_:nullptr,
                budget.maxCumulativeFaceEvidence,budget.maxLivingDescendants,
                budget.maxInflateVertices,budget.maxInflateFaces);
            // Preflight history authentication, not just geometric policy:
            // a source-invalid candidate must not throw after successful CAS.
            verifyOriginalPatchPublicationTransition(
                *previous,candidate,old,updated,budget.maxLivingDescendants);
            // M30 final pre-CAS acceptance: the actual M29 history append
            // always revalidates its immutable default proof-work bound.
            // A caller may configure a larger M21 audit budget, but must
            // NEVER make a successful CAS depend on a post-CAS source
            // certification that the history's bound will reject.
            verifyOriginalPatchPublicationTransition(
                *previous,candidate,old,updated);
            (void)evaluateOriginalPatchIntent(
                *previous,candidate,declaration,
                budget.maximumProtectedDeviationInEdges,
                budget.maxDistanceTests,budget.maxLivingDescendants);
            finalFidelity=std::make_shared<const OriginalPatchLifetime>(
                std::move(candidate));
        }catch(const std::exception& error) {
            rejection=std::string("Physical first-original terminal policy refused: ")+
                      error.what();
        }
    }
    lastStepResourceWork_.stageWall.originalProofMs=
        seconds(proofStarted,Clock::now())*1000;
    const auto admissionStarted=Clock::now();
    // M40: measure the ENTIRE would-be external Step, including cloned
    // partitions from every private Inflate/refine/collapse, optional V13
    // cut/refinement/seam join, and V16 repair. Never mistake the one final
    // COW delta for all intermediate memory-copy work. This admission
    // executes after candidate source validation, but before diagnostic
    // Clean credit, the indexed-root CAS, and any durable mirror/history.
    std::vector<PartitionId> finalDirty;
    if(rejection.empty()&&updated!=old) {
        try {
            auto& measured=lastStepResourceWork_;
            measured.privateCopiedBytes=checkedStepSum({
                trial.work.privateCopiedBytes,
                inflated.commitStatistics.clonedPartitionBytes,
                refined.clonedPartitionBytes,
                collapsed.clonedPartitionBytes,
                physicalExactUnion?physicalExactUnion->copiedPayloadBytes:0,
                collisionRepair?
                    collisionRepair->commit.statistics.clonedPartitionBytes:0
            });
            measured.changedIdReferences=checkedStepSum({
                trial.work.changedIdReferences,
                changedVertices.size(),changedFaces.size()});
            finalDirty=dirtyRenderPartitions(
                *old,*updated,changedVertices,changedFaces);
            measured.finalDirtyPartitions=finalDirty.size();
            measured.evaluated=true;
            if(collision_.physicalExact||
               collision_.offModeConstraints.enabled)
                rejection=refuseExcessStepWork(
                    measured,collision_.stepResources);
        }catch(const std::length_error& e) {
            rejection=std::string("Aggregate private Step resource refusal: ")+
                      e.what();
        }catch(const std::logic_error& e) {
            rejection=std::string("Aggregate private Step resource refusal: ")+
                      e.what();
        }
    }
    // Failed M04 probes are still genuine private COW work measured
    // above; force the one terminal refusal only AFTER billing all
    // attempted bytes and changed stable-ID references.
    if(rejection.empty()&&!trial.refusal.empty())
        rejection=trial.refusal;
    // M41–M45: true geometry, contour/seam, original seven-witness,
    // protected feature, reflected-source and authenticated history
    // preflight work from the ENTIRE private candidate is admitted
    // BEFORE final-root CAS or any quiet Clean credit.
    if(rejection.empty()&&updated!=old&&collision_.physicalExact) {
        rejection=refuseExactStageWork(lastStepResourceWork_.exactStage,
                                      collision_.exactStageLimits);
    }
    // V18-M02 evaluates the ACTUAL complete private post-remesh root
    // after M40 has measured private copy/dirty work, but BEFORE the
    // original CAS, mirror/history publication or Clean episode credit.
    // Thus a veto keeps its true attempted Step costs and rolls back.
    if(rejection.empty()&&collision_.offModeConstraints.enabled&&updated!=old) {
        ++collisionChecked_;
        if(symmetric_) {
            const auto audited=auditOffModeMirrors(
                updated,collapser_.mirrors(),changedVertices,changedFaces);
            if(!audited.complete)
                rejection="V18 OFF-mode mirrored terminal refusal: "+audited.refusal;
            for(const auto v:changedVertices) {
                if(!rejection.empty())break;
                if(!old->maybeVertex(v)||updated->maybeVertex(v))continue;
                const auto original=inflate_.mirrors().find(v);
                if(original==inflate_.mirrors().end()||
                   updated->maybeVertex(original->second))
                    rejection="V18 OFF-mode mirrored terminal retired only one old mirror";
            }
        }
        auto limits=collision_.offModeConstraints.clearanceLimits;
        limits.contacts=collision_.offModeConstraints.contactLimits;
        const auto assessment=rejection.empty()?
            assessOffModeMeshClearance(
                old,updated,changedVertices,changedFaces,limits):
            OffModeClearanceReport{};
        if(rejection.empty()&&!assessment.allowed())
            rejection="V18 OFF-mode collision constraint refused: "+
                (assessment.reason.empty()?
                 std::string("uncertain private clearance"):assessment.reason);
    }
    // M23: exactly ONE episode observation per classified full physical
    // proposal. A refused conclusive Contact remains known Contact;
    // a refused Clean is UNKNOWN rather than permission to advance the
    // consecutive quiet count. Any uncertain / exhausted classification
    // breaks the run, but never fabricates a Clean or ends the episode.
    lastStepResourceWork_.stageWall.admissionMs=
        seconds(admissionStarted,Clock::now())*1000;
    if(classifiedEpisodeStep)
        contact_.observe((!rejection.empty()&&episodeClassification==false)?
                         std::nullopt:episodeClassification);
    if(!rejection.empty()) {
        collapser_.rollbackStep();
        refiner_.rollbackStep();
        lastDirtyPartitions_.clear();
        lastRejection_=std::move(rejection);
        ++rejected_;
        if(collision_.physicalExact&&!physicalExactUnion&&updated!=old)
            ++rejectedUnions_;
        details_={};
        details_.areas.assign(areas.begin(),areas.end());
        details_.influenced=inflated.candidates;
        details_.vertices=vertices_;
        details_.triangles=faces_;
        timings_={seconds(start,afterInflate),seconds(afterInflate,afterRefine),
                  seconds(afterRefine,afterCollapse),0};
        ++completed_;
        lastStepResourceWork_.refused=true;
        lastStepResourceWork_.refusalCategory=lastRejection_.find("budget")!=std::string::npos||
            lastRejection_.find("exhaust")!=std::string::npos||
            lastRejection_.find("limit")!=std::string::npos?
                SculptRefusalCategory::FiniteWorkBudget:
            lastRejection_.find("first-original")!=std::string::npos?
                SculptRefusalCategory::CallerOrProvenance:
                SculptRefusalCategory::GeometryOrTopology;
        lastStepResourceWork_.refusal=lastRejection_;
        lastStepResourceWork_.elapsedMilliseconds=
            seconds(start,Clock::now())*1000;
        recordStepDiagnostics();
        return;
    }
    // Private verification and its bounded work have ended; nothing
    // after publication may be retroactively billed to this admission.
    countExactGeometry.stop();
    const auto publicationStarted=Clock::now();
    lastDirtyPartitions_=std::move(finalDirty);

    // All geometry and connectivity are already fully validated. Publish
    // the exact index + mesh revision pair atomically once per completed
    // Step, never publishing partially refined intermediate topology.
    if(updated!=old) {
        postCasInFlight_.store(true,std::memory_order_release);
        if(!published_.publishIfCurrent(old,updated))
            throw std::logic_error("Single writer lost indexed root");
        didPublish=true;
        injectPostCasFault(SculptPostCasFaultPoint::AfterRootCas);
        ++publishedRevisions_;
    }
    // M18 publishes ONLY the final physical-to-exact mirror net. IDs
    // inserted and then retired inside this ONE private Step never reach
    // Inflate, persistent history or durable checkpoints.
    if(physicalExactUnion) {
        for(const auto& [a,b]:physicalNetRemoved)
            inflate_.removeMirrorPair(a,b);
        for(const auto& [a,b]:physicalNetAdded)
            inflate_.addMirrorPair(a,b);
    }else {
    // Mirror-ID changes are cheap local deltas, not a global map copy.
    for(const auto& pair:refined.insertedMirrorPairs) {
        inflate_.addMirrorPair(pair.first,pair.second);
        // V6 itself and V7's pre-collapse state already own this pair.
    }
    for(const auto& pair:collapsed.removedMirrorPairs) {
        inflate_.removeMirrorPair(pair.first,pair.second);
        refiner_.removeMirrorPair(pair.first,pair.second);
    }
    for(const auto& [a,b]:repairedRemovedMirrors)
        inflate_.removeMirrorPair(a,b);
    for(const auto& [a,b]:repairedAddedMirrors)
        inflate_.addMirrorPair(a,b);
    }
    if(didPublish)
        injectPostCasFault(SculptPostCasFaultPoint::AfterMirrorState);
    vertices_+=refined.insertedVertices.size();
    faces_+=2*refined.insertedVertices.size();
    vertices_-=collapsed.removedVertices.size();
    faces_-=collapsed.removedFaces.size();
    if(physicalExactUnion) {
        const auto adjust=[](std::size_t count,std::ptrdiff_t delta) {
            if(delta<0) {
                const auto n=static_cast<std::size_t>(-(delta+1))+1;
                if(count<n)throw std::logic_error("Physical union live ID count underflow");
                return count-n;
            }
            if(static_cast<std::size_t>(delta)>
               std::numeric_limits<std::size_t>::max()-count)
                throw std::logic_error("Physical union live ID count overflow");
            return count+static_cast<std::size_t>(delta);
        };
        vertices_=adjust(vertices_,physicalExactUnion->vertexCountDelta);
        faces_=adjust(faces_,physicalExactUnion->faceCountDelta);
        ++collisionRepaired_;
        ++completedUnions_;
    }
    if(certifiedAutoRepair&&certifiedAutoDelta) {
        // The R06 pass includes both old survivor replacements and entirely
        // new topology. Count the ACTUAL final root, not just its zipper
        // vertex count or source excision estimate.
        vertices_=certifiedAutoDelta->liveVertices;
        faces_=certifiedAutoDelta->liveFaces;
        ++collisionRepaired_;
    }
    if(collisionRepair) {
        vertices_+=collisionRepair->insertedVertices.size();
        vertices_-=collisionRepair->retiredVertices.size();
        faces_+=collisionRepair->insertedFaces.size();
        faces_-=collisionRepair->retiredFaces.size();
        ++collisionRepaired_;
    }
    modifiedPartitions_+=inflated.commitStatistics.modifiedPartitions+
                         refined.modifiedPartitions+collapsed.modifiedPartitions;
    copiedDirectoryNodes_+=inflated.commitStatistics.copiedDirectoryNodes+
                            refined.copiedDirectoryNodes+
                            collapsed.copiedDirectoryNodes;
    clonedPartitionBytes_+=inflated.commitStatistics.clonedPartitionBytes+
                           refined.clonedPartitionBytes+collapsed.clonedPartitionBytes;
    if(physicalExactUnion) {
        modifiedPartitions_+=physicalExactUnion->commit.statistics.modifiedPartitions;
        copiedDirectoryNodes_+=physicalExactUnion->commit.statistics.copiedDirectoryNodes;
        clonedPartitionBytes_+=physicalExactUnion->copiedPayloadBytes;
    }
    if(certifiedAutoRepair) {
        // R06's multi-stage private COW cost is separately bounded by its
        // repair budgets. The final dirty owners are already accounted by
        // dirtyRenderPartitions and the single-CAS history transition.
        modifiedPartitions_+=lastDirtyPartitions_.size();
    }
    if(collisionRepair) {
        modifiedPartitions_+=collisionRepair->commit.statistics.modifiedPartitions;
        copiedDirectoryNodes_+=collisionRepair->commit.statistics.copiedDirectoryNodes;
        clonedPartitionBytes_+=collisionRepair->commit.statistics.clonedPartitionBytes;
    }
    // One COW root per actual geometry-changing Step, within both a
    // fixed entry count and copied-payload budget. Mirror changes are stored
    // only as incremental transitions, never a full per-stamp map copy.
    auto addedMirrors=physicalExactUnion?
        physicalNetAdded:refined.insertedMirrorPairs;
    auto removedMirrors=physicalExactUnion?
        physicalNetRemoved:collapsed.removedMirrorPairs;
    if(!physicalExactUnion) {
    addedMirrors.insert(addedMirrors.end(),repairedAddedMirrors.begin(),
                        repairedAddedMirrors.end());
    removedMirrors.insert(removedMirrors.end(),repairedRemovedMirrors.begin(),
                          repairedRemovedMirrors.end());
    }
    // A conclusively unchanged stamp is not an external source Step and
    // must not offer null credentials to tracked M29 history.
    if(updated!=old) {
        history_.append(updated,lastDirtyPartitions_,
                        std::move(addedMirrors),std::move(removedMirrors),
                        lastStepResourceWork_.privateCopiedBytes,
                        vertices_,faces_,std::move(finalFidelity));
        injectPostCasFault(SculptPostCasFaultPoint::AfterHistoryAppend);
    }
    details_.areas.assign(areas.begin(),areas.end());
    details_.influenced=inflated.candidates;
    details_.moved=inflated.moved;
    details_.preStampCopiedVertices=0;
    details_.splitPairs=refined.statistics.splitPairs;
    details_.flipPairs=refined.statistics.flipPairs;
    details_.collapsedPairs=collapsed.statistics.accepted;
    details_.vertices=vertices_;
    details_.triangles=faces_;
    timings_={seconds(start,afterInflate),seconds(afterInflate,afterRefine),
              seconds(afterRefine,afterCollapse),0};
    ++completed_;
    lastRejection_.clear();
    lastStepResourceWork_.published=updated!=old;
    lastStepResourceWork_.elapsedMilliseconds=
        seconds(start,Clock::now())*1000;
    lastStepResourceWork_.stageWall.publicationAndHistoryMs=
        seconds(publicationStarted,Clock::now())*1000;
    collapser_.commitStep();
    refiner_.commitStep();
    if(didPublish)
        injectPostCasFault(SculptPostCasFaultPoint::AfterAuxCommit);
    recordStepDiagnostics();
    postCasInFlight_.store(false,std::memory_order_release);
    }catch(...) {
        if(didPublish) {
            lastStepResourceWork_.elapsedMilliseconds=
                seconds(start,Clock::now())*1000;
            markPostCasFailure(std::current_exception());
        }else {
            collapser_.rollbackStep();
            refiner_.rollbackStep();
        }
        postCasInFlight_.store(false,std::memory_order_release);
        throw;
    }
}

SculptSourceContactAssessment VersionedSculptBrush::discoverLiveMergeContacts(
    const SculptSourceContactLimits& requested) const {
    ensureOperational();
    SculptSourceContactAssessment refused;
    const auto fail=[&](std::string reason) {
        refused.refusal=std::move(reason);
        return refused;
    };
    // A live discovery is NOT a way to upgrade an unprotected OFF-mode
    // session or a recovered V13 disk-only root to a new first original.
    if(!collision_.physicalExact||!firstOriginalAuthority_)
        return fail("Live Merge requires previously sealed first-original authority");
    const auto current=history_.currentFidelity();
    const auto root=published_.snapshot();
    if(!current||!root||current->lineage.terminal!=root||
       current->anchors.original!=firstOriginalAuthority_->anchors.original)
        return fail("Live Merge source certificate is missing or not current");
    // A UI caller may request TIGHTER bounds, never raise any default
    // finite original-source query budget or suppress whole-root coverage.
    SculptSourceContactLimits limits;
    limits.maxVertices=std::min(limits.maxVertices,requested.maxVertices);
    limits.maxFaces=std::min(limits.maxFaces,requested.maxFaces);
    limits.maxSectionTests=std::min(limits.maxSectionTests,requested.maxSectionTests);
    auto& q=limits.query;
    const auto& r=requested.query;
    q.maxSeedFaces=std::min(q.maxSeedFaces,r.maxSeedFaces);
    q.maxBoxTests=std::min(q.maxBoxTests,r.maxBoxTests);
    q.maxPartitionTests=std::min(q.maxPartitionTests,r.maxPartitionTests);
    q.maxCandidateTriangles=std::min(q.maxCandidateTriangles,r.maxCandidateTriangles);
    q.maxNarrowPhaseTests=std::min(q.maxNarrowPhaseTests,r.maxNarrowPhaseTests);
    q.maxReportedContacts=std::min(q.maxReportedContacts,r.maxReportedContacts);
    try {
        verifyOriginalPatchPublicationRoot(
            *current,root,collision_.originalTerminal.maxLivingDescendants);
        return assessPostSculptOriginalContacts(
            {root,root,{},{}},symmetric_,limits,
            symmetric_?&collapser_.mirrors():nullptr);
    }catch(const std::exception& e) {
        return fail(std::string("Live Merge source-provenance refusal: ")+e.what());
    }
}

bool VersionedSculptBrush::mergeExactContacts(
    std::shared_ptr<const RegionTopologyIndex> expected,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ExactCutStepLimits& requested,
    const std::vector<FaceId>& intendedOriginals) {
    ensureOperational();
    const auto started=Clock::now();
    lastStepResourceWork_={};
    lastStepResourceWork_.path=SculptStepResourcePath::QueuedExact;
    ScopedExactStageWork countExactGeometry(
        lastStepResourceWork_.exactStage);
    const auto reject=[&](const std::string& reason,
                          SculptRefusalCategory category=
                              SculptRefusalCategory::GeometryOrTopology) {
        lastDirtyPartitions_.clear();
        lastRejection_=reason;
        ++rejectedUnions_;
        lastStepResourceWork_.refused=true;
        lastStepResourceWork_.refusalCategory=category;
        lastStepResourceWork_.refusal=reason;
        lastStepResourceWork_.elapsedMilliseconds=
            seconds(started,Clock::now())*1000;
        recordStepDiagnostics();
        return false;
    };
    const auto old=published_.snapshot();
    if(!expected||old!=expected)
        return reject("Exact Boolean Step expected root is stale",
                      SculptRefusalCategory::StaleExpectedRoot);
    // A protected history MUST never be edited through a credential-free
    // command. An untracked legacy queue remains opt-in and unchanged.
    const auto currentSource=history_.currentFidelity();
    if((collision_.physicalExact||originalFidelityRequired_)&&!currentSource)
        return reject("Protected exact union lacks authenticated first-original source history");
    if(currentSource&&(!collision_.physicalExact||!firstOriginalAuthority_))
        return reject("Protected exact union requires physicalExact first-original authority");
    if(!currentSource) {
        if(const auto invalid=validateOriginalIntentSources(intendedOriginals);
           !invalid.empty())
            return reject(invalid);
    }
    if(contacts.empty())
        return reject("Exact Boolean Step requires a complete explicit contact set");
    if(inflate_.mirrors()!=refiner_.mirrors()||
       inflate_.mirrors()!=collapser_.mirrors())
        return reject("Sculpt mirror controllers disagree before Boolean union");
    ExactCutStepLimits limits=requested;
    // Symmetric sculpt sessions may NEVER silently become asymmetric
    // merely because a queued request omitted mirror correspondence.
    limits.requireMirrors=symmetric_;
    const auto result=prepareExactCutStep(
        old,contacts,limits,symmetric_?&collapser_.mirrors():nullptr);
    lastStepResourceWork_.stageWall.initialGeometryMs=
        seconds(started,Clock::now())*1000;
    if(!result.complete||!result.final||
       result.expected!=old||result.commit.revision.get()!=
           &result.final->revision())
        return reject("Exact Boolean Step preparation refused: "+result.refusal);

    // Every mirror mutation must be a valid extension or retirement of
    // the CURRENT three synchronized controller maps. This is checked
    // before any publication or speculative state mutation.
    const auto& living=collapser_.mirrors();
    std::set<VertexId> removed;
    for(const auto& [a,b]:result.removedMirrors) {
        if(!a||!b||!living.contains(a)||!living.contains(b)||
           living.at(a)!=b||living.at(b)!=a||
           !removed.insert(a).second||
           (a!=b&&!removed.insert(b).second)||
           result.final->maybeVertex(a)||result.final->maybeVertex(b)||
           (a==b&&(old->vertex(a).position.x!=0)))
            return reject("Stale or duplicated original mirror retirement");
    }
    std::set<VertexId> inserted;
    for(const auto& [a,b]:result.addedMirrors) {
        if(!a||!b||living.contains(a)||living.contains(b)||
           !inserted.insert(a).second||
           (a!=b&&!inserted.insert(b).second)||
           !result.final->maybeVertex(a)||!result.final->maybeVertex(b)||
           old->maybeVertex(a)||old->maybeVertex(b))
            return reject("New reflected vertex conflicts with current mirror maps");
        const auto p=result.final->vertex(a).position;
        const auto q=result.final->vertex(b).position;
        if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
           (a==b&&p.x!=0))
            return reject("New mirror orbit is not physically self/reciprocally reflected");
    }
    if(!symmetric_&&(!result.addedMirrors.empty()||
                     !result.removedMirrors.empty()))
        return reject("Asymmetric sculpt request created mirror journal entries");

    const auto changedVertices=[&] {
        std::vector<VertexId> ids=result.addedVertices;
        ids.insert(ids.end(),result.retiredVertices.begin(),
                   result.retiredVertices.end());
        return ids;
    }();
    const auto changedFaces=[&] {
        std::vector<FaceId> ids=result.addedFaces;
        ids.insert(ids.end(),result.rewrittenFaces.begin(),
                   result.rewrittenFaces.end());
        ids.insert(ids.end(),result.retiredFaces.begin(),
                   result.retiredFaces.end());
        return ids;
    }();
    if(validator_) {
        std::string reason;
        try {
            reason=validator_({old,result.final,
                std::span<const VertexId>(changedVertices),
                std::span<const FaceId>(changedFaces)});
        }catch(const std::length_error& e){reason=e.what();}
        catch(const std::logic_error& e){reason=e.what();}
        if(!reason.empty())return reject(reason);
    }
    // M33: the complete private c1/c2/c3/M03/d2 surgery is the ONE
    // source-owned successor of the ACTUAL expected published M29 root.
    // M26 independently replays the exact prepared children, mirrored
    // source IDs, final d2 journals and wholly classified retirements.
    // M21 checks ALL first-original live descendants against the final
    // terminal, not an intermediate cut root or an XYZ-matched bystander.
    // Nothing below this block (not even speculative controller maps)
    // may change until this proof and its finite work limits succeed.
    lastStepResourceWork_.stageWall.sourceGeometryMs=
        seconds(started,Clock::now())*1000-
        lastStepResourceWork_.stageWall.initialGeometryMs;
    const auto queuedProofStarted=Clock::now();
    std::shared_ptr<const OriginalPatchLifetime> finalFidelity;
    if(currentSource) {
        try {
            const auto& budget=collision_.originalTerminal;
            if(!collision_.originalIntentInput.maxDeclaredSources||
               intendedOriginals.size()>
                   collision_.originalIntentInput.maxDeclaredSources)
                throw std::length_error(
                    "Queued original FaceId input exceeds finite declaration bound");
            if(currentSource!=history_.currentFidelity()||
               currentSource->lineage.terminal!=old||
               currentSource->anchors.original!=firstOriginalAuthority_->anchors.original)
                throw std::logic_error(
                    "Queued source-exact union has stale or substituted original certificate");
            verifyOriginalPatchPublicationRoot(
                *currentSource,old,budget.maxLivingDescendants);
            // The request may explicitly CONTINUE earlier sculpt intent.
            // A named now-retired source is allowable ONLY if it was
            // independently c3/M03-certified wholly Buried/Outside.
            std::set<FaceId> activeOrigins,nominated;
            for(const auto& [first,faces]:currentSource->lineage.living)
                if(!faces.empty())activeOrigins.insert(first);
            std::vector<FaceId> activeIntent;
            activeIntent.reserve(intendedOriginals.size());
            for(const auto id:intendedOriginals) {
                if(!id||!nominated.insert(id).second)
                    throw std::logic_error("Duplicated or null queued first-original FaceId intent");
                if(activeOrigins.contains(id))
                    activeIntent.push_back(id);
                else if(!std::binary_search(
                            currentSource->certifiedRetiredOriginals.begin(),
                            currentSource->certifiedRetiredOriginals.end(),id))
                    throw std::logic_error(
                        "Queued original intent is foreign or lacks whole-source retirement");
            }
            const auto declaration=declareOriginalPatchIntent(
                *currentSource,activeIntent,
                symmetric_?&firstOriginalMirrors_:nullptr,
                collision_.originalIntentInput.maxDeclaredSources);
            auto next=advanceOriginalPatchLifetime(
                *currentSource,result,
                symmetric_?&collapser_.mirrors():nullptr,
                budget.maxCumulativeFaceEvidence,budget.maxLivingDescendants);
            verifyOriginalPatchPublicationTransition(
                *currentSource,next,old,result.final,
                budget.maxLivingDescendants);
            // M29's append uses its immutable default finite proof cap,
            // even if a caller configured a more permissive M21 policy.
            // Preflight BOTH before CAS to avoid a post-CAS proof failure.
            verifyOriginalPatchPublicationTransition(
                *currentSource,next,old,result.final);
            (void)evaluateOriginalPatchIntent(
                *currentSource,next,declaration,
                budget.maximumProtectedDeviationInEdges,
                budget.maxDistanceTests,budget.maxLivingDescendants);
            finalFidelity=std::make_shared<const OriginalPatchLifetime>(
                std::move(next));
        }catch(const std::exception& error) {
            return reject(std::string(
                "Queued first-original exact terminal policy refused: ")+error.what());
        }
    }
    lastStepResourceWork_.stageWall.originalProofMs=
        seconds(queuedProofStarted,Clock::now())*1000;
    const auto queuedAdmissionStarted=Clock::now();
    const auto countAfter=[&](std::size_t current,std::ptrdiff_t delta) {
        if(delta<0) {
            const auto decrement=static_cast<std::size_t>(-(delta+1))+1;
            if(current<decrement)throw std::logic_error(
                "Boolean union element count underflow");
            return current-decrement;
        }
        if(static_cast<std::size_t>(delta)>
           std::numeric_limits<std::size_t>::max()-current)
            throw std::logic_error("Boolean union element count overflow");
        return current+static_cast<std::size_t>(delta);
    };
    const auto newVertices=countAfter(vertices_,result.vertexCountDelta);
    const auto newFaces=countAfter(faces_,result.faceCountDelta);

    // M40: a queued exact Step is already a complete private
    // cut/refinement/join root. Its copiedPayloadBytes includes ALL
    // intermediate COW edits; changed net stable-ID evidence and final
    // render owner partitions are assessed independently of the
    // per-stage V13 limits before any speculative mirror journal or CAS.
    try {
        auto& measured=lastStepResourceWork_;
        measured.privateCopiedBytes=result.copiedPayloadBytes;
        measured.changedIdReferences=checkedStepSum({
            changedVertices.size(),changedFaces.size()});
        measured.finalDirtyPartitions=result.dirtyPartitions.size();
        measured.evaluated=true;
        if(const auto reason=refuseExcessStepWork(
               measured,collision_.stepResources);!reason.empty())
            return reject(reason,SculptRefusalCategory::FiniteWorkBudget);
    }catch(const std::length_error& e) {
        return reject(e.what(),SculptRefusalCategory::FiniteWorkBudget);
    }
    // M41–M45: the queued original-root source candidate has executed
    // geometry, source replay, original witness, protected-feature,
    // mirror and M29 history preflight; refuse ALL operation costs before
    // speculative mirror updates, history or the one final-root CAS.
    if(const auto reason=refuseExactStageWork(
           lastStepResourceWork_.exactStage,collision_.exactStageLimits);
       !reason.empty())
        return reject(reason,SculptRefusalCategory::FiniteWorkBudget);
    lastStepResourceWork_.stageWall.admissionMs=
        seconds(queuedAdmissionStarted,Clock::now())*1000;
    countExactGeometry.stop();
    const auto queuedPublicationStarted=Clock::now();
    // The worker is the only writer of the sculpt controller maps,
    // history, and snapshots. Begin both speculative mirror journals;
    // any prepublication failure rolls both back without changing the
    // source root, history, rendered snapshot, or Inflate mirror map.
    refiner_.beginStep();
    try {collapser_.beginStep();}
    catch(...) {refiner_.rollbackStep();throw;}
    bool didPublish=false;
    try {
        for(const auto& [a,b]:result.removedMirrors) {
            refiner_.removeMirrorPair(a,b);
            collapser_.removeMirrorPair(a,b);
        }
        for(const auto& [a,b]:result.addedMirrors) {
            refiner_.addMirrorPair(a,b);
            collapser_.addMirrorPair(a,b);
        }
        // ONE original-root indexed CAS, with NO intermediate cut or
        // seam-only root ever visible to renderer/picker subscribers.
        postCasInFlight_.store(true,std::memory_order_release);
        if(!published_.publishIfCurrent(old,result.final)) {
            postCasInFlight_.store(false,std::memory_order_release);
            collapser_.rollbackStep();
            refiner_.rollbackStep();
            return reject("Exact Boolean Step lost its original-root CAS");
        }
        didPublish=true;
        injectPostCasFault(SculptPostCasFaultPoint::AfterRootCas);
        for(const auto& [a,b]:result.removedMirrors)
            inflate_.removeMirrorPair(a,b);
        for(const auto& [a,b]:result.addedMirrors)
            inflate_.addMirrorPair(a,b);
        injectPostCasFault(SculptPostCasFaultPoint::AfterMirrorState);
        history_.append(result.final,result.dirtyPartitions,
            result.addedMirrors,result.removedMirrors,
            result.copiedPayloadBytes,newVertices,newFaces,
            std::move(finalFidelity));
        injectPostCasFault(SculptPostCasFaultPoint::AfterHistoryAppend);
        refiner_.commitStep();
        collapser_.commitStep();
        injectPostCasFault(SculptPostCasFaultPoint::AfterAuxCommit);
    }catch(...) {
        if(didPublish) {
            lastStepResourceWork_.elapsedMilliseconds=
                seconds(started,Clock::now())*1000;
            markPostCasFailure(std::current_exception());
        }else {
            collapser_.rollbackStep();
            refiner_.rollbackStep();
        }
        postCasInFlight_.store(false,std::memory_order_release);
        throw;
    }
    vertices_=newVertices;
    faces_=newFaces;
    lastDirtyPartitions_=result.dirtyPartitions;
    lastRejection_.clear();
    details_={};
    details_.vertices=vertices_;
    details_.triangles=faces_;
    modifiedPartitions_+=result.commit.statistics.modifiedPartitions;
    copiedDirectoryNodes_+=result.commit.statistics.copiedDirectoryNodes;
    clonedPartitionBytes_+=result.copiedPayloadBytes;
    ++publishedRevisions_;
    ++completedUnions_;
    lastStepResourceWork_.published=true;
    lastStepResourceWork_.elapsedMilliseconds=
        seconds(started,Clock::now())*1000;
    lastStepResourceWork_.stageWall.publicationAndHistoryMs=
        seconds(queuedPublicationStarted,Clock::now())*1000;
    recordStepDiagnostics();
    postCasInFlight_.store(false,std::memory_order_release);
    return true;
}

bool VersionedSculptBrush::undo() {
    ensureOperational();
    if(!history_.canUndo())return false;
    const auto previous=published_.snapshot();
    const auto step=history_.undo();
    for(const auto& [a,b]:step.removeMirrors) {
        inflate_.removeMirrorPair(a,b);
        refiner_.removeMirrorPair(a,b);
        collapser_.removeMirrorPair(a,b);
    }
    for(const auto& [a,b]:step.addMirrors) {
        inflate_.addMirrorPair(a,b);
        refiner_.addMirrorPair(a,b);
        collapser_.addMirrorPair(a,b);
    }
    if(!published_.publishIfCurrent(previous,step.index))
        throw std::logic_error("History undo base revision changed");
    lastDirtyPartitions_=step.dirtyPartitions;
    vertices_=step.vertices;
    faces_=step.faces;
    details_.vertices=vertices_;
    details_.triangles=faces_;
    details_.moved=details_.splitPairs=details_.flipPairs=details_.collapsedPairs=0;
    ++publishedRevisions_;
    return true;
}
bool VersionedSculptBrush::redo() {
    ensureOperational();
    if(!history_.canRedo())return false;
    const auto previous=published_.snapshot();
    const auto step=history_.redo();
    for(const auto& [a,b]:step.removeMirrors) {
        inflate_.removeMirrorPair(a,b);
        refiner_.removeMirrorPair(a,b);
        collapser_.removeMirrorPair(a,b);
    }
    for(const auto& [a,b]:step.addMirrors) {
        inflate_.addMirrorPair(a,b);
        refiner_.addMirrorPair(a,b);
        collapser_.addMirrorPair(a,b);
    }
    if(!published_.publishIfCurrent(previous,step.index))
        throw std::logic_error("History redo base revision changed");
    lastDirtyPartitions_=step.dirtyPartitions;
    vertices_=step.vertices;
    faces_=step.faces;
    details_.vertices=vertices_;
    details_.triangles=faces_;
    details_.moved=details_.splitPairs=details_.flipPairs=details_.collapsedPairs=0;
    ++publishedRevisions_;
    return true;
}
DurableCheckpoint VersionedSculptBrush::checkpoint() const {
    ensureOperational();
    if(postCasInFlight_.load(std::memory_order_acquire))
        throw std::logic_error("Checkpoint forbidden during original-root auxiliary commit");
    DurableCheckpoint snapshot;
    snapshot.index=published_.snapshot();
    snapshot.initialVertexCount=initialVertexCount_;
    snapshot.verifiedOriginalFidelity=history_.currentFidelity();
    snapshot.originalFidelityRequired=originalFidelityRequired_||
        static_cast<bool>(snapshot.verifiedOriginalFidelity);
    // Trusted-only first-root mirrored identity travels separately from
    // the current mirror map, which can have retired many original IDs.
    if(firstOriginalAuthority_)
        for(const auto& [a,b]:firstOriginalMirrors_)
            if(a<=b)snapshot.verifiedFirstOriginalMirrorPairs.emplace_back(a,b);
    std::sort(snapshot.verifiedFirstOriginalMirrorPairs.begin(),
              snapshot.verifiedFirstOriginalMirrorPairs.end());
    for(const auto& [a,b]:inflate_.mirrors())
        if(a<=b)snapshot.mirrorPairs.push_back({a,b});
    std::sort(snapshot.mirrorPairs.begin(),snapshot.mirrorPairs.end());
    return snapshot;
}

} // namespace mesher
