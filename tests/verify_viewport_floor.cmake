if(NOT DEFINED MESHER_SOURCE_ROOT)
    message(FATAL_ERROR "M57 source root required")
endif()
file(READ "${MESHER_SOURCE_ROOT}/src/viewport.cpp" source)
file(READ "${MESHER_SOURCE_ROOT}/include/mesher/viewport_floor.hpp" floor)
foreach(needle
    "Drawing buildWorkFloor(double z,Vec3 center={},double halfExtent=8.0)"
    "floorZ_=viewportWorkFloorZ(mesh_);"
    "const auto scenery=interaction_.startupBounds?"
    "buildWorkFloor(floorZ_,sceneBounds_.center(),"
    "buildWorkFloor(floorZ_);"
    "floorZ_=prepared.bounds->minimum.z-"
    "floorSolid=upload(scenery.solid);"
    "Buffer solid{},floorSolid{},hud{};"
    "destroyBuffer(floorSolid);"
    "if(justPressed(GLFW_KEY_G,floorKeyWasDown_))"
    "if(floorVisible_) {")
    string(FIND "${source}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "M57 floor draw/scene contract missing: ${needle}")
    endif()
endforeach()
string(REGEX MATCHALL "if\\(floorVisible_\\) \\{" conditionalDraws "${source}")
list(LENGTH conditionalDraws count)
if(count LESS 1)
    message(FATAL_ERROR "M57 G must gate the solid floor")
endif()
file(READ "${MESHER_SOURCE_ROOT}/shaders/mesh.frag" fragment)
string(FIND "${source}" "constants.wireRaster[3]=wireVisible_?1.0f:0.0f;" wSwitch)
string(FIND "${fragment}" "nearest=abs(worldPosition.xy-round(worldPosition.xy));" gridShader)
if(wSwitch EQUAL -1 OR gridShader EQUAL -1)
    message(FATAL_ERROR "M68 independent W/G floor grid requires solid-depth surface coverage")
endif()
string(FIND "${floor}" "minimum<legacySphereFloor?minimum-clearance:legacySphereFloor" bound)
if(bound EQUAL -1)
    message(FATAL_ERROR "M57 floor height is no longer source-bounded")
endif()
message(STATUS "M57 Vulkan scenery geometry isolation and G display guard verified")
