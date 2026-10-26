#include "mesher/repair_zipper_front.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
UnifiedZipperRim circle(double x,std::size_t n,VertexId base) {
    UnifiedZipperRim rim;
    const double pi=std::acos(-1.);
    for(std::size_t i=0;i<n;++i) {
        const double a=2*pi*static_cast<double>(i)/n;
        rim.ids.push_back(base+static_cast<VertexId>(i));
        rim.positions.push_back({x,.4*std::cos(a),.4*std::sin(a)});
        rim.outgoingNormals.push_back({0,0,1});
    }
    return rim;
}
void test(bool mirrored) {
    auto first=circle(.6,8,100);
    UnifiedZipperRim second;
    for(std::size_t i=first.ids.size();i-->0;) {
        const auto p=first.positions[i];
        second.positions.push_back({-.6,p.y,p.z});
        second.ids.push_back(static_cast<VertexId>(200+i));
        second.outgoingNormals.push_back({0,0,1});
    }
    if(!mirrored) {
        // First and second remain legal oppositely oriented rim loops.
        second.positions[1].y+=.01;
        second.positions[3].z+=.02;
    }
    SpatialCapSettings settings;
    settings.useAdvancingFront=true;
    settings.maxBoundaryVertices=256;
    settings.maxIntersectionTests=500000;
    const auto p=buildUnifiedZipperFront(
        first,second,mirrored,settings);
    require(p.bridgeFaceCount==(mirrored?4u:3u),
        "M23B did not build a complete positive-width finite bridge");
    require(p.bridgeVertexCount==(mirrored?2u:1u),
        "M23B wrong seam vertex count");
    require(p.boundaryIds.size()==16u+(mirrored?2u:1u),
        "M23B spliced boundary contains missing/duplicate source vertices");
    require(p.positions.size()>=p.boundaryIds.size()&&
            p.faces.size()>p.bridgeFaceCount,
        "M23B did not continue filling through the existing cap constructor");
    require(p.minimumSeedQuality>=settings.desiredMinimumQuality,
        "M23B admitted a bad bridge seed");
    std::set<VertexId> sources;
    for(const auto id:p.boundaryIds)
        if(id)require(sources.insert(id).second,
            "M23B duplicated an original source ID in remaining front");
    require(sources.size()==first.ids.size()+second.ids.size(),
        "M23B lost original source IDs");
    if(mirrored) {
        for(const auto& f:p.faces) {
            const auto x0=p.positions[f[0]].x;
            const auto x1=p.positions[f[1]].x;
            const auto x2=p.positions[f[2]].x;
            require(!(std::min({x0,x1,x2})<0&&
                      std::max({x0,x1,x2})>0),
                "M23B bridged X=0 with an off-plane triangle");
        }
    }
    std::cout<<"[r06-m23b] reflected="<<mirrored
             <<" faces="<<p.faces.size()
             <<" generated="<<p.positions.size()-p.boundaryIds.size()
             <<" min_seed_q="<<p.minimumSeedQuality<<" PASS\n";
}
void malformedMustRefuse() {
    auto a=circle(.6,8,100);
    auto b=circle(-.6,8,200);
    b.ids[0]=a.ids[0];
    SpatialCapSettings settings;
    settings.useAdvancingFront=true;
    bool refused=false;
    try{(void)buildUnifiedZipperFront(a,b,false,settings);}
    catch(const std::logic_error&) {refused=true;}
    require(refused,"M23B must refuse shared original source IDs");
}
}
int main() {
    try {
        test(false);
        test(true);
        malformedMustRefuse();
    }catch(const std::exception& e) {
        std::cerr<<"R06-M23B: "<<e.what()<<"\n";
        return 1;
    }
}
