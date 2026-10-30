#include "mesher/mesh_stl.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mesher {
namespace {
constexpr std::size_t maxStlFacets=2000000, maxStlVertices=1000000;
using Bytes=std::vector<unsigned char>;
std::uint16_t u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (unsigned(p[1])<<8));
}
std::uint32_t u32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) |
           (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
double f32(const unsigned char* p) {
    const float f=std::bit_cast<float>(u32(p));
    if(!std::isfinite(f))throw std::runtime_error("STL has nonfinite binary coordinate/normal");
    return static_cast<double>(f);
}
bool hasLabel(const unsigned char* header,const char* label,std::size_t n) {
    for(std::size_t i=0;i+n<=80;++i)
        if(std::memcmp(header+i,label,n)==0)return true;
    return false;
}
std::optional<VertexRGBA> magicsDefault(const unsigned char* h) {
    if(hasLabel(h,"MATERIAL=",9))
        throw std::runtime_error("STL Magics MATERIAL= appearance is unsupported");
    for(std::size_t i=0;i+6<=80;++i) {
        if(std::memcmp(h+i,"COLOR=",6)!=0)continue;
        if(i+10>80)
            throw std::runtime_error("STL Magics COLOR= header is truncated");
        const auto* p=h+i+6;
        return VertexRGBA{p[0]/255.f,p[1]/255.f,p[2]/255.f,p[3]/255.f};
    }
    return std::nullopt;
}
VertexRGBA packedColour(std::uint16_t attribute,bool magics) {
    const auto red=magics?(attribute&31u):((attribute>>10)&31u);
    const auto green=(attribute>>5)&31u;
    const auto blue=magics?((attribute>>10)&31u):(attribute&31u);
    return {red/31.f,green/31.f,blue/31.f,1.f};
}
struct Facet {
    std::array<Vec3,3> vertices{};
    std::optional<VertexRGBA> color{};
};
struct Builder {
    Mesh mesh{};
    std::map<std::tuple<double,double,double>,std::uint32_t> lookup{};
    std::vector<std::optional<VertexRGBA>> facetColours{};
    StlImportOptions options{};
    bool anyColoured=false;

    void add(const Facet& f) {
        if(mesh.triangles.size()>=maxStlFacets)
            throw std::length_error("STL facet limit exceeded");
        std::array<std::uint32_t,3> ids{};
        for(std::size_t i=0;i<3;++i) {
            const auto v=f.vertices[i];
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
                throw std::runtime_error("STL contains nonfinite vertex");
            const auto key=std::make_tuple(v.x,v.y,v.z);
            const auto it=lookup.find(key);
            if(it==lookup.end()) {
                if(mesh.vertices.size()>=maxStlVertices)
                    throw std::length_error("STL welded vertex limit exceeded");
                ids[i]=static_cast<std::uint32_t>(mesh.vertices.size());
                lookup.emplace(key,ids[i]);
                mesh.vertices.push_back(v);
                // Decoded colour belongs to the facet, not this vertex.
            }else {
                ids[i]=it->second;
                // Preserve shared topology even if incident facet colours disagree.
            }
        }
        if(ids[0]==ids[1]||ids[1]==ids[2]||ids[0]==ids[2])
            throw std::runtime_error(
                "STL facet "+std::to_string(mesh.triangles.size())+
                " collapses under exact vertex welding (coincident corners). "
                "Regenerate or remove zero-area source facets; a closed, "
                "consistently wound mesh is required.");
        const Vec3 a=mesh.vertices[ids[0]], b=mesh.vertices[ids[1]],
                   c=mesh.vertices[ids[2]];
        const double scale=length(b-a)*length(c-a);
        if(!(scale>0)||!std::isfinite(scale)||
           !(length(cross(b-a,c-a))>1e-12*scale))
            throw std::runtime_error("STL facet has zero area");
        mesh.triangles.push_back({ids[0],ids[1],ids[2]});
        anyColoured|=f.color.has_value();
        facetColours.push_back(f.color);
    }
    Mesh finish() {
        // A mixed coloured/uncoloured VisCAM source uses opaque-white
        // fallback for absent colours in the comparative approximation.
        // Every exactly welded edge must have two oppositely oriented
        // incident triangles; no open seams or duplicate facet shells.
        using Edge=std::pair<std::uint32_t,std::uint32_t>;
        struct Occurrence {std::uint32_t face;int edge;bool forward;};
        std::map<Edge,std::vector<Occurrence>> edges;
        std::vector<std::vector<std::uint32_t>> stars(mesh.vertices.size());
        for(std::uint32_t i=0;i<mesh.triangles.size();++i) {
            const auto f=mesh.triangles[i];
            const std::array<std::uint32_t,3> v{f.a,f.b,f.c};
            for(int j=0;j<3;++j) {
                stars[v[j]].push_back(i);
                const auto a=v[j],b=v[(j+1)%3];
                auto& list=edges[{std::min(a,b),std::max(a,b)}];
                if(list.size()==2)
                    throw std::runtime_error("STL nonmanifold edge: more than two facets");
                list.push_back({i,j,a<b});
            }
        }
        std::vector<std::array<std::uint32_t,3>> neighbours(
            mesh.triangles.size());
        for(const auto& [edge,incident]:edges) {
            (void)edge;
            if(incident.size()!=2||incident[0].forward==incident[1].forward)
                throw std::runtime_error(
                    "STL must be closed and consistently wound after welding");
            neighbours[incident[0].face][incident[0].edge]=incident[1].face;
            neighbours[incident[1].face][incident[1].edge]=incident[0].face;
        }
        // Equal XYZ on unrelated sheets must not silently join their
        // vertex fans. Each welded vertex needs exactly one disk-like fan.
        std::vector<std::uint32_t> stamps(mesh.triangles.size(),0);
        std::uint32_t stamp=0;
        for(std::uint32_t v=0;v<stars.size();++v) {
            if(stars[v].empty())throw std::runtime_error("STL isolated welded vertex");
            ++stamp;
            std::vector<std::uint32_t> work{stars[v][0]};
            stamps[work[0]]=stamp;
            for(std::size_t at=0;at<work.size();++at) {
                const auto face=work[at];
                const auto triangle=mesh.triangles[face];
                const std::array<std::uint32_t,3> ids{
                    triangle.a,triangle.b,triangle.c};
                for(int e=0;e<3;++e) {
                    if(ids[e]!=v&&ids[(e+1)%3]!=v)continue;
                    const auto other=neighbours[face][e];
                    if(stamps[other]!=stamp) {
                        stamps[other]=stamp;
                        work.push_back(other);
                    }
                }
            }
            if(work.size()!=stars[v].size())
                throw std::runtime_error(
                    "STL coincident but disconnected vertex fans cannot be welded");
        }
        if(anyColoured)
            mesh.colors=convertStlFacetColours(
                mesh,facetColours,options.method,options.seed);
        return std::move(mesh);
    }
};
Mesh binary(const Bytes& data,StlImportOptions options) {
    if(data.size()<84)throw std::runtime_error("STL binary header is truncated");
    const std::size_t count=u32(data.data()+80);
    if(count>maxStlFacets||count==0)
        throw std::length_error("STL binary facet count is invalid/over limit");
    if(count>(std::numeric_limits<std::size_t>::max()-84)/50 ||
       data.size()!=84+count*50)
        throw std::runtime_error("STL binary facet count/length mismatch or truncated data");
    const auto defaultColor=magicsDefault(data.data());
    const bool magics=defaultColor.has_value();
    Builder builder;
    builder.options=options;
    for(std::size_t i=0;i<count;++i) {
        const auto* bytes=data.data()+84+50*i;
        // STL normals are advisory, but nonfinite normals are malformed.
        for(int k=0;k<3;++k)(void)f32(bytes+4*k);
        Facet facet;
        for(int v=0;v<3;++v)
            facet.vertices[v]={f32(bytes+12+12*v),
                               f32(bytes+16+12*v),f32(bytes+20+12*v)};
        const auto attribute=u16(bytes+48);
        if(magics)
            facet.color=(attribute&0x8000u)?
                *defaultColor:packedColour(attribute,true);
        else if(attribute&0x8000u)
            facet.color=packedColour(attribute,false);
        else if(attribute!=0)
            throw std::runtime_error(
                "STL facet attribute is ambiguous without Magics COLOR= header "
                "or VisCAM valid-colour bit");
        builder.add(facet);
    }
    return builder.finish();
}
double real(std::istream& in,const char* what) {
    double number=0;
    if(!(in>>number)||!std::isfinite(number))
        throw std::runtime_error(std::string("STL malformed ")+what);
    return number;
}
void expect(std::istream& in,const char* expected) {
    std::string token;
    if(!(in>>token)||token!=expected)
        throw std::runtime_error(std::string("STL expected '")+expected+"'");
}
Mesh ascii(const Bytes& data,StlImportOptions options) {
    const std::string input(data.begin(),data.end());
    if(input.find('\0')!=std::string::npos)
        throw std::runtime_error("STL ASCII contains embedded NUL");
    std::istringstream file(input);
    expect(file,"solid");
    std::string rest;
    std::getline(file,rest);
    Builder builder;
    builder.options=options;
    std::string tag;
    bool closed=false;
    while(file>>tag) {
        if(tag=="endsolid") {
            closed=true;
            std::getline(file,rest);
            break;
        }
        if(tag!="facet")
            throw std::runtime_error("STL ASCII expected facet or endsolid");
        expect(file,"normal");
        for(int i=0;i<3;++i)(void)real(file,"facet normal");
        expect(file,"outer");expect(file,"loop");
        Facet facet;
        for(auto& v:facet.vertices) {
            expect(file,"vertex");
            v={real(file,"vertex x"),real(file,"vertex y"),real(file,"vertex z")};
        }
        expect(file,"endloop");expect(file,"endfacet");
        builder.add(facet);
    }
    if(!closed||file>>tag)
        throw std::runtime_error("STL ASCII missing endsolid or has trailing records");
    return builder.finish();
}
} // namespace

Mesh parseStlMesh(const std::filesystem::path& path,
                  StlImportOptions options) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot open STL mesh");
    file.seekg(0,std::ios::end);
    const auto size=file.tellg();
    if(size<0||size>static_cast<std::streamoff>(256ull*1024*1024))
        throw std::length_error("STL input exceeds finite file size limit");
    file.seekg(0);
    Bytes data(static_cast<std::size_t>(size));
    if(!file.read(reinterpret_cast<char*>(data.data()),
                  static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("STL input truncated during read");
    const bool exactBinary=data.size()>=84&&
        std::size_t(u32(data.data()+80))<=maxStlFacets&&
        std::size_t(u32(data.data()+80))>0&&
        data.size()==84+std::size_t(u32(data.data()+80))*50;
    if(exactBinary)return binary(data,options);
    // Binary files may begin "solid". Prefer an exact binary layout above.
    // The explicit STL filter nevertheless detects partial binary payloads.
    const bool beginsSolid=data.size()>=5&&
        std::memcmp(data.data(),"solid",5)==0;
    if(beginsSolid)return ascii(data,options);
    if(data.size()>=84)
        throw std::runtime_error("STL binary facet count/length mismatch or truncated data");
    throw std::runtime_error("STL missing solid header or complete binary header");
}
} // namespace mesher
