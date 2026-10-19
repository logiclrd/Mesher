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
    // M43: counted original contour/closure work across repeated
    // private selection, not retained final topology size.
    std::size_t contourEdgeProbes{},contourBoundaryEdges{};
    std::size_t contourLoopVisits{},closureRingTests{};
    std::size_t closurePairCandidates{},closureSampleTests{};
    std::size_t closureDpTransitions{};
    // M44: actual private certified seam trace/refinement and join work.
    std::size_t seamTraceProbes{},seamMatchedIntervals{};
    std::size_t seamWeldComparisons{},seamRefinePredicates{};
    std::size_t seamRefineMatches{},seamMirrorOrbitChecks{};
    std::size_t seamFaceRewires{};
    // M45: real first-root proof work, across every repeated private audit.
    // Immutable-source checks, live child inspections and actual seven-
    // witness/owned-triangle comparisons must never be inferred from the
    // final authenticated descendant count alone.
    std::size_t originalAnchorProofs{},originalDescendantProofs{};
    std::size_t originalRetirementProofs{},originalWitnessTriangles{};
    std::size_t originalIntentChecks{},originalProtectedFeatures{};
    std::size_t originalMirrorOrbitChecks{};
    std::size_t originalHistoryRoots{},originalHistoryTransitions{};
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
    std::size_t maxCollisionBoxes=4194304;
    std::size_t maxCollisionPartitions=1048576;
    std::size_t maxCollisionCandidates=2097152;
    std::size_t maxCollisionNarrowPhase=1048576;
    std::size_t maxWindingQueries=262144;
    std::size_t maxWindingBoxes=4194304;
    std::size_t maxWindingPartitions=1048576;
    std::size_t maxWindingTriangles=4194304;
    std::size_t maxWindingRays=1048576;
    std::size_t maxArrangementPairTests=1048576;
    std::size_t maxArrangementPredicates=4194304;
    std::size_t maxContourEdgeProbes=4194304;
    std::size_t maxContourBoundaryEdges=1048576;
    std::size_t maxContourLoopVisits=1048576;
    std::size_t maxClosureRingTests=65536;
    std::size_t maxClosurePairCandidates=1048576;
    std::size_t maxClosureSampleTests=16777216;
    std::size_t maxClosureDpTransitions=8388608;
    std::size_t maxSeamTraceProbes=4194304;
    std::size_t maxSeamMatchedIntervals=1048576;
    std::size_t maxSeamWeldComparisons=4194304;
    std::size_t maxSeamRefinePredicates=2097152;
    std::size_t maxSeamRefineMatches=1048576;
    std::size_t maxSeamMirrorOrbitChecks=4194304;
    std::size_t maxSeamFaceRewires=2097152;
    std::size_t maxOriginalAnchorProofs=1048576;
    std::size_t maxOriginalDescendantProofs=4194304;
    std::size_t maxOriginalRetirementProofs=1048576;
    std::size_t maxOriginalWitnessTriangles=16777216;
    std::size_t maxOriginalIntentChecks=1048576;
    std::size_t maxOriginalProtectedFeatures=1048576;
    std::size_t maxOriginalMirrorOrbitChecks=4194304;
    std::size_t maxOriginalHistoryRoots=65536;
    std::size_t maxOriginalHistoryTransitions=65536;
};

inline std::string refuseExactStageWork(
    const ExactStageWork& value,const ExactStageWorkLimits& limits) {
    if(value.overflow)return "Aggregate source geometry work counter overflow";
    if(value.collisionQueries>limits.maxCollisionQueries)
        return "Aggregate source collision query budget exhausted";
    if(value.collisionBoxes>limits.maxCollisionBoxes)
        return "Aggregate source collision box budget exhausted";
    if(value.collisionPartitions>limits.maxCollisionPartitions)
        return "Aggregate source collision partition budget exhausted";
    if(value.collisionCandidates>limits.maxCollisionCandidates)
        return "Aggregate source collision candidate budget exhausted";
    if(value.collisionNarrowPhase>limits.maxCollisionNarrowPhase)
        return "Aggregate source collision narrow-phase budget exhausted";
    if(value.windingQueries>limits.maxWindingQueries)
        return "Aggregate source winding query budget exhausted";
    if(value.windingBoxes>limits.maxWindingBoxes)
        return "Aggregate source winding box budget exhausted";
    if(value.windingPartitions>limits.maxWindingPartitions)
        return "Aggregate source winding partition budget exhausted";
    if(value.windingTriangles>limits.maxWindingTriangles)
        return "Aggregate source winding triangle budget exhausted";
    if(value.windingRays>limits.maxWindingRays)
        return "Aggregate source winding ray budget exhausted";
    if(value.arrangementPairTests>limits.maxArrangementPairTests)
        return "Aggregate source arrangement pair-test budget exhausted";
    if(value.arrangementPredicates>limits.maxArrangementPredicates)
        return "Aggregate source arrangement predicate budget exhausted";
    if(value.contourEdgeProbes>limits.maxContourEdgeProbes)
        return "Aggregate source contour edge-probe budget exhausted";
    if(value.contourBoundaryEdges>limits.maxContourBoundaryEdges)
        return "Aggregate source contour boundary-edge budget exhausted";
    if(value.contourLoopVisits>limits.maxContourLoopVisits)
        return "Aggregate source contour loop-visit budget exhausted";
    if(value.closureRingTests>limits.maxClosureRingTests)
        return "Aggregate source closure ring-test budget exhausted";
    if(value.closurePairCandidates>limits.maxClosurePairCandidates)
        return "Aggregate source closure pair-candidate budget exhausted";
    if(value.closureSampleTests>limits.maxClosureSampleTests)
        return "Aggregate source closure sample-test budget exhausted";
    if(value.closureDpTransitions>limits.maxClosureDpTransitions)
        return "Aggregate source closure DP-transition budget exhausted";
    if(value.seamTraceProbes>limits.maxSeamTraceProbes)
        return "Aggregate source seam trace-probe budget exhausted";
    if(value.seamMatchedIntervals>limits.maxSeamMatchedIntervals)
        return "Aggregate source seam matched-interval budget exhausted";
    if(value.seamWeldComparisons>limits.maxSeamWeldComparisons)
        return "Aggregate source seam weld-comparison budget exhausted";
    if(value.seamRefinePredicates>limits.maxSeamRefinePredicates)
        return "Aggregate source seam refinement predicate budget exhausted";
    if(value.seamRefineMatches>limits.maxSeamRefineMatches)
        return "Aggregate source seam refinement match budget exhausted";
    if(value.seamMirrorOrbitChecks>limits.maxSeamMirrorOrbitChecks)
        return "Aggregate source seam mirror-orbit budget exhausted";
    if(value.seamFaceRewires>limits.maxSeamFaceRewires)
        return "Aggregate source seam face-rewire budget exhausted";
    if(value.originalAnchorProofs>limits.maxOriginalAnchorProofs)
        return "Aggregate first-original anchor-proof budget exhausted";
    if(value.originalDescendantProofs>limits.maxOriginalDescendantProofs)
        return "Aggregate first-original descendant-proof budget exhausted";
    if(value.originalRetirementProofs>limits.maxOriginalRetirementProofs)
        return "Aggregate first-original retirement-proof budget exhausted";
    if(value.originalWitnessTriangles>limits.maxOriginalWitnessTriangles)
        return "Aggregate first-original witness-triangle budget exhausted";
    if(value.originalIntentChecks>limits.maxOriginalIntentChecks)
        return "Aggregate first-original intent-check budget exhausted";
    if(value.originalProtectedFeatures>limits.maxOriginalProtectedFeatures)
        return "Aggregate first-original protected-feature budget exhausted";
    if(value.originalMirrorOrbitChecks>limits.maxOriginalMirrorOrbitChecks)
        return "Aggregate first-original mirror-orbit budget exhausted";
    if(value.originalHistoryRoots>limits.maxOriginalHistoryRoots)
        return "Aggregate first-original history-root budget exhausted";
    if(value.originalHistoryTransitions>limits.maxOriginalHistoryTransitions)
        return "Aggregate first-original history-transition budget exhausted";
    return {};
}

// One worker owns an attempt. Thread locality isolates simultaneous
// workers and offline test queries; nesting restores the preceding meter.
// Counters are informational evidence, NEVER Boolean deletion authority.
inline thread_local ExactStageWork* executingExactStageWork=nullptr;
// Only the true M27 first-original witness-to-owned-living-child audit
// charges this category. Generic legacy, V16 and unrelated sampled
// surface distance queries are not mislabelled as source proof.
inline thread_local bool measuringFirstOriginalWitnesses=false;
class ScopedFirstOriginalWitnessMeasurement {
public:
    ScopedFirstOriginalWitnessMeasurement() noexcept
        :previous_(std::exchange(measuringFirstOriginalWitnesses,true)) {}
    ~ScopedFirstOriginalWitnessMeasurement() noexcept {
        measuringFirstOriginalWitnesses=previous_;
    }
    ScopedFirstOriginalWitnessMeasurement(
        const ScopedFirstOriginalWitnessMeasurement&)=delete;
    ScopedFirstOriginalWitnessMeasurement& operator=(
        const ScopedFirstOriginalWitnessMeasurement&)=delete;
private:
    bool previous_;
};
class ScopedExactStageWork {
public:
    explicit ScopedExactStageWork(ExactStageWork& work) noexcept
        :previous_(std::exchange(executingExactStageWork,&work)) {}
    ~ScopedExactStageWork() noexcept {stop();}
    // End the actual candidate's observation at its pre-CAS admission.
    // Post-publication callbacks and history work cannot silently add
    // to a previously approved finite source-query budget.
    void stop() noexcept {
        if(active_) {
            executingExactStageWork=previous_;
            active_=false;
        }
    }
    ScopedExactStageWork(const ScopedExactStageWork&)=delete;
    ScopedExactStageWork& operator=(const ScopedExactStageWork&)=delete;
private:
    ExactStageWork* previous_;
    bool active_=true;
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
