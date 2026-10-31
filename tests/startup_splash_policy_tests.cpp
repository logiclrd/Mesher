#include "mesher/startup_splash_policy.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void require(bool passed,const char* message) {
    if(!passed)throw std::runtime_error(message);
}
}

int main() {
    try {
        using namespace mesher;
        // Owner coordinates may be negative on monitors to the left.
        require(centerSplash({-1600,120,1200,760},800,280)==
                    std::pair<int,int>{-1400,360},
                "Splash must center over actual nonprimary owner");
        require(centerSplash({1920,0,2560,1370},800,280)==
                    std::pair<int,int>{2800,545},
                "Fullscreen owner centers relative to its actual monitor");
        require(!splashUsesWayland("x11",true),
                "Explicit X11 session wins over incidental WAYLAND_DISPLAY");
        require(splashUsesWayland("wayland",false)&&
                splashUsesWayland("",true)&&
                !splashUsesWayland("x11",false),
                "Wayland detection is session-aware");
        require(followSplashPlacement(0.020,false)&&
                followSplashPlacement(0.050,false)&&
                !followSplashPlacement(0.051,false)&&
                followSplashPlacement(0.5,true),
                "Follow early WM moves for 50ms and late owner geometry transitions");
        require(std::abs(splashFadeOpacity(0.0f)-0.0f)<1e-6f&&
                std::abs(splashFadeOpacity(0.125f)-0.5f)<1e-6f&&
                std::abs(splashFadeOpacity(0.250f)-1.0f)<1e-6f&&
                std::abs(splashFadeOpacity(4.0f)-1.0f)<1e-6f,
                "Splash fades from transparent to opaque over 250ms");
        require(!splashHasTimedOut(3.999)&&splashHasTimedOut(4.0),
                "Splash timeout is four seconds");
        // The OS may not grant the floating splash keyboard focus
        // (GLFW_FOCUSED=false), yet clicking it can cause a WM focus-loss
        // callback and visual disappearance. That is a terminal dismissal,
        // not merely a transient lowering until four seconds elapse.
        require(splashShouldDismissOnFocusLoss(true,false),
                "Focus loss must dismiss a shown splash even without prior focus");
        require(!splashShouldDismissOnFocusLoss(false,false)&&
                !splashShouldDismissOnFocusLoss(true,true),
                "Do not dismiss an unshown or still-focused splash");
        bool dismissed=false;
        require(splashShouldRender(true,dismissed),
                "Shown undismissed splash must remain eligible for fade-in");
        dismissed=splashShouldDismissOnFocusLoss(true,false);
        require(!splashShouldRender(true,dismissed)&&
                splashHasTimedOut(4.0)&&
                !splashShouldRender(true,dismissed),
                "Splash dismissed on focus loss must never reappear at timeout");
        dismissed=true; // mouse/keyboard dismissal follows the same terminal rule
        require(!splashShouldRender(true,dismissed),
                "Explicit mouse dismissal must disable all future fades");

        // On X11 the WM can swallow the activation click without forwarding
        // a GLFW ButtonPress or focus transition. The native XQueryPointer
        // fallback must recognize a fresh press inside the splash rectangle.
        const SplashRect imageRect{0,0,640,360};
        require(splashPointerDownInside(false,true,320,150,imageRect)&&
                splashPointerDownInside(false,true,0,0,imageRect)&&
                splashPointerDownInside(false,true,639,359,imageRect),
                "X11 first click inside splash must initiate dismissal");
        require(!splashPointerDownInside(false,true,-1,150,imageRect)&&
                !splashPointerDownInside(false,true,640,150,imageRect)&&
                !splashPointerDownInside(false,true,320,360,imageRect),
                "X11 native pointer outside splash must not dismiss");
        require(!splashPointerDownInside(true,true,320,150,imageRect)&&
                !splashPointerDownInside(false,false,320,150,imageRect),
                "A held button or button release must not count as a new press");
        // The pointer-down result must remain terminal, even after the
        // original timeout would have elapsed.
        bool dismissedByPointer=splashPointerDownInside(
            false,true,320,150,imageRect);
        require(dismissedByPointer&&
                !splashShouldRender(true,dismissedByPointer)&&
                splashHasTimedOut(4.0),
                "Pointer dismissal must prevent later timeout exposure");
        std::cout<<"Startup splash placement and lifecycle policy passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Startup splash policy failed: "<<e.what()<<"\n";
        return 1;
    }
}
