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
        const Vec3 centroid{.25,.5,-.125};
        ViewportOrbit startView;
        const Vec3 centerForward=normalized(
            Vec3{}-viewportCameraEye(1280,720));
        const auto visual=viewportRay(790,415,1280,720);
        const auto picked=startView.choosePivot(
            visual,Vec3{1,2,3},centroid,centerForward);
        check(nearEnough(picked,{1,2,3}),
              "M55 exact foreground source hit outranks background plane");
        const auto background=startView.choosePivot(
            visual,std::nullopt,centroid,centerForward);
        check(std::abs(dot(startView.displayed(background)-
                               startView.displayed(centroid),
                           centerForward))<1e-9,
              "M55 background plane is truly perpendicular to centre ray");
        check(length(cross(startView.displayed(background)-visual.origin,
                           visual.direction))<1e-8,
              "M55 background pivot also lies on the visual pointer ray");
        const Ray parallel{{0,-3,1},normalized(
            cross(centerForward,{1,0,0}))};
        check(nearEnough(startView.choosePivot(
                parallel,std::nullopt,centroid,centerForward),centroid),
              "M55 screen-parallel plane refusal falls back to source centroid");
        const Ray behind{{0,10,1},centerForward};
        check(nearEnough(startView.choosePivot(
                behind,std::nullopt,centroid,centerForward),centroid),
              "M55 plane behind eye falls back to source centroid");

        ViewportOrbit movedAndRotated;
        movedAndRotated.drag({-.25,.125,.5},135,80,720);
        movedAndRotated.dolly(centerForward,50,720,12);
        const auto rotatedPivot=movedAndRotated.choosePivot(
            visual,std::nullopt,centroid,centerForward);
        const auto displayPivot=movedAndRotated.displayed(rotatedPivot);
        check(std::abs(dot(displayPivot-
                           movedAndRotated.displayed(centroid),
                           centerForward))<1e-8,
              "M55 rotated and translated background pivot stays SCREEN-parallel");
        check(length(cross(displayPivot-visual.origin,
                           visual.direction))<1e-7,
              "M55 rotated background pivot is still on pointer visual ray");
        check(std::abs(rotatedPivot.y-centroid.y)>1e-4,
              "M55 rotated screen-parallel plane is not original-world Y constant");

        ViewportOrbit dolly;
        dolly.dolly(centerForward,25,720,12);
        const Vec3 delta=dolly.translation();
        check(dot(delta,centerForward)<0&&
              length(cross(delta,centerForward))<1e-9,
              "M55 Alt-right positive X moves camera forward (scene backward) along centre ray");
        const Vec3 position=dolly.displayed({1,2,3});
        const Ray dollyPick=dolly.pickingRay(visual);
        check(nearEnough(dolly.displayed(dollyPick.origin),visual.origin)&&
              nearEnough(position,{1,2,3}+delta),
              "M55 dolly only updates visual translation and inverse source picking");
        dolly.dolly(centerForward,-25,720,12);
        check(nearEnough(dolly.translation(),Vec3{}),
              "M55 reverse dolly traverses back to original camera location");
        const auto frozen=dolly.translation();
        dolly.dolly(centerForward,25,0,12);
        check(nearEnough(dolly.translation(),frozen),
              "M55 invalid frame size leaves scene transform unchanged");

        ViewportCursorWrap wrap;
        wrap.begin(90,50,100,100);
        auto one=wrap.sample(99,50,100,100);
        check(one.warp&&one.warpX==5&&one.warpY==50&&
              std::abs(one.dx-9)<1e-9,
              "M55 right edge wraps cursor to left after real motion");
        wrap.didWarp(one.warpX,one.warpY);
        auto stale=wrap.sample(99,50,100,100);
        check(stale.dx==0&&!stale.warp,
              "M55 queued stale pre-warp callback makes NO giant reverse drag");
        auto fromLeft=wrap.sample(6,50,100,100);
        check(fromLeft.dx==1&&!fromLeft.warp,
              "M55 first actual post-warp movement continues smoothly");
        auto left=wrap.sample(1,50,100,100);
        check(left.warp&&left.warpX==95&&left.dx==-5,
              "M55 left edge wraps back to right with correct drag sign");
        wrap.didWarp(left.warpX,left.warpY);
        auto next=wrap.sample(94,50,100,100);
        check(next.dx==-1&&!next.warp,
              "M55 reverse long gesture has no synthetic cursor delta");
        auto top=wrap.sample(94,1,100,100);
        check(top.warp&&top.warpY==95&&top.dy==-49,
              "M55 vertical crossing wraps to opposite edge");
        wrap.didWarp(top.warpX,top.warpY);
        check(wrap.sample(94,94,100,100).dy==-1,
              "M55 vertical post-warp motion remains continuous");
        check(wrap.sample(60,60,120,120).dx==0,
              "M55 client resize resets baseline, not a spurious giant drag");
        wrap.disableWarp();
        check(!wrap.sample(119,119,120,120).warp,
              "M55 platform-refused GLFW cursor wrapping degrades safely");
        wrap.stop();
        check(wrap.sample(119,119,120,120).dx==0,
              "M55 ordinary mouse movement after release never wraps");


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
