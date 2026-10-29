#pragma once

#include "mesher/durable_checkpoint.hpp"
#include <cstddef>
#include <filesystem>

namespace mesher {
// Private, validated inputs for UX06. Construction never changes the active
// worker, viewport, renderer, history, checkpoint path, or brush settings.
// The UI may publish a replacement only after completing its own rebind.
enum class MeshOpenFormat {Checkpoint,WavefrontObj};
// Caller-selected interpretation is authoritative; only AutoDetect
// examines contents, never the extension (or an alleged file name).
enum class MeshOpenFilter {MesherCheckpoint,WavefrontObj,AllFiles};
struct MeshOpenCandidate {
    DurableCheckpoint snapshot;
    MeshOpenFormat format{};
    std::size_t vertexCount{},faceCount{};
    bool hasVertexColors{};
};
MeshOpenCandidate readMeshOpenCandidate(
    const std::filesystem::path& path,
    MeshOpenFilter selectedFilter=MeshOpenFilter::AllFiles);
} // namespace mesher
