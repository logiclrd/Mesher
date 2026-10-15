#include "mesher/versioned_mesh.hpp"

#include <stdexcept>
#include <utility>

namespace mesher {

namespace {
constexpr unsigned directoryLevels = 8; // 32-bit IDs, four bits per level.
constexpr unsigned directoryBits = 4;
constexpr unsigned directoryMask = 15;
}

struct MeshDirectoryNode {
    std::array<std::shared_ptr<const MeshDirectoryNode>,16> children{};
    std::shared_ptr<const MeshPartition> partition;
};

namespace {
std::shared_ptr<const MeshPartition> findPartition(
    const std::shared_ptr<const MeshDirectoryNode>& root, PartitionId id) {
    auto node = root;
    for (unsigned depth=0; depth<directoryLevels && node; ++depth) {
        const auto shift=(directoryLevels-1-depth)*directoryBits;
        node=node->children[(id>>shift)&directoryMask];
    }
    return node ? node->partition : nullptr;
}

// Path copying: changing one partition copies nine directory nodes
// regardless of the total number of partitions or mesh vertices.
std::shared_ptr<const MeshDirectoryNode> replacePartition(
    const std::shared_ptr<const MeshDirectoryNode>& current,
    PartitionId id, unsigned depth,
    const std::shared_ptr<const MeshPartition>& replacement,
    std::size_t& copiedNodes) {
    if (!current && !replacement) return nullptr;
    auto next=std::make_shared<MeshDirectoryNode>(
        current ? *current : MeshDirectoryNode{});
    ++copiedNodes;
    if(depth==directoryLevels) {
        next->partition=replacement;
    } else {
        const auto shift=(directoryLevels-1-depth)*directoryBits;
        const auto slot=(id>>shift)&directoryMask;
        next->children[slot]=replacePartition(
            next->children[slot],id,depth+1,replacement,copiedNodes);
    }
    if(!next->partition) {
        bool empty=true;
        for(const auto& child:next->children)
            if(child) { empty=false; break; }
        if(empty)return nullptr;
    }
    return next;
}
} // namespace

MeshRevision::MeshRevision(std::shared_ptr<const MeshDirectoryNode> root,
                           std::size_t count)
    : root_(std::move(root)),partitionCount_(count) {}

std::shared_ptr<const MeshRevision> MeshRevision::empty() {
    return std::shared_ptr<const MeshRevision>(new MeshRevision(nullptr,0));
}
std::shared_ptr<const MeshPartition> MeshRevision::partition(PartitionId id) const {
    return findPartition(root_,id);
}
MutableMeshView MeshRevision::createMutableView() const {
    return MutableMeshView(*this);
}
MutableMeshView::MutableMeshView(const MeshRevision& base)
    :root_(base.root_),partitionCount_(base.partitionCount_) {}

MeshPartition& MutableMeshView::editPartition(PartitionId id) {
    if(committed_)throw std::logic_error("Cannot edit a committed mesh view");
    const auto [it,inserted]=changes_.try_emplace(id);
    auto& change=it->second;
    if(inserted || change.removed) {
        auto previous=change.removed ? nullptr : findPartition(root_,id);
        change.partition=previous ? std::make_shared<MeshPartition>(*previous)
                                  : std::make_shared<MeshPartition>();
        change.removed=false;
        if(previous)++clonedPartitions_;
    }
    return *change.partition;
}
void MutableMeshView::erasePartition(PartitionId id) {
    if(committed_)throw std::logic_error("Cannot edit a committed mesh view");
    if(!changes_.contains(id) && !findPartition(root_,id))return;
    changes_[id]={nullptr,true};
}
RevisionCommit MutableMeshView::commit() {
    if(committed_)throw std::logic_error("Cannot commit a mesh view twice");
    // Build privately: if allocation throws, the view remains usable.
    auto nextRoot=root_;
    std::size_t nextCount=partitionCount_;
    RevisionCommitStatistics stats;
    stats.clonedPartitions=clonedPartitions_;
    for(const auto& [id,change]:changes_) {
        const bool before=bool(findPartition(root_,id));
        const bool after=!change.removed;
        if(!before && !after)continue;
        if(before && !after)--nextCount;
        if(!before && after)++nextCount;
        nextRoot=replacePartition(nextRoot,id,0,change.partition,
                                  stats.copiedDirectoryNodes);
        ++stats.modifiedPartitions;
    }
    auto next=std::shared_ptr<const MeshRevision>(
        new MeshRevision(std::move(nextRoot),nextCount));
    committed_=true;
    changes_.clear();
    return {std::move(next),stats};
}

MeshRepository::MeshRepository(std::shared_ptr<const MeshRevision> initial)
    :published_(std::move(initial)) {
    if(!published_.load())throw std::invalid_argument("Initial revision is null");
}
std::shared_ptr<const MeshRevision> MeshRepository::snapshot() const noexcept {
    return published_.load(std::memory_order_acquire);
}
void MeshRepository::publish(std::shared_ptr<const MeshRevision> next) {
    if(!next)throw std::invalid_argument("Cannot publish null mesh revision");
    published_.store(std::move(next),std::memory_order_release);
}

} // namespace mesher
