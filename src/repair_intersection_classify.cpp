#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
using Kind=RegionTopologyIndex::FragmentKind;
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
std::array<Vec3,3> points(const RegionTopologyIndex& index,FaceId id) {
    const auto corners=index.face(id).vertices;
    return {index.vertex(corners[0]).position,
            index.vertex(corners[1]).position,
            index.vertex(corners[2]).position};
}
double shortEdge(const std::array<Vec3,3>& p) {
    return std::min({length(p[0]-p[1]),length(p[1]-p[2]),
                     length(p[2]-p[0])});
}
double longEdge(const std::array<Vec3,3>& p) {
    return std::max({length(p[0]-p[1]),length(p[1]-p[2]),
                     length(p[2]-p[0])});
}
Vec3 point(const std::array<Vec3,3>& triangle,
           const std::array<double,3>& bary) {
    return triangle[0]*bary[0]+triangle[1]*bary[1]+
           triangle[2]*bary[2];
}
} // namespace

CutFragmentAssessment assessConformingCutFragments(
    const ConformingCutCandidate& cut,const CutFragmentLimits& limits) {
    CutFragmentAssessment out;
    const auto fail=[&](const char* reason) {
        out.complete=false;
        out.refusal=reason;
        out.fragments.clear(); // NEVER expose partial authoritative labels
        return out;
    };
    if(!cut.originalIndex||!cut.index||!cut.commit.revision||
       cut.index==cut.originalIndex||
       cut.commit.revision.get()!=&cut.index->revision())
        return fail("No consistent original and private subdivided index roots");
    const std::size_t expected=cut.rewrittenFaces.size()+cut.insertedFaces.size();
    if(expected==0||cut.childSources.size()!=expected)
        return fail("Incomplete explicit original-to-child cut provenance");
    if(expected>limits.maxFragments)
        throw std::length_error("Exact child-fragment count budget exhausted");
    std::set<FaceId> modified;
    modified.insert(cut.rewrittenFaces.begin(),cut.rewrittenFaces.end());
    modified.insert(cut.insertedFaces.begin(),cut.insertedFaces.end());
    if(modified.size()!=expected)
        return fail("Duplicate or conflicting cut child FaceId");
    std::set<FaceId> seen;
    std::map<FaceId,double> areas;
    out.fragments.reserve(expected);
    // A face in this batch is an actual complete triangle in the one
    // conforming RegionEdit, not an approximate patch of an uncut parent.
    // Prove the stated provenance and source-face area conservation first.
    for(const auto& [child,source]:cut.childSources) {
        if(!modified.contains(child)||!seen.insert(child).second||
           !cut.index->maybeFace(child)||!cut.originalIndex->maybeFace(source))
            return fail("Child has missing or contradictory source-face provenance");
        if(cut.originalIndex->maybeFace(child)&&child!=source)
            return fail("Rewritten old face must retain its original identity");
        if(!cut.originalIndex->maybeFace(child)&&
           !std::count(cut.insertedFaces.begin(),cut.insertedFaces.end(),child))
            return fail("Unreported new child face");
        const auto old=points(*cut.originalIndex,source);
        const auto tri=points(*cut.index,child);
        const auto normal=cross(old[1]-old[0],old[2]-old[0]);
        const double norm=length(normal),originalScale=longEdge(old);
        if(!(norm>0)||!std::isfinite(norm)||!(originalScale>0))
            return fail("Degenerate original face cannot certify exact child geometry");
        const auto tolerance=1e-9*originalScale;
        for(const auto p:tri) {
            if(!finite(p)||std::abs(dot(p-old[0],normal))/norm>tolerance)
                return fail("Child vertex is off its original face plane");
            for(unsigned j=0;j<3;++j) {
                const auto a=old[j],b=old[(j+1)%3];
                if(dot(cross(b-a,p-a),normal)<
                   -tolerance*length(b-a)*norm)
                    return fail("Child triangle escapes original source boundary");
            }
        }
        const double oriented=dot(
            cross(tri[1]-tri[0],tri[2]-tri[0]),normal)/norm;
        if(!std::isfinite(oriented)||
           !(oriented>1e-12*norm))
            return fail("Child triangle lost positive original winding or area");
        areas[source]+=oriented;
    }
    if(seen!=modified)return fail("Some staged cut child FaceIds lack provenance");
    for(const auto& [source,area]:areas) {
        const auto old=points(*cut.originalIndex,source);
        const auto total=length(cross(old[1]-old[0],old[2]-old[0]));
        if(std::abs(total-area)>1e-8*total)
            return fail("Subdivided child triangles do not conserve original face area");
    }

    // Three off-centroid interior checks in addition to the centroid.
    // Two signed offsets per side reject near-surface disagreement. This
    // cannot by itself certify *unobserved* subface interior behavior:
    // no surgical permission follows until the exact contact-coverage
    // and final contour/closure acceptance milestones.
    constexpr std::array<std::array<double,3>,4> barycentrics{{
        {1.0/3,1.0/3,1.0/3},
        {.6,.2,.2},{.2,.6,.2},{.2,.2,.6}
    }};
    for(const auto& [child,source]:cut.childSources) {
        const auto tri=points(*cut.index,child);
        const auto original=points(*cut.originalIndex,source);
        const auto normal=cross(tri[1]-tri[0],tri[2]-tri[0]);
        const double magnitude=length(normal);
        const double childEdge=shortEdge(tri),sourceEdge=shortEdge(original);
        if(!(magnitude>0)||!(childEdge>0)||!(sourceEdge>0))
            return fail("Small or degenerate exact child triangle");
        const auto n=normal/magnitude;
        // Both the ORIGINAL local mesh scale and the actual tiny child
        // scale matter. Offset samples must not leap through thin details.
        const double delta=std::min(1e-5*sourceEdge,1e-3*childEdge);
        if(!(delta>0)||!std::isfinite(delta))
            return fail("No reliable normal offset for exact fragment");
        bool firstSample=true;
        int referenceMinus=0,referencePlus=0;
        for(const auto bary:barycentrics) {
            const auto center=point(tri,bary);
            const auto coordinateScale=std::max({
                1.0,std::abs(center.x),std::abs(center.y),std::abs(center.z)});
            if(2*delta<=
               256*std::numeric_limits<double>::epsilon()*coordinateScale)
                return fail("Cut fragment offset is below floating-point resolution");
            for(const double multiplier:std::array<double,2>{1.0,2.0}) {
                std::array<int,2> winding{};
                for(unsigned side=0;side<2;++side) {
                    if(out.windingQueries>=limits.maxWindingQueries)
                        throw std::length_error("Cut fragment winding-query budget exhausted");
                    ++out.windingQueries;
                    const auto query=center+n*delta*multiplier*(side?1.0:-1.0);
                    WindingQueryLimits remaining;
                    remaining.maxBoxTests=
                        limits.maxWindingBoxTests-out.windingWork.boxesTested;
                    remaining.maxPartitionTests=
                        limits.maxWindingPartitionTests-out.windingWork.partitionsTested;
                    remaining.maxTriangleTests=
                        limits.maxWindingTriangleTests-out.windingWork.trianglesTested;
                    remaining.maxRays=
                        limits.maxWindingRays-out.windingWork.raysTested;
                    RegionTopologyIndex::WindingStatistics work;
                    const auto sample=cut.index->positiveWindingAt(
                        query,&work,false,remaining);
                    out.windingWork.boxesTested+=work.boxesTested;
                    out.windingWork.partitionsTested+=work.partitionsTested;
                    out.windingWork.trianglesTested+=work.trianglesTested;
                    out.windingWork.raysTested+=work.raysTested;
                    out.windingWork.referenceFallbacks+=work.referenceFallbacks;
                    out.windingWork.referenceFallbackRefusals+=
                        work.referenceFallbackRefusals;
                    if(!sample.reliable)
                        return fail("One signed-winding witness was ambiguous");
                    winding[side]=sample.winding;
                }
                if(winding[0]-winding[1]!=1)
                    return fail("Child face lacks a reliable unit signed-winding jump");
                if(firstSample) {
                    referenceMinus=winding[0];
                    referencePlus=winding[1];
                    firstSample=false;
                }else if(referenceMinus!=winding[0]||
                         referencePlus!=winding[1])
                    return fail("Child face has inconsistent signed winding across its interior");
            }
        }
        const bool insideMinus=referenceMinus>0;
        const bool insidePlus=referencePlus>0;
        Kind kind=Kind::Outside;
        if(insideMinus&&!insidePlus)kind=Kind::Exposed;
        else if(!insideMinus&&insidePlus)kind=Kind::ReversedBoundary;
        else if(insideMinus)kind=Kind::Buried;
        out.fragments.push_back({child,source,kind,referenceMinus,referencePlus});
    }
    out.complete=true;
    return out;
}
} // namespace mesher
