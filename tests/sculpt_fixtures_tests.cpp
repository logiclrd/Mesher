#include "mesher/sculpt_fixtures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
constexpr double pi=3.1415926535897932384626433832795;
void require(bool yes,const char* what) {
    if(!yes)throw std::runtime_error(what);
}
bool same(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}
void torus() {
    const auto surface=generateTorusFixture(5000,1,65);
    const auto stats=auditFixtureTopology(surface);
    require(stats.vertices==5000&&stats.faces==10000&&
            stats.edges==15000&&stats.eulerCharacteristic==0&&
            stats.components==1,"Torus must be one closed genus-one surface with N original vertices");
    std::vector<unsigned> areaBuckets(10);
    for(const auto p:surface.vertices) {
        const double radial=std::hypot(p.x,p.y);
        const double tube=std::hypot(radial-.8,p.z);
        require(std::abs(tube-.2)<1e-10,"Every relaxed point must lie on analytic torus");
        double v=std::atan2(p.z,radial-.8);
        if(v<0)v+=2*pi;
        const double area=(.8*v+.2*std::sin(v))/(2*pi*.8);
        const auto bucket=std::min(std::size_t{9},
                                   static_cast<std::size_t>(area*10));
        ++areaBuckets[bucket];
    }
    for(const auto count:areaBuckets)
        require(count>=400&&count<=600,
                "Torus random stratum counts must reflect actual surface area, not uniform angle");
    for(const auto t:surface.triangles) {
        const auto a=surface.vertices[t.a],b=surface.vertices[t.b],
                   c=surface.vertices[t.c];
        const auto center=(a+b+c)/3.0;
        const double u=std::atan2(center.y,center.x);
        const double v=std::atan2(center.z,std::hypot(center.x,center.y)-.8);
        const Vec3 normal{std::cos(u)*std::cos(v),
                          std::sin(u)*std::cos(v),std::sin(v)};
        require(dot(cross(b-a,c-a),normal)>0,
                "Torus must be consistently outward oriented, including both periodic seams");
    }
    const auto repeated=generateTorusFixture(5000,1,65);
    require(repeated.triangles.size()==surface.triangles.size(),
            "Torus seed reproducibility must preserve triangle counts");
    for(std::size_t i=0;i<surface.vertices.size();++i)
        require(same(repeated.vertices[i],surface.vertices[i]),
                "Torus seeded coordinates must be deterministic");
    for(const auto n:std::vector<std::uint32_t>{128,501,4096}) {
        const auto variant=generateTorusFixture(n,20,5);
        const auto audit=auditFixtureTopology(variant);
        require(audit.vertices==n&&audit.faces==2*n&&
                audit.components==1&&audit.eulerCharacteristic==0,
                "Torus must support arbitrary N (not only regular grid multiples)");
    }
    bool refused=false;
    try {(void)generateTorusFixture(40,1,4);}catch(const std::invalid_argument&){refused=true;}
    require(refused,"Too-small torus must fail clearly");
    std::cout<<"[fixtures] 5000 area-uniform torus, exact N, manifold genus one: PASS\n";
}
void hollow() {
    const auto shell=generateHollowSphereFixture(5000,1,65);
    const auto stats=auditFixtureTopology(shell);
    require(stats.components==1&&stats.eulerCharacteristic==2&&
            stats.faces==2*stats.vertices-4,
            "Connected mouth-bearing shell must be a closed genus-zero surface");
    std::vector<double> anglesOuter,anglesInner;
    std::size_t outerVertices=0,innerVertices=0;
    for(const auto p:shell.vertices) {
        const double radius=length(p),axis=std::hypot(p.x,p.z);
        if(std::abs(p.y+.96)<1e-12&&std::abs(axis-.095)<1e-12)
            anglesOuter.push_back(std::atan2(p.z,p.x));
        if(std::abs(p.y+.93)<1e-12&&std::abs(axis-.095)<1e-12)
            anglesInner.push_back(std::atan2(p.z,p.x));
        if(std::abs(radius-1)<1e-10) {
            ++outerVertices;
            require(!(p.y<0&&axis<.1),
                    "Outer sphere cap vertices must actually be excised");
        }
        if(std::abs(radius-.95)<1e-10) {
            ++innerVertices;
            require(!(p.y<0&&axis<.1),
                    "Inner sphere cap vertices must actually be excised");
        }
    }
    require(outerVertices>2000&&innerVertices>2000&&
            anglesOuter.size()>=8&&anglesOuter.size()==anglesInner.size(),
            "Both sphere shells and both separate tube rings must survive");
    const auto steps=anglesOuter.size();
    const double step=2*pi/double(steps);
    for(std::size_t i=0;i<steps;++i) {
        const double u=step*i;
        const double v=step*(i+.5);
        bool outerFound=false,innerFound=false;
        for(const auto t:anglesOuter)
            if(std::abs(std::remainder(t-u,2*pi))<1e-10)outerFound=true;
        for(const auto t:anglesInner)
            if(std::abs(std::remainder(t-v,2*pi))<1e-10)innerFound=true;
        require(outerFound&&innerFound,
                "Tube rings must have specified half-phase angular offset");
    }
    std::size_t outerFaces=0,innerFaces=0;
    for(const auto tri:shell.triangles) {
        const auto a=shell.vertices[tri.a],b=shell.vertices[tri.b],
                   c=shell.vertices[tri.c];
        const double ra=length(a),rb=length(b),rc=length(c);
        const auto center=(a+b+c)/3.0;
        const double orientation=dot(cross(b-a,c-a),center);
        if(std::abs(ra-1)<1e-10&&std::abs(rb-1)<1e-10&&
           std::abs(rc-1)<1e-10) {
            ++outerFaces;
            require(orientation>0,"Outer original shell normals must face outward");
        }
        if(std::abs(ra-.95)<1e-10&&std::abs(rb-.95)<1e-10&&
           std::abs(rc-.95)<1e-10) {
            ++innerFaces;
            require(orientation<0,"Inner original shell normals must face into void");
        }
    }
    require(outerFaces>1000&&innerFaces>1000,
            "Both original sphere orientations need significant surviving surface");
    const auto repeated=generateHollowSphereFixture(5000,1,65);
    require(repeated.vertices.size()==shell.vertices.size()&&
            repeated.triangles.size()==shell.triangles.size(),
            "Same hollow-shell seed must reproduce geometry and topology counts");
    bool odd=false,small=false;
    try {(void)generateHollowSphereFixture(5001,1,2);}
    catch(const std::invalid_argument&){odd=true;}
    try {(void)generateHollowSphereFixture(100,1,2);}
    catch(const std::invalid_argument&){small=true;}
    require(odd&&small,"Invalid hollow-shell counts must be refused explicitly");
    std::cout<<"[fixtures] two clipped spheres, phased tube, directed inner normals, genus zero: PASS\n";
}
}
int main() {
    try {torus();hollow();return 0;}
    catch(const std::exception& e) {
        std::cerr<<"Fixture regression FAILED: "<<e.what()<<'\n';
        return 1;
    }
}
