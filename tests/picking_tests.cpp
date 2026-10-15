#include "mesher/picking.hpp"
#include "mesher/brush.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace mesher;
void check(bool ok,const char* what) {if(!ok)throw std::runtime_error(what);}
int main() {
    try {
        for(bool symmetric:{true,false}) {
            SphereOptions options;
            options.symmetric=symmetric;
            options.vertexCount=800;
            options.relaxationIterations=20;
            auto mesh=generateSphere(options);
            const auto ray=viewportRay(640,360,1280,720);
            const auto hit=firstMeshHit(mesh,ray);
            check(bool(hit),"Center-screen ray hits the sphere");
            check(length(*hit)>0.9 && length(*hit)<1.1,"Ray hit lies on tessellated sphere");
            check(!firstMeshHit(mesh,viewportRay(0,0,1280,720)),
                  "Corner ray misses the sphere");
            // Interactive brush must work with precisely one influence on
            // asymmetric topology; no generator mirror map is required.
            AdaptiveInflateBrush brush(mesh,.01,{true,.25},{},{},symmetric);
            BrushStroke stroke(mesh,brush,.02);
            std::vector<BrushArea> areas{{*hit,.2,.3}};
            if(symmetric)areas.push_back({{-hit->x,hit->y,hit->z},.2,.3});
            stroke.startTimed(areas,.2);
            const auto beforeCount=brush.completedStamps();
            for(int frame=1;frame<=12;++frame) {
                if(symmetric) {
                    const std::array<Vec3,2> points{*hit,{-hit->x,hit->y,hit->z}};
                    stroke.updateTimed(points,frame*.025);
                }else {
                    const std::array<Vec3,1> points{*hit};
                    stroke.updateTimed(points,frame*.025);
                }
            }
            check(brush.completedStamps()>beforeCount,
                  "Holding mouse position increases stamp count");
            if(symmetric) {
                const std::array<Vec3,2> points{*hit,{-hit->x,hit->y,hit->z}};
                stroke.finishTimed(points,.3);
            } else {
                const std::array<Vec3,1> points{*hit};
                stroke.finishTimed(points,.3);
            }
            check(!stroke.active(),"Mouse release ends the stroke");
        }
        std::cout<<"Mouse picking tests passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Picking test failed: "<<error.what()<<'\n';
        return 1;
    }
}
