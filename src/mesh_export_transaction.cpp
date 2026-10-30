#include "mesher/mesh_export_transaction.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ios>
#include <limits>
#include <ostream>
#include <random>
#include <stdexcept>
#include <streambuf>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mesher {
namespace {
namespace fs=std::filesystem;

// FILE* is kept open from exclusive creation through sync/close, so an
// unrelated writer cannot interpose between reservation and actual output.
class FileStreamBuffer final:public std::streambuf {
public:
    explicit FileStreamBuffer(std::FILE* file):file_(file) {
        setp(buffer_.data(),buffer_.data()+buffer_.size());
    }
protected:
    int_type overflow(int_type ch) override {
        if(flushBuffer()!=0)return traits_type::eof();
        if(!traits_type::eq_int_type(ch,traits_type::eof())) {
            *pptr()=traits_type::to_char_type(ch);
            pbump(1);
        }
        return traits_type::not_eof(ch);
    }
    std::streamsize xsputn(const char* source,std::streamsize count) override {
        std::streamsize written=0;
        while(written<count) {
            if(pptr()==epptr()&&flushBuffer()!=0)break;
            const auto available=static_cast<std::streamsize>(epptr()-pptr());
            const auto size=std::min(available,count-written);
            std::memcpy(pptr(),source+written,static_cast<std::size_t>(size));
            pbump(static_cast<int>(size));
            written+=size;
        }
        return written;
    }
    int sync() override {
        if(flushBuffer()!=0)return -1;
        return std::fflush(file_)==0?0:-1;
    }
private:
    int flushBuffer() {
        const auto size=static_cast<std::size_t>(pptr()-pbase());
        if(size&&std::fwrite(pbase(),1,size,file_)!=size)return -1;
        pbump(-static_cast<int>(size));
        return 0;
    }
    std::FILE* file_;
    std::array<char,65536> buffer_{};
};

std::string uniqueSuffix() {
    static std::atomic<std::uint64_t> sequence{0};
    std::random_device random;
    const std::uint64_t a=(std::uint64_t{random()}<<32)|random();
    const std::uint64_t b=(std::uint64_t{random()}<<32)|random();
    const auto id=sequence.fetch_add(1,std::memory_order_relaxed);
    constexpr char digits[]="0123456789abcdef";
    std::string result=".mesher-export-";
    for(const std::uint64_t value:{a,b,id})
        for(int shift=60;shift>=0;shift-=4)
            result.push_back(digits[(value>>shift)&15u]);
    return result+".tmp";
}

class ExclusiveTempFile {
public:
    explicit ExclusiveTempFile(const fs::path& destination) {
        for(int attempt=0;attempt<32;++attempt) {
            path_=destination;
            path_+=uniqueSuffix();
            int descriptor=-1;
#ifdef _WIN32
            const errno_t result=_wsopen_s(&descriptor,path_.c_str(),
                _O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_SH_DENYWR,
                _S_IREAD|_S_IWRITE);
            if(result==EEXIST)continue;
            if(result!=0)
                throw std::system_error(result,std::generic_category(),
                                        "Unable to create STL/PLY/OBJ export temporary file");
            file_=_fdopen(descriptor,"wb");
            if(!file_)_close(descriptor);
#else
            descriptor=::open(path_.c_str(),O_WRONLY|O_CREAT|O_EXCL,0666);
            if(descriptor<0&&errno==EEXIST)continue;
            if(descriptor<0)
                throw std::system_error(errno,std::generic_category(),
                                        "Unable to create export temporary file");
            file_=::fdopen(descriptor,"wb");
            if(!file_)::close(descriptor);
#endif
            if(!file_)
                throw std::system_error(errno,std::generic_category(),
                                        "Unable to attach export output stream");
            return;
        }
        throw std::runtime_error("Export temporary filename collisions exhausted");
    }
    ExclusiveTempFile(const ExclusiveTempFile&)=delete;
    ExclusiveTempFile& operator=(const ExclusiveTempFile&)=delete;
    ~ExclusiveTempFile() {
        if(file_)std::fclose(file_);
        if(!published_) {
            std::error_code ignored;
            fs::remove(path_,ignored);
        }
    }
    std::FILE* file() const noexcept {return file_;}
    void syncAndClose() {
        if(!file_)throw std::logic_error("Export temporary file already closed");
        if(std::fflush(file_)!=0)
            throw std::system_error(errno,std::generic_category(),
                                    "Export temporary flush failed");
#ifdef _WIN32
        if(_commit(_fileno(file_))!=0)
#else
        if(::fsync(::fileno(file_))!=0)
#endif
            throw std::system_error(errno,std::generic_category(),
                                    "Export temporary file sync failed");
        const int result=std::fclose(file_);
        file_=nullptr;
        if(result!=0)
            throw std::runtime_error("Export temporary file close failed");
    }
    void publish(const fs::path& destination) {
        if(file_)throw std::logic_error("Cannot publish an open export stream");
        // Same-directory rename: atomic replacement on supported platforms.
        // In particular, NEVER unlink the existing destination on Windows
        // to make a failed overwrite appear to succeed.
        fs::rename(path_,destination);
        published_=true;
    }
private:
    fs::path path_;
    std::FILE* file_{};
    bool published_=false;
};
} // namespace

MeshExportRequest captureMeshExportRequest(
    const std::filesystem::path& destination,
    std::shared_ptr<const RegionTopologyIndex> acceptedIndex) {
    if(!acceptedIndex||destination.empty()||destination.filename().empty())
        throw std::invalid_argument(
            "Export requires an accepted immutable root and filename");
    auto absolute=std::filesystem::absolute(destination).lexically_normal();
    if(absolute.filename().empty())
        throw std::invalid_argument("Export destination must name a file");
    return {std::move(absolute),std::move(acceptedIndex)};
}

Mesh materializeMeshExport(const MeshExportRequest& request) {
    if(!request.acceptedIndex)
        throw std::invalid_argument("Export snapshot has no immutable indexed root");
    const auto& revision=request.acceptedIndex->revision();
    FlatMeshIndex ordered;
    revision.forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& v:part.vertices)ordered.vertexIds.push_back(v.id);
        for(const auto& f:part.faces)ordered.faceIds.push_back(f.id);
    });
    std::sort(ordered.vertexIds.begin(),ordered.vertexIds.end());
    std::sort(ordered.faceIds.begin(),ordered.faceIds.end());
    // Validates duplicate/absent IDs before writing; stable ordering survives
    // arbitrary partition changes, sculpt edits, deleted IDs and COW history.
    return reconstructFlatMesh(revision,ordered);
}

void writeMeshExportAtomic(const MeshExportRequest& request,
                           const MeshExportEncoder& encode) {
    if(!encode)
        throw std::invalid_argument("Export has no selected format encoder");
    if(!request.acceptedIndex||
       request.absoluteDestination.empty()||
       !request.absoluteDestination.is_absolute()||
       request.absoluteDestination.filename().empty())
        throw std::invalid_argument(
            "Export requires a captured absolute destination and immutable root");

    // Materialize and validate the immutable accepted source before creating
    // any output. The live editor may continue changing after this point.
    const Mesh mesh=materializeMeshExport(request);
    ExclusiveTempFile temporary(request.absoluteDestination);
    {
        FileStreamBuffer buffer(temporary.file());
        std::ostream output(&buffer);
        output.exceptions(std::ios::badbit|std::ios::failbit);
        encode(output,mesh);
        output.flush();
        if(!output)throw std::runtime_error("Export stream flush failed");
    }
    temporary.syncAndClose();
    temporary.publish(request.absoluteDestination);
}

void writePlyExportAtomic(const MeshExportRequest& request,
                          PlyExportOptions options) {
    writeMeshExportAtomic(request,[options](std::ostream& output,const Mesh& mesh) {
        writePlyMesh(output,mesh,options);
    });
}

AsyncMeshExportWriter::AsyncMeshExportWriter()
    :worker_([this] {run();}) {}
AsyncMeshExportWriter::~AsyncMeshExportWriter() {
    {
        std::lock_guard lock(mutex_);
        stopping_=true;
    }
    ready_.notify_all();
    if(worker_.joinable())worker_.join();
}

void AsyncMeshExportWriter::request(MeshExportRequest snapshot,
                                    MeshExportEncoder encoder) {
    if(!snapshot.acceptedIndex||snapshot.absoluteDestination.empty()||
       !snapshot.absoluteDestination.is_absolute()||!encoder)
        throw std::invalid_argument("Async export requires a captured request and encoder");
    {
        std::lock_guard lock(mutex_);
        if(stopping_)throw std::runtime_error("Export writer is stopping");
        if(pending_.size()>=32)
            throw std::length_error("Too many pending explicit mesh exports");
        pending_.push_back({std::move(snapshot),std::move(encoder)});
        ++statistics_.requested;
    }
    ready_.notify_one();
}
MeshExportStatistics AsyncMeshExportWriter::statistics() const {
    std::lock_guard lock(mutex_);
    auto current=statistics_;
    current.pending=pending_.size();
    current.busy=busy_;
    return current;
}
void AsyncMeshExportWriter::drain() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock,[this] {return pending_.empty()&&!busy_;});
}
void AsyncMeshExportWriter::run() noexcept {
    while(true) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock,[this] {return stopping_||!pending_.empty();});
            if(pending_.empty())return;
            task=std::move(pending_.front());
            pending_.pop_front();
            busy_=true;
        }
        std::string error;
        try {writeMeshExportAtomic(task.snapshot,task.encoder);}
        catch(const std::exception& e){error=e.what();}
        catch(...){error="Unknown export failure";}
        {
            std::lock_guard lock(mutex_);
            if(error.empty())++statistics_.completed;
            else {
                ++statistics_.failed;
                statistics_.lastError=std::move(error);
            }
            busy_=false;
        }
        idle_.notify_all();
    }
}
} // namespace mesher
