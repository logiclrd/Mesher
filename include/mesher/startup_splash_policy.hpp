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

// Once the visible splash loses focus, dismissal is terminal even if it
// was created with GLFW_FOCUSED=false and never received keyboard focus.
// A compositor/WM can obscure a transient child on a click before GLFW has
// delivered any button press to that child; "focusedOnce" is not a valid
// prerequisite for recognizing that transition.
inline bool splashShouldDismissOnFocusLoss(bool wasShown,bool focused) noexcept {
    return wasShown&&!focused;
}

// Fade and placement updates must never run after logical dismissal.
// The lifecycle owner is responsible for closing the already-hidden window.
inline bool splashShouldRender(bool wasShown,bool dismissalRequested) noexcept {
    return wasShown&&!dismissalRequested;
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
