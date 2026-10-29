#pragma once
#include "mesher/mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace mesher {

// Hue cycles in [0,1); all channel controls are normalized to [0,1].
// RGB is stored once in linear floats. Each model is a live VIEW of it;
// switching modes never quantizes or transforms the selected colour.
enum class PaintChannels {RGB,HSV,HSL};
inline std::array<double,3> rgbToHsv(VertexRGBA rgb) {
    const double hi=std::max({double(rgb.r),double(rgb.g),double(rgb.b)});
    const double lo=std::min({double(rgb.r),double(rgb.g),double(rgb.b)});
    const double d=hi-lo;
    double h=0;
    if(d>1e-12) {
        if(hi==rgb.r)h=std::fmod((rgb.g-rgb.b)/d,6.)/6.;
        else if(hi==rgb.g)h=((rgb.b-rgb.r)/d+2.)/6.;
        else h=((rgb.r-rgb.g)/d+4.)/6.;
    }
    if(h<0)h+=1;
    return {h,hi>0?d/hi:0,hi};
}
inline std::array<double,3> rgbToHsl(VertexRGBA rgb) {
    const auto hsv=rgbToHsv(rgb);
    const double hi=std::max({double(rgb.r),double(rgb.g),double(rgb.b)});
    const double lo=std::min({double(rgb.r),double(rgb.g),double(rgb.b)});
    const double l=(hi+lo)*.5;
    const double d=hi-lo;
    return {hsv[0],d>0?d/(1-std::abs(2*l-1)):0,l};
}
inline VertexRGBA hueToRgb(std::array<double,3> channel,
                            PaintChannels mode,float alpha) {
    const double h=channel[0]-std::floor(channel[0]);
    const double s=std::clamp(channel[1],0.,1.);
    const double v=std::clamp(channel[2],0.,1.);
    const double c=mode==PaintChannels::HSL?
        (1-std::abs(2*v-1))*s:v*s;
    const double x=c*(1-std::abs(std::fmod(h*6,2.)-1));
    const double m=mode==PaintChannels::HSL?v-c/2:v-c;
    std::array<double,3> rgb{};
    if(h<1./6)rgb={c,x,0};
    else if(h<2./6)rgb={x,c,0};
    else if(h<3./6)rgb={0,c,x};
    else if(h<4./6)rgb={0,x,c};
    else if(h<5./6)rgb={x,0,c};
    else rgb={c,0,x};
    return {float(rgb[0]+m),float(rgb[1]+m),float(rgb[2]+m),alpha};
}
struct PaintControls {
    VertexRGBA rgba{1.f,0.f,0.f,1.f};
    PaintChannels channels=PaintChannels::RGB;
    double hardness=0.5;

    std::array<double,3> values() const noexcept {
        switch(channels) {
        case PaintChannels::RGB:return {rgba.r,rgba.g,rgba.b};
        case PaintChannels::HSV:return rgbToHsv(rgba);
        case PaintChannels::HSL:return rgbToHsl(rgba);
        }
        return {rgba.r,rgba.g,rgba.b};
    }
    void setChannel(unsigned index,double value) {
        if(index>=3||!std::isfinite(value))
            throw std::invalid_argument("Invalid paint channel");
        value=std::clamp(value,0.,1.);
        auto v=values();
        v[index]=value;
        if(channels==PaintChannels::RGB) {
            rgba={float(v[0]),float(v[1]),float(v[2]),rgba.a};
        }else rgba=hueToRgb(v,channels,rgba.a);
    }
    void setWheel(double hue,double saturation) {
        if(!std::isfinite(hue)||!std::isfinite(saturation))
            throw std::invalid_argument("Invalid colour wheel coordinates");
        const auto target=channels==PaintChannels::HSL?
            PaintChannels::HSL:PaintChannels::HSV;
        auto hsv=target==PaintChannels::HSL?rgbToHsl(rgba):rgbToHsv(rgba);
        hsv[0]=hue;hsv[1]=std::clamp(saturation,0.,1.);
        rgba=hueToRgb(hsv,target,rgba.a);
    }
    void setAlpha(double value) {
        if(!std::isfinite(value))throw std::invalid_argument("Invalid alpha");
        rgba.a=float(std::clamp(value,0.,1.));
    }
    void setHardness(double value) {
        if(!std::isfinite(value))throw std::invalid_argument("Invalid hardness");
        hardness=std::clamp(value,0.,1.);
    }
};
}
