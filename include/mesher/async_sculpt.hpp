#pragma once
#include "mesher/brush.hpp"

#include <atomic>
#include <chrono>
#include <stop_token>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

namespace mesher {

// The background worker exclusively owns the sculptable mesh, the brush,
// and the timed BrushStroke. The UI only enqueues input and consumes copies
// of *completed* mesh states. No Vulkan/GLFW calls occur on the worker.
class AsyncSculptWorker {
public:
    struct Snapshot {
        Mesh mesh;
        std::size_t completedStamps=0;
    };
    struct Status {
        std::size_t completedStamps=0;
        std::size_t totalQueuedSteps=0;
        std::size_t pendingSteps=0;
        std::size_t pendingSegments=0;
        std::size_t inputCommands=0;
        bool active=false;
        StampTimings timings{};
        StampDetails details{};
        double lastWorkerStampMs=0;
    };
    AsyncSculptWorker(Mesh initial, double step, double strength,
                      double timeSpeed, CurvatureSettings curvature,
                      CollapseSettings collapse, RefineSettings refine,
                      bool symmetric);
    ~AsyncSculptWorker();
    AsyncSculptWorker(const AsyncSculptWorker&)=delete;
    AsyncSculptWorker& operator=(const AsyncSculptWorker&)=delete;

    void begin(Vec3 point,double radius);
    void update(Vec3 point,double elapsedSeconds);
    void end(Vec3 point,double elapsedSeconds);
    void setRadius(double radius);

    bool canBegin() const noexcept {return !reserved_.load();}
    Status status() const;
    std::optional<Snapshot> takeSnapshot();

private:
    enum class Kind {Begin,Update,End,Radius};
    struct Command {Kind kind;Vec3 point{};double elapsed=0,radius=0;};
    void enqueue(Command command);
    void run(std::stop_token token);
    void publish(bool force=false);
    void refreshStatus();
    std::vector<Vec3> centers(Vec3 p) const;

    Mesh mesh_;
    AdaptiveInflateBrush brush_;
    BrushStroke stroke_;
    double timeSpeed_;
    bool symmetric_;
    std::mutex commandsMutex_;
    std::condition_variable_any ready_;
    std::deque<Command> commands_;
    std::atomic<bool> reserved_{false};
    mutable std::mutex statusMutex_;
    Status status_{};
    std::exception_ptr error_;
    std::mutex snapshotMutex_;
    std::optional<Snapshot> latest_;
    std::chrono::steady_clock::time_point lastPublish_{};
    std::jthread worker_;
};
} // namespace mesher
