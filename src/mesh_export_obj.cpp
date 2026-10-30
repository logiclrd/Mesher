#include "mesher/mesh_export_obj.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <string>

namespace mesher {
namespace {
void validate(const Mesh& mesh, ObjExportEncoding encoding) {
    if(mesh.vertices.empty() || mesh.triangles.empty())
        throw std::invalid_argument("OBJ export requires nonempty geometry");
    if(!mesh.colors.empty() && mesh.colors.size()!=mesh.vertices.size())
        throw std::invalid_argument("OBJ export colour stream size mismatch");
    if(encoding!=ObjExportEncoding::GeometryOnly && mesh.colors.empty())
        throw std::invalid_argument("Coloured OBJ export requires vertex colours");
    for(const auto& v:mesh.vertices)
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
            throw std::invalid_argument("OBJ export has nonfinite coordinates");
    for(const auto& f:mesh.triangles)
        if(f.a>=mesh.vertices.size()||f.b>=mesh.vertices.size()||
           f.c>=mesh.vertices.size()||f.a==f.b||f.b==f.c||f.c==f.a)
            throw std::invalid_argument("OBJ export has invalid face indices");
    if(encoding!=ObjExportEncoding::GeometryOnly) {
        for(const auto& c:mesh.colors) {
            for(float value:std::array<float,4>{c.r,c.g,c.b,c.a})
                if(!std::isfinite(value)||value<0.f||value>1.f)
                    throw std::invalid_argument(
                        "OBJ export has colour outside finite [0,1] range");
        }
    }
}
unsigned channelByte(float value) {
    return static_cast<unsigned>(std::lround(static_cast<double>(value)*255.0));
}
char hexDigit(unsigned n) {
    constexpr char digits[]="0123456789ABCDEF";
    return digits[n&15u];
}
void hexByte(std::ostream& out,unsigned byte) {
    out.put(hexDigit(byte>>4));
    out.put(hexDigit(byte));
}
} // namespace

void writeObjMesh(std::ostream& output,const Mesh& mesh,
                  ObjExportEncoding encoding) {
    validate(mesh,encoding);
    // Restore the caller's formatting even when the stream throws. The
    // output is staged by a higher-level transaction before publication.
    struct RestoreFormat {
        std::ostream& out;
        std::ios saved{nullptr};
        explicit RestoreFormat(std::ostream& stream):out(stream) {
            saved.copyfmt(stream);
        }
        ~RestoreFormat() {try {out.copyfmt(saved);} catch(...) {}}
    } restore(output);
    output.exceptions(output.exceptions()|std::ios::badbit|std::ios::failbit);
    output.imbue(std::locale::classic());
    output<<std::setprecision(std::numeric_limits<double>::max_digits10);
    output<<"# Mesher Wavefront OBJ export\n";
    for(std::size_t i=0;i<mesh.vertices.size();++i) {
        const auto& v=mesh.vertices[i];
        output<<"v "<<v.x<<' '<<v.y<<' '<<v.z;
        if(encoding==ObjExportEncoding::InlineRgb ||
           encoding==ObjExportEncoding::InlineRgba) {
            const auto& c=mesh.colors[i];
            // The importer accepts normalized numeric RGB(A).
            output<<' '<<static_cast<double>(c.r)
                  <<' '<<static_cast<double>(c.g)
                  <<' '<<static_cast<double>(c.b);
            if(encoding==ObjExportEncoding::InlineRgba)
                output<<' '<<static_cast<double>(c.a);
        }
        output<<'\n';
    }
    if(encoding==ObjExportEncoding::ZbrushMrgb) {
        // The ZBrush reader expects exactly one MMRRGGBB word per vertex,
        // with at most 64 words per #MRGB record. MM is an editor mask.
        for(std::size_t i=0;i<mesh.colors.size();++i) {
            if(i%64==0)output<<"#MRGB ";
            const auto& c=mesh.colors[i];
            hexByte(output,255u); // fully unmasked; never an alpha byte
            hexByte(output,channelByte(c.r));
            hexByte(output,channelByte(c.g));
            hexByte(output,channelByte(c.b));
            if(i%64==63||i+1==mesh.colors.size())output<<'\n';
        }
    }
    for(const auto& f:mesh.triangles)
        output<<"f "<<static_cast<std::uint64_t>(f.a)+1u
              <<' '<<static_cast<std::uint64_t>(f.b)+1u
              <<' '<<static_cast<std::uint64_t>(f.c)+1u<<'\n';
    output.flush();
}
} // namespace mesher
