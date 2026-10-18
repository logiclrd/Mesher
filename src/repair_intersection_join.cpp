#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
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
        if(!cut.index->maybeVertex(a)||!cut.index->maybeVertex(b)||
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
    for(const auto& [a,b]:cut.mirroredVertices) {
        if(a==b)throw std::logic_error("Off-plane new cut orbit is self paired");
        add(a,b,false);
    }
    for(const auto id:cut.mirroredSeamVertices) {
        if(cut.index->vertex(id).position.x!=0)
            throw std::logic_error("New source-fixed vertex is not exactly on X=0");
        add(id,id,false);
    }
    const std::set<VertexId> inserted(cut.insertedVertices.begin(),
                                      cut.insertedVertices.end());
    if(inserted.size()!=cut.insertedVertices.size()||
       inserted.size()!=cut.mirroredVertices.size()*2+
                         cut.mirroredSeamVertices.size())
        throw std::logic_error("Mirror cut does not account for every new vertex orbit");
    for(const auto id:inserted)
        if(!result.contains(id))
            throw std::logic_error("Unmapped inserted source-cut vertex orbit");
    return result;
}
} // namespace

ExactCutJoinResult joinCertifiedCutSections(
    const ConformingCutCandidate& cut,
    const ExactCutJoinLimits& limits,
    const std::unordered_map<VertexId,VertexId>* originalMirrors,
    bool includeAuthorizedUncutOriginals,
    const UncutFaceRetirementLimits& uncutLimits) {
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
                            !cut.mirroredVertices.empty()||
                            !cut.mirroredSeamVertices.empty();
        if(mirrored!=(originalMirrors!=nullptr))
            return refused("Mirrored seam join needs explicit original vertex mirror involution");
        const auto mirrors=mirrored?mirrorMap(cut,*originalMirrors):
                                      std::map<VertexId,VertexId>{};
        const auto& index=*cut.index;
        std::set<FaceId> deleted(nomination.proposedRemoval.begin(),
                                 nomination.proposedRemoval.end());
        std::vector<FaceId> extraUncut,protectedUncut;
        if(includeAuthorizedUncutOriginals) {
            const auto uncut=planUncutFaceRetirement(
                cut,limits.removal,uncutLimits,originalMirrors);
            if(uncut.status!=CutRemovalStatus::NoChanges&&
               !uncut.ready())
                return refused("Independent uncut original authorization: "+
                               uncut.refusal);
            // Even NoChanges may have surveyed protected Exposed or
            // ReversedBoundary original faces. Their immutable identity
            // and physical corners remain mandatory join invariants.
            protectedUncut=uncut.protectedUncut;
            if(uncut.ready()) {
                if(!std::includes(uncut.combinedRemoval.begin(),
                                  uncut.combinedRemoval.end(),
                                  nomination.proposedRemoval.begin(),
                                  nomination.proposedRemoval.end()))
                    return refused("Uncut and c3 child removal journals disagree");
                deleted.insert(uncut.authorizedUncut.begin(),
                               uncut.authorizedUncut.end());
                extraUncut=uncut.authorizedUncut;
            }
        }
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
                if(leader==mirroredLeader) {
                    // A self-reflected BOOLEAN weld needs source proof for
                    // EVERY sheet-local ID in this class. An ordinary
                    // off-plane pair cannot be folded into one ID merely
                    // because its two members happen to meet on X=0.
                    for(const auto v:members) {
                        if(!mirrors.contains(v)||mirrors.at(v)!=v||
                           index.vertex(v).position.x!=0)
                            return refused("Nonfixed vertex in a self-reflected seam class");
                        const bool newFixed=std::find(
                            cut.mirroredSeamVertices.begin(),
                            cut.mirroredSeamVertices.end(),v)!=
                            cut.mirroredSeamVertices.end();
                        if(newFixed) {
                            if(cut.originalIndex->maybeVertex(v))
                                return refused("Source-fixed new seam ID was historical");
                        }else {
                            if(!cut.originalIndex->maybeVertex(v)||
                               !originalMirrors->contains(v)||
                               originalMirrors->at(v)!=v)
                                return refused("Historical self-seam ID has no source orbit");
                            bool originalCorner=false;
                            for(const auto& [child,source]:cut.childSources) {
                                const auto& current=index.face(child).vertices;
                                const auto& past=cut.originalIndex->face(source).vertices;
                                if(std::find(current.begin(),current.end(),v)!=current.end()&&
                                   std::find(past.begin(),past.end(),v)!=past.end()) {
                                    originalCorner=true;
                                    break;
                                }
                            }
                            if(!originalCorner)
                                return refused("Historical fixed seam ID lacks source corner");
                        }
                    }
                    for(const auto v:members)representative[v]=first;
                    completed.insert(leader);
                    continue;
                }
                if(!classes.contains(mirroredLeader))
                    return refused("Unpaired reflected seam equivalence class");
                const auto& other=classes.at(mirroredLeader);
                if(members.size()!=other.size())
                    return refused("Reflected seam classes have different cardinality");
                for(const auto v:members)
                    if(!mirrors.contains(v)||!other.contains(mirrors.at(v)))
                        return refused("A seam class is not closed under vertex reflection");
                // Off-plane classes remain distinct and reciprocal.
                if(first==mirrors.at(first)||mirrors.at(first)==first)
                    return refused("Fixed source seam class paired to an off-plane class");
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
        for(const auto face:protectedUncut) {
            if(!final->maybeFace(face)||
               !cut.originalIndex->maybeFace(face)||
               !exact(final->vertex(final->face(face).vertices[0]).position,
                      cut.originalIndex->vertex(
                          cut.originalIndex->face(face).vertices[0]).position)||
               final->face(face).vertices!=cut.originalIndex->face(face).vertices)
                return refused("Private seam changed an exposed/cavity uncut original");
        }
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
        result.retiredUncut=std::move(extraUncut);
        result.protectedUncut=std::move(protectedUncut);
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


ExactSeamRefinementResult reconcileCertifiedSeamSubdivisions(
    const ConformingCutCandidate& cut,
    const ExactSeamRefinementLimits& limits,
    bool includeAuthorizedUncutOriginals,
    const UncutFaceRetirementLimits& uncutLimits) {
    ExactSeamRefinementResult out;
    const auto require=[](bool good,const char* reason) {
        if(!good)throw std::logic_error(reason);
    };
    try {
        require(cut.index&&cut.originalIndex&&cut.commit.revision&&
            cut.commit.revision.get()==&cut.index->revision()&&
            !cut.sourceContacts.empty(),
            "Seam refinement requires a complete private conforming cut");
        const auto& index=*cut.index;
        const auto originalNomination=planExactCutRemoval(cut,limits.join.removal);
        require(originalNomination.ready()&&
                !originalNomination.proposedRemoval.empty(),
                "Seam refinement needs complete c1/c2/c3 removal authorization");
        std::set<FaceId> deleted(
            originalNomination.proposedRemoval.begin(),
            originalNomination.proposedRemoval.end());
        if(includeAuthorizedUncutOriginals) {
            const auto uncut=planUncutFaceRetirement(
                cut,limits.join.removal,uncutLimits);
            require(uncut.ready()||uncut.status==CutRemovalStatus::NoChanges,
                    "Source-aligned seam needs independently certified uncut originals");
            deleted.insert(uncut.authorizedUncut.begin(),
                           uncut.authorizedUncut.end());
        }
        std::map<FaceId,FaceId> sources;
        for(const auto& [child,original]:cut.childSources)
            require(sources.emplace(child,original).second,
                    "Seam refinement has duplicate child provenance");
        const auto gather=[&](const RegionTopologyIndex& root,
                              const std::set<FaceId>& removed,
                              const std::map<FaceId,FaceId>& origin) {
            std::vector<Boundary> found;
            std::set<Edge> seen;
            for(const auto face:removed) {
                const auto tri=root.face(face).vertices;
                for(unsigned j=0;j<3;++j) {
                    const auto e=key(tri[j],tri[(j+1)%3]);
                    const auto sides=root.maybeEdgeSides(e);
                    require(sides&&sides->size()==2,
                            "Refinement removal has nonmanifold boundary");
                    FaceId retained{};
                    for(const auto& side:*sides)
                        if(!removed.contains(side.face))retained=side.face;
                    if(!retained)continue;
                    require(seen.insert(e).second&&origin.contains(retained),
                            "Refinement boundary escaped source child provenance");
                    const auto vertices=root.face(retained).vertices;
                    bool directed=false;
                    for(unsigned k=0;k<3;++k)
                        if(key(vertices[k],vertices[(k+1)%3])==e) {
                            found.push_back({vertices[k],vertices[(k+1)%3],
                                retained,origin.at(retained),false});
                            directed=true;break;
                        }
                    require(directed,"Refinement survivor edge has no direction");
                    if(found.size()>limits.join.maxBoundaryEdges)
                        throw std::length_error("Seam refinement boundary budget");
                }
            }
            return found;
        };
        auto boundary=gather(index,deleted,sources);
        require(!boundary.empty(),"Seam refinement has no c3 survivor boundary");
        std::map<Edge,std::vector<Vec3>> splits;
        std::size_t candidates=0,traceTests=0;
        const auto inspect=[](const RegionTopologyIndex& root,Vec3 position,
                              const Arc& arc,Vec3 begin,Vec3 end) {
            if(exact(position,root.vertex(arc.from).position)||
               exact(position,root.vertex(arc.to).position))
                return false;
            const auto span=end-begin;
            const auto magnitude=length(span);
            const auto direction=span/magnitude;
            const auto t=dot(position-begin,direction);
            const auto epsilon=std::max(1e-10*magnitude,1e-12);
            if(t<arc.lo-epsilon||t>arc.hi+epsilon)return false;
            if(t<=arc.lo+epsilon||t>=arc.hi-epsilon)
                throw std::logic_error(
                    "Near-equal but nonexact seam endpoint cannot be reconciled");
            return true;
        };
        for(std::size_t k=0;k<cut.sourceContacts.size();++k) {
            const auto pair=cut.sourceContacts[k];
            const auto sa=cut.sectionFirst.at(k),sb=cut.sectionSecond.at(k);
            const auto begin=index.vertex(sa[0]).position,
                       end=index.vertex(sa[1]).position;
            require(exact(begin,index.vertex(sb[0]).position)&&
                    exact(end,index.vertex(sb[1]).position),
                    "Original source-paired section endpoints are not exact");
            auto a=trace(index,boundary,pair.first,begin,end,
                         traceTests,limits.join.maxTraceTests);
            auto b=trace(index,boundary,pair.second,begin,end,
                         traceTests,limits.join.maxTraceTests);
            const auto len=length(end-begin);
            const auto epsilon=std::max(1e-10*len,1e-12);
            std::vector<double> stops;
            for(const auto& arcs:{a,b})
                for(const auto& arc:arcs) {
                    stops.push_back(arc.lo);
                    stops.push_back(arc.hi);
                }
            std::sort(stops.begin(),stops.end());
            stops.erase(std::unique(stops.begin(),stops.end(),
                [epsilon](double x,double y){return std::abs(x-y)<=epsilon;}),
                stops.end());
            // Gaps inside the THIRD solid are allowed, but both sheets
            // MUST expose the same open intervals. A unilateral interval
            // cannot be repaired by adding faces or a nearby weld.
            for(std::size_t n=1;n<stops.size();++n) {
                if(++candidates>limits.maxCandidateTests)
                    throw std::length_error("Seam interval sweep budget");
                if(stops[n]-stops[n-1]<=2*epsilon)continue;
                const double at=stops[n-1]+(stops[n]-stops[n-1])/2;
                const auto covered=[at](const std::vector<Arc>& arcs) {
                    return std::any_of(arcs.begin(),arcs.end(),
                        [at](const Arc& arc){
                            return arc.lo<at&&at<arc.hi;
                        });
                };
                require(covered(a)==covered(b),
                        "Surviving contact interval is present on only one sheet");
            }
            const auto collect=[&](const std::vector<Arc>& own,
                                   const std::vector<Arc>& opposite) {
                for(const auto& arc:own)
                    for(const auto& other:opposite)
                        for(const auto vertex:{other.from,other.to}) {
                            if(++candidates>limits.maxCandidateTests)
                                throw std::length_error("Seam split predicate budget");
                            const auto p=index.vertex(vertex).position;
                            if(!inspect(index,p,arc,begin,end))continue;
                            auto& group=splits[key(arc.from,arc.to)];
                            if(std::none_of(group.begin(),group.end(),
                                [&](Vec3 old){return exact(old,p);}))
                                group.push_back(p);
                        }
            };
            collect(a,b);
            collect(b,a);
        }
        if(!splits.empty()) {
            require(cut.mirroredFaces.empty()&&cut.mirroredVertices.empty(),
                "New mirrored seam refinements require V17-M05 orbit staging");
        }
        ConformingCutCandidate refined=cut;
        if(!splits.empty()) {
            std::set<VertexId> seeds;
            std::map<FaceId,std::vector<std::array<VertexId,3>>> faces;
            std::map<FaceId,FaceId> sourceOf=sources;
            for(const auto& [edge,group]:splits) {
                require(!group.empty(),"Refinement lost candidate split points");
                const auto sides=index.maybeEdgeSides(edge);
                require(sides&&sides->size()==2,
                        "Refinement edge has no two incident source children");
                for(const auto side:*sides) {
                    require(sources.contains(side.face),
                            "Refinement edge crosses unrelated unproven source face");
                    const auto tri=index.face(side.face).vertices;
                    seeds.insert(tri.begin(),tri.end());
                    faces.try_emplace(side.face,
                        std::vector<std::array<VertexId,3>>{tri});
                }
            }
            require(!seeds.empty(),"No local refinement source vertices");
            RegionEdit edit(cut.index,{seeds.begin(),seeds.end()}, {},
                            limits.join.partitionCapacity);
            std::map<VertexId,Vec3> addedPositions;
            for(const auto& [edge,group]:splits) {
                const auto a=index.vertex(edge.a).position,
                           b=index.vertex(edge.b).position;
                const auto vector=b-a;
                const auto length2=dot(vector,vector);
                require(length2>0,"Zero-length seam split edge");
                std::vector<std::pair<double,Vec3>> ordered;
                for(const auto p:group) {
                    const auto t=dot(p-a,vector)/length2;
                    const auto projected=a+vector*t;
                    require(t>1e-9&&t<1-1e-9&&
                        length(p-projected)<=1e-9*std::sqrt(length2),
                        "Source-proven split is off the local seam edge");
                    ordered.push_back({t,p});
                }
                std::sort(ordered.begin(),ordered.end(),
                    [](const auto& x,const auto& y){return x.first<y.first;});
                for(std::size_t n=1;n<ordered.size();++n)
                    require(ordered[n].first-ordered[n-1].first>1e-9,
                            "Conflicting near-duplicate section refinement points");
                VertexId previous=edge.a;
                for(const auto& [t,p]:ordered) {
                    (void)t;
                    if(refined.insertedVertices.size()-cut.insertedVertices.size()>=
                       limits.maxNewVertices)
                        throw std::length_error("Refinement new-vertex budget");
                    const auto middle=edit.appendLocalVertex(
                        index.vertex(edge.a).owner,p);
                    refined.insertedVertices.push_back(middle);
                    addedPositions.emplace(middle,p);
                    for(const auto side:index.edgeSides(edge)) {
                        auto& triangles=faces.at(side.face);
                        bool found=false;
                        for(std::size_t i=0;i<triangles.size()&&!found;++i) {
                            const auto old=triangles[i];
                            for(unsigned j=0;j<3;++j) {
                                const auto from=old[j],to=old[(j+1)%3];
                                if(key(from,to)!=key(previous,edge.b))continue;
                                const auto opposite=old[(j+2)%3];
                                triangles[i]={from,middle,opposite};
                                triangles.push_back({middle,to,opposite});
                                found=true;break;
                            }
                        }
                        require(found,"Source-child seam fan was not conformingly split");
                    }
                    previous=middle;
                }
            }
            const auto coordinate=[&](VertexId v) {
                const auto it=addedPositions.find(v);
                return it==addedPositions.end()?index.vertex(v).position:it->second;
            };
            for(const auto& [face,triangles]:faces) {
                const auto original=index.face(face).vertices;
                const auto a=coordinate(original[0]),b=coordinate(original[1]),
                           c=coordinate(original[2]);
                const auto normal=cross(b-a,c-a);
                const auto oldArea=length(normal);
                require(oldArea>0,"Refinement original child has no area");
                double area=0;
                for(const auto tri:triangles) {
                    const auto p=coordinate(tri[0]),q=coordinate(tri[1]),
                               r=coordinate(tri[2]);
                    const auto oriented=dot(cross(q-p,r-p),normal)/oldArea;
                    require(oriented>1e-12*oldArea&&std::isfinite(oriented),
                            "Refined child has invalid source orientation or area");
                    area+=oriented;
                }
                require(std::abs(area-oldArea)<=1e-8*oldArea,
                        "Refinement loses original child triangle area");
                edit.replaceFace(face,triangles.front());
                for(std::size_t n=1;n<triangles.size();++n) {
                    if(refined.insertedFaces.size()-cut.insertedFaces.size()>=
                       limits.maxNewFaces)
                        throw std::length_error("Refinement new-face budget");
                    const auto child=edit.appendLocalFace(
                        index.face(face).owner,triangles[n]);
                    refined.insertedFaces.push_back(child);
                    refined.childSources.emplace_back(child,sourceOf.at(face));
                }
            }
            refined.commit=edit.commit();
            refined.index=edit.updatedIndex();
            require(refined.index&&refined.commit.revision.get()==
                    &refined.index->revision(),"Missing refined COW root");
        }
        // Reapply ALL three original cut admissibility gates on the
        // staged new root: split triangles have distinct FaceIds and
        // must never inherit authorization solely from the parent.
        const auto updated=planExactCutRemoval(refined,limits.join.removal);
        require(updated.ready(),"Refined source children failed c1/c2/c3 authorization");
        std::map<FaceId,FaceId> newSources;
        for(const auto& [child,origin]:refined.childSources)
            require(newSources.emplace(child,origin).second,
                    "Refined source child journal duplicated a FaceId");
        std::set<FaceId> newDeleted(
            updated.proposedRemoval.begin(),updated.proposedRemoval.end());
        if(includeAuthorizedUncutOriginals) {
            const auto uncut=planUncutFaceRetirement(
                refined,limits.join.removal,uncutLimits);
            require(uncut.ready()||uncut.status==CutRemovalStatus::NoChanges,
                    "Refined seam lost original uncut-face authorization");
            newDeleted.insert(uncut.authorizedUncut.begin(),
                              uncut.authorizedUncut.end());
        }
        auto matched=gather(*refined.index,newDeleted,newSources);
        std::size_t checks=0,matches=0;
        for(std::size_t k=0;k<refined.sourceContacts.size();++k) {
            const auto pair=refined.sourceContacts[k];
            const auto first=refined.sectionFirst.at(k),
                       second=refined.sectionSecond.at(k);
            const auto a=refined.index->vertex(first[0]).position,
                       b=refined.index->vertex(first[1]).position;
            require(exact(a,refined.index->vertex(second[0]).position)&&
                    exact(b,refined.index->vertex(second[1]).position),
                    "Refined original V13 endpoint mismatch");
            auto aa=trace(*refined.index,matched,pair.first,a,b,
                          checks,limits.join.maxTraceTests);
            auto bb=trace(*refined.index,matched,pair.second,a,b,
                          checks,limits.join.maxTraceTests);
            require(aa.size()==bb.size(),
                    "Refined opposite source section chain lengths differ");
            for(std::size_t j=0;j<aa.size();++j) {
                const auto& x=aa[j],&y=bb[j];
                const auto loX=x.increasing?x.from:x.to;
                const auto hiX=x.increasing?x.to:x.from;
                const auto loY=y.increasing?y.from:y.to;
                const auto hiY=y.increasing?y.to:y.from;
                require(x.increasing!=y.increasing&&
                        exact(refined.index->vertex(loX).position,
                              refined.index->vertex(loY).position)&&
                        exact(refined.index->vertex(hiX).position,
                              refined.index->vertex(hiY).position),
                        "Refined seam endpoints are not exact opposite partners");
                require(!matched[x.boundaryIndex].accounted&&
                        !matched[y.boundaryIndex].accounted,
                        "Refined seam is ambiguously owned by multiple contacts");
                matched[x.boundaryIndex].accounted=true;
                matched[y.boundaryIndex].accounted=true;
                if(++matches>limits.join.maxSeamPairs)
                    throw std::length_error("Refined seam match budget");
            }
        }
        for(const auto& b:matched)
            require(b.accounted,"Refinement left unmatched authorized source boundary");
        out.complete=true;
        out.insertedVertices=refined.insertedVertices.size()-
                             cut.insertedVertices.size();
        out.insertedFaces=refined.insertedFaces.size()-cut.insertedFaces.size();
        out.matchedIntervals=matches;
        out.refined.emplace(std::move(refined));
        return out;
    }catch(const std::exception& e) {
        out.refusal=e.what();
        return out; // zero leaked private cut roots or partial journals
    }
}

} // namespace mesher
