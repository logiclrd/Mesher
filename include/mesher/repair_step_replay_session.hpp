#pragma once
#include "mesher/repair_case.hpp"
#include "mesher/repair_inspection.hpp"
#include "mesher/repair_step_trace.hpp"
#include "mesher/repair_step_scene.hpp"
#include "mesher/viewport.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace mesher {

// One opt-in exact archived repair. Run only from an initialized viewport's
// onReady callback. The worker NEVER touches Vulkan, partition buffers,
// mutable renderer state, revision CAS, checkpoints or sculpt history.
class RepairReplayStepSession final {
public:
    RepairReplayStepSession(RepairCase saved,
        AutoClosureClassifier classifier,bool warningOnly,
        bool seamAudit,bool exactOverlay,bool scaleAware=false,
        bool optimizeFront=false,bool optimizeCurvatureFront=false,
        bool guideCurvaturePoisson=false,
        bool probeSeamFeasibility=false,
        bool probeDiagonalFeasibility=false,
        bool stageFeasibilityPreview=false,
        bool stageDiagonalPreview=false,
        bool diagnoseRecursiveCap=false);
    ~RepairReplayStepSession();
    RepairReplayStepSession(const RepairReplayStepSession&)=delete;
    RepairReplayStepSession& operator=(const RepairReplayStepSession&)=delete;
    void start();
    void command(int cmd); // 0=Next, 1=Previous, 2=Current, 3=Continue, 4=Cancel
    std::optional<ViewportMeshUpdate> takeSnapshot(); // Vulkan thread only
    std::string caption() const;
    void cancelAndJoin();
private:
    RepairCase saved_;
    AutoClosureClassifier classifier_;
    bool warningOnly_,seamAudit_,exactOverlay_,scaleAware_,optimizeFront_,
         optimizeCurvatureFront_,guideCurvaturePoisson_,
         probeSeamFeasibility_,probeDiagonalFeasibility_,
         stageFeasibilityPreview_,stageDiagonalPreview_,diagnoseRecursiveCap_;
    std::shared_ptr<RepairStepTrace> trace_;
    std::thread worker_;
    mutable std::mutex stateMutex_;
    std::shared_ptr<const RepairInspectionSnapshot> result_;
    std::string error_;
    std::atomic<bool> started_{false},done_{false};
    // Only polled/updated by Vulkan thread.
    const RepairStepGeometry* lastFrame_{};
    std::size_t lastSequence_{};
    bool lastFinished_=false,lastCancelled_=false,lastDone_=false;
    bool lastWaiting_=false,lastRunning_=false;
    bool presentedInitial_=false;
};
} // namespace mesher
