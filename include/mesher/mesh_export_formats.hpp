#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mesher {

// An export file-type selection determines the codec. Never infer the output
// format from a filename extension or discard alpha without an explicit
// policy selection.
enum class MeshExportPreset {
    ObjGeometry,
    ObjInlineRgb,
    ObjInlineRgba,
    ObjZbrushMrgb,
    PlyAsciiGeometry,
    PlyAsciiRgb,
    PlyAsciiMeshAlpha,
    PlyAsciiSourceAlpha,
    PlyLittleGeometry,
    PlyLittleRgb,
    PlyLittleMeshAlpha,
    PlyLittleSourceAlpha,
    PlyBigGeometry,
    PlyBigRgb,
    PlyBigMeshAlpha,
    PlyBigSourceAlpha,
    StlAscii,
    StlBinary,
    StlViscamRgb,
    StlMagicsRgb,
    StlMagicsObjectRgba,
};

struct MeshExportFormatEntry {
    MeshExportPreset preset;
    std::string_view label;
    std::string_view extension;
    // Non-empty when a mode deliberately loses information, quantizes data,
    // substitutes the stored alpha source, or approximates facet colours.
    std::string_view disclosure;
};

inline constexpr std::array<MeshExportFormatEntry,21> meshExportFormats{{
    {MeshExportPreset::ObjGeometry,
     "Wavefront OBJ - geometry only (*.obj)","obj",
     "Standard OBJ does not store Mesher vertex colour or alpha. Both are omitted."},
    {MeshExportPreset::ObjInlineRgb,
     "Wavefront OBJ - inline RGB (*.obj)","obj",
     "Inline vertex RGB is a nonstandard OBJ extension. Editable alpha is omitted."},
    {MeshExportPreset::ObjInlineRgba,
     "Wavefront OBJ - inline RGBA (*.obj)","obj",
     "Inline vertex RGBA is a nonstandard OBJ extension; some readers ignore it."},
    {MeshExportPreset::ObjZbrushMrgb,
     "ZBrush OBJ - MRGB vertex colours (*.obj)","obj",
     "ZBrush #MRGB encodes 8-bit RGB and an unmasked mask byte, NOT alpha. "
     "Mesh alpha is omitted."},

    {MeshExportPreset::PlyAsciiGeometry,
     "Stanford PLY - ASCII geometry only (*.ply)","ply",
     "This PLY mode omits vertex RGB and alpha."},
    {MeshExportPreset::PlyAsciiRgb,
     "Stanford PLY - ASCII RGB (*.ply)","ply",
     "Vertex RGB is quantized to 8 bits. Editable alpha is omitted."},
    {MeshExportPreset::PlyAsciiMeshAlpha,
     "Stanford PLY - ASCII RGBA mesh alpha (*.ply)","ply",
     "Vertex RGBA is quantized to 8 bits; preserved source alpha is NOT selected."},
    {MeshExportPreset::PlyAsciiSourceAlpha,
     "Stanford PLY - ASCII RGBA original alpha (*.ply)","ply",
     "Uses original source-file alpha, NOT the editable mesh alpha. "
     "Every vertex must retain source-alpha provenance; otherwise export fails."},

    {MeshExportPreset::PlyLittleGeometry,
     "Stanford PLY - binary LE geometry only (*.ply)","ply",
     "This PLY mode omits vertex RGB and alpha."},
    {MeshExportPreset::PlyLittleRgb,
     "Stanford PLY - binary LE RGB (*.ply)","ply",
     "Vertex RGB is quantized to 8 bits. Editable alpha is omitted."},
    {MeshExportPreset::PlyLittleMeshAlpha,
     "Stanford PLY - binary LE RGBA mesh alpha (*.ply)","ply",
     "Vertex RGBA is quantized to 8 bits; preserved source alpha is NOT selected."},
    {MeshExportPreset::PlyLittleSourceAlpha,
     "Stanford PLY - binary LE RGBA original alpha (*.ply)","ply",
     "Uses original source-file alpha, NOT the editable mesh alpha. "
     "Every vertex must retain source-alpha provenance; otherwise export fails."},

    {MeshExportPreset::PlyBigGeometry,
     "Stanford PLY - binary BE geometry only (*.ply)","ply",
     "This PLY mode omits vertex RGB and alpha."},
    {MeshExportPreset::PlyBigRgb,
     "Stanford PLY - binary BE RGB (*.ply)","ply",
     "Vertex RGB is quantized to 8 bits. Editable alpha is omitted."},
    {MeshExportPreset::PlyBigMeshAlpha,
     "Stanford PLY - binary BE RGBA mesh alpha (*.ply)","ply",
     "Vertex RGBA is quantized to 8 bits; preserved source alpha is NOT selected."},
    {MeshExportPreset::PlyBigSourceAlpha,
     "Stanford PLY - binary BE RGBA original alpha (*.ply)","ply",
     "Uses original source-file alpha, NOT the editable mesh alpha. "
     "Every vertex must retain source-alpha provenance; otherwise export fails."},

    {MeshExportPreset::StlAscii,
     "STL - ASCII geometry only (*.stl)","stl",
     "Standard ASCII STL has NO vertex colours or alpha."},
    {MeshExportPreset::StlBinary,
     "STL - binary geometry only (*.stl)","stl",
     "Standard binary STL has NO vertex colours or alpha. "
     "Coordinates become 32-bit floats."},
    {MeshExportPreset::StlViscamRgb,
     "STL - binary VisCAM/SolidView facet RGB (*.stl)","stl",
     "Every triangle receives its three vertex RGBs averaged and rounded "
     "to 5-bit VisCAM facet colour. Alpha and high-frequency detail are lost."},
    {MeshExportPreset::StlMagicsRgb,
     "STL - binary Materialise Magics facet RGB (*.stl)","stl",
     "Every triangle receives its three vertex RGBs averaged and rounded "
     "to 5-bit Magics facet colour. Alpha and high-frequency detail are lost."},
    {MeshExportPreset::StlMagicsObjectRgba,
     "STL - binary Magics single averaged RGBA (*.stl)","stl",
     "ALL vertices are collapsed to ONE object colour: the arithmetic mean "
     "of the current mesh's vertex RGBA. The complete spatial colour variation "
     "is lost; the RGBA is quantized to 8 bits and geometry to float32."}
}};

inline const MeshExportFormatEntry& meshExportFormat(MeshExportPreset preset) {
    const auto index=static_cast<std::size_t>(preset);
    if(index>=meshExportFormats.size()||meshExportFormats[index].preset!=preset)
        throw std::invalid_argument("Unknown mesh export file-type selection");
    return meshExportFormats[index];
}

inline std::filesystem::path validateExportFilename(
    std::filesystem::path path,MeshExportPreset preset) {
    const auto& info=meshExportFormat(preset);
    if(path.empty()||path.filename().empty())
        throw std::invalid_argument("Export file dialog selected no filename");
    const auto expected=std::string(".")+std::string(info.extension);
    const auto suffix=path.extension().string();
    if(suffix.empty())path+=expected;
    else {
        // The dropdown is the authority. Switching between the three known
        // export suffixes updates the filename, as native Save As dialogs do.
        // Unknown unrelated suffixes require an explicit user correction.
        auto lowercase=[](std::string value) {
            for(auto& c:value)
                if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
            return value;
        };
        const auto normalized=lowercase(suffix);
        if(normalized!=expected) {
            if(normalized==".obj"||normalized==".ply"||normalized==".stl")
                path.replace_extension(expected);
            else throw std::invalid_argument(
                "Selected export file type conflicts with a non-export extension");
        }
    }
    return path;
}

} // namespace mesher
