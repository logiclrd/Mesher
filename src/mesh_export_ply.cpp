#include "mesher/mesh_export_ply.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <stdexcept>
#include <string>

namespace mesher {
namespace {
bool hasColour(PlyExportColour mode) {
    return mode!=PlyExportColour::None;
}
bool hasAlpha(PlyExportColour mode) {
    return mode==PlyExportColour::RgbaFromMesh||
           mode==PlyExportColour::RgbaFromFile;
}
void validate(const Mesh& mesh,PlyExportOptions options) {
    if(mesh.vertices.size()<4||mesh.triangles.size()<4||
       mesh.vertices.size()>1000000||mesh.triangles.size()>2000000)
        throw std::invalid_argument("PLY export vertex/face counts out of bounds");
    if(!mesh.colors.empty()&&mesh.colors.size()!=mesh.vertices.size())
        throw std::invalid_argument("PLY export colour stream size mismatch");
    if(hasColour(options.colour)&&mesh.colors.empty())
        throw std::invalid_argument("Coloured PLY export requires vertex colours");
    for(const auto& point:mesh.vertices) {
        if(!std::isfinite(point.x)||!std::isfinite(point.y)||
           !std::isfinite(point.z))
            throw std::invalid_argument("PLY export nonfinite vertex coordinate");
    }
    for(const auto& f:mesh.triangles) {
        if(f.a>=mesh.vertices.size()||f.b>=mesh.vertices.size()||
           f.c>=mesh.vertices.size()||f.a==f.b||f.b==f.c||f.c==f.a)
            throw std::invalid_argument("PLY export invalid triangle indices");
    }
    if(!hasColour(options.colour))return;
    const auto check=[](float channel) {
        if(!std::isfinite(channel)||channel<0.f||channel>1.f)
            throw std::invalid_argument(
                "PLY export colour channel outside finite [0,1]");
    };
    for(const auto& c:mesh.colors) {
        check(c.r);check(c.g);check(c.b);
        if(options.colour==PlyExportColour::RgbaFromMesh)check(c.a);
        if(options.colour==PlyExportColour::RgbaFromFile) {
            if(!c.sourceAlpha)
                throw std::invalid_argument(
                    "PLY export From File requires source alpha at every vertex");
            check(*c.sourceAlpha);
        }
    }
}
std::uint8_t byte(float value) {
    return static_cast<std::uint8_t>(std::lround(
        static_cast<double>(value)*255.0));
}
template<class T>
void binary(std::ostream& output,T value,bool little) {
    const auto bytes=std::bit_cast<std::array<std::byte,sizeof(T)>>(value);
    if((std::endian::native==std::endian::little)==little) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }else {
        for(std::size_t n=bytes.size();n>0;--n)
            output.put(static_cast<char>(std::to_integer<unsigned char>(
                bytes[n-1])));
    }
}
float chosenAlpha(const VertexRGBA& c,PlyExportColour mode) {
    return mode==PlyExportColour::RgbaFromFile?*c.sourceAlpha:c.a;
}
} // namespace

void writePlyMesh(std::ostream& output,const Mesh& mesh,
                  PlyExportOptions options) {
    validate(mesh,options);
    // Save and restore the caller's formatting, including locale and
    // exception flags. For failed output the caller discards staged bytes.
    struct Restore {
        std::ostream& output;
        std::ios settings{nullptr};
        explicit Restore(std::ostream& out):output(out),settings(out.rdbuf()) {
            settings.copyfmt(out);
        }
        ~Restore() {try {output.copyfmt(settings);} catch(...) {}}
    } restore(output);
    output.exceptions(output.exceptions()|std::ios::badbit|std::ios::failbit);
    output.imbue(std::locale::classic());
    const bool ascii=options.encoding==PlyExportEncoding::Ascii;
    const bool little=options.encoding==PlyExportEncoding::BinaryLittleEndian;
    output<<"ply\nformat "<<(ascii?"ascii":little?"binary_little_endian":
        "binary_big_endian")<<" 1.0\n";
    output<<"comment Mesher PLY export\n";
    output<<"element vertex "<<mesh.vertices.size()<<'\n';
    output<<"property double x\nproperty double y\nproperty double z\n";
    if(hasColour(options.colour)) {
        output<<"property uchar red\nproperty uchar green\n"
              <<"property uchar blue\n";
        if(hasAlpha(options.colour))output<<"property uchar alpha\n";
    }
    output<<"element face "<<mesh.triangles.size()<<'\n'
          <<"property list uchar int vertex_indices\nend_header\n";
    if(ascii) {
        output<<std::setprecision(std::numeric_limits<double>::max_digits10);
        for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto& p=mesh.vertices[i];
            output<<p.x<<' '<<p.y<<' '<<p.z;
            if(hasColour(options.colour)) {
                const auto& c=mesh.colors[i];
                output<<' '<<static_cast<unsigned>(byte(c.r))
                      <<' '<<static_cast<unsigned>(byte(c.g))
                      <<' '<<static_cast<unsigned>(byte(c.b));
                if(hasAlpha(options.colour))
                    output<<' '<<static_cast<unsigned>(
                        byte(chosenAlpha(c,options.colour)));
            }
            output<<'\n';
        }
        for(const auto& f:mesh.triangles)
            output<<"3 "<<f.a<<' '<<f.b<<' '<<f.c<<'\n';
    }else {
        for(std::size_t i=0;i<mesh.vertices.size();++i) {
            const auto& p=mesh.vertices[i];
            binary(output,p.x,little);
            binary(output,p.y,little);
            binary(output,p.z,little);
            if(hasColour(options.colour)) {
                const auto& c=mesh.colors[i];
                binary(output,byte(c.r),little);
                binary(output,byte(c.g),little);
                binary(output,byte(c.b),little);
                if(hasAlpha(options.colour))
                    binary(output,byte(chosenAlpha(c,options.colour)),little);
            }
        }
        for(const auto& f:mesh.triangles) {
            binary(output,static_cast<std::uint8_t>(3),little);
            binary(output,static_cast<std::int32_t>(f.a),little);
            binary(output,static_cast<std::int32_t>(f.b),little);
            binary(output,static_cast<std::int32_t>(f.c),little);
        }
    }
    output.flush();
}
} // namespace mesher
