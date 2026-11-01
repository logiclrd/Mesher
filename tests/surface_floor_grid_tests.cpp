#include "mesher/wire_appearance.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
double gridPixelDistance(double x,double y,
                         double unitsPerPixelX,double unitsPerPixelY) {
    const auto nearest=[](double p){return std::abs(p-std::round(p));};
    return std::min(nearest(x)/unitsPerPixelX,
                    nearest(y)/unitsPerPixelY);
}
void need(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}
void test() {
    for(double pixPerUnit:{10.,20.,60.,200.}) {
        const double step=1.0/pixPerUnit;
        const double unit=1.0/pixPerUnit;
        for(int gx=-7;gx<=7;++gx) {
            for(int gy=-7;gy<=7;++gy) {
                need(gridPixelDistance(gx,gy+.5,unit,unit)<1e-9,
                    "M68 integer X floor line has zero pixel distance");
                need(gridPixelDistance(gx+.5,gy,unit,unit)<1e-9,
                    "M68 integer Y floor line has zero pixel distance");
                const float midpoint=mesher::wireCoverage(float(
                    gridPixelDistance(gx+step,gy+.5,unit,unit)));
                need(std::abs(midpoint-.5f)<2e-5,
                    "M68 grid line 1px shoulder matches calibrated smoothstep");
                need(mesher::wireCoverage(float(
                    gridPixelDistance(gx+.5,gy+.5,unit,unit)))==0.f,
                    "M68 floor triangulation diagonal is NOT a grid line");
            }
        }
        const double far=gridPixelDistance(0.0,0.0,unit,unit);
        need(far==0,"M68 grid intersection is identical to both unit lines");
    }
    // Same fractional XY grid coordinates under different local
    // screen projections produce coverage determined in physical pixels.
    const double dA=gridPixelDistance(.01,.5,.01,.01);
    const double dB=gridPixelDistance(.01,.5,.005,.005);
    need(std::abs(dA-1.0)<1e-10&&std::abs(dB-2.0)<1e-10,
         "M68 local projection controls physical pixel width without any Z bias");
    std::cout<<"M68 floor independent unit grid screen-pixel distance passed\n";
}
}
int main() {
    try{test();return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
    return 0;
}
