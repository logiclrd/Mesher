#pragma once

#include "mesher/region_edit.hpp"

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
struct DurableCheckpoint {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<std::pair<VertexId,VertexId>> mirrorPairs;
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
    std::vector<Task> superseded_; // bounded to one between worker wakeups
    CheckpointStatistics statistics_;
    std::jthread worker_;
};
} // namespace mesher
