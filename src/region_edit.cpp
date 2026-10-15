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
    setFace(first,replacement);
    setFace(second,reflected);
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
    if(committed_)throw std::logic_error("Region was already committed");
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
