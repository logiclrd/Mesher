#pragma once
#include "mesher/partition_render.hpp"
#include "mesher/repair_preview.hpp"
#include "mesher/repair_excision.hpp"
#include "mesher/repair_zipper.hpp"
#include "mesher/repair_cap.hpp"
#include "mesher/repair_auto_private.hpp"
#include "mesher/repair_seam_audit.hpp"
#include "mesher/repair_seam_provenance.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mesher {

struct ProposedSculptStep;

// A read-only preview of a candidate that was NEVER published. The red
// topology is incident to intersecting FACES; original vertices are not
// necessarily located on the geometric triangle/triangle intersection.
struct RepairInspectionSnapshot {
    std::shared_ptr<const RegionTopologyIndex> candidate;
    std::vector<RegionTopologyIndex::CollisionPair> contacts;
    std::vector<FaceId> affectedFaces;
    std::vector<VertexId> incidentVertices;
    std::vector<RegionTopologyIndex::Edge> incidentEdges;
    RegionTopologyIndex::CollisionStatistics work{};
    RegionTopologyIndex::CollisionStatistics baselineWork{};
    std::size_t preexistingPairs=0; // not automatically a new-crossing freeze
    std::string diagnostic; // nonempty on bounded-query/incomplete evidence
    bool previewEnabled=false;
    bool excisionEnabled=false;
    RepairExcisionPreview excision;
    std::optional<PrivateExcisionResult> privateCut;
    std::string excisionDiagnostic;
    bool zipperEnabled=false;
    std::optional<ForcedZipperResult> privateZipper;
    std::string zipperDiagnostic;
    bool capEnabled=false;
    std::optional<ForcedCapResult> privateCap;
    std::string capDiagnostic;
    bool autoEnabled=false;
    std::optional<AutoClosurePlan> autoPlan;
    // M09 compare BOTH read-only M03 plans on the same candidate/R03 cut.
    // No alternative M04 geometry is built. Labels are only plans, not
    // correctness or evidence that either plan can be constructed.
    std::array<std::string,2> classifierChoices{{"UNAVAILABLE","UNAVAILABLE"}};
    bool classifierComparable=false;
    bool classifiersAgree=false;
    AutoClosureClassifier activeClassifier=AutoClosureClassifier::Area;
    std::optional<CertifiedPrivateAutoResult> privateAuto;
    std::string autoDiagnostic;
    bool warnQualityEnabled=false;
    std::optional<MixedPrivateClosureResult> warningPreview;
    std::string warningPreviewDiagnostic;
    bool exactOverlay=false;
    std::optional<RepairSeamAudit> seamAudit;
    std::optional<RepairSeamProvenanceReport> seamProvenance;
    std::string seamAuditDiagnostic;
};

// Thread-safe one-shot observer, called by the existing PRE-CAS sculpt
// validator. Returns a refusal only on a freeze; no worker root is published.
class RepairInspectionSession {
public:
    explicit RepairInspectionSession(bool preview=false,bool excise=false,
                                     bool zipper=false,bool cap=false,bool autoMode=false,
                                     bool symmetricSource=false,
                                     AutoClosureClassifier classifier=AutoClosureClassifier::Area,
                                     bool warnQuality=false,
                                     bool auditSeam=false,bool exactOverlay=false,
                                     bool scaleAware=false,
                                     bool optimizeFront=false,
                                     bool optimizeCurvatureFront=false,
                                     bool guideCurvaturePoisson=false,
                                     bool probeSeamFeasibility=false,
                                     bool probeDiagonalFeasibility=false)
        :previewEnabled_(preview||excise||zipper||cap||autoMode),
         excisionEnabled_(excise||zipper||cap||autoMode),
         zipperEnabled_(zipper),capEnabled_(cap),autoEnabled_(autoMode),
         symmetricSource_(symmetricSource),classifier_(classifier),
         warnQuality_(warnQuality),auditSeam_(auditSeam),
         exactOverlay_(exactOverlay),scaleAware_(scaleAware),
         optimizeFront_(optimizeFront),
         optimizeCurvatureFront_(optimizeCurvatureFront),
         guideCurvaturePoisson_(guideCurvaturePoisson),
         probeSeamFeasibility_(probeSeamFeasibility),
         probeDiagonalFeasibility_(probeDiagonalFeasibility) {}
    std::string inspect(const ProposedSculptStep& proposed);
    std::shared_ptr<const RepairInspectionSnapshot> frozenSnapshot() const;
    bool frozen() const noexcept {return frozen_.load(std::memory_order_acquire);}
private:
    bool previewEnabled_=false;
    bool excisionEnabled_=false;
    bool zipperEnabled_=false;
    bool capEnabled_=false;
    bool autoEnabled_=false;
    bool symmetricSource_=false;
    AutoClosureClassifier classifier_=AutoClosureClassifier::Area;
    bool warnQuality_=false;
    bool auditSeam_=false,exactOverlay_=false;
    bool scaleAware_=false,optimizeFront_=false,optimizeCurvatureFront_=false,
         guideCurvaturePoisson_=false,probeSeamFeasibility_=false,
         probeDiagonalFeasibility_=false;
    std::atomic<bool> frozen_{false};
    mutable std::mutex mutex_;
    std::shared_ptr<const RepairInspectionSnapshot> snapshot_;
};

class RepairStepTrace;
struct RepairCase;
// Replay bypasses the sculpt worker entirely and executes the SAME R02,
// R03 and R06 private closure on the saved, exact POST-STAMP candidate.
// This is a new private snapshot, never a published revision/history root.
std::shared_ptr<const RepairInspectionSnapshot> replayPrivateAutomaticRepair(
    const RepairCase& capture,
    AutoClosureClassifier classifier=AutoClosureClassifier::Area,
    bool warnQuality=false,bool auditSeam=false,
    bool exactOverlay=false,
    std::shared_ptr<RepairStepTrace> stepTrace={},
    bool scaleAware=false,bool optimizeFront=false,
    bool optimizeCurvatureFront=false,bool guideCurvaturePoisson=false,
    bool probeSeamFeasibility=false,
    bool probeDiagonalFeasibility=false,
    bool stageFeasibilityPreview=false,
    bool stageDiagonalPreview=false);

// CPU-generated unlit colored triangular prisms/octahedra. A viewport may
// upload this once, and orbit inside/outside the frozen candidate freely.
std::vector<PartitionRenderVertex> buildRepairInspectionOverlay(
    const RepairInspectionSnapshot& snapshot);

} // namespace mesher
