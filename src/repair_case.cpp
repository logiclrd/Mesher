#include "mesher/repair_case.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace mesher {
namespace {
constexpr std::array<std::uint8_t,8> signature{'M','S','H','R','C','A','S','1'};
constexpr std::uint64_t hashSeed=14695981039346656037ULL;
constexpr std::uint64_t hashPrime=1099511628211ULL;
constexpr std::uint64_t maxArchive=256ULL*1024*1024;
constexpr std::uint64_t maxMeshBytes=240ULL*1024*1024;
constexpr std::uint32_t maxContacts=512;
constexpr std::uint32_t maxDiagnostic=65536;

void put(std::vector<std::uint8_t>& bytes,std::uint64_t n,unsigned size) {
    for(unsigned k=0;k<size;++k)
        bytes.push_back(static_cast<std::uint8_t>((n>>(8*k))&255));
}
std::uint64_t get(const std::vector<std::uint8_t>& bytes,
                  std::size_t& offset,unsigned size) {
    if(size>8||size>bytes.size()-offset)
        throw std::runtime_error("R06 case truncated integer");
    std::uint64_t value=0;
    for(unsigned k=0;k<size;++k)
        value|=std::uint64_t{bytes[offset++]}<<(8*k);
    return value;
}
std::uint64_t checksum(const std::vector<std::uint8_t>& bytes,
                       std::size_t length) {
    auto value=hashSeed;
    for(std::size_t i=0;i<length;++i)value=(value^bytes[i])*hashPrime;
    return value;
}
void valid(const RepairCase& capture) {
    if(!capture.candidate.index||capture.contacts.empty()||
       capture.contacts.size()>maxContacts||
       capture.originalDiagnostic.size()>maxDiagnostic)
        throw std::invalid_argument("R06 case has no candidate, contacts, or finite metadata");
    std::set<std::pair<FaceId,FaceId>> unique;
    for(const auto& pair:capture.contacts) {
        if(!pair.first||!pair.second||pair.first>=pair.second||
           !capture.candidate.index->maybeFace(pair.first)||
           !capture.candidate.index->maybeFace(pair.second)||
           !unique.emplace(pair.first,pair.second).second)
            throw std::invalid_argument("R06 case contains invalid or duplicate trusted contact FaceIds");
    }
}
struct RemoveFile {
    std::filesystem::path path;
    ~RemoveFile() {std::error_code error;std::filesystem::remove(path,error);}
};
std::vector<std::uint8_t> loadBytes(
    const std::filesystem::path& path,std::uint64_t maximum) {
    const auto size=std::filesystem::file_size(path);
    if(size>maximum||size>std::numeric_limits<std::size_t>::max())
        throw std::length_error("R06 case exceeded finite file budget");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path,std::ios::binary);
    if(!stream||(!bytes.empty()&&!stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()))))
        throw std::runtime_error("Cannot read complete R06 case bytes");
    return bytes;
}
void storeBytes(const std::filesystem::path& path,
                const std::uint8_t* data,std::size_t length) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out||!out.write(reinterpret_cast<const char*>(data),
                        static_cast<std::streamsize>(length)))
        throw std::runtime_error("Cannot write R06 case bytes");
    out.flush();
    if(!out)throw std::runtime_error("Cannot flush R06 case bytes");
}
std::filesystem::path temporaryMesh(const std::filesystem::path& file) {
    auto p=file;p+=".embedded-checkpoint.mshr";return p;
}
}
void writeRepairCase(const std::filesystem::path& destination,
                     const RepairCase& capture) {
    valid(capture);
    if(destination.empty()||std::filesystem::exists(destination))
        throw std::invalid_argument("R06 case needs a new, unused destination");
    const auto meshPath=temporaryMesh(destination);
    const RemoveFile meshClean{meshPath};
    const RemoveFile meshTempClean{meshPath.string()+".tmp"};
    auto temporary=destination;temporary+=".tmp";
    const RemoveFile tempClean{temporary};
    if(std::filesystem::exists(meshPath)||std::filesystem::exists(temporary))
        throw std::runtime_error("R06 case temporary destination already exists");
    writeCheckpoint(meshPath,capture.candidate);
    const auto mesh=loadBytes(meshPath,maxMeshBytes);
    std::vector<std::uint8_t> encoded;
    encoded.reserve(mesh.size()+capture.contacts.size()*17+128+
                    capture.originalDiagnostic.size());
    encoded.insert(encoded.end(),signature.begin(),signature.end());
    put(encoded,1,4); // case version, independent of embedded checkpoint version
    put(encoded,capture.symmetricSource?1:0,1);
    put(encoded,capture.expectedClosure?1:0,1);
    put(encoded,static_cast<std::uint32_t>(capture.contacts.size()),4);
    put(encoded,static_cast<std::uint32_t>(capture.originalDiagnostic.size()),4);
    put(encoded,mesh.size(),8);
    for(const auto& p:capture.contacts) {
        put(encoded,p.first,8);
        put(encoded,p.second,8);
        put(encoded,p.ambiguous?1:0,1);
    }
    for(const auto c:capture.originalDiagnostic)
        encoded.push_back(static_cast<std::uint8_t>(c));
    encoded.insert(encoded.end(),mesh.begin(),mesh.end());
    put(encoded,checksum(encoded,encoded.size()),8);
    if(encoded.size()>maxArchive)
        throw std::length_error("R06 complete case exceeded finite file budget");
    storeBytes(temporary,encoded.data(),encoded.size());
    // Destination did not exist: never overwrite a prior failure capture.
    if(std::filesystem::exists(destination))
        throw std::runtime_error("R06 case destination appeared during save");
    std::filesystem::rename(temporary,destination);
}

RepairCase readRepairCase(const std::filesystem::path& source) {
    const auto bytes=loadBytes(source,maxArchive);
    constexpr std::size_t header=8+4+1+1+4+4+8;
    if(bytes.size()<header+8||
       !std::equal(signature.begin(),signature.end(),bytes.begin()))
        throw std::runtime_error("Unrecognized or truncated R06 case signature");
    std::size_t cursor=8;
    if(get(bytes,cursor,4)!=1)
        throw std::runtime_error("Unsupported R06 case file version");
    const auto symmetric=get(bytes,cursor,1);
    const auto succeeded=get(bytes,cursor,1);
    const auto count=get(bytes,cursor,4);
    const auto diagnosticSize=get(bytes,cursor,4);
    const auto meshSize=get(bytes,cursor,8);
    if(symmetric>1||succeeded>1||count==0||count>maxContacts||
       diagnosticSize>maxDiagnostic||meshSize>maxMeshBytes||
       std::uint64_t{header}+17*count+diagnosticSize+meshSize+8!=bytes.size())
        throw std::runtime_error("R06 case has invalid counts, flags, or trailing bytes");
    std::size_t hashPos=bytes.size()-8;
    const auto calculated=checksum(bytes,hashPos);
    if(get(bytes,hashPos,8)!=calculated)
        throw std::runtime_error("R06 case checksum mismatch");
    RepairCase capture;
    capture.symmetricSource=symmetric==1;
    capture.expectedClosure=succeeded==1;
    capture.contacts.reserve(static_cast<std::size_t>(count));
    for(std::uint64_t k=0;k<count;++k) {
        const auto a=get(bytes,cursor,8);
        const auto b=get(bytes,cursor,8);
        const auto ambiguous=get(bytes,cursor,1);
        if(ambiguous>1)
            throw std::runtime_error("R06 case invalid contact ambiguity flag");
        capture.contacts.push_back({a,b,ambiguous!=0});
    }
    capture.originalDiagnostic.assign(
        reinterpret_cast<const char*>(bytes.data()+cursor),
        static_cast<std::size_t>(diagnosticSize));
    cursor+=static_cast<std::size_t>(diagnosticSize);
    const auto meshPath=temporaryMesh(source);
    const RemoveFile cleanup{meshPath};
    if(std::filesystem::exists(meshPath))
        throw std::runtime_error("R06 replay temporary mesh path already occupied");
    storeBytes(meshPath,bytes.data()+cursor,static_cast<std::size_t>(meshSize));
    capture.candidate=readCheckpoint(meshPath);
    valid(capture);
    return capture;
}
std::filesystem::path uniqueRepairCasePath(
    const std::filesystem::path& directory) {
    if(directory.empty())throw std::invalid_argument("R06 case directory empty");
    std::filesystem::create_directories(directory);
    for(unsigned i=1;i<=100000;++i) {
        auto destination=directory/
            ("refusal-"+std::to_string(i)+".r06case");
        if(!std::filesystem::exists(destination)&&
           !std::filesystem::exists(destination.string()+".tmp")&&
           !std::filesystem::exists(temporaryMesh(destination)))
            return destination;
    }
    throw std::length_error("R06 refusal case name budget exhausted");
}
} // namespace mesher
