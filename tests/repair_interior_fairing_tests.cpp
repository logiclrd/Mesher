#include "mesher/repair_interior_fairing.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* what){if(!yes)throw std::runtime_error(what);}
double quality(Vec3 a,Vec3 b,Vec3 c){
    const auto sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
struct Case {
    std::map<VertexId,Vec3> positions;
    std::vector<MixedClosureFace> faces;
};
Case sample(bool reflected) {
    Case t;
    // Square rim and one badly offset new interior vertex. Keep all
    // boundary IDs frozen, even when the center is very near a rim edge.
    for(unsigned k=0;k<4;++k) {
        constexpr std::array<std::array<double,2>,4> yz{{
            {-1,-1},{1,-1},{1,1},{-1,1}
        }};
        t.positions.emplace(k+1,Vec3{-2,yz[k][0],yz[k][1]});
    }
    t.positions.emplace(5,Vec3{-2,.90,.01});
    for(VertexId i=1;i<=4;++i) {
        const auto next=i==4?1:i+1;
        t.faces.push_back({0,1,{5,i,next},MixedClosureFaceKind::Cap,3});
    }
    if(reflected) {
        for(VertexId i=1;i<=5;++i) {
            const auto p=t.positions.at(i);
            t.positions.emplace(i+5,Vec3{-p.x,p.y,p.z});
        }
        for(VertexId i=1;i<=4;++i) {
            const auto next=i==4?1:i+1;
            t.faces.push_back({
                0,2,{10,next+5,i+5},MixedClosureFaceKind::Cap,3});
        }
    }
    return t;
}
double minimum(const Case& c) {
    double result=1;
    for(const auto& f:c.faces) {
        const auto& v=f.vertices;
        result=std::min(result,quality(
            c.positions.at(v[0]),c.positions.at(v[1]),c.positions.at(v[2])));
    }
    return result;
}
void ordinary() {
    auto s=sample(false);
    const auto original=s.positions;
    const auto before=minimum(s);
    require(before<.1,"fixture must start below q=.10");
    const auto changed=fairOneConstrainedClosureVertex(
        s.faces,s.positions,{5},false,.1,.1,2.5,2.5);
    require(changed==1,"an interior vertex should move once");
    require(minimum(s)>before+1e-6,"fairing should improve worst local triangle");
    for(VertexId i=1;i<=4;++i)
        require(s.positions.at(i).x==original.at(i).x&&
                s.positions.at(i).y==original.at(i).y&&
                s.positions.at(i).z==original.at(i).z,
                "the R03 rim must be frozen exactly");
    auto guarded=sample(false);
    guarded.faces[0].stableId=52;
    require(fairOneConstrainedClosureVertex(
        guarded.faces,guarded.positions,{5},false,.1,.1,2.5,2.5)==0,
        "never reposition a vertex touching original surviving FaceId");
}
void mirrors() {
    auto s=sample(true);
    const auto before=minimum(s);
    require(fairOneConstrainedClosureVertex(
        s.faces,s.positions,{5,10},true,.1,.1,2.5,2.5)==2,
        "reflected interior positions must move as one complete orbit");
    const auto a=s.positions.at(5),b=s.positions.at(10);
    require(a.x==-b.x&&a.y==b.y&&a.z==b.z&&minimum(s)>before+1e-6,
        "reflected orbit must remain exact and improve quality");
    auto incomplete=sample(true);
    require(fairOneConstrainedClosureVertex(
        incomplete.faces,incomplete.positions,{5},true,.1,.1,2.5,2.5)==0,
        "never move one unpaired interior vertex under intrinsic symmetry");
    auto ambiguous=sample(true);
    ambiguous.positions.emplace(11,ambiguous.positions.at(10));
    require(fairOneConstrainedClosureVertex(
        ambiguous.faces,ambiguous.positions,{5,10},true,.1,.1,2.5,2.5)==0,
        "ambiguous mirrored XYZ must be refused rather than guessed");
}
} // namespace
int main() {
    try {
        ordinary();
        mirrors();
        std::cout<<"[r06-m11a3a] finite fixed-rim local quality, mirrored orbit, survivor/duplicate refusal PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06 M11A3a constrained fairing FAILED: "<<e.what()<<"\n";
        return 1;
    }
}
