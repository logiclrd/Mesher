#include "mesher/mesh_ply.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {
namespace {
constexpr std::size_t maxVertices=1000000,maxFaces=2000000;
constexpr std::size_t maxHeaderLines=4096,maxLineBytes=1048576;
enum class Encoding {Ascii,Little,Big};
enum class Scalar {I8,U8,I16,U16,I32,U32,F32,F64};
struct Property {
    std::string name;
    Scalar type{},countType{};
    bool list=false;
};
struct Element {
    std::string name;
    std::size_t count{};
    std::vector<Property> properties;
};
struct Header {
    Encoding encoding{};
    std::vector<Element> elements;
};

std::string takeLine(std::istream& input,const char* context) {
    std::string line;
    if(!std::getline(input,line))
        throw std::runtime_error(std::string("PLY: truncated ")+context);
    if(line.size()>maxLineBytes)
        throw std::runtime_error("PLY: oversized input line");
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    return line;
}
std::size_t checkedCount(const std::string& value,std::size_t maximum) {
    std::size_t consumed=0;
    unsigned long long count=0;
    try { count=std::stoull(value,&consumed,10); }
    catch(const std::exception&) {
        throw std::runtime_error("PLY: invalid element count");
    }
    if(value.empty()||value[0]=='-'||consumed!=value.size()||count>maximum)
        throw std::runtime_error("PLY: invalid or excessive element count");
    return static_cast<std::size_t>(count);
}
Scalar scalarType(const std::string& s) {
    if(s=="char"||s=="int8")return Scalar::I8;
    if(s=="uchar"||s=="uint8")return Scalar::U8;
    if(s=="short"||s=="int16")return Scalar::I16;
    if(s=="ushort"||s=="uint16")return Scalar::U16;
    if(s=="int"||s=="int32")return Scalar::I32;
    if(s=="uint"||s=="uint32")return Scalar::U32;
    if(s=="float"||s=="float32")return Scalar::F32;
    if(s=="double"||s=="float64")return Scalar::F64;
    throw std::runtime_error("PLY: unsupported property type '"+s+"'");
}
bool integer(Scalar type) {return type!=Scalar::F32&&type!=Scalar::F64;}
bool colourWord(const std::string& name) {
    // Explicitly refuse other colour conventions instead of discarding
    // colour as a generic unrecognized property.
    return name.find("red")!=std::string::npos||
           name.find("green")!=std::string::npos||
           name.find("blue")!=std::string::npos||
           name.find("alpha")!=std::string::npos||
           name.find("color")!=std::string::npos||
           name.find("colour")!=std::string::npos;
}
Header parseHeader(std::istream& input) {
    if(takeLine(input,"magic")!="ply")
        throw std::runtime_error("PLY: missing 'ply' magic");
    Header h;
    bool hasFormat=false,ended=false;
    for(std::size_t n=0;n<maxHeaderLines;++n) {
        const auto line=takeLine(input,"header");
        if(line=="end_header"){ended=true;break;}
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        std::string tag;
        if(!(row>>tag))continue;
        if(tag=="comment"||tag=="obj_info")continue;
        std::string a,b,c,extra;
        if(tag=="format") {
            if(hasFormat||!h.elements.empty()||!(row>>a>>b)||
               b!="1.0"||(row>>extra))
                throw std::runtime_error("PLY: invalid format declaration");
            if(a=="ascii")h.encoding=Encoding::Ascii;
            else if(a=="binary_little_endian")h.encoding=Encoding::Little;
            else if(a=="binary_big_endian")h.encoding=Encoding::Big;
            else throw std::runtime_error("PLY: unsupported encoding '"+a+"'");
            hasFormat=true;
        }else if(tag=="element") {
            if(!hasFormat||!(row>>a>>b)||(row>>extra))
                throw std::runtime_error("PLY: malformed element declaration");
            if(a!="vertex"&&a!="face")
                throw std::runtime_error("PLY: unsupported element '"+a+
                    "' (extra elements could contain material/colour data)");
            for(const auto& element:h.elements)
                if(element.name==a)
                    throw std::runtime_error("PLY: duplicate element '"+a+"'");
            if(a=="face"&&(h.elements.empty()||h.elements[0].name!="vertex"))
                throw std::runtime_error("PLY: vertex element must precede face");
            h.elements.push_back({a,checkedCount(b,a=="vertex"?maxVertices:maxFaces),{}});
        }else if(tag=="property") {
            if(h.elements.empty())
                throw std::runtime_error("PLY: property before element");
            auto& element=h.elements.back();
            if(element.properties.size()>=64)
                throw std::runtime_error("PLY: excessive element properties");
            Property prop;
            if(!(row>>a))
                throw std::runtime_error("PLY: missing property type");
            if(a=="list") {
                if(!(row>>a>>b>>c)||(row>>extra))
                    throw std::runtime_error("PLY: malformed list property");
                prop.list=true;
                prop.countType=scalarType(a);
                prop.type=scalarType(b);
                prop.name=c;
                if(!integer(prop.countType)||!integer(prop.type))
                    throw std::runtime_error("PLY: list count/index must be integer");
                if(element.name!="face"||
                   (prop.name!="vertex_indices"&&prop.name!="vertex_index"))
                    throw std::runtime_error(
                        "PLY: unsupported list property '"+prop.name+"'");
            }else {
                if(!(row>>b)||(row>>extra))
                    throw std::runtime_error("PLY: malformed scalar property");
                prop.type=scalarType(a);
                prop.name=b;
            }
            for(const auto& existing:element.properties)
                if(existing.name==prop.name)
                    throw std::runtime_error("PLY: duplicate property '"+prop.name+"'");
            element.properties.push_back(std::move(prop));
        }else throw std::runtime_error("PLY: unsupported header line '"+tag+"'");
    }
    if(!ended||!hasFormat||h.elements.size()!=2||
       h.elements[0].name!="vertex"||h.elements[1].name!="face")
        throw std::runtime_error("PLY: missing complete vertex/face header");
    const auto& vertex=h.elements[0];
    const auto& face=h.elements[1];
    bool x=false,y=false,z=false,red=false,green=false,blue=false,alpha=false;
    for(const auto& p:vertex.properties) {
        if(p.list)throw std::runtime_error("PLY: vertex list properties unsupported");
        if(p.name=="x")x=true;
        else if(p.name=="y")y=true;
        else if(p.name=="z")z=true;
        else if(p.name=="red")red=true;
        else if(p.name=="green")green=true;
        else if(p.name=="blue")blue=true;
        else if(p.name=="alpha")alpha=true;
        else if(colourWord(p.name))
            throw std::runtime_error("PLY: unsupported vertex colour variant '"+p.name+"'");
        if((p.name=="red"||p.name=="green"||p.name=="blue"||
            p.name=="alpha")&&p.type!=Scalar::U8)
            throw std::runtime_error(
                "PLY: vertex RGB(A) must be uchar/uint8 (0..255)");
    }
    if(!x||!y||!z||(red!=green)||(red!=blue)||(alpha&&!red))
        throw std::runtime_error("PLY: missing XYZ or incomplete vertex RGB(A)");
    bool indices=false;
    for(const auto& p:face.properties) {
        if(p.list) {
            if(indices)throw std::runtime_error("PLY: duplicate face index list");
            indices=true;
        }else if(colourWord(p.name))
            throw std::runtime_error(
                "PLY: facet colour properties are unsupported; cannot safely "
                "convert facet colours to vertex RGBA");
    }
    if(!indices)throw std::runtime_error("PLY: missing face vertex_indices list");
    if(vertex.count<4||face.count<4)
        throw std::runtime_error("PLY: editable surface needs at least four vertices/faces");
    return h;
}
std::size_t byteSize(Scalar t) {
    switch(t) {
        case Scalar::I8: case Scalar::U8:return 1;
        case Scalar::I16:case Scalar::U16:return 2;
        case Scalar::I32:case Scalar::U32:case Scalar::F32:return 4;
        case Scalar::F64:return 8;
    }
    throw std::runtime_error("PLY: unknown scalar size");
}
double readNumber(std::istream& input,Scalar t,Encoding encoding) {
    if(encoding==Encoding::Ascii) {
        std::string token;
        if(!(input>>token))throw std::runtime_error("PLY: truncated ASCII record");
        std::istringstream number(token);
        number.imbue(std::locale::classic());
        double value=0;
        number>>value;
        if(!number||!number.eof()||!std::isfinite(value))
            throw std::runtime_error("PLY: malformed/nonfinite ASCII value");
        if(integer(t)) {
            // Check integral representation and the declared bit range,
            // not merely whether the token looks numeric.
            const auto width=byteSize(t)*8;
            const bool sign=t==Scalar::I8||t==Scalar::I16||t==Scalar::I32;
            const double low=sign?-std::ldexp(1.0,static_cast<int>(width-1)):0.0;
            const double high=std::ldexp(1.0,static_cast<int>(width-(sign?1:0)));
            if(std::trunc(value)!=value||value<low||value>=high)
                throw std::runtime_error("PLY: integer property out of range");
        }
        return value;
    }
    const auto count=byteSize(t);
    std::array<unsigned char,8> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(count));
    if(!input)throw std::runtime_error("PLY: truncated binary payload");
    std::uint64_t bits=0;
    for(std::size_t j=0;j<count;++j) {
        const std::size_t i=encoding==Encoding::Little?count-1-j:j;
        bits=(bits<<8)|bytes[i];
    }
    switch(t) {
        case Scalar::I8:return std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(bits));
        case Scalar::U8:return static_cast<std::uint8_t>(bits);
        case Scalar::I16:return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits));
        case Scalar::U16:return static_cast<std::uint16_t>(bits);
        case Scalar::I32:return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits));
        case Scalar::U32:return static_cast<std::uint32_t>(bits);
        case Scalar::F32:return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
        case Scalar::F64:return std::bit_cast<double>(bits);
    }
    throw std::runtime_error("PLY: unknown scalar type");
}
Mesh readBody(std::istream& input,const Header& header) {
    Mesh mesh;
    const auto& vertex=header.elements[0];
    const auto& face=header.elements[1];
    mesh.vertices.reserve(vertex.count);
    mesh.triangles.reserve(face.count);
    const bool withColors=[&] {
        for(const auto& p:vertex.properties)if(p.name=="red")return true;
        return false;
    }();
    if(withColors)mesh.colors.reserve(vertex.count);
    bool withAlpha=false;
    for(const auto& p:vertex.properties)if(p.name=="alpha")withAlpha=true;
    const auto record=[&](const Element& element,auto&& process) {
        for(std::size_t n=0;n<element.count;++n) {
            std::istringstream line;
            std::istream* source=&input;
            if(header.encoding==Encoding::Ascii) {
                line.str(takeLine(input,"ASCII mesh record"));
                line.imbue(std::locale::classic());
                source=&line;
            }
            process(*source);
            if(header.encoding==Encoding::Ascii) {
                std::string extra;
                if(line>>extra)throw std::runtime_error("PLY: trailing ASCII record values");
            }
        }
    };
    record(vertex,[&](std::istream& source) {
        Vec3 point{};
        std::array<double,4> channels{255,255,255,255};
        for(const auto& p:vertex.properties) {
            const double value=readNumber(source,p.type,header.encoding);
            if(!std::isfinite(value))
                throw std::runtime_error("PLY: nonfinite vertex property");
            if(p.name=="x")point.x=value;
            else if(p.name=="y")point.y=value;
            else if(p.name=="z")point.z=value;
            else if(p.name=="red")channels[0]=value;
            else if(p.name=="green")channels[1]=value;
            else if(p.name=="blue")channels[2]=value;
            else if(p.name=="alpha")channels[3]=value;
        }
        mesh.vertices.push_back(point);
        if(withColors) {
            VertexRGBA color{
                static_cast<float>(channels[0]/255.0),
                static_cast<float>(channels[1]/255.0),
                static_cast<float>(channels[2]/255.0),1.f};
            if(withAlpha)
                color.sourceAlpha=static_cast<float>(channels[3]/255.0);
            mesh.colors.push_back(color);
        }
    });
    record(face,[&](std::istream& source) {
        Triangle triangle{};
        for(const auto& p:face.properties) {
            if(!p.list) {
                (void)readNumber(source,p.type,header.encoding);
                continue;
            }
            const double length=readNumber(source,p.countType,header.encoding);
            if(length!=3)throw std::runtime_error(
                "PLY: only triangle face indices are supported; polygons require triangulation");
            std::array<std::uint32_t,3> ids{};
            for(auto& id:ids) {
                const auto value=readNumber(source,p.type,header.encoding);
                if(value<0||value>=mesh.vertices.size()||
                   value!=std::trunc(value))
                    throw std::runtime_error("PLY: face index out of range");
                id=static_cast<std::uint32_t>(value);
            }
            triangle={ids[0],ids[1],ids[2]};
        }
        mesh.triangles.push_back(triangle);
    });
    if(header.encoding==Encoding::Ascii) {
        std::string trailing;
        while(std::getline(input,trailing))
            if(trailing.find_first_not_of(" \t\r\n")!=std::string::npos)
                throw std::runtime_error("PLY: unrecognized trailing records");
    }else if(input.peek()!=std::char_traits<char>::eof())
        throw std::runtime_error("PLY: unexpected binary trailing data");
    return mesh;
}
} // namespace

Mesh parsePlyMesh(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot open PLY for reading");
    const auto header=parseHeader(input);
    return readBody(input,header);
}
} // namespace mesher
