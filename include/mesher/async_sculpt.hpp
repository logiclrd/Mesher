#pragma once
#include "mesher/versioned_sculpt.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/portable_concurrency.hpp"

#include <atomic>
#include <chrono>
#include <stop_token>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace mesher {

// The background worker owns versioned sculpting and the unchanged exact
// 4D BrushStroke sampler. The UI queues input and receives immutable indexed
// revision roots; only the viewport compatibility adapter flattens geometry.
// No Vulkan/GLFW calls or whole-mesh copies occur on the worker.
class AsyncSculptWorker {
public:
    struct Snapshot {
        std::shared_ptr<const RegionTopologyIndex> index;
        std::shared_ptr<const FlatMeshIndex> originalOrder;
        std::size_t completedStamps=0;
        std::vector<PartitionId> dirtyPartitions;
    };
    struct Status {
        std::size_t completedStamps=0;
        std::size_t rejectedStamps=0;
        std::size_t collisionCheckedStamps=0;
        std::size_t collisionRepairedStamps=0;
        std::size_t completedUnions=0;
        std::size_t rejectedUnions=0;
        std::size_t liveMergeRequests=0,liveMergeRefusals=0,liveMergeAccepted=0;
        std::size_t pendingLiveMergeRequests=0;
        std::string liveMergeRefusal;
        SculptContactStatistics contact{};
        std::string lastRejection;
        std::size_t totalQueuedSteps=0;
        std::size_t pendingSteps=0;
        std::size_t pendingSegments=0;
        std::size_t inputCommands=0;
        bool active=false;
        StampTimings timings{};
        StampDetails details{};
        SculptStepResourceWork stepResources{};
        SculptResourceTotals resources{}; // current live session, not disk telemetry
        std::size_t completedUndos{},completedRedos{},checkpointCommands{};
        std::size_t snapshotPublications{},lastSnapshotDirtyOwners{};
        std::size_t totalSnapshotDirtyOwners{};
        bool terminalFailure=false;
        std::string terminalFailureReason;
        double lastWorkerStampMs=0;
        std::size_t publishedRevisions=0;
        std::size_t modifiedPartitions=0;
        std::size_t copiedDirectoryNodes=0;
        std::size_t clonedPartitionBytes=0;
        std::size_t liveRevisions=0;
        std::size_t peakLiveRevisions=0;
        double lastPublicationMs=0;
        HistoryStatistics history{};
        CheckpointStatistics checkpoints{};
    };
    AsyncSculptWorker(Mesh initial, double step, double strength,
                      double timeSpeed, CurvatureSettings curvature,
                      CollapseSettings collapse, RefineSettings refine,
                      bool symmetric,
                      SculptStepValidator validator={},
                      SculptCollisionSettings collision={});
    AsyncSculptWorker(DurableCheckpoint recovered,double step,double strength,
                     double timeSpeed,CurvatureSettings curvature,
                     CollapseSettings collapse,RefineSettings refine,
                     SculptStepValidator validator={},
                     SculptCollisionSettings collision={},
                     // Explicit trusted in-memory FIRST-root face set only;
                     // unverified V13 disk snapshots can never supply it.
                     std::vector<FaceId> trustedFirstOriginalFaces={});

    ~AsyncSculptWorker();
    AsyncSculptWorker(const AsyncSculptWorker&)=delete;
    AsyncSculptWorker& operator=(const AsyncSculptWorker&)=delete;

    // First-source selection belongs to the queued Begin command.
    void begin(Vec3 point,double radius,
               std::vector<FaceId> intendedOriginals={},
               double strengthScale=1.0);
    void update(Vec3 point,double elapsedSeconds);
    void end(Vec3 point,double elapsedSeconds);
    // Explicit diagnostic single-stamp command: no timed 4D input
    // segments, no hold-to-sculpt, and one published snapshot before
    // the next one-shot can be reserved.
    void stampOnce(Vec3 point,double radius);
    void setRadius(double radius);
    void undo();
    void redo();
    // Explicit worker-only opt-in. Contacts and their expected immutable
    // root travel together; queued sculpt changes make this request stale
    // rather than redirecting it to a different surface revision.
    void mergeExactContacts(
        std::shared_ptr<const RegionTopologyIndex> expected,
        std::vector<RegionTopologyIndex::CollisionPair> contacts,
        ExactCutStepLimits limits={},
        std::vector<FaceId> intendedOriginals={});
    // M47 opt-in: snapshot token selected on the UI; the worker itself
    // performs the BOUNDED complete original-contact query after dequeue.
    // No mutable worker mesh or unbounded collision scan on render thread.
    void mergeDiscoveredContacts(
        std::shared_ptr<const RegionTopologyIndex> expected,
        std::vector<FaceId> explicitlySelectedFirstOriginals,
        ExactCutStepLimits limits={},
        SculptSourceContactLimits discovery={});
    void saveCheckpoint(std::filesystem::path destination);
    void paint(std::vector<PaintDab> dabs);
    void beginPaintStroke();
    void endPaintStroke();
    CheckpointStatistics checkpointStatistics() const {
        return checkpoints_.statistics();
    }

    bool canBegin() const noexcept {
        return !reserved_.load()&&!brush_->terminalFailure()&&
               pendingLiveMergeRequests_.load(std::memory_order_acquire)==0;
    }
    Status status() const;
    std::optional<Snapshot> takeSnapshot();
    std::shared_ptr<const RegionTopologyIndex> initialRevision() const noexcept {
        return brush_->snapshot();
    }

private:
    enum class Kind {Begin,SingleStamp,Update,End,Radius,Undo,Redo,Checkpoint,
                     PaintBegin,Paint,PaintEnd,Merge,DiscoverMerge};
    struct Command {
        Kind kind;
        Vec3 point{};
        double elapsed=0,radius=0;
        double strengthScale=1.0;
        std::filesystem::path checkpointPath{};
        std::vector<PaintDab> paintDabs{};
        std::shared_ptr<const RegionTopologyIndex> expected{};
        std::vector<RegionTopologyIndex::CollisionPair> contacts{};
        ExactCutStepLimits mergeLimits{};
        SculptSourceContactLimits discoveryLimits{};
        std::vector<FaceId> intendedOriginals{};
        std::chrono::steady_clock::time_point queuedAt{};
    };
    void enqueue(Command command);
    void run(StopToken token);
    void publish(bool force=false);
    void refreshStatus();
    std::vector<Vec3> centers(Vec3 p) const;

    Mesh samplingOnlyMesh_; // never contains any sculpted geometry
    std::unique_ptr<VersionedSculptBrush> brush_;
    BrushStroke stroke_;
    double timeSpeed_;
    bool symmetric_;
    std::mutex commandsMutex_;
    std::condition_variable_any ready_;
    std::deque<Command> commands_;
    AsyncCheckpointWriter checkpoints_;
    std::atomic<bool> reserved_{false};
    mutable std::mutex statusMutex_;
    Status status_{};
    std::exception_ptr error_;
    std::mutex snapshotMutex_;
    std::optional<Snapshot> latest_;
    // Worker-only accumulation between throttled publications.
    std::vector<PartitionId> unpublishedDirty_;
    std::chrono::steady_clock::time_point lastPublish_{};
    std::size_t completedUndos_{},completedRedos_{},checkpointCommands_{};
    std::size_t liveMergeRequests_{},liveMergeRefusals_{},liveMergeAccepted_{};
    std::atomic<std::size_t> pendingLiveMergeRequests_{0};
    std::string liveMergeRefusal_;
    std::vector<FaceId> trustedFirstOriginalFaces_;
    std::size_t snapshotPublications_{},lastSnapshotDirtyOwners_{};
    std::size_t totalSnapshotDirtyOwners_{};
    JoiningThread worker_;
};
} // namespace mesher
