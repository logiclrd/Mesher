#pragma once

#include "mesher/durable_checkpoint.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mesher {

// R06 developer-only, self-contained replay of the precise POST-STAMP,
// PRE-REPAIR candidate. Neither a brush gesture nor contour coordinates
// reconstruct its surviving topology. Checkpoint bytes preserve stable IDs,
// allocation watermarks, partition owners, and tombstones.
// An explicit human correction to M03's closure classification, independent
// of whether the original geometry build succeeded. Never infer this from a
// screenshot, contact-pair count or a recorded success bit.
enum class RepairCaseIntent : std::uint8_t {
    Unspecified=0,
    PreferCaps=1,
    PreferZipper=2
};
const char* repairCaseIntentName(RepairCaseIntent intent);

struct RepairCase {
    DurableCheckpoint candidate;
    std::vector<RegionTopologyIndex::CollisionPair> contacts;
    bool symmetricSource=false;
    bool expectedClosure=false;
    RepairCaseIntent intendedSelection=RepairCaseIntent::Unspecified;
    std::string originalDiagnostic;
};

// Versioned, checksummed, finite file containing an embedded V12/V13 mesh
// checkpoint and the actual new R01 trusted face-contact pairs. This is not
// a live source-fidelity certificate or permission to publish edited roots.
// Writes v2, reads both v1 (existing physical captures) and v2.
void writeRepairCase(const std::filesystem::path& destination,
                     const RepairCase& capture);
RepairCase readRepairCase(const std::filesystem::path& source);

// Never silently overwrite a previous physical failure.
std::filesystem::path uniqueRepairCasePath(
    const std::filesystem::path& directory="repair-cases",
    const std::string& stem="refusal");

} // namespace mesher
