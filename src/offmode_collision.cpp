#include "mesher/offmode_collision.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
using Pair=RegionTopologyIndex::CollisionPair;
using Key=std::pair<FaceId,FaceId>;

bool sameCoordinates(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}

bool sameFace(const RegionTopologyIndex& before,
              const RegionTopologyIndex& after,FaceId id) {
    const auto old=before.maybeFace(id),now=after.maybeFace(id);
    if(!old||!now||old->vertices!=now->vertices)return false;
    for(const auto v:old->vertices) {
        const auto p=before.maybeVertex(v),q=after.maybeVertex(v);
        if(!p||!q||!sameCoordinates(p->position,q->position))
            return false;
    }
    return true;
}

template<class ID,class Exists,class WasRetired>
bool collectKnown(std::span<const ID> inputs,
                  const Exists& exists,const WasRetired& wasRetired,
                  std::vector<ID>& before,std::vector<ID>& after) {
    for(const auto id:inputs) {
        const bool old=exists(id,true),now=exists(id,false);
        if(!old&&!now&&!wasRetired(id))return false;
        if(old)before.push_back(id);
        if(now)after.push_back(id);
    }
    const auto unique=[](std::vector<ID>& ids) {
        std::sort(ids.begin(),ids.end());
        ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    };
    unique(before);
    unique(after);
    return true;
}
} // namespace

OffModeContactReport assessOffModePostStateContacts(
    const std::shared_ptr<const RegionTopologyIndex>& before,
    const std::shared_ptr<const RegionTopologyIndex>& candidate,
    std::span<const VertexId> changedVertices,
    std::span<const FaceId> changedFaces,
    const OffModeContactLimits& limits) {
    OffModeContactReport report;
    const auto refuse=[&](const char* reason) {
        report.status=OffModeContactStatus::Uncertain;
        report.reason=reason;
        report.introduced.clear();
        report.retained.clear();
        report.uncertain.clear();
        report.resolvedOldContacts=0;
        return report;
    };
    if(!before||!candidate)
        return refuse("V18 contact ledger requires two immutable indexed roots");
    if(changedVertices.size()>limits.maxChangedInputRecords||
       changedFaces.size()>limits.maxChangedInputRecords||
       changedVertices.size()>limits.maxChangedInputRecords-
                                 changedFaces.size())
        return refuse("V18 contact ledger changed-input budget exhausted");
    if(changedVertices.empty()&&changedFaces.empty()) {
        if(before!=candidate)
            return refuse("V18 contact ledger cannot certify a changed root without changed IDs");
        report.status=OffModeContactStatus::Clean;
        return report;
    }

    std::vector<VertexId> oldVertices,newVertices;
    std::vector<FaceId> oldFaces,newFaces;
    const auto knownVertices=collectKnown<VertexId>(
        changedVertices,
        [&](VertexId id,bool isBefore) {
            return static_cast<bool>((isBefore?before:candidate)->maybeVertex(id));
        },
        [&](VertexId id) {
            return before->retiredVertex(id)||candidate->retiredVertex(id);
        },oldVertices,newVertices);
    const auto knownFaces=collectKnown<FaceId>(
        changedFaces,
        [&](FaceId id,bool isBefore) {
            return static_cast<bool>((isBefore?before:candidate)->maybeFace(id));
        },
        [&](FaceId id) {
            return before->retiredFace(id)||candidate->retiredFace(id);
        },oldFaces,newFaces);
    if(!knownVertices||!knownFaces)
        return refuse("V18 contact ledger received unknown stable vertex/face ID");

    try {
        // Each query has its OWN complete finite accounting. A narrow
        // phase on before is never confused with a candidate predicate.
        const auto old=before->triangleIntersections(
            oldVertices,oldFaces,&report.beforeWork,limits.query);
        const auto now=candidate->triangleIntersections(
            newVertices,newFaces,&report.candidateWork,limits.query);
        std::map<Key,Pair> oldPairs;
        for(const auto& p:old)oldPairs.emplace(Key{p.first,p.second},p);
        for(const auto& p:now) {
            const Key key{p.first,p.second};
            const auto prior=oldPairs.find(key);
            if(prior==oldPairs.end()) {
                if(p.ambiguous)report.uncertain.push_back(p);
                else report.introduced.push_back(p);
            }else if(!p.ambiguous&&!prior->second.ambiguous&&
                     sameFace(*before,*candidate,p.first)&&
                     sameFace(*before,*candidate,p.second)) {
                report.retained.push_back(p);
            }else {
                // A crossing that keeps its FaceIds can still worsen
                // when either face moves. Do NOT certify by key alone.
                report.uncertain.push_back(p);
            }
            if(prior!=oldPairs.end())oldPairs.erase(prior);
        }
        report.resolvedOldContacts=oldPairs.size();
    }catch(const std::length_error&) {
        return refuse("V18 contact ledger finite BVH work budget exhausted");
    }catch(const std::out_of_range&) {
        return refuse("V18 contact ledger source revision has invalid changed ID");
    }catch(const std::logic_error&) {
        return refuse("V18 contact ledger source topology is not a supported safe query");
    }

    if(!report.introduced.empty()) {
        report.status=OffModeContactStatus::NewContact;
        report.reason="V18 candidate introduces new nonadjacent contact";
    }else if(!report.uncertain.empty()) {
        report.status=OffModeContactStatus::Uncertain;
        report.reason="V18 candidate has ambiguous or moved pre-existing contact";
    }else {
        report.status=report.retained.empty()?
            OffModeContactStatus::Clean:OffModeContactStatus::UnchangedPreexisting;
    }
    return report;
}

} // namespace mesher
