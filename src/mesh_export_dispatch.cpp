#include "mesher/mesh_export_dispatch.hpp"
#include "mesher/mesh_export_obj.hpp"
#include "mesher/mesh_export_ply.hpp"
#include "mesher/mesh_export_stl.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace mesher {
namespace {
VertexRGBA averageObjectColour(const Mesh& mesh) {
    if(mesh.vertices.empty()||mesh.colors.size()!=mesh.vertices.size())
        throw std::invalid_argument(
            "Magics single-object RGBA requires a complete vertex colour stream");
    std::array<double,4> totals{};
    for(const auto& c:mesh.colors) {
        for(unsigned i=0;i<4;++i) {
            const float value=i==0?c.r:i==1?c.g:i==2?c.b:c.a;
            if(!std::isfinite(value)||value<0.f||value>1.f)
                throw std::invalid_argument("Invalid source RGBA for Magics object export");
            totals[i]+=value;
        }
    }
    const auto scale=1.0/static_cast<double>(mesh.colors.size());
    return {static_cast<float>(totals[0]*scale),
            static_cast<float>(totals[1]*scale),
            static_cast<float>(totals[2]*scale),
            static_cast<float>(totals[3]*scale)};
}
} // namespace

MeshExportEncoder meshExportEncoder(MeshExportPreset preset) {
    (void)meshExportFormat(preset); // explicit validation
    const auto index=static_cast<std::size_t>(preset);
    if(index<=static_cast<std::size_t>(MeshExportPreset::ObjZbrushMrgb)) {
        const auto encoding=static_cast<ObjExportEncoding>(index);
        return [encoding](std::ostream& out,const Mesh& mesh) {
            writeObjMesh(out,mesh,encoding);
        };
    }
    if(index<=static_cast<std::size_t>(MeshExportPreset::PlyBigSourceAlpha)) {
        const auto ordinal=index-static_cast<std::size_t>(
            MeshExportPreset::PlyAsciiGeometry);
        const auto encoding=static_cast<PlyExportEncoding>(ordinal/4);
        const auto colour=static_cast<PlyExportColour>(ordinal%4);
        return [encoding,colour](std::ostream& out,const Mesh& mesh) {
            writePlyMesh(out,mesh,{encoding,colour});
        };
    }
    const auto ordinal=index-static_cast<std::size_t>(MeshExportPreset::StlAscii);
    const auto encoding=static_cast<StlExportEncoding>(ordinal);
    return [encoding](std::ostream& out,const Mesh& mesh) {
        StlExportOptions options{encoding};
        if(encoding==StlExportEncoding::BinaryMagicsObject)
            options.objectColour=averageObjectColour(mesh);
        writeStlMesh(out,mesh,options);
    };
}
} // namespace mesher
