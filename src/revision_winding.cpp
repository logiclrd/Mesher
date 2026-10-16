#include "mesher/region_edit.hpp"
#include "mesher/revision_picking.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
using Kind=RegionTopologyIndex::FragmentKind;
using Bary=std::array<double,3>;
using Corners=std::array<Bary,3>;
struct LongVec {long double x{},y{},z{};};
LongVec longVector(Vec3 point,Vec3 origin) {
    return {static_cast<long double>(point.x)-origin.x,
            static_cast<long double>(point.y)-origin.y,
            static_cast<long double>(point.z)-origin.z};
}
long double dotLong(LongVec a,LongVec b) {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
LongVec crossLong(LongVec a,LongVec b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
long double magnitude(LongVec p) {
    return std::sqrt(dotLong(p,p));
}
long double solidAngle(const std::array<Vec3,3>& triangle,Vec3 p) {
    const auto a=longVector(triangle[0],p);
    const auto b=longVector(triangle[1],p);
    const auto c=longVector(triangle[2],p);
    const auto la=magnitude(a),lb=magnitude(b),lc=magnitude(c);
    if(la==0||lb==0||lc==0)
        return std::numeric_limits<long double>::quiet_NaN();
    const auto numerator=dotLong(a,crossLong(b,c));
    const auto denominator=la*lb*lc+dotLong(a,b)*lc+
                           dotLong(b,c)*la+dotLong(c,a)*lb;
    return 2*std::atan2(numerator,denominator);
}
Bary midpoint(Bary a,Bary b) {
    return {(a[0]+b[0])/2,(a[1]+b[1])/2,(a[2]+b[2])/2};
}
Vec3 surfacePoint(const std::array<Vec3,3>& vertices,Bary bary) {
    return vertices[0]*bary[0]+vertices[1]*bary[1]+vertices[2]*bary[2];
}
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
std::array<Vec3,3> facePoints(const RegionTopologyIndex& index,FaceId id) {
    const auto& vertices=index.face(id).vertices;
    return {index.vertex(vertices[0]).position,
            index.vertex(vertices[1]).position,
            index.vertex(vertices[2]).position};
}
}
long double RegionTopologyIndex::referenceWindingAt(Vec3 point) const {
    if(!finite(point))throw std::invalid_argument("Nonfinite winding query point");
    long double sum=0;
    // Explicit O(N) independent mathematical reference, not the live
    // acceleration path. Used for tests and exceptional ray ambiguity only.
    revision().forEachPartition([&](PartitionId,const MeshPartition& partition) {
        for(const auto& face:partition.faces) {
            const std::array<Vec3,3> points{
                vertex(face.vertices[0]).position,
                vertex(face.vertices[1]).position,
                vertex(face.vertices[2]).position};
            sum+=solidAngle(points,point);
        }
    });
    constexpr long double fourPi=
        12.5663706143591729538505735331180115368L;
    return sum/fourPi;
}

RegionTopologyIndex::WindingSample RegionTopologyIndex::positiveWindingAt(
    Vec3 point,WindingStatistics* measured) const {
    if(!finite(point))throw std::invalid_argument("Nonfinite winding query point");
    WindingStatistics local{};
    auto& stats=measured?*measured:local;
    stats={};
    const std::array<Vec3,3> directions{{
        normalized(Vec3{1,0.193117,0.527463}),
        normalized(Vec3{-0.327151,1,0.169281}),
        normalized(Vec3{0.187397,-0.521311,1})
    }};
    int answer=0;
    unsigned confident=0;
    for(const auto direction:directions) {
        bool ambiguous=false;
        const int count=picking_->signedRayCrossings(
            {point,direction},stats,ambiguous);
        if(ambiguous)continue;
        if(confident>0&&count!=answer)
            return {0,false}; // disagreeing rays: do not certify
        answer=count;
        ++confident;
        if(confident>=2)return {answer,true};
    }
    // Exceptional fallback. Do not incur O(N) on normal local queries.
    ++stats.referenceFallbacks;
    const auto exact=referenceWindingAt(point);
    if(!std::isfinite(exact))return {0,false};
    const auto rounded=std::round(exact);
    if(std::abs(exact-rounded)>0.02L||
       rounded<static_cast<long double>(std::numeric_limits<int>::min())||
       rounded>static_cast<long double>(std::numeric_limits<int>::max()))
        return {0,false}; // near the surface or unreliable arithmetic
    if(confident&&answer!=static_cast<int>(rounded))return {0,false};
    return {static_cast<int>(rounded),true};
}

std::vector<RegionTopologyIndex::SurfaceFragment>
RegionTopologyIndex::classifyFaceFragments(FaceId id,unsigned depth) const {
    if(depth>4)throw std::invalid_argument("Fragment depth exceeds bounded limit");
    const auto tri=facePoints(*this,id);
    const auto normal=cross(tri[1]-tri[0],tri[2]-tri[0]);
    const double magnitude=length(normal);
    const double edge=std::min({length(tri[1]-tri[0]),
                                length(tri[2]-tri[1]),
                                length(tri[0]-tri[2])});
    const bool degenerate=!std::isfinite(magnitude)||magnitude<=0||edge<=0;
    const auto n=degenerate?Vec3{}:normal/magnitude;
    std::vector<SurfaceFragment> patches;
    std::size_t expected=1;
    for(unsigned i=0;i<depth;++i)expected*=4;
    patches.reserve(expected);
    const auto visit=[&](auto&& self,Corners corners,unsigned remaining)->void {
        if(remaining) {
            const auto ab=midpoint(corners[0],corners[1]);
            const auto bc=midpoint(corners[1],corners[2]);
            const auto ca=midpoint(corners[2],corners[0]);
            self(self,{{corners[0],ab,ca}},remaining-1);
            self(self,{{ab,corners[1],bc}},remaining-1);
            self(self,{{ca,bc,corners[2]}},remaining-1);
            self(self,{{ab,bc,ca}},remaining-1);
            return;
        }
        SurfaceFragment patch;
        patch.face=id;
        patch.corners=corners;
        if(degenerate) {patches.push_back(patch);return;}
        const Bary bary{
            (corners[0][0]+corners[1][0]+corners[2][0])/3,
            (corners[0][1]+corners[1][1]+corners[2][1])/3,
            (corners[0][2]+corners[1][2]+corners[2][2])/3
        };
        const auto center=surfacePoint(tri,bary);
        // Check two local offset scales. Disagreement exposes close surfaces
        // or very thin layers instead of granting permission to delete.
        const double delta=std::max(1e-5*edge,
            128*std::numeric_limits<double>::epsilon()*
                std::max({1.0,std::abs(center.x),
                          std::abs(center.y),std::abs(center.z)}));
        const auto minus=positiveWindingAt(center-n*delta);
        const auto plus=positiveWindingAt(center+n*delta);
        const auto minusFar=positiveWindingAt(center-n*(delta*2));
        const auto plusFar=positiveWindingAt(center+n*(delta*2));
        if(!minus.reliable||!plus.reliable||
           !minusFar.reliable||!plusFar.reliable||
           minus.winding!=minusFar.winding||
           plus.winding!=plusFar.winding||
           minus.winding-plus.winding!=1) {
            patches.push_back(patch);
            return;
        }
        patch.negativeWinding=minus.winding;
        patch.positiveWinding=plus.winding;
        const bool insideMinus=minus.winding>0,insidePlus=plus.winding>0;
        if(insideMinus&&!insidePlus)patch.kind=Kind::Exposed;
        else if(!insideMinus&&insidePlus)patch.kind=Kind::ReversedBoundary;
        else if(insideMinus)patch.kind=Kind::Buried;
        else patch.kind=Kind::Outside;
        patches.push_back(patch);
    };
    visit(visit,{{Bary{1,0,0},Bary{0,1,0},Bary{0,0,1}}},depth);
    return patches;
}

RegionTopologyIndex::ComponentClassification
RegionTopologyIndex::classifyClosedComponent(
    FaceId seed,std::size_t maxFaces) const {
    (void)face(seed); // invalid seed must be observable
    ComponentClassification output;
    if(maxFaces==0)return output;
    std::queue<FaceId> pending;
    std::unordered_set<FaceId> seen{seed};
    std::vector<FaceId> faces;
    pending.push(seed);
    while(!pending.empty()) {
        if(faces.size()>=maxFaces) {
            output.facesVisited=faces.size();
            return output; // no partial classification
        }
        const auto id=pending.front();pending.pop();
        faces.push_back(id);
        const auto& vertices=face(id).vertices;
        for(unsigned e=0;e<3;++e) {
            auto a=vertices[e],b=vertices[(e+1)%3];
            if(a>b)std::swap(a,b);
            const auto sides=maybeEdgeSides({a,b});
            if(!sides||sides->size()!=2)return {ComponentKind::Ambiguous,
                                                faces.size(),false};
            for(const auto& side:*sides)
                if(seen.insert(side.face).second)pending.push(side.face);
        }
    }
    output.complete=true;
    output.facesVisited=faces.size();
    bool allRedundant=true,allBoundary=true;
    for(const auto id:faces) {
        // Avoid classifying an intersecting connected component using a
        // single face sample, which could miss cut fragments.
        if(!triangleIntersections({}, {id}).empty())
            return {ComponentKind::Ambiguous,faces.size(),true};
        const auto fragments=classifyFaceFragments(id,0);
        const auto kind=fragments.front().kind;
        if(kind!=Kind::Buried&&kind!=Kind::Outside)allRedundant=false;
        if(kind!=Kind::Exposed&&kind!=Kind::ReversedBoundary)allBoundary=false;
        if(!allRedundant&&!allBoundary)
            return {ComponentKind::Ambiguous,faces.size(),true};
    }
    if(allRedundant)return {ComponentKind::Redundant,faces.size(),true};
    if(!allBoundary)return {ComponentKind::Ambiguous,faces.size(),true};

    // Determine whether this closed shell is nested inside other oriented
    // geometry. A boundary with positive occupancy outside the component
    // could be an intentional cavity: *report*, never silently delete.
    const auto points=facePoints(*this,seed);
    const auto normal=normalized(cross(points[1]-points[0],
                                        points[2]-points[0]));
    const auto center=(points[0]+points[1]+points[2])/3;
    const double small=std::min({length(points[1]-points[0]),
                                 length(points[2]-points[1]),
                                 length(points[0]-points[2])})*1e-4;
    const auto probe=center+normal*small;
    const auto global=positiveWindingAt(probe);
    if(!global.reliable)return {ComponentKind::Ambiguous,faces.size(),true};
    long double componentAngle=0;
    for(const auto id:faces)
        componentAngle+=solidAngle(facePoints(*this,id),probe);
    constexpr long double fourPi=
        12.5663706143591729538505735331180115368L;
    const auto other=static_cast<long double>(global.winding)-
                     componentAngle/fourPi;
    if(!std::isfinite(other)||std::abs(other-std::round(other))>0.02L)
        return {ComponentKind::Ambiguous,faces.size(),true};
    return {std::round(other)>0?ComponentKind::PotentialCavity:
            ComponentKind::Boundary,faces.size(),true};
}
} // namespace mesher
