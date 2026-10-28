#include "mesher/region_edit.hpp"
#include "mesher/exact_stage_work.hpp"
#include "mesher/revision_picking.hpp"
#include "mesher/triangle_collision.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
std::atomic<std::size_t> liveIndexedRoots{0};
std::atomic<std::size_t> peakIndexedRoots{0};
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
RegionTopologyIndex::Edge edge(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
void validateFace(std::array<VertexId,3> v) {
    if(v[0]==v[1]||v[0]==v[2]||v[1]==v[2])
        throw std::logic_error("Region index cannot store a degenerate face");
}
void eraseFaceId(std::vector<FaceId>& ids,FaceId id) {
    const auto it=std::find(ids.begin(),ids.end(),id);
    if(it==ids.end())throw std::logic_error("Missing face incidence during index update");
    ids.erase(it);
}
std::int64_t gridCoordinate(long double number) {
    constexpr auto low=static_cast<long double>(
        std::numeric_limits<std::int64_t>::min());
    constexpr auto high=static_cast<long double>(
        std::numeric_limits<std::int64_t>::max());
    if(std::isnan(number))throw std::invalid_argument("Invalid spatial coordinate");
    if(number<=low)return std::numeric_limits<std::int64_t>::min();
    if(number>=high)return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(std::floor(number));
}
std::int64_t toCell(double coordinate,double cellSize) {
    return gridCoordinate(static_cast<long double>(coordinate)/
                          static_cast<long double>(cellSize));
}
}

std::size_t RegionTopologyIndex::EdgeHash::operator()(Edge e) const noexcept {
    const auto a=std::hash<VertexId>{}(e.a);
    const auto b=std::hash<VertexId>{}(e.b);
    return a^(b+std::size_t{0x9e3779b9}+(a<<6)+(a>>2));
}
std::size_t RegionTopologyIndex::CellHash::operator()(Cell c) const noexcept {
    std::size_t h=0;
    for(const auto coord:{c.x,c.y,c.z}) {
        const auto next=std::hash<std::int64_t>{}(coord);
        h^=next+std::size_t{0x9e3779b9}+(h<<6)+(h>>2);
    }
    return h;
}
RegionTopologyIndex::RegionTopologyIndex(
    std::shared_ptr<const MeshRevision> revision,double cellSize)
    :revision_(std::move(revision)),cellSize_(cellSize) {
    const auto live=liveIndexedRoots.fetch_add(1)+1;
    auto peak=peakIndexedRoots.load();
    while(peak<live&&!peakIndexedRoots.compare_exchange_weak(peak,live)) {}
}
RegionTopologyIndex::~RegionTopologyIndex() {
    liveIndexedRoots.fetch_sub(1);
}
std::size_t RegionTopologyIndex::liveRevisionCount() noexcept {
    return liveIndexedRoots.load();
}
std::size_t RegionTopologyIndex::peakLiveRevisionCount() noexcept {
    return peakIndexedRoots.load();
}
RegionTopologyIndex::Cell RegionTopologyIndex::cellFor(
    Vec3 point,double cellSize) {
    return {toCell(point.x,cellSize),toCell(point.y,cellSize),
            toCell(point.z,cellSize)};
}

std::shared_ptr<const RegionTopologyIndex> RegionTopologyIndex::build(
    std::shared_ptr<const MeshRevision> revision,double cellSize) {
    if(!revision || !std::isfinite(cellSize) || cellSize<=0)
        throw std::invalid_argument("Region index needs a revision and positive grid size");
    auto result=std::shared_ptr<RegionTopologyIndex>(
        new RegionTopologyIndex(std::move(revision),cellSize));
    // One-time construction from a newly imported mesh. Nothing in advance()
    // or the transaction's commit path calls forEachPartition().
    std::unordered_map<VertexId,Vertex> vertices;
    std::unordered_map<FaceId,Face> faces;
    std::unordered_map<Edge,std::vector<EdgeSide>,EdgeHash> edges;
    std::unordered_map<Cell,std::vector<VertexId>,CellHash> cells;
    result->revision_->forEachPartition(
        [&](PartitionId owner,const MeshPartition& partition) {
        for(const auto& v:partition.vertices) {
            if(v.id==0||!finite(v.position)||
               !vertices.emplace(v.id,Vertex{owner,v.position,{}}).second)
                throw std::logic_error("Invalid or duplicate vertex ID in region index");
            cells[cellFor(v.position,cellSize)].push_back(v.id);
        }
        for(const auto& f:partition.faces) {
            validateFace(f.vertices);
            if(f.id==0||!faces.emplace(f.id,Face{owner,f.vertices}).second)
                throw std::logic_error("Invalid or duplicate face ID in region index");
        }
    });
    for(const auto& [id,f]:faces) {
        for(const auto v:f.vertices) {
            const auto it=vertices.find(v);
            if(it==vertices.end())
                throw std::logic_error("Face references an absent vertex");
            it->second.incidentFaces.push_back(id);
        }
        for(unsigned i=0;i<3;++i) {
            const auto a=f.vertices[i],b=f.vertices[(i+1)%3];
            const auto key=edge(a,b);
            edges[key].push_back({id,a==key.a});
        }
    }
    for(auto& [id,v]:vertices) {
        std::sort(v.incidentFaces.begin(),v.incidentFaces.end());
        result->vertices_.set(id,std::move(v));
    }
    for(auto& [id,f]:faces)result->faces_.set(id,std::move(f));
    for(auto& [key,sides]:edges) {
        std::sort(sides.begin(),sides.end(),[](auto a,auto b){
            return a.face<b.face;
        });
        result->edges_.set(key,std::move(sides));
    }
    for(auto& [key,ids]:cells) {
        std::sort(ids.begin(),ids.end());
        result->cells_.set(key,std::move(ids));
    }
    result->picking_=RevisionPickingIndex::build(*result);
    result->statistics_.fullMeshTraversals=1;
    return result;
}

std::shared_ptr<const RegionTopologyIndex> RegionTopologyIndex::advance(
    const RegionTopologyIndex& old,std::shared_ptr<const MeshRevision> revision,
    const std::unordered_map<VertexId,std::optional<Vec3>>& vertexChanges,
    const std::unordered_map<FaceId,
        std::optional<std::array<VertexId,3>>>& faceChanges,
    const std::unordered_map<VertexId,PartitionId>& addedVertices,
    const std::unordered_map<FaceId,PartitionId>& addedFaces) {
    if(!revision)throw std::invalid_argument("Indexed revision may not be null");
    auto result=std::shared_ptr<RegionTopologyIndex>(
        new RegionTopologyIndex(std::move(revision),old.cellSize_));
    // Copy four roots, NOT their elements: each field is a persistent map.
    result->vertices_=old.vertices_;
    result->faces_=old.faces_;
    result->edges_=old.edges_;
    result->cells_=old.cells_;
    result->retiredVertices_=old.retiredVertices_;
    result->retiredFaces_=old.retiredFaces_;

    std::unordered_map<VertexId,Vertex> vertices;
    std::unordered_map<Edge,std::vector<EdgeSide>,EdgeHash> edges;
    std::unordered_map<Cell,std::vector<VertexId>,CellHash> cells;
    auto writableVertex=[&](VertexId id)->Vertex& {
        auto it=vertices.find(id);
        if(it!=vertices.end())return it->second;
        const auto inserted=addedVertices.find(id);
        if(inserted!=addedVertices.end()) {
            const auto pos=vertexChanges.find(id);
            if(pos==vertexChanges.end()||!pos->second)
                throw std::logic_error("Added vertex lacks final position");
            return vertices.emplace(id,Vertex{inserted->second,*pos->second,{}})
                           .first->second;
        }
        return vertices.emplace(id,old.vertex(id)).first->second;
    };
    auto writableEdge=[&](Edge id)->std::vector<EdgeSide>& {
        auto it=edges.find(id);
        if(it!=edges.end())return it->second;
        const auto existing=old.edges_.find(id);
        return edges.emplace(id,existing ? *existing:std::vector<EdgeSide>{})
                    .first->second;
    };
    auto writableCell=[&](Cell key)->std::vector<VertexId>& {
        auto it=cells.find(key);
        if(it!=cells.end())return it->second;
        const auto existing=old.cells_.find(key);
        return cells.emplace(key,existing ? *existing:std::vector<VertexId>{})
                    .first->second;
    };
    // Subtract each old face's incidences before adding replacement faces.
    for(const auto& [id,_]:faceChanges) {
        if(addedFaces.contains(id))continue;
        const auto before=old.face(id).vertices;
        for(const auto vertex:before)
            eraseFaceId(writableVertex(vertex).incidentFaces,id);
        for(unsigned i=0;i<3;++i) {
            const auto a=before[i],b=before[(i+1)%3];
            auto& sides=writableEdge(edge(a,b));
            const auto it=std::find_if(sides.begin(),sides.end(),
                                       [id](auto side){return side.face==id;});
            if(it==sides.end())
                throw std::logic_error("Edge lost its previous face incidence");
            sides.erase(it);
        }
    }
    for(const auto& [id,replacement]:faceChanges) {
        if(!replacement)continue;
        validateFace(*replacement);
        for(const auto vertex:*replacement) {
            auto& incident=writableVertex(vertex).incidentFaces;
            if(std::find(incident.begin(),incident.end(),id)!=incident.end())
                throw std::logic_error("Duplicate updated face incidence");
            incident.push_back(id);
        }
        for(unsigned i=0;i<3;++i) {
            const auto a=(*replacement)[i],b=(*replacement)[(i+1)%3];
            const auto key=edge(a,b);
            writableEdge(key).push_back({id,a==key.a});
        }
    }
    // Vertex position edits affect only the old and new spatial hash cells.
    for(const auto& [id,position]:vertexChanges) {
        const auto previous=old.vertices_.find(id);
        if(previous) {
            auto& previousCell=writableCell(cellFor(previous->position,old.cellSize_));
            const auto it=std::find(previousCell.begin(),previousCell.end(),id);
            if(it==previousCell.end())
                throw std::logic_error("Vertex absent from original spatial bucket");
            previousCell.erase(it);
        }
        if(position) {
            if(!finite(*position))
                throw std::logic_error("Non-finite indexed vertex");
            writableVertex(id).position=*position;
            writableCell(cellFor(*position,old.cellSize_)).push_back(id);
        }else if(!previous)
            throw std::logic_error("Cannot remove a vertex that was never present");
    }
    std::size_t& copied=result->statistics_.copiedTreeNodes;
    for(auto& [id,v]:vertices) {
        const auto removed=vertexChanges.find(id);
        if(removed!=vertexChanges.end()&&!removed->second) {
            if(!v.incidentFaces.empty())
                throw std::logic_error("Removed vertex retained incident faces");
            result->vertices_.erase(id,&copied);
            result->retiredVertices_.set(id,true,&copied);
        }else {
            std::sort(v.incidentFaces.begin(),v.incidentFaces.end());
            result->vertices_.set(id,std::move(v),&copied);
        }
        ++result->statistics_.changedVertexRecords;
    }
    for(const auto& [id,replacement]:faceChanges) {
        if(replacement) {
            const auto added=addedFaces.find(id);
            const auto owner=added!=addedFaces.end() ? added->second:old.face(id).owner;
            result->faces_.set(id,Face{owner,*replacement},&copied);
        }else {
            result->faces_.erase(id,&copied);
            result->retiredFaces_.set(id,true,&copied);
        }
        ++result->statistics_.changedFaceRecords;
    }
    for(auto& [key,sides]:edges) {
        std::sort(sides.begin(),sides.end(),[](auto a,auto b){
            return a.face<b.face;
        });
        if(sides.empty())result->edges_.erase(key,&copied);
        else result->edges_.set(key,std::move(sides),&copied);
        ++result->statistics_.changedEdgeRecords;
    }
    for(auto& [key,ids]:cells) {
        std::sort(ids.begin(),ids.end());
        if(ids.empty())result->cells_.erase(key,&copied);
        else result->cells_.set(key,std::move(ids),&copied);
        ++result->statistics_.changedCellRecords;
    }
    result->picking_=RevisionPickingIndex::advance(
        *old.picking_,old,*result,vertexChanges,faceChanges,
        result->statistics_.rebuiltPickingPartitions,
        result->statistics_.copiedPickingDirectoryNodes);
    return result;
}

std::shared_ptr<const RegionTopologyIndex> RegionTopologyIndex::restore(
    std::shared_ptr<const MeshRevision> revision,
    const std::vector<VertexId>& retiredVertices,
    const std::vector<FaceId>& retiredFaces,double cellSize) {
    // Tombstone IDs must have been allocated before the serialized
    // allocator high-water marks. Otherwise the very next new element
    // could recycle an identity reported as permanently deleted.
    const auto marks=revision->identityWatermarks();
    auto result=std::const_pointer_cast<RegionTopologyIndex>(
        build(std::move(revision),cellSize));
    for(const auto id:retiredVertices) {
        if(!id||id>=marks.nextVertex||result->maybeVertex(id)||
           result->retiredVertex(id))
            throw std::invalid_argument("Invalid checkpoint vertex tombstone");
        result->retiredVertices_.set(id,true);
    }
    for(const auto id:retiredFaces) {
        if(!id||id>=marks.nextFace||result->faces_.find(id)||
           result->retiredFace(id))
            throw std::invalid_argument("Invalid checkpoint face tombstone");
        result->retiredFaces_.set(id,true);
    }
    return result;
}
std::shared_ptr<const RegionTopologyIndex> RegionTopologyIndex::privateExcision(
    const RegionTopologyIndex& source,
    std::shared_ptr<const MeshRevision> excised,
    const std::vector<VertexId>& removedVertices,
    const std::vector<FaceId>& removedFaces) {
    if(!excised)
        throw std::invalid_argument("R03 incremental excision requires a private revision");
    // These are source-backed DELETIONS, not a facility for replaying
    // arbitrary new geometry or reassigning existing stable IDs.
    std::unordered_map<VertexId,std::optional<Vec3>> vertices;
    std::unordered_map<FaceId,std::optional<std::array<VertexId,3>>> faces;
    vertices.reserve(removedVertices.size());
    faces.reserve(removedFaces.size());
    for(const auto id:removedVertices) {
        if(!source.maybeVertex(id)||!vertices.emplace(id,std::nullopt).second)
            throw std::logic_error("R03 incremental excision has invalid/duplicate source VertexId");
    }
    for(const auto id:removedFaces) {
        if(!source.maybeFace(id)||!faces.emplace(id,std::nullopt).second)
            throw std::logic_error("R03 incremental excision has invalid/duplicate source FaceId");
    }
    // The ordinary persistent topology updater also tombstones every
    // retired ID, preserves all previous tombstones and updates indexed
    // geometry and the partition BVH for only the changed region.
    return advance(source,std::move(excised),vertices,faces,{},{});
}

std::vector<VertexId> RegionTopologyIndex::retiredVertexIds() const {
    std::vector<VertexId> result;
    result.reserve(retiredVertices_.size());
    retiredVertices_.forEach([&](VertexId id,bool){result.push_back(id);});
    return result;
}
std::vector<FaceId> RegionTopologyIndex::retiredFaceIds() const {
    std::vector<FaceId> result;
    result.reserve(retiredFaces_.size());
    retiredFaces_.forEach([&](FaceId id,bool){result.push_back(id);});
    return result;
}

std::vector<VertexId> RegionTopologyIndex::querySphere(
    Vec3 center,double radius) const {
    if(!finite(center)||!std::isfinite(radius)||radius<0)
        throw std::invalid_argument("Invalid indexed sphere query");
    std::vector<VertexId> hits;
    if(radius==0)return hits;
    const Cell low=cellFor({center.x-radius,center.y-radius,center.z-radius},cellSize_);
    const Cell high=cellFor({center.x+radius,center.y+radius,center.z+radius},cellSize_);
    const auto inside=[&](VertexId id) {
        const auto& p=vertex(id).position;
        const long double x=static_cast<long double>(p.x)-center.x;
        const long double y=static_cast<long double>(p.y)-center.y;
        const long double z=static_cast<long double>(p.z)-center.z;
        const auto d=std::hypot(x,y,z);
        if(d<static_cast<long double>(radius))hits.push_back(id);
    };
    const auto axisLength=[](std::int64_t a,std::int64_t b)->long double {
        return static_cast<long double>(b)-static_cast<long double>(a)+1;
    };
    const long double volume=axisLength(low.x,high.x)*
                             axisLength(low.y,high.y)*axisLength(low.z,high.z);
    if(volume<=4096 && volume>0) {
        for(auto x=low.x;;++x) {
            for(auto y=low.y;;++y) {
                for(auto z=low.z;;++z) {
                    const auto bin=cells_.find({x,y,z});
                    if(bin)for(const auto id:*bin)inside(id);
                    if(z==high.z)break;
                }
                if(y==high.y)break;
            }
            if(x==high.x)break;
        }
    }else {
        // Huge radii: traverse occupied buckets, not unbounded empty volume.
        cells_.forEach([&](Cell key,const std::vector<VertexId>& ids) {
            if(key.x<low.x||key.x>high.x||key.y<low.y||key.y>high.y||
               key.z<low.z||key.z>high.z)return;
            for(const auto id:ids)inside(id);
        });
    }
    std::sort(hits.begin(),hits.end());
    return hits;
}

std::optional<RegionTopologyIndex::FaceRayHit>
RegionTopologyIndex::firstRayFaceHit(
    Ray ray,PickStatistics* statistics) const {
    if(!finite(ray.origin)||!finite(ray.direction)||
       length(ray.direction)<1e-12)
        throw std::invalid_argument("Invalid indexed picking ray");
    return picking_->firstRayFaceHit(ray,statistics);
}
std::optional<Vec3> RegionTopologyIndex::firstRayHit(
    Ray ray,PickStatistics* statistics) const {
    if(!finite(ray.origin)||!finite(ray.direction)||
       length(ray.direction)<1e-12)
        throw std::invalid_argument("Invalid indexed picking ray");
    return picking_->firstRayHit(ray,statistics);
}
const void* RegionTopologyIndex::pickingRootIdentity() const noexcept {
    return picking_->identity();
}

std::vector<RegionTopologyIndex::CollisionPair>
RegionTopologyIndex::triangleIntersections(
    const std::vector<VertexId>& changedVertices,
    const std::vector<FaceId>& changedFaces,
    CollisionStatistics* measured,
    const CollisionQueryLimits& limits,
    std::size_t groupedSeeds) const {
    CollisionStatistics local{};
    auto& stats=measured?*measured:local;
    stats={};
    const ScopedExactQueryAccounting queryAccounting([&]() noexcept {
        if(auto* work=executingExactStageWork)
            work->collision(stats.boxesTested,stats.partitionsTested,
                            stats.candidateTriangles,stats.narrowPhaseTests);
    });
    std::set<FaceId> affected;
    const auto addSeed=[&](FaceId id) {
        if(affected.contains(id))return;
        if(affected.size()>=limits.maxSeedFaces)
            throw std::length_error("Collision seed-face budget exhausted");
        affected.insert(id);
        stats.seedFaces=affected.size();
    };
    for(const auto id:changedVertices) {
        const auto v=maybeVertex(id);
        if(!v) {
            if(retiredVertex(id))continue;
            throw std::out_of_range("Unknown collision-query vertex ID");
        }
        for(const auto face:v->incidentFaces)addSeed(face);
    }
    for(const auto id:changedFaces) {
        if(retiredFace(id))continue;
        (void)face(id);
        addSeed(id);
    }
    std::set<std::pair<FaceId,FaceId>> visited;
    std::vector<CollisionPair> found;
    using Bounds=RevisionPickingIndex::Bounds;
    using Triangle=RevisionPickingIndex::Triangle;
    struct Seed {
        FaceId id{};
        std::array<VertexId,3> vertices{};
        std::array<Vec3,3> points{};
        Bounds bounds{};
        Vec3 center{};
    };
    const auto mergeBounds=[](Bounds& into,const Bounds& other) {
        if(!other.valid)return;
        if(!into.valid) {into=other;return;}
        into.low.x=std::min(into.low.x,other.low.x);
        into.low.y=std::min(into.low.y,other.low.y);
        into.low.z=std::min(into.low.z,other.low.z);
        into.high.x=std::max(into.high.x,other.high.x);
        into.high.y=std::max(into.high.y,other.high.y);
        into.high.z=std::max(into.high.z,other.high.z);
    };
    const auto intersectsBounds=[](const Bounds& a,const Bounds& b) {
        if(!a.valid||!b.valid)return false;
        const auto eps=[](double x,double y) {
            return 64*std::numeric_limits<double>::epsilon()*
                   std::max({1.0,std::abs(x),std::abs(y)});
        };
        return a.low.x<=b.high.x+eps(a.low.x,b.high.x)&&
               b.low.x<=a.high.x+eps(b.low.x,a.high.x)&&
               a.low.y<=b.high.y+eps(a.low.y,b.high.y)&&
               b.low.y<=a.high.y+eps(b.low.y,a.high.y)&&
               a.low.z<=b.high.z+eps(a.low.z,b.high.z)&&
               b.low.z<=a.high.z+eps(b.low.z,a.high.z);
    };
    std::vector<Seed> seeds;
    seeds.reserve(affected.size());
    for(const auto id:affected) {
        Seed seed;
        seed.id=id;
        seed.vertices=face(id).vertices;
        for(unsigned i=0;i<3;++i) {
            const auto p=vertex(seed.vertices[i]).position;
            seed.points[i]=p;
            seed.center=seed.center+p/3.0;
            Bounds point;
            point.low=point.high=p;
            point.valid=true;
            mergeBounds(seed.bounds,point);
        }
        seeds.push_back(seed);
    }
    // A one-axis sort can group faces on opposite sides of a round
    // shape into one giant Y/Z AABB, defeating BVH locality. A bounded
    // 10-bit-per-axis Morton key keeps each batch spatially coherent in
    // THREE dimensions, including on a symmetric bulging sculpt. This
    // only changes broad-phase envelopes, never candidate contact tests.
    if(groupedSeeds>1) {
        Bounds volume;
        for(const auto& seed:seeds)mergeBounds(volume,seed.bounds);
        const auto extent=volume.high-volume.low;
        const auto quantize=[](double p,double lo,double span) {
            if(span<=0)return std::uint32_t{0};
            const double t=std::clamp((p-lo)/span,0.0,1.0);
            return static_cast<std::uint32_t>(t*1023.0);
        };
        const auto morton=[&](Vec3 p) {
            const auto x=quantize(p.x,volume.low.x,extent.x);
            const auto y=quantize(p.y,volume.low.y,extent.y);
            const auto z=quantize(p.z,volume.low.z,extent.z);
            std::uint64_t key=0;
            for(unsigned bit=0;bit<10;++bit) {
                key|=static_cast<std::uint64_t>((x>>bit)&1u)<<(3*bit);
                key|=static_cast<std::uint64_t>((y>>bit)&1u)<<(3*bit+1);
                key|=static_cast<std::uint64_t>((z>>bit)&1u)<<(3*bit+2);
            }
            return key;
        };
        std::sort(seeds.begin(),seeds.end(),[&](const Seed& a,const Seed& b) {
            const auto ac=morton(a.center),bc=morton(b.center);
            return ac==bc?a.id<b.id:ac<bc;
        });
    }
    const auto evaluate=[&](const Seed& seed,const Triangle& candidate) {
        if(candidate.id==seed.id)return;
        if(!intersectsBounds(seed.bounds,candidate.bounds))return;
        const auto key=std::minmax(seed.id,candidate.id);
        if(visited.contains({key.first,key.second}))return;
        if(stats.narrowPhaseTests>=limits.maxNarrowPhaseTests)
            throw std::length_error("Collision narrow-phase budget exhausted");
        visited.emplace(key.first,key.second);
        ++stats.narrowPhaseTests;
        const auto kind=triangleContact(seed.points,seed.vertices,
                                        candidate.vertices,
                                        face(candidate.id).vertices);
        if(kind==TriangleContact::Intersecting||
           kind==TriangleContact::Ambiguous) {
            if(stats.reportedContacts>=limits.maxReportedContacts)
                throw std::length_error("Collision contact-count budget exhausted");
            ++stats.reportedContacts;
            found.push_back({key.first,key.second,
                             kind==TriangleContact::Ambiguous});
        }
    };
    // Preserve the original one-triangle-per-query path for all existing
    // callers. R07 may opt in to one BVH traversal per nearby face group.
    const auto group=std::max<std::size_t>(1,std::min<std::size_t>(groupedSeeds,32));
    for(std::size_t first=0;first<seeds.size();first+=group) {
        const auto last=std::min(first+group,seeds.size());
        Bounds bounds;
        for(std::size_t i=first;i<last;++i)
            mergeBounds(bounds,seeds[i].bounds);
        picking_->forEachOverlappingTriangle(bounds,[&](const Triangle& candidate) {
            for(std::size_t i=first;i<last;++i)
                evaluate(seeds[i],candidate);
        },stats,limits);
    }
    std::sort(found.begin(),found.end());
    return found;
}


std::vector<std::pair<FaceId,FaceId>>
RegionTopologyIndex::nearbyTrianglePairs(
    const std::vector<VertexId>& changedVertices,
    const std::vector<FaceId>& changedFaces,
    double localEdgeRatio,CollisionStatistics* measured,
    const CollisionQueryLimits& limits) const {
    if(!std::isfinite(localEdgeRatio)||localEdgeRatio<=0||
       localEdgeRatio>1)
        throw std::invalid_argument("V18 proximity requires a finite local edge ratio in (0,1]");
    CollisionStatistics local{};
    auto& stats=measured?*measured:local;
    stats={};
    std::set<FaceId> seeds;
    const auto addSeed=[&](FaceId id) {
        if(seeds.contains(id))return;
        if(seeds.size()>=limits.maxSeedFaces)
            throw std::length_error("V18 proximity seed-face budget exhausted");
        seeds.insert(id);
        stats.seedFaces=seeds.size();
    };
    for(const auto id:changedVertices) {
        const auto v=maybeVertex(id);
        if(!v) {
            if(retiredVertex(id))continue;
            throw std::out_of_range("Unknown V18 proximity vertex ID");
        }
        for(const auto face:v->incidentFaces)addSeed(face);
    }
    for(const auto id:changedFaces) {
        if(retiredFace(id))continue;
        (void)face(id);
        addSeed(id);
    }
    std::set<std::pair<FaceId,FaceId>> result;
    for(const auto id:seeds) {
        const auto record=face(id);
        std::array<Vec3,3> positions{};
        RevisionPickingIndex::Bounds bounds{};
        for(unsigned i=0;i<3;++i) {
            positions[i]=vertex(record.vertices[i]).position;
            if(!bounds.valid) {
                bounds.low=bounds.high=positions[i];
                bounds.valid=true;
            }else {
                const auto p=positions[i];
                bounds.low.x=std::min(bounds.low.x,p.x);
                bounds.low.y=std::min(bounds.low.y,p.y);
                bounds.low.z=std::min(bounds.low.z,p.z);
                bounds.high.x=std::max(bounds.high.x,p.x);
                bounds.high.y=std::max(bounds.high.y,p.y);
                bounds.high.z=std::max(bounds.high.z,p.z);
            }
        }
        double shortest=std::numeric_limits<double>::infinity();
        for(unsigned i=0;i<3;++i)
            shortest=std::min(shortest,length(
                positions[(i+1)%3]-positions[i]));
        if(!std::isfinite(shortest)||shortest<=0)
            throw std::logic_error("V18 proximity has degenerate source triangle");
        const double range=shortest*localEdgeRatio;
        if(!std::isfinite(range))
            throw std::length_error("V18 proximity radius overflow");
        bounds.low=bounds.low-Vec3{range,range,range};
        bounds.high=bounds.high+Vec3{range,range,range};
        if(!finite(bounds.low)||!finite(bounds.high))
            throw std::length_error("V18 proximity AABB overflow");
        picking_->forEachOverlappingTriangle(bounds,[&](
            const RevisionPickingIndex::Triangle& target) {
            if(id==target.id)return;
            const auto pair=std::minmax(id,target.id);
            if(result.contains({pair.first,pair.second}))return;
            if(stats.narrowPhaseTests>=limits.maxNarrowPhaseTests)
                throw std::length_error("V18 proximity pair-test budget exhausted");
            ++stats.narrowPhaseTests;
            // Shared stable vertices are topologically adjacent. M01's
            // independent contact predicate already checks their folding;
            // they must not produce an unavoidable zero clearance.
            const auto other=face(target.id);
            for(const auto a:record.vertices)
                for(const auto b:other.vertices)
                    if(a==b)return;
            if(stats.reportedContacts>=limits.maxReportedContacts)
                throw std::length_error("V18 proximity near-pair budget exhausted");
            result.emplace(pair.first,pair.second);
            ++stats.reportedContacts;
        },stats,limits);
    }
    return {result.begin(),result.end()};
}


IndexedMeshRepository::IndexedMeshRepository(
    std::shared_ptr<const RegionTopologyIndex> initial)
    :published_(std::move(initial)) {
    if(!published_.load())
        throw std::invalid_argument("Indexed repository needs an initial state");
}
std::shared_ptr<const RegionTopologyIndex>
IndexedMeshRepository::snapshot() const noexcept {
    return published_.load(std::memory_order_acquire);
}
bool IndexedMeshRepository::publishIfCurrent(
    const std::shared_ptr<const RegionTopologyIndex>& expected,
    std::shared_ptr<const RegionTopologyIndex> next) {
    if(!expected||!next)
        throw std::invalid_argument("Indexed publication needs two states");
    auto observed=expected;
    return published_.compare_exchange_strong(observed,std::move(next),
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire);
}

} // namespace mesher
