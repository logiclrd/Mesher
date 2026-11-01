#include "mesher/repair_interior_collapse.hpp"
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace mesher;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct Fixture {
    std::map<VertexId,Vec3> p;
    std::vector<MixedClosureFace> f;
};
Fixture make(bool mirrored) {
    Fixture t;
    t.p={{1,{-2,0,0}},{2,{-2,2,0}},{3,{-2,1,1.732}},
         {4,{-2,1,.002}}};
    t.f={{0,1,{4,1,2},MixedClosureFaceKind::Cap,3.0},
         {0,1,{4,2,3},MixedClosureFaceKind::Cap,3.0},
         {0,1,{4,3,1},MixedClosureFaceKind::Cap,3.0}};
    if(mirrored) {
        for(VertexId id=1;id<=4;++id) {
            const auto p=t.p.at(id);
            t.p.emplace(id+4,Vec3{-p.x,p.y,p.z});
        }
        t.f.push_back({0,2,{8,6,5},MixedClosureFaceKind::Cap,3.0});
        t.f.push_back({0,2,{8,7,6},MixedClosureFaceKind::Cap,3.0});
        t.f.push_back({0,2,{8,5,7},MixedClosureFaceKind::Cap,3.0});
    }
    return t;
}
void ordinary() {
    auto t=make(false);
    const auto original=t.p;
    const auto removed=collapseOneInferiorCapFan(
        t.f,t.p,{4},false,.1,2.5);
    check(removed.size()==1&&removed.front()==4,
        "M11A3b redundant skinny valence-three fan must collapse");
    check(t.f.size()==1&&!t.p.contains(4)&&
          t.f[0].vertices==PrivateRepairTriangle{1,2,3},
          "M11A3b 3-to-1 replacement preserves oriented exterior triangle");
    for(VertexId id=1;id<=3;++id)
        check(t.p.at(id).x==original.at(id).x&&
              t.p.at(id).y==original.at(id).y&&
              t.p.at(id).z==original.at(id).z,
              "M11A3b every source rim position must be bitwise frozen");
    auto guarded=make(false);
    guarded.f[0].stableId=80;
    check(collapseOneInferiorCapFan(
        guarded.f,guarded.p,{4},false,.1,2.5).empty(),
        "M11A3b may not delete original surviving face fan");
    auto altered=make(false);
    altered.f[1].vertices={4,3,2}; // wrong oriented neighbor
    check(collapseOneInferiorCapFan(
        altered.f,altered.p,{4},false,.1,2.5).empty(),
        "M11A3b may not repair a folded/inconsistent winding fan");
}
void reflected() {
    auto t=make(true);
    const auto removed=collapseOneInferiorCapFan(
        t.f,t.p,{4,8},true,.1,2.5);
    check(removed.size()==2&&t.f.size()==2&&
          !t.p.contains(4)&&!t.p.contains(8),
          "M11A3b must remove both reflected fan centers atomically");
    check(t.f[0].vertices==PrivateRepairTriangle{1,2,3}&&
          t.f[1].vertices==PrivateRepairTriangle{5,7,6},
          "M11A3b maintain oriented mirrored triangular disks");
    auto orphan=make(true);
    check(collapseOneInferiorCapFan(
        orphan.f,orphan.p,{4},true,.1,2.5).empty(),
        "M11A3b never change only one half of intrinsic symmetry");
    auto ambiguous=make(true);
    ambiguous.p.emplace(9,ambiguous.p.at(8));
    check(collapseOneInferiorCapFan(
        ambiguous.f,ambiguous.p,{4,8},true,.1,2.5).empty(),
        "M11A3b reject multiple exact reflection counterparts");
}
} // namespace
int main() {
    try {
        ordinary();
        reflected();
        std::cout<<"[r06-m11a3b] 3-to-1 redundant fan, fixed rim, reflected atomicity and refusal guards PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06 M11A3b collapse tests failed: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
