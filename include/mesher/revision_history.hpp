#pragma once

#include "mesher/region_edit.hpp"
#include "mesher/revision_retirement.hpp"

#include <cstddef>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

namespace mesher {

using MirrorPair=std::pair<VertexId,VertexId>;

struct HistoryTransition {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<PartitionId> dirtyPartitions;
    std::vector<MirrorPair> addMirrors;
    std::vector<MirrorPair> removeMirrors;
    std::size_t vertices{},faces{};
};

struct HistoryStatistics {
    std::size_t retainedRevisions{};
    std::size_t retainedChangeBytes{}; // COW payload estimate, not process RSS
    std::size_t droppedRevisions{};
    std::size_t discardedRedoRevisions{};
    std::size_t undoDepth{};
    std::size_t redoDepth{};
};

// Bounded, branch-aware snapshot history. No geometry copies are performed:
// entries share immutable revision roots. Mirror changes are per-step deltas,
// not complete O(total vertices) correspondence maps.
class RevisionHistory {
public:
    explicit RevisionHistory(std::shared_ptr<const RegionTopologyIndex> initial,
        std::size_t maximumRevisions=64,std::size_t maximumChangeBytes=32*1024*1024,
        RevisionRetirementQueue* retirement=nullptr,
        std::size_t initialVertices=0,std::size_t initialFaces=0);
    void append(std::shared_ptr<const RegionTopologyIndex> index,
        std::vector<PartitionId> changedPartitions,
        std::vector<MirrorPair> addedMirrors,
        std::vector<MirrorPair> removedMirrors,
        std::size_t copiedPayloadBytes,
        std::size_t vertices=0,std::size_t faces=0);
    bool canUndo() const noexcept {return cursor_>0;}
    bool canRedo() const noexcept {return cursor_+1<entries_.size();}
    HistoryTransition undo();
    HistoryTransition redo();
    HistoryStatistics statistics() const noexcept;
    std::shared_ptr<const RegionTopologyIndex> current() const noexcept {
        return entries_[cursor_].index;
    }
private:
    struct Entry {
        std::shared_ptr<const RegionTopologyIndex> index;
        std::vector<PartitionId> dirty;
        std::vector<MirrorPair> added,removed;
        std::size_t bytes{};
    };
    void retire(Entry&& entry);
    void discardRedo();
    std::deque<Entry> entries_;
    std::size_t cursor_{},totalBytes_{},dropped_{},redoDropped_{};
    std::size_t maximumRevisions_,maximumChangeBytes_;
    RevisionRetirementQueue* retirement_{};
};

} // namespace mesher
