#include "mesher/durable_checkpoint.hpp"

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
    (void)path; // Flush through std::ofstream; durable media sync is OS-specific.
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
    const auto marks=index.revision().identityWatermarks();
    const auto retiredVertices=index.retiredVertexIds();
    const auto retiredFaces=index.retiredFaceIds();
    writer.number(magic);
    writer.number(12,4);
    writer.number(marks.nextVertex);
    writer.number(marks.nextFace);
    writer.number(marks.nextOverflow);
    writer.number(index.revision().partitionCount());
    writer.number(snapshot.mirrorPairs.size());
    writer.number(retiredVertices.size());
    writer.number(retiredFaces.size());
    index.revision().forEachPartition(
        [&](PartitionId id,const MeshPartition& partition) {
            writer.number(id,4);
            writer.number(partition.vertices.size(),4);
            writer.number(partition.faces.size(),4);
            for(const auto& vertex:partition.vertices) {
                writer.number(vertex.id);
                writer.position(vertex.position);
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
    if(reader.number()!=magic||reader.number(4)!=12)
        throw std::runtime_error("Unsupported checkpoint magic/version");
    MeshIdentityWatermarks marks{reader.number(),reader.number(),reader.number()};
    const auto partitions=reader.number();
    const auto pairs=reader.number();
    const auto retiredVertexCount=reader.number();
    const auto retiredFaceCount=reader.number();
    if(partitions>maxRecords||pairs>maxRecords||
       retiredVertexCount>maxRecords||retiredFaceCount>maxRecords)
        throw std::runtime_error("Checkpoint record counts exceed supported limits");
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
        for(std::uint64_t j=0;j<vertices;++j)
            part.vertices.push_back({reader.number(),reader.position()});
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
    for(std::uint64_t j=0;j<pairs;++j)
        mirrors.emplace_back(reader.number(),reader.number());
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
    return {std::move(index),std::move(mirrors)};
}

AsyncCheckpointWriter::AsyncCheckpointWriter()
    :worker_([this](std::stop_token token){run(token);}) {}
AsyncCheckpointWriter::~AsyncCheckpointWriter() {
    worker_.request_stop();
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
void AsyncCheckpointWriter::run(std::stop_token token) {
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
