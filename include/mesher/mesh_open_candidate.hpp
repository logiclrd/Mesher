#pragma once

#include "mesher/durable_checkpoint.hpp"
#include <cstddef>
#include <filesystem>

namespace mesher {
// Private, validated inputs for UX06. Construction never changes the active
// worker, viewport, renderer, history, checkpoint path, or brush settings.
// The UI may publish a replacement only after completing its own rebind.
enum class MeshOpenFormat {Checkpoint,WavefrontObj};
struct MeshOpenCandidate {
    DurableCheckpoint snapshot;
    MeshOpenFormat format{};
    std::size_t vertexCount{},faceCount{};
    bool hasVertexColors{};
};
MeshOpenCandidate readMeshOpenCandidate(const std::filesystem::path& path);
} // namespace mesher
