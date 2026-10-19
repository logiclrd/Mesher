#include "mesher/async_sculpt.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mesher {
AsyncSculptWorker::AsyncSculptWorker(Mesh initial,double step,double strength,
                                     double timeSpeed,CurvatureSettings curvature,
                                     CollapseSettings collapse,RefineSettings refine,
                                     bool symmetric,SculptStepValidator validator,
                                     SculptCollisionSettings collision)
    :brush_(std::make_unique<VersionedSculptBrush>(
         initial,strength,curvature,collapse,refine,symmetric,256,
         std::move(validator),std::move(collision))),
     stroke_(samplingOnlyMesh_,*brush_,step),timeSpeed_([](double t) {
        if(!std::isfinite(t)||t<=0)
            throw std::invalid_argument("Sculpt time speed must be positive");
        return t;
     }(timeSpeed)),symmetric_(symmetric),
     worker_([this](std::stop_token token){run(token);}) {}
AsyncSculptWorker::AsyncSculptWorker(
    DurableCheckpoint recovered,double step,double strength,double timeSpeed,
    CurvatureSettings curvature,CollapseSettings collapse,RefineSettings refine,
    SculptStepValidator validator,SculptCollisionSettings collision)
    :brush_(std::make_unique<VersionedSculptBrush>(
         recovered,strength,curvature,collapse,refine,256,
         std::move(validator),std::move(collision))),
     stroke_(samplingOnlyMesh_,*brush_,step),timeSpeed_([](double t) {
        if(!std::isfinite(t)||t<=0)
            throw std::invalid_argument("Sculpt time speed must be positive");
        return t;
     }(timeSpeed)),symmetric_(!recovered.mirrorPairs.empty()),
     worker_([this](std::stop_token token){run(token);}) {}
AsyncSculptWorker::~AsyncSculptWorker() {
    worker_.request_stop();
    ready_.notify_all();
    worker_.join();
}
std::vector<Vec3> AsyncSculptWorker::centers(Vec3 point) const {
    if(symmetric_)return {point,{-point.x,point.y,point.z}};
    return {point};
}
void AsyncSculptWorker::enqueue(Command command) {
    {
        std::lock_guard guard(statusMutex_);
        if(error_)std::rethrow_exception(error_);
    }
    {
        std::lock_guard guard(commandsMutex_);
        commands_.push_back(std::move(command));
    }
    ready_.notify_one();
}
void AsyncSculptWorker::begin(
    Vec3 point,double radius,std::vector<FaceId> intendedOriginals) {
    if(reserved_.exchange(true))
        throw std::logic_error("Previous sculpt stroke is still processing");
    Command command{Kind::Begin,point,0,radius};
    command.intendedOriginals=std::move(intendedOriginals);
    try {enqueue(std::move(command));}
    catch(...) {reserved_=false;throw;}
}
void AsyncSculptWorker::update(Vec3 point,double elapsedSeconds) {
    enqueue({Kind::Update,point,elapsedSeconds,0});
}
void AsyncSculptWorker::end(Vec3 point,double elapsedSeconds) {
    enqueue({Kind::End,point,elapsedSeconds,0});
}
void AsyncSculptWorker::setRadius(double radius) {
    enqueue({Kind::Radius,{},0,radius});
}
void AsyncSculptWorker::undo() {
    if(!reserved_.load())enqueue({Kind::Undo});
}
void AsyncSculptWorker::redo() {
    if(!reserved_.load())enqueue({Kind::Redo});
}
void AsyncSculptWorker::mergeExactContacts(
    std::shared_ptr<const RegionTopologyIndex> expected,
    std::vector<RegionTopologyIndex::CollisionPair> contacts,
    ExactCutStepLimits limits,std::vector<FaceId> intendedOriginals) {
    if(!expected||contacts.empty())
        throw std::invalid_argument(
            "Worker union requires original snapshot and explicit contacts");
    if(reserved_.load())
        throw std::logic_error(
            "Cannot enqueue union while a sculpt stroke is reserved");
    Command command{Kind::Merge};
    command.expected=std::move(expected);
    command.contacts=std::move(contacts);
    command.mergeLimits=std::move(limits);
    command.intendedOriginals=std::move(intendedOriginals);
    enqueue(std::move(command));
}
void AsyncSculptWorker::mergeDiscoveredContacts(
    std::shared_ptr<const RegionTopologyIndex> expected,
    ExactCutStepLimits limits,SculptSourceContactLimits discovery) {
    if(!expected)
        throw std::invalid_argument("Live Merge needs selected immutable expected root");
    if(reserved_.load())
        throw std::logic_error("Live Merge cannot overtake an active sculpt stroke");
    Command command{Kind::DiscoverMerge};
    command.expected=std::move(expected);
    command.mergeLimits=std::move(limits);
    command.discoveryLimits=std::move(discovery);
    enqueue(std::move(command));
}
void AsyncSculptWorker::saveCheckpoint(std::filesystem::path path) {
    if(path.empty())throw std::invalid_argument("Checkpoint path is empty");
    Command command{Kind::Checkpoint};
    command.checkpointPath=std::move(path);
    enqueue(std::move(command));
}
AsyncSculptWorker::Status AsyncSculptWorker::status() const {
    Status current;
    {
        std::lock_guard guard(statusMutex_);
        if(error_&&!status_.terminalFailure)std::rethrow_exception(error_);
        current=status_;
    }
    // Disk I/O finishes independently of worker stamps; do not leave save
    // completion/error reporting frozen until the next mouse event.
    current.checkpoints=checkpoints_.statistics();
    return current;
}
std::optional<AsyncSculptWorker::Snapshot> AsyncSculptWorker::takeSnapshot() {
    // Only shared immutable roots are exchanged. No geometry copy is made
    // on the worker or while holding this short-lived snapshot lock.
    std::lock_guard guard(snapshotMutex_);
    auto result=std::move(latest_);
    latest_.reset();
    return result;
}
void AsyncSculptWorker::refreshStatus() {
    Status s;
    s.completedStamps=brush_->completedStamps();
    s.rejectedStamps=brush_->rejectedStamps();
    s.collisionCheckedStamps=brush_->collisionCheckedStamps();
    s.collisionRepairedStamps=brush_->collisionRepairedStamps();
    s.completedUnions=brush_->completedUnions();
    s.rejectedUnions=brush_->rejectedUnions();
    s.liveMergeRequests=liveMergeRequests_;
    s.liveMergeRefusals=liveMergeRefusals_;
    s.liveMergeAccepted=liveMergeAccepted_;
    s.liveMergeRefusal=liveMergeRefusal_;
    s.contact=brush_->contactStatistics();
    s.lastRejection=brush_->lastRejection();
    s.totalQueuedSteps=stroke_.totalQueuedSteps();
    s.pendingSteps=stroke_.pendingSamples();
    s.pendingSegments=stroke_.pendingSegments();
    s.active=stroke_.active();
    s.timings=brush_->lastTimings();
    s.details=brush_->lastDetails();
    s.stepResources=brush_->lastStepResourceWork();
    s.resources=brush_->resourceTotals();
    s.completedUndos=completedUndos_;
    s.completedRedos=completedRedos_;
    s.checkpointCommands=checkpointCommands_;
    s.snapshotPublications=snapshotPublications_;
    s.lastSnapshotDirtyOwners=lastSnapshotDirtyOwners_;
    s.totalSnapshotDirtyOwners=totalSnapshotDirtyOwners_;
    s.publishedRevisions=brush_->publishedRevisions();
    s.modifiedPartitions=brush_->modifiedPartitions();
    s.copiedDirectoryNodes=brush_->copiedDirectoryNodes();
    s.clonedPartitionBytes=brush_->clonedPartitionBytes();
    s.history=brush_->historyStatistics();
    s.checkpoints=checkpoints_.statistics();
    s.liveRevisions=RegionTopologyIndex::liveRevisionCount();
    s.peakLiveRevisions=RegionTopologyIndex::peakLiveRevisionCount();
    {
        std::lock_guard guard(commandsMutex_);
        s.inputCommands=commands_.size();
    }
    {
        std::lock_guard guard(statusMutex_);
        s.lastWorkerStampMs=status_.lastWorkerStampMs;
        s.lastPublicationMs=status_.lastPublicationMs;
        status_=std::move(s);
    }
}
void AsyncSculptWorker::publish(bool force) {
    using Clock=std::chrono::steady_clock;
    const auto now=Clock::now();
    // Coalesce states when many stamps complete within one video frame.
    // The UI sees the newest committed mesh; no stamps are skipped.
    if(!force && lastPublish_.time_since_epoch().count()!=0 &&
       now-lastPublish_<std::chrono::milliseconds(8))return;
    const auto started=Clock::now();
    Snapshot snapshot{brush_->snapshot(),brush_->originalOrder(),
                      brush_->completedStamps(),std::move(unpublishedDirty_)};
    unpublishedDirty_.clear();
    {
        std::lock_guard guard(snapshotMutex_);
        // A frame can coalesce multiple published revisions; union the dirty
        // IDs until a consumer has actually taken the previous snapshot.
        if(latest_) {
            auto& combined=snapshot.dirtyPartitions;
            combined.insert(combined.end(),latest_->dirtyPartitions.begin(),
                            latest_->dirtyPartitions.end());
        }
        auto& all=snapshot.dirtyPartitions;
        std::sort(all.begin(),all.end());
        all.erase(std::unique(all.begin(),all.end()),all.end());
        lastSnapshotDirtyOwners_=all.size();
        ++snapshotPublications_;
        totalSnapshotDirtyOwners_+=lastSnapshotDirtyOwners_;
        latest_=std::move(snapshot);
    }
    lastPublish_=now;
    {
        std::lock_guard guard(statusMutex_);
        status_.lastPublicationMs=std::chrono::duration<double,std::milli>(
            Clock::now()-started).count();
        // refreshStatus runs before publish(). Keep snapshot telemetry
        // coherent with the actual latest published status even at idle.
        status_.snapshotPublications=snapshotPublications_;
        status_.lastSnapshotDirtyOwners=lastSnapshotDirtyOwners_;
        status_.totalSnapshotDirtyOwners=totalSnapshotDirtyOwners_;
    }
}
void AsyncSculptWorker::run(std::stop_token token) {
    using Clock=std::chrono::steady_clock;
    // The UI can reserve and enqueue the NEXT Begin while this worker is
    // completing an unrelated queued Merge/Undo/Checkpoint iteration.
    // A raw reserved_ check at the end of THAT iteration must not clear
    // a new reservation before the worker has even consumed its Begin.
    // This flag is WORKER-owned: only a successfully started stroke may
    // release its corresponding producer reservation on completion.
    bool workerOwnsStartedStroke=false;
    try {
        while(!token.stop_requested()) {
            std::optional<Command> command;
            {
                std::unique_lock lock(commandsMutex_);
                if(commands_.empty() && stroke_.pendingSamples()==0)
                    ready_.wait(lock,token,[&]{return !commands_.empty();});
                if(token.stop_requested())break;
                if(!commands_.empty()) {
                    command=commands_.front();
                    commands_.pop_front();
                }
            }
            bool changed=false;
            if(command) {
                switch(command->kind) {
                case Kind::Begin: {
                    brush_->setOriginalPatchIntent(std::move(command->intendedOriginals));
                    auto areas=std::vector<BrushArea>{{command->point,command->radius,.3}};
                    if(symmetric_)areas.push_back({{-command->point.x,
                        command->point.y,command->point.z},command->radius,.3});
                    stroke_.startTimed(std::move(areas),timeSpeed_);
                    workerOwnsStartedStroke=true;
                    // startTimed synchronously applies the mouse-down stamp;
                    // it does not pass through pumpTimedRealtime(), so its
                    // dirty halo must be collected separately.
                    {
                        const auto& dirty=brush_->lastDirtyPartitions();
                        unpublishedDirty_.insert(unpublishedDirty_.end(),
                                                  dirty.begin(),dirty.end());
                    }
                    changed=true; // The first stamp happens at mouse-down.
                    break;
                }
                case Kind::Update:
                    stroke_.updateTimedRealtime(centers(command->point),command->elapsed);
                    break;
                case Kind::End:
                    stroke_.finishTimedRealtime(centers(command->point),command->elapsed);
                    break;
                case Kind::Radius:
                    if(stroke_.active())stroke_.setRadius(command->radius);
                    break;
                case Kind::Undo:
                case Kind::Redo:
                    if(!stroke_.active()&&stroke_.pendingSamples()==0) {
                        const bool restored=command->kind==Kind::Undo?
                            brush_->undo():brush_->redo();
                        if(restored) {
                            if(command->kind==Kind::Undo)++completedUndos_;
                            else ++completedRedos_;
                            const auto& dirty=brush_->lastDirtyPartitions();
                            unpublishedDirty_.insert(unpublishedDirty_.end(),
                                                      dirty.begin(),dirty.end());
                            changed=true;
                        }
                    }
                    break;
                case Kind::Checkpoint:
                    checkpoints_.request(command->checkpointPath,
                                         brush_->checkpoint());
                    ++checkpointCommands_;
                    break;
                case Kind::DiscoverMerge: {
                    ++liveMergeRequests_;
                    auto refuse=[&](std::string reason) {
                        ++liveMergeRefusals_;
                        liveMergeRefusal_=std::move(reason);
                    };
                    if(stroke_.active()||stroke_.pendingSamples()!=0) {
                        refuse("Live Merge requires idle source worker");
                        break;
                    }
                    const auto selected=brush_->snapshot();
                    if(!selected||selected!=command->expected) {
                        refuse("Live Merge selected root is stale");
                        break;
                    }
                    const auto firstRoot=brush_->firstOriginalSourceRoot();
                    const auto order=brush_->originalOrder();
                    if(!firstRoot||!order||order->faceIds.empty()) {
                        refuse("Live Merge requires the trusted initial first-original FaceId source, not a disk-only or OFF-mode root");
                        break;
                    }
                    // No selection fallback to XYZ coincidence, and no
                    // expensive whole-root scan on the Vulkan thread.
                    const auto assessed=brush_->discoverLiveMergeContacts(
                        command->discoveryLimits);
                    if(!assessed.ready()||assessed.originalCandidate!=selected) {
                        refuse(assessed.status==SculptSourceContactStatus::Clean?
                               "Live Merge found no intersecting source contacts":
                               assessed.refusal.empty()?
                               "Live Merge source contact discovery refused":
                               assessed.refusal);
                        break;
                    }
                    const bool merged=brush_->mergeExactContacts(
                        selected,assessed.contacts,command->mergeLimits,
                        order->faceIds);
                    if(!merged) {
                        refuse(brush_->lastRejection());
                        break;
                    }
                    liveMergeRefusal_.clear();
                    ++liveMergeAccepted_;
                    const auto& dirty=brush_->lastDirtyPartitions();
                    unpublishedDirty_.insert(unpublishedDirty_.end(),
                                              dirty.begin(),dirty.end());
                    changed=true;
                    break;
                }
                case Kind::Merge: {
                    if(stroke_.active()||stroke_.pendingSamples()!=0)
                        throw std::logic_error(
                            "Serialized Boolean union reached an active stroke");
                    const auto merged=brush_->mergeExactContacts(
                        command->expected,command->contacts,
                        command->mergeLimits,command->intendedOriginals);
                    if(merged) {
                        const auto& dirty=brush_->lastDirtyPartitions();
                        unpublishedDirty_.insert(unpublishedDirty_.end(),
                                                  dirty.begin(),dirty.end());
                        changed=true;
                    }
                    break;
                }
                }
            }
            // Input capture is decoupled from processing. The worker pumps
            // without waiting for a Vulkan frame; each pump is still exactly
            // one consecutive 4D Step, including its mirrored counterpart.
            if(stroke_.active() && stroke_.pendingSamples()>0) {
                const auto started=Clock::now();
                const bool stamped=stroke_.pumpTimedRealtime();
                if(stamped) {
                    changed=true;
                    const auto& dirty=brush_->lastDirtyPartitions();
                    unpublishedDirty_.insert(unpublishedDirty_.end(),
                                              dirty.begin(),dirty.end());
                    std::lock_guard guard(statusMutex_);
                    status_.lastWorkerStampMs=
                        std::chrono::duration<double,std::milli>(
                            Clock::now()-started).count();
                }
            }
            refreshStatus();
            if(changed) {
                bool idle=stroke_.pendingSamples()==0;
                if(idle) {
                    std::lock_guard lock(commandsMutex_);
                    idle=commands_.empty();
                }
                publish(idle || !stroke_.active());
            }
            if(workerOwnsStartedStroke&&!stroke_.active()&&
               stroke_.pendingSamples()==0) {
                // The final root and all accumulated dirty owners must be
                // visible to renderer/picker BEFORE the UI can reserve the
                // next 4D stroke. A queued-but-unprocessed Begin cannot
                // satisfy workerOwnsStartedStroke, even if a previous
                // unrelated command just finished while idle.
                publish(true);
                workerOwnsStartedStroke=false;
                reserved_=false;
            }
        }
    }catch(...) {
        const auto failure=std::current_exception();
        const bool postCas=brush_->terminalFailure();
        {
            std::lock_guard guard(snapshotMutex_);
            // A post-CAS uncertain source is NEVER presented to a renderer.
            // Earlier frames already delivered remain immutable.
            latest_.reset();
            unpublishedDirty_.clear();
        }
        {
            std::lock_guard guard(statusMutex_);
            status_.terminalFailure=postCas;
            if(postCas) {
                status_.terminalFailureReason=brush_->terminalFailureReason();
                status_.lastRejection=brush_->lastRejection();
                status_.stepResources=brush_->lastStepResourceWork();
                status_.resources=brush_->resourceTotals();
            }
            error_=failure;
        }
        reserved_=false;
    }
}
} // namespace mesher
