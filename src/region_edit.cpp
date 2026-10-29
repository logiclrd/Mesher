#include "mesher/region_edit.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <stdexcept>
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
template<typename T>
std::vector<T> ordered(const std::unordered_set<T>& set) {
    std::vector<T> result(set.begin(),set.end());
    std::sort(result.begin(),result.end());
    return result;
}
bool cyclic(std::array<VertexId,3> a,std::array<VertexId,3> b) {
    for(unsigned offset=0;offset<3;++offset)
        if(a[0]==b[offset]&&a[1]==b[(offset+1)%3]&&
           a[2]==b[(offset+2)%3])return true;
    return false;
}
void requireTriangle(std::array<VertexId,3> v) {
    if(v[0]==v[1]||v[1]==v[2]||v[0]==v[2])
        throw std::logic_error("A face needs three distinct vertices");
}
} // namespace

RegionEdit::RegionEdit(
    std::shared_ptr<const RegionTopologyIndex> index,
    std::vector<VertexId> seeds,
    std::vector<std::pair<VertexId,VertexId>> mirrors,
    std::size_t maximumElementsPerPartition)
    :index_(std::move(index)),
     view_((index_ ? index_->revision_ : MeshRevision::empty())->createMutableView()),
     maximumElementsPerPartition_(maximumElementsPerPartition) {
    if(maximumElementsPerPartition_==0)
        throw std::invalid_argument("Partition capacity must be positive");
    if(!index_ || seeds.empty())
        throw std::invalid_argument("Region requires an index and at least one seed");
    for(const auto& [a,b]:mirrors) {
        (void)index_->vertex(a);
        (void)index_->vertex(b);
        const auto [first,inserted]=mirror_.emplace(a,b);
        if(!inserted&&first->second!=b)
            throw std::invalid_argument("Conflicting mirror mapping");
        const auto [second,insertedOther]=mirror_.emplace(b,a);
        if(!insertedOther&&second->second!=a)
            throw std::invalid_argument("Mirror mapping is not an involution");
        if(a==b&&index_->vertex(a).position.x!=0)
            throw std::invalid_argument("Self-mirrored seam vertex must lie on X=0");
        if(a!=b) {
            const auto p=index_->vertex(a).position;
            const auto q=index_->vertex(b).position;
            if(p.x!=-q.x||p.y!=q.y||p.z!=q.z)
                throw std::invalid_argument("Mirror pair must be geometrically reflected");
        }
    }
    for(const auto id:seeds) {
        (void)index_->vertex(id);
        selected_.insert(id);
        if(!mirror_.empty()) {
            const auto it=mirror_.find(id);
            if(it==mirror_.end())
                throw std::invalid_argument("Every symmetric seed needs a mirror");
            selected_.insert(it->second);
        }
    }
    // The editable face set contains ALL incident faces of the selected
    // vertices, including faces owned by other partitions.
    for(const auto id:selected_) {
        const auto& vertex=index_->vertex(id);
        writers_.insert(vertex.owner);
        readers_.insert(vertex.owner);
        accessibleVertices_.insert(id);
        for(const auto face:vertex.incidentFaces)faces_.insert(face);
    }
    for(const auto id:faces_) {
        const auto& face=index_->face(id);
        writers_.insert(face.owner);
        readers_.insert(face.owner);
        for(const auto v:face.vertices) {
            accessibleVertices_.insert(v);
            readers_.insert(index_->vertex(v).owner);
        }
        // Read the opposing faces along each boundary edge. Only an actual
        // topology operation may later require editing additional partitions;
        // the selected write closure remains fixed and must be respected.
        for(unsigned i=0;i<3;++i) {
            const auto key=edge(face.vertices[i],face.vertices[(i+1)%3]);
            for(const auto adjacent:index_->edgeSides(key)) {
                const auto& other=index_->face(adjacent.face);
                readers_.insert(other.owner);
                for(const auto v:other.vertices) {
                    accessibleVertices_.insert(v);
                    readers_.insert(index_->vertex(v).owner);
                }
            }
        }
    }
    footprint_.editableVertices=ordered(selected_);
    footprint_.editableFaces=ordered(faces_);
    footprint_.writePartitions=ordered(writers_);
    footprint_.readPartitions=ordered(readers_);
    // Eagerly clone the complete write set BEFORE the first mutation. The
    // render thread still holds the unmodified immutable base revision.
    for(const auto id:footprint_.writePartitions) {
        const auto& partition=view_.editPartition(id);
        if(partition.vertices.size()>maximumElementsPerPartition_ ||
           partition.faces.size()>maximumElementsPerPartition_)
            throw std::length_error("Writable partition already exceeds capacity");
    }
}

void RegionEdit::ensureWritable(PartitionId id) const {
    if(committed_ || !writers_.contains(id))
        throw std::logic_error("Partition not writable in this region");
}
PartitionId RegionEdit::chooseInsertionPartition(PartitionId preferred,
                                                 bool vertex) {
    ensureWritable(preferred);
    const auto available=[&](PartitionId owner) {
        const auto& partition=view_.editPartition(owner);
        return (vertex?partition.vertices.size():partition.faces.size())<
               maximumElementsPerPartition_;
    };
    if(available(preferred))return preferred;
    for(const auto owner:footprint_.writePartitions)
        if(available(owner))return owner;
    const auto fresh=index_->revision().allocateOverflowPartition();
    if(index_->revision().partition(fresh))
        throw std::logic_error("Allocated overflow ID collides with existing mesh");
    (void)view_.editPartition(fresh);
    writers_.insert(fresh);
    footprint_.writePartitions.push_back(fresh);
    return fresh;
}
VertexId RegionEdit::appendLocalVertex(PartitionId preferred,Vec3 position) {
    return addVertex(chooseInsertionPartition(preferred,true),position);
}
FaceId RegionEdit::appendLocalFace(
    PartitionId preferred,std::array<VertexId,3> vertices) {
    return addFace(chooseInsertionPartition(preferred,false),vertices);
}
void RegionEdit::ensureReferences(const std::array<VertexId,3>& ids) const {
    requireTriangle(ids);
    for(const auto id:ids) {
        if(!accessibleVertices_.contains(id) &&
           !newVertices_.contains(id))
            throw std::logic_error("Face vertex lies outside transaction read halo");
        const auto it=vertexChanges_.find(id);
        if(it!=vertexChanges_.end()&&!it->second)
            throw std::logic_error("Face references vertex scheduled for deletion");
    }
}
void RegionEdit::setPosition(VertexId id,Vec3 position) {
    if(!selected_.contains(id) || !finite(position))
        throw std::logic_error("Invalid vertex move outside selected region");
    const auto owner=index_->vertex(id).owner;
    ensureWritable(owner);
    auto& partition=view_.editPartition(owner);
    const auto it=std::find_if(partition.vertices.begin(),partition.vertices.end(),
                              [id](const auto& v){return v.id==id;});
    if(it==partition.vertices.end())
        throw std::logic_error("Selected vertex was removed");
    it->position=position;
    vertexChanges_[id]=position;
}
void RegionEdit::moveVertex(VertexId id,Vec3 position) {
    if(!selected_.contains(id))throw std::logic_error("Vertex not selected");
    if(!finite(position))throw std::invalid_argument("Non-finite position");
    const auto it=mirror_.find(id);
    if(!mirror_.empty()&&it==mirror_.end())
        throw std::logic_error("Symmetric edit lacks a mirror mapping");
    if(it!=mirror_.end()) {
        if(it->second==id) {
            if(position.x!=0)
                throw std::logic_error("Seam vertex must remain on X=0");
        }else {
            // Both ownership locations were selected and cloned at creation.
            setPosition(it->second,{-position.x,position.y,position.z});
        }
    }
    setPosition(id,position);
}
void RegionEdit::setVertexColor(VertexId id,VertexRGBA color) {
    if(!selected_.contains(id)&&!newVertices_.contains(id))
        throw std::logic_error("Cannot recolor vertex outside edit write halo");
    const auto owner=newVertices_.contains(id)?newVertices_.at(id):
        index_->vertex(id).owner;
    ensureWritable(owner);
    const auto valid=[](float x){return std::isfinite(x)&&x>=0&&x<=1;};
    if(!valid(color.r)||!valid(color.g)||!valid(color.b)||!valid(color.a))
        throw std::invalid_argument("Invalid remeshed vertex RGBA");
    view_.setVertexColor(owner,id,color);
}
VertexId RegionEdit::addVertex(PartitionId owner,Vec3 position) {
    if(!mirror_.empty())
        throw std::logic_error("Symmetric vertex insertion awaits paired topology tools");
    ensureWritable(owner);
    if(!finite(position))throw std::invalid_argument("Non-finite vertex position");
    if(view_.editPartition(owner).vertices.size()>=maximumElementsPerPartition_)
        throw std::length_error("Vertex partition is at capacity");
    const auto id=view_.addVertex(owner,position);
    newVertices_[id]=owner;
    accessibleVertices_.insert(id);
    vertexChanges_[id]=position;
    return id;
}
FaceId RegionEdit::addFace(PartitionId owner,std::array<VertexId,3> vertices) {
    if(!mirror_.empty())
        throw std::logic_error("Symmetric face insertion awaits paired topology tools");
    ensureWritable(owner);
    ensureReferences(vertices);
    if(view_.editPartition(owner).faces.size()>=maximumElementsPerPartition_)
        throw std::length_error("Face partition is at capacity");
    const auto id=view_.addFace(owner,vertices);
    newFaces_[id]=owner;
    faceChanges_[id]=vertices;
    return id;
}
void RegionEdit::setFace(FaceId id,
                         std::optional<std::array<VertexId,3>> vertices) {
    if(!faces_.contains(id))
        throw std::logic_error("Face not in editable topological halo");
    const auto owner=index_->face(id).owner;
    ensureWritable(owner);
    if(vertices)ensureReferences(*vertices);
    auto& partition=view_.editPartition(owner);
    const auto it=std::find_if(partition.faces.begin(),partition.faces.end(),
                              [id](const auto& f){return f.id==id;});
    if(vertices) {
        if(it==partition.faces.end())
            throw std::logic_error("Face was already deleted");
        it->vertices=*vertices;
    }else {
        if(it==partition.faces.end())
            throw std::logic_error("Face was already deleted");
        partition.faces.erase(it);
    }
    faceChanges_[id]=vertices;
}
void RegionEdit::replaceFace(FaceId id,std::array<VertexId,3> vertices) {
    if(!mirror_.empty())
        throw std::logic_error("Use replaceMirroredFaces for symmetric topology");
    setFace(id,vertices);
}
void RegionEdit::removeFace(FaceId id) {
    if(!mirror_.empty())
        throw std::logic_error("Symmetric face removal awaits paired topology tools");
    setFace(id,std::nullopt);
}
void RegionEdit::removeVertex(VertexId id) {
    if(!mirror_.empty())
        throw std::logic_error("Symmetric vertex removal awaits paired topology tools");
    if(!selected_.contains(id))
        throw std::logic_error("Cannot remove vertex outside selected region");
    ensureWritable(index_->vertex(id).owner);
    auto& partition=view_.editPartition(index_->vertex(id).owner);
    const auto it=std::find_if(partition.vertices.begin(),partition.vertices.end(),
                              [id](const auto& v){return v.id==id;});
    if(it==partition.vertices.end())
        throw std::logic_error("Vertex was already deleted");
    partition.vertices.erase(it);
    vertexChanges_[id]=std::nullopt;
}
void RegionEdit::replaceMirroredFaces(
    FaceId first,FaceId second,std::array<VertexId,3> replacement) {
    if(mirror_.empty()||first==second)
        throw std::logic_error("Requires distinct mirrored faces and mappings");
    if(!faces_.contains(first)||!faces_.contains(second))
        throw std::logic_error("Both mirrored faces must be editable");
    const auto original=index_->face(first).vertices;
    const auto other=index_->face(second).vertices;
    auto mapped=[this](VertexId id)->VertexId {
        const auto it=mirror_.find(id);
        if(it==mirror_.end())
            throw std::logic_error("Incomplete symmetric vertex correspondence");
        return it->second;
    };
    const std::array<VertexId,3> reflectedOld={
        mapped(original[0]),mapped(original[2]),mapped(original[1])};
    if(!cyclic(reflectedOld,other))
        throw std::logic_error("Selected faces are not reflected counterparts");
    ensureReferences(replacement);
    const std::array<VertexId,3> reflected={
        mapped(replacement[0]),mapped(replacement[2]),mapped(replacement[1])};
    ensureReferences(reflected);
    try {
        setFace(first,replacement);
        setFace(second,reflected);
        faceMirror_.emplace(first,second);
        faceMirror_.emplace(second,first);
    }catch(...) {
        poisoned_=true;
        throw;
    }
}

VertexId RegionEdit::mirroredVertex(VertexId vertex) const {
    const auto it=mirror_.find(vertex);
    if(it==mirror_.end())
        throw std::logic_error("Vertex has no declared mirror correspondence");
    return it->second;
}
std::pair<VertexId,VertexId> RegionEdit::appendMirroredVertices(
    PartitionId firstOwner,PartitionId secondOwner,Vec3 position) {
    if(mirror_.empty()||!finite(position)||position.x==0)
        throw std::logic_error("Paired vertex insertion requires nonzero X and mirror context");
    try {
        const auto first=chooseInsertionPartition(firstOwner,true);
        const auto a=view_.addVertex(first,position);
        newVertices_[a]=first;
        vertexChanges_[a]=position;
        accessibleVertices_.insert(a);
        const auto other=Vec3{-position.x,position.y,position.z};
        const auto second=chooseInsertionPartition(secondOwner,true);
        const auto b=view_.addVertex(second,other);
        newVertices_[b]=second;
        vertexChanges_[b]=other;
        accessibleVertices_.insert(b);
        mirror_.emplace(a,b);
        mirror_.emplace(b,a);
        return {a,b};
    }catch(...) {
        poisoned_=true; // No half-paired topology can ever commit.
        throw;
    }
}
VertexId RegionEdit::appendSeamVertex(PartitionId owner,Vec3 position) {
    if(mirror_.empty()||!finite(position)||position.x!=0)
        throw std::logic_error("Shared symmetry seam vertex must be on X=0");
    try {
        const auto writable=chooseInsertionPartition(owner,true);
        const auto id=view_.addVertex(writable,position);
        newVertices_[id]=writable;
        vertexChanges_[id]=position;
        accessibleVertices_.insert(id);
        mirror_.emplace(id,id);
        return id;
    }catch(...) {
        poisoned_=true;
        throw;
    }
}
std::pair<FaceId,FaceId> RegionEdit::appendMirroredFaces(
    PartitionId firstOwner,PartitionId secondOwner,
    std::array<VertexId,3> firstTriangle) {
    if(mirror_.empty())
        throw std::logic_error("Paired face insertion requires mirror context");
    try {
        ensureReferences(firstTriangle);
        const std::array<VertexId,3> reflected={
            mirroredVertex(firstTriangle[0]),
            mirroredVertex(firstTriangle[2]),
            mirroredVertex(firstTriangle[1])};
        ensureReferences(reflected);
        // A triangle lying entirely on X=0 would create two identical
        // coincident sheets. Such faces are never a valid mirrored pair.
        if(firstTriangle==reflected ||
           (mirror_.at(firstTriangle[0])==firstTriangle[0]&&
            mirror_.at(firstTriangle[1])==firstTriangle[1]&&
            mirror_.at(firstTriangle[2])==firstTriangle[2]))
            throw std::logic_error("Cannot duplicate an all-seam face");
        const auto first=chooseInsertionPartition(firstOwner,false);
        const auto a=view_.addFace(first,firstTriangle);
        newFaces_[a]=first;
        faceChanges_[a]=firstTriangle;
        const auto second=chooseInsertionPartition(secondOwner,false);
        const auto b=view_.addFace(second,reflected);
        newFaces_[b]=second;
        faceChanges_[b]=reflected;
        faceMirror_.emplace(a,b);
        faceMirror_.emplace(b,a);
        return {a,b};
    }catch(...) {
        poisoned_=true;
        throw;
    }
}
std::pair<FaceId,FaceId> RegionEdit::splitSelfMirroredFace(
    FaceId id,VertexId from,VertexId to,VertexId midpoint) {
    if(mirror_.empty()||!faces_.contains(id)||faceChanges_.contains(id))
        throw std::logic_error("Unmodified self-reflected face must be writable");
    const auto& original=index_->face(id);
    const auto old=original.vertices;
    const auto mirror=[this](VertexId v)->VertexId {
        const auto it=mirror_.find(v);
        if(it==mirror_.end())
            throw std::logic_error("Self-reflected face has an unmapped vertex");
        return it->second;
    };
    if(from==to||mirror(from)!=to||mirror(to)!=from||
       mirror(midpoint)!=midpoint)
        throw std::logic_error("Crossing edge needs paired ends and one seam midpoint");
    VertexId third=0;
    bool oriented=false;
    for(unsigned i=0;i<3;++i)
        if(old[i]==from&&old[(i+1)%3]==to) {
            third=old[(i+2)%3];oriented=true;break;
        }
    if(!oriented||mirror(third)!=third)
        throw std::logic_error("Crossing edge does not belong to a self-reflected triangle");
    if(!cyclic({mirror(old[0]),mirror(old[2]),mirror(old[1])},old))
        throw std::logic_error("Face is not self-reflected with reversed winding");

    const auto a=index_->vertex(from).position;
    const auto b=index_->vertex(to).position;
    // A valid reflected pair has equal Y/Z, and its plane crossing is
    // EXACTLY (0,a.y,a.z). No snap-to-plane or midpoint drift.
    const auto seam=vertexChanges_.find(midpoint);
    if(seam==vertexChanges_.end()||!seam->second||
       !newVertices_.contains(midpoint)||a.x==0||
       a.x!=-b.x||a.y!=b.y||a.z!=b.z||
       seam->second->x!=0||
       seam->second->y!=a.y||seam->second->z!=a.z)
        throw std::logic_error("Self-reflected face split has no exact shared midpoint");
    try {
        const std::array<VertexId,3> first{from,midpoint,third};
        const std::array<VertexId,3> second{midpoint,to,third};
        ensureReferences(first);
        ensureReferences(second);
        // Keep the stable old face ID. The second half receives a fresh
        // ID; they are *now* distinct reverse-winding mirror partners.
        setFace(id,first);
        const auto owner=chooseInsertionPartition(original.owner,false);
        const auto newId=view_.addFace(owner,second);
        newFaces_[newId]=owner;
        faceChanges_[newId]=second;
        faceMirror_.emplace(id,newId);
        faceMirror_.emplace(newId,id);
        return {id,newId};
    }catch(...) {
        poisoned_=true;
        throw;
    }
}
std::vector<std::pair<FaceId,FaceId>> RegionEdit::replaceSelfMirroredFace(
    FaceId id,const std::vector<std::array<VertexId,3>>& children) {
    if(mirror_.empty()||!faces_.contains(id)||faceChanges_.contains(id)||
       children.size()<2)
        throw std::logic_error("Invariant source replacement needs complete paired fan");
    const auto old=index_->face(id).vertices;
    const std::array<VertexId,3> reflected{
        mirroredVertex(old[0]),mirroredVertex(old[2]),mirroredVertex(old[1])};
    if(!cyclic(reflected,old))
        throw std::logic_error("Changed original face is not an invariant reflected source");
    std::size_t fixed=0;
    for(const auto v:old)fixed+=mirroredVertex(v)==v;
    if(fixed!=1)
        throw std::logic_error("Invariant source fan must contain one old fixed-point vertex");
    std::vector<std::pair<std::size_t,std::size_t>> pairIndices;
    std::vector<bool> used(children.size());
    for(std::size_t i=0;i<children.size();++i) {
        if(used[i])continue;
        ensureReferences(children[i]);
        const auto tri=children[i];
        const std::array<VertexId,3> twin{
            mirroredVertex(tri[0]),mirroredVertex(tri[2]),mirroredVertex(tri[1])};
        if(cyclic(twin,tri)) {
            // A genuine invariant CHILD is a single topological
            // triangle, not two coincident faces. No self-cycle can
            // consist of three different on-plane vertices.
            std::size_t fixedCount=0;
            for(const auto v:tri)fixedCount+=mirroredVertex(v)==v;
            if(fixedCount!=1)
                throw std::logic_error(
                    "Self-reflected child has an invalid fixed-point fan");
            used[i]=true;
            pairIndices.emplace_back(i,i);
            continue;
        }
        std::optional<std::size_t> found;
        for(std::size_t j=i+1;j<children.size();++j) {
            if(used[j])continue;
            if(cyclic(twin,children[j])) {
                if(found)throw std::logic_error(
                    "Invariant fan has duplicate mirrored child triangles");
                found=j;
            }
        }
        if(!found)throw std::logic_error(
            "Invariant fan lacks an opposite-winding child");
        ensureReferences(children[*found]);
        used[i]=used[*found]=true;
        pairIndices.emplace_back(i,*found);
    }
    try {
        std::vector<std::pair<FaceId,FaceId>> result;
        result.reserve(pairIndices.size());
        const auto owner=index_->face(id).owner;
        for(std::size_t k=0;k<pairIndices.size();++k) {
            const auto [a,b]=pairIndices[k];
            const auto first=children[a],second=children[b];
            if(k==0) {
                setFace(id,first);
                if(a==b) {
                    faceMirror_.emplace(id,id);
                    result.emplace_back(id,id);
                }else {
                    const auto writable=chooseInsertionPartition(owner,false);
                    const auto mate=view_.addFace(writable,second);
                    newFaces_[mate]=writable;
                    faceChanges_[mate]=second;
                    faceMirror_.emplace(id,mate);
                    faceMirror_.emplace(mate,id);
                    result.emplace_back(id,mate);
                }
            }else if(a==b) {
                const auto writable=chooseInsertionPartition(owner,false);
                const auto self=view_.addFace(writable,first);
                newFaces_[self]=writable;
                faceChanges_[self]=first;
                faceMirror_.emplace(self,self);
                result.emplace_back(self,self);
            }else {
                const auto ids=appendMirroredFaces(owner,owner,first);
                result.push_back(ids);
            }
        }
        return result;
    }catch(...) {
        poisoned_=true;
        throw;
    }
}

void RegionEdit::removeMirroredFaces(FaceId a,FaceId b) {
    if(mirror_.empty()||a==b||!faces_.contains(a)||!faces_.contains(b))
        throw std::logic_error("Both paired faces must be editable");
    const auto original=index_->face(a).vertices;
    const auto paired=index_->face(b).vertices;
    if(!cyclic({mirroredVertex(original[0]),
                mirroredVertex(original[2]),
                mirroredVertex(original[1])},paired))
        throw std::logic_error("Removed faces are not reflected partners");
    try {
        setFace(a,std::nullopt);
        setFace(b,std::nullopt);
        faceMirror_.emplace(a,b);
        faceMirror_.emplace(b,a);
    }catch(...) {
        poisoned_=true;
        throw;
    }
}
void RegionEdit::removeMirroredVertices(VertexId a,VertexId b) {
    if(mirror_.empty()||mirroredVertex(a)!=b||
       mirroredVertex(b)!=a||!selected_.contains(a)||
       !selected_.contains(b))
        throw std::logic_error("Removed vertices are not a selected mirror pair");
    try {
        const auto remove=[&](VertexId id) {
            ensureWritable(index_->vertex(id).owner);
            auto& part=view_.editPartition(index_->vertex(id).owner);
            const auto it=std::find_if(part.vertices.begin(),part.vertices.end(),
                [id](const auto& v){return v.id==id;});
            if(it==part.vertices.end())
                throw std::logic_error("Mirrored vertex already removed");
            part.vertices.erase(it);
            vertexChanges_[id]=std::nullopt;
        };
        remove(a);
        if(b!=a)remove(b);
    }catch(...) {
        poisoned_=true;
        throw;
    }
}

void RegionEdit::validate() const {
    // Validate only local deltas against the immutable topology index.
    // No revision traversal and no global topology rebuild occur on commit.
    auto position=[&](VertexId id)->Vec3 {
        const auto changed=vertexChanges_.find(id);
        if(changed!=vertexChanges_.end()) {
            if(!changed->second)
                throw std::logic_error("Face references a removed vertex");
            return *changed->second;
        }
        return index_->vertex(id).position;
    };
    auto face=[&](FaceId id)->std::optional<std::array<VertexId,3>> {
        const auto changed=faceChanges_.find(id);
        if(changed!=faceChanges_.end())return changed->second;
        return index_->face(id).vertices;
    };
    std::unordered_set<FaceId> geometry;
    for(const auto& [id,_]:faceChanges_)geometry.insert(id);
    for(const auto& [id,_]:vertexChanges_)
        if(!newVertices_.contains(id))
            for(const auto incident:index_->vertex(id).incidentFaces)
                geometry.insert(incident);
    for(const auto id:geometry) {
        const auto f=face(id);
        if(!f)continue;
        ensureReferences(*f);
        const auto a=position((*f)[0]),b=position((*f)[1]),c=position((*f)[2]);
        if(!finite(a)||!finite(b)||!finite(c) ||
           !(length(cross(b-a,c-a))>0))
            throw std::logic_error("Local face has degenerate/non-finite geometry");
    }
    if(!mirror_.empty()) {
        // Validate that every changed triangle has a distinct reflected
        // partner and that all new/changed vertices remain paired.
        for(const auto& [id,_]:vertexChanges_) {
            const auto other=mirroredVertex(id);
            const auto mine=vertexChanges_.at(id);
            const auto theirs=vertexChanges_.find(other);
            if(theirs==vertexChanges_.end()||bool(mine)!=bool(theirs->second))
                throw std::logic_error("Asymmetric vertex update");
            if(mine) {
                const auto p=*mine,q=*theirs->second;
                if(p.x!=-q.x||p.y!=q.y||p.z!=q.z)
                    throw std::logic_error("Updated vertices are not reflected");
            }
        }
        for(const auto& [id,triangle]:faceChanges_) {
            const auto it=faceMirror_.find(id);
            if(it==faceMirror_.end())
                throw std::logic_error("Symmetric topology has an unpaired face update");
            const auto twin=faceChanges_.find(it->second);
            if(twin==faceChanges_.end()||bool(triangle)!=bool(twin->second))
                throw std::logic_error("Mirrored faces differ in existence");
            if(triangle) {
                const std::array<VertexId,3> reflected={
                    mirroredVertex((*triangle)[0]),
                    mirroredVertex((*triangle)[2]),
                    mirroredVertex((*triangle)[1])};
                if(!cyclic(reflected,*twin->second))
                    throw std::logic_error("Mirrored faces differ in winding");
            }
        }
    }
    if(faceChanges_.empty()&&vertexChanges_.empty())return;

    // Removing a vertex cannot leave even an unchanged face pointing at it.
    for(const auto& [id,pos]:vertexChanges_)if(!pos) {
        for(const auto adjacent:index_->vertex(id).incidentFaces) {
            const auto current=face(adjacent);
            if(current && (std::find(current->begin(),current->end(),id)!=
                           current->end()))
                throw std::logic_error("Removed vertex is still referenced");
        }
    }
    if(faceChanges_.empty())return;

    std::unordered_set<RegionTopologyIndex::Edge,
                       RegionTopologyIndex::EdgeHash> touchedEdges;
    std::unordered_set<VertexId> touchedVertices;
    auto record=[&](const std::array<VertexId,3>& f) {
        for(unsigned i=0;i<3;++i) {
            touchedVertices.insert(f[i]);
            touchedEdges.insert(edge(f[i],f[(i+1)%3]));
        }
    };
    for(const auto& [id,replacement]:faceChanges_) {
        if(!newFaces_.contains(id))record(index_->face(id).vertices);
        if(replacement)record(*replacement);
    }

    // The current sphere prototype requires closed, orientable two-manifold
    // connectivity on all modified edges. Unchanged edges are not traversed.
    for(const auto key:touchedEdges) {
        std::vector<RegionTopologyIndex::EdgeSide> sides;
        const auto original=index_->maybeEdgeSides(key);
        if(original)
            for(const auto side:*original)
                if(!faceChanges_.contains(side.face))sides.push_back(side);
        for(const auto& [id,replacement]:faceChanges_)if(replacement) {
            for(unsigned i=0;i<3;++i) {
                const auto a=(*replacement)[i],b=(*replacement)[(i+1)%3];
                if(edge(a,b)==key)sides.push_back({id,a==key.a});
            }
        }
        // An edge removed by a valid flip/collapse has zero remaining faces.
        // Every surviving or newly created edge must have exactly two faces
        // of opposing orientation.
        if(!sides.empty() &&
           (sides.size()!=2 || sides[0].forward==sides[1].forward))
            throw std::logic_error("Local topology would produce an invalid edge");
    }

    // Edge incidence alone permits two disjoint fans sharing one vertex.
    // Verify each touched vertex has a single cyclic one-ring link.
    for(const auto id:touchedVertices) {
        std::vector<FaceId> incident;
        const auto old=index_->maybeVertex(id);
        if(old)
            for(const auto f:old->incidentFaces)
                if(!faceChanges_.contains(f))incident.push_back(f);
        for(const auto& [f,replacement]:faceChanges_)if(replacement)
            if(std::find(replacement->begin(),replacement->end(),id)!=
               replacement->end())incident.push_back(f);
        if(incident.empty())continue; // An orphan/removed vertex is permitted.
        std::unordered_map<VertexId,std::unordered_set<VertexId>> link;
        for(const auto f:incident) {
            const auto vertices=face(f);
            if(!vertices)throw std::logic_error("Deleted face in fan");
            std::array<VertexId,2> neighbors{};
            unsigned count=0;
            for(const auto v:*vertices)if(v!=id)neighbors[count++]=v;
            if(count!=2)throw std::logic_error("Invalid vertex link");
            link[neighbors[0]].insert(neighbors[1]);
            link[neighbors[1]].insert(neighbors[0]);
        }
        for(const auto& [_,neighbors]:link)
            if(neighbors.size()!=2)
                throw std::logic_error("Vertex link has an open/nonmanifold fan");
        std::unordered_set<VertexId> visited;
        std::deque<VertexId> pending{link.begin()->first};
        while(!pending.empty()) {
            const auto current=pending.front();
            pending.pop_front();
            if(!visited.insert(current).second)continue;
            for(const auto neighbor:link.at(current))pending.push_back(neighbor);
        }
        if(visited.size()!=link.size() || incident.size()!=link.size())
            throw std::logic_error("Vertex link has disconnected fans");
    }
}

RevisionCommit RegionEdit::commit() {
    if(committed_||poisoned_)
        throw std::logic_error("Region was committed or an atomic mirrored operation failed");
    validate();
    auto result=view_.commit();
    auto nextIndex=RegionTopologyIndex::advance(
        *index_,result.revision,vertexChanges_,faceChanges_,
        newVertices_,newFaces_);
    updatedIndex_=std::move(nextIndex);
    committed_=true;
    return result;
}

} // namespace mesher
