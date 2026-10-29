#include "mesher/versioned_mesh.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mesher {

namespace {
constexpr unsigned directoryLevels = 8; // 32-bit IDs, four bits per level.
constexpr unsigned directoryBits = 4;
constexpr unsigned directoryMask = 15;
std::uint64_t allocateId(std::atomic<std::uint64_t>& next) {
    auto current=next.load(std::memory_order_relaxed);
    for(;;) {
        if(current==std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Mesh element ID space exhausted");
        if(next.compare_exchange_weak(current,current+1,std::memory_order_relaxed))
            return current;
    }
}
}

// The shared identity source is separate from immutable geometry.
class MeshIdentityDomain {
public:
    explicit MeshIdentityDomain(MeshIdentityWatermarks marks={})
        :nextVertex_(marks.nextVertex),nextFace_(marks.nextFace),
         nextOverflow_(marks.nextOverflow) {}
    MeshIdentityWatermarks watermarks() const noexcept {
        return {nextVertex_.load(),nextFace_.load(),nextOverflow_.load()};
    }
    VertexId vertex() { return allocateId(nextVertex_); }
    FaceId face() { return allocateId(nextFace_); }
    PartitionId overflowPartition() {
        const auto ordinal=allocateId(nextOverflow_);
        if(ordinal>std::uint64_t{0x7fffffff})
            throw std::overflow_error("Mesh overflow partition ID space exhausted");
        return std::numeric_limits<PartitionId>::max()-
               static_cast<PartitionId>(ordinal-1);
    }
private:
    std::atomic<std::uint64_t> nextVertex_{1};
    std::atomic<std::uint64_t> nextFace_{1};
    std::atomic<std::uint64_t> nextOverflow_{1};
};

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
void visitPartition(const std::shared_ptr<const MeshDirectoryNode>& node,
                    unsigned depth, PartitionId id,
                    const std::function<void(PartitionId,const MeshPartition&)>& visit) {
    if(!node)return;
    if(depth==directoryLevels) {
        if(node->partition)visit(id,*node->partition);
        return;
    }
    for(std::uint32_t i=0;i<16;++i)
        if(node->children[i])
            visitPartition(node->children[i],depth+1,
                           static_cast<PartitionId>((id<<directoryBits)|i),visit);
}
} // namespace

MeshRevision::MeshRevision(std::shared_ptr<const MeshDirectoryNode> root,
                           std::size_t count,
                           std::shared_ptr<MeshIdentityDomain> identities)
    : root_(std::move(root)),partitionCount_(count),
      identities_(std::move(identities)) {}

MeshIdentityWatermarks MeshRevision::identityWatermarks() const noexcept {
    return identities_->watermarks();
}
std::shared_ptr<const MeshRevision> MeshRevision::restore(
    std::vector<std::pair<PartitionId,MeshPartition>> partitions,
    MeshIdentityWatermarks marks) {
    if(!marks.nextVertex||!marks.nextFace||!marks.nextOverflow||
       marks.nextOverflow>std::uint64_t{0x80000000})
        throw std::invalid_argument("Invalid checkpoint identity high water");
    std::unordered_set<PartitionId> owners;
    std::unordered_set<VertexId> vertices;
    std::unordered_set<FaceId> faces;
    for(const auto& [id,p]:partitions) {
        if(!owners.insert(id).second)
            throw std::invalid_argument("Duplicate checkpoint partition");
        if(id>=0x80000000u) {
            const auto ordinal=std::uint64_t{0xffffffff}-id+1;
            if(marks.nextOverflow<=ordinal)
                throw std::invalid_argument("Checkpoint overflow high water regressed");
        }
        if(p.vertices.size()>256||p.faces.size()>256)
            throw std::invalid_argument("Checkpoint partition exceeds 256 elements");
        for(const auto& v:p.vertices) {
            if(!v.id||v.id>=marks.nextVertex||
               !std::isfinite(v.position.x)||!std::isfinite(v.position.y)||
               !std::isfinite(v.position.z)||!vertices.insert(v.id).second||
                !std::isfinite(v.color.r)||!std::isfinite(v.color.g)||
                !std::isfinite(v.color.b)||!std::isfinite(v.color.a)||
                v.color.r<0||v.color.r>1||v.color.g<0||v.color.g>1||
                v.color.b<0||v.color.b>1||v.color.a<0||v.color.a>1)
                throw std::invalid_argument("Invalid checkpoint stable vertex");
        }
        for(const auto& f:p.faces)
            if(!f.id||f.id>=marks.nextFace||!faces.insert(f.id).second)
                throw std::invalid_argument("Invalid checkpoint stable face");
    }
    for(const auto& [_,p]:partitions)
        for(const auto& f:p.faces)
            for(const auto v:f.vertices)
                if(!vertices.contains(v))
                    throw std::invalid_argument("Checkpoint face has missing vertex");
    auto base=std::shared_ptr<const MeshRevision>(
        new MeshRevision(nullptr,0,std::make_shared<MeshIdentityDomain>(marks)));
    auto edit=base->createMutableView();
    for(auto& [id,part]:partitions)edit.editPartition(id)=std::move(part);
    return edit.commit().revision;
}
std::shared_ptr<const MeshRevision> MeshRevision::empty() {
    return std::shared_ptr<const MeshRevision>(
        new MeshRevision(nullptr,0,std::make_shared<MeshIdentityDomain>()));
}
std::shared_ptr<const MeshPartition> MeshRevision::partition(PartitionId id) const {
    return findPartition(root_,id);
}
MutableMeshView MeshRevision::createMutableView() const {
    return MutableMeshView(*this);
}
PartitionId MeshRevision::allocateOverflowPartition() const {
    return identities_->overflowPartition();
}
void MeshRevision::forEachPartition(
    const std::function<void(PartitionId,const MeshPartition&)>& visit) const {
    visitPartition(root_,0,0,visit);
}
MutableMeshView::MutableMeshView(const MeshRevision& base)
    :root_(base.root_),partitionCount_(base.partitionCount_),
     identities_(base.identities_) {}

MeshPartition& MutableMeshView::editPartition(PartitionId id) {
    if(committed_)throw std::logic_error("Cannot edit a committed mesh view");
    const auto [it,inserted]=changes_.try_emplace(id);
    auto& change=it->second;
    if(inserted || change.removed) {
        auto previous=change.removed ? nullptr : findPartition(root_,id);
        change.partition=previous ? std::make_shared<MeshPartition>(*previous)
                                  : std::make_shared<MeshPartition>();
        change.removed=false;
        if(previous) {
            ++clonedPartitions_;
            clonedPartitionBytes_+=previous->vertices.size()*sizeof(PartitionVertex)+
                                   previous->faces.size()*sizeof(PartitionFace);
        }
    }
    return *change.partition;
}
void MutableMeshView::erasePartition(PartitionId id) {
    if(committed_)throw std::logic_error("Cannot edit a committed mesh view");
    if(!changes_.contains(id) && !findPartition(root_,id))return;
    changes_[id]={nullptr,true};
}
VertexId MutableMeshView::addVertex(PartitionId owner, Vec3 position,
                                    VertexRGBA color) {
    auto& partition=editPartition(owner);
    const auto id=identities_->vertex();
    partition.vertices.push_back({id,position,color});
    return id;
}
void MutableMeshView::setVertexColor(PartitionId owner,VertexId vertex,
                                     VertexRGBA color) {
    auto& partition=editPartition(owner);
    const auto it=std::find_if(partition.vertices.begin(),partition.vertices.end(),
                               [vertex](const auto& v){return v.id==vertex;});
    if(it==partition.vertices.end())
        throw std::out_of_range("Paint vertex absent from its owner partition");
    it->color=color;
}
FaceId MutableMeshView::addFace(PartitionId owner,
                                std::array<VertexId,3> vertices) {
    auto& partition=editPartition(owner);
    const auto id=identities_->face();
    partition.faces.push_back({id,vertices});
    return id;
}
RevisionCommit MutableMeshView::commit() {
    if(committed_)throw std::logic_error("Cannot commit a mesh view twice");
    // Build privately: if allocation throws, the view remains usable.
    auto nextRoot=root_;
    std::size_t nextCount=partitionCount_;
    RevisionCommitStatistics stats;
    stats.clonedPartitions=clonedPartitions_;
    stats.clonedPartitionBytes=clonedPartitionBytes_;
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
        new MeshRevision(std::move(nextRoot),nextCount,identities_));
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
bool MeshRepository::publishIfCurrent(
    const std::shared_ptr<const MeshRevision>& expected,
    std::shared_ptr<const MeshRevision> next) {
    if(!expected || !next)
        throw std::invalid_argument("Publish compare-and-swap needs two revisions");
    auto observed=expected;
    return published_.compare_exchange_strong(observed,std::move(next),
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire);
}

} // namespace mesher
