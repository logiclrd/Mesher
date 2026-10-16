#include "mesher/revision_history.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mesher {
RevisionHistory::RevisionHistory(
    std::shared_ptr<const RegionTopologyIndex> initial,
    std::size_t maximumRevisions,std::size_t maximumChangeBytes,
    RevisionRetirementQueue* retirement)
    :maximumRevisions_(maximumRevisions),maximumChangeBytes_(maximumChangeBytes),
     retirement_(retirement) {
    if(!initial||!maximumRevisions_||!maximumChangeBytes_)
        throw std::invalid_argument("History needs initial revision and positive limits");
    entries_.push_back({std::move(initial),{},{},{},0});
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
    std::size_t copiedPayloadBytes) {
    if(!index)throw std::invalid_argument("Cannot append null history root");
    if(index==current())return;
    discardRedo();
    std::sort(changedPartitions.begin(),changedPartitions.end());
    changedPartitions.erase(std::unique(changedPartitions.begin(),
                                        changedPartitions.end()),
                            changedPartitions.end());
    if(copiedPayloadBytes>std::numeric_limits<std::size_t>::max()-totalBytes_)
        throw std::overflow_error("History payload counter overflow");
    entries_.push_back({std::move(index),std::move(changedPartitions),
                        std::move(addedMirrors),std::move(removedMirrors),
                        copiedPayloadBytes});
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
HistoryTransition RevisionHistory::undo() {
    if(!canUndo())throw std::logic_error("History has no undo revision");
    auto& previous=entries_[cursor_];
    auto changed=previous.dirty;
    auto add=previous.removed;
    auto remove=previous.added;
    --cursor_;
    return {entries_[cursor_].index,std::move(changed),
            std::move(add),std::move(remove)};
}
HistoryTransition RevisionHistory::redo() {
    if(!canRedo())throw std::logic_error("History has no redo revision");
    ++cursor_;
    auto& next=entries_[cursor_];
    return {next.index,next.dirty,next.added,next.removed};
}
HistoryStatistics RevisionHistory::statistics() const noexcept {
    return {entries_.size(),totalBytes_,dropped_,redoDropped_,
            cursor_,entries_.size()-cursor_-1};
}
} // namespace mesher
