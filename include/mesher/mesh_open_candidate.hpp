#pragma once

#include "mesher/durable_checkpoint.hpp"
#include "mesher/mesh_stl.hpp"
#include "mesher/mesh_export_formats.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>

namespace mesher {
// Private, validated inputs for UX06. Construction never changes the active
// worker, viewport, renderer, history, checkpoint path, or brush settings.
// The UI may publish a replacement only after completing its own rebind.
enum class MeshOpenFormat {Checkpoint,WavefrontObj,StanfordPly,StereolithographyStl};
// Caller-selected interpretation is authoritative; only AutoDetect
// examines contents, never the extension (or an alleged file name).
enum class MeshOpenFilter {MesherCheckpoint,WavefrontObj,StanfordPly,StereolithographyStl,AllFiles};
struct MeshOpenCandidate {
    DurableCheckpoint snapshot;
    MeshOpenFormat format{};
    std::size_t vertexCount{},faceCount{};
    bool hasVertexColors{};
    // Exact parsed codec when distinguishable. A filename or Open filter
    // never chooses export semantics; the parser reports them after success.
    std::optional<MeshExportPreset> detectedExportPreset;
    // >=1.5% alpha byte span needs explicit interpretation by the user.
    // Until then, RGB remains visible with opaque mesh alpha.
    bool alphaDecisionRequired{};
    float sourceAlphaMinimum=1.f,sourceAlphaMaximum=1.f;
};
void applyPlySourceAlphaAsOpacity(MeshOpenCandidate& candidate);
MeshOpenCandidate readMeshOpenCandidate(
    const std::filesystem::path& path,
    MeshOpenFilter selectedFilter=MeshOpenFilter::AllFiles,
    StlImportOptions stlOptions={});
} // namespace mesher
