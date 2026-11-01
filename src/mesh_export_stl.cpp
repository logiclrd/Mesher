#include "mesher/mesh_export_stl.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <ios>
#include <limits>
#include <locale>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mesher {
namespace {
bool binaryMode(StlExportEncoding mode) {
    return mode!=StlExportEncoding::Ascii;
}
bool facetColour(StlExportEncoding mode) {
    return mode==StlExportEncoding::BinaryViscam||
           mode==StlExportEncoding::BinaryMagicsFacet;
}
bool magics(StlExportEncoding mode) {
    return mode==StlExportEncoding::BinaryMagicsFacet||
           mode==StlExportEncoding::BinaryMagicsObject;
}
void channel(float v) {
    if(!std::isfinite(v)||v<0.f||v>1.f)
        throw std::invalid_argument("STL export colour outside finite [0,1]");
}
void checkColour(VertexRGBA colour) {
    channel(colour.r);channel(colour.g);channel(colour.b);channel(colour.a);
}
std::array<Vec3,3> points(const Mesh& mesh,Triangle face,
                          const std::vector<Vec3>& represented) {
    (void)mesh;
    return {represented[face.a],represented[face.b],represented[face.c]};
}
struct CheckedGeometry {
    std::vector<Vec3> represented;
    std::vector<Vec3> normals;
};
CheckedGeometry validate(const Mesh& mesh,StlExportOptions options) {
    if(mesh.vertices.size()<4||mesh.vertices.size()>1000000||
       mesh.triangles.size()<4||mesh.triangles.size()>2000000)
        throw std::invalid_argument("STL export vertex/facet count outside limits");
    if(!mesh.colors.empty()&&mesh.colors.size()!=mesh.vertices.size())
        throw std::invalid_argument("STL export colour stream size mismatch");
    if(facetColour(options.encoding)&&mesh.colors.empty())
        throw std::invalid_argument("Coloured STL facets require vertex colours");
    if((options.encoding==StlExportEncoding::BinaryMagicsObject)!=
       options.objectColour.has_value())
        throw std::invalid_argument(
            "STL Magics object mode requires exactly one explicit object RGBA");
    if(options.objectColour)checkColour(*options.objectColour);
    if(facetColour(options.encoding))
        for(const auto& colour:mesh.colors)checkColour(colour);
    else if(options.strategy!=StlFacetColourStrategy::Mean)
        throw std::invalid_argument(
            "STL facet colour strategy requires a coloured facet encoding");

    CheckedGeometry checked;
    checked.represented.reserve(mesh.vertices.size());
    std::map<std::tuple<double,double,double>,std::size_t> positions;
    for(std::size_t i=0;i<mesh.vertices.size();++i) {
        const auto p=mesh.vertices[i];
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            throw std::invalid_argument("STL export nonfinite coordinate");
        Vec3 q=p;
        if(binaryMode(options.encoding)) {
            const float x=static_cast<float>(p.x);
            const float y=static_cast<float>(p.y);
            const float z=static_cast<float>(p.z);
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))
                throw std::invalid_argument("STL export float32 coordinate overflow");
            q={static_cast<double>(x),static_cast<double>(y),
               static_cast<double>(z)};
        }
        const auto [it,inserted]=positions.emplace(
            std::make_tuple(q.x,q.y,q.z),i);
        if(!inserted)
            throw std::invalid_argument(
                "STL export distinct vertices collapse under exact coordinate welding");
        (void)it;
        checked.represented.push_back(q);
    }

    using Edge=std::pair<std::uint32_t,std::uint32_t>;
    std::map<Edge,std::pair<unsigned,int>> edges;
    checked.normals.reserve(mesh.triangles.size());
    for(const auto& f:mesh.triangles) {
        if(f.a>=mesh.vertices.size()||f.b>=mesh.vertices.size()||
           f.c>=mesh.vertices.size()||f.a==f.b||f.b==f.c||f.c==f.a)
            throw std::invalid_argument("STL export invalid triangle indices");
        const auto p=points(mesh,f,checked.represented);
        const Vec3 ab=p[1]-p[0],ac=p[2]-p[0];
        const double area=length(cross(ab,ac));
        const double scale=length(ab)*length(ac);
        if(!(scale>0)||!std::isfinite(scale)||
           !(area>1e-12*scale)||!std::isfinite(area))
            throw std::invalid_argument(
                "STL export degenerate triangle after coordinate conversion");
        const Vec3 n=cross(ab,ac)/area;
        if(!std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z))
            throw std::invalid_argument("STL export invalid facet normal");
        checked.normals.push_back(n);
        const std::array<std::uint32_t,3> ids{f.a,f.b,f.c};
        for(unsigned k=0;k<3;++k) {
            const auto a=ids[k],b=ids[(k+1)%3];
            auto& use=edges[{std::min(a,b),std::max(a,b)}];
            ++use.first;
            use.second+=a<b?1:-1;
        }
    }
    for(const auto& [edge,use]:edges) {
        (void)edge;
        if(use.first!=2||use.second!=0)
            throw std::invalid_argument(
                "STL export requires a closed consistently wound triangle surface");
    }
    return checked;
}
void byte(std::ostream& out,std::uint8_t x) {
    out.put(static_cast<char>(x));
}
void u16(std::ostream& out,std::uint16_t x) {
    byte(out,static_cast<std::uint8_t>(x&255u));
    byte(out,static_cast<std::uint8_t>((x>>8)&255u));
}
void u32(std::ostream& out,std::uint32_t x) {
    for(unsigned i=0;i<4;++i)
        byte(out,static_cast<std::uint8_t>((x>>(8*i))&255u));
}
void f32(std::ostream& out,double value) {
    const float f=static_cast<float>(value);
    u32(out,std::bit_cast<std::uint32_t>(f));
}
std::uint8_t u8(float value) {
    return static_cast<std::uint8_t>(std::lround(double(value)*255.0));
}
std::uint16_t rgb555(const Mesh& mesh,Triangle f,StlExportEncoding mode) {
    const std::array<std::uint32_t,3> ids{f.a,f.b,f.c};
    const auto mean=[&](int component) {
        double sum=0;
        for(auto i:ids) {
            const auto& colour=mesh.colors[i];
            sum+=component==0?colour.r:component==1?colour.g:colour.b;
        }
        return static_cast<std::uint16_t>(std::lround(sum*(31.0/3.0)));
    };
    const auto r=mean(0),g=mean(1),b=mean(2);
    if(mode==StlExportEncoding::BinaryViscam)
        return static_cast<std::uint16_t>(0x8000u|(r<<10)|(g<<5)|b);
    return static_cast<std::uint16_t>((b<<10)|(g<<5)|r);
}
} // namespace

void writeStlMesh(std::ostream& output,const Mesh& mesh,
                  StlExportOptions options) {
    const auto checked=validate(mesh,options);
    // Only experimental modes build the dual graph. The historical
    // averaging path remains byte-for-byte identical and no slower.
    // Analyse all experimental attributes before writing the STL header.
    const auto colours=facetColour(options.encoding)&&
                       options.strategy!=StlFacetColourStrategy::Mean?
        stlFacetColourRGB555(mesh,options.strategy,options.seed):
        std::vector<std::uint16_t>{};
    struct Restore {
        std::ostream& out;
        std::ios saved{nullptr};
        explicit Restore(std::ostream& stream):out(stream),saved(stream.rdbuf()) {
            saved.copyfmt(stream);
        }
        ~Restore() {try {out.copyfmt(saved);} catch(...) {}}
    } restore(output);
    output.exceptions(output.exceptions()|std::ios::failbit|std::ios::badbit);
    output.imbue(std::locale::classic());
    if(options.encoding==StlExportEncoding::Ascii) {
        output<<std::setprecision(std::numeric_limits<double>::max_digits10);
        output<<"solid Mesher\n";
        for(std::size_t i=0;i<mesh.triangles.size();++i) {
            const auto n=checked.normals[i];
            const auto v=points(mesh,mesh.triangles[i],checked.represented);
            output<<"facet normal "<<n.x<<' '<<n.y<<' '<<n.z
                  <<"\n outer loop\n";
            for(const auto& p:v)
                output<<"  vertex "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
            output<<" endloop\nendfacet\n";
        }
        output<<"endsolid Mesher\n";
    }else {
        std::array<unsigned char,80> header{};
        constexpr char name[]="Mesher STL export";
        std::memcpy(header.data(),name,sizeof(name)-1);
        if(magics(options.encoding)) {
            constexpr char label[]="COLOR=";
            constexpr std::size_t offset=32;
            std::memcpy(header.data()+offset,label,sizeof(label)-1);
            const VertexRGBA colour=options.objectColour.value_or(VertexRGBA{});
            header[offset+6]=u8(colour.r);
            header[offset+7]=u8(colour.g);
            header[offset+8]=u8(colour.b);
            header[offset+9]=u8(colour.a);
        }
        output.write(reinterpret_cast<const char*>(header.data()),
                     static_cast<std::streamsize>(header.size()));
        u32(output,static_cast<std::uint32_t>(mesh.triangles.size()));
        for(std::size_t i=0;i<mesh.triangles.size();++i) {
            const auto n=checked.normals[i];
            const auto v=points(mesh,mesh.triangles[i],checked.represented);
            f32(output,n.x);f32(output,n.y);f32(output,n.z);
            for(const auto& p:v) {
                f32(output,p.x);f32(output,p.y);f32(output,p.z);
            }
            std::uint16_t attribute=0;
            if(facetColour(options.encoding)) {
                if(options.strategy==StlFacetColourStrategy::Mean)
                    attribute=rgb555(mesh,mesh.triangles[i],options.encoding);
                else {
                    const auto rgb=colours[i];
                    const auto r=rgb&31u,g=(rgb>>5)&31u,b=(rgb>>10)&31u;
                    attribute=options.encoding==StlExportEncoding::BinaryViscam?
                        static_cast<std::uint16_t>(
                            0x8000u|(r<<10)|(g<<5)|b):rgb;
                }
            }else if(options.encoding==StlExportEncoding::BinaryMagicsObject)
                attribute=0x8000u;
            u16(output,attribute);
        }
    }
    output.flush();
}
} // namespace mesher
