# M61: actual GLFW input, not a test-only helper, must be wired.
if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M61 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" view)
file(READ "${MESHER_SOURCE_ROOT}/include/mesher/viewport_orbit.hpp" helper)
foreach(needle
    "cameraGestures_.sample("
    "GLFW_MOUSE_BUTTON_LEFT"
    "GLFW_MOUSE_BUTTON_RIGHT"
    "GLFW_KEY_LEFT_CONTROL"
    "GLFW_KEY_RIGHT_CONTROL"
    "ViewportCameraMode::Strafe"
    "ViewportCameraMode::Dolly"
    "orbit_.strafe(viewRight,viewUp,dx,dy,double(fh),length(eye));"
    "orbit_.dolly(forward,dx,span,length(eye));"
    "if(orbitDragging_) {"
    "if(right&&!wasRight_&&!left)"
    "else if(left&&!wasLeft_&&!right&&ctrl)")
    string(FIND "${view}${helper}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "M61 actual Ctrl gesture route/suppression missing: ${needle}")
    endif()
endforeach()
string(FIND "${view}" "GLFW_KEY_LEFT_ALT" altView)
string(FIND "${view}" "GLFW_KEY_RIGHT_ALT" altView2)
if(NOT altView EQUAL -1 OR NOT altView2 EQUAL -1)
    message(FATAL_ERROR "M61 Alt must no longer select a camera control")
endif()
message(STATUS "M61 real Ctrl-only camera gesture routing and sculpt suppression certified")
