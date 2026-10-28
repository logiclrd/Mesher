#include "mesher/repair_preview.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Kind=RegionTopologyIndex::FragmentKind;
RegionTopologyIndex::Edge key(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
std::size_t contactIslandCount(
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts) {
    // Contact-pair connected components, not closure-loop counts.
    std::map<FaceId,std::set<FaceId>> graph;
    for(const auto& pair:contacts) {
        graph[pair.first].insert(pair.second);
        graph[pair.second].insert(pair.first);
    }
    std::set<FaceId> visited;
    std::size_t components=0;
    for(const auto& [seed,_]:graph) {
        if(visited.contains(seed))continue;
        ++components;
        std::vector<FaceId> pending{seed};
        while(!pending.empty()) {
            const auto f=pending.back();pending.pop_back();
            if(!visited.insert(f).second)continue;
            for(const auto next:graph.at(f))pending.push_back(next);
        }
    }
    return components;
}
std::vector<FaceId> neighbors(const RegionTopologyIndex& index,FaceId id) {
    std::set<FaceId> neighbors;
    const auto& tri=index.face(id).vertices;
    for(unsigned k=0;k<3;++k) {
        const auto sides=index.maybeEdgeSides(key(tri[k],tri[(k+1)%3]));
        if(!sides||sides->size()!=2)
            throw std::runtime_error("R02 requires a closed two-sided original edge manifold");
        for(const auto& side:*sides)
            if(side.face!=id)neighbors.insert(side.face);
    }
    return {neighbors.begin(),neighbors.end()};
}
std::size_t countFaceComponents(const RegionTopologyIndex& index,
                                const std::set<FaceId>& removed) {
    std::set<FaceId> visited;
    std::size_t count=0;
    for(const auto seed:removed) {
        if(visited.contains(seed))continue;
        ++count;
        std::vector<FaceId> pending{seed};
        while(!pending.empty()) {
            const auto face=pending.back();pending.pop_back();
            if(!visited.insert(face).second)continue;
            for(const auto adjacent:neighbors(index,face))
                if(removed.contains(adjacent))pending.push_back(adjacent);
        }
    }
    return count;
}
bool expandCompleteFans(const RegionTopologyIndex& index,
                        const std::set<VertexId>& removedVertices,
                        std::set<FaceId>& removedFaces,std::size_t maxFaces,
                        std::string& diagnostic) {
    removedFaces.clear();
    for(const auto vertex:removedVertices) {
        const auto original=index.maybeVertex(vertex);
        if(!original) {
            diagnostic="R02 contact references a missing candidate vertex";
            return false;
        }
        for(const auto id:original->incidentFaces) {
            if(!index.maybeFace(id)) {
                diagnostic="R02 vertex fan contains a missing face";
                return false;
            }
            removedFaces.insert(id);
            if(removedFaces.size()>maxFaces) {
                diagnostic="R02 complete incident face fans exceed finite removal budget";
                return false;
            }
        }
    }
    return true;
}
struct BoundaryCheck {
    bool valid=true;
    std::set<VertexId> branchingVertices;
    std::size_t edges{};
    std::string error;
};
BoundaryCheck boundaryGraph(const RegionTopologyIndex& index,
                            const std::set<FaceId>& removed) {
    BoundaryCheck out;
    std::map<VertexId,std::pair<std::size_t,std::size_t>> degrees; // out, in
    for(const auto id:removed) {
        const auto& tri=index.face(id).vertices;
        for(unsigned e=0;e<3;++e) {
            const auto a=tri[e],b=tri[(e+1)%3];
            const auto sides=index.maybeEdgeSides(key(a,b));
            if(!sides||sides->size()!=2) {
                out.valid=false;
                out.error="R02 candidate includes open/nonmanifold source edge";
                return out;
            }
            FaceId surviving=0;
            for(const auto& side:*sides)
                if(!removed.contains(side.face))surviving=side.face;
            if(!surviving)continue;
            const auto& survivor=index.face(surviving).vertices;
            bool reverse=false;
            for(unsigned j=0;j<3;++j)
                if(survivor[j]==b&&survivor[(j+1)%3]==a)reverse=true;
            if(!reverse) {
                out.valid=false;
                out.error="R02 cut borders have inconsistent source triangle winding";
                return out;
            }
            ++degrees[b].first; ++degrees[a].second; ++out.edges;
        }
    }
    for(const auto& [id,degree]:degrees) {
        if(degree.first==1&&degree.second==1)continue;
        if(degree.first>1||degree.second>1) {
            out.branchingVertices.insert(id);
            continue;
        }
        out.valid=false;
        out.error="R02 cut border has an open-ended edge";
        return out;
    }
    return out;
}
void classifyAdvisory(const RegionTopologyIndex& index,
                      const std::set<FaceId>& contacting,
                      const RepairPreviewLimits& limits,
                      RepairExcisionPreview& result) {
    // These samples are hints for future R03/R06 interior selection.
    // They NEVER veto or authorize the current conservative vertex halo.
    for(const auto id:contacting) {
        if(result.classifiedFaces>=limits.maxClassifiedFaces) {
            result.classificationAdvisory=
                "V14 informational sample budget exceeded";
            return;
        }
        ++result.classifiedFaces;
        try {
            const auto fragments=index.classifyFaceFragments(
                id,limits.sampleDepth,false);
            std::size_t buried=0,visible=0,uncertain=0;
            for(const auto& fragment:fragments) {
                if(fragment.kind==Kind::Buried||fragment.kind==Kind::Outside)
                    ++buried;
                else if(fragment.kind==Kind::Ambiguous)++uncertain;
                else ++visible;
            }
            if(uncertain)result.uncertainFaces.push_back(id);
            if(buried&&visible)result.mixedFaces.push_back(id);
        }catch(const std::exception& e) {
            result.uncertainFaces.push_back(id);
            result.classificationAdvisory=e.what();
        }
    }
}
} // namespace

RepairExcisionPreview previewApproximateExcision(
    const RegionTopologyIndex& index,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const RepairPreviewLimits& limits) {
    if(!limits.maxSeedVertices||!limits.maxProposedVertices||
       !limits.maxProposedFaces||!limits.maxContours||
       !limits.maxClassifiedFaces||limits.sampleDepth>4)
        throw std::invalid_argument("Invalid R02 finite-work limits");
    RepairExcisionPreview result;
    result.contactIslands=contactIslandCount(contacts);
    result.complete=true;
    if(contacts.empty())return result;
    std::set<FaceId> contacting;
    std::set<VertexId> selected;
    for(const auto& pair:contacts) {
        if(pair.ambiguous) {
            result.complete=false;
            result.diagnostic="R02 uncertain geometric contact cannot certify deletion";
            return result;
        }
        for(const auto id:{pair.first,pair.second}) {
            const auto face=index.maybeFace(id);
            if(!face) {
                result.complete=false;
                result.diagnostic="R02 contact refers to a missing FaceId";
                return result;
            }
            contacting.insert(id);
            for(const auto vertex:face->vertices)selected.insert(vertex);
        }
    }
    if(selected.size()>limits.maxSeedVertices||
       selected.size()>limits.maxProposedVertices) {
        result.complete=false;
        result.diagnostic="R02 contact vertex budget exceeded";
        return result;
    }
    std::set<FaceId> removed;
    if(!expandCompleteFans(index,selected,removed,limits.maxProposedFaces,
                           result.diagnostic)) {
        result.complete=false;
        return result;
    }
    // The closed star of a union of vertices can have two prospective
    // holes meeting at a *retained* vertex. That vertex would have two
    // separate survivor fans and create a non-manifold pinch after removal.
    // Conservatively absorb the offending vertex's complete star, iterating
    // until every oriented boundary vertex has exactly one in/out edge.
    BoundaryCheck border;
    while(true) {
        border=boundaryGraph(index,removed);
        if(!border.valid) {
            result.complete=false;
            result.diagnostic=border.error;
            break;
        }
        if(border.branchingVertices.empty())break;
        if(result.boundaryRepairs>=limits.maxBoundaryRepairs||
           selected.size()+border.branchingVertices.size()>
               limits.maxProposedVertices) {
            result.complete=false;
            result.diagnostic="R02 finite boundary-pinch cleanup exhausted";
            break;
        }
        for(const auto id:border.branchingVertices)
            if(selected.insert(id).second)result.expandedBoundaryVertices.push_back(id);
        ++result.boundaryRepairs;
        if(!expandCompleteFans(index,selected,removed,limits.maxProposedFaces,
                               result.diagnostic)) {
            result.complete=false;
            break;
        }
    }
    if(result.complete) {
        // A coarse entire-component excision may leave untouched vertex
        // IDs with NO surviving incident faces. They must be retired by
        // R03 too, otherwise those would become orphaned mesh vertices.
        std::set<VertexId> incident;
        for(const auto id:removed)
            for(const auto vertex:index.face(id).vertices)
                incident.insert(vertex);
        for(const auto id:incident) {
            if(selected.contains(id))continue;
            bool hasSurvivor=false;
            for(const auto face:index.vertex(id).incidentFaces)
                if(!removed.contains(face)) {hasSurvivor=true;break;}
            if(!hasSurvivor) {
                if(selected.size()>=limits.maxProposedVertices) {
                    result.complete=false;
                    result.diagnostic="R02 orphan vertex retirement budget exhausted";
                    break;
                }
                selected.insert(id);
                result.newlyOrphanedVertices.push_back(id);
            }
        }
    }
    result.proposedVertices.assign(selected.begin(),selected.end());
    result.proposedFaces.assign(removed.begin(),removed.end());
    if(result.complete) {
        try {
            result.excisionComponents=countFaceComponents(index,removed);
            RepairPlanSettings planning;
            planning.maxRemovedFaces=limits.maxProposedFaces;
            planning.maxBoundaryEdges=limits.maxProposedFaces*3;
            planning.maxContours=limits.maxContours;
            const auto plan=planRemovalContours(index,result.proposedFaces,planning);
            result.contourStatus=plan.status;
            result.boundaryEdges=plan.boundaryEdges;
            result.contours=plan.contours;
            if(plan.status!=RepairPlanStatus::Ready) {
                if(plan.status==RepairPlanStatus::UnmatchedContours||
                   plan.status==RepairPlanStatus::ExcessiveDeviation||
                   plan.status==RepairPlanStatus::AmbiguousClosure)
                    result.closureAdvisory=plan.reason;
                else {
                    result.complete=false;
                    result.diagnostic="R02 invalid hypothetical excision: "+plan.reason;
                }
            }
        }catch(const std::exception& e) {
            result.complete=false;
            result.diagnostic=std::string("R02 contour work refused: ")+e.what();
        }
    }
    classifyAdvisory(index,contacting,limits,result);
    return result;
}
} // namespace mesher
