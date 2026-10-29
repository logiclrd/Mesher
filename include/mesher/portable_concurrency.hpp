#pragma once

// C++20 shared ownership publication and cooperative threads.
// Apple libc++ (Xcode 16) does not yet supply atomic<shared_ptr> or
// std::jthread/stop_token. Preserve the same semantics using the established
// shared_ptr atomic free functions and a joining std::thread on that toolchain.
// On fully supporting standard libraries, use their native facilities.

#include <atomic>
#include <memory>
#include <thread>
#include <utility>
#include <version>

namespace mesher {

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
template<class T>
using AtomicSharedPtr=std::atomic<std::shared_ptr<T>>;
#else
template<class T>
class AtomicSharedPtr {
public:
    explicit AtomicSharedPtr(std::shared_ptr<T> initial={}) noexcept
        :value_(std::move(initial)) {}
    AtomicSharedPtr(const AtomicSharedPtr&)=delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&)=delete;

    std::shared_ptr<T> load(
        std::memory_order order=std::memory_order_seq_cst) const noexcept {
        return std::atomic_load_explicit(&value_,order);
    }
    void store(std::shared_ptr<T> next,
               std::memory_order order=std::memory_order_seq_cst) noexcept {
        std::atomic_store_explicit(&value_,std::move(next),order);
    }
    bool compare_exchange_strong(std::shared_ptr<T>& expected,
                                 std::shared_ptr<T> next,
                                 std::memory_order success,
                                 std::memory_order failure) noexcept {
        return std::atomic_compare_exchange_strong_explicit(
            &value_,&expected,std::move(next),success,failure);
    }
private:
    mutable std::shared_ptr<T> value_;
};
#endif

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L && \
    !defined(MESHER_FORCE_THREAD_FALLBACK)
using StopToken=std::stop_token;
using JoiningThread=std::jthread;
#else
class StopToken {
public:
    explicit StopToken(std::shared_ptr<std::atomic<bool>> state) noexcept
        :state_(std::move(state)) {}
    bool stop_requested() const noexcept {
        return state_&&state_->load(std::memory_order_acquire);
    }
private:
    std::shared_ptr<std::atomic<bool>> state_;
};

class JoiningThread {
public:
    JoiningThread()=default;
    template<class Fn>
    explicit JoiningThread(Fn fn)
        :stop_(std::make_shared<std::atomic<bool>>(false)),
         thread_([fn=std::move(fn),token=StopToken(stop_)]() mutable {
             fn(token);
         }) {}
    JoiningThread(const JoiningThread&)=delete;
    JoiningThread& operator=(const JoiningThread&)=delete;
    ~JoiningThread() {
        if(thread_.joinable()) {
            request_stop();
            thread_.join();
        }
    }
    void request_stop() noexcept {
        if(stop_)stop_->store(true,std::memory_order_release);
    }
    bool joinable() const noexcept {return thread_.joinable();}
    void join() {thread_.join();}
private:
    std::shared_ptr<std::atomic<bool>> stop_;
    std::thread thread_;
};
#endif

} // namespace mesher
