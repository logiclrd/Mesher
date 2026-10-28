#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace mesher {

// Coordinates in GLFW screen-coordinate units; do not mix with framebuffer
// pixels. GLFW sizes and positions use the same units on HiDPI displays.
struct SplashRect {int x=0,y=0,width=0,height=0;};

inline std::pair<int,int> centerSplash(SplashRect anchor,int width,int height) {
    return {anchor.x+(anchor.width-width)/2,
            anchor.y+(anchor.height-height)/2};
}

// A real X11 session takes precedence over an incidental WAYLAND_DISPLAY
// inherited from a nested compositor (Heresy's KDE/X11 finding).
inline bool splashUsesWayland(std::string_view session,bool waylandDisplay) {
    if(session=="x11"||session=="X11")return false;
    if(session=="wayland"||session=="Wayland")return true;
    return waylandDisplay;
}

inline bool followSplashPlacement(double elapsedSeconds,bool ownerStateChanged) {
    return ownerStateChanged || elapsedSeconds<=0.050;
}

inline float splashFadeOpacity(double elapsedSeconds) {
    return static_cast<float>(std::clamp(elapsedSeconds/0.250,0.0,1.0));
}

inline bool splashHasTimedOut(double elapsedSeconds) {
    return elapsedSeconds>=4.0;
}

} // namespace mesher
