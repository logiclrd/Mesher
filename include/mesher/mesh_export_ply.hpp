#pragma once

#include "mesher/mesh.hpp"

#include <iosfwd>

namespace mesher {

// UX07B: portable serializer. Caller provides a frozen accepted Mesh and
// handles safe file replacement; no live editor state is modified here.
enum class PlyExportEncoding {
    Ascii,
    BinaryLittleEndian,
    BinaryBigEndian
};
enum class PlyExportColour {
    None,          // Standard colourless PLY
    Rgb,           // RGB uchar (no alpha)
    RgbaFromMesh,  // RGB + editable per-vertex alpha
    RgbaFromFile   // RGB + preserved original source alpha provenance
};
struct PlyExportOptions {
    PlyExportEncoding encoding{PlyExportEncoding::BinaryLittleEndian};
    PlyExportColour colour{PlyExportColour::None};
};

// Coordinates are declared double (binary float64) for exact position
// round-trip, faces are list uchar int with exactly three indices.
// The RGB(A) uchar channels quantize to 8 bits. In FromFile mode all
// vertices must have original sourceAlpha; missing provenance is an error,
// never silently replaced by mesh alpha. Validates BEFORE any stream output.
// Only transaction layer UX07D should write to final destinations.
void writePlyMesh(std::ostream& output,const Mesh& mesh,
                  PlyExportOptions options={});
} // namespace mesher
