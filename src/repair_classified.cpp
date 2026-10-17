#include "mesher/repair_surgery.hpp"

#include <stdexcept>
#include <utility>

namespace mesher {

ClassifiedExcisionRepair applyClassifiedExcisionRepair(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const std::vector<VertexId>& changedVertices,
    const std::vector<FaceId>& changedFaces,
    const std::vector<FaceId>& componentSeeds,
    ClassifiedFeaturePolicy policy,
    const RepairSurgerySettings& settings) {
    if(!baseline)
        throw std::invalid_argument("Classified repair requires an immutable candidate");
    if(settings.requireReverseSurfaceFidelity||
       settings.requireExposedFragmentFidelity)
        throw std::invalid_argument(
            "Classified repair selects its reverse fidelity ONLY through explicit policy");
    if(policy!=ClassifiedFeaturePolicy::TopologyOnly&&
       policy!=ClassifiedFeaturePolicy::PreserveSampledExposedFeatures)
        throw std::invalid_argument("Unknown classified feature policy");

    ClassifiedExcisionRepair output;
    // Fresh V13 collision and V14 winding evidence from exactly the
    // revision that may be repaired. A stale caller-supplied low-level
    // contour plan is never permission to delete feature-bearing faces.
    output.plan=planApproximateExcision(
        *baseline,changedVertices,changedFaces,componentSeeds,
        settings.planning);
    if(!output.plan.ready())return output; // NoChanges, ambiguity, or budget
    if(output.plan.removedFaces.empty())
        throw std::logic_error("Ready classified excision has no removed faces");

    RepairSurgerySettings checked=settings;
    checked.requireExposedFragmentFidelity=
        policy==ClassifiedFeaturePolicy::PreserveSampledExposedFeatures;
    // The committed result is STILL PRIVATE. A caller with the original
    // repository snapshot must independently publishIfCurrent().
    output.surgery=applyRepairSurgery(
        std::move(baseline),output.plan,checked);
    return output;
}
} // namespace mesher
