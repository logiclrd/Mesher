#pragma once

#include "mesher/mesh.hpp"
#include "mesher/region_edit.hpp"
#include "mesher/mesh_export_ply.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mesher {

// An accepted, immutable RegionTopologyIndex is retained by shared ownership.
// The absolute path is captured at dialog acceptance, before any CWD change.
// Export never reads a later worker/viewport root or modifies history.
struct MeshExportRequest {
    std::filesystem::path absoluteDestination;
    std::shared_ptr<const RegionTopologyIndex> acceptedIndex;
};

MeshExportRequest captureMeshExportRequest(
    const std::filesystem::path& destination,
    std::shared_ptr<const RegionTopologyIndex> acceptedIndex);

// Stable-ID-ordered flat snapshot, including original source-alpha metadata.
// Does not assume any original import ordering (sculpting adds/removes IDs).
Mesh materializeMeshExport(const MeshExportRequest& request);

using MeshExportEncoder=std::function<void(std::ostream&,const Mesh&)>;

// Produces a uniquely named adjacent temporary file with exclusive creation,
// validates completion and flush/sync before atomic rename over destination.
// Any failure before rename preserves the old destination; the function never
// unlinks an existing destination to make a rename succeed. Caller must supply
// one explicit format/colour policy; no suffix sniffing or implicit losses.
void writeMeshExportAtomic(const MeshExportRequest& request,
                           const MeshExportEncoder& encode);

// The already-merged UX07B PLY encoder is the first concrete integration.
// UX07A/C can use writeMeshExportAtomic with their writers when merged.
void writePlyExportAtomic(const MeshExportRequest& request,
                          PlyExportOptions options={});

struct MeshExportStatistics {
    std::uint64_t requested{},completed{},failed{};
    std::size_t pending{};
    bool busy{};
    std::string lastError;
};

// Per-request result ordered by the writer FIFO, consumed by the UI thread.
// Aggregate counters cannot identify which path/format succeeded if several
// exports finish between display frames.
struct MeshExportCompletion {
    std::filesystem::path destination;
    bool success{};
    std::string error;
};
// A FIFO of explicit exports; unlike autosave/checkpoint requests, accepted
// export destinations must NOT be coalesced or silently dropped. Work occurs
// outside UI/brush threads. Exceptions are recorded, not thrown from worker.
class AsyncMeshExportWriter {
public:
    AsyncMeshExportWriter();
    ~AsyncMeshExportWriter();
    AsyncMeshExportWriter(const AsyncMeshExportWriter&)=delete;
    AsyncMeshExportWriter& operator=(const AsyncMeshExportWriter&)=delete;
    void request(MeshExportRequest snapshot,MeshExportEncoder encoder);
    MeshExportStatistics statistics() const;
    std::vector<MeshExportCompletion> takeCompletions();
    void drain();
private:
    struct Task {
        MeshExportRequest snapshot;
        MeshExportEncoder encoder;
    };
    void run() noexcept;
    mutable std::mutex mutex_;
    std::condition_variable ready_,idle_;
    std::deque<Task> pending_;
    bool stopping_=false,busy_=false;
    MeshExportStatistics statistics_;
    std::deque<MeshExportCompletion> completions_;
    std::thread worker_;
};
} // namespace mesher
