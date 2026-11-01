#include "mesher/repair_case.hpp"
#include "mesher/repair_preview.hpp"
#include "mesher/repair_excision.hpp"
#include "mesher/repair_auto_plan.hpp"
#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_mixed_closure.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* msg) {
    if(!ok)throw std::runtime_error(msg);
}
double minZipperQuality(const MixedClosureStaging& staged) {
    double result=1;
    std::size_t count=0;
    for(const auto& f:staged.faces()) {
        if(f.kind!=MixedClosureFaceKind::Zipper)continue;
        const auto v=f.triangle();
        const auto a=staged.positions().at(v[0]);
        const auto b=staged.positions().at(v[1]);
        const auto c=staged.positions().at(v[2]);
        const double scale=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
        const double q=scale>0?2*std::sqrt(3.)*
            length(cross(b-a,c-a))/scale:0.;
        result=std::min(result,q);
        ++count;
    }
    require(count>0,"M23B refusal-2 did not stage any zipper faces");
    return result;
}
struct NormalMetrics {
    std::size_t zipperFaces{};
    std::size_t seamEdges{},internalEdges{};
    double seamP90{},seamMax{},internalP90{},internalMax{};
    std::size_t maxBlueValence{};
};
NormalMetrics normalMetrics(const MixedPrivateClosureResult& result) {
    NormalMetrics metrics;
    metrics.zipperFaces=result.blueFaces.size();
    const std::set<FaceId> blues(result.blueFaces.begin(),result.blueFaces.end());
    std::vector<double> seam,interior;
    std::map<VertexId,std::size_t> valences;
    const auto normal=[&](FaceId id) {
        const auto ids=result.index->face(id).vertices;
        const auto a=result.index->vertex(ids[0]).position;
        const auto b=result.index->vertex(ids[1]).position;
        const auto c=result.index->vertex(ids[2]).position;
        const auto crossN=cross(b-a,c-a);
        const auto len=length(crossN);
        return len>1e-12?crossN/len:Vec3{};
    };
    const auto pi=std::acos(-1.);
    for(const auto fid:result.blueFaces) {
        const auto ids=result.index->face(fid).vertices;
        for(unsigned i=0;i<3;++i) {
            const auto a=ids[i],b=ids[(i+1)%3];
            ++valences[a];
            const RegionTopologyIndex::Edge ends{std::min(a,b),std::max(a,b)};
            const auto sides=result.index->maybeEdgeSides(ends);
            if(!sides||sides->size()!=2)continue;
            FaceId other=0;
            for(const auto& side:*sides)
                if(side.face!=fid)other=side.face;
            if(!other)continue;
            const bool otherBlue=blues.count(other)>0;
            if(otherBlue&&fid>other)continue;
            const auto n=normal(fid),m=normal(other);
            if(length(n)<1e-12||length(m)<1e-12)continue;
            const auto degrees=std::acos(std::clamp(dot(n,m),-1.,1.))*180./pi;
            (otherBlue?interior:seam).push_back(degrees);
        }
    }
    for(const auto& [vertex,valence]:valences) {
        (void)vertex;
        metrics.maxBlueValence=std::max(metrics.maxBlueValence,valence);
    }
    const auto finish=[](std::vector<double>& values,double& p90,double& max) {
        if(values.empty())return;
        std::sort(values.begin(),values.end());
        p90=values[(values.size()-1)*9/10];
        max=values.back();
    };
    metrics.seamEdges=seam.size();
    metrics.internalEdges=interior.size();
    finish(seam,metrics.seamP90,metrics.seamMax);
    finish(interior,metrics.internalP90,metrics.internalMax);
    return metrics;
}

void originalCase() {
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/
        "repair-cases"/"refusal-2.r06case";
    const auto saved=readRepairCase(path);
    require(saved.candidate.index&&!saved.contacts.empty(),
        "M23B saved refusal-2 did not load");
    const auto originalRetirementCount=saved.candidate.index->retiredFaceCount();
    const auto excision=previewApproximateExcision(
        *saved.candidate.index,saved.contacts);
    require(excision.complete,"M23B refusal-2 requires R03 excision");
    const auto cut=applyPrivateExcision(saved.candidate.index,excision);
    const auto plan=planPrivateAutomaticClosure(
        *saved.candidate.index,cut,excision,saved.contacts);
    require(plan.ready()&&plan.zipperPairs.size()==1,
        "M23B refusal-2 must choose one authorized zipper pair");
    MixedClosureStageSettings old;
    old.requireReflectedGeometry=saved.symmetricSource;
    const auto prior=stagePrivateMixedClosure(cut,excision,plan,old);
    const auto previousQ=minZipperQuality(prior);
    old.useUnifiedZipper=true;
    old.emitTrace=true; // exact archived placement/join diagnostics remain opt-in
    std::ostringstream capturedTrace;
    // The staging operation is synchronous: capture its diagnostics, then
    // verify that BOTH the face-map IDs and placement IDs are exactly the
    // same VertexIds used in the staged private geometry.
    const auto candidate=[&]() {
        struct Capture {
            explicit Capture(std::ostringstream& out)
                :previous(std::cout.rdbuf(out.rdbuf())) {}
            ~Capture() {std::cout.rdbuf(previous);}
            std::streambuf* previous;
        } capture(capturedTrace);
        return stagePrivateMixedClosure(cut,excision,plan,old);
    }();
    std::size_t placed=0,mappedFaces=0,actualFaceMatches=0;
    std::set<VertexId> tracedIds;
    std::istringstream lines(capturedTrace.str());
    std::string line;
    const auto number=[&](const std::string& text,
                          const std::string& key)->VertexId {
        const auto i=text.find(key);
        require(i!=std::string::npos,"M23B trace missing replay join field");
        return static_cast<VertexId>(
            std::stoull(text.substr(i+key.size())));
    };
    while(std::getline(lines,line)) {
        if(line.find("[repair-r06-m23b-placement]")!=std::string::npos) {
            ++placed;
            const auto id=number(line," provisional=");
            require(candidate.positions().contains(id)&&
                    std::find(candidate.provisionalSpatialVertices().begin(),
                              candidate.provisionalSpatialVertices().end(),id)!=
                        candidate.provisionalSpatialVertices().end(),
                "M23B placement trace has no corresponding replay VertexId");
            require(line.find(" raw=")!=std::string::npos&&
                    line.find(" tangent=")!=std::string::npos&&
                    line.find(" gravity=")!=std::string::npos&&
                    line.find(" poisson=")!=std::string::npos&&
                    line.find(" afm_support_faces=")!=std::string::npos,
                "M23B trace missing one of the four diagnostic positions");
            tracedIds.insert(id);
        }else if(line.find("[repair-r06-m23b-face-map]")!=std::string::npos) {
            ++mappedFaces;
            const auto a=number(line," A="),b=number(line," B="),
                       c=number(line," C=");
            for(const auto& f:candidate.faces()) {
                if(f.kind!=MixedClosureFaceKind::Zipper)continue;
                const auto v=f.triangle();
                if(v[0]==a&&v[1]==b&&v[2]==c) {
                    ++actualFaceMatches;
                    break;
                }
            }
        }
    }
    require(placed>0&&tracedIds.size()==placed,
        "M23B accepted AFM proposals lack unique provisional vertex IDs");
    require(mappedFaces>0&&mappedFaces==actualFaceMatches,
        "M23B face-map arguments do not match staged ENSUREFACE IDs");
    std::cout<<"[r06-m23b-replay-join] vertices="<<placed
             <<" mapped_faces="<<mappedFaces<<" PASS\n";
    const auto nextQ=minZipperQuality(candidate);
    require(candidate.boundarySidesCovered==cut.boundaryEdges,
        "M23B staged geometry did not seal every original R03 rim");
    require(candidate.provisionalSpatialVertices().size()>0,
        "M23B new zipper should add finite-width generated vertices");
    require(saved.candidate.index->retiredFaceCount()==originalRetirementCount,
        "M23B staging changed original root retirement count");
    std::cout<<"[r06-m23b-refusal2] original_q="<<previousQ
             <<" new_q="<<nextQ
             <<" generated_vertices="<<candidate.provisionalSpatialVertices().size()
             <<" zipper_faces="<<candidate.zipperTriangles
             <<" rim_sides="<<candidate.boundarySidesCovered<<" PASS\n";
    MixedPrivateClosureLimits settings;
    settings.requireReflectedGeometry=saved.symmetricSource;
    const auto oldResult=applyPrivateMixedClosure(cut,excision,plan,settings);
    const auto oldMetrics=normalMetrics(oldResult);
    settings.staging.useUnifiedZipper=true;
    const auto result=applyPrivateMixedClosure(cut,excision,plan,settings);
    require(result.index&&result.zipperPairs==1&&
            result.minimumZipperQuality>=settings.minZipperQuality,
        "M23B refusal-2 lacked certified closed source mesh");
    const auto nextMetrics=normalMetrics(result);
    std::cout<<"[r06-m23b-refusal2-final]"
             <<" old_q="<<oldResult.minimumZipperQuality
             <<" new_q="<<result.minimumZipperQuality
             <<" old_faces="<<oldResult.blueFaces.size()
             <<" new_faces="<<result.blueFaces.size()
             <<" old_inserted="<<oldResult.insertedVertices.size()
             <<" new_inserted="<<result.insertedVertices.size()
             <<" old_refinement="<<oldResult.refinementSteps
             <<" new_refinement="<<result.refinementSteps
             <<" old_zip_internal_p90="<<oldMetrics.internalP90
             <<" new_zip_internal_p90="<<nextMetrics.internalP90
             <<" old_zip_internal_max="<<oldMetrics.internalMax
             <<" new_zip_internal_max="<<nextMetrics.internalMax
             <<" old_survivor_seam_p90="<<oldMetrics.seamP90
             <<" new_survivor_seam_p90="<<nextMetrics.seamP90
             <<" old_survivor_seam_max="<<oldMetrics.seamMax
             <<" new_survivor_seam_max="<<nextMetrics.seamMax
             <<" old_blue_valence_max="<<oldMetrics.maxBlueValence
             <<" new_blue_valence_max="<<nextMetrics.maxBlueValence
             <<" PASS\n";
    std::cout<<"[r06-m23b-refusal2] SOURCE_CLOSURE_ACCEPTED"
             <<" min_zipper_q="<<result.minimumZipperQuality
             <<" inserted="<<result.insertedVertices.size()<<" PASS\n";
}
void firstCapNonRegression() {
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/
        "repair-cases"/"refusal-1.r06case";
    const auto saved=readRepairCase(path);
    require(saved.candidate.index&&!saved.contacts.empty(),
        "M23B archived refusal-1 did not load");
    const auto proposed=previewApproximateExcision(
        *saved.candidate.index,saved.contacts);
    require(proposed.complete,"M23B refusal-1 R03 preview failed");
    const auto cut=applyPrivateExcision(saved.candidate.index,proposed);
    const auto plan=planPrivateAutomaticClosure(
        *saved.candidate.index,cut,proposed,saved.contacts);
    require(plan.ready()&&plan.zipperPairs.empty()&&
            plan.cappedContours.size()==1,
        "M23B refusal-1 must remain an ordinary cap");
    MixedPrivateClosureLimits limits;
    limits.requireReflectedGeometry=saved.symmetricSource;
    const auto original=applyPrivateMixedClosure(cut,proposed,plan,limits);
    limits.staging.useUnifiedZipper=true;
    const auto enabled=applyPrivateMixedClosure(cut,proposed,plan,limits);
    require(original.index&&enabled.index&&
            original.minimumCapQuality==enabled.minimumCapQuality&&
            original.greenFaces.size()==enabled.greenFaces.size()&&
            original.insertedVertices.size()==enabled.insertedVertices.size()&&
            original.refinementSteps==enabled.refinementSteps&&
            original.newFaces==enabled.newFaces,
        "M23B opt-in changed refusal-1 ordinary cap geometry/quality");
    std::cout<<"[r06-m23b-refusal1] SOURCE_CLOSURE_ACCEPTED"
             <<" identical_cap_metrics=1"
             <<" q="<<enabled.minimumCapQuality<<" PASS\n";
}
}
int main() {
    try{originalCase();firstCapNonRegression();}
    catch(const std::exception& e) {
        std::cerr<<"R06-M23B exact refusal-2: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
