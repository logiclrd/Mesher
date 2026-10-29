#include "mesher/portable_concurrency.hpp"

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace {

void check(bool okay,const char* message) {
    if(!okay)throw std::runtime_error(message);
}

void testAtomicRevisionPublication() {
    const auto original=std::make_shared<const int>(100);
    const auto updated=std::make_shared<const int>(200);
    const auto unrelated=std::make_shared<const int>(300);
    mesher::AtomicSharedPtr<const int> root(original);
    check(root.load(std::memory_order_acquire)==original,
          "Original immutable root should be readable");

    auto observed=original;
    check(root.compare_exchange_strong(observed,updated,
              std::memory_order_acq_rel,std::memory_order_acquire),
          "Matching original pointer must allow publication");
    check(root.load(std::memory_order_acquire)==updated,
          "Accepted root must become visible");
    observed=original;
    check(!root.compare_exchange_strong(observed,unrelated,
              std::memory_order_acq_rel,std::memory_order_acquire),
          "Stale original pointer cannot overwrite published revision");
    check(observed==updated && root.load()==updated,
          "Failed CAS reports actual current root and does not mutate it");
    root.store(original,std::memory_order_release);
    check(root.load(std::memory_order_acquire)==original,
          "Explicit publication must retain shared ownership");
}

void testWorkerCooperativeShutdown() {
    std::mutex mutex;
    std::condition_variable ready;
    bool exited=false;
    mesher::JoiningThread worker([&](mesher::StopToken stop) {
        std::unique_lock lock(mutex);
        ready.wait(lock,[&] {return stop.stop_requested();});
        exited=true;
    });
    // Same mutex discipline as the existing history and checkpoint worker:
    // stop state must be signaled under the predicate's mutex, then notified.
    {
        std::lock_guard lock(mutex);
        worker.request_stop();
    }
    ready.notify_all();
    worker.join();
    check(exited,"Stop token must wake worker before joining");
}

} // namespace

int main() {
    try {
        testAtomicRevisionPublication();
        testWorkerCooperativeShutdown();
        std::cout<<"Portable atomic root and joining thread tests passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Portable concurrency regression: "<<error.what()<<'\n';
        return 1;
    }
}
