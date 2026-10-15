#include "mesher/region_edit.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
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
    :revision_(std::move(revision)),cellSize_(cellSize) {}
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
        }else result->faces_.erase(id,&copied);
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
        const long double d=x*x+y*y+z*z;
        const auto r=static_cast<long double>(radius);
        if(d<r*r)hits.push_back(id);
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

std::optional<Vec3> RegionTopologyIndex::firstRayHit(Ray ray) const {
    if(!finite(ray.origin)||!finite(ray.direction)||
       length(ray.direction)<1e-12)
        throw std::invalid_argument("Invalid indexed picking ray");
    double nearest=std::numeric_limits<double>::infinity();
    faces_.forEach([&](FaceId,const Face& f) {
        const auto a=vertex(f.vertices[0]).position;
        const auto b=vertex(f.vertices[1]).position;
        const auto c=vertex(f.vertices[2]).position;
        const auto ab=b-a,ac=c-a;
        const auto p=cross(ray.direction,ac);
        const double determinant=dot(ab,p);
        if(std::abs(determinant)<1e-13)return;
        const double inverse=1/determinant;
        const auto displacement=ray.origin-a;
        const double u=dot(displacement,p)*inverse;
        if(u<-1e-12||u>1.0+1e-12)return;
        const auto q=cross(displacement,ab);
        const double v=dot(ray.direction,q)*inverse;
        if(v<-1e-12||u+v>1.0+1e-12)return;
        const double distance=dot(ac,q)*inverse;
        if(distance>1e-8 && distance<nearest)nearest=distance;
    });
    if(!std::isfinite(nearest))return std::nullopt;
    return ray.origin+ray.direction*nearest;
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
