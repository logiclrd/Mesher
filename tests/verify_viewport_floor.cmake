if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M57 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" source)
file(READ "${MESHER_SOURCE_ROOT}/include/mesher/viewport_floor.hpp" floor)
foreach(needle
    "Drawing buildWorkFloor(double z)"
    "floorZ_=viewportWorkFloorZ(mesh_);"
    "const auto scenery=buildWorkFloor(floorZ_);"
    "floorSolid=upload(scenery.solid);"
    "floorLines=uploadWire(scenery.lines);"
    "Buffer solid{},lines{},floorSolid{},floorLines{},hud{};"
    "destroyBuffer(floorSolid);destroyBuffer(floorLines);"
    "if(justPressed(GLFW_KEY_G,floorKeyWasDown_))"
    "if(floorVisible_) {")
    string(FIND "${source}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M57 floor draw/scene contract missing: ${needle}")
    endif()
endforeach()
string(REGEX MATCHALL "if\\(floorVisible_\\) \\{" conditionalDraws "${source}")
list(LENGTH conditionalDraws count)
if(count LESS 2)
    message(FATAL_ERROR "M57 G must independently gate solid floor AND wire grid")
endif()
string(FIND "${floor}" "minimum<legacySphereFloor?minimum-clearance:legacySphereFloor" bound)
if(bound EQUAL -1)
    message(FATAL_ERROR "M57 floor height is no longer source-bounded")
endif()
message(STATUS "M57 Vulkan scenery geometry isolation and G display guard verified")
