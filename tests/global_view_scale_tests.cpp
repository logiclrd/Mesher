#include "mesher/viewport_scale.hpp"
#include "mesher/viewport_orbit.hpp"
#include "mesher/picking.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace mesher;
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
bool close(double a,double b,double tolerance=1e-9) {
    return std::abs(a-b)<=tolerance*std::max({1.0,std::abs(a),std::abs(b)});
}
void testSlider() {
    require(viewportScaleFromExponent(0)==1.0,"Generated geometry starts at 10^0");
    require(close(viewportScaleFromExponent(-5),1e-5)&&
            close(viewportScaleFromExponent(5),1e5),
            "Scale endpoints are inclusive");
    require(viewportScaleFromExponent(-10)==1e-5&&
            viewportScaleFromExponent(10)==1e5,
            "Exponent clamps to -5 through +5");
    require(close(viewportScaleDrag(0,-20,200,false),1.0),
            "Twenty pixels is one evenly spaced power-of-ten step");
    require(close(viewportScaleDrag(0,-20,200,true),1.0/15),
            "Ctrl-drag changes exponent exactly 15 times slower");
    require(viewportScaleDrag(5,-2000,200,false)==5&&
            viewportScaleDrag(-5,2000,200,false)==-5,
            "Dragging outside the track clamps at both endpoints");
    require(viewportScaleLabel(-3.2455)=="10^-3.2455"&&
            viewportScaleLabel(0)=="10^0.0000"&&
            viewportScaleLabel(3)=="10^3.0000",
            "Hover readout uses four decimals without an extraneous plus sign");
}
void testBoundsAndFit() {
    ViewportSceneBounds duck;
    duck.include({-50,-30,160});
    duck.include({80,40,300});
    require(duck.hasVertices&&close(duck.center().x,15)&&
            close(duck.center().y,5)&&close(duck.center().z,230),
            "Off-origin model framing uses bounding-box centre, not world zero");
    const auto eye=viewportCameraEye(1440,900);
    const auto exponent=viewportFitExponent(duck.radius(),length(eye),1440.0/900);
    require(exponent<-1&&exponent>-4,
            "Hundreds-of-units PLY auto-fits without manual dolly");
    ViewportSceneBounds tiny;
    tiny.include({1e-8,-1e-8,0});
    tiny.include({2e-8,1e-8,1e-8});
    require(viewportFitExponent(tiny.radius(),length(eye),1.6)==5,
            "Tiny input clamps at 10^+5");
    ViewportSceneBounds huge;
    huge.include({-1e10,-1e10,-1e10});
    huge.include({1e10,1e10,1e10});
    require(viewportFitExponent(huge.radius(),length(eye),1.6)==-5,
            "Extremely large input clamps at 10^-5");
}
void testPickingAndOrbit() {
    const Vec3 sourceCenter{120.0,-40.0,245.0};
    ViewportOrbit orbit;
    orbit.frame(sourceCenter,0.008);
    require(close(orbit.scale(),0.008)&&
            close(length(orbit.displayed(sourceCenter)),0.0),
            "Source centre becomes view origin without changing source coordinates");
    const auto view=viewportRay(600,400,1200,800);
    const auto source=orbit.pickingRay(view);
    const auto viewBack=orbit.displayed(source.origin);
    require(close(length(viewBack-view.origin),0.0),
            "Inverse picking preserves the framebuffer eye after scene scaling");
    require(close(source.direction.x,view.direction.x)&&
            close(source.direction.y,view.direction.y)&&
            close(source.direction.z,view.direction.z),
            "Scaling changes ray origin but not normalized direction");
    const Vec3 pivot=sourceCenter+Vec3{10,3,2};
    const auto before=orbit.displayed(pivot);
    orbit.drag(pivot,50,-30,800);
    const auto after=orbit.displayed(pivot);
    require(close(length(before-after),0.0),
            "Turntable orbit preserves the source-space pivot with global scale");
    const auto rayAfter=orbit.pickingRay(view);
    require(close(length(orbit.displayed(rayAfter.origin)-view.origin),0.0),
            "Picking remains inverse under turntable rotation");
    // Camera anchor is an original source vertex, including for an
    // off-centre, previously rotated view.
    ViewportScaleFollow follow;
    const Vec3 baselineEye{0,-6,2};
    const Vec3 anchor=sourceCenter+Vec3{10,3,2};
    const Vec3 anchorBefore=orbit.displayed(anchor);
    const Vec3 originalCameraToAnchor=baselineEye-anchorBefore;
    follow.begin(anchor,anchorBefore,baselineEye,0.0);
    require(follow.active()&&
            close(length(follow.sourceAnchor()-anchor),0.0),
            "First scale movement latches one original-space anchor");
    orbit.setScale(0.08);
    const Vec3 anchorAfter=orbit.displayed(anchor);
    const Vec3 expectedCamera=anchorAfter+originalCameraToAnchor;
    follow.change(anchorAfter,baselineEye,0.01);
    require(close(length(baselineEye+follow.targetCameraOffset()-
                         expectedCamera),0.0),
            "Camera target follows scaled off-centre point, preserving full 3D vector");
    follow.advance(1.0/60.0,0.01+1.0/60.0);
    require(close(length(follow.cameraOffset()-
                         follow.targetCameraOffset()*0.8),0.0),
            "Single 60Hz frame covers exactly 80 percent of target displacement");
    ViewportScaleFollow halfFrames;
    halfFrames.begin(anchor,anchorBefore,baselineEye,0);
    halfFrames.change(anchorAfter,baselineEye,0.01);
    halfFrames.advance(1.0/120,0.01+1.0/120);
    halfFrames.advance(1.0/120,0.01+1.0/60);
    require(close(length(halfFrames.cameraOffset()-
                         follow.cameraOffset()),0.0),
            "Two 120Hz frames match one 60Hz smoothing frame");
    const Vec3 intermediate=follow.cameraOffset();
    follow.advance(1.0/60,1.95);
    require(follow.active(),"Operation persists until 2 seconds without change");
    orbit.setScale(0.8);
    follow.change(orbit.displayed(anchor),baselineEye,1.96);
    require(follow.active()&&
            length(follow.cameraOffset()-intermediate)>0,
            "Within a gesture, anchor stays fixed while camera target updates");
    follow.advance(1.0/60,3.94);
    require(follow.active(),"Changing scale resets idle expiry");
    follow.advance(1.0/60,3.97);
    require(!follow.active(),"Gesture ends after a full two-second idle");
    follow.cancel();
    require(close(length(follow.targetCameraOffset()-follow.cameraOffset()),0),
            "Manual camera gesture cancels any residual automatic drift");
    follow.reset();
    require(close(length(follow.cameraOffset()),0)&&!follow.active(),
            "Independent Open resets camera offset and gesture");
    const std::vector<ViewportZoomProbe> probes{
        {sourceCenter+Vec3{1,0,0},9,true},
        {sourceCenter+Vec3{0,1,0},4,true},
        {sourceCenter+Vec3{0,0,1},7,true},
        {sourceCenter,1,false}};
    const auto picked=nearestViewportProbe(probes,sourceCenter,3);
    require(picked.surface&&picked.distance==4&&
            close(length(picked.sourcePoint-
                 (sourceCenter+Vec3{0,1,0})),0),
            "Across all sixteen sample rays choose nearest real surface hit");
    const auto fallback=nearestViewportProbe({},sourceCenter,3);
    require(!fallback.surface&&fallback.distance==3&&
            close(length(fallback.sourcePoint-sourceCenter),0),
            "Empty grid falls back to centre-plane distance and mesh centroid");
    require(close(viewportZoomFollowFraction(1.0/60.0),0.8)&&
            viewportZoomFollowFraction(0)==0,
            "Frame-rate-independent tracking has correct origin and 60Hz reference");
}
void testClipping() {
    const Vec3 eye=viewportCameraEye(1200,800);
    auto near=viewportClipRange(eye,{},1);
    require(near.nearPlane>0&&near.farPlane>=100,
            "Normal scene has a finite positive depth interval");
    const auto remote=viewportClipRange(eye,{0,0,-20000},2500);
    require(remote.farPlane>22000&&remote.nearPlane>0&&
            remote.nearPlane<remote.farPlane,
            "Dolly and large scene bounds increase far clip automatically");
    const auto inside=viewportClipRange({0,0,0},{0,0,0},500);
    require(inside.nearPlane>0&&inside.farPlane>500,
            "Camera inside enlarged model can still draw exterior depth");
}
}
int main() {
    try {
        testSlider();testBoundsAndFit();testPickingAndOrbit();testClipping();
        std::cout<<"UX08 logarithmic framing, clip range and inverse picking passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"UX08 regression: "<<error.what()<<'\n';
        return 1;
    }
}
