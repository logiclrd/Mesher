#include "mesher/repair_split_template.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace mesher;
namespace {
void check(bool cond,const char* message) {
    if(!cond)throw std::runtime_error(message);
}
using Kind=RepairSplitTemplateKind;
RepairSplitTemplateInput input(
    Vec3 a,Vec3 b,Vec3 opposite,double floor,double target) {
    RepairSplitTemplateInput value;
    value.firstId=11;value.secondId=22;
    value.first=a;value.second=b;
    value.sides={{opposite,floor}};
    value.policy.targetLength=target;
    return value;
}
const RepairSplitTemplateCandidate* candidate(
    const RepairSplitTemplateDecision& result,Kind kind,double t,
    double second=0.) {
    for(const auto& item:result.candidates)
        if(item.kind==kind&&std::abs(item.firstT-t)<1e-10&&
           std::abs(item.secondT-second)<1e-10)return &item;
    return nullptr;
}
void healthyCoarseAndFiniteBudget() {
    auto x=input({0,0,0},{10,0,0},{5,6,0},.1,2);
    x.policy.remainingOperations=1;
    auto p=evaluateRepairSplitTemplates(x);
    check(p.coarse&&p.hasSelection()&&
          p.candidates[p.selected].kind==Kind::Midpoint,
          "healthy large face must prefer balanced coarse midpoint");
    check(std::all_of(p.candidates.begin(),p.candidates.end(),
          [](const auto& v){return v.kind!=Kind::Hybrid||!v.admissible;}),
          "a hybrid cannot escape the one-operation work budget");
    x.policy.remainingOperations=2;x.policy.maximumDepth=1;
    p=evaluateRepairSplitTemplates(x);
    check(std::all_of(p.candidates.begin(),p.candidates.end(),
          [](const auto& v){return v.kind!=Kind::Hybrid||!v.admissible;}),
          "hybrid must respect maximum recursion depth");
    x.policy.maximumDepth=2;
    const auto restored=evaluateRepairSplitTemplates(x);
    check(restored.hasSelection(),"finite budget restored");
    std::cout<<"[r06-m17a] coarse healthy midpoint and hard 1/2-step budgets PASS\n";
}
void hybridNotMistakenForShapeRepair() {
    const auto x=input({0,0,0},{10,0,0},{8.2,.4,0},.1,2);
    const auto p=evaluateRepairSplitTemplates(x);
    const auto midpoint=candidate(p,Kind::Midpoint,.5);
    const auto foot=candidate(p,Kind::OffCenter,.82);
    const auto hybrid=candidate(p,Kind::Hybrid,.5,.82);
    check(midpoint&&foot&&hybrid&&
          hybrid->operations==2&&hybrid->resultingFacesPerSide==3,
          "hybrid must expose two serial splits yielding three triangles");
    // The unchanged A-midpoint-C triangle still controls the minimum
    // quality after the corrective split. Do not assert a fictitious cure!
    check(std::abs(midpoint->worstAfter-hybrid->worstAfter)<1e-12&&
          midpoint->worstAfter<midpoint->worstBefore&&
          !midpoint->admissible&&!hybrid->admissible,
          "sliver remains after both midpoint and hybrid; never silently worsen it");
    check(foot->admissible&&p.hasSelection()&&
          p.candidates[p.selected].kind==Kind::OffCenter,
          "if balanced coarse splitting worsens a sliver, only an independently validated fallback may be proposed");
    auto local=x;local.policy.targetLength=8;
    const auto shaped=evaluateRepairSplitTemplates(local);
    check(!shaped.coarse&&!shaped.hasSelection()&&
          shaped.reason.find("shape repair")!=std::string::npos,
          "once at manageable scale, underquality geometry must be handed to shape repair");
    std::cout<<"[r06-m17a] long needle, hybrid unchanged worst child, scale handoff PASS\n";
}
void endpointChasingAndBoundedCandidates() {
    auto x=input({0,0,0},{10,0,0},{9.8,1,0},.1,2);
    const auto p=evaluateRepairSplitTemplates(x);
    const auto nearEnd=candidate(p,Kind::OffCenter,.98);
    const auto hybrid=candidate(p,Kind::Hybrid,.5,.98);
    check(nearEnd&&!nearEnd->admissible&&hybrid&&!hybrid->admissible,
          "near-endpoint perpendicular split and hybrid cannot chase apex");
    check(p.candidates.size()<=9,
          "candidate enumeration must be finite independent of geometry");
    for(const auto& c:p.candidates)if(c.admissible)
        check(c.shortestBaseFraction+.0000001>=x.policy.minSegmentFraction,
              "no tiny child base segments permitted");
    std::cout<<"[r06-m17a] near-endpoint zigzag rejection, finite candidate set PASS\n";
}
void jointSidesAndCanonicalOrientation() {
    auto x=input({0,0,0},{10,0,0},{7,1,0},.1,2);
    x.sides.push_back({{3,-.05,0},.1});
    const auto p=evaluateRepairSplitTemplates(x);
    check(p.candidates.size()>3&&p.edgeToTargetRatio==5,
          "shared indexed edge must consider both different opposite feet");
    auto one=x;one.sides.pop_back();
    const auto alone=evaluateRepairSplitTemplates(one);
    const auto c1=candidate(p,Kind::OffCenter,.7);
    const auto c2=candidate(alone,Kind::OffCenter,.7);
    check(c1&&c2&&c1->worstAfter<c2->worstAfter,
          "complete two-sided merit must not ignore a worse adjacent survivor");
    auto reversed=x;
    reversed.firstId=22;reversed.secondId=11;
    std::swap(reversed.first,reversed.second);
    const auto reverse=evaluateRepairSplitTemplates(reversed);
    check(reverse.candidates.size()==p.candidates.size(),
          "reversed caller endpoints must canonicalize identical templates");
    for(std::size_t i=0;i<p.candidates.size();++i)
        check(p.candidates[i].kind==reverse.candidates[i].kind&&
              std::abs(p.candidates[i].firstT-
                       reverse.candidates[i].firstT)<1e-12&&
              p.candidates[i].admissible==reverse.candidates[i].admissible,
              "stable-ID canonical t must be independent of input winding");
    auto reflected=x;
    reflected.first.x=-x.first.x;reflected.second.x=-x.second.x;
    for(std::size_t i=0;i<reflected.sides.size();++i)
        reflected.sides[i].opposite.x=-x.sides[i].opposite.x;
    const auto mirror=evaluateRepairSplitTemplates(reflected);
    check(mirror.candidates.size()==p.candidates.size(),
          "reflection should preserve finite choices");
    for(std::size_t i=0;i<p.candidates.size();++i)
        check(p.candidates[i].admissible==mirror.candidates[i].admissible&&
              std::abs(p.candidates[i].firstT-mirror.candidates[i].firstT)<1e-12,
              "mirrored endpoints use the same canonical parameter when their IDs correspond");
    std::cout<<"[r06-m17a] two incident projections and exact canonical mirrored t PASS\n";
}
void actualHybridTrajectoryMerit() {
    auto x=input({0,0,0},{10,0,0},{2,2,0},.1,2);
    auto p=evaluateRepairSplitTemplates(x);
    check(p.coarse&&p.hasSelection()&&
          p.candidates[p.selected].kind==Kind::Hybrid&&
          p.candidates[p.selected].firstT==.5&&
          p.candidates[p.selected].operations==2&&
          p.candidates[p.selected].correctedChildGain>=.10&&
          p.candidates[p.selected].worstAfter>=
             .9*p.candidates.front().worstAfter,
          "coarse healthy face must support a quality-safe, locally useful, bounded hybrid");
    const auto second=p.candidates[p.selected].secondT;
    check(second>.12&&second<.5,
          "one corrective split should target the actual left child");
    x.policy.remainingOperations=1;
    p=evaluateRepairSplitTemplates(x);
    check(p.hasSelection()&&
          p.candidates[p.selected].kind!=Kind::Hybrid,
          "hybrid may never circumvent exhausted second-operation budget");
    x.policy.remainingOperations=2;
    x.sides.front().qualityFloor=.5; // parent is now below quality floor
    p=evaluateRepairSplitTemplates(x);
    check(!p.hasSelection()||
          p.candidates[p.selected].kind!=Kind::Hybrid,
          "under-quality needle must not select coarse hybrid just for local density");
    std::cout<<"[r06-m17c] hybrid only for coarse healthy patch with real child gain and two-step budget PASS\n";
}
void strictInvalidAndNoUnneededEdits() {
    auto x=input({0,0,0},{10,0,0},{5,6,0},.1,20);
    const auto p=evaluateRepairSplitTemplates(x);
    check(!p.hasSelection()&&p.reason.find("healthy edge")!=std::string::npos,
          "healthy in-density geometry needs no gratuitous split");
    x.second=x.first;
    bool failed=false;
    try {(void)evaluateRepairSplitTemplates(x);}
    catch(const std::invalid_argument&){failed=true;}
    check(failed,"zero-length edge must fail before quality evaluation");
    x.second={10,0,0};
    x.policy.targetLength=0;
    failed=false;
    try {(void)evaluateRepairSplitTemplates(x);}
    catch(const std::invalid_argument&){failed=true;}
    check(failed,"nonphysical target edge length must fail closed");
    x.policy.targetLength=2;x.policy.remainingOperations=0;
    const auto zero=evaluateRepairSplitTemplates(x);
    check(!zero.hasSelection()&&
          std::none_of(zero.candidates.begin(),zero.candidates.end(),
             [](const auto& v){return v.admissible;}),
          "exhausted work budget cannot stage a future split");
    std::cout<<"[r06-m17a] no work when healthy, invalid dimensions, zero budget PASS\n";
}
}
int main() {
    try {
        healthyCoarseAndFiniteBudget();
        hybridNotMistakenForShapeRepair();
        endpointChasingAndBoundedCandidates();
        jointSidesAndCanonicalOrientation();
        strictInvalidAndNoUnneededEdits();
        actualHybridTrajectoryMerit();
    }catch(const std::exception& e) {
        std::cerr<<"[r06-m17a] FAILED "<<e.what()<<"\n";
        return 1;
    }
}
