#pragma once

#include "mesher/startup_splash_policy.hpp"

#include <filesystem>
#include <vector>

struct GLFWwindow;

namespace mesher {

struct SplashPixels {
    int width=0,height=0;
    std::vector<unsigned char> rgba;
};

// Use the committed PNG icon converted without a runtime image dependency.
// GLFW's window icon API is available on Windows/X11; native Wayland may
// ignore requests because the compositor owns application icons.
// Resolve shipped shaders and images relative to the executable, not
// the build tree or current working directory.
std::filesystem::path mesherExecutableDirectory();

void setMesherWindowIcon(GLFWwindow* window,
                         const std::filesystem::path& assets);

// Separate owned nonmodal OpenGL/GLFW splash window. The actual Mesher
// viewport is Vulkan and continues initialization and rendering normally.
class StartupSplash {
public:
    StartupSplash(GLFWwindow* owner,const std::filesystem::path& assets);
    ~StartupSplash();
    StartupSplash(const StartupSplash&)=delete;
    StartupSplash& operator=(const StartupSplash&)=delete;
    // Begin a terminal dismissal now: hide before the next event-loop tick
    // so the floating OS window cannot flash over the already-visible editor.
    void dismiss(const char* reason="owner input") noexcept;
    void tick();
    bool active() const noexcept {return splash_!=nullptr;}
private:
    void close();
    void recenter();
    void repaint();
    GLFWwindow* owner_{};
    GLFWwindow* splash_{};
    SplashPixels picture_;
    bool explicitPosition_=true;
    bool dismissedForFocus_=false;
    bool shown_=false; // true only after the splash presentation starts
    double openedAt_=0;
    double lastOpacity_=-1;
    SplashRect ownerBounds_{};
    SplashRect splashBounds_{};
    int imageFbWidth_=0,imageFbHeight_=0;
    bool dismissRequested_=false;
    bool returnFocusAfterDismiss_=false;
};

} // namespace mesher
