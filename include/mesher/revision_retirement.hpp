#pragma once

#include "mesher/region_edit.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace mesher {

// Only the retirement thread drops queued references to old immutable mesh
// graphs. At most maxPending roots await release: saturation uses explicit
// backpressure rather than silently retaining unbounded version history.
// A reader may keep any revision independently of this application queue.
struct RevisionRetirementStatistics {
    std::size_t queuedRoots{};
    std::size_t peakQueuedRoots{};
    std::size_t submittedRoots{};
    std::size_t releasedReferences{};
    std::size_t backpressureWaits{};
};

class RevisionRetirementQueue {
public:
    explicit RevisionRetirementQueue(std::size_t maxPending=64);
    ~RevisionRetirementQueue();
    RevisionRetirementQueue(const RevisionRetirementQueue&)=delete;
    RevisionRetirementQueue& operator=(const RevisionRetirementQueue&)=delete;

    // Hands off an old root. Zero-cost for null roots. Enqueuing is normally
    // short; if the queue is saturated the producer is backpressured.
    void retire(std::shared_ptr<const RegionTopologyIndex> old);
    RevisionRetirementStatistics statistics() const;
    // Synchronization hook for deterministic tests and orderly shutdown;
    // not part of the interactive frame loop.
    void drain();

private:
    void run(std::stop_token stop);
    const std::size_t maxPending_;
    mutable std::mutex mutex_;
    std::condition_variable available_,idle_;
    std::deque<std::shared_ptr<const RegionTopologyIndex>> queue_;
    RevisionRetirementStatistics counters_;
    bool processing_{};
    std::jthread worker_;
};

} // namespace mesher
