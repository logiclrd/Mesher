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
                                     bool symmetric)
    :brush_(std::make_unique<VersionedSculptBrush>(
         initial,strength,curvature,collapse,refine,symmetric)),
     stroke_(samplingOnlyMesh_,*brush_,step),timeSpeed_([](double t) {
        if(!std::isfinite(t)||t<=0)
            throw std::invalid_argument("Sculpt time speed must be positive");
        return t;
     }(timeSpeed)),symmetric_(symmetric),
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
        std::lock_guard guard(commandsMutex_);
        commands_.push_back(command);
    }
    ready_.notify_one();
}
void AsyncSculptWorker::begin(Vec3 point,double radius) {
    if(reserved_.exchange(true))
        throw std::logic_error("Previous sculpt stroke is still processing");
    enqueue({Kind::Begin,point,0,radius});
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
AsyncSculptWorker::Status AsyncSculptWorker::status() const {
    std::lock_guard guard(statusMutex_);
    if(error_)std::rethrow_exception(error_);
    return status_;
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
    s.totalQueuedSteps=stroke_.totalQueuedSteps();
    s.pendingSteps=stroke_.pendingSamples();
    s.pendingSegments=stroke_.pendingSegments();
    s.active=stroke_.active();
    s.timings=brush_->lastTimings();
    s.details=brush_->lastDetails();
    s.publishedRevisions=brush_->publishedRevisions();
    s.modifiedPartitions=brush_->modifiedPartitions();
    s.copiedDirectoryNodes=brush_->copiedDirectoryNodes();
    s.clonedPartitionBytes=brush_->clonedPartitionBytes();
    s.liveRevisions=RegionTopologyIndex::liveRevisionCount();
    s.peakLiveRevisions=RegionTopologyIndex::peakLiveRevisionCount();
    {
        std::lock_guard guard(commandsMutex_);
        s.inputCommands=commands_.size();
    }
    {
        std::lock_guard guard(statusMutex_);
        s.lastWorkerStampMs=status_.lastWorkerStampMs;
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
        latest_=std::move(snapshot);
    }
    lastPublish_=now;
}
void AsyncSculptWorker::run(std::stop_token token) {
    using Clock=std::chrono::steady_clock;
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
                    auto areas=std::vector<BrushArea>{{command->point,command->radius,.3}};
                    if(symmetric_)areas.push_back({{-command->point.x,
                        command->point.y,command->point.z},command->radius,.3});
                    stroke_.startTimed(std::move(areas),timeSpeed_);
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
            if(!stroke_.active() && reserved_.load()) {
                publish(true);
                reserved_=false;
            }
        }
    }catch(...) {
        {
            std::lock_guard guard(statusMutex_);
            error_=std::current_exception();
        }
        reserved_=false;
    }
}
} // namespace mesher
