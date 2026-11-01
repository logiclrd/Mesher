#pragma once

#include "mesher/mesh.hpp"
#include "mesher/stl_facet_colour.hpp"

#include <iosfwd>
#include <optional>

namespace mesher {

// UX07C uses the exact dialects recognized by the existing strict STL
// importer. STL stores triangles, not indexed vertices; binary XYZ and
// normals are IEEE-754 float32, and all facets have exactly one colour.
// Conversion is necessarily lossy and must be disclosed by the future UI.
enum class StlExportEncoding {
    Ascii,               // Standard ASCII, no colour; full double text
    BinaryUncoloured,    // Standard binary with zero attribute words
    BinaryViscam,        // SolidView/VisCAM: bit15=1, R-high 5:5:5 RGB
    BinaryMagicsFacet,   // Magics COLOR= header, bit15=0, R-low 5:5:5 RGB
    BinaryMagicsObject   // Magics COLOR= header, bit15=1, one object RGBA
};
struct StlExportOptions {
    StlExportEncoding encoding{StlExportEncoding::BinaryUncoloured};
    // Required for BinaryMagicsObject; caller deliberately selects a single
    // global RGBA, regardless of existing per-vertex colours. Other dialects
    // reject an unexpected objectColour to avoid accidental metadata loss.
    std::optional<VertexRGBA> objectColour{};
    // The export dialog exposes explicit comparison modes for both RGB555
    // facet dialects. Existing callers retain exact historical means.
    StlFacetColourStrategy strategy=StlFacetColourStrategy::Mean;
    std::uint64_t seed=42;
};

// Writes no bytes until geometry, float32 representability, topology and
// requested colour encoding have been validated. In coloured facet modes,
// the default takes the arithmetic mean of its three vertex RGB colours,
// rounded to five bits/channel. Optional Palette and Diffusion strategies
// select quantized source colours and avoid averaging them. Alpha and
// cannot round-trip through facet-colour STL; objectColour RGBA is stored in
// the Magics object header and represents the whole mesh uniformly.
// UI/file transaction and immutable snapshot ownership are UX07D/E.
void writeStlMesh(std::ostream& output,const Mesh& mesh,
                  StlExportOptions options={});

} // namespace mesher
