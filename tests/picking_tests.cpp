#include "mesher/picking.hpp"
#include "mesher/viewport_orbit.hpp"
#include "mesher/brush.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace mesher;
void check(bool ok,const char* what) {if(!ok)throw std::runtime_error(what);}
int main() {
    try {
        // M53 pure UI scene transform regression. No Vulkan driver and no
        // mutation of the worker, original mesh, or selected FaceIds.
        auto nearEnough=[](Vec3 a,Vec3 b,double tolerance=1e-9) {
            return length(a-b)<=tolerance;
        };
        const Ray forward{{0,-3,1},{0,1,0}};
        const Vec3 centroid{.25,.5,-.125};
        check(nearEnough(ViewportOrbit::choosePivot(forward,std::nullopt,centroid),
                         {0,.5,1}),
              "M53 background RMB hits centroid-Y XZ plane, not world Z plane");
        check(nearEnough(ViewportOrbit::choosePivot(forward,Vec3{1,2,3},centroid),
                         {1,2,3}),
              "M53 surface hit outranks background plane");
        check(nearEnough(ViewportOrbit::choosePivot(
                {{0,0,0},{1,0,0}},std::nullopt,centroid),centroid),
              "M53 parallel background ray uses finite centroid fallback");
        check(nearEnough(ViewportOrbit::choosePivot(
                {{0,1,0},{0,1,0}},std::nullopt,centroid),centroid),
              "M53 behind-eye plane intersection uses finite centroid fallback");

        ViewportOrbit orbit;
        const Vec3 pivot{.375,-.75,1.125};
        check(nearEnough(orbit.displayed(pivot),pivot),
              "M53 initial transform is identity");
        orbit.drag(pivot,90,60,720);
        check(nearEnough(orbit.displayed(pivot),pivot),
              "M53 fixed original hit remains under pointer after combined yaw/pitch");
        const Ray camera{{1,-12,5},normalized(Vec3{-1,4,-1})};
        const Ray source=orbit.pickingRay(camera);
        check(nearEnough(orbit.displayed(source.origin),camera.origin)&&
              nearEnough(orbit.transformVector(source.direction),
                         camera.direction),
              "M53 CPU source rays exactly invert GPU scene model transform");
        orbit.drag(pivot,-135,72,1080);
        check(nearEnough(orbit.displayed(pivot),pivot),
              "M53 fixed hit persists over multiple high-DPI frames");
        const Ray sourceAgain=orbit.pickingRay(camera);
        check(nearEnough(orbit.displayed(sourceAgain.origin),camera.origin),
              "M53 second interaction still picks original coordinates");
        for(int i=0;i<1000;++i)orbit.drag(pivot,1.4,-.6,720);
        check(nearEnough(orbit.displayed(pivot),pivot,1e-6),
              "M53 long-running viewport drag retains source-space pivot");
        const auto axes=orbit.basis();
        check(std::abs(dot(axes[0],axes[1]))<1e-9&&
              std::abs(dot(axes[1],axes[2]))<1e-9&&
              std::abs(length(axes[0])-1)<1e-9,
              "M53 repeated rigid rotations retain orthonormal source transform");

        ViewportOrbit horizontal;
        horizontal.drag({},24,0,720);
        check(horizontal.displayed({0,-1,0}).x>0,
              "M53 positive cursor-X rotates nearer negative-Y material right around Z");
        ViewportOrbit vertical;
        vertical.drag({},0,24,720);
        check(vertical.displayed({0,-1,0}).z<0,
              "M53 positive cursor-Y rotates nearer negative-Y material down around X");
        const Ray noOpRay=horizontal.pickingRay(camera);
        horizontal.drag(pivot,0,0,0);
        const auto noOpAgain=horizontal.pickingRay(camera);
        check(nearEnough(noOpRay.origin,noOpAgain.origin),
              "M53 invalid framebuffer span never corrupts picking transform");

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
