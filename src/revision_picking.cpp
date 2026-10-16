#include "mesher/revision_picking.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
using Bounds=RevisionPickingIndex::Bounds;
using Triangle=RevisionPickingIndex::Triangle;
using Partition=RevisionPickingIndex::Partition;
using Node=RevisionPickingIndex::Node;
void extend(Bounds& bounds,Vec3 p) {
    if(!bounds.valid) {bounds.low=bounds.high=p;bounds.valid=true;return;}
    bounds.low.x=std::min(bounds.low.x,p.x);
    bounds.low.y=std::min(bounds.low.y,p.y);
    bounds.low.z=std::min(bounds.low.z,p.z);
    bounds.high.x=std::max(bounds.high.x,p.x);
    bounds.high.y=std::max(bounds.high.y,p.y);
    bounds.high.z=std::max(bounds.high.z,p.z);
}
void extend(Bounds& bounds,const Bounds& other) {
    if(!other.valid)return;
    extend(bounds,other.low);
    extend(bounds,other.high);
}
double coordinate(Vec3 p,unsigned axis) {
    return axis==0?p.x:axis==1?p.y:p.z;
}
unsigned longestAxis(const Bounds& b) {
    const Vec3 span=b.high-b.low;
    if(span.x>=span.y&&span.x>=span.z)return 0;
    return span.y>=span.z?1:2;
}
bool rayBounds(const Bounds& box,Ray ray,double maximum) {
    if(!box.valid)return false;
    double low=0,high=maximum;
    for(unsigned k=0;k<3;++k) {
        const double origin=coordinate(ray.origin,k);
        const double direction=coordinate(ray.direction,k);
        const double a=coordinate(box.low,k),b=coordinate(box.high,k);
        // Expand slightly so barycentric acceptance tolerance at triangle
        // boundaries can never be eliminated by the box culling stage.
        const double epsilon=1e-10*(1+std::max(std::abs(a),std::abs(b)));
        if(std::abs(direction)<1e-300) {
            if(origin<a-epsilon||origin>b+epsilon)return false;
            continue;
        }
        const double x=(a-epsilon-origin)/direction;
        const double y=(b+epsilon-origin)/direction;
        low=std::max(low,std::min(x,y));
        high=std::min(high,std::max(x,y));
        if(high<low)return false;
    }
    return high>=0;
}
void intersect(Triangle t,Ray ray,double& best,
               RegionTopologyIndex::PickStatistics& stats) {
    ++stats.trianglesTested;
    const auto& [a,b,c]=t.vertices;
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
    if(distance>1e-8&&distance<best)best=distance;
}
std::int32_t createNode(Partition& part,std::size_t start,std::size_t end) {
    const auto id=static_cast<std::int32_t>(part.nodes.size());
    part.nodes.push_back({});
    Bounds bounds,centers;
    for(std::size_t i=start;i<end;++i) {
        extend(bounds,part.triangles[i].bounds);
        extend(centers,part.triangles[i].center);
    }
    part.nodes[static_cast<std::size_t>(id)].bounds=bounds;
    if(end-start<=4) {
        auto& leaf=part.nodes[static_cast<std::size_t>(id)];
        leaf.begin=start;leaf.count=end-start;
        return id;
    }
    const unsigned axis=longestAxis(centers);
    const auto mid=start+(end-start)/2;
    std::nth_element(part.triangles.begin()+static_cast<std::ptrdiff_t>(start),
                     part.triangles.begin()+static_cast<std::ptrdiff_t>(mid),
                     part.triangles.begin()+static_cast<std::ptrdiff_t>(end),
                     [axis](const Triangle& a,const Triangle& b) {
        const auto ac=coordinate(a.center,axis),bc=coordinate(b.center,axis);
        return ac==bc?a.id<b.id:ac<bc;
    });
    const auto left=createNode(part,start,mid);
    const auto right=createNode(part,mid,end);
    part.nodes[static_cast<std::size_t>(id)].left=left;
    part.nodes[static_cast<std::size_t>(id)].right=right;
    return id;
}
bool overlaps(const Bounds& a,const Bounds& b) {
    if(!a.valid||!b.valid)return false;
    const auto epsilon=[](double v,double w) {
        return 64*std::numeric_limits<double>::epsilon()*
               std::max({1.0,std::abs(v),std::abs(w)});
    };
    return a.low.x<=b.high.x+epsilon(a.low.x,b.high.x)&&
           b.low.x<=a.high.x+epsilon(b.low.x,a.high.x)&&
           a.low.y<=b.high.y+epsilon(a.low.y,b.high.y)&&
           b.low.y<=a.high.y+epsilon(b.low.y,a.high.y)&&
           a.low.z<=b.high.z+epsilon(a.low.z,b.high.z)&&
           b.low.z<=a.high.z+epsilon(b.low.z,a.high.z);
}
void queryPartition(const Partition& part,const Bounds& bounds,
                    const std::function<void(const Triangle&)>& visitor,
                    RegionTopologyIndex::CollisionStatistics& stats) {
    ++stats.partitionsTested;
    std::vector<std::int32_t> stack{0};
    while(!stack.empty()) {
        const auto id=stack.back();stack.pop_back();
        const auto& node=part.nodes[static_cast<std::size_t>(id)];
        ++stats.boxesTested;
        if(!overlaps(node.bounds,bounds))continue;
        if(node.count) {
            for(std::size_t i=node.begin;i<node.begin+node.count;++i) {
                const auto& tri=part.triangles[i];
                if(overlaps(tri.bounds,bounds)) {
                    ++stats.candidateTriangles;
                    visitor(tri);
                }
            }
        }else {
            if(node.left>=0)stack.push_back(node.left);
            if(node.right>=0)stack.push_back(node.right);
        }
    }
}
void queryDirectory(
    const std::shared_ptr<const RevisionPickingIndex::Directory>& directory,
    const Bounds& bounds,const std::function<void(const Triangle&)>& visitor,
    RegionTopologyIndex::CollisionStatistics& stats) {
    if(!directory)return;
    ++stats.boxesTested;
    if(!overlaps(directory->bounds,bounds))return;
    if(directory->partition)queryPartition(*directory->partition,bounds,visitor,stats);
    for(const auto& child:directory->children)
        if(child)queryDirectory(child,bounds,visitor,stats);
}
// Ray crossing signs encode the *integer* signed winding of a closed,
// consistently oriented immersed mesh. Unlike nearest-hit picking, every
// strictly interior positive ray hit contributes to the total.
void crossingPartition(const Partition& part,Ray ray,
                      RegionTopologyIndex::WindingStatistics& stats,
                      int& winding,bool& ambiguous) {
    ++stats.partitionsTested;
    std::vector<std::int32_t> stack{0};
    while(!stack.empty()) {
        const auto id=stack.back();stack.pop_back();
        const auto& node=part.nodes[static_cast<std::size_t>(id)];
        ++stats.boxesTested;
        if(!rayBounds(node.bounds,ray,
                      std::numeric_limits<double>::infinity()))continue;
        if(node.count==0) {
            if(node.left>=0)stack.push_back(node.left);
            if(node.right>=0)stack.push_back(node.right);
            continue;
        }
        for(std::size_t i=node.begin;i<node.begin+node.count;++i) {
            const auto& tri=part.triangles[i];
            if(!rayBounds(tri.bounds,ray,
                          std::numeric_limits<double>::infinity()))continue;
            ++stats.trianglesTested;
            const auto& [a,b,c]=tri.vertices;
            const auto ab=b-a,ac=c-a;
            const auto normal=cross(ab,ac);
            const auto determinant=dot(ab,cross(ray.direction,ac));
            const double threshold=1e-13*length(normal);
            if(std::abs(determinant)<=threshold) {
                // Parallel rays lying in the face plane are ill-defined.
                if(std::abs(dot(ray.origin-a,normal))<=
                   1e-12*length(normal))ambiguous=true;
                continue;
            }
            const auto p=ray.origin-a;
            const double u=dot(p,cross(ray.direction,ac))/determinant;
            const double v=dot(ray.direction,cross(p,ab))/determinant;
            const double t=dot(ac,cross(p,ab))/determinant;
            constexpr double margin=1e-11;
            if(u< -margin||v< -margin||u+v>1+margin||t< -margin)
                continue;
            // Grazing any triangle boundary or starting on a face cannot
            // safely be counted independently of adjacent face hits.
            if(t<=margin||u<=margin||v<=margin||
               1-u-v<=margin) {
                ambiguous=true;
                continue;
            }
            winding+=determinant<0?1:-1;
        }
    }
}
void crossingDirectory(
    const std::shared_ptr<const RevisionPickingIndex::Directory>& directory,
    Ray ray,RegionTopologyIndex::WindingStatistics& stats,
    int& winding,bool& ambiguous) {
    if(!directory)return;
    ++stats.boxesTested;
    if(!rayBounds(directory->bounds,ray,
                  std::numeric_limits<double>::infinity()))return;
    if(directory->partition)
        crossingPartition(*directory->partition,ray,stats,winding,ambiguous);
    for(const auto& child:directory->children)
        if(child)crossingDirectory(child,ray,stats,winding,ambiguous);
}
void hitPartition(const Partition& part,Ray ray,double& best,
                  RegionTopologyIndex::PickStatistics& stats) {
    ++stats.partitionsTested;
    std::vector<std::int32_t> stack{0};
    while(!stack.empty()) {
        const auto i=stack.back();stack.pop_back();
        const auto& node=part.nodes[static_cast<std::size_t>(i)];
        ++stats.boxesTested;
        if(!rayBounds(node.bounds,ray,best))continue;
        if(node.count) {
            for(std::size_t j=node.begin;j<node.begin+node.count;++j)
                intersect(part.triangles[j],ray,best,stats);
        }else {
            if(node.left>=0)stack.push_back(node.left);
            if(node.right>=0)stack.push_back(node.right);
        }
    }
}
void hitDirectory(const std::shared_ptr<const RevisionPickingIndex::Directory>& directory,
                  Ray ray,double& best,
                  RegionTopologyIndex::PickStatistics& stats) {
    if(!directory)return;
    ++stats.boxesTested;
    if(!rayBounds(directory->bounds,ray,best))return;
    if(directory->partition)hitPartition(*directory->partition,ray,best,stats);
    for(const auto& child:directory->children)
        if(child)hitDirectory(child,ray,best,stats);
}
}
std::shared_ptr<const RevisionPickingIndex::Partition>
RevisionPickingIndex::buildPartition(
    const RegionTopologyIndex& index,PartitionId owner) {
    const auto storage=index.revision().partition(owner);
    if(!storage||storage->faces.empty())return nullptr;
    auto result=std::make_shared<Partition>();
    result->triangles.reserve(storage->faces.size());
    for(const auto& face:storage->faces) {
        Triangle triangle;
        triangle.id=face.id;
        for(unsigned i=0;i<3;++i) {
            triangle.vertices[i]=index.vertex(face.vertices[i]).position;
            extend(triangle.bounds,triangle.vertices[i]);
        }
        triangle.center=(triangle.vertices[0]+triangle.vertices[1]+
                         triangle.vertices[2])/3.0;
        extend(result->bounds,triangle.bounds);
        result->triangles.push_back(std::move(triangle));
    }
    createNode(*result,0,result->triangles.size());
    return result;
}
std::shared_ptr<const RevisionPickingIndex::Directory>
RevisionPickingIndex::replace(
    const std::shared_ptr<const Directory>& previous,
    PartitionId id,unsigned depth,
    std::shared_ptr<const Partition> newPartition,std::size_t& copied) {
    auto current=std::make_shared<Directory>();
    if(previous)*current=*previous;
    if(depth==8)current->partition=std::move(newPartition);
    else {
        const auto shift=28u-4u*depth;
        const auto branch=static_cast<std::size_t>((id>>shift)&15u);
        current->children[branch]=replace(
            previous?previous->children[branch]:nullptr,id,depth+1,
            std::move(newPartition),copied);
    }
    current->bounds={};
    if(current->partition)extend(current->bounds,current->partition->bounds);
    for(const auto& child:current->children)
        if(child)extend(current->bounds,child->bounds);
    ++copied;
    // Remove empty branches, including empty overflow partitions. Their
    // persistent old nodes remain alive in earlier immutable revisions.
    if(!current->bounds.valid)return nullptr;
    return current;
}
std::shared_ptr<const RevisionPickingIndex> RevisionPickingIndex::build(
    const RegionTopologyIndex& index) {
    auto result=std::make_shared<RevisionPickingIndex>();
    std::size_t copied=0;
    index.revision().forEachPartition(
        [&](PartitionId id,const MeshPartition& partition) {
        if(!partition.faces.empty())
            result->root_=replace(result->root_,id,0,
                                  buildPartition(index,id),copied);
    });
    return result;
}
std::shared_ptr<const RevisionPickingIndex> RevisionPickingIndex::advance(
    const RevisionPickingIndex& old,const RegionTopologyIndex& before,
    const RegionTopologyIndex& after,
    const std::unordered_map<VertexId,std::optional<Vec3>>& vertexChanges,
    const std::unordered_map<FaceId,
        std::optional<std::array<VertexId,3>>>& faceChanges,
    std::size_t& touchedPartitions,std::size_t& copiedNodes) {
    auto result=std::make_shared<RevisionPickingIndex>();
    result->root_=old.root_;
    std::set<PartitionId> dirty;
    for(const auto* index:{&before,&after}) {
        for(const auto& [id,_]:faceChanges) {
            if(index->retiredFace(id))continue;
            try{dirty.insert(index->face(id).owner);}
            catch(const std::out_of_range&) {}
        }
        for(const auto& [id,_]:vertexChanges) {
            const auto v=index->maybeVertex(id);
            if(!v)continue;
            for(const auto face:v->incidentFaces)
                dirty.insert(index->face(face).owner);
        }
    }
    touchedPartitions=dirty.size();
    for(const auto owner:dirty)
        result->root_=replace(result->root_,owner,0,
                              buildPartition(after,owner),copiedNodes);
    return result;
}
void RevisionPickingIndex::forEachOverlappingTriangle(
    const Bounds& bounds,const std::function<void(const Triangle&)>& visitor,
    RegionTopologyIndex::CollisionStatistics& stats) const {
    queryDirectory(root_,bounds,visitor,stats);
}
int RevisionPickingIndex::signedRayCrossings(
    Ray ray,RegionTopologyIndex::WindingStatistics& stats,
    bool& ambiguous) const {
    int winding=0;
    ambiguous=false;
    ++stats.raysTested;
    crossingDirectory(root_,ray,stats,winding,ambiguous);
    return winding;
}
std::optional<Vec3> RevisionPickingIndex::firstRayHit(
    Ray ray,RegionTopologyIndex::PickStatistics* measured) const {
    RegionTopologyIndex::PickStatistics local;
    auto& stats=measured?*measured:local;
    stats={};
    double best=std::numeric_limits<double>::infinity();
    hitDirectory(root_,ray,best,stats);
    if(!std::isfinite(best))return std::nullopt;
    return ray.origin+ray.direction*best;
}
} // namespace mesher
