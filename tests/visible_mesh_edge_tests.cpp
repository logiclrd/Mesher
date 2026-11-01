#include "mesher/wire_appearance.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
struct V {double x,y;};
V sub(V a,V b){return {a.x-b.x,a.y-b.y};}
double cross(V a,V b){return a.x*b.y-a.y*b.x;}
double distance(V a,V b,V p) {
    const V v=sub(b,a);
    return std::abs(cross(v,sub(p,a)))/std::hypot(v.x,v.y);
}
std::array<double,3> bary(V a,V b,V c,V p) {
    const double d=cross(sub(b,a),sub(c,a));
    return {cross(sub(b,p),sub(c,p))/d,
            cross(sub(c,p),sub(a,p))/d,
            cross(sub(a,p),sub(b,p))/d};
}
void require(bool condition,const char* label) {
    if(!condition)throw std::runtime_error(label);
}
void checkTriangle(const std::array<V,3>& tri,V p) {
    const auto [a,b,c]=tri;
    const auto w=bary(a,b,c,p);
    require(std::min({w[0],w[1],w[2]})>=-1e-12,
            "M67 point outside owning triangle");
    const double area=std::abs(cross(sub(b,a),sub(c,a)));
    const double h0=area/std::hypot(b.x-c.x,b.y-c.y);
    const double h1=area/std::hypot(a.x-c.x,a.y-c.y);
    const double h2=area/std::hypot(a.x-b.x,a.y-b.y);
    const double byBary=std::min({w[0]*h0,w[1]*h1,w[2]*h2});
    const double geometric=std::min({distance(b,c,p),distance(c,a,p),distance(a,b,p)});
    require(std::abs(byBary-geometric)<1e-9,
            "M67 barycentric gradient exactly reproduces screen-pixel edge distance");
    require(std::abs(mesher::wireCoverage(float(byBary))-
                     mesher::wireCoverage(float(geometric)))<1e-6,
            "M67 pixel coverage is independent of face depth");
}
void test() {
    for(int scale=1;scale<=4;++scale) {
        const double s=double(scale);
        const std::array<V,3> t{{{0,0},{10*s,0},{0,10*s}}};
        checkTriangle(t,{.5*s,5*s});
        checkTriangle(t,{2*s,3*s});
        checkTriangle(t,{1*s,7*s});
        const std::array<V,3> skew{{{-20*s,8*s},{32*s,11*s},{9*s,46*s}}};
        const V p{skew[0].x*.2+skew[1].x*.3+skew[2].x*.5,
                  skew[0].y*.2+skew[1].y*.3+skew[2].y*.5};
        checkTriangle(skew,p);
        checkTriangle(skew,{(skew[0].x+skew[1].x)*.5,
                            (skew[0].y+skew[1].y)*.5});
    }
    require(mesher::wireCoverage(0.f)==1.f&&
            mesher::wireCoverage(1.f)==.5f&&
            mesher::wireCoverage(1.5f)==0.f,
            "M67 unchanged physical pixel AA calibration");
    std::cout<<"M67 barycentric physical edge-distance tests passed\n";
}
}
int main() {
    try {test();return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
    return 0;
}
