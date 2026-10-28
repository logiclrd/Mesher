#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace mesher {

// Distinct from a refused repair: leaving debug mode intentionally wakes
// the paused producer without allowing it to publish an unfinished edit.
class RepairStepCancelled final : public std::exception {
public:
    const char* what() const noexcept override {
        return "R06 step-debugger cancelled";
    }
};

// The producer owns all mutable staging state. Only independently owned,
// immutable frames cross to the render/event thread. At most two frames are
// retained by the channel; consumers can keep a frame alive for GPU upload.
// The producer never owns the channel mutex while running geometry code.
template<class Frame>
class RepairStepGate final {
public:
    struct View {
        std::shared_ptr<const Frame> current;
        std::shared_ptr<const Frame> previous;
        std::shared_ptr<const Frame> displayed;
        std::size_t sequence{};
        bool waiting=false;
        bool running=false;
        bool showingPrevious=false;
        bool finished=false;
        bool cancelled=false;
    };

    RepairStepGate()=default;
    RepairStepGate(const RepairStepGate&)=delete;
    RepairStepGate& operator=(const RepairStepGate&)=delete;
    ~RepairStepGate() {cancel();}

    // Called by ONE calculation thread after a whole logical stage operation.
    // A failed/cancelled attempt never reaches a published mesh revision.
    void publishAndWait(Frame frame) {
        auto immutable=std::make_shared<const Frame>(std::move(frame));
        std::unique_lock lock(mutex_);
        if(cancelled_)throw RepairStepCancelled{};
        if(finished_)throw std::logic_error("R06 step gate received frame after completion");
        previous_=std::move(current_);
        current_=std::move(immutable);
        showingPrevious_=false;
        ++sequence_;
        waiting_=!running_;
        changed_.notify_all();
        changed_.wait(lock,[this] {
            return cancelled_||running_||!waiting_;
        });
        if(cancelled_)throw RepairStepCancelled{};
    }

    // The viewport controls only the current producer pause; commands
    // issued before the next operation are NOT accumulated as step credits.
    bool next() {
        std::lock_guard lock(mutex_);
        if(cancelled_||finished_||running_||!waiting_)return false;
        waiting_=false;
        changed_.notify_all();
        return true;
    }
    void continueRunning() {
        std::lock_guard lock(mutex_);
        if(cancelled_||finished_)return;
        running_=true;
        waiting_=false;
        changed_.notify_all();
    }
    bool showPrevious() {
        std::lock_guard lock(mutex_);
        if(!previous_)return false;
        showingPrevious_=true;
        return true;
    }
    void showCurrent() {
        std::lock_guard lock(mutex_);
        showingPrevious_=false;
    }
    void finish() {
        std::lock_guard lock(mutex_);
        finished_=true;
        waiting_=false;
        changed_.notify_all();
    }
    void cancel() {
        std::lock_guard lock(mutex_);
        cancelled_=true;
        waiting_=false;
        changed_.notify_all();
    }
    View view() const {
        std::lock_guard lock(mutex_);
        return {current_,previous_,
                showingPrevious_&&previous_?previous_:current_,
                sequence_,waiting_,running_,showingPrevious_,finished_,cancelled_};
    }
    // Headless tests / consumer optional event-driven waiting. No timeout
    // or sleep is used by the Vulkan update loop: it may simply poll view().
    template<class Rep,class Period>
    bool waitForSequence(std::size_t requested,
                         std::chrono::duration<Rep,Period> timeout) const {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock,timeout,[&] {
            return sequence_>=requested||finished_||cancelled_;
        })&&sequence_>=requested;
    }
private:
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    std::shared_ptr<const Frame> current_,previous_;
    std::size_t sequence_{};
    bool waiting_=false,running_=false,showingPrevious_=false;
    bool finished_=false,cancelled_=false;
};

} // namespace mesher
