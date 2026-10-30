#pragma once

#include "mesher/mesh.hpp"

#include <iosfwd>

namespace mesher {

// UX07A: serialization only. The caller owns its immutable snapshot and
// destination transaction; this routine never opens files or edits a mesh.
// OBJ is not a standards-based vertex-colour format: the three colour
// dialects are explicitly named and intentionally separate.
enum class ObjExportEncoding {
    GeometryOnly,
    InlineRgb,
    InlineRgba,
    ZbrushMrgb
};

// Writes faces in stored winding and 1-based vertex index order.
// Colourless exports intentionally discard colour and alpha. InlineRgb
// and ZbrushMrgb discard alpha. ZbrushMrgb writes mask FF, which is NOT
// transparency. Callers must disclose these conversions in their UI.
// Throws on invalid/unrepresentable data or stream write errors.
void writeObjMesh(std::ostream& output, const Mesh& mesh,
                  ObjExportEncoding encoding);
} // namespace mesher
