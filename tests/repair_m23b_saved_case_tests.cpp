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
    const auto candidate=stagePrivateMixedClosure(cut,excision,plan,old);
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
    settings.staging.useUnifiedZipper=true;
    const auto result=applyPrivateMixedClosure(cut,excision,plan,settings);
    require(result.index&&result.zipperPairs==1&&
            result.minimumZipperQuality>=settings.minZipperQuality,
        "M23B refusal-2 lacked certified closed source mesh");
    std::cout<<"[r06-m23b-refusal2] SOURCE_CLOSURE_ACCEPTED"
             <<" min_zipper_q="<<result.minimumZipperQuality
             <<" inserted="<<result.insertedVertices.size()<<" PASS\n";
}
}
int main() {
    try{originalCase();}
    catch(const std::exception& e) {
        std::cerr<<"R06-M23B exact refusal-2: "<<e.what()<<"\n";
        return 1;
    }
}
