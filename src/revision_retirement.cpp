#include "mesher/revision_retirement.hpp"

#include <stdexcept>
#include <utility>

namespace mesher {
RevisionRetirementQueue::RevisionRetirementQueue(std::size_t maxPending)
    :maxPending_(maxPending),
     worker_([this](std::stop_token token){run(token);}) {
    if(!maxPending_)
        throw std::invalid_argument("Revision retirement capacity must be positive");
}
RevisionRetirementQueue::~RevisionRetirementQueue() {
    // Stop means finish everything currently pending and then exit. A caller
    // can hold an old root beyond queue destruction: shared ownership works.
    worker_.request_stop();
    available_.notify_all();
    worker_.join();
}
void RevisionRetirementQueue::retire(
    std::shared_ptr<const RegionTopologyIndex> old) {
    if(!old)return;
    std::unique_lock lock(mutex_);
    if(queue_.size()>=maxPending_)++counters_.backpressureWaits;
    available_.wait(lock,[this]{return queue_.size()<maxPending_;});
    queue_.push_back(std::move(old));
    ++counters_.submittedRoots;
    counters_.queuedRoots=queue_.size();
    if(counters_.queuedRoots>counters_.peakQueuedRoots)
        counters_.peakQueuedRoots=counters_.queuedRoots;
    available_.notify_all();
}
RevisionRetirementStatistics RevisionRetirementQueue::statistics() const {
    std::lock_guard lock(mutex_);
    return counters_;
}
void RevisionRetirementQueue::drain() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock,[this]{return queue_.empty()&&!processing_;});
}
void RevisionRetirementQueue::run(std::stop_token stop) {
    std::unique_lock lock(mutex_);
    while(true) {
        available_.wait(lock,[&]{return stop.stop_requested()||!queue_.empty();});
        if(queue_.empty()&&stop.stop_requested())break;
        if(queue_.empty())continue;
        auto old=std::move(queue_.front());
        queue_.pop_front();
        processing_=true;
        counters_.queuedRoots=queue_.size();
        available_.notify_all();
        lock.unlock();
        old.reset(); // Potentially expensive persistent graph destruction.
        lock.lock();
        ++counters_.releasedReferences;
        processing_=false;
        if(queue_.empty())idle_.notify_all();
    }
    idle_.notify_all();
}
} // namespace mesher
