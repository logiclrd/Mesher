#include "mesher/repair_quality_flip.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void expect(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
double q(Vec3 a,Vec3 b,Vec3 c) {
    const auto sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
double minQ(const std::vector<MixedClosureFace>& f,
            const std::map<VertexId,Vec3>& p) {
    double minimum=1;
    for(const auto& face:f) {
        const auto& v=face.vertices;
        minimum=std::min(minimum,q(p.at(v[0]),p.at(v[1]),p.at(v[2])));
    }
    return minimum;
}
std::map<VertexId,Vec3> positions(bool reflected=false) {
    // Intentionally skinny original diagonal (1,2), while the convex
    // replacement diagonal (3,4) yields two healthy triangles.
    std::map<VertexId,Vec3> p{
        {1,{5.0,0,0}},{2,{8.0,0,0}},
        {3,{7.269881588553663,0.12268701074125805,0}},
        {4,{8.0855206755773086,-0.9681983813799359,0}}
    };
    if(reflected)
        for(const std::pair<VertexId,VertexId> ids:
            {std::pair<VertexId,VertexId>{1,5},{2,6},{3,7},{4,8}}) {
            auto v=p.at(ids.first);
            v.x=-v.x;
            p.emplace(ids.second,v);
        }
    return p;
}
std::vector<MixedClosureFace> faces(bool reflected=false) {
    std::vector<MixedClosureFace> f{
        {0,1,{1,2,3},MixedClosureFaceKind::Cap,3.0},
        {0,1,{2,1,4},MixedClosureFaceKind::Cap,3.0}
    };
    if(reflected) {
        f.push_back({0,2,{6,5,7},MixedClosureFaceKind::Cap,3.0});
        f.push_back({0,2,{5,6,8},MixedClosureFaceKind::Cap,3.0});
    }
    return f;
}
std::size_t improve(std::vector<MixedClosureFace>& f,
                    const std::map<VertexId,Vec3>& p,bool reflected=false) {
    return flipOneInferiorClosureDiagonal(f,p,reflected,.1,.1,2.5,2.5);
}
void ordinary() {
    auto p=positions();
    auto f=faces();
    expect(minQ(f,p)<.1,"M10 test fixture must start with a skinny triangle");
    expect(improve(f,p)==1,"M10 new/new interior bad diagonal should flip");
    expect(minQ(f,p)>.3,"M10 flip must improve minimum quality past .10");
    expect(improve(f,p)==0,"M10 must not oscillate a good diagonal");
    for(auto& face:f)face.stableId=17;
    expect(improve(f,p)==0,"M10 never re-triangulates old stable-ID survivor");
}
void reflected() {
    auto p=positions(true);
    auto f=faces(true);
    expect(improve(f,p,true)==2,
        "M10 reflected pair of diagonals must flip as a complete orbit");
    expect(minQ(f,p)>.3,"M10 both sides of mirror must pass quality");
    expect(improve(f,p,true)==0,
        "M10 healthy reflected orbit must stay unchanged");
    f=faces(true);
    f[2].kind=MixedClosureFaceKind::Survivor;
    expect(improve(f,p,true)==0,
        "M10 cannot flip only one side if the mirror orbit has a survivor");
    expect(f[0].vertices==PrivateRepairTriangle{1,2,3},
        "M10 refused mirror orbit must not partially modify original face");
}
void unsafeNeighbors() {
    auto p=positions();
    auto f=faces();
    f[0].kind=MixedClosureFaceKind::Zipper;
    expect(improve(f,p)==0,
        "M10 may not swap diagonals between cap and zipper");
    f=faces();
    f[1].stableId=55;
    expect(improve(f,p)==0,
        "M10 cannot touch the original survivor on one side");
    f=faces();
    f.push_back({0,1,{3,4,17},MixedClosureFaceKind::Cap,3.0});
    p.emplace(17,Vec3{9,1,0});
    expect(improve(f,p)==0,
        "M10 cannot introduce an already-existing topological diagonal");
    f=faces();
    p[4]={7.0,0.02,0};
    expect(improve(f,p)==0,
        "M10 must not flip a non-convex quad into inverted faces");
}
}
int main() {
    try {
        ordinary();
        reflected();
        unsafeNeighbors();
        std::cout<<"[r06-m10] local old/new guards, strict q, complete mirror orbit, unsafe topology PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06 M10 flip regression FAILED: "<<e.what()<<"\n";
        return 1;
    }
}
