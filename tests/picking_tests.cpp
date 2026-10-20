#include "mesher/picking.hpp"
#include "mesher/viewport_orbit.hpp"
#include "mesher/brush.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
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
              nearEnough(position,Vec3{1,2,3}+delta),
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

        // M60: four pointer segments forming a CLOSED circle must not
        // accumulate a hidden Euler roll/holonomy, even repeated hundreds
        // of times around an off-origin original-space surface pivot.
        ViewportOrbit circle;
        const Vec3 cyclePivot{.375,-.75,1.125};
        const auto initialAxes=circle.basis();
        for(int repeat=0;repeat<400;++repeat) {
            circle.drag(cyclePivot,30,0,720);
            circle.drag(cyclePivot,0,30,720);
            circle.drag(cyclePivot,-30,0,720);
            circle.drag(cyclePivot,0,-30,720);
        }
        for(std::size_t axis=0;axis<3;++axis)
            check(nearEnough(circle.basis()[axis],initialAxes[axis],1e-8),
                  "M60 circular RMB motion never accumulates camera roll");
        check(nearEnough(circle.displayed(cyclePivot),cyclePivot,1e-8),
              "M60 fixed off-origin original pivot survives all closed gestures");

        // The camera view uses global Z as screen-up reference. Even at
        // nonzero pitch, arbitrary yaw leaves the displayed Z-up vector
        // screen-vertical (zero component along the display's right axis).
        // One entire horizontal screen-span is exactly 360 degrees.
        const Vec3 worldUp{0,0,1};
        const Vec3 cameraRight=normalized(cross(centerForward,worldUp));
        const Vec3 cameraUp=cross(cameraRight,centerForward);
        ViewportOrbit turntable;
        turntable.drag(cyclePivot,0,50,720);
        const auto tiltedAxes=turntable.basis();
        const Vec3 tiltedUp=turntable.transformVector(worldUp);
        check(std::abs(dot(tiltedUp,cameraRight))<1e-12&&
              dot(tiltedUp,cameraUp)>0,
              "M60 nonzero pitch keeps source Z vertically up on screen");
        for(int step=0;step<72;++step) {
            turntable.drag(cyclePivot,10,0,720);
            const Vec3 up=turntable.transformVector(worldUp);
            check(std::abs(dot(up,cameraRight))<1e-12&&
                  dot(up,cameraUp)>0&&nearEnough(up,tiltedUp,1e-12),
                  "M60 sustained full yaw never spins the up vector");
        }
        for(std::size_t axis=0;axis<3;++axis)
            check(nearEnough(turntable.basis()[axis],tiltedAxes[axis],1e-9),
                  "M60 one complete turn returns to identical tilted orientation");
        check(nearEnough(turntable.displayed(cyclePivot),cyclePivot,1e-8),
              "M60 yaw maintains the exact original source pivot");

        // Positive and negative pitch extremes stay short of the fixed
        // elevated camera's inversion; further mouse motion cannot flip.
        ViewportOrbit capped;
        capped.drag({},0,1000,720);
        const auto atUpper=capped.basis();
        capped.drag({},0,3000,720);
        for(std::size_t axis=0;axis<3;++axis)
            check(nearEnough(capped.basis()[axis],atUpper[axis],1e-12),
                  "M60 positive pitch stops at hard upright boundary");
        check(dot(capped.transformVector(worldUp),cameraUp)>0,
              "M60 positive pitch limit still projects world up above screen center");
        capped.drag({},0,-100,720);
        check(!nearEnough(capped.basis()[2],atUpper[2],1e-6),
              "M60 reverse dragging immediately moves off positive pitch boundary");
        capped.drag({},0,-4000,720);
        const auto atLower=capped.basis();
        capped.drag({},0,-1000,720);
        for(std::size_t axis=0;axis<3;++axis)
            check(nearEnough(capped.basis()[axis],atLower[axis],1e-12),
                  "M60 negative pitch stops at hard upright boundary");
        check(dot(capped.transformVector(worldUp),cameraUp)>0,
              "M60 negative pitch limit still projects world up above screen center");

        // Dolly translation and the screen-space background pivot remain
        // compatible with turntable angles; inverse picking still maps
        // visual rays exactly to the same immutable source.
        ViewportOrbit chained;
        chained.drag(cyclePivot,45,40,720);
        chained.dolly(centerForward,35,720,12);
        const Vec3 screenPivot=chained.choosePivot(
            visual,std::nullopt,centroid,centerForward);
        const Vec3 anchored=chained.displayed(screenPivot);
        chained.drag(screenPivot,75,-45,720);
        check(nearEnough(chained.displayed(screenPivot),anchored,1e-8),
              "M60 orbit after dolly preserves chosen screen-parallel source pivot");
        const Ray chainedSource=chained.pickingRay(visual);
        check(nearEnough(chained.displayed(chainedSource.origin),visual.origin,1e-8)&&
              nearEnough(chained.transformVector(chainedSource.direction),
                         visual.direction,1e-8),
              "M60 post-dolly turntable still preserves exact source picking");
        // The frame-span ratio may overflow even when raw inputs are finite.
        const auto preservedAxes=chained.basis();
        const auto preservedTranslation=chained.translation();
        chained.drag(screenPivot,500,300,
                     std::numeric_limits<double>::min()/2);
        chained.drag(screenPivot,
                     std::numeric_limits<double>::quiet_NaN(),10,720);
        for(std::size_t axis=0;axis<3;++axis)
            check(nearEnough(chained.basis()[axis],preservedAxes[axis]),
                  "M60 invalid/overflowing drag cannot poison view orientation");
        check(nearEnough(chained.translation(),preservedTranslation),
              "M60 invalid drag cannot move the source-space picking transform");

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
