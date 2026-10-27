#include "mesher/repair_cap_spatial.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {
namespace {
bool finite(Vec3 v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
}
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const auto sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}

}

// M22A: the fixed-rim Poisson stage depends on indexed connectivity and
// explicit interior guidance, NEVER on a ring vertex ordinal or a hub.
void solveSpatialCapFixedRimPoisson(
    SpatialCapPatch& patch,
    const std::vector<Vec3>& interiorGuidance,
    const SpatialCapSettings& limits,
    const std::vector<std::size_t>& mirrorPartners) {
    const auto n=patch.boundaryCount;
    if(n<3||n>limits.maxBoundaryVertices||
       patch.positions.size()<n||
       patch.positions.size()-n>limits.maxInteriorVertices||
       patch.faces.empty()||patch.faces.size()>limits.maxFaces||
       !limits.maxPoissonIterations||
       !std::isfinite(limits.poissonTolerance)||limits.poissonTolerance<=0||
       !std::isfinite(limits.normalGuidance)||
       limits.normalGuidance<0||limits.normalGuidance>.5||
       !std::isfinite(limits.placementAnchorWeight)||
       limits.placementAnchorWeight<0||
       limits.placementAnchorWeight>4||
       interiorGuidance.size()!=patch.positions.size()-n)
        throw std::invalid_argument("R06-M22A invalid generic Poisson input or finite bounds");

    for(const auto p:patch.positions)
        if(!finite(p))throw std::logic_error("R06-M22A nonfinite patch position");
    for(const auto direction:interiorGuidance)
        if(!finite(direction))
            throw std::logic_error("R06-M22A nonfinite interior guidance");
    std::vector<double> originalEdges;
    originalEdges.reserve(n);
    for(std::size_t i=0;i<n;++i)
        originalEdges.push_back(length(
            patch.positions[(i+1)%n]-patch.positions[i]));
    std::sort(originalEdges.begin(),originalEdges.end());
    const auto h=originalEdges[n/2];
    if(!(h>1e-12)||!std::isfinite(h))
        throw std::logic_error("R06-M22A degenerate fixed boundary scale");

    const auto reflect=[](Vec3 p) {return Vec3{-p.x,p.y,p.z};};
    if(limits.requireMirrorPlane) {
        if(mirrorPartners.size()!=patch.positions.size())
            throw std::logic_error("R06-M22A missing exact mirror partner map");
        for(std::size_t i=0;i<patch.positions.size();++i) {
            const auto j=mirrorPartners[i];
            if(j>=patch.positions.size()||mirrorPartners[j]!=i||
               ((i<n)!=(j<n)))
                throw std::logic_error("R06-M22A invalid reflected vertex orbit");
            const auto p=patch.positions[i],q=patch.positions[j];
            if(q.x!=-p.x||q.y!=p.y||q.z!=p.z||
               (j==i&&p.x!=0))
                throw std::logic_error("R06-M22A inaccurate reflected vertex positions");
            if(i>=n) {
                const auto a=interiorGuidance[i-n],b=interiorGuidance[j-n];
                if(b.x!=-a.x||b.y!=a.y||b.z!=a.z)
                    throw std::logic_error("R06-M22A non-reflected Poisson guidance");
            }
        }
    }else if(!mirrorPartners.empty()) {
        throw std::invalid_argument("R06-M22A unexpected asymmetric mirror mapping");
    }

    std::vector<std::set<std::uint32_t>> adjacent(patch.positions.size());
    for(const auto& face:patch.faces) {
        for(unsigned k=0;k<3;++k)
            if(face[k]>=patch.positions.size()||
               face[k]==face[(k+1)%3])
                throw std::logic_error("R06-M22A malformed patch face");
        for(unsigned k=0;k<3;++k) {
            const auto a=face[k],b=face[(k+1)%3];
            adjacent[a].insert(b);
            adjacent[b].insert(a);
        }
    }
    for(std::size_t i=n;i<patch.positions.size();++i)
        if(adjacent[i].empty())
            throw std::logic_error("R06-M22A orphan interior Poisson vertex");

    // Compute in a private copy; on any refusal the original caller's
    // patch remains bit-identical (including solver metadata).
    auto candidate=patch;
    const auto anchorWeight=limits.placementAnchorWeight;
    std::vector<Vec3> anchors;
    if(anchorWeight>0)anchors=patch.positions;
    const auto sourceAt=[&](std::size_t i) {
        const auto direction=interiorGuidance[i-n];
        const auto degree=static_cast<double>(adjacent[i].size());
        return direction*(limits.normalGuidance*h*degree);
    };
    bool converged=false;
    const auto tolerance=limits.poissonTolerance*h;
    for(std::size_t iteration=0;iteration<limits.maxPoissonIterations;++iteration) {
        double residual=0;
        if(limits.requireMirrorPlane) {
            // Jacobi updates and exact pair projection commute with reflection.
            auto next=candidate.positions;
            for(std::size_t i=n;i<candidate.positions.size();++i) {
                Vec3 rhs=sourceAt(i);
                for(const auto neighbor:adjacent[i])
                    rhs=rhs+candidate.positions[neighbor];
                if(anchorWeight>0)rhs=rhs+anchors[i]*anchorWeight;
                next[i]=rhs/(static_cast<double>(adjacent[i].size())+
                             anchorWeight);
                if(!finite(next[i]))
                    throw std::logic_error(
                        "R06-M22A mirrored Poisson nonfinite interior vertex");
            }
            for(std::size_t i=n;i<next.size();++i) {
                const auto j=mirrorPartners[i];
                if(j<i)continue;
                if(j==i) {
                    next[i].x=0;
                }else {
                    const auto mean=(next[i]+reflect(next[j]))*.5;
                    next[i]=mean;
                    next[j]=reflect(mean);
                }
            }
            for(std::size_t i=n;i<next.size();++i)
                residual=std::max(residual,
                    length(next[i]-candidate.positions[i]));
            candidate.positions=std::move(next);
        }else {
            for(std::size_t i=n;i<candidate.positions.size();++i) {
                Vec3 rhs=sourceAt(i);
                for(const auto neighbor:adjacent[i])
                    rhs=rhs+candidate.positions[neighbor];
                if(anchorWeight>0)rhs=rhs+anchors[i]*anchorWeight;
                const auto updated=rhs/
                    (static_cast<double>(adjacent[i].size())+anchorWeight);
                if(!finite(updated))
                    throw std::logic_error(
                        "R06-M22A Poisson produced nonfinite interior vertex");
                residual=std::max(residual,length(updated-candidate.positions[i]));
                candidate.positions[i]=updated;
            }
        }
        candidate.poissonIterations=iteration+1;
        candidate.poissonResidual=residual;
        if(residual<=tolerance){converged=true;break;}
    }
    if(!converged)
        throw std::logic_error("R06-M22A Poisson did not meet finite residual bound");
    for(std::size_t i=0;i<n;++i)
        if(candidate.positions[i].x!=patch.positions[i].x||
           candidate.positions[i].y!=patch.positions[i].y||
           candidate.positions[i].z!=patch.positions[i].z)
            throw std::logic_error("R06-M22A fixed R03 rim moved");
    patch=std::move(candidate);
}

// M22B: ordinary advancing-front construction. This produces the SAME
// SpatialCapPatch interface consumed by the existing source-backed M04 stage.
// All edits are local copies. The only mirror-specific rules are paired
// operations, a seam-vertex two-triangle turn, and no cross-plane edge.
namespace {
struct AdaptiveFrontState {
    SpatialCapPatch patch;
    std::vector<std::uint32_t> front;
    std::vector<std::size_t> twins;
    std::vector<Vec3> guides; // indexed by all positions (rim included)
    std::vector<std::uint8_t> originSides; // 0=unattributed ordinary cap
};

SpatialCapPatch constructAdaptiveFront(
    const std::vector<Vec3>& rim,
    const std::vector<Vec3>& normals,
    const std::vector<std::size_t>& boundaryTwins,
    const SpatialCapSettings& limits,
    const SpatialCapPlacementAdjustment* placementAdjustment,
    const std::vector<std::uint8_t>* rimSides) {
    const auto n=rim.size();
    if(n<3||n>limits.maxBoundaryVertices||
       !(limits.nearSeamSnapFraction>=0&&
         limits.nearSeamSnapFraction<=.5&&
         std::isfinite(limits.nearSeamSnapFraction)))
        throw std::invalid_argument("R06-M22B invalid advancing-front limits");
    AdaptiveFrontState state;
    state.patch.boundaryCount=n;
    state.patch.usedAdaptiveFront=true;
    state.patch.positions=rim;
    state.front.reserve(n+limits.maxInteriorVertices);
    state.guides.reserve(n+limits.maxInteriorVertices);
    state.twins.reserve(n+limits.maxInteriorVertices);
    state.originSides.reserve(n+limits.maxInteriorVertices);
    if(rimSides&&rimSides->size()!=n)
        throw std::invalid_argument("R06-M23B invalid seed-front side ancestry");
    Vec3 area{};
    for(std::size_t i=0;i<n;++i) {
        const auto j=(i+1)%n;
        state.front.push_back(static_cast<std::uint32_t>(i));
        area=area+cross(rim[i],rim[j]);
        auto g=normals.empty()?Vec3{}:normals[i];
        if(!finite(g))throw std::logic_error("R06-M22B nonfinite rim guidance");
        const auto m=length(g);
        state.guides.push_back(m>1e-12?g/m:Vec3{});
        state.twins.push_back(limits.requireMirrorPlane?boundaryTwins[i]:i);
        state.originSides.push_back(rimSides?rimSides->at(i):0);
    }
    const auto reflect=[](Vec3 p){return Vec3{-p.x,p.y,p.z};};
    const auto rimScale=[&]() {
        std::vector<double> lengths;
        lengths.reserve(n);
        for(std::size_t i=0;i<n;++i)
            lengths.push_back(length(rim[(i+1)%n]-rim[i]));
        std::sort(lengths.begin(),lengths.end());
        return lengths[n/2];
    }();
    if(!(rimScale>1e-12&&std::isfinite(rimScale)))
        throw std::logic_error("R06-M22B degenerate front scale");
    const auto normal=length(area)>1e-12?normalized(area):Vec3{0,0,1};
    const auto samePoint=[](Vec3 a,Vec3 b) {
        return a.x==b.x&&a.y==b.y&&a.z==b.z;
    };
    const auto addFace=[&](AdaptiveFrontState& s,
                          std::uint32_t a,std::uint32_t b,std::uint32_t d) {
        s.patch.faces.push_back({a,b,d});
    };
    std::size_t tests=0;
    std::size_t attempts=0;
    const auto legal=[&](const AdaptiveFrontState& s) {
        if(s.patch.faces.size()>limits.maxFaces||
           s.patch.positions.size()-n>limits.maxInteriorVertices||
           s.front.size()>limits.maxFaces+2)
            return false;
        std::set<std::uint32_t> unique;
        for(const auto id:s.front) {
            if(id>=s.patch.positions.size()||!unique.insert(id).second)
                return false;
        }
        if(limits.requireMirrorPlane) {
            if(s.twins.size()!=s.patch.positions.size())return false;
            for(std::size_t i=0;i<s.twins.size();++i) {
                const auto j=s.twins[i];
                if(j>=s.twins.size()||s.twins[j]!=i||
                   !samePoint(s.patch.positions[j],
                              reflect(s.patch.positions[i])))
                    return false;
            }
        }
        // No front edge may straddle X=0 without a real seam vertex.
        for(std::size_t i=0;i<s.front.size();++i) {
            const auto a=s.patch.positions[s.front[i]];
            const auto b=s.patch.positions[s.front[(i+1)%s.front.size()]];
            if(limits.requireMirrorPlane&&a.x*b.x<0)return false;
        }
        for(const auto& face:s.patch.faces) {
            if(face[0]>=s.patch.positions.size()||
               face[1]>=s.patch.positions.size()||
               face[2]>=s.patch.positions.size())
                return false;
            const auto a=s.patch.positions[face[0]];
            const auto b=s.patch.positions[face[1]];
            const auto d=s.patch.positions[face[2]];
            if(limits.requireMirrorPlane&&
               std::min({a.x,b.x,d.x})<0&&
               std::max({a.x,b.x,d.x})>0)return false;
            if(!(quality(a,b,d)>1e-8))return false;
        }
        // Bounded contact checks on generated faces, including intersections
        // caused by otherwise plausible but folded advancing-front turns.
        for(std::size_t i=0;i<s.patch.faces.size();++i)
        for(std::size_t j=i+1;j<s.patch.faces.size();++j) {
            // Previous committed faces were already checked. Only contact
            // pairs involving this candidate's new faces need rechecking.
            if(i<state.patch.faces.size()&&
               j<state.patch.faces.size())continue;
            std::array<Vec3,3> p{},q{};
            std::array<VertexId,3> pi{},qi{};
            for(unsigned k=0;k<3;++k) {
                pi[k]=static_cast<VertexId>(s.patch.faces[i][k])+1;
                qi[k]=static_cast<VertexId>(s.patch.faces[j][k])+1;
                p[k]=s.patch.positions[s.patch.faces[i][k]];
                q[k]=s.patch.positions[s.patch.faces[j][k]];
            }
            // Cheap conservative broad-phase: a spatially disjoint AABB
            // proves noncontact and does not spend a narrow-phase test.
            const auto apart=[&](auto coordinate) {
                const auto amin=std::min({coordinate(p[0]),coordinate(p[1]),
                                          coordinate(p[2])});
                const auto amax=std::max({coordinate(p[0]),coordinate(p[1]),
                                          coordinate(p[2])});
                const auto bmin=std::min({coordinate(q[0]),coordinate(q[1]),
                                          coordinate(q[2])});
                const auto bmax=std::max({coordinate(q[0]),coordinate(q[1]),
                                          coordinate(q[2])});
                return amax<bmin||bmax<amin;
            };
            if(apart([](Vec3 v){return v.x;})||
               apart([](Vec3 v){return v.y;})||
               apart([](Vec3 v){return v.z;}))continue;
            if(++tests>limits.maxIntersectionTests)
                throw std::length_error(
                    "R06-M22B advancing-front contact budget front="+
                    std::to_string(s.front.size())+
                    " faces="+std::to_string(s.patch.faces.size())+
                    " attempts="+std::to_string(attempts));
            const auto hit=triangleContact(p,pi,q,qi);
            if(hit!=TriangleContact::Disjoint&&
               hit!=TriangleContact::ExpectedAdjacency)return false;
        }
        return true;
    };
    // Work budget includes rejected proposals: never retry indefinitely.
    const auto maxAttempts=std::min<std::size_t>(
        limits.maxFaces*8+limits.maxBoundaryVertices*8,65536);
    // Every adaptive insertion must be followed by enough convex ear
    // closure to reduce the FRONT below its last committed size. Merely
    // insisting on one ear after two insertions permits net growth.
    bool requireNetReduction=false;
    std::size_t nextFrontTarget=n-1;
    while(state.front.size()>3) {
        if(++attempts>maxAttempts)
            throw std::length_error("R06-M22B advancing-front attempt budget");
        // Exact mirrored terminal: the final two half-disks close by one
        // shared real seam edge, NEVER by a crossing-plane triangle.
        if(limits.requireMirrorPlane&&state.front.size()==4) {
            std::vector<std::uint32_t> seams,off;
            for(const auto id:state.front)
                (state.patch.positions[id].x==0?seams:off).push_back(id);
            if(seams.size()==2&&off.size()==2&&
               state.twins[off[0]]==off[1]) {
                auto trial=state;
                // Find one candidate half triangle from cyclic front order.
                bool closed=false;
                for(std::size_t i=0;i<4&&!closed;++i) {
                    const auto a=trial.front[i];
                    const auto b=trial.front[(i+1)%4];
                    const auto d=trial.front[(i+2)%4];
                    if(trial.patch.positions[a].x==0&&
                       trial.patch.positions[d].x==0&&
                       trial.patch.positions[b].x!=0) {
                        addFace(trial,a,b,d);
                        addFace(trial,
                                static_cast<std::uint32_t>(state.twins[d]),
                                static_cast<std::uint32_t>(state.twins[b]),
                                static_cast<std::uint32_t>(state.twins[a]));
                        trial.front.clear();
                        if(legal(trial)){state=std::move(trial);closed=true;}
                        else throw std::logic_error(
                            "R06-M22B symmetric terminal half-disks not embedded");
                    }
                }
                if(closed)break;
            }
        }
        // Advance using the surface that has ACTUALLY been generated,
        // rather than classifying every corner against the starting rim's
        // fixed area vector. The latter can point nearly sideways to a
        // nearly closed, nonplanar front (refusal-3's six-vertex endgame).
        // Use the same accumulated accepted-face normals for both ordinary
        // caps and zipper fronts. No trial face contributes to its own
        // classification; vertices without accepted support retain their
        // existing incoming guidance.
        std::vector<Vec3> currentNormals(
            state.patch.positions.size(),Vec3{});
        for(const auto& face:state.patch.faces) {
            const auto p=state.patch.positions[face[0]];
            const auto q=state.patch.positions[face[1]];
            const auto r=state.patch.positions[face[2]];
            const auto nFace=cross(q-p,r-p);
            for(const auto id:face)
                currentNormals[id]=currentNormals[id]+nFace;
        }
        for(std::size_t id=0;id<currentNormals.size();++id) {
            auto nFace=currentNormals[id];
            if(limits.requireMirrorPlane&&state.patch.positions[id].x==0)
                nFace.x=0;
            const auto magnitude=length(nFace);
            if(magnitude>1e-12*rimScale*rimScale&&finite(nFace))
                currentNormals[id]=nFace/magnitude;
            else currentNormals[id]=state.guides[id];
        }
        // Evaluate all corners, preferring the smallest oriented angle.
        // Reflected off-seam corners form one indivisible orbit.
        struct Corner {std::size_t index;double angle;};
        std::vector<Corner> choices;
        for(std::size_t i=0;i<state.front.size();++i) {
            const auto id=state.front[i];
            if(limits.requireMirrorPlane&&state.twins[id]<id)continue;
            const auto a=state.patch.positions[state.front[
                (i+state.front.size()-1)%state.front.size()]];
            const auto b=state.patch.positions[id];
            const auto d=state.patch.positions[state.front[
                (i+1)%state.front.size()]];
            const auto u=a-b,v=d-b;
            if(!(length(u)>1e-12&&length(v)>1e-12))continue;
            double theta=std::acos(std::clamp(
                dot(u,v)/(length(u)*length(v)),-1.,1.));
            // Both cap and zipper corners use their evolving geometric
            // support. The initial rim area vector is only a fallback
            // when neither accepted faces nor incoming guidance exists.
            const auto cornerNormal=
                length(currentNormals[id])>1e-12?
                    currentNormals[id]:normal;
            if(dot(cross(u,v),cornerNormal)>1e-10*length(u)*length(v))
                theta=2*std::acos(-1.)-theta;
            choices.push_back({i,theta});
        }
        std::stable_sort(choices.begin(),choices.end(),
            [](const Corner& a,const Corner& b) {
                if(a.angle!=b.angle)return a.angle<b.angle;
                return a.index<b.index;
            });
        bool accepted=false;
        for(const auto& choice:choices) {
            if(accepted)break;
            const auto size=state.front.size();
            const auto at=choice.index;
            const auto vId=state.front[at];
            const auto aId=state.front[(at+size-1)%size];
            const auto bId=state.front[(at+1)%size];
            const auto vertex=state.patch.positions[vId];
            const auto a=state.patch.positions[aId];
            const auto b=state.patch.positions[bId];
            const auto ua=a-vertex,ub=b-vertex;
            const auto la=length(ua),lb=length(ub);
            if(!(la>1e-12&&lb>1e-12))continue;
            const auto dirA=ua/la,dirB=ub/lb;
            const auto bisector=dirA+dirB;
            const auto bisectorLen=length(bisector);
            const bool seam=limits.requireMirrorPlane&&vertex.x==0;
            const auto pi=std::acos(-1.);
            unsigned mode=seam?2:
                (choice.angle<=75*pi/180?1:
                 choice.angle<=135*pi/180?2:3);
            // The paper's 2/3-face turns can leave the front unchanged
            // or grow it. Once one is used, close convex ears until the
            // new front is STRICTLY smaller than it was before that turn.
            // A mirror-plane vertex retains its compulsory two-face mode.
            if(!seam&&requireNetReduction&&choice.angle<pi)
                mode=1;
            if(mode!=1&&!(bisectorLen>1e-10))continue;
            for(const double factor:{.7,1.0,.45}) {
                if(accepted)break;
                if(++attempts>maxAttempts)
                    throw std::length_error("R06-M22B front proposal budget");
                AdaptiveFrontState trial=state;
                std::vector<std::uint32_t> replacement;
                std::vector<std::array<std::uint32_t,3>> localFaces;
                if(mode==1) {
                    localFaces.push_back({aId,vId,bId});
                }else {
                    std::vector<Vec3> proposals;
                    if(seam&&choice.angle<75*pi/180) {
                        // Two mirrored right triangles, seam midpoint.
                        proposals.push_back({0,(a.y+b.y)*.5,(a.z+b.z)*.5});
                    }else if(mode==2) {
                        auto q=vertex+bisector/bisectorLen*
                            (factor*std::min(la,lb));
                        if(seam)q.x=0;
                        proposals.push_back(q);
                    }else {
                        auto first=dirA*2.+dirB;
                        auto second=dirA+dirB*2.;
                        if(!(length(first)>1e-12&&length(second)>1e-12))
                            continue;
                        proposals.push_back(vertex+normalized(first)*
                            (factor*std::min(la,lb)));
                        proposals.push_back(vertex+normalized(second)*
                            (factor*std::min(la,lb)));
                    }
                    bool rejected=false;
                    for(std::size_t ordinal=0;ordinal<proposals.size();++ordinal) {
                        auto q=proposals[ordinal];
                        SpatialCapPlacementTrace trace;
                        if(rimSides) {
                            trace.rawProposal=q;
                            trace.frontVertex=vId;
                            trace.frontPrev=aId;
                            trace.frontNext=bId;
                            trace.proposalOrdinal=ordinal;
                            trace.frontOperation=state.patch.faces.size();
                            trace.originatingSide=state.originSides[vId];
                            trace.tangentNormal=currentNormals[vId];
                            // Capture the actual already-created supporting
                            // faces at this front edge, not hypothetical
                            // normals or future faces. Bridge seed triangles
                            // are outside this cap generator's face list.
                            for(const auto& face:state.patch.faces) {
                                const auto sharesEdge=[&](std::uint32_t x,
                                                          std::uint32_t y) {
                                    bool hasX=false,hasY=false;
                                    for(const auto id:face) {
                                        hasX=hasX||id==x;
                                        hasY=hasY||id==y;
                                    }
                                    return hasX&&hasY;
                                };
                                if(!sharesEdge(aId,vId)&&
                                   !sharesEdge(vId,bId))continue;
                                if(trace.supportCount==trace.supportFaces.size())
                                    break;
                                const auto k=trace.supportCount++;
                                trace.supportFaces[k]=face;
                                for(std::size_t corner=0;corner<3;++corner)
                                    trace.supportAtProposal[k][corner]=
                                        state.patch.positions[face[corner]];
                            }
                        }
                        // M23B: an advancing front follows its LOCAL tangent
                        // plane; the zipper's gravitational callback then
                        // bends the point toward the opposing side. Preserve
                        // the original proposed edge length, just change its
                        // direction. This applies to both inserted vertices.
                        if(rimSides&&length(currentNormals[vId])>1e-12) {
                            const auto surfaceNormal=currentNormals[vId];
                            const auto d=q-vertex;
                            const auto projected=d-
                                surfaceNormal*dot(d,surfaceNormal);
                            const auto along=length(projected);
                            const auto step=length(d);
                            if(!(along>1e-10*step)&&step>0) {
                                rejected=true;break;
                            }
                            if(along>0)q=vertex+projected*(step/along);
                            if(seam)q.x=0;
                        }
                        if(rimSides)trace.tangentProposal=q;
                        // M23A: zero work and identical floating-point behavior
                        // for the null path. This hook is used only for proposed
                        // new positions, BEFORE any snap, contact or face test.
                        if(placementAdjustment) {
                            q=(*placementAdjustment)(SpatialCapPlacementProposal{
                                q,vertex,a,b,std::min(la,lb),
                                at,vId,aId,bId,mode,ordinal,
                                state.originSides[vId],&state.patch.positions,
                                &state.front,&state.originSides,
                                rimSides?currentNormals[vId]:Vec3{}});
                        }
                        if(rimSides)trace.gravityProposal=q;
                        if(!finite(q)){rejected=true;break;}
                        if(limits.requireMirrorPlane&&!seam) {
                            const auto h=std::min(la,lb);
                            const bool crossing=vertex.x*q.x<0;
                            if(crossing||std::abs(q.x)<=
                               limits.nearSeamSnapFraction*h) {
                                // Distinct *candidate* positions; do not
                                // force a topologically pinched seam.
                                if(crossing) {
                                    const auto t=vertex.x/(vertex.x-q.x);
                                    q=vertex+(q-vertex)*t;
                                }
                                q.x=0;
                            }
                        }
                        if(length(q-vertex)<1e-10*rimScale) {
                            rejected=true;break;
                        }
                        const auto id=static_cast<std::uint32_t>(
                            trial.patch.positions.size());
                        if(rimSides) {
                            trace.patchVertex=id;
                            trace.stagedProposal=q;
                            trial.patch.placementTrace.push_back(trace);
                        }
                        trial.patch.positions.push_back(q);
                        trial.guides.push_back(state.guides[vId]);
                        trial.twins.push_back(id);
                        trial.originSides.push_back(state.originSides[vId]);
                        if(limits.requireMirrorPlane&&q.x!=0) {
                            const auto mate=static_cast<std::uint32_t>(
                                trial.patch.positions.size());
                            trial.patch.positions.push_back(reflect(q));
                            trial.guides.push_back(reflect(state.guides[vId]));
                            trial.twins.push_back(id);
                            trial.originSides.push_back(
                                state.originSides[vId]==1?2:
                                state.originSides[vId]==2?1:0);
                            trial.twins[id]=mate;
                            if(rimSides) {
                                auto reflectedTrace=trace;
                                reflectedTrace.patchVertex=mate;
                                reflectedTrace.frontVertex=
                                    static_cast<std::uint32_t>(
                                        state.twins[vId]);
                                reflectedTrace.frontPrev=
                                    static_cast<std::uint32_t>(
                                        state.twins[bId]);
                                reflectedTrace.frontNext=
                                    static_cast<std::uint32_t>(
                                        state.twins[aId]);
                                reflectedTrace.originatingSide=
                                    trace.originatingSide==1?2:
                                    trace.originatingSide==2?1:0;
                                reflectedTrace.rawProposal=reflect(trace.rawProposal);
                                reflectedTrace.tangentProposal=reflect(trace.tangentProposal);
                                reflectedTrace.gravityProposal=reflect(trace.gravityProposal);
                                reflectedTrace.stagedProposal=reflect(trace.stagedProposal);
                                reflectedTrace.tangentNormal=reflect(trace.tangentNormal);
                                for(std::size_t k=0;k<trace.supportCount;++k) {
                                    const auto& support=trace.supportFaces[k];
                                    for(std::size_t c=0;c<3;++c) {
                                        reflectedTrace.supportFaces[k][c]=
                                            static_cast<std::uint32_t>(
                                                state.twins[support[c]]);
                                        reflectedTrace.supportAtProposal[k][c]=
                                            reflect(trace.supportAtProposal[k][c]);
                                    }
                                }
                                trial.patch.placementTrace.push_back(
                                    std::move(reflectedTrace));
                            }
                        }else if(limits.requireMirrorPlane) {
                            trial.guides[id].x=0;
                        }
                        replacement.push_back(id);
                    }
                    if(rejected)continue;
                    if(mode==2) {
                        localFaces.push_back({aId,vId,replacement[0]});
                        localFaces.push_back({vId,bId,replacement[0]});
                    }else {
                        localFaces.push_back({aId,vId,replacement[0]});
                        localFaces.push_back({vId,replacement[1],replacement[0]});
                        localFaces.push_back({vId,bId,replacement[1]});
                    }
                }
                for(const auto& tri:localFaces) {
                    addFace(trial,tri[0],tri[1],tri[2]);
                    if(limits.requireMirrorPlane&&!seam)
                        addFace(trial,
                            static_cast<std::uint32_t>(trial.twins[tri[2]]),
                            static_cast<std::uint32_t>(trial.twins[tri[1]]),
                            static_cast<std::uint32_t>(trial.twins[tri[0]]));
                }
                // Replace the selected front corner and its mirrored
                // counterpart in the SAME original cyclic walk.
                std::vector<std::uint32_t> newFront;
                newFront.reserve(size+4);
                for(std::size_t j=0;j<size;++j) {
                    const auto old=state.front[j];
                    if(j==at) {
                        newFront.insert(newFront.end(),
                            replacement.begin(),replacement.end());
                    }else if(limits.requireMirrorPlane&&!seam&&
                             old==state.twins[vId]) {
                        for(auto it=replacement.rbegin();it!=replacement.rend();++it)
                            newFront.push_back(static_cast<std::uint32_t>(
                                trial.twins[*it]));
                    }else newFront.push_back(old);
                }
                trial.front=std::move(newFront);
                if(!legal(trial))continue;
                if(trial.front.size()<=nextFrontTarget) {
                    requireNetReduction=false;
                    nextFrontTarget=trial.front.empty()?0:trial.front.size()-1;
                }else requireNetReduction=true;
                state=std::move(trial);
                accepted=true;
            }
        }
        if(!accepted) {
            if(rimSides) {
                throw std::logic_error(
                    "R06-M23B advancing-front dead end front="+
                    std::to_string(state.front.size())+
                    " seed_boundary="+std::to_string(n)+
                    " generated_faces="+std::to_string(state.patch.faces.size())+
                    " attempts="+std::to_string(attempts)+
                    " require_mirror="+std::to_string(limits.requireMirrorPlane));
            }
            throw std::logic_error(
                "R06-M22B no admissible advancing-front mirrored operation");
        }
    }
    if(state.front.size()==3) {
        if(limits.requireMirrorPlane)
            throw std::logic_error("R06-M22B symmetric front reached odd terminal face");
        addFace(state,state.front[0],state.front[1],state.front[2]);
        state.front.clear();
    }
    if(!state.front.empty()||!legal(state))
        throw std::logic_error("R06-M22B final advancing-front patch invalid");
    std::vector<Vec3> interior;
    interior.reserve(state.patch.positions.size()-n);
    for(std::size_t i=n;i<state.patch.positions.size();++i)
        interior.push_back(state.guides[i]);
    solveSpatialCapFixedRimPoisson(state.patch,interior,limits,
        limits.requireMirrorPlane?state.twins:
            std::vector<std::size_t>{});
    if(rimSides) {
        for(auto& trace:state.patch.placementTrace)
            trace.poissonPosition=state.patch.positions.at(trace.patchVertex);
    }
    return state.patch;
}
} // namespace

SpatialCapPatch buildSpatialCapFrontPoisson(
    const std::vector<Vec3>& orientedCapRim,
    const std::vector<Vec3>& survivingFaceNormals,
    const SpatialCapSettings& limits,
    const SpatialCapPlacementAdjustment* placementAdjustment,
    const std::vector<std::uint8_t>* boundarySides) {
    const auto n=orientedCapRim.size();
    if(!limits.useAdvancingFront)
        throw std::invalid_argument(
            "R06 retired ring-and-hub cap: advancing front is required");
    if(n<3||n>limits.maxBoundaryVertices||
       !limits.maxPoissonIterations||!limits.maxIntersectionTests||
       !std::isfinite(limits.poissonTolerance)||
       !std::isfinite(limits.maximumInitialEdgeFactor)||
       !std::isfinite(limits.normalGuidance)||
       limits.poissonTolerance<=0||limits.maximumInitialEdgeFactor<=1||
       limits.normalGuidance<0||limits.normalGuidance>.5)
        throw std::length_error("R06-M22B advancing-front finite-work limits refused");
    if(!survivingFaceNormals.empty()&&survivingFaceNormals.size()!=n)
        throw std::invalid_argument("R06-M06C front normals not indexed to cap rim");
    if(placementAdjustment&&!limits.useAdvancingFront)
        throw std::invalid_argument("R06-M23A placement adjustment requires advancing-front mode");
    if(boundarySides&&(!limits.useAdvancingFront||
       boundarySides->size()!=n||
       std::any_of(boundarySides->begin(),boundarySides->end(),
           [](std::uint8_t side){return side>2;})))
        throw std::invalid_argument("R06-M23B invalid advancing-front ancestry");

    SpatialCapPatch result;
    result.boundaryCount=n;
    result.positions=orientedCapRim;
    Vec3 center{};
    std::vector<double> originalEdges;
    originalEdges.reserve(n);
    for(std::size_t i=0;i<n;++i) {
        const auto a=orientedCapRim[i];
        const auto b=orientedCapRim[(i+1)%n];
        if(!finite(a)||!finite(b))
            throw std::logic_error("R06-M06C front nonfinite rim position");
        center=center+a;
        originalEdges.push_back(length(b-a));
    }
    center=center/static_cast<double>(n);
    if(limits.requireMirrorPlane)center.x=0;
    std::sort(originalEdges.begin(),originalEdges.end());
    const auto h=originalEdges[n/2];
    if(!(h>1e-12)||!std::isfinite(h))
        throw std::logic_error("R06-M06C front degenerate edge scale");

    const auto reflect=[](Vec3 p) {return Vec3{-p.x,p.y,p.z};};
    std::vector<std::size_t> partner(2*n+1);
    if(limits.requireMirrorPlane) {
        std::size_t seam=0;
        for(std::size_t i=0;i<n;++i) {
            const auto p=orientedCapRim[i];
            if(p.x==0)++seam;
            std::size_t match=n;
            for(std::size_t j=0;j<n;++j) {
                const auto q=orientedCapRim[j];
                // Do not infer topology from approximate mirror proximity.
                // Exact source-symmetry coordinates are necessary here.
                if(q.x!=-p.x||q.y!=p.y||q.z!=p.z)continue;
                if(match!=n)
                    throw std::logic_error(
                        "R06-M06C ambiguous mirrored boundary identity");
                match=j;
            }
            if(match==n)
                throw std::logic_error(
                    "R06-M06C missing exact mirrored boundary vertex");
            partner[i]=match;
        }
        if(seam!=2)
            throw std::logic_error(
                "R06-M06C reflected spatial disk requires exactly two existing X=0 seam vertices");
        for(std::size_t i=0;i<n;++i) {
            if(partner[partner[i]]!=i)
                throw std::logic_error(
                    "R06-M06C noninvolutive reflected rim orbit");
            const auto j=(i+1)%n;
            if(orientedCapRim[i].x*orientedCapRim[j].x<0)
                throw std::logic_error(
                    "R06-M06C boundary edge crosses X=0 without an actual seam vertex");
            partner[n+i]=n+partner[i];
        }
        partner[2*n]=2*n;
    }

    {
        auto patch=constructAdaptiveFront(
            orientedCapRim,survivingFaceNormals,partner,limits,
            placementAdjustment,boundarySides);
        // Generic AFM patches have no positional one-to-one n-ring or hub.
        // Intentionally bypass M17–M21 ring-index optimizations here.
        std::size_t tests=0;
        patch.minimumQuality=1.;
        for(const auto& face:patch.faces) {
            const auto a=patch.positions[face[0]],b=patch.positions[face[1]],
                       c=patch.positions[face[2]];
            const auto q=quality(a,b,c);
            if(!(q>1e-8&&std::isfinite(q)))
                throw std::logic_error("R06-M22B Poisson collapsed advancing-front face");
            patch.minimumQuality=std::min(patch.minimumQuality,q);
            if(std::max({length(a-b),length(b-c),length(c-a)})>
               h*limits.maximumInitialEdgeFactor)
                throw std::logic_error("R06-M22B advancing-front edge scale exceeded");
        }
        for(std::size_t x=0;x<patch.faces.size();++x)
        for(std::size_t y=x+1;y<patch.faces.size();++y) {
            if(++tests>limits.maxIntersectionTests)
                throw std::length_error("R06-M22B final cap contact budget");
            std::array<Vec3,3> a{},b{};
            std::array<VertexId,3> ai{},bi{};
            for(unsigned k=0;k<3;++k) {
                ai[k]=patch.faces[x][k]+1;
                bi[k]=patch.faces[y][k]+1;
                a[k]=patch.positions[patch.faces[x][k]];
                b[k]=patch.positions[patch.faces[y][k]];
            }
            const auto contact=triangleContact(a,ai,b,bi);
            if(contact!=TriangleContact::Disjoint&&
               contact!=TriangleContact::ExpectedAdjacency)
                throw std::logic_error("R06-M22B settled advancing-front self contact");
        }
        return patch;
    }

}
} // namespace mesher
