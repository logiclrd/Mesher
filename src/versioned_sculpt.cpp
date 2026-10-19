#include "mesher/versioned_sculpt.hpp"
#include "mesher/partition_render.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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
        return; // Exhaustion or ambiguity can NEVER certify a clean interval.
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
        stats_.quietRun=0;
    }
}

SculptSourceContactAssessment assessPostSculptOriginalContacts(
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
            vertices,{},&result.work,limits.query);
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
        result.status=SculptSourceContactStatus::CompleteTransverse;
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
     history_(published_.snapshot(),64,32*1024*1024,&historyRetirement_,
              initialCount,flatOrder_->faceIds.size()),
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
    details_.vertices=vertices_;
    details_.triangles=faces_;
    if(collision_.physicalExact) {
        // Only a genuinely new imported original may create M27's first
        // anchors. Accepted later terminal revisions NEVER reset them.
        firstOriginalAuthority_=std::make_shared<const OriginalPatchLifetime>(
            beginOriginalPatchLifetime(captureOriginalSourceAnchors(
                published_.snapshot(),flatOrder_->faceIds,
                collision_.originalIntentInput.maxOriginalWitnesses)));
        if(symmetric_)firstOriginalMirrors_=inflate_.mirrors();
    }
}

VersionedSculptBrush::VersionedSculptBrush(
    DurableCheckpoint recovered,double strength,CurvatureSettings curvature,
    CollapseSettings collapse,RefineSettings refine,
    std::size_t partitionCapacity,SculptStepValidator validator,
    SculptCollisionSettings collision)
    :flatOrder_(std::make_shared<const FlatMeshIndex>()),
     published_(recovered.index),
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
    details_.vertices=vertices_;
    details_.triangles=faces_;
    if(collision_.physicalExact&&recovered.verifiedOriginalFidelity) {
        // A trusted recovery may retain opaque FIRST-root authority.
        // Never re-anchor a recovered later terminal, including V12/V13.
        firstOriginalAuthority_=recovered.verifiedOriginalFidelity;
        auto originalPairs=recovered.verifiedFirstOriginalMirrorPairs;
        if(originalPairs.empty()&&firstOriginalAuthority_->acceptedTransitions==0&&
           firstOriginalAuthority_->anchors.original==published_.snapshot())
            originalPairs=recovered.mirrorPairs;
        for(const auto& [a,b]:originalPairs) {
            if(!a||!b||firstOriginalMirrors_.contains(a)&&firstOriginalMirrors_.at(a)!=b||
               firstOriginalMirrors_.contains(b)&&firstOriginalMirrors_.at(b)!=a)
                throw std::logic_error("Conflicting original reflected-source authority");
            firstOriginalMirrors_[a]=b;
            firstOriginalMirrors_[b]=a;
        }
    }
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
    physicalIntendedOriginals_=std::move(originals);
    physicalIntentRefusal_=validateOriginalIntentSources(
        physicalIntendedOriginals_);
}

void VersionedSculptBrush::stamp(
    Mesh&,std::span<const BrushArea> areas) {
    const auto old=published_.snapshot();
    const auto start=Clock::now();
    // Scope is resolved on the worker at Begin and is never filled from
    // candidate geometry. Bad source input must not terminate FIFO.
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
        return;
    }
    const auto inflated=inflate_.stamp(old,areas,partitionCapacity_);
    const auto afterInflate=Clock::now();
    refiner_.beginStep();
    try {collapser_.beginStep();}
    catch(...) {refiner_.rollbackStep();throw;}
    try {
    const auto refined=refiner_.apply(inflated.index,inflated.baseline);
    // Collapse must see V6's newly inserted stable mirror IDs immediately.
    for(const auto& pair:refined.insertedMirrorPairs)
        collapser_.addMirrorPair(pair.first,pair.second);
    const auto afterRefine=Clock::now();
    const auto collapsed=collapser_.apply(refined.index,inflated.baseline);
    const auto afterCollapse=Clock::now();
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
            if(discovered.status==SculptSourceContactStatus::Refused) {
                physicalRefusal="Physical exact source refusal: "+discovered.refusal;
                contact_.observe(std::nullopt);
            }else if(discovered.status==SculptSourceContactStatus::Clean) {
                contact_.observe(false);
            }else {
                contact_.observe(true);
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
    // A selected collision policy first classifies the FULL post-remesh
    // proposal. When supported, it may supply a privately repaired root.
    // The V17c validator below observes that final root and expanded dirty
    // set, never a partly repaired intermediate revision.
    std::string collisionRefusal;
    std::optional<RepairSurgeryResult> collisionRepair;
    std::vector<MirrorPair> repairedAddedMirrors,repairedRemovedMirrors;
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
            (judged.plan.status==RepairPlanStatus::NoChanges||
             judged.plan.status==RepairPlanStatus::Ready);
        contact_.observe(conclusive?
            std::optional<bool>(judged.plan.intersections>0):std::nullopt);
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
    std::string rejection=physicalRefusal.empty()?
        std::move(collisionRefusal):std::move(physicalRefusal);
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
        return;
    }
    lastDirtyPartitions_=updated==old?std::vector<PartitionId>{}:
        dirtyRenderPartitions(*old,*updated,changedVertices,changedFaces);

    // All geometry and connectivity are already fully validated. Publish
    // the exact index + mesh revision pair atomically once per completed
    // Step, never publishing partially refined intermediate topology.
    if(updated!=old) {
        if(!published_.publishIfCurrent(old,updated))
            throw std::logic_error("Single writer lost indexed root");
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
    history_.append(updated,lastDirtyPartitions_,
                    std::move(addedMirrors),std::move(removedMirrors),
                    inflated.commitStatistics.clonedPartitionBytes+
                    refined.clonedPartitionBytes+collapsed.clonedPartitionBytes+
                    (physicalExactUnion?physicalExactUnion->copiedPayloadBytes:0)+
                    (collisionRepair?
                     collisionRepair->commit.statistics.clonedPartitionBytes:0),
                    vertices_,faces_);
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
    collapser_.commitStep();
    refiner_.commitStep();
    }catch(...) {
        collapser_.rollbackStep();
        refiner_.rollbackStep();
        throw;
    }
}

bool VersionedSculptBrush::mergeExactContacts(
    std::shared_ptr<const RegionTopologyIndex> expected,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ExactCutStepLimits& requested,
    const std::vector<FaceId>& intendedOriginals) {
    const auto reject=[&](const std::string& reason) {
        lastDirtyPartitions_.clear();
        lastRejection_=reason;
        ++rejectedUnions_;
        return false;
    };
    const auto old=published_.snapshot();
    if(!expected||old!=expected)
        return reject("Exact Boolean Step expected root is stale");
    if(const auto invalid=validateOriginalIntentSources(intendedOriginals);
       !invalid.empty())
        return reject(invalid);
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
        if(!published_.publishIfCurrent(old,result.final)) {
            collapser_.rollbackStep();
            refiner_.rollbackStep();
            return reject("Exact Boolean Step lost its original-root CAS");
        }
        didPublish=true;
        for(const auto& [a,b]:result.removedMirrors)
            inflate_.removeMirrorPair(a,b);
        for(const auto& [a,b]:result.addedMirrors)
            inflate_.addMirrorPair(a,b);
        history_.append(result.final,result.dirtyPartitions,
            result.addedMirrors,result.removedMirrors,
            result.copiedPayloadBytes,newVertices,newFaces);
        refiner_.commitStep();
        collapser_.commitStep();
    }catch(...) {
        // A post-CAS failure is fatal, as with the pre-existing sculpt
        // publication path: it must NEVER be called a recoverable refusal.
        if(!didPublish) {
            collapser_.rollbackStep();
            refiner_.rollbackStep();
        }
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
    return true;
}

bool VersionedSculptBrush::undo() {
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
