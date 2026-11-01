#pragma once
#include "mesher/mesh.hpp"
#include <filesystem>

namespace mesher {
// Standalone, bounded PLY reader. Does not modify any live revision,
// worker, renderer, picking state, checkpoint path or history.
// The parsed PLY header's actual representation, including Mesher's optional
// source-alpha provenance comment. Generic RGBA PLY cannot distinguish where
// its alpha originated; in that case prefer editable-mesh-alpha for export.
enum class PlyDetectedEncoding {Ascii, BinaryLittle, BinaryBig};
enum class PlyDetectedColour {None, Rgb, MeshRgba, OriginalRgba};
struct PlyDetectedFormat {
    PlyDetectedEncoding encoding{PlyDetectedEncoding::Ascii};
    PlyDetectedColour colour{PlyDetectedColour::None};
};
Mesh parsePlyMesh(const std::filesystem::path& path,
                  PlyDetectedFormat* detected=nullptr);
} // namespace mesher
