#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
using Kind=RegionTopologyIndex::FragmentKind;
Edge key(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
bool exact(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}
bool reflected(Vec3 a,Vec3 b) {
    return a.x==-b.x&&a.y==b.y&&a.z==b.z;
}
ExactCutJoinResult refused(const std::string& message) {
    ExactCutJoinResult result;
    result.refusal=message;
    return result; // no partial revision, commit, or approved vertex map
}
struct Boundary {
    VertexId from{},to{}; // survivor's original oriented edge
    FaceId survivor{},source{};
    bool accounted{};
};
struct Arc {
    VertexId from{},to{};
    double lo{},hi{};
    bool increasing{};
    std::size_t boundaryIndex{};
};
std::vector<Arc> trace(
    const RegionTopologyIndex& index,std::vector<Boundary>& boundary,
    FaceId original,Vec3 start,Vec3 end,
    std::size_t& tested,std::size_t maximum) {
    const auto vector=end-start;
    const auto length0=length(vector);
    if(!(length0>0)||!std::isfinite(length0))
        throw std::logic_error("Contact section has invalid line length");
    const auto direction=vector/length0;
    const auto epsilon=std::max(1e-10*length0,1e-12);
    const auto project=[&](Vec3 point) {
        return dot(point-start,direction);
    };
    const auto onSegment=[&](Vec3 point) {
        const auto t=project(point);
        return std::isfinite(t)&&t>=-epsilon&&t<=length0+epsilon&&
               length(point-(start+direction*t))<=epsilon;
    };
    std::vector<Arc> found;
    for(std::size_t i=0;i<boundary.size();++i) {
        if(tested>=maximum)
            throw std::length_error("V17 seam edge trace budget exhausted");
        ++tested;
        const auto& b=boundary[i];
        if(b.source!=original)continue;
        const auto a=index.vertex(b.from).position,
                   c=index.vertex(b.to).position;
        if(!onSegment(a)||!onSegment(c))continue;
        const auto u=project(a),v=project(c);
        if(std::abs(u-v)<=epsilon)
            throw std::logic_error("Degenerate source seam boundary edge");
        found.push_back({b.from,b.to,std::min(u,v),
                         std::max(u,v),u<v,i});
    }
    std::sort(found.begin(),found.end(),[](const Arc& a,const Arc& b) {
        if(a.lo!=b.lo)return a.lo<b.lo;
        return a.hi<b.hi;
    });
    // In a multi-solid union an original contact section can pass through
    // a THIRD occupied solid. Its buried subintervals have no surviving
    // removal boundary on either side, even though the c2 certificate
    // must still trace the ENTIRE original curve along both cut sheets.
    // Collect only actual c3-directed survivor boundaries here. Intervals
    // may have gaps, but never overlap (duplicate ownership). The caller
    // independently requires matching intervals and opposite directions
    // on the two original sheets before any seam weld can be authorized.
    double previousHigh=-epsilon;
    for(const auto& arc:found) {
        if(arc.lo< -epsilon||arc.hi>length0+epsilon||
           arc.lo<previousHigh-epsilon)
            throw std::logic_error(
                "Original section has duplicate, overlapping or off-section boundary interval");
        previousHigh=arc.hi;
    }
    return found;
}
std::map<VertexId,VertexId> mirrorMap(
    const ConformingCutCandidate& cut,
    const std::unordered_map<VertexId,VertexId>& original) {
    std::map<VertexId,VertexId> result;
    const auto add=[&](VertexId a,VertexId b,bool old) {
        if(a==b||!cut.index->maybeVertex(a)||!cut.index->maybeVertex(b)||
           !reflected(cut.index->vertex(a).position,
                      cut.index->vertex(b).position)||
           (old&&(!cut.originalIndex->maybeVertex(a)||
                  !cut.originalIndex->maybeVertex(b)))||
           (!old&&(cut.originalIndex->maybeVertex(a)||
                   cut.originalIndex->maybeVertex(b)))||
           (result.contains(a)&&result.at(a)!=b)||
           (result.contains(b)&&result.at(b)!=a))
            throw std::logic_error("Missing or conflicting exact cut vertex mirror orbit");
        result[a]=b;result[b]=a;
    };
    for(const auto& [a,b]:original) {
        if(!original.contains(b)||original.at(b)!=a)
            throw std::logic_error("Historical mirror mapping is not reciprocal");
        add(a,b,true);
    }
    for(const auto& [a,b]:cut.mirroredVertices)add(a,b,false);
    if(cut.insertedVertices.size()!=cut.mirroredVertices.size()*2)
        throw std::logic_error("Mirror cut does not account for every new vertex orbit");
    return result;
}
} // namespace

ExactCutJoinResult joinCertifiedCutSections(
    const ConformingCutCandidate& cut,
    const ExactCutJoinLimits& limits,
    const std::unordered_map<VertexId,VertexId>* originalMirrors) {
    try {
        if(!cut.index||!cut.originalIndex||!cut.commit.revision||
           cut.commit.revision.get()!=&cut.index->revision()||
           !limits.partitionCapacity||!limits.maxBoundaryEdges||
           !limits.maxSeamPairs||!limits.maxTraceTests||
           !limits.maxRewrittenFaces||
           !limits.maxCandidateFaces)
            return refused("Missing exact private cut root or invalid seam budgets");
        const auto nomination=planExactCutRemoval(cut,limits.removal);
        if(!nomination.ready()||nomination.proposedRemoval.empty())
            return refused("Seam join needs c3 Ready exact child selection: "+
                           nomination.refusal);
        const bool mirrored=!cut.mirroredFaces.empty()||
                            !cut.mirroredVertices.empty();
        if(mirrored!=(originalMirrors!=nullptr))
            return refused("Mirrored seam join needs explicit original vertex mirror involution");
        const auto mirrors=mirrored?mirrorMap(cut,*originalMirrors):
                                      std::map<VertexId,VertexId>{};
        const auto& index=*cut.index;
        std::set<FaceId> deleted(nomination.proposedRemoval.begin(),
                                 nomination.proposedRemoval.end());
        std::map<FaceId,FaceId> sources;
        for(const auto& [child,source]:cut.childSources)
            if(!sources.emplace(child,source).second)
                return refused("Duplicate cut child provenance");
        std::vector<Boundary> boundary;
        std::set<Edge> uniqueBoundary;
        for(const auto face:deleted) {
            if(!index.maybeFace(face))
                return refused("C3 authorized a stale FaceId");
            const auto tri=index.face(face).vertices;
            for(unsigned i=0;i<3;++i) {
                const auto edge=key(tri[i],tri[(i+1)%3]);
                const auto sides=index.maybeEdgeSides(edge);
                if(!sides||sides->size()!=2)
                    return refused("Removed source has open/nonmanifold boundary");
                FaceId survivor{};
                for(const auto& side:*sides)
                    if(!deleted.contains(side.face))survivor=side.face;
                if(!survivor)continue;
                if(!uniqueBoundary.insert(edge).second||
                   !sources.contains(survivor))
                    return refused("Seam boundary escaped original cut child provenance");
                const auto current=index.face(survivor).vertices;
                bool found=false;
                for(unsigned k=0;k<3;++k)
                    if(key(current[k],current[(k+1)%3])==edge) {
                        boundary.push_back({current[k],current[(k+1)%3],
                                            survivor,sources.at(survivor),false});
                        found=true;
                    }
                if(!found)return refused("Survivor edge orientation vanished");
                if(boundary.size()>limits.maxBoundaryEdges)
                    throw std::length_error("V17 seam boundary count exceeded budget");
            }
        }
        if(boundary.empty())
            return refused("No open intersection seam follows authorized face retirement");

        // Correspondences are introduced ONLY by a known V13 source pair
        // and certified child-EDGE intervals on BOTH independently represented
        // sheets. A third shell may bury a section subinterval; matching
        // survivor intervals still require identical subdivisions/opposite
        // winding. The final closed-manifold, complete c2 and whole-halo
        // collision audits remain mandatory. No coordinate hash or
        // nearest-vertex welding.
        std::map<VertexId,VertexId> parent;
        const auto root=[&](VertexId v) {
            while(parent.contains(v)&&parent.at(v)!=v)v=parent.at(v);
            return v;
        };
        const auto join=[&](VertexId a,VertexId b) {
            if(!exact(index.vertex(a).position,index.vertex(b).position))
                throw std::logic_error("Certified section counterparts are not EXACTLY coincident");
            const auto x=root(a),y=root(b);
            if(x==y)return;
            if(!parent.contains(x))parent[x]=x;
            if(!parent.contains(y))parent[y]=y;
            parent[y]=x;
        };
        std::size_t traceTests=0,matchedPairs=0;
        for(std::size_t k=0;k<cut.sourceContacts.size();++k) {
            const auto& pair=cut.sourceContacts[k];
            const auto sectionA=cut.sectionFirst.at(k);
            const auto sectionB=cut.sectionSecond.at(k);
            if(!sources.contains(pair.first)&&!sources.contains(pair.second))
                return refused("Contact source has no cut children");
            const auto start=index.vertex(sectionA[0]).position,
                       end=index.vertex(sectionA[1]).position;
            if(!exact(start,index.vertex(sectionB[0]).position)||
               !exact(end,index.vertex(sectionB[1]).position))
                return refused("Independent source section endpoints differ exactly");
            auto first=trace(index,boundary,pair.first,start,end,
                             traceTests,limits.maxTraceTests);
            auto second=trace(index,boundary,pair.second,start,end,
                              traceTests,limits.maxTraceTests);
            if(first.size()!=second.size())
                return refused("Independently subdivided source section edge counts differ, contact "+
                    std::to_string(k)+": "+
                    std::to_string(first.size())+" versus "+
                    std::to_string(second.size()));
            for(std::size_t i=0;i<first.size();++i) {
                const auto& a=first[i],&b=second[i];
                // Projection coordinates use independent floating-point
                // dot products, so exactly coincident 3D section vertices
                // can yield slightly different scalar parameters. The
                // stable FaceId/V13 source pairing authorizes comparison,
                // but ONLY exact 3D endpoint equality authorizes welding.
                const auto aLow=a.increasing?a.from:a.to;
                const auto aHigh=a.increasing?a.to:a.from;
                const auto bLow=b.increasing?b.from:b.to;
                const auto bHigh=b.increasing?b.to:b.from;
                if(a.increasing==b.increasing||
                   !exact(index.vertex(aLow).position,index.vertex(bLow).position)||
                   !exact(index.vertex(aHigh).position,index.vertex(bHigh).position)) {
                    // Preserve full double precision in diagnostics:
                    // decimal-rounded interval labels can look identical
                    // even when the strict protected-corner match fails.
                    std::ostringstream why;
                    why<<std::setprecision(17)
                       <<"Unmatched subdivision or aligned winding along contact "
                       <<k<<" interval "<<i<<": A["<<a.lo<<","<<a.hi
                       <<"] B["<<b.lo<<","<<b.hi<<"] directions "
                       <<a.increasing<<","<<b.increasing;
                    return refused(why.str());
                }
                if(boundary[a.boundaryIndex].accounted||
                   boundary[b.boundaryIndex].accounted)
                    return refused("A seam boundary edge belongs to multiple contact sections");
                boundary[a.boundaryIndex].accounted=true;
                boundary[b.boundaryIndex].accounted=true;
                const auto equalAt=[&](VertexId v,VertexId w) {
                    join(v,w);
                };
                if(a.increasing) {
                    equalAt(a.from,b.to);
                    equalAt(a.to,b.from);
                }else {
                    equalAt(a.to,b.from);
                    equalAt(a.from,b.to);
                }
                if(++matchedPairs>limits.maxSeamPairs)
                    throw std::length_error("V17 joined seam segment budget exhausted");
            }
        }
        for(const auto& item:boundary)
            if(!item.accounted)
                return refused("An authorized removal edge is not on ANY certified section");

        // Resolve each explicit seam equivalence class to one ORIGINAL
        // permanent ID. For reflected input, choose paired representatives
        // so reflected stable-ID orbits remain exact and all-or-nothing.
        std::map<VertexId,std::set<VertexId>> classes;
        for(const auto& [v,_]:parent)classes[root(v)].insert(v);
        std::map<VertexId,VertexId> representative;
        if(mirrored) {
            std::set<VertexId> completed;
            for(const auto& [leader,members]:classes) {
                if(completed.contains(leader))continue;
                if(members.empty())return refused("Empty seam equivalence class");
                const auto first=*members.begin();
                if(!mirrors.contains(first))
                    return refused("Weld source has no explicit mirror orbit");
                const auto mirroredLeader=root(mirrors.at(first));
                if(leader==mirroredLeader||!classes.contains(mirroredLeader))
                    return refused("Self-reflected/unpaired seam equivalence class unsupported");
                const auto& other=classes.at(mirroredLeader);
                for(const auto v:members)
                    if(!mirrors.contains(v)||!other.contains(mirrors.at(v)))
                        return refused("A seam class is not closed under vertex reflection");
                const auto mate=mirrors.at(first);
                for(const auto v:members)representative[v]=first;
                for(const auto v:other)representative[v]=mate;
                completed.insert(leader);
                completed.insert(mirroredLeader);
            }
        }else {
            for(const auto& [_,members]:classes) {
                const auto id=*members.begin();
                for(const auto v:members)representative[v]=id;
            }
        }
        std::map<FaceId,std::array<VertexId,3>> rewritten;
        std::set<VertexId> seeds;
        std::set<VertexId> changedVertices;
        for(const auto face:deleted) {
            const auto tri=index.face(face).vertices;
            seeds.insert(tri.begin(),tri.end());
            changedVertices.insert(tri.begin(),tri.end());
        }
        for(const auto& [old,keep]:representative) {
            changedVertices.insert(old);
            if(old!=keep) {
                changedVertices.insert(keep);
                for(const auto face:index.vertex(old).incidentFaces) {
                    if(deleted.contains(face))continue;
                    auto triangle=index.face(face).vertices;
                    for(auto& vertex:triangle)
                        if(vertex==old)vertex=keep;
                    if(triangle[0]==triangle[1]||triangle[1]==triangle[2]||
                       triangle[0]==triangle[2])
                        return refused("Seam weld collapses a surviving triangle");
                    rewritten[face]=triangle;
                    const auto original=index.face(face).vertices;
                    seeds.insert(original.begin(),original.end());
                }
            }
        }
        // Multiple merged vertices in a single triangle must all be
        // rewritten together (the preceding loop may have overwritten a
        // previous choice using still-original face vertices).
        for(auto& [face,triangle]:rewritten) {
            triangle=index.face(face).vertices;
            for(auto& vertex:triangle)
                if(representative.contains(vertex))
                    vertex=representative.at(vertex);
            if(triangle[0]==triangle[1]||triangle[1]==triangle[2]||
               triangle[0]==triangle[2])
                return refused("Combined seam equivalence collapses a surviving face");
        }
        if(rewritten.size()>limits.maxRewrittenFaces)
            throw std::length_error("V17 rewired retained-face budget exhausted");
        // Orphans comprise vertices without ANY surviving incident faces
        // after the face retirement and all exact seam substitutions.
        std::set<VertexId> removedVertices;
        std::set<VertexId> rewiredSurvivors;
        for(const auto& [face,triangle]:rewritten) {
            (void)face;
            rewiredSurvivors.insert(triangle.begin(),triangle.end());
        }
        for(const auto v:changedVertices) {
            // An ID originally orphaned on its own sheet may now own
            // NEW references from the opposite sheet. Do not retire it.
            bool retained=rewiredSurvivors.contains(v);
            for(const auto face:index.vertex(v).incidentFaces) {
                if(deleted.contains(face))continue;
                const auto it=rewritten.find(face);
                const auto tri=it==rewritten.end()?index.face(face).vertices:
                                                  it->second;
                if(std::find(tri.begin(),tri.end(),v)!=tri.end()) {
                    retained=true;break;
                }
            }
            if(!retained) {
                removedVertices.insert(v);
                seeds.insert(v);
            }
        }
        if(seeds.empty())return refused("No writable exact seam neighborhood");
        RegionEdit edit(cut.index,{seeds.begin(),seeds.end()}, {},
                        limits.partitionCapacity);
        for(const auto face:deleted)edit.removeFace(face);
        for(const auto& [face,tri]:rewritten)edit.replaceFace(face,tri);
        for(const auto v:removedVertices)edit.removeVertex(v);
        auto committed=edit.commit();
        auto final=edit.updatedIndex();
        if(!final||committed.revision.get()!=&final->revision())
            return refused("Private seam RegionEdit returned mismatched root");

        if(mirrored) {
            for(const auto& [a,b]:cut.mirroredFaces) {
                if(deleted.contains(a)!=deleted.contains(b))
                    return refused("Final reflected deletion lost a face orbit");
                if(deleted.contains(a))continue;
                if(!final->maybeFace(a)||!final->maybeFace(b))
                    return refused("Surviving reflected child face retired");
                const auto first=final->face(a).vertices,
                           second=final->face(b).vertices;
                bool accepted=false;
                for(unsigned shift=0;shift<3;++shift) {
                    bool same=true;
                    for(unsigned i=0;i<3;++i) {
                        const auto v=first[i],w=second[(shift+3-i)%3];
                        if(!mirrors.contains(v)||mirrors.at(v)!=w)same=false;
                    }
                    if(same)accepted=true;
                }
                if(!accepted)
                    return refused("Private welded surviving face lost mirrored winding");
            }
            for(const auto v:removedVertices)
                if(!mirrors.contains(v)||!removedVertices.contains(mirrors.at(v)))
                    return refused("Private join retired an asymmetric vertex orbit");
        }

        std::set<FaceId> candidates;
        for(const auto& [child,_]:cut.childSources)
            if(final->maybeFace(child))candidates.insert(child);
        for(const auto& [face,_]:rewritten)candidates.insert(face);
        if(candidates.size()>limits.maxCandidateFaces)
            throw std::length_error("Final joined whole-cut collision seed budget exhausted");
        const auto collisions=final->triangleIntersections(
            {},{candidates.begin(),candidates.end()},nullptr,
            limits.residualCollisionLimits);
        if(!collisions.empty())
            return refused("Joined private seam has residual nonadjacent intersections");
        for(const auto face:nomination.protectedChildren) {
            if(!final->maybeFace(face))
                return refused("Private seam retired an exposed or legitimate cavity child");
            const auto old=index.face(face).vertices,
                       now=final->face(face).vertices;
            for(unsigned k=0;k<3;++k)
                if(!exact(index.vertex(old[k]).position,
                          final->vertex(now[k]).position))
                    return refused("A protected exposed/cavity face changed geometric position");
        }
        // There are no added triangles and every surviving source child
        // retains all three original corner positions EXACTLY; both
        // directions of exposed/cavity feature fidelity are therefore zero.
        ExactCutJoinResult result;
        result.complete=true;
        result.index=std::move(final);
        result.commit=std::move(committed);
        result.retiredFaces.assign(deleted.begin(),deleted.end());
        result.retiredVertices.assign(removedVertices.begin(),removedVertices.end());
        for(const auto& [face,_]:rewritten)result.rewrittenFaces.push_back(face);
        for(const auto& [old,keep]:representative)
            if(old!=keep)result.weldedPairs.emplace_back(old,keep);
        result.checkedBoundaryEdges=boundary.size();
        result.checkedContacts=cut.sourceContacts.size();
        return result;
    }catch(const std::exception& error) {
        return refused(error.what());
    }
}

} // namespace mesher
