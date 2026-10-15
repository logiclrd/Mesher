#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mesher {
namespace {
struct Bounds {
    std::array<long double,3> low{};
    std::array<long double,3> high{};
};
std::array<long double,3> xyz(Vec3 p) {
    return {static_cast<long double>(p.x),static_cast<long double>(p.y),
            static_cast<long double>(p.z)};
}
std::uint32_t quantize(long double value,long double low,long double high) {
    if(low==high)return 0;
    const auto span=high-low;
    const long double ratio=std::isfinite(span) ? (value-low)/span :
        (value/2-low/2)/(high/2-low/2);
    const long double normalized=std::clamp(ratio,0.0L,1.0L);
    return static_cast<std::uint32_t>(normalized*1023.0L);
}
std::uint32_t mortonKey(const std::array<long double,3>& point,
                        const Bounds& bounds) {
    const auto x=quantize(point[0],bounds.low[0],bounds.high[0]);
    const auto y=quantize(point[1],bounds.low[1],bounds.high[1]);
    const auto z=quantize(point[2],bounds.low[2],bounds.high[2]);
    std::uint32_t key=0;
    for(unsigned bit=0;bit<10;++bit)
        key|=((x>>bit)&1u)<<(3*bit) | ((y>>bit)&1u)<<(3*bit+1) |
             ((z>>bit)&1u)<<(3*bit+2);
    return key;
}
struct SpatialEntry {
    std::uint32_t key{};
    std::size_t original{};
};
void spatialSort(std::vector<SpatialEntry>& entries) {
    std::sort(entries.begin(),entries.end(),[](auto a,auto b){
        return a.key!=b.key ? a.key<b.key : a.original<b.original;
    });
}
} // namespace

PartitionedMeshImport partitionFlatMesh(
    const Mesh& flat,std::size_t maxElementsPerPartition) {
    if(maxElementsPerPartition==0)
        throw std::invalid_argument("Mesh partition capacity must be positive");
    const auto maximum=std::max(flat.vertices.size(),flat.triangles.size());
    if(maximum!=0 && (maximum-1)/maxElementsPerPartition >=
       (std::uint64_t{1}<<31))
        throw std::length_error("Mesh requires more than 2^32 partitions");
    Bounds bounds{};
    if(!flat.vertices.empty()) {
        bounds.low=bounds.high=xyz(flat.vertices.front());
        for(const auto point:flat.vertices) {
            const auto coords=xyz(point);
            for(unsigned axis=0;axis<3;++axis) {
                if(!std::isfinite(coords[axis]))
                    throw std::invalid_argument("Spatial partitioning requires finite vertices");
                bounds.low[axis]=std::min(bounds.low[axis],coords[axis]);
                bounds.high[axis]=std::max(bounds.high[axis],coords[axis]);
            }
        }
    }
    std::vector<SpatialEntry> vertexOrder;
    vertexOrder.reserve(flat.vertices.size());
    for(std::size_t i=0;i<flat.vertices.size();++i)
        vertexOrder.push_back({mortonKey(xyz(flat.vertices[i]),bounds),i});
    spatialSort(vertexOrder);

    std::vector<SpatialEntry> faceOrder;
    faceOrder.reserve(flat.triangles.size());
    for(std::size_t i=0;i<flat.triangles.size();++i) {
        const auto face=flat.triangles[i];
        if(face.a>=flat.vertices.size() || face.b>=flat.vertices.size() ||
           face.c>=flat.vertices.size())
            throw std::invalid_argument("Face references vertex outside flat mesh");
        const auto a=xyz(flat.vertices[face.a]);
        const auto b=xyz(flat.vertices[face.b]);
        const auto c=xyz(flat.vertices[face.c]);
        std::array<long double,3> center{};
        for(unsigned axis=0;axis<3;++axis)
            center[axis]=a[axis]/3.0L+b[axis]/3.0L+c[axis]/3.0L;
        faceOrder.push_back({mortonKey(center,bounds),i});
    }
    spatialSort(faceOrder);

    auto view=MeshRevision::empty()->createMutableView();
    FlatMeshIndex index;
    index.vertexIds.resize(flat.vertices.size());
    index.faceIds.resize(flat.triangles.size());
    for(std::size_t ordinal=0;ordinal<vertexOrder.size();++ordinal) {
        const auto original=vertexOrder[ordinal].original;
        const auto owner=static_cast<PartitionId>(ordinal/maxElementsPerPartition);
        index.vertexIds[original]=view.addVertex(owner,flat.vertices[original]);
    }
    for(std::size_t ordinal=0;ordinal<faceOrder.size();++ordinal) {
        const auto original=faceOrder[ordinal].original;
        const auto face=flat.triangles[original];
        const auto owner=static_cast<PartitionId>(ordinal/maxElementsPerPartition);
        index.faceIds[original]=view.addFace(owner,{
            index.vertexIds[face.a],index.vertexIds[face.b],index.vertexIds[face.c]});
    }
    auto revision=view.commit().revision;
    validatePartitionedMesh(*revision);
    return {std::move(revision),std::move(index)};
}

void validatePartitionedMesh(const MeshRevision& revision) {
    std::unordered_set<VertexId> vertices;
    std::unordered_set<FaceId> faces;
    std::vector<std::array<VertexId,3>> references;
    revision.forEachPartition([&](PartitionId,const MeshPartition& partition) {
        for(const auto& vertex:partition.vertices)
            if(vertex.id==0 || !vertices.insert(vertex.id).second)
                throw std::logic_error("Duplicate or invalid vertex ID");
        for(const auto& face:partition.faces) {
            if(face.id==0 || !faces.insert(face.id).second)
                throw std::logic_error("Duplicate or invalid face ID");
            references.push_back(face.vertices);
        }
    });
    for(const auto& ref:references)
        for(const auto vertex:ref)
            if(!vertices.contains(vertex))
                throw std::logic_error("Face references absent vertex ID");
}

Mesh reconstructFlatMesh(const MeshRevision& revision,
                         const FlatMeshIndex& originalOrder) {
    validatePartitionedMesh(revision);
    std::unordered_map<VertexId,Vec3> positions;
    std::unordered_map<FaceId,std::array<VertexId,3>> faces;
    revision.forEachPartition([&](PartitionId,const MeshPartition& partition) {
        for(const auto& v:partition.vertices)positions.emplace(v.id,v.position);
        for(const auto& f:partition.faces)faces.emplace(f.id,f.vertices);
    });
    if(positions.size()!=originalOrder.vertexIds.size() ||
       faces.size()!=originalOrder.faceIds.size())
        throw std::logic_error("Flat mapping excludes changed mesh elements");
    if(originalOrder.vertexIds.size()>
       static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::length_error("Flat mesh vertex indices exceed uint32 range");
    Mesh output;
    output.vertices.resize(originalOrder.vertexIds.size());
    output.triangles.resize(originalOrder.faceIds.size());
    std::unordered_map<VertexId,std::uint32_t> indices;
    for(std::size_t i=0;i<originalOrder.vertexIds.size();++i) {
        const auto id=originalOrder.vertexIds[i];
        const auto it=positions.find(id);
        if(it==positions.end() || !indices.emplace(id,static_cast<std::uint32_t>(i)).second)
            throw std::logic_error("Flat mapping contains missing/repeated vertex ID");
        output.vertices[i]=it->second;
    }
    std::unordered_set<FaceId> visitedFaces;
    for(std::size_t i=0;i<originalOrder.faceIds.size();++i) {
        const auto id=originalOrder.faceIds[i];
        const auto it=faces.find(id);
        if(it==faces.end() || !visitedFaces.insert(id).second)
            throw std::logic_error("Flat mapping contains missing/repeated face ID");
        const auto& f=it->second;
        output.triangles[i]={indices.at(f[0]),indices.at(f[1]),indices.at(f[2])};
    }
    return output;
}

} // namespace mesher
