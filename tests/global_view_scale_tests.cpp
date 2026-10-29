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
            viewportScaleLabel(0)=="10^+0.0000",
            "Hover readout carries four decimal places and the exponent sign");
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
    orbit.setScale(.04);
    require(close(orbit.scale(),.04)&&
            close(length(orbit.displayed(sourceCenter)-orbit.translation()),0.0),
            "Slider changes view scale without disturbing scene centre or pan");
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
