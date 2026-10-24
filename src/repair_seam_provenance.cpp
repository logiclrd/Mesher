#include "mesher/repair_seam_provenance.hpp"

#include <algorithm>
#include <map>
#include <ostream>
#include <set>
#include <tuple>
#include <utility>

namespace mesher {
namespace {
using Edge=std::pair<VertexId,VertexId>;
Edge canonical(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
RepairWitnessSourceEdge inspectEdge(
    const RegionTopologyIndex& original,
    const RegionTopologyIndex& candidate,VertexId a,VertexId b) {
    RepairWitnessSourceEdge result;
    if(!a||!b||a==b)return result;
    const auto edge=canonical(a,b);
    result.a=edge.first;
    result.b=edge.second;
    if(const auto old=original.maybeEdgeSides({edge.first,edge.second})) {
        result.sourceSides=old->size();
        for(const auto& side:*old)result.sourceFaces.push_back(side.face);
        std::sort(result.sourceFaces.begin(),result.sourceFaces.end());
        result.sourceFaces.erase(
            std::unique(result.sourceFaces.begin(),result.sourceFaces.end()),
            result.sourceFaces.end());
    }
    if(const auto now=candidate.maybeEdgeSides({edge.first,edge.second}))
        result.candidateSides=now->size();
    return result;
}
bool isIndexedError(const std::string& kind) {
    return kind=="MISMATCHED_INTERFACE_INCIDENCE"||
           kind=="SAME_DIRECTION_INTERFACE"||
           kind=="OPEN_OR_NONMANIFOLD_NEW_EDGE";
}
using WitnessKey=std::tuple<std::string,VertexId,VertexId,Edge,Edge>;
WitnessKey keyFor(const RepairSeamIssue& issue) {
    return {issue.kind,issue.vertex,issue.otherVertex,
            canonical(issue.generatedA,issue.generatedB),
            canonical(issue.survivorA,issue.survivorB)};
}
const char* vertexOrigin(bool inSource,bool introduced) {
    if(inSource)return "SOURCE_R03";
    return introduced?"TRANSACTION_NEW":"UNKNOWN";
}
const char* sourceEdgeKind(const RepairWitnessSourceEdge& e) {
    if(!e.a)return "NOT_REPORTED";
    if(e.sourceSides==1)return "SOURCE_RIM";
    if(e.sourceSides==2)return "SOURCE_INTERIOR";
    if(!e.sourceSides)return "NOT_SOURCE_EDGE";
    return "SOURCE_NONMANIFOLD";
}
} // namespace

RepairSeamProvenanceReport explainRepairSeamWitnesses(
    const RegionTopologyIndex& sourceR03,
    const RegionTopologyIndex& consolidatedPrivate,
    const RepairSeamAudit& audit,
    const std::vector<VertexId>& introducedVertices) {
    RepairSeamProvenanceReport report;
    report.inputStoredIssues=audit.issues.size();
    report.recordedSplits=actualSplits.size();
    std::map<VertexId,MixedClosureSplitEvent> byMidpoint;
    for(const auto& split:actualSplits)
        if(!byMidpoint.emplace(split.midpoint,split).second)
            throw std::logic_error("R06-M15D duplicate midpoint ID in split ancestry");
    report.complete=audit.complete&&!audit.issueListTruncated;
    const std::set<VertexId> introduced(
        introducedVertices.begin(),introducedVertices.end());
    std::map<WitnessKey,std::size_t> seen;
    for(const auto& issue:audit.issues) {
        const auto [it,newEntry]=seen.try_emplace(
            keyFor(issue),report.witnesses.size());
        if(!newEntry) {
            ++report.witnesses[it->second].repeats;
            ++report.repeatedWitnesses;
            continue;
        }
        RepairWitnessProvenance p;
        p.kind=issue.kind;
        p.vertex=issue.vertex;
        p.otherVertex=issue.otherVertex;
        p.vertexInSource=issue.vertex&&bool(sourceR03.maybeVertex(issue.vertex));
        p.otherInSource=issue.otherVertex&&
            bool(sourceR03.maybeVertex(issue.otherVertex));
        p.vertexIntroduced=issue.vertex&&!p.vertexInSource&&
            (introduced.contains(issue.vertex)||
             bool(consolidatedPrivate.maybeVertex(issue.vertex)));
        p.otherIntroduced=issue.otherVertex&&!p.otherInSource&&
            (introduced.contains(issue.otherVertex)||
             bool(consolidatedPrivate.maybeVertex(issue.otherVertex)));
        p.indexedTopologyWitness=isIndexedError(issue.kind);
        if(const auto it=byMidpoint.find(issue.vertex);
           it!=byMidpoint.end()) {
            p.verifiedStageSplitMidpoint=true;
            p.splitParentA=it->second.originalA;
            p.splitParentB=it->second.originalB;
            p.splitParentR03Sides=it->second.originalR03Sides;
            p.splitIncidentSurvivorSides=it->second.incidentSurvivorSides;
            p.splitIncidentCapSides=it->second.incidentCapSides;
            p.splitIncidentZipperSides=it->second.incidentZipperSides;
        }
        if(issue.kind=="NEW_VERTEX_INSIDE_SURVIVOR_EDGE") {
            if(p.verifiedStageSplitMidpoint)++report.witnessedSplitMidpoints;
            else if(p.vertexIntroduced)++report.witnessedOtherTransactionVertices;
        }
        p.newEdge=inspectEdge(sourceR03,consolidatedPrivate,
                               issue.generatedA,issue.generatedB);
        p.survivorEdge=inspectEdge(sourceR03,consolidatedPrivate,
                                    issue.survivorA,issue.survivorB);
        report.witnesses.push_back(std::move(p));
    }
    report.uniqueWitnesses=report.witnesses.size();
    return report;
}
void printRepairSeamProvenance(
    std::ostream& out,const RepairSeamProvenanceReport& report) {
    out<<"[repair-r06-provenance] SUMMARY"
       <<" complete="<<report.complete
       <<" stored_only="<<report.storedOnly
       <<" stored_m13_issues="<<report.inputStoredIssues
       <<" unique_witnesses="<<report.uniqueWitnesses
       <<" duplicate_id_witnesses="<<report.repeatedWitnesses
       <<" recorded_stage_splits="<<report.recordedSplits
       <<" stored_witness_split_midpoints="<<report.witnessedSplitMidpoints
       <<" stored_witness_other_new_vertices="<<report.witnessedOtherTransactionVertices
       <<" validation_gate=FINAL_MANIFOLD_NOT_GEOMETRIC_CONTACT"
       <<" unpublished=1\n";
    for(const auto& w:report.witnesses) {
        out<<"[repair-r06-provenance] WITNESS"
           <<" kind="<<w.kind
           <<" type="<<(w.indexedTopologyWitness?
                    "INDEXED_TOPOLOGY":"GEOMETRIC_ONLY")
           <<" vertex="<<w.vertex
           <<" vertex_origin="<<vertexOrigin(w.vertexInSource,w.vertexIntroduced)
           <<" other="<<w.otherVertex
           <<" other_origin="<<vertexOrigin(w.otherInSource,w.otherIntroduced)
           <<" new_edge="<<w.newEdge.a<<","<<w.newEdge.b
           <<" new_edge_source="<<sourceEdgeKind(w.newEdge)
           <<" new_edge_final_sides="<<w.newEdge.candidateSides
           <<" survivor_edge="<<w.survivorEdge.a<<","<<w.survivorEdge.b
           <<" survivor_edge_source="<<sourceEdgeKind(w.survivorEdge)
           <<" survivor_edge_source_faces=";
        for(std::size_t i=0;i<w.survivorEdge.sourceFaces.size();++i) {
            if(i)out<<",";
            out<<w.survivorEdge.sourceFaces[i];
        }
        out<<" survivor_edge_final_sides="<<w.survivorEdge.candidateSides
           <<" repeats="<<w.repeats
           <<" vertex_creation="<<(w.verifiedStageSplitMidpoint?
                "VERIFIED_EDGE_SPLIT":"NOT_IDENTIFIED_AS_SPLIT")
           <<" split_parent="<<w.splitParentA<<","<<w.splitParentB
           <<" split_parent_r03_sides="<<w.splitParentR03Sides
           <<" split_survivor_sides="<<w.splitIncidentSurvivorSides
           <<" split_cap_sides="<<w.splitIncidentCapSides
           <<" split_zipper_sides="<<w.splitIncidentZipperSides
           <<" intended_attachment=UNKNOWN"
           <<" mandatory_refusal=0\n";
    }
}
} // namespace mesher
