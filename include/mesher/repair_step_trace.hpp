#pragma once

#include "mesher/repair_step_gate.hpp"
#include "mesher/repair_step_snapshot.hpp"
#include "mesher/repair_mixed_closure.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace mesher {

// M16C3: opt-in CPU-only observation of REAL private R06 execution. A gate
// controls the calculating thread, never the Vulkan thread or a published root.
// Each frame owns the changing geometry ledger but shares the immutable R03
// source. The input may be a trusted strict attempt or an explicitly untrusted
// quality-warning inspection attempt; neither is a publishable MeshRevision.
class RepairStepTrace final {
public:
    using Gate=RepairStepGate<RepairStepGeometry>;
    explicit RepairStepTrace(std::size_t maxFrameBytes=16*1024*1024)
        :maxFrameBytes_(maxFrameBytes) {
        if(!maxFrameBytes_)throw std::invalid_argument(
            "R06-M16C3 requires nonzero per-frame memory budget");
    }

    Gate& gate() noexcept {return gate_;}
    const Gate& gate() const noexcept {return gate_;}

    // The callbacks retain a shared owning pointer and are installed only
    // in an explicitly traced run. Ordinary R06 bypasses every snapshot and
    // every condition-variable wait.
    void attach(MixedPrivateClosureLimits& policy,
                const std::shared_ptr<RepairStepTrace>& owner,
                bool warningOnly=false) {
        if(!owner||owner.get()!=this)
            throw std::invalid_argument("R06-M16C3 requires owned trace lifetime");
        policy.staging.operationObserver=[owner,warningOnly](
            const MixedClosureStaging& stage,const std::string& op,
            const std::string& arguments) {
            owner->gate_.publishAndWait(captureRepairStepGeometry(
                stage,op,(warningOnly?"UNTRUSTED_QUALITY_WARNING_PREVIEW ":"")
                          +arguments,owner->maxFrameBytes_));
        };
        policy.terminalObserver=[owner](const MixedClosureTerminalEvent& event) {
            owner->recordTerminal(event);
        };
    }

    struct ClassifierDisplay {
        std::array<std::string,2> choices{{"UNAVAILABLE","UNAVAILABLE"}};
        bool comparable=false,agree=false;
        int active=0;
    };
    void setClassifier(ClassifierDisplay value) {
        std::lock_guard lock(mutex_);
        classifier_=std::move(value);
    }
    ClassifierDisplay classifier() const {
        std::lock_guard lock(mutex_);
        return classifier_;
    }
    void recordTerminal(const MixedClosureTerminalEvent& event) {
        std::lock_guard lock(mutex_);
        // A strict failure followed by a warning-only rerun is legitimate;
        // never let this become an unbounded diagnostic history.
        if(events_.size()<16)events_.push_back(event);
    }
    std::vector<MixedClosureTerminalEvent> terminalEvents() const {
        std::lock_guard lock(mutex_);
        return events_;
    }
    void finish() {gate_.finish();}
    void cancel() {gate_.cancel();}
private:
    std::size_t maxFrameBytes_;
    Gate gate_;
    mutable std::mutex mutex_;
    std::vector<MixedClosureTerminalEvent> events_;
    ClassifierDisplay classifier_{};
};

} // namespace mesher
