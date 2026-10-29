#include "mesher/revision_history.hpp"
#include "mesher/repair_surgery.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mesher {
RevisionHistory::RevisionHistory(
    std::shared_ptr<const RegionTopologyIndex> initial,
    std::size_t maximumRevisions,std::size_t maximumChangeBytes,
    RevisionRetirementQueue* retirement,
    std::size_t initialVertices,std::size_t initialFaces,
    std::shared_ptr<const OriginalPatchLifetime> firstOriginal)
    :maximumRevisions_(maximumRevisions),maximumChangeBytes_(maximumChangeBytes),
     retirement_(retirement) {
    if(!initial||!maximumRevisions_||!maximumChangeBytes_)
        throw std::invalid_argument("History needs initial revision and positive limits");
    if(firstOriginal)
        verifyOriginalPatchPublicationRoot(*firstOriginal,initial);
    entries_.push_back({std::move(initial),{},{},{},0,initialVertices,
                        initialFaces,std::move(firstOriginal)});
}
void RevisionHistory::retire(Entry&& entry) {
    if(entry.bytes>totalBytes_)throw std::logic_error("History byte accounting underflow");
    totalBytes_-=entry.bytes;
    if(retirement_)retirement_->retire(std::move(entry.index));
}
void RevisionHistory::discardRedo() {
    while(canRedo()) {
        auto entry=std::move(entries_.back());
        entries_.pop_back();
        retire(std::move(entry));
        ++redoDropped_;
    }
}
void RevisionHistory::append(
    std::shared_ptr<const RegionTopologyIndex> index,
    std::vector<PartitionId> changedPartitions,
    std::vector<MirrorPair> addedMirrors,
    std::vector<MirrorPair> removedMirrors,
    std::size_t copiedPayloadBytes,
    std::size_t vertices,std::size_t faces,
    std::shared_ptr<const OriginalPatchLifetime> fidelity) {
    if(!index)throw std::invalid_argument("Cannot append null history root");
    if(index==current()) {
        if(fidelity!=currentFidelity())
            throw std::logic_error("Unchanged history root cannot replace first-original credentials");
        return;
    }
    // Verify BEFORE discarding the redo branch, modifying the byte budget
    // or committing an index. The original source reference cannot be
    // restarted by passing a new independently anchored lifetime.
    if(currentFidelity()) {
        if(!fidelity)
            throw std::logic_error("Missing first-original lifetime on tracked history append");
        verifyOriginalPatchPublicationTransition(
            *currentFidelity(),*fidelity,current(),index);
    }else if(fidelity) {
        throw std::logic_error("Untracked history cannot silently re-anchor original fidelity");
    }
    discardRedo();
    std::sort(changedPartitions.begin(),changedPartitions.end());
    changedPartitions.erase(std::unique(changedPartitions.begin(),
                                        changedPartitions.end()),
                            changedPartitions.end());
    // Split and collapse can create and retire the same mirrored midpoint
    // within one stamp. Such a pair never existed at either endpoint and
    // must not be replayed by undo or redo.
    auto canonical=[](MirrorPair pair) {
        if(pair.first>pair.second)std::swap(pair.first,pair.second);
        return pair;
    };
    for(auto& pair:addedMirrors)pair=canonical(pair);
    for(auto& pair:removedMirrors)pair=canonical(pair);
    auto normalize=[](auto& pairs) {
        std::sort(pairs.begin(),pairs.end());
        pairs.erase(std::unique(pairs.begin(),pairs.end()),pairs.end());
    };
    normalize(addedMirrors);
    normalize(removedMirrors);
    std::vector<MirrorPair> netAdded,netRemoved;
    std::set_difference(addedMirrors.begin(),addedMirrors.end(),
                        removedMirrors.begin(),removedMirrors.end(),
                        std::back_inserter(netAdded));
    std::set_difference(removedMirrors.begin(),removedMirrors.end(),
                        addedMirrors.begin(),addedMirrors.end(),
                        std::back_inserter(netRemoved));
    addedMirrors=std::move(netAdded);
    removedMirrors=std::move(netRemoved);
    if(copiedPayloadBytes>std::numeric_limits<std::size_t>::max()-totalBytes_)
        throw std::overflow_error("History payload counter overflow");
    entries_.push_back({std::move(index),std::move(changedPartitions),
                        std::move(addedMirrors),std::move(removedMirrors),
                        copiedPayloadBytes,vertices,faces,std::move(fidelity)});
    totalBytes_+=copiedPayloadBytes;
    cursor_=entries_.size()-1;
    // The current entry is always retained even if one large edit exceeds
    // the budget; every earlier root can be evicted.
    while(entries_.size()>1 &&
          (entries_.size()>maximumRevisions_||totalBytes_>maximumChangeBytes_)) {
        auto old=std::move(entries_.front());
        entries_.pop_front();
        --cursor_;
        retire(std::move(old));
        ++dropped_;
        // The incoming oldest transition is now the base revision; it no
        // longer needs its own predecessor's delta.
        if(!entries_.empty()) {
            totalBytes_-=entries_.front().bytes;
            entries_.front().bytes=0;
            entries_.front().added.clear();
            entries_.front().removed.clear();
            entries_.front().dirty.clear();
        }
    }
}
void RevisionHistory::amendCurrent(
    std::shared_ptr<const RegionTopologyIndex> index,
    std::vector<PartitionId> additionalDirty,
    std::size_t additionalCopiedPayloadBytes) {
    if(!index||!canUndo()||canRedo())
        throw std::logic_error("Paint amendment requires terminal history entry");
    auto& currentEntry=entries_[cursor_];
    if(index==currentEntry.index)return;
    if(currentEntry.added.size()||currentEntry.removed.size())
        throw std::logic_error("Paint cannot amend a topology-mirror transition");
    if(currentEntry.fidelity)
        verifyOriginalPatchPublicationTransition(
            *currentEntry.fidelity,*currentEntry.fidelity,
            currentEntry.index,index);
    std::sort(additionalDirty.begin(),additionalDirty.end());
    additionalDirty.erase(std::unique(additionalDirty.begin(),
                                     additionalDirty.end()),
                          additionalDirty.end());
    std::vector<PartitionId> dirty=currentEntry.dirty;
    dirty.insert(dirty.end(),additionalDirty.begin(),additionalDirty.end());
    std::sort(dirty.begin(),dirty.end());
    dirty.erase(std::unique(dirty.begin(),dirty.end()),dirty.end());
    // The per-dab COW clones are TRANSIENT: only the endpoint's distinct
    // changed owners remain in this Undo transition. Charging every
    // intermediate clone would evict the original state of a long stroke.
    const auto perOwner=[](std::size_t bytes,std::size_t owners) {
        return owners?bytes/owners+(bytes%owners!=0):std::size_t{0};
    };
    const auto unit=std::max(perOwner(currentEntry.bytes,
                                     currentEntry.dirty.size()),
                             perOwner(additionalCopiedPayloadBytes,
                                      additionalDirty.size()));
    if(unit&&dirty.size()>std::numeric_limits<std::size_t>::max()/unit)
        throw std::overflow_error("Paint history byte accounting overflow");
    const auto estimatedBytes=dirty.size()*unit;
    if(estimatedBytes>currentEntry.bytes&&
       estimatedBytes-currentEntry.bytes>
           std::numeric_limits<std::size_t>::max()-totalBytes_)
        throw std::overflow_error("Paint history byte accounting overflow");
    // Commit the amended endpoint only after all validation/allocation.
    auto previous=std::move(currentEntry.index);
    currentEntry.index=std::move(index);
    currentEntry.dirty=std::move(dirty);
    totalBytes_=totalBytes_-currentEntry.bytes+estimatedBytes;
    currentEntry.bytes=estimatedBytes;
    if(retirement_)retirement_->retire(std::move(previous));
    while(entries_.size()>1 &&
          (entries_.size()>maximumRevisions_||totalBytes_>maximumChangeBytes_)) {
        auto old=std::move(entries_.front());
        entries_.pop_front();
        --cursor_;
        retire(std::move(old));
        ++dropped_;
        if(!entries_.empty()) {
            totalBytes_-=entries_.front().bytes;
            entries_.front().bytes=0;
            entries_.front().added.clear();
            entries_.front().removed.clear();
            entries_.front().dirty.clear();
        }
    }
}
HistoryTransition RevisionHistory::undo() {
    if(!canUndo())throw std::logic_error("History has no undo revision");
    auto& previous=entries_[cursor_];
    auto changed=previous.dirty;
    auto add=previous.removed;
    auto remove=previous.added;
    --cursor_;
    return {entries_[cursor_].index,std::move(changed),
            std::move(add),std::move(remove),
            entries_[cursor_].vertices,entries_[cursor_].faces,
            entries_[cursor_].fidelity};
}
HistoryTransition RevisionHistory::redo() {
    if(!canRedo())throw std::logic_error("History has no redo revision");
    ++cursor_;
    auto& next=entries_[cursor_];
    return {next.index,next.dirty,next.added,next.removed,
            next.vertices,next.faces,next.fidelity};
}
HistoryStatistics RevisionHistory::statistics() const noexcept {
    return {entries_.size(),totalBytes_,dropped_,redoDropped_,
            cursor_,entries_.size()-cursor_-1};
}
} // namespace mesher
