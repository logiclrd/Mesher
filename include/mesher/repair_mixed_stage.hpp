#pragma once

#include "mesher/repair_auto_plan.hpp"
#include "mesher/repair_mixed_primitives.hpp"
#include "mesher/repair_mixed_source_view.hpp"
#include "mesher/repair_mixed_positions.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace mesher {

// R06-M04A stages BOTH closure kinds before any private COW modification.
// No intermediate mesh is published, committed or assumed closed. Actual
// quality-guided splitting and one final COW transaction belong to M04B.
enum class MixedClosureFaceKind {Survivor,Cap,Zipper};
struct MixedClosureFace {
    FaceId stableId{}; // zero -> new, assigned only in later M04B
    PartitionId owner{};
    PrivateRepairTriangle vertices{};
    MixedClosureFaceKind kind{MixedClosureFaceKind::Survivor};
    double targetEdge{};
    // For original R03 survivors this is a FaceId handle, NOT a duplicate
    // editable triangle. Unbound records are transaction-new faces, or
    // standalone legacy helper fixtures that retain local vertices.
    std::shared_ptr<const MixedClosureSourceView> sourceView{};

    PrivateRepairTriangle triangle() const {
        if(stableId&&sourceView)
            return sourceView->face(stableId).vertices;
        return vertices;
    }
    void setTriangle(PrivateRepairTriangle next) {
        if(stableId&&sourceView)
            throw std::logic_error("M15C3 original survivor faces are editable only by the staging editor");
        vertices=next;
    }
};
// An opt-in snapshot/observer failure is not evidence of geometry refusal.
// Keep it outside logic_error, which the planar cap generator catches for
// intentional spatial fallback.
class RepairStepObserverFailure final : public std::runtime_error {
public:
    explicit RepairStepObserverFailure(const std::string& reason)
        :std::runtime_error(reason) {}
};
// M15B: mandatory and optional split requests share the same stage-owned
// finite/quality/mirror preflight. All limits concern the complete proposed
// transaction, not just the currently tracked contour halo.
struct MixedClosureEdgeNeighborhood;
// Exact ID ancestry from an ACTUAL staging-owned split, never inferred
// from a geometric proximity match. Used by opt-in M15D replay diagnosis.
struct MixedClosureSplitEvent {
    VertexId midpoint{},originalA{},originalB{};
    std::size_t originalR03Sides{};
    std::size_t incidentSurvivorSides{};
    std::size_t incidentCapSides{};
    std::size_t incidentZipperSides{};
    // M17B: canonical t from smaller to larger ORIGINAL endpoint ID.
    // The position is exact private 3D geometry, never an inferred weld.
    double canonicalT=0.5;
    Vec3 position{};
};
struct MixedClosureSplitSettings {
    bool requireReflectedGeometry=false;
    double minSurvivorQuality=.012;
    double minCapQuality=.10;
    double minZipperQuality=.10;
    std::size_t maxInsertedVertices=1024;
    std::size_t maxRefinementSteps=512;
    std::size_t maxTrackedFaces=1000000;
    double minimumSplitFraction=0.12; // bounded off-center child intervals
};

class MixedClosureStaging {
public:
    // Opt-in diagnostic observer, invoked synchronously after a complete
    // logical mutation. It may copy immutable geometry and block the worker;
    // it is never called while a stage mutex is held. Default: no observer.
    using OperationObserver=std::function<void(
        const MixedClosureStaging&,const std::string&,const std::string&)>;
    void setOperationObserver(OperationObserver cb) {observer_=std::move(cb);}
    // M17C: record an ATTEMPT which changed no geometry. This is an
    // immutable diagnostic snapshot, not a mutation or a fake success.
    // Synchronous step-gate semantics are the same as for real edits.
    void reportRefinementAttempt(const std::string& operation,
                                 const std::string& arguments) const {
        notifyOperation(operation,arguments);
    }
    // M15A foundation: bind one EXACT immutable R03 open mesh and its
    // directed hole rims. Read-only boundary queries reflect subsequently
    // inserted faces and source-face overrides; no stale yellow path.
    MixedClosureStaging()=default; // staged fixture compatibility until M15C
    MixedClosureStaging(
        std::shared_ptr<const RegionTopologyIndex> source,
        const std::vector<RepairContour>& contours);
    std::size_t trackSurvivor(FaceId id,std::size_t maxFaces);
    // Allocate an identity only inside the private COW revision. This is
    // intentionally NOT idempotent: coordinate coincidence is not welding.
    VertexId addVertex(MutableMeshView& privateView,Vec3 position,
                       PartitionId owner,std::size_t maxGeneratedVertices=1024);
    // Oriented face-existence request. Cyclic rotations are identical;
    // reverse orientation conflicts; a repeat returns the same ordinal.
    // A generated face's permanent FaceId is assigned at consolidation.
    std::size_t ensureFace(
        VertexId a,VertexId b,VertexId c,MixedClosureFaceKind kind,
        PartitionId owner,double targetEdge=0,
        std::size_t maxTrackedFaces=1000000,
        std::size_t maxGeneratedFaces=4096);
    // Split the CURRENT indexed edge once. An absent AB is a hard failure
    // even when its old children AM and MB still exist. One-sided live
    // R03 boundaries are legal; two-sided survivor/patch edges update both
    // triangles. Mirror companions are preflighted as one logical edit.
    // Returns the one or two newly reserved stable midpoint VertexIds.
    // Optional quality refinement may return empty when it is unsafe;
    // an ABSENT edge always throws even for an optional request.
    // canonicalT is measured from MIN(VertexId) to MAX(VertexId), even
    // if the caller supplies reversed endpoints. For reflected edges,
    // preflight maps t to 1-t where stable-ID orientations are reversed.
    // Ordinary callers omitting it retain the exact midpoint path.
    std::vector<VertexId> splitEdge(
        MutableMeshView& privateView,VertexId a,VertexId b,
        const MixedClosureSplitSettings& settings,
        std::size_t alreadyInserted=0,
        std::size_t alreadyRefined=0,
        bool optionalQualityOnly=false,
        double canonicalT=0.5);
    // Other topology-changing quality operations also remain within
    // the staging editor; no closure-algorithm caller mutates its face
    // records directly when flipping a diagonal or collapsing a cap fan.
    std::size_t improveInteriorDiagonal(
        bool requireReflection,double minCapQuality,double minZipperQuality,
        double maxCapEdgeFactor,double maxZipperEdgeFactor);
    std::vector<VertexId> collapseInteriorCapFan(
        const std::vector<VertexId>& createdVertices,bool requireReflection,
        double minCapQuality,double maxCapEdgeFactor);
    std::size_t fairInteriorVertex(
        const std::vector<VertexId>& createdVertices,bool requireReflection,
        double minCapQuality,double minZipperQuality,
        double maxCapEdgeFactor,double maxZipperEdgeFactor);
    bool hasEffectiveEdge(VertexId a,VertexId b) const;
    // Current directed, closed walks of one-sided effective edges.
    // Incomplete/forked boundary topology fails instead of rendering a
    // stale R03 loop. Empty when completely capped/zippered.
    std::vector<std::vector<VertexId>> currentBoundaryLoops() const;
    // Read-only topology. Mutations are only available through the
    // staging editor; callers cannot obtain mutable face arrays,
    // working positions, or the original-source override object.
    const std::vector<MixedClosureFace>& faces() const noexcept {return faces_;}
    const MixedClosurePositions& positions() const noexcept {return positions_;}
    std::shared_ptr<const MixedClosureSourceView> sourceView() const noexcept {return sourceView_;}
    const std::map<VertexId,PartitionId>& vertexOwners() const noexcept {return vertexOwners_;}
    const std::vector<FaceId>& survivingFaceIds() const noexcept {return survivingFaceIds_;}
    const std::vector<VertexId>& provisionalSpatialVertices() const noexcept {return provisionalSpatialVertices_;}
    const std::vector<MixedClosureSplitEvent>& splitEvents() const noexcept {return splitEvents_;}
    // M20A diagnostic metadata only: local-cap FACE identities and a
    // proposed second rim, never additions/deletions to staged topology.
    void recordCapAnomalyPreview(std::vector<PrivateRepairTriangle> faces,
                                 std::vector<std::vector<VertexId>> contours,
                                 std::string status) {
        suspectCapFaces_=std::move(faces);
        secondaryCapContours_=std::move(contours);
        capAnomalyStatus_=std::move(status);
    }
    const std::vector<PrivateRepairTriangle>& suspectCapFaces() const noexcept {
        return suspectCapFaces_;
    }
    const std::vector<std::vector<VertexId>>& secondaryCapContours() const noexcept {
        return secondaryCapContours_;
    }
    const std::string& capAnomalyStatus() const noexcept {return capAnomalyStatus_;}
    PartitionId vertexOwner(VertexId id) const {return vertexOwners_.at(id);}
    void trackOriginalVertex(VertexId id);
    void stageProvisionalVertex(VertexId id,Vec3 pos,PartitionId owner,
                                std::size_t maxProvisional);
    std::vector<VertexId> resolveProvisionalVertices(
        MutableMeshView& privateView,std::size_t maxNew);
    void retirePrivateVertex(MutableMeshView& privateView,VertexId id);
    MixedClosureEdgeNeighborhood inspectEdge(VertexId a,VertexId b) const;
    // For tests that need a deliberately incomplete candidate, manipulate
    // an exported patch ledger, NEVER the staging object's live topology.
private:
    // The source and its transactional overrides are authoritative for
    // every original survivor face and vertex. New triangles remain local.
    std::shared_ptr<MixedClosureSourceView> sourceView_;
    std::vector<FaceId> survivingFaceIds_;
    std::vector<MixedClosureFace> faces_;
    // Initially immutable source contour walks (not the mutable displayed
    // paths); currentBoundaryLoops derives new walks from edge incidence.
    std::vector<std::vector<VertexId>> initialRimPaths_;
    // Source-backed survivor handles plus owned new vertex positions.
    MixedClosurePositions positions_;
    std::map<VertexId,PartitionId> vertexOwners_;
public:
    // Descriptive metrics; these do not grant access to working topology.
    std::size_t capLoops{};
    std::size_t zipperPairs{};
    std::size_t capTriangles{};
    std::size_t zipperTriangles{};
    std::size_t boundarySidesCovered{};
    // Temporary IDs are NEVER published. M04B replaces them with actual
    // monotonically allocated immutable IDs before its conforming audit.
private:
    std::vector<VertexId> provisionalSpatialVertices_;
    std::vector<MixedClosureSplitEvent> splitEvents_;
    std::vector<PrivateRepairTriangle> suspectCapFaces_;
    std::vector<std::vector<VertexId>> secondaryCapContours_;
    std::string capAnomalyStatus_;
    OperationObserver observer_;
    bool insideAtomicSplit_=false;
    void notifyOperation(const std::string& name,const std::string& args) const;
public:
    bool usedSpatialCapFallback{};
    bool usedMirroredSeamZipper{};
    std::size_t mirroredSeamZipperPairs{};
    std::size_t mirroredSeamZipperVertices{};
    std::size_t spatialCapLoops{};
    std::size_t mirroredRibbonLoops{};
    std::size_t spatialPoissonIterations{};
    double minimumSpatialInitialQuality{1};
};

struct MixedClosureStageSettings {
    bool requireReflectedGeometry=false;
    bool optimizeSpatialFront=false; // M17D opt-in, default preserves old R06
    bool optimizeCurvatureFront=false; // M18D independent opt-in
    bool guideCurvaturePoisson=false; // M18E opt-in RHS constraints
    bool probeSeamFeasibility=false; // M19A diagnostic only
    bool stageFeasibilityPreview=false; // M19A interactive step-only candidate
    bool probeDiagonalFeasibility=false; // M19C diagnostic only
    bool stageDiagonalPreview=false; // M19D private step-debug topology only
    bool diagnoseRecursiveCap=false; // M20A read-only anomaly and would-be hole
    double targetInitialCapQuality=0.10;
    std::size_t maxLoops=16;
    std::size_t maxNewTriangles=4096;
    std::size_t maxTrackedFaces=1000000;
    ForcedZipperSettings zipper{};
    // Diagnostic only; the default execution does not copy or publish steps.
    MixedClosureStaging::OperationObserver operationObserver{};
};

// One immutable R03 root and exact global M03 contour decisions. Rejects
// duplicated, missing, misoriented or cross-stitched rims before returning a
// deterministic list of pre-refinement triangles and retained face records.
MixedClosureStaging stagePrivateMixedClosure(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const MixedClosureStageSettings& bounds={});

} // namespace mesher
