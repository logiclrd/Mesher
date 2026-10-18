#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {
namespace {
ExactCutStep reject(const std::string& why) {
    ExactCutStep result;
    result.refusal=why;
    // A failed multi-edit preparation must expose neither its cut root,
    // its terminal root, nor a partially consumable history journal.
    return result;
}
template<class T>
void sortedUnique(std::vector<T>& values) {
    std::sort(values.begin(),values.end());
    values.erase(std::unique(values.begin(),values.end()),values.end());
}
bool same(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}
bool reflected(Vec3 a,Vec3 b) {
    return a.x==-b.x&&a.y==b.y&&a.z==b.z;
}
std::pair<VertexId,VertexId> ordered(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
bool mirroredFace(const RegionTopologyIndex& index,FaceId a,FaceId b) {
    const auto first=index.face(a).vertices,second=index.face(b).vertices;
    for(unsigned shift=0;shift<3;++shift) {
        bool matches=true;
        for(unsigned i=0;i<3;++i)
            if(!reflected(index.vertex(first[i]).position,
                          index.vertex(second[(shift+3-i)%3]).position))
                matches=false;
        if(matches)return true;
    }
    return false;
}
} // namespace

ExactCutStep prepareExactCutStep(
    std::shared_ptr<const RegionTopologyIndex> original,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ExactCutStepLimits& limits,
    const std::unordered_map<VertexId,VertexId>* originalMirrors) {
    try {
        if(limits.requireMirrors&&!originalMirrors)
            return reject("Symmetric Step requires the original explicit mirror involution");
        if(!original||contacts.empty()||!limits.partitionCapacity||
           !limits.maxNetChangedIds||!limits.maxDirtyPartitions||
           !limits.maxFinalCollisionFaces)
            return reject("Original-root Step requires an immutable source and finite budgets");
        // The ONLY API that exposes this intermediate is internal to
        // preparing the Step; callers receive the original and final roots
        // together, never a publishable half-subdivided contact surface.
        const auto cut=materializeTransverseCutCandidate(
            original,contacts,limits.cut,limits.partitionCapacity,
            originalMirrors);
        if(!cut.index||cut.originalIndex!=original||
           !cut.commit.revision||
           cut.commit.revision.get()!=&cut.index->revision())
            return reject("Original-root Step subdivided a mismatched baseline");
        const auto joined=joinCertifiedCutSections(
            cut,limits.join,originalMirrors);
        if(!joined.complete||!joined.index||!joined.commit||
           !joined.commit->revision||
           joined.commit->revision.get()!=&joined.index->revision())
            return reject("Original-root Step seam join refused: "+joined.refusal);
        const auto& final=*joined.index;
        const auto& previous=*cut.index;

        // The complete changed halo is rechecked against the entire FINAL
        // index. No appeal to a previous iteration's contact query suffices.
        std::set<FaceId> seeds;
        for(const auto& [child,_]:cut.childSources)
            if(final.maybeFace(child))seeds.insert(child);
        for(const auto face:joined.rewrittenFaces)
            if(final.maybeFace(face))seeds.insert(face);
        if(seeds.empty()||seeds.size()>limits.maxFinalCollisionFaces)
            throw std::length_error("Original-root Step final collision halo exhausted");
        const auto remaining=final.triangleIntersections(
            {},{seeds.begin(),seeds.end()},nullptr,
            limits.finalCollisionLimits);
        if(!remaining.empty())
            return reject("Final original-root Step still has local surface intersections");

        // Reassess the complete private cut child set to make sure no
        // original exposed/cavity face disappeared behind the weld. A
        // surviving child's coordinates must match EXACTLY, not merely
        // project onto another approximate triangle.
        const auto policy=planExactCutRemoval(cut,limits.join.removal);
        if(!policy.ready()||
           policy.proposedRemoval!=joined.retiredFaces)
            return reject("Final Step and certified exact child removal disagree");
        for(const auto face:policy.protectedChildren) {
            if(!final.maybeFace(face))
                return reject("Final Step retired an exposed or cavity interface");
            const auto a=previous.face(face).vertices,
                       b=final.face(face).vertices;
            for(unsigned k=0;k<3;++k)
                if(!same(previous.vertex(a[k]).position,
                         final.vertex(b[k]).position))
                    return reject("Final Step displaced protected exact-child surface");
        }

        std::set<VertexId> cutAddedVertices(
            cut.insertedVertices.begin(),cut.insertedVertices.end());
        std::set<FaceId> cutAddedFaces(
            cut.insertedFaces.begin(),cut.insertedFaces.end());
        std::set<VertexId> removedVertices(
            joined.retiredVertices.begin(),joined.retiredVertices.end());
        std::set<FaceId> removedFaces(
            joined.retiredFaces.begin(),joined.retiredFaces.end());
        if(cutAddedVertices.size()!=cut.insertedVertices.size()||
           cutAddedFaces.size()!=cut.insertedFaces.size()||
           removedVertices.size()!=joined.retiredVertices.size()||
           removedFaces.size()!=joined.retiredFaces.size())
            return reject("Duplicate stable IDs in composed local edit journals");

        ExactCutStep composed;
        std::set<PartitionId> owners;
        const auto originalVertexOwner=[&](VertexId id) {
            if(auto p=original->maybeVertex(id))owners.insert(p->owner);
        };
        const auto cutVertexOwner=[&](VertexId id) {
            if(auto p=previous.maybeVertex(id))owners.insert(p->owner);
        };
        const auto finalVertexOwner=[&](VertexId id) {
            if(auto p=final.maybeVertex(id))owners.insert(p->owner);
        };
        const auto originalFaceOwner=[&](FaceId id) {
            if(auto p=original->maybeFace(id))owners.insert(p->owner);
        };
        const auto cutFaceOwner=[&](FaceId id) {
            if(auto p=previous.maybeFace(id))owners.insert(p->owner);
        };
        const auto finalFaceOwner=[&](FaceId id) {
            if(auto p=final.maybeFace(id))owners.insert(p->owner);
        };

        for(const auto id:cutAddedVertices) {
            if(original->maybeVertex(id))
                return reject("An inserted cut VertexId was already present at original root");
            cutVertexOwner(id);
            if(final.maybeVertex(id)) {
                composed.addedVertices.push_back(id);
                finalVertexOwner(id);
            }else if(!removedVertices.contains(id))
                return reject("Cut-inserted missing VertexId lacks retirement provenance");
        }
        for(const auto id:removedVertices) {
            cutVertexOwner(id);
            if(final.maybeVertex(id))
                return reject("Retired seam VertexId survives in the final root");
            if(original->maybeVertex(id)) {
                composed.retiredVertices.push_back(id);
                originalVertexOwner(id);
            }else if(!cutAddedVertices.contains(id))
                return reject("Unknown retired seam VertexId in composed journal");
        }
        for(const auto id:cutAddedFaces) {
            if(original->maybeFace(id))
                return reject("An inserted FaceId was already present at original root");
            cutFaceOwner(id);
            if(final.maybeFace(id)) {
                composed.addedFaces.push_back(id);
                finalFaceOwner(id);
            }else if(!removedFaces.contains(id))
                return reject("Missing child FaceId lacks retirement provenance");
        }
        for(const auto id:removedFaces) {
            cutFaceOwner(id);
            if(final.maybeFace(id))
                return reject("Retired FaceId survives in the final Step");
            if(original->maybeFace(id)) {
                composed.retiredFaces.push_back(id);
                originalFaceOwner(id);
            }else if(!cutAddedFaces.contains(id))
                return reject("Unknown retired child FaceId in composed journal");
        }

        std::set<FaceId> modifiedOriginals(
            cut.rewrittenFaces.begin(),cut.rewrittenFaces.end());
        modifiedOriginals.insert(
            joined.rewrittenFaces.begin(),joined.rewrittenFaces.end());
        for(const auto id:modifiedOriginals) {
            cutFaceOwner(id);
            if(!original->maybeFace(id)) {
                if(!cutAddedFaces.contains(id))
                    return reject("Rewritten face is neither original nor cut-inserted");
                continue;
            }
            originalFaceOwner(id);
            if(final.maybeFace(id)) {
                finalFaceOwner(id);
                if(original->face(id).vertices!=final.face(id).vertices)
                    composed.rewrittenFaces.push_back(id);
            }else if(!removedFaces.contains(id))
                return reject("Original altered face disappeared without retirement");
        }
        // Transitions may be born then retired before publication. Such
        // identities remain unavailable for reuse but are not new LIVE IDs
        // and must NOT enter checkpoint mirror or history added-pair deltas.
        if(originalMirrors) {
            std::set<std::pair<VertexId,VertexId>> visited;
            for(const auto id:composed.retiredVertices) {
                const auto it=originalMirrors->find(id);
                if(it==originalMirrors->end()||it->second==id||
                   !originalMirrors->contains(it->second)||
                   originalMirrors->at(it->second)!=id)
                    return reject("Retired historical vertex lacks its exact reciprocal mirror");
                const auto partner=it->second;
                if(final.maybeVertex(partner)||
                   !std::binary_search(composed.retiredVertices.begin(),
                                       composed.retiredVertices.end(),partner))
                    return reject("Final Step retired only half an original mirror orbit");
                if(visited.insert(ordered(id,partner)).second)
                    composed.removedMirrors.push_back(ordered(id,partner));
            }
            for(const auto& [a,b]:cut.mirroredVertices) {
                if(a==b||!cutAddedVertices.contains(a)||
                   !cutAddedVertices.contains(b))
                    return reject("New mirror journal contains non-new or self-paired ID");
                const bool liveA=final.maybeVertex(a)!=nullptr,
                           liveB=final.maybeVertex(b)!=nullptr;
                if(liveA!=liveB)
                    return reject("Only half the new exact reflected vertex orbit survived");
                if(liveA) {
                    if(!reflected(final.vertex(a).position,
                                  final.vertex(b).position))
                        return reject("Final composed Step lost a new vertex mirror");
                    composed.addedMirrors.push_back(ordered(a,b));
                }
            }
            for(const auto& [a,b]:cut.mirroredFaces) {
                const bool liveA=final.maybeFace(a)!=nullptr,
                           liveB=final.maybeFace(b)!=nullptr;
                if(liveA!=liveB)
                    return reject("Only half a reflected child FaceId orbit survived");
                if(liveA) {
                    if(!mirroredFace(final,a,b))
                        return reject("Reflected surviving child faces are inconsistent");
                    composed.survivingMirroredFaces.emplace_back(
                        std::min(a,b),std::max(a,b));
                }
            }
        }else if(!cut.mirroredVertices.empty()||!cut.mirroredFaces.empty())
            return reject("Nonmirrored Step unexpectedly allocated reflected orbits");

        // There is no whole-mesh traversal on this hot path. All partition
        // owners arise from explicit cut and seam write journals. Unchanged
        // partition pointers remain path-shared between the original and
        // terminal COW revisions. History dirties are this exact local set.
        if(owners.empty()||owners.size()>limits.maxDirtyPartitions)
            throw std::length_error("Original-root dirty partition budget exhausted");
        for(const auto owner:owners) {
            const auto before=original->revision().partition(owner);
            const auto after=final.revision().partition(owner);
            if(before==after)
                continue; // an entirely born-then-dead private write
            if(!after)
                return reject("Private Step unexpectedly erased a whole partition");
            composed.dirtyPartitions.push_back(owner);
        }
        if(composed.dirtyPartitions.empty())
            return reject("Composed Step contains no published partition changes");

        sortedUnique(composed.addedVertices);
        sortedUnique(composed.retiredVertices);
        sortedUnique(composed.addedFaces);
        sortedUnique(composed.rewrittenFaces);
        sortedUnique(composed.retiredFaces);
        sortedUnique(composed.addedMirrors);
        sortedUnique(composed.removedMirrors);
        sortedUnique(composed.survivingMirroredFaces);
        const auto total=composed.addedVertices.size()+
            composed.retiredVertices.size()+composed.addedFaces.size()+
            composed.rewrittenFaces.size()+composed.retiredFaces.size()+
            composed.addedMirrors.size()*2+composed.removedMirrors.size()*2;
        if(total>limits.maxNetChangedIds)
            throw std::length_error("Original-root net ID journal budget exhausted");
        if(composed.addedVertices.size()>
               static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())||
           composed.retiredVertices.size()>
               static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())||
           composed.addedFaces.size()>
               static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())||
           composed.retiredFaces.size()>
               static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
            throw std::length_error("Composed Step net element delta overflow");
        composed.vertexCountDelta=
            static_cast<std::ptrdiff_t>(composed.addedVertices.size())-
            static_cast<std::ptrdiff_t>(composed.retiredVertices.size());
        composed.faceCountDelta=
            static_cast<std::ptrdiff_t>(composed.addedFaces.size())-
            static_cast<std::ptrdiff_t>(composed.retiredFaces.size());
        const auto a=cut.commit.statistics.clonedPartitionBytes,
                   b=joined.commit->statistics.clonedPartitionBytes;
        if(a>std::numeric_limits<std::size_t>::max()-b)
            throw std::length_error("Combined private COW payload work overflow");
        composed.copiedPayloadBytes=a+b;
        composed.expected=std::move(original);
        composed.final=joined.index;
        composed.commit.revision=joined.commit->revision;
        composed.commit.statistics.clonedPartitions=
            cut.commit.statistics.clonedPartitions+
            joined.commit->statistics.clonedPartitions;
        composed.commit.statistics.clonedPartitionBytes=
            composed.copiedPayloadBytes;
        composed.commit.statistics.copiedDirectoryNodes=
            cut.commit.statistics.copiedDirectoryNodes+
            joined.commit->statistics.copiedDirectoryNodes;
        composed.commit.statistics.modifiedPartitions=
            composed.dirtyPartitions.size();
        composed.finalCollisionSeeds=seeds.size();
        composed.checkedProtectedFaces=policy.protectedChildren.size();
        composed.complete=true;
        return composed;
    }catch(const std::exception& e) {
        return reject(e.what());
    }
}
} // namespace mesher
