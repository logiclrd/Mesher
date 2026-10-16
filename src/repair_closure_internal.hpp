#pragma once
#include "mesher/repair_plan.hpp"

namespace mesher {
// Expects fully validated, closed directed contours from V15. Performs
// read-only topology-option scoring and bounded global closure selection.
RepairPlan selectRepairClosures(const RegionTopologyIndex& index,
                               RepairPlan plan,
                               const RepairPlanSettings& settings);
} // namespace mesher
