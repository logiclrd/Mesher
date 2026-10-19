#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace mesher {

// V17-M41: monotone, attempted work of ONE executing original-root Step.
// These are real operation counts, NOT inferred from live topology size,
// a CPU-time bound, or a memory-residency estimate.
struct ExactStageWork {
    std::size_t collisionQueries{};
    std::size_t collisionBoxes{},collisionPartitions{};
    std::size_t collisionCandidates{},collisionNarrowPhase{};
    std::size_t windingQueries{};
    std::size_t windingBoxes{},windingPartitions{};
    std::size_t windingTriangles{},windingRays{};
    std::size_t arrangementPairTests{},arrangementPredicates{};
    bool overflow{};
    void add(std::size_t& target,std::size_t count) noexcept {
        if(count>std::numeric_limits<std::size_t>::max()-target) {
            target=std::numeric_limits<std::size_t>::max();
            overflow=true;
        }else target+=count;
    }
    void collision(std::size_t boxes,std::size_t partitions,
                   std::size_t candidates,std::size_t narrow) noexcept {
        add(collisionQueries,1);
        add(collisionBoxes,boxes);
        add(collisionPartitions,partitions);
        add(collisionCandidates,candidates);
        add(collisionNarrowPhase,narrow);
    }
    void winding(std::size_t boxes,std::size_t partitions,
                 std::size_t triangles,std::size_t rays) noexcept {
        add(windingQueries,1);
        add(windingBoxes,boxes);
        add(windingPartitions,partitions);
        add(windingTriangles,triangles);
        add(windingRays,rays);
    }
    void arrangement(std::size_t pairTests,std::size_t predicates) noexcept {
        add(arrangementPairTests,pairTests);
        add(arrangementPredicates,predicates);
    }
};

struct ExactStageWorkLimits {
    // Every category is independent. No counter may borrow spare work
    // from another; exact equality succeeds and one-under must refuse.
    std::size_t maxCollisionQueries=262144;
    std::size_t maxCollisionNarrowPhase=1048576;
    std::size_t maxWindingQueries=262144;
    std::size_t maxWindingTriangles=4194304;
    std::size_t maxWindingRays=1048576;
    std::size_t maxArrangementPairTests=1048576;
    std::size_t maxArrangementPredicates=4194304;
};

inline std::string refuseExactStageWork(
    const ExactStageWork& value,const ExactStageWorkLimits& limits) {
    if(value.overflow)return "Aggregate source geometry work counter overflow";
    if(value.collisionQueries>limits.maxCollisionQueries)
        return "Aggregate source collision query budget exhausted";
    if(value.collisionNarrowPhase>limits.maxCollisionNarrowPhase)
        return "Aggregate source collision narrow-phase budget exhausted";
    if(value.windingQueries>limits.maxWindingQueries)
        return "Aggregate source winding query budget exhausted";
    if(value.windingTriangles>limits.maxWindingTriangles)
        return "Aggregate source winding triangle budget exhausted";
    if(value.windingRays>limits.maxWindingRays)
        return "Aggregate source winding ray budget exhausted";
    if(value.arrangementPairTests>limits.maxArrangementPairTests)
        return "Aggregate source arrangement pair-test budget exhausted";
    if(value.arrangementPredicates>limits.maxArrangementPredicates)
        return "Aggregate source arrangement predicate budget exhausted";
    return {};
}

// One worker owns an attempt. Thread locality isolates simultaneous
// workers and offline test queries; nesting restores the preceding meter.
// Counters are informational evidence, NEVER Boolean deletion authority.
inline thread_local ExactStageWork* executingExactStageWork=nullptr;
class ScopedExactStageWork {
public:
    explicit ScopedExactStageWork(ExactStageWork& work) noexcept
        :previous_(std::exchange(executingExactStageWork,&work)) {}
    ~ScopedExactStageWork() noexcept {executingExactStageWork=previous_;}
    ScopedExactStageWork(const ScopedExactStageWork&)=delete;
    ScopedExactStageWork& operator=(const ScopedExactStageWork&)=delete;
private:
    ExactStageWork* previous_;
};

// Records actual operations even when an individual local query throws.
// Accounting in the destructor must not throw or convert work exhaustion
// into a false success. The original query's local caps still fail closed.
template<class Function>
class ScopedExactQueryAccounting {
public:
    explicit ScopedExactQueryAccounting(Function fn)
        :fn_(std::move(fn)) {}
    ~ScopedExactQueryAccounting() noexcept {fn_();}
    ScopedExactQueryAccounting(const ScopedExactQueryAccounting&)=delete;
    ScopedExactQueryAccounting& operator=(const ScopedExactQueryAccounting&)=delete;
private:
    Function fn_;
};
template<class F> ScopedExactQueryAccounting(F)->ScopedExactQueryAccounting<F>;

} // namespace mesher
