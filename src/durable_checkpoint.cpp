#include "mesher/durable_checkpoint.hpp"
#include "mesher/repair_surgery.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace mesher {
namespace {
constexpr std::uint64_t magic=0x323156524853454dULL; // "MESHRV12" in LE
constexpr std::uint64_t offset=14695981039346656037ULL;
constexpr std::uint64_t prime=1099511628211ULL;
constexpr std::uint64_t maxRecords=100000000ULL;
struct BinaryWriter {
    std::ofstream out;
    std::uint64_t hash=offset;
    explicit BinaryWriter(const std::filesystem::path& p)
        :out(p,std::ios::binary|std::ios::trunc) {
        if(!out)throw std::runtime_error("Cannot open checkpoint temp file");
    }
    void number(std::uint64_t value,unsigned count=8,bool include=true) {
        std::array<char,8> bytes{};
        for(unsigned i=0;i<count;++i) {
            const auto b=static_cast<std::uint8_t>((value>>(i*8))&255u);
            bytes[i]=static_cast<char>(b);
            if(include)hash=(hash^b)*prime;
        }
        out.write(bytes.data(),static_cast<std::streamsize>(count));
        if(!out)throw std::runtime_error("Checkpoint write failed");
    }
    void position(Vec3 p) {
        number(std::bit_cast<std::uint64_t>(p.x));
        number(std::bit_cast<std::uint64_t>(p.y));
        number(std::bit_cast<std::uint64_t>(p.z));
    }
};
struct BinaryReader {
    std::ifstream in;
    std::uint64_t hash=offset;
    std::uintmax_t remaining=0;
    explicit BinaryReader(const std::filesystem::path& p)
        :in(p,std::ios::binary),remaining(std::filesystem::file_size(p)) {
        if(!in)throw std::runtime_error("Cannot read checkpoint file");
    }
    std::uint64_t number(unsigned count=8,bool include=true) {
        if(remaining<count)throw std::runtime_error("Truncated checkpoint");
        std::array<unsigned char,8> bytes{};
        in.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(count));
        if(!in)throw std::runtime_error("Checkpoint I/O failure");
        remaining-=count;
        std::uint64_t result=0;
        for(unsigned i=0;i<count;++i) {
            result|=std::uint64_t{bytes[i]}<<(8*i);
            if(include)hash=(hash^bytes[i])*prime;
        }
        return result;
    }
    Vec3 position() {
        return {std::bit_cast<double>(number()),std::bit_cast<double>(number()),
                std::bit_cast<double>(number())};
    }
};
void durableFlush(const std::filesystem::path& path) {
#ifndef _WIN32
    const auto name=path.string();
    const auto fd=::open(name.c_str(),O_RDONLY);
    if(fd<0)throw std::runtime_error("Unable to open checkpoint for sync");
    const auto ok=::fsync(fd)==0;
    (void)::close(fd);
    if(!ok)throw std::runtime_error("Cannot sync checkpoint file");
#else
    const HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)
        throw std::runtime_error("Unable to reopen checkpoint for FlushFileBuffers");
    const BOOL flushed=FlushFileBuffers(handle);
    (void)CloseHandle(handle);
    if(!flushed)throw std::runtime_error("Cannot flush checkpoint file");
#endif
}
void durableDirectory(const std::filesystem::path& path) {
#ifndef _WIN32
    auto parent=path.parent_path();
    if(parent.empty())parent=".";
    const auto name=parent.string();
    const auto fd=::open(name.c_str(),O_RDONLY);
    if(fd<0)return;
    (void)::fsync(fd);
    (void)::close(fd);
#else
    (void)path;
#endif
}
void validateMirrors(const std::vector<std::pair<VertexId,VertexId>>& pairs,
                     const RegionTopologyIndex& index) {
    std::unordered_map<VertexId,VertexId> seen;
    for(const auto& [a,b]:pairs) {
        if(!a||!b||!index.maybeVertex(a)||!index.maybeVertex(b))
            throw std::invalid_argument("Checkpoint mirror refers to deleted vertex");
        const auto i=seen.find(a),j=seen.find(b);
        if((i!=seen.end()&&i->second!=b)||
           (j!=seen.end()&&j->second!=a))
            throw std::invalid_argument("Conflicting checkpoint symmetry");
        seen[a]=b;
        seen[b]=a;
    }
}
void writePayload(BinaryWriter& writer,const DurableCheckpoint& snapshot) {
    const auto& index=*snapshot.index;
    validateMirrors(snapshot.mirrorPairs,index);
    const bool fidelityRequired=snapshot.originalFidelityRequired||
        static_cast<bool>(snapshot.verifiedOriginalFidelity);
    if(snapshot.verifiedOriginalFidelity)
        verifyOriginalPatchPublicationRoot(
            *snapshot.verifiedOriginalFidelity,snapshot.index);
    const auto marks=index.revision().identityWatermarks();
    const auto retiredVertices=index.retiredVertexIds();
    const auto retiredFaces=index.retiredFaceIds();
    bool hasColor=false;
    index.revision().forEachPartition([&](PartitionId,const MeshPartition& p) {
        for(const auto& v:p.vertices)
            if(v.color!=VertexRGBA{})hasColor=true;
    });
    writer.number(magic);
    // Legacy V12/13 remain byte-for-byte unchanged for unpainted roots.
    // New V14/15 add a 16-byte float RGBA stream per vertex.
    writer.number(hasColor?(fidelityRequired?15:14):
                  (fidelityRequired?13:12),4);
    writer.number(marks.nextVertex);
    writer.number(marks.nextFace);
    writer.number(marks.nextOverflow);
    writer.number(snapshot.initialVertexCount?snapshot.initialVertexCount:
        [&] {
            std::size_t vertices=0;
            index.revision().forEachPartition([&](PartitionId,const MeshPartition& p) {
                vertices+=p.vertices.size();
            });
            return vertices;
        }());
    writer.number(index.revision().partitionCount());
    writer.number(snapshot.mirrorPairs.size());
    writer.number(retiredVertices.size());
    writer.number(retiredFaces.size());
    // No opaque proof is created by loading serialized metadata. The
    // version 13 marker survives a checksummed write/read roundtrip,
    // requiring explicit trusted first-original reauthentication.
    if(fidelityRequired)writer.number(1,1);
    index.revision().forEachPartition(
        [&](PartitionId id,const MeshPartition& partition) {
            writer.number(id,4);
            writer.number(partition.vertices.size(),4);
            writer.number(partition.faces.size(),4);
            for(const auto& vertex:partition.vertices) {
                writer.number(vertex.id);
                writer.position(vertex.position);
                if(hasColor)for(const float channel:{vertex.color.r,vertex.color.g,
                                                   vertex.color.b,vertex.color.a})
                    writer.number(std::bit_cast<std::uint32_t>(channel),4);
            }
            for(const auto& face:partition.faces) {
                writer.number(face.id);
                for(const auto vertex:face.vertices)writer.number(vertex);
            }
        });
    for(const auto& [a,b]:snapshot.mirrorPairs) {
        writer.number(a);
        writer.number(b);
    }
    for(const auto id:retiredVertices)writer.number(id);
    for(const auto id:retiredFaces)writer.number(id);
    writer.number(writer.hash,8,false);
    writer.out.flush();
    if(!writer.out)throw std::runtime_error("Checkpoint flush failed");
    writer.out.close();
    if(!writer.out)throw std::runtime_error("Checkpoint close failed");
}
}

void writeCheckpoint(const std::filesystem::path& destination,
                     const DurableCheckpoint& snapshot) {
    if(!snapshot.index||destination.empty())
        throw std::invalid_argument("Checkpoint needs a revision and filename");
    auto temporary=destination;
    temporary+=".tmp";
    try {
        {
            BinaryWriter writer(temporary);
            writePayload(writer,snapshot);
        }
        durableFlush(temporary);
        std::filesystem::rename(temporary,destination);
        durableDirectory(destination);
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(temporary,ec);
        throw;
    }
}

DurableCheckpoint readCheckpoint(const std::filesystem::path& destination) {
    BinaryReader reader(destination);
    if(reader.number()!=magic)
        throw std::runtime_error("Unsupported checkpoint magic/version");
    const auto version=reader.number(4);
    if(version!=12&&version!=13&&version!=14&&version!=15)
        throw std::runtime_error("Unsupported checkpoint magic/version");
    MeshIdentityWatermarks marks{reader.number(),reader.number(),reader.number()};
    const auto initialVertexCount=reader.number();
    if(!initialVertexCount||initialVertexCount>maxRecords)
        throw std::runtime_error("Checkpoint reference density invalid");
    const auto partitions=reader.number();
    const auto pairs=reader.number();
    const auto retiredVertexCount=reader.number();
    const auto retiredFaceCount=reader.number();
    const bool requiresOriginalFidelity=(version==13||version==15);
    const bool hasColor=(version>=14);
    if(requiresOriginalFidelity&&reader.number(1)!=1)
        throw std::runtime_error("Checkpoint protected source marker invalid");
    if(partitions>maxRecords||pairs>maxRecords||
       retiredVertexCount>maxRecords||retiredFaceCount>maxRecords||
       reader.remaining<8||
       partitions>(reader.remaining-8)/12||
       pairs>(reader.remaining-8)/16||
       retiredVertexCount>(reader.remaining-8)/8||
       retiredFaceCount>(reader.remaining-8)/8)
        throw std::runtime_error("Checkpoint record counts exceed file limits");
    std::vector<std::pair<PartitionId,MeshPartition>> loaded;
    loaded.reserve(static_cast<std::size_t>(partitions));
    std::uint64_t totalVertices=0,totalFaces=0;
    for(std::uint64_t i=0;i<partitions;++i) {
        const auto owner=static_cast<PartitionId>(reader.number(4));
        const auto vertices=reader.number(4);
        const auto faces=reader.number(4);
        if(vertices>256||faces>256||totalVertices+vertices>maxRecords||
           totalFaces+faces>maxRecords)
            throw std::runtime_error("Checkpoint partition limits exceeded");
        totalVertices+=vertices;
        totalFaces+=faces;
        MeshPartition part;
        part.vertices.reserve(static_cast<std::size_t>(vertices));
        part.faces.reserve(static_cast<std::size_t>(faces));
        for(std::uint64_t j=0;j<vertices;++j) {
            PartitionVertex v;
            v.id=reader.number();
            v.position=reader.position();
            if(hasColor) {
                float* const channels[]={&v.color.r,&v.color.g,&v.color.b,&v.color.a};
                for(auto* channel:channels)
                    *channel=std::bit_cast<float>(static_cast<std::uint32_t>(reader.number(4)));
                const auto valid=[](float x){return std::isfinite(x)&&x>=0&&x<=1;};
                if(!valid(v.color.r)||!valid(v.color.g)||
                   !valid(v.color.b)||!valid(v.color.a))
                    throw std::runtime_error("Checkpoint has invalid vertex RGBA");
            }
            part.vertices.push_back(v);
        }
        for(std::uint64_t j=0;j<faces;++j) {
            PartitionFace f;
            f.id=reader.number();
            for(auto& vertex:f.vertices)vertex=reader.number();
            part.faces.push_back(f);
        }
        loaded.emplace_back(owner,std::move(part));
    }
    std::vector<std::pair<VertexId,VertexId>> mirrors;
    mirrors.reserve(static_cast<std::size_t>(pairs));
    // These are stateful stream reads: C++ does not specify function
    // argument evaluation order for emplace_back(a(),b()). Explicit
    // sequencing preserves saved canonical pairs on GCC/Clang/MSVC.
    for(std::uint64_t j=0;j<pairs;++j) {
        const auto first=reader.number();
        const auto second=reader.number();
        mirrors.emplace_back(first,second);
    }
    std::vector<VertexId> retiredVertices;
    std::vector<FaceId> retiredFaces;
    retiredVertices.reserve(static_cast<std::size_t>(retiredVertexCount));
    retiredFaces.reserve(static_cast<std::size_t>(retiredFaceCount));
    for(std::uint64_t j=0;j<retiredVertexCount;++j)
        retiredVertices.push_back(reader.number());
    for(std::uint64_t j=0;j<retiredFaceCount;++j)
        retiredFaces.push_back(reader.number());
    const auto expected=reader.hash;
    if(reader.number(8,false)!=expected||reader.remaining!=0)
        throw std::runtime_error("Checkpoint checksum or trailing data mismatch");
    auto revision=MeshRevision::restore(std::move(loaded),marks);
    auto index=RegionTopologyIndex::restore(
        std::move(revision),retiredVertices,retiredFaces);
    validateMirrors(mirrors,*index);
    DurableCheckpoint recovered;
    recovered.index=std::move(index);
    recovered.mirrorPairs=std::move(mirrors);
    recovered.initialVertexCount=static_cast<std::size_t>(initialVertexCount);
    recovered.originalFidelityRequired=requiresOriginalFidelity;
    // Never reconstruct source ancestry from a hash of the terminal mesh.
    recovered.verifiedOriginalFidelity.reset();
    return recovered;
}

AsyncCheckpointWriter::AsyncCheckpointWriter()
    :worker_([this](StopToken token){run(token);}) {}
AsyncCheckpointWriter::~AsyncCheckpointWriter() {
    // ready_.wait() tests stop_requested() under mutex_. Synchronize
    // cancellation with that lock so an idle writer cannot miss the
    // wakeup between testing its predicate and sleeping.
    {
        std::lock_guard lock(mutex_);
        worker_.request_stop();
    }
    ready_.notify_all();
    worker_.join();
}
void AsyncCheckpointWriter::request(std::filesystem::path destination,
                                    DurableCheckpoint snapshot) {
    if(!snapshot.index||destination.empty())
        throw std::invalid_argument("Invalid checkpoint request");
    std::optional<Task> stale;
    {
        std::lock_guard lock(mutex_);
        if(pending_) {
            stale=std::move(pending_);
            ++statistics_.coalesced;
        }
        pending_=Task{std::move(destination),std::move(snapshot)};
        ++statistics_.requested;
    }
    if(stale)discarded_.retire(std::move(stale->snapshot.index));
    ready_.notify_one();
}
CheckpointStatistics AsyncCheckpointWriter::statistics() const {
    std::lock_guard lock(mutex_);
    return statistics_;
}
void AsyncCheckpointWriter::drain() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock,[this]{return !pending_&&!statistics_.busy;});
}
void AsyncCheckpointWriter::run(StopToken token) {
    std::unique_lock lock(mutex_);
    while(true) {
        ready_.wait(lock,[&]{return token.stop_requested()||pending_.has_value();});
        if(!pending_&&token.stop_requested())break;
        if(!pending_)continue;
        auto task=std::move(*pending_);
        pending_.reset();
        statistics_.busy=true;
        lock.unlock();
        std::string failure;
        std::uintmax_t bytes=0;
        try {
            writeCheckpoint(task.path,task.snapshot);
            bytes=std::filesystem::file_size(task.path);
        }catch(const std::exception& e){failure=e.what();}
        discarded_.retire(std::move(task.snapshot.index));
        lock.lock();
        if(failure.empty()) {
            ++statistics_.completed;
            statistics_.lastBytes=bytes;
            statistics_.lastError.clear();
        }else {
            ++statistics_.failed;
            statistics_.lastError=std::move(failure);
        }
        statistics_.busy=false;
        if(!pending_)idle_.notify_all();
    }
    idle_.notify_all();
}
} // namespace mesher
