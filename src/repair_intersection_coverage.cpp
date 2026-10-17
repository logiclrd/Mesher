#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using CollisionPair=RegionTopologyIndex::CollisionPair;
using Points=std::array<Vec3,3>;

Points facePoints(const RegionTopologyIndex& index,FaceId id) {
    const auto ids=index.face(id).vertices;
    return {index.vertex(ids[0]).position,index.vertex(ids[1]).position,
            index.vertex(ids[2]).position};
}
double extent(const Points& p) {
    return std::max({length(p[1]-p[0]),length(p[2]-p[1]),
                     length(p[0]-p[2])});
}
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
double tolerance(const Points& a,const Points& b) {
    double coordinate=1;
    for(const auto& tri:{a,b})
        for(const auto p:tri)
            coordinate=std::max({coordinate,std::abs(p.x),
                                 std::abs(p.y),std::abs(p.z)});
    return std::max(1e-9*std::max(extent(a),extent(b)),
        128*std::numeric_limits<double>::epsilon()*coordinate);
}
bool sameEnds(const std::array<Vec3,2>& a,
              const std::array<Vec3,2>& b,double limit) {
    const auto similar=[&](Vec3 x,Vec3 y) {
        return finite(x)&&finite(y)&&length(x-y)<=limit;
    };
    return (similar(a[0],b[0])&&similar(a[1],b[1]))||
           (similar(a[0],b[1])&&similar(a[1],b[0]));
}

// Each child is a planar triangle. If all three vertex distances to the
// opposing source plane are on ONE side (or on its line), its OPEN INTERIOR
// cannot intersect that plane, hence cannot cross that entire other face.
// The entire original-source contact curve must additionally be traced
// along child EDGES to prove the cut was actually installed.
bool noInteriorContactCrossing(const Points& child,const Points& opposing,
                               Vec3 sectionStart,Vec3 sectionEnd,
                               double epsilon) {
    const auto normal=cross(opposing[1]-opposing[0],
                            opposing[2]-opposing[0]);
    const double norm=length(normal);
    const auto span=sectionEnd-sectionStart;
    const double spanLength=length(span);
    if(!(norm>0)||!std::isfinite(norm)||!(spanLength>4*epsilon))
        return false;
    const auto direction=span/spanLength;
    std::array<double,3> distance{};
    unsigned positive=0,negative=0,onPlane=0;
    for(unsigned i=0;i<3;++i) {
        if(!finite(child[i]))return false;
        distance[i]=dot(child[i]-opposing[0],normal)/norm;
        if(!std::isfinite(distance[i]))return false;
        if(distance[i]>epsilon)++positive;
        else if(distance[i]< -epsilon)++negative;
        else ++onPlane;
    }
    if(onPlane==3)return false; // unsupported coplanar original children
    if(!positive||!negative)return true;

    // A triangle may cross the OTHER face's infinite plane OUTSIDE
    // the FINITE true original face/face intersection. Intersect the
    // child's edges with that plane, project its entire clipped interval
    // onto the known finite V13 section and refuse ONLY when that
    // interval overlaps the section's open interior with positive extent.
    std::vector<double> lineParameters;
    for(unsigned i=0;i<3;++i) {
        const auto j=(i+1)%3;
        if(std::abs(distance[i])<=epsilon)
            lineParameters.push_back(dot(child[i]-sectionStart,direction));
        if((distance[i]>epsilon&&distance[j]< -epsilon)||
           (distance[i]< -epsilon&&distance[j]>epsilon)) {
            const auto fraction=distance[i]/(distance[i]-distance[j]);
            const auto p=child[i]+(child[j]-child[i])*fraction;
            if(!finite(p))return false;
            lineParameters.push_back(dot(p-sectionStart,direction));
        }
    }
    if(lineParameters.size()<2)return false; // ill-conditioned clip
    const auto [lo,hi]=std::minmax_element(
        lineParameters.begin(),lineParameters.end());
    if(!std::isfinite(*lo)||!std::isfinite(*hi))return false;
    return !(std::min(*hi,spanLength)-std::max(*lo,0.0)>epsilon);
}

// Extract the exact straight-line interval from the collection of REAL
// child triangle edges. Multiple collinear child edges (crossings or T
// junctions) may partition one original transverse contact section.
// A gap, missing endpoint, interior diagonal-only approximation, or
// unpaired sheet will refuse rather than blessing finite probes.
bool tracedByChildEdges(const std::vector<FaceId>& children,
                        const RegionTopologyIndex& index,
                        Vec3 start,Vec3 end,double epsilon,
                        std::size_t& tests,std::size_t budget) {
    const auto span=end-start;
    const double spanLength=length(span);
    if(!(spanLength>4*epsilon)||!std::isfinite(spanLength))
        return false;
    const auto direction=span/spanLength;
    const auto projected=[&](Vec3 p) {return dot(p-start,direction);};
    const auto nearLine=[&](Vec3 p) {
        const auto along=projected(p);
        return finite(p)&&length(p-(start+direction*along))<=epsilon;
    };
    std::vector<std::pair<double,double>> intervals;
    for(const auto face:children) {
        const auto tri=facePoints(index,face);
        for(unsigned e=0;e<3;++e) {
            if(tests>=budget)
                throw std::length_error("Cut-curve child-edge trace budget exhausted");
            ++tests;
            const auto a=tri[e],b=tri[(e+1)%3];
            if(!nearLine(a)||!nearLine(b))continue;
            const auto x=projected(a),y=projected(b);
            const auto lo=std::max(0.0,std::min(x,y));
            const auto hi=std::min(spanLength,std::max(x,y));
            if(hi-lo>epsilon*.25)intervals.emplace_back(lo,hi);
        }
    }
    std::sort(intervals.begin(),intervals.end());
    double covered=0;
    for(const auto& [low,high]:intervals) {
        if(low>covered+epsilon)return false;
        covered=std::max(covered,high);
        if(covered>=spanLength-epsilon)return true;
    }
    return false;
}
} // namespace

CutCoverageCertificate certifyConformingCutCoverage(
    const ConformingCutCandidate& cut,const CutCoverageLimits& limits) {
    CutCoverageCertificate out;
    const auto reject=[&](const char* message) {
        out.complete=false;
        out.refusal=message;
        out.coveredContacts.clear();
        return out;
    };
    if(!cut.originalIndex||!cut.index||!cut.commit.revision||
       cut.index==cut.originalIndex||
       cut.commit.revision.get()!=&cut.index->revision())
        return reject("Coverage requires one matching immutable source and candidate root");
    const auto count=cut.rewrittenFaces.size()+cut.insertedFaces.size();
    if(count==0||cut.childSources.size()!=count)
        return reject("Coverage lacks a complete materialized child FaceId journal");
    if(count>limits.maxChildren||cut.sourceContacts.size()>limits.maxContacts)
        throw std::length_error("Cut coverage child/contact budget exhausted");
    if(cut.sourceContacts.empty()||
       cut.sectionFirst.size()!=cut.sourceContacts.size()||
       cut.sectionSecond.size()!=cut.sourceContacts.size())
        return reject("Coverage requires every original source contact and both cut sections");

    std::set<FaceId> changed;
    changed.insert(cut.rewrittenFaces.begin(),cut.rewrittenFaces.end());
    changed.insert(cut.insertedFaces.begin(),cut.insertedFaces.end());
    if(changed.size()!=count)return reject("Duplicate changed child FaceId");
    std::map<FaceId,std::vector<FaceId>> children;
    for(const auto& [child,source]:cut.childSources) {
        if(!changed.contains(child)||!cut.index->maybeFace(child)||
           !cut.originalIndex->maybeFace(source)||
           (cut.originalIndex->maybeFace(child)&&child!=source))
            return reject("Invalid child/source face provenance in coverage");
        children[source].push_back(child);
    }
    std::size_t journalCount=0;
    for(const auto& [_,ids]:children)journalCount+=ids.size();
    if(journalCount!=count)return reject("Duplicate or missing source-to-child pair");
    if(children.size()!=cut.rewrittenFaces.size())
        return reject("An original affected source face lacks a retained child");
    for(const auto id:cut.rewrittenFaces)
        if(!children.contains(id))
            return reject("Original rewritten face missing from child journal");

    // This coverage result must stand on its own: never assume a separate
    // sampled-winding assessment has validated the private child geometry.
    // For every original source, all staged children must lie within its
    // oriented planar triangle and conserve its full signed area.
    for(const auto& [source,ids]:children) {
        const auto original=facePoints(*cut.originalIndex,source);
        const auto n=cross(original[1]-original[0],
                           original[2]-original[0]);
        const auto norm=length(n);
        const auto scale=extent(original);
        if(!(norm>0)||!std::isfinite(norm)||!(scale>0))
            return reject("Original face has no valid oriented area");
        const auto epsilon=1e-9*scale;
        double area=0;
        for(const auto child:ids) {
            const auto tri=facePoints(*cut.index,child);
            for(const auto p:tri) {
                if(!finite(p)||std::abs(dot(p-original[0],n))/norm>
                   epsilon)
                    return reject("Child moved off original planar face");
                for(unsigned j=0;j<3;++j) {
                    const auto a=original[j],b=original[(j+1)%3];
                    if(dot(cross(b-a,p-a),n)<
                       -epsilon*length(b-a)*norm)
                        return reject("Child escapes original triangle boundary");
                }
            }
            const auto oriented=dot(
                cross(tri[1]-tri[0],tri[2]-tri[0]),n)/norm;
            if(!std::isfinite(oriented)||!(oriented>1e-12*norm))
                return reject("Child lost positive original face winding");
            area+=oriented;
        }
        if(std::abs(area-norm)>1e-8*norm)
            return reject("Source face is not completely conserved by its children");
    }

    // Recover every V13 contact involving any modified ORIGINAL face.
    // This is independent of the subset the planner was told to cut.
    // Each limit is applied INSIDE the BVH query; exhaustion never returns
    // an authoritative truncated set.
    std::vector<FaceId> touched;
    touched.reserve(children.size());
    for(const auto& [source,_]:children)touched.push_back(source);
    const auto discovered=cut.originalIndex->triangleIntersections(
        {},touched,&out.discoveryWork,limits.originalCollisionLimits);
    std::set<std::pair<FaceId,FaceId>> declared,actual;
    for(const auto& contact:cut.sourceContacts) {
        if(contact.ambiguous||contact.first==contact.second||
           contact.first>contact.second||
           !children.contains(contact.first)||
           !children.contains(contact.second)||
           !declared.emplace(contact.first,contact.second).second)
            return reject("Invalid, duplicated or not fully edited declared contact");
    }
    for(const auto& contact:discovered) {
        if(contact.ambiguous)
            return reject("Uncertain original V13 contact prevents full coverage");
        actual.emplace(contact.first,contact.second);
    }
    if(actual!=declared)
        return reject("Original affected faces have unaccounted or stale V13 contacts");

    // There cannot be ANY ignored original intersection involving a changed
    // source face now. Verify that every known curve is the one materialized
    // in the private candidate and traces a full child-EDGE path in BOTH
    // independent surface meshes.
    for(std::size_t i=0;i<cut.sourceContacts.size();++i) {
        const auto contact=cut.sourceContacts[i];
        const auto originalA=facePoints(*cut.originalIndex,contact.first);
        const auto originalB=facePoints(*cut.originalIndex,contact.second);
        const auto section=splitTransverseTriangleContact(
            originalA,cut.originalIndex->face(contact.first).vertices,
            originalB,cut.originalIndex->face(contact.second).vertices);
        if(section.status!=IntersectionSectionStatus::TransverseSegment)
            return reject("Contact has unsupported original transverse geometry");
        const auto epsilon=tolerance(originalA,originalB);
        const auto& a=cut.sectionFirst[i],&b=cut.sectionSecond[i];
        std::array<Vec3,2> positionedA{},positionedB{};
        for(unsigned e=0;e<2;++e) {
            if(a[e]==b[e]||!cut.index->maybeVertex(a[e])||
               !cut.index->maybeVertex(b[e]))
                return reject("Cut section loses independent paired endpoint identities");
            positionedA[e]=cut.index->vertex(a[e]).position;
            positionedB[e]=cut.index->vertex(b[e]).position;
        }
        if(!sameEnds(positionedA,section.endpoints,epsilon)||
           !sameEnds(positionedB,section.endpoints,epsilon)||
           !sameEnds(positionedA,positionedB,epsilon))
            return reject("Materialized cut endpoints mismatch complete original curve");
        if(!tracedByChildEdges(children.at(contact.first),*cut.index,
                               positionedA[0],positionedA[1],epsilon,
                               out.edgeTraceTests,limits.maxEdgeTraceTests)||
           !tracedByChildEdges(children.at(contact.second),*cut.index,
                               positionedB[0],positionedB[1],epsilon,
                               out.edgeTraceTests,limits.maxEdgeTraceTests))
            return reject("An original intersection curve is not fully on BOTH child edge sets");

        for(const auto child:children.at(contact.first)) {
            if(out.childPairTests>=limits.maxChildPairTests)
                throw std::length_error("Cut coverage child-plane tests exhausted");
            ++out.childPairTests;
            if(!noInteriorContactCrossing(facePoints(*cut.index,child),
                                       originalB,positionedA[0],positionedA[1],epsilon))
                return reject("A child open interior still crosses a finite original contact");
        }
        for(const auto child:children.at(contact.second)) {
            if(out.childPairTests>=limits.maxChildPairTests)
                throw std::length_error("Cut coverage child-plane tests exhausted");
            ++out.childPairTests;
            if(!noInteriorContactCrossing(facePoints(*cut.index,child),
                                       originalA,positionedB[0],positionedB[1],epsilon))
                return reject("A reflected/second child still crosses a finite original contact");
        }
        ++out.checkedContacts;
    }

    // Child triangles within the SAME original face must not overlap in
    // positive area. Combined with existing V17c1 original-plane, area,
    // winding and region fan checks, this rules out overlapping planar
    // scaffolds masquerading as a valid complete face subdivision.
    for(const auto& [source,ids]:children) {
        (void)source;
        for(std::size_t i=0;i<ids.size();++i)
            for(std::size_t j=i+1;j<ids.size();++j) {
                if(out.childPairTests>=limits.maxChildPairTests)
                    throw std::length_error("Cut child overlap test budget exhausted");
                ++out.childPairTests;
                const auto kind=triangleContact(
                    facePoints(*cut.index,ids[i]),cut.index->face(ids[i]).vertices,
                    facePoints(*cut.index,ids[j]),cut.index->face(ids[j]).vertices);
                if(kind==TriangleContact::Ambiguous||
                   kind==TriangleContact::Intersecting)
                    return reject("Overlapping or ambiguous child triangles of one source");
            }
    }
    out.checkedChildren=count;
    out.coveredContacts=discovered;
    out.complete=true;
    return out;
}
} // namespace mesher
