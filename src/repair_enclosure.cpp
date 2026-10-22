#include "mesher/repair_enclosure.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
struct Box {Vec3 low{},high{};};
struct LongVec {long double x{},y{},z{};};
LongVec relative(Vec3 p,Vec3 o) {
    return {static_cast<long double>(p.x)-o.x,
            static_cast<long double>(p.y)-o.y,
            static_cast<long double>(p.z)-o.z};
}
long double dotLong(LongVec a,LongVec b) {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
LongVec crossLong(LongVec a,LongVec b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
long double lengthLong(LongVec a) {
    return std::sqrt(dotLong(a,a));
}
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
std::array<Vec3,3> points(const RegionTopologyIndex& mesh,FaceId id) {
    const auto& face=mesh.face(id);
    return {mesh.vertex(face.vertices[0]).position,
            mesh.vertex(face.vertices[1]).position,
            mesh.vertex(face.vertices[2]).position};
}
Box bounds(const RegionTopologyIndex& mesh,const RepairSurvivorComponent& component) {
    Box out{};
    if(component.vertices.empty())
        throw std::logic_error("R06-M02 component without vertices");
    out.low=out.high=mesh.vertex(component.vertices.front()).position;
    for(const auto id:component.vertices) {
        const auto p=mesh.vertex(id).position;
        if(!finite(p))throw std::logic_error("R06-M02 nonfinite mesh coordinates");
        out.low={std::min(out.low.x,p.x),std::min(out.low.y,p.y),
                 std::min(out.low.z,p.z)};
        out.high={std::max(out.high.x,p.x),std::max(out.high.y,p.y),
                  std::max(out.high.z,p.z)};
    }
    return out;
}
long double boxVolume(Box box) {
    return static_cast<long double>(box.high.x-box.low.x)*
           (box.high.y-box.low.y)*(box.high.z-box.low.z);
}
bool withinBox(Box inner,Box outer) {
    // Necessary, not sufficient, condition for geometric containment.
    // Do not use box nesting as a deletion certificate.
    const double span=std::max({outer.high.x-outer.low.x,
                                outer.high.y-outer.low.y,
                                outer.high.z-outer.low.z});
    const double slack=std::max(span*1e-10,
                                256*std::numeric_limits<double>::epsilon()*
                                std::max({1.0,std::abs(outer.low.x),
                                            std::abs(outer.high.x),
                                            std::abs(outer.low.y),
                                            std::abs(outer.high.y),
                                            std::abs(outer.low.z),
                                            std::abs(outer.high.z)}));
    return inner.low.x>=outer.low.x-slack &&
           inner.low.y>=outer.low.y-slack &&
           inner.low.z>=outer.low.z-slack &&
           inner.high.x<=outer.high.x+slack &&
           inner.high.y<=outer.high.y+slack &&
           inner.high.z<=outer.high.z+slack;
}
long double volume(const RegionTopologyIndex& mesh,
                   const RepairSurvivorComponent& component,Box box) {
    // Translation-invariant tetrahedron sum; long double accumulation avoids
    // catastrophic cancellation for small features far from the origin.
    const Vec3 origin=(box.low+box.high)/2;
    long double sixVolume=0;
    for(const auto id:component.faces) {
        const auto tri=points(mesh,id);
        sixVolume+=dotLong(relative(tri[0],origin),
                          crossLong(relative(tri[1],origin),
                                    relative(tri[2],origin)));
    }
    return sixVolume/6;
}
long double angle(const std::array<Vec3,3>& triangle,Vec3 point) {
    const auto a=relative(triangle[0],point);
    const auto b=relative(triangle[1],point);
    const auto c=relative(triangle[2],point);
    const auto la=lengthLong(a),lb=lengthLong(b),lc=lengthLong(c);
    if(la==0||lb==0||lc==0)
        return std::numeric_limits<long double>::quiet_NaN();
    return 2*std::atan2(dotLong(a,crossLong(b,c)),
        la*lb*lc+dotLong(a,b)*lc+dotLong(b,c)*la+dotLong(c,a)*lb);
}
long double winding(const RegionTopologyIndex& mesh,
                    const RepairSurvivorComponent& enclosing,
                    Vec3 sample,
                    std::size_t& work,const EnclosedComponentLimits& limits) {
    constexpr long double fourPi=
        12.5663706143591729538505735331180115368L;
    long double total=0;
    for(const auto id:enclosing.faces) {
        if(++work>limits.maxWindingTriangleVisits)
            throw std::length_error("R06-M02 winding triangle work limit");
        total+=angle(points(mesh,id),sample);
    }
    return total/fourPi;
}
Vec3 faceCenter(const RegionTopologyIndex& mesh,FaceId id) {
    const auto p=points(mesh,id);
    return (p[0]+p[1]+p[2])/3;
}
void chargeCollision(RegionTopologyIndex::CollisionStatistics& total,
                     const RegionTopologyIndex::CollisionStatistics& work) {
    total.seedFaces+=work.seedFaces;
    total.boxesTested+=work.boxesTested;
    total.partitionsTested+=work.partitionsTested;
    total.candidateTriangles+=work.candidateTriangles;
    total.narrowPhaseTests+=work.narrowPhaseTests;
    total.reportedContacts+=work.reportedContacts;
}
} // namespace

EnclosedComponentReport inspectEnclosedComponents(
    std::shared_ptr<const RegionTopologyIndex> closed,
    const EnclosedComponentLimits& limits) {
    if(!closed || !limits.maxFaces || !limits.maxEdges ||
       !limits.maxComponents || !limits.maxWindingTriangleVisits ||
       !limits.maxCollisionSeedFaces || !limits.maxCollisionBoxTests ||
       !limits.maxCollisionPartitionTests ||
       !limits.maxCollisionCandidateTriangles ||
       !limits.maxCollisionNarrowPhaseTests ||
       !limits.maxCollisionReportedContacts ||
       !(limits.minimumRelativeVolume>0) ||
       !(limits.windingTolerance>0&&limits.windingTolerance<0.25) ||
       !std::isfinite(limits.minimumRelativeVolume) ||
       !std::isfinite(limits.windingTolerance))
        throw std::invalid_argument("R06-M02 requires finite positive analysis limits");

    // Reuse the R06-M01 *actual* directed manifold component audit with an
    // empty boundary contract. This is the closed-revision variant: no
    // invented R03 excision or mutation occurs.
    PrivateExcisionResult closedView;
    closedView.index=closed;
    closed->revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
        if(part.faces.size()>limits.maxFaces-std::min(limits.maxFaces,closedView.survivingFaces))
            throw std::length_error("R06-M02 closed face count limit");
        closedView.survivingFaces+=part.faces.size();
        if(closedView.survivingFaces>limits.maxFaces)
            throw std::length_error("R06-M02 closed face count limit");
    });
    RepairExcisionPreview noOpenContours;
    noOpenContours.complete=true;
    EnclosedComponentReport result;
    result.inventory=inventoryPrivateExcision(
        closedView,noOpenContours,
        {limits.maxFaces,limits.maxEdges,1});
    if(result.inventory.boundaryEdges || result.inventory.boundaryLoops)
        throw std::logic_error("R06-M02 refuses a surface with open boundaries");
    if(result.inventory.components.size()>limits.maxComponents)
        throw std::length_error("R06-M02 closed component count limit");

    const auto& pieces=result.inventory.components;
    const auto count=pieces.size();
    result.components.resize(count);
    std::vector<Box> boxes(count);
    std::vector<int> orientation(count);
    for(std::size_t i=0;i<count;++i) {
        auto& info=result.components[i];
        info.component=i;
        boxes[i]=bounds(*closed,pieces[i]);
        const auto signedVolume=volume(*closed,pieces[i],boxes[i]);
        const auto extent=boxVolume(boxes[i]);
        if(!std::isfinite(signedVolume)||!std::isfinite(extent) ||
           extent<=0 || std::abs(signedVolume)<=
                            extent*limits.minimumRelativeVolume) {
            info.reason="R06-M02 degenerate or ambiguous oriented volume";
            continue;
        }
        info.signedVolume=static_cast<double>(signedVolume);
        if(!std::isfinite(info.signedVolume)) {
            info.reason="R06-M02 unrepresentable signed component volume";
            continue;
        }
        orientation[i]=signedVolume>0?1:-1;
    }

    for(std::size_t i=0;i<count;++i) {
        auto& info=result.components[i];
        if(!orientation[i])continue;

        std::vector<std::size_t> enclosing;
        bool unsure=false;
        for(std::size_t j=0;j<count;++j) {
            if(i==j || !withinBox(boxes[i],boxes[j]))continue;
            if(!orientation[j]){unsure=true;continue;}

            // Three separated witness FACE CENTERS. One valid winding sample
            // is not enough to classify nested shells or a grazing face.
            const auto& faces=pieces[i].faces;
            const std::array<std::size_t,3> samples{{0,faces.size()/2,faces.size()-1}};
            bool inside=true,outside=true;
            for(const auto sampleId:samples) {
                const auto w=winding(*closed,pieces[j],
                                    faceCenter(*closed,faces[sampleId]),
                                    result.windingTriangleVisits,limits);
                ++info.witnessPoints;
                if(!std::isfinite(w)||
                   std::abs(w-std::round(w))>limits.windingTolerance) {
                    unsure=true;
                    inside=false;
                    outside=false;
                    break;
                }
                if(std::abs(w-1)>limits.windingTolerance)inside=false;
                if(std::abs(w)>limits.windingTolerance)outside=false;
            }
            if(inside)enclosing.push_back(j);
            else if(!outside)unsure=true; // partial enclosure/crossing
        }

        if(unsure) {
            info.reason="R06-M02 ambiguous nested shell winding or orientation";
            continue;
        }
        if(enclosing.empty()) {
            if(orientation[i]>0) {
                info.kind=EnclosedComponentKind::Exterior;
                info.reason="outward closed exterior component";
            }else info.reason="inward closed component not proved enclosed";
            continue;
        }
        if(enclosing.size()!=1) {
            info.reason="R06-M02 multiple nested shells require unambiguous material evidence";
            continue;
        }
        const auto enclosingId=enclosing.front();
        if(orientation[i]>0) {
            info.reason="R06-M02 nested outward shell requires separate burial proof";
            continue;
        }
        if(orientation[enclosingId]!=1) {
            info.reason="R06-M02 shell is not enclosed by an outward material boundary";
            continue;
        }

        // A fully enclosed inversion can be erased only if its surface is
        // disjoint from all other surfaces, and has no self-crossings. Use
        // existing indexed triangle/triangle evidence rather than merely
        // assuming two nested bounding boxes imply disjoint geometry.
        RegionTopologyIndex::CollisionQueryLimits query;
        query.maxSeedFaces=limits.maxCollisionSeedFaces;
        query.maxBoxTests=limits.maxCollisionBoxTests;
        query.maxPartitionTests=limits.maxCollisionPartitionTests;
        query.maxCandidateTriangles=limits.maxCollisionCandidateTriangles;
        query.maxNarrowPhaseTests=limits.maxCollisionNarrowPhaseTests;
        query.maxReportedContacts=limits.maxCollisionReportedContacts;
        RegionTopologyIndex::CollisionStatistics work;
        const auto intersections=closed->triangleIntersections(
            {},pieces[i].faces,&work,query);
        chargeCollision(result.collisionWork,work);
        if(!intersections.empty()) {
            info.reason="R06-M02 inward shell has unresolved triangle contacts";
            continue;
        }
        info.kind=EnclosedComponentKind::RedundantEnclosedCavity;
        info.enclosingComponent=enclosingId;
        info.reason="enclosed inward shell; no intentional internal voids";
    }
    return result;
}
} // namespace mesher
