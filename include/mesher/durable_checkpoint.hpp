#pragma once

#include "mesher/region_edit.hpp"
#include "mesher/revision_retirement.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mesher {
struct OriginalPatchLifetime;
struct DurableCheckpoint {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<std::pair<VertexId,VertexId>> mirrorPairs;
    std::size_t initialVertexCount{}; // reference density for remeshing ratios
    // M29: this root belonged to a protected first-original provenance
    // session. Persistent v13 marks the requirement, NOT an unverifiable
    // homemade lineage certificate. Read-back returns no opaque proof and
    // therefore requires trusted source reauthentication before opt-in M30.
    bool originalFidelityRequired=false;
    // Only an IN-MEMORY trusted snapshot can hold this sealed certificate;
    // a binary checkpoint never synthesizes a trusted source proof.
    std::shared_ptr<const OriginalPatchLifetime> verifiedOriginalFidelity;
};
// Binary V12 checkpoint: live partition/face identities, independent shared
// allocator high-water marks, tombstones and mirrored identities. An explicit
// checksum and version protect against truncation/corruption. Save is a
// full-graph export (off the sculpt thread), never a local editing operation.
void writeCheckpoint(const std::filesystem::path& destination,
    const DurableCheckpoint& snapshot);
DurableCheckpoint readCheckpoint(const std::filesystem::path& destination);

struct CheckpointStatistics {
    std::uint64_t requested{},completed{},failed{},coalesced{};
    std::uintmax_t lastBytes{};
    bool busy{};
    std::string lastError;
};
// One background writer, one pending latest request. Replaced pending roots
// are released on the writer thread; enqueue never waits on disk I/O.
class AsyncCheckpointWriter {
public:
    AsyncCheckpointWriter();
    ~AsyncCheckpointWriter();
    AsyncCheckpointWriter(const AsyncCheckpointWriter&)=delete;
    AsyncCheckpointWriter& operator=(const AsyncCheckpointWriter&)=delete;
    void request(std::filesystem::path destination,DurableCheckpoint snapshot);
    CheckpointStatistics statistics() const;
    // Only for deterministic tests and explicit controlled shutdown.
    void drain();
private:
    struct Task {
        std::filesystem::path path;
        DurableCheckpoint snapshot;
    };
    void run(std::stop_token token);
    mutable std::mutex mutex_;
    std::condition_variable ready_,idle_;
    std::optional<Task> pending_;
    RevisionRetirementQueue discarded_{64};
    CheckpointStatistics statistics_;
    std::jthread worker_;
};
} // namespace mesher
