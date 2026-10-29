#include "mesher/vertex_paint.hpp"
#include "mesher/partition_render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace mesher {
namespace {
bool validColor(VertexRGBA c) {
    return std::isfinite(c.r)&&c.r>=0&&c.r<=1&&
           std::isfinite(c.g)&&c.g>=0&&c.g<=1&&
           std::isfinite(c.b)&&c.b>=0&&c.b<=1&&
           std::isfinite(c.a)&&c.a>=0&&c.a<=1;
}
VertexRGBA paintOver(VertexRGBA previous,VertexRGBA brush,float influence) {
    // Brush alpha is deposit opacity, not the destination alpha value.
    // Compose source-over in linear, unpremultiplied RGBA. This preserves
    // the existing coat under translucent brush dabs and keeps opaque
    // painted/unpainted surfaces opaque.
    const float opacity=brush.a*influence;
    if(opacity<=0.f)return previous;
    if(opacity>=1.f)return {brush.r,brush.g,brush.b,1.f};
    const float retained=previous.a*(1.f-opacity);
    const float alpha=opacity+retained;
    if(alpha<=0.f)return previous;
    return {(brush.r*opacity+previous.r*retained)/alpha,
            (brush.g*opacity+previous.g*retained)/alpha,
            (brush.b*opacity+previous.b*retained)/alpha,alpha};
}
VertexRGBA localColorAt(const RegionTopologyIndex& index,VertexId id) {
    const auto vertex=index.vertex(id);
    const auto storage=index.revision().partition(vertex.owner);
    if(!storage)throw std::logic_error("Paint vertex owner missing");
    for(const auto& v:storage->vertices)if(v.id==id)return v.color;
    throw std::logic_error("Paint vertex missing");
}
Vec3 normalAt(const RegionTopologyIndex& index,VertexId id) {
    Vec3 sum{};
    for(const auto face:index.vertex(id).incidentFaces) {
        const auto f=index.face(face).vertices;
        const Vec3 a=index.vertex(f[0]).position;
        const Vec3 b=index.vertex(f[1]).position;
        const Vec3 c=index.vertex(f[2]).position;
        sum=sum+cross(b-a,c-a);
    }
    return length(sum)>1e-12?normalized(sum):Vec3{};
}
float weight(double distance,double closest,const PaintDab& dab) {
    // The visibility ray begins above the mesh, but brush coverage lives
    // on the surface. Normalize the first sampled visible vertex to the
    // centre, so small dabs do not have a permanent weak central hole.
    // The outer radius stays exactly the requested radius.
    const double span=dab.radius-closest;
    const double t=span>1e-12?
        std::clamp((distance-closest)/span,0.0,1.0):0.0;
    const double hardness=std::clamp(dab.hardness,0.0,1.0);
    if(t<=hardness||hardness>=1.0)return 1.f;
    return static_cast<float>(1.0-(t-hardness)/(1.0-hardness));
}
struct VisiblePaintSamples {
    std::vector<std::pair<VertexId,double>> vertices;
    double fullStrengthDistance{};
};
VisiblePaintSamples collectVisiblePaintSamples(
    const RegionTopologyIndex& source,const PaintDab& dab,bool preview,
    std::size_t& candidates,std::size_t& triangleTests,
    std::size_t maxVertices,std::size_t maxRayTriangles) {
        if(!std::isfinite(dab.point.x)||!std::isfinite(dab.point.y)||
           !std::isfinite(dab.point.z)||!std::isfinite(dab.radius)||
           dab.radius<=0||!std::isfinite(dab.nominalEdgeLength)||
           dab.nominalEdgeLength<=0||!std::isfinite(dab.hardness)||
           dab.hardness<0||dab.hardness>1||!validColor(dab.rgba)||
           !std::isfinite(dab.outwardNormal.x)||
           !std::isfinite(dab.outwardNormal.y)||
           !std::isfinite(dab.outwardNormal.z)||
           length(dab.outwardNormal)<1e-12)
            throw std::invalid_argument("Paint dab contains invalid parameters");
        const Vec3 outward=normalized(dab.outwardNormal);
        // A fixed quarter-edge ray lift cuts off a large brush at the
        // sphere's near-tangent horizon. Widen the visibility cone with
        // the selected radius; the ray still originates outside the
        // surface and every target remains subject to self/other-sheet
        // occlusion and backface tests.
        const double lift=std::max(dab.nominalEdgeLength*0.25,dab.radius);
        const Vec3 origin=dab.point+outward*lift;
        // Alpha zero is a no-op (including for history), even at full
        // hardness. Paint radius is measured from the SURFACE hit.
        if(!preview&&dab.rgba.a==0.f)return {};
        const auto ids=source.querySphere(dab.point,dab.radius);
        if(ids.size()>maxVertices-candidates)
            throw std::length_error("Paint local vertex budget exhausted");
        candidates+=ids.size();
        std::vector<std::pair<VertexId,double>> visible;
        visible.reserve(ids.size());
        double closest=dab.radius;
        std::array<double,3> nearest{dab.radius,dab.radius,dab.radius};
        const auto addCenterSample=[&](double distance) {
            closest=std::min(closest,distance);
            if(distance<nearest[0]) {
                nearest[2]=nearest[1];nearest[1]=nearest[0];
                nearest[0]=distance;
            }else if(distance<nearest[1]) {
                nearest[2]=nearest[1];nearest[1]=distance;
            }else if(distance<nearest[2])nearest[2]=distance;
        };
        for(const auto id:ids) {
            const Vec3 target=source.vertex(id).position;
            const double radiusDistance=length(target-dab.point);
            if(radiusDistance>dab.radius)continue;
            // Already opaque, exact-colour vertices cannot change under
            // source-over paint. Semi-transparent source paint must NOT
            // be skipped merely because its RGBA matches a prior coat:
            // repeated dabs legitimately build coverage.
            const auto current=localColorAt(source,id);
            if(!preview&&current.a==1.f&&current.r==dab.rgba.r&&
               current.g==dab.rgba.g&&current.b==dab.rgba.b) {
                addCenterSample(radiusDistance);
                continue;
            }
            // Back-facing vertices do not receive paint even if a ray
            // grazes their shared edge or a thin sheet.
            const Vec3 surfaceNormal=normalAt(source,id);
            if(length(surfaceNormal)<1e-12||
               dot(surfaceNormal,origin-target)<-1e-10)continue;
            const Vec3 displacement=target-origin;
            const double distance=length(displacement);
            if(distance<1e-12)continue;
            RegionTopologyIndex::PickStatistics work{};
            const auto first=source.firstRayHit({origin,displacement/distance},&work);
            if(work.trianglesTested>maxRayTriangles-triangleTests)
                throw std::length_error("Paint occlusion ray budget exhausted");
            triangleTests+=work.trianglesTested;
            // Allow a small surface-endpoint tolerance for the target's
            // incident faces. Anything closer is an intervening shadowing
            // sheet and must NOT receive paint.
            const double endpointTolerance=std::max(
                1e-9*std::max(1.0,dab.nominalEdgeLength),
                dab.nominalEdgeLength*1e-4);
            if(first&&length(*first-origin)<distance-endpointTolerance)
                continue;
            visible.emplace_back(id,radiusDistance);
            addCenterSample(radiusDistance);
        }
        // The surface colour is interpolated across triangles. At low
        // hardness, a single full-colour vertex is insufficient to make
        // the actual picked pixel fully saturated. Cover the picked
        // triangle's three corners, bounded by the requested radius.
        // Mirrored and API dabs without a picked face use three nearest
        // visible samples as a conservative sampling-radius fallback.
        double center=nearest[2]<dab.radius?nearest[2]:closest;
        if(dab.sourceFace) {
            if(const auto face=source.maybeFace(dab.sourceFace)) {
                for(const auto id:face->vertices)
                    center=std::max(center,length(
                        source.vertex(id).position-dab.point));
            }
        }
        center=std::min(center,dab.radius);

    return {std::move(visible),center};
}
} // namespace
std::vector<PaintDab> paintDabsWithMirror(PaintDab dab,bool symmetric) {
    std::vector<PaintDab> dabs{dab};
    if(symmetric&&(std::abs(dab.point.x)>1e-10||
                   std::abs(dab.outwardNormal.x)>1e-10)) {
        dab.point.x=-dab.point.x;
        dab.outwardNormal.x=-dab.outwardNormal.x;
        dab.sourceFace=0;
        dabs.push_back(dab);
    }
    return dabs;
}
PaintPreview collectPaintPreview(
    std::shared_ptr<const RegionTopologyIndex> source,
    const std::vector<PaintDab>& dabs,
    std::size_t maxVertices,std::size_t maxRayTriangles,
    std::size_t maxFaces) {
    if(!source)throw std::invalid_argument("Preview needs immutable root");
    if(!maxVertices||!maxRayTriangles||!maxFaces)
        throw std::invalid_argument("Preview work budgets must be positive");
    std::size_t candidates=0,triangleTests=0;
    std::set<VertexId> visibleVertices;
    for(const auto& dab:dabs) {
        const auto samples=collectVisiblePaintSamples(*source,dab,true,
            candidates,triangleTests,maxVertices,maxRayTriangles);
        for(const auto& [id,distance]:samples.vertices) {
            (void)distance;
            visibleVertices.insert(id);
        }
    }
    std::set<FaceId> faces;
    for(const auto id:visibleVertices)
        for(const auto face:source->vertex(id).incidentFaces) {
            faces.insert(face);
            if(faces.size()>maxFaces)
                throw std::length_error("Paint preview face budget exhausted");
        }
    return {{visibleVertices.begin(),visibleVertices.end()},
            {faces.begin(),faces.end()}};
}
PaintDabResult applyVertexPaintDabs(
    std::shared_ptr<const RegionTopologyIndex> source,
    const std::vector<PaintDab>& dabs,
    std::size_t maxVertices,std::size_t maxRayTriangles) {
    if(!source)throw std::invalid_argument("Painting needs immutable root");
    if(!maxVertices||!maxRayTriangles)
        throw std::invalid_argument("Paint work budgets must be positive");
    if(dabs.empty())return {source,{},{},0};
    struct Influence {float strength;VertexRGBA color;};
    std::map<VertexId,Influence> influences;
    std::size_t candidates=0,triangleTests=0;
    for(const auto& dab:dabs) {
        const auto samples=collectVisiblePaintSamples(*source,dab,false,
            candidates,triangleTests,maxVertices,maxRayTriangles);
        for(const auto& [id,distance]:samples.vertices) {
            const float influence=weight(distance,samples.fullStrengthDistance,dab);
            if(influence<=0.f)continue;
            const auto [it,inserted]=influences.try_emplace(
                id,Influence{influence,dab.rgba});
            if(!inserted&&influence>it->second.strength)
                it->second={influence,dab.rgba};
        }
    }
    if(influences.empty())return {source,{},{},0};
    auto view=source->revision().createMutableView();
    std::vector<VertexId> changed;
    changed.reserve(influences.size());
    for(const auto& [id,choice]:influences) {
        const auto previous=localColorAt(*source,id);
        const auto rgba=paintOver(previous,choice.color,choice.strength);
        if(rgba==previous)continue;
        view.setVertexColor(source->vertex(id).owner,id,rgba);
        changed.push_back(id);
    }
    if(changed.empty())return {source,{},{},0};
    auto commit=view.commit();
    auto colored=RegionTopologyIndex::withVertexColors(*source,commit.revision);
    auto dirty=dirtyRenderPartitions(*source,*colored,changed,{});
    return {std::move(colored),std::move(changed),std::move(dirty),
            commit.statistics.clonedPartitionBytes};
}
VertexRGBA vertexColorAt(const RegionTopologyIndex& source,VertexId id) {
    return localColorAt(source,id);
}
} // namespace mesher
