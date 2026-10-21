#pragma once

#include "mesher/versioned_sculpt.hpp"

#include <stdexcept>

namespace mesher {

// V18-M06: the separately armed *Merge geometry: OFF* avoidance policy.
// This is NOT the V17 original-source Boolean union authorization.
// No checkpoint or ordinary/default startup implicitly enables it.
struct DesktopCollisionSelection {
    bool protectedMerge=false;
    bool avoidCollisions=false;
    bool interactiveInflate=true;
};

// Validated before GLFW/Vulkan initialization and before reading a checkpoint.
// Mode selection is immutable for this desktop worker session. Disk stores
// only the edited mesh/mirror IDs; recovery uses the NEW command line.
inline SculptCollisionSettings desktopCollisionSettings(
    DesktopCollisionSelection choice) {
    if(choice.protectedMerge&&choice.avoidCollisions)
        throw std::invalid_argument(
            "--avoid-collisions conflicts with --merge-geometry: OFF-mode avoidance is not protected Merge");
    if(choice.avoidCollisions&&!choice.interactiveInflate)
        throw std::invalid_argument(
            "--avoid-collisions requires interactive mouse Inflate (no demo or comparisons)");
    SculptCollisionSettings result;
    result.physicalExact=choice.protectedMerge;
    result.offModeConstraints.enabled=choice.avoidCollisions;
    return result;
}

} // namespace mesher
