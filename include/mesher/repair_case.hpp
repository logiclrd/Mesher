#pragma once

#include "mesher/durable_checkpoint.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace mesher {

// R06 developer-only, self-contained replay of the precise POST-STAMP,
// PRE-REPAIR candidate. Neither a brush gesture nor contour coordinates
// reconstruct its surviving topology. Checkpoint bytes preserve stable IDs,
// allocation watermarks, partition owners, and tombstones.
struct RepairCase {
    DurableCheckpoint candidate;
    std::vector<RegionTopologyIndex::CollisionPair> contacts;
    bool symmetricSource=false;
    bool expectedClosure=false;
    std::string originalDiagnostic;
};

// Versioned, checksummed, finite file containing an embedded V12/V13 mesh
// checkpoint and the actual new R01 trusted face-contact pairs. This is not
// a live source-fidelity certificate or permission to publish edited roots.
void writeRepairCase(const std::filesystem::path& destination,
                     const RepairCase& capture);
RepairCase readRepairCase(const std::filesystem::path& source);

// Never silently overwrite a previous physical failure.
std::filesystem::path uniqueRepairCasePath(
    const std::filesystem::path& directory="repair-cases");

} // namespace mesher
