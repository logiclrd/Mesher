#include "mesher/mesh_open_candidate.hpp"
#include "mesher/mesh_ply.hpp"
#include "mesher/mesh_stl.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mesher {
namespace {
constexpr std::uintmax_t maxInputBytes=256ull*1024*1024;
constexpr std::size_t maxVertices=1000000,maxFaces=2000000;
std::string lowercase(std::string value) {
    for(char& c:value)c=static_cast<char>(std::tolower(
        static_cast<unsigned char>(c)));
    return value;
}
double finiteNumber(const std::string& token,const std::string& source) {
    std::istringstream input(token);
    input.imbue(std::locale::classic());
    double value=0;
    input>>value;
    if(!input||!input.eof()||!std::isfinite(value))
        throw std::runtime_error(source+": nonfinite or malformed numeric value");
    return value;
}
std::uint32_t faceIndex(const std::string& token,std::size_t count,
                         const std::string& location) {
    if(token.find('/')!=std::string::npos)
        throw std::runtime_error(location+
            ": OBJ indexed texture/normal faces are not implemented yet");
    std::size_t consumed=0;
    long long raw=0;
    try {raw=std::stoll(token,&consumed,10);}
    catch(const std::exception&) {
        throw std::runtime_error(location+": invalid OBJ face index");
    }
    if(consumed!=token.size()||raw==0||
       (raw>0&&static_cast<unsigned long long>(raw)>count)||
       (raw<0&&static_cast<unsigned long long>(-(raw+1))+1>count))
        throw std::runtime_error(location+": OBJ face index out of range");
    const auto absolute=raw>0?static_cast<std::size_t>(raw-1):
        count-static_cast<std::size_t>(-(raw+1))-1;
    return static_cast<std::uint32_t>(absolute);
}
void validateClosedTriangles(const Mesh& mesh,const char* format) {
    if(mesh.vertices.size()<4||mesh.triangles.size()<4)
        throw std::runtime_error("Imported mesh must contain a closed triangle surface");
    using Edge=std::pair<std::uint32_t,std::uint32_t>;
    std::map<Edge,std::pair<unsigned,int>> edges;
    for(const auto f:mesh.triangles) {
        const std::array<std::uint32_t,3> ids{f.a,f.b,f.c};
        if(f.a==f.b||f.b==f.c||f.c==f.a)
            throw std::runtime_error(std::string(format)+": degenerate triangle");
        const auto a=mesh.vertices[f.a],b=mesh.vertices[f.b],
                   c=mesh.vertices[f.c];
        const auto ab=b-a,ac=c-a;
        const double edgeProduct=length(ab)*length(ac);
        if(!(edgeProduct>0)||!std::isfinite(edgeProduct)||
           !(length(cross(ab,ac))>1e-12*edgeProduct))
            throw std::runtime_error(std::string(format)+": zero-area triangle");
        for(int j=0;j<3;++j) {
            const auto u=ids[static_cast<std::size_t>(j)];
            const auto v=ids[static_cast<std::size_t>((j+1)%3)];
            const Edge key{std::min(u,v),std::max(u,v)};
            auto& entry=edges[key];
            if(++entry.first>2)
                throw std::runtime_error(std::string(format)+": nonmanifold edge");
            entry.second+=(u<v?1:-1);
        }
    }
    for(const auto& [edge,use]:edges) {
        (void)edge;
        if(use.first!=2||use.second!=0)
            throw std::runtime_error(
                std::string(format)+": requires a closed, consistently wound 2-manifold");
    }
}
unsigned objHexDigit(char ch) {
    if(ch>='0'&&ch<='9')return static_cast<unsigned>(ch-'0');
    if(ch>='a'&&ch<='f')return static_cast<unsigned>(ch-'a'+10);
    if(ch>='A'&&ch<='F')return static_cast<unsigned>(ch-'A'+10);
    throw std::runtime_error("OBJ MRGB contains a non-hexadecimal digit");
}
void appendObjMrgbLine(std::istringstream& row,
                       std::vector<VertexRGBA>& colours,
                       const std::string& location) {
    // ZBrush emits up to 64 MMRRGGBB words per #MRGB line. Accept packed
    // words or whitespace-separated COMPLETE words; do not join half words.
    std::size_t lineCount=0;
    std::string group;
    while(row>>group) {
        if(group.size()%8!=0)
            throw std::runtime_error(location+
                ": OBJ MRGB requires complete eight-hex-digit MMRRGGBB words");
        for(char ch:group)(void)objHexDigit(ch); // validate mask too
        for(std::size_t at=0;at<group.size();at+=8) {
            if(++lineCount>64)
                throw std::runtime_error(location+
                    ": OBJ MRGB permits at most 64 vertex colours per line");
            if(colours.size()>=maxVertices)
                throw std::length_error(location+
                    ": OBJ MRGB vertex-colour limit exceeded");
            const auto byte=[&](std::size_t offset) {
                return (objHexDigit(group[at+offset])<<4)|
                        objHexDigit(group[at+offset+1]);
            };
            // MM (the first byte) is a ZBrush *mask*, NEVER alpha.
            colours.push_back({byte(2)/255.f,byte(4)/255.f,
                               byte(6)/255.f,1.f});
        }
    }
    if(lineCount==0)
        throw std::runtime_error(location+": OBJ MRGB record has no colour words");
}
Mesh parseWavefrontObj(const std::filesystem::path& path) {
    std::ifstream input(path);
    if(!input)throw std::runtime_error("Cannot open OBJ for reading");
    input.imbue(std::locale::classic());
    Mesh mesh;
    bool anyColor=false,anyPlain=false,hasMrgb=false;
    std::vector<VertexRGBA> mrgbColours;
    std::string line;
    std::size_t lineNumber=0;
    while(std::getline(input,line)) {
        if(++lineNumber>10000000||line.size()>1048576)
            throw std::runtime_error("OBJ line/work limit exceeded");
        // ZBrush 2025 exports valid OBJ records followed by a single
        // NUL C-string terminator, even after the final newline. Treat
        // NUL bytes as padding ONLY when they occupy the file's tail;
        // an embedded NUL must never conceal subsequent OBJ records.
        const auto terminator=line.find('\0');
        if(terminator!=std::string::npos) {
            const bool onlyZeroPadding=std::all_of(
                line.begin()+static_cast<std::string::difference_type>(terminator),
                line.end(),[](char ch){return ch=='\0';});
            if(!onlyZeroPadding||
               input.peek()!=std::char_traits<char>::eof())
                throw std::runtime_error(
                    "OBJ line "+std::to_string(lineNumber)+
                    ": embedded NUL byte; only terminal NUL padding is permitted");
            line.resize(terminator);
        }
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        std::string tag;
        if(!(row>>tag))continue;
        const auto loc="OBJ line "+std::to_string(lineNumber);
        if(tag[0]=='#') {
            const auto commentTag=lowercase(tag);
            if(commentTag=="#mrgb") {
                if(anyColor)
                    throw std::runtime_error(loc+
                        ": OBJ MRGB conflicts with inline vertex RGB(A)");
                appendObjMrgbLine(row,mrgbColours,loc);
                hasMrgb=true;
                continue;
            }
            if(commentTag.starts_with("#mrgb"))
                throw std::runtime_error(loc+
                    ": malformed OBJ MRGB marker; expected '#MRGB ' and words");
            const auto comment=lowercase(line);
            // Other colour metadata conventions remain unsupported rather
            // than being silently discarded.
            if(comment.find("#vc ")!=std::string::npos||
               comment.find("# vertex color")!=std::string::npos||
               comment.find("# vertex colour")!=std::string::npos)
                throw std::runtime_error(loc+
                    ": OBJ metadata vertex-colour convention requires explicit support");
            continue;
        }
        if(tag=="v") {
            if(mesh.vertices.size()>=maxVertices)
                throw std::length_error("OBJ vertex limit exceeded");
            std::vector<double> data;
            std::string token;
            while(row>>token)data.push_back(finiteNumber(token,loc));
            if(data.size()!=3&&data.size()!=6&&data.size()!=7)
                throw std::runtime_error(loc+
                    ": expected XYZ, XYZ RGB or XYZ RGBA vertex values");
            mesh.vertices.push_back({data[0],data[1],data[2]});
            const bool colored=data.size()>=6;
            if(colored&&hasMrgb)
                throw std::runtime_error(loc+
                    ": OBJ inline vertex RGB(A) conflicts with MRGB");
            anyColor|=colored;
            anyPlain|=!colored;
            if(colored) {
                for(std::size_t i=3;i<data.size();++i)
                    if(data[i]<0||data[i]>1)
                        throw std::runtime_error(loc+
                            ": vertex RGB(A) must use normalized [0,1]; byte encoding ambiguous");
                mesh.colors.push_back({static_cast<float>(data[3]),
                    static_cast<float>(data[4]),static_cast<float>(data[5]),
                    data.size()==7?static_cast<float>(data[6]):1.f});
            }
        }else if(tag=="f") {
            if(mesh.triangles.size()>=maxFaces)
                throw std::length_error("OBJ face limit exceeded");
            std::array<std::uint32_t,3> ids{};
            std::string token;
            for(auto& id:ids) {
                if(!(row>>token))throw std::runtime_error(loc+
                    ": OBJ faces must be triangles");
                id=faceIndex(token,mesh.vertices.size(),loc);
            }
            if(row>>token)throw std::runtime_error(loc+
                ": polygon triangulation is not supported; face must have 3 vertices");
            mesh.triangles.push_back({ids[0],ids[1],ids[2]});
        }else if(tag=="o"||tag=="g"||tag=="s"||tag=="vn"||tag=="vt") {
            // Names, smoothing groups and unused vertex attributes do not
            // change the geometry of bare indexed triangle faces.
            continue;
        }else if(tag=="mtllib"||tag=="usemtl")
            throw std::runtime_error(loc+
                ": material library colours require explicit OBJ material import");
        else
            throw std::runtime_error(loc+": unsupported OBJ record '"+tag+"'");
    }
    if(!input.eof()&&input.fail())throw std::runtime_error("OBJ read failure");
    if(hasMrgb) {
        if(anyColor)
            throw std::runtime_error("OBJ MRGB conflicts with inline vertex RGB(A)");
        if(mrgbColours.size()!=mesh.vertices.size())
            throw std::runtime_error(
                "OBJ MRGB colour count ("+std::to_string(mrgbColours.size())+
                ") does not match vertex count ("+
                std::to_string(mesh.vertices.size())+")");
        mesh.colors=std::move(mrgbColours);
    }else if(anyColor&&anyPlain)
        throw std::runtime_error("OBJ mixes coloured and uncoloured vertices");
    validateClosedTriangles(mesh,"OBJ");
    return mesh;
}
// Headers are intentionally distinct. Repair cases contain a checkpoint
// inside the archive, but the outer file is NOT an editable mesh checkpoint.
MeshOpenFilter classifyMeshOpenFile(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot open file for format detection");
    const auto bytes=std::filesystem::file_size(path);
    std::array<char,4096> sample{};
    input.read(sample.data(),static_cast<std::streamsize>(sample.size()));
    const auto read=static_cast<std::size_t>(input.gcount());
    const std::string_view prefix(sample.data(),read);
    if(prefix.starts_with("MESHRV12"))return MeshOpenFilter::MesherCheckpoint;
    if(prefix.starts_with("ply\n")||prefix.starts_with("ply\r\n"))
        return MeshOpenFilter::StanfordPly;
    if(prefix.starts_with("MSHRCAS1"))
        throw std::runtime_error(
            "This is an R06 repair-case archive (.r06case), not an editable "
            ".mshr checkpoint. Use --repair-replay to inspect the case.");
    // Binary STL has no magic; recognize only exact 84 + 50*N layout.
    // Check before the text heuristic because binary headers can say "solid".
    if(bytes>=134&&read>=84) {
        const auto* q=reinterpret_cast<const unsigned char*>(sample.data()+80);
        const auto count=std::uint64_t(q[0])|(std::uint64_t(q[1])<<8)|
                         (std::uint64_t(q[2])<<16)|(std::uint64_t(q[3])<<24);
        if(count>0&&count<=maxFaces&&bytes==84+50*count)
            return MeshOpenFilter::StereolithographyStl;
    }
    if(prefix.starts_with("solid ")||prefix.starts_with("solid\n")||
       prefix.starts_with("solid\r\n"))
        return MeshOpenFilter::StereolithographyStl;
    // Conservative OBJ heuristic: only match recognized geometry records,
    // not arbitrary text; the strict OBJ parser will validate full contents.
    // Skip blank/comment lines; at least one vertex and face directive are
    // required in the peek window to avoid accepting unrelated text.
    std::istringstream lines{std::string(prefix)};
    std::string line;
    bool vertex=false,face=false;
    while(std::getline(lines,line)) {
        const auto pos=line.find_first_not_of(" \t\r");
        if(pos==std::string::npos||line[pos]=='#')continue;
        const std::string_view record(line.data()+pos,line.size()-pos);
        if(record.starts_with("v ")||record.starts_with("v\t"))vertex=true;
        else if(record.starts_with("f ")||record.starts_with("f\t"))face=true;
        else if(!(record.starts_with("o ")||record.starts_with("g ")||
                  record.starts_with("s ")||record.starts_with("vt ")||
                  record.starts_with("vn ")||record.starts_with("mtllib ")||
                  record.starts_with("usemtl ")))
            throw std::runtime_error(
                "Unrecognized mesh file header; no supported format detected");
    }
    if(vertex&&face)return MeshOpenFilter::WavefrontObj;
    // Some valid OBJ files have substantial vertex data before the first
    // face. A recognizable vertex prefix can identify the format safely;
    // the parser still rejects missing or invalid faces.
    if(vertex)return MeshOpenFilter::WavefrontObj;
    throw std::runtime_error(
        "Unrecognized mesh file header; supported formats: MESHRV12 "
        "checkpoints, PLY, STL and Wavefront OBJ text. R06 repair cases are replay-only.");
}
} // namespace

MeshOpenCandidate readMeshOpenCandidate(
    const std::filesystem::path& path,MeshOpenFilter selectedFilter,
    StlImportOptions stlOptions) {
    if(path.empty())throw std::invalid_argument("Open path is empty");
    const auto bytes=std::filesystem::file_size(path);
    if(!bytes||bytes>maxInputBytes)
        throw std::length_error("Mesh import file is empty or exceeds 256 MiB limit");
    const auto format=selectedFilter==MeshOpenFilter::AllFiles?
        classifyMeshOpenFile(path):selectedFilter;
    MeshOpenCandidate candidate;
    if(format==MeshOpenFilter::MesherCheckpoint) {
        // Preserve the explicit selected parser, but give a known foreign
        // Mesher archive a useful error instead of an opaque magic failure.
        std::ifstream probe(path,std::ios::binary);
        std::array<char,8> signature{};
        probe.read(signature.data(),static_cast<std::streamsize>(
            signature.size()));
        if(probe.gcount()==static_cast<std::streamsize>(signature.size())&&
           std::string_view(signature.data(),signature.size())=="MSHRCAS1")
            throw std::runtime_error(
                "This file is an R06 repair-case archive (.r06case), "
                "not an editable .mshr checkpoint. Use --repair-replay.");
        candidate.snapshot=readCheckpoint(path);
        candidate.format=MeshOpenFormat::Checkpoint;
    }else if(format==MeshOpenFilter::WavefrontObj||
             format==MeshOpenFilter::StanfordPly||
             format==MeshOpenFilter::StereolithographyStl) {
        const auto began=std::chrono::steady_clock::now();
        auto mesh=format==MeshOpenFilter::StanfordPly?parsePlyMesh(path):
                  format==MeshOpenFilter::StereolithographyStl?
                  parseStlMesh(path,stlOptions):parseWavefrontObj(path);
        const auto parsed=std::chrono::steady_clock::now();
        // OBJ validated before returning; PLY must enforce the same
        // topology invariants before its private revision is published.
        if(format==MeshOpenFilter::StanfordPly||
           format==MeshOpenFilter::StereolithographyStl)
            validateClosedTriangles(mesh,
                format==MeshOpenFilter::StanfordPly?"PLY":"STL");
        const auto validated=std::chrono::steady_clock::now();
        candidate.vertexCount=mesh.vertices.size();
        candidate.faceCount=mesh.triangles.size();
        candidate.hasVertexColors=!mesh.colors.empty();
        if(format==MeshOpenFilter::StanfordPly) {
            bool anySource=false;
            for(const auto& color:mesh.colors) {
                if(!color.sourceAlpha)continue;
                if(!anySource) {
                    candidate.sourceAlphaMinimum=*color.sourceAlpha;
                    candidate.sourceAlphaMaximum=*color.sourceAlpha;
                    anySource=true;
                }else {
                    candidate.sourceAlphaMinimum=std::min(
                        candidate.sourceAlphaMinimum,*color.sourceAlpha);
                    candidate.sourceAlphaMaximum=std::max(
                        candidate.sourceAlphaMaximum,*color.sourceAlpha);
                }
            }
            const bool originallyVaried=anySource&&
                candidate.sourceAlphaMaximum!=candidate.sourceAlphaMinimum;
            candidate.alphaDecisionRequired=anySource&&
                (candidate.sourceAlphaMaximum-candidate.sourceAlphaMinimum)>=0.015f;
            for(auto& color:mesh.colors)
                color.sourceAlphaOriginallyVaried=originallyVaried;
        }
        auto parts=partitionFlatMesh(mesh,256);
        candidate.snapshot=DurableCheckpoint{
            RegionTopologyIndex::build(std::move(parts.revision)),
            {},mesh.vertices.size()};
        candidate.format=format==MeshOpenFilter::StanfordPly?
            MeshOpenFormat::StanfordPly:
            format==MeshOpenFilter::StereolithographyStl?
            MeshOpenFormat::StereolithographyStl:MeshOpenFormat::WavefrontObj;
        if(format==MeshOpenFilter::StereolithographyStl) {
            const auto complete=std::chrono::steady_clock::now();
            const auto elapsed=[](auto a,auto b){
                return std::chrono::duration<double,std::milli>(b-a).count();
            };
            std::cout<<"[stl-import] parse_weld_ms="<<elapsed(began,parsed)
                     <<" topology_validation_ms="<<elapsed(parsed,validated)
                     <<" index_build_ms="<<elapsed(validated,complete)
                     <<" total_candidate_ms="<<elapsed(began,complete)<<"\n";
        }
    }else throw std::runtime_error("Unsupported selected mesh interpretation");
    if(!candidate.snapshot.index)
        throw std::runtime_error("Candidate import has no valid indexed root");
    if(candidate.format==MeshOpenFormat::Checkpoint) {
        candidate.snapshot.index->revision().forEachPartition(
            [&](PartitionId,const MeshPartition& part) {
                candidate.vertexCount+=part.vertices.size();
                candidate.faceCount+=part.faces.size();
                for(const auto& v:part.vertices)
                    candidate.hasVertexColors|=(v.color!=VertexRGBA{});
            });
    }
    if(candidate.vertexCount<4||candidate.faceCount<4)
        throw std::runtime_error("Candidate import has no editable closed surface");
    return candidate;
}
void applyPlySourceAlphaAsOpacity(MeshOpenCandidate& candidate) {
    if(candidate.format!=MeshOpenFormat::StanfordPly||
       !candidate.alphaDecisionRequired||!candidate.snapshot.index)
        throw std::invalid_argument("PLY alpha interpretation is unavailable");
    const auto previous=candidate.snapshot.index;
    auto view=previous->revision().createMutableView();
    previous->revision().forEachPartition(
        [&](PartitionId id,const MeshPartition& part) {
            for(const auto& vertex:part.vertices) {
                if(!vertex.color.sourceAlpha)
                    throw std::logic_error("PLY source alpha is incomplete");
                auto color=vertex.color;
                color.a=*color.sourceAlpha;
                view.setVertexColor(id,vertex.id,color);
            }
        });
    candidate.snapshot.index=RegionTopologyIndex::withVertexColors(
        *previous,view.commit().revision);
}
} // namespace mesher
