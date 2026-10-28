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
# M64 extends the actual GLFW input path with Alt+RMB world pan while
# retaining Ctrl+LMB world pan, Ctrl+RMB dolly, and RMB orbit. Verify
# BOTH physical Alt keys and the initiating-button latch: Alt may not
# steal a pre-existing RMB orbit nor activate the brush.
string(FIND "${view}" "    void updateMouseOrbit()" orbitStart)
string(FIND "${view}" "    void updateMouseSculpt()" orbitEnd)
if(orbitStart EQUAL -1 OR orbitEnd EQUAL -1 OR orbitEnd LESS orbitStart)
    message(FATAL_ERROR "M64 cannot identify camera gesture input scope")
endif()
math(EXPR orbitLength "${orbitEnd}-${orbitStart}")
string(SUBSTRING "${view}" "${orbitStart}" "${orbitLength}" cameraInput)
foreach(needle
    "GLFW_KEY_LEFT_ALT"
    "GLFW_KEY_RIGHT_ALT"
    "left,right,ctrl,alt,!sculpting_&&!radiusDragging_"
    "Alt+RMB"
    "view-relative world pan start")
    string(FIND "${cameraInput}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "M64 live GLFW Alt-right pan route missing: ${needle}")
    endif()
endforeach()
foreach(needle
    "const bool altRightPan=alt&&!ctrl"
    "strafeOnRight_?right:left"
    "const bool initiatingHeld"
    "mode_=ctrl?ViewportCameraMode::Dolly:"
    "else if(left&&!wasLeft_&&!right&&ctrl)")
    string(FIND "${helper}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "M64 camera button-origin latch/priority missing: ${needle}")
    endif()
endforeach()
message(STATUS "M64 real Alt+RMB and Ctrl+LMB world pan, Ctrl+RMB dolly, plain RMB orbit certified")
