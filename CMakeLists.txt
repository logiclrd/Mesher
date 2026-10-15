cmake_minimum_required(VERSION 3.22)
project(Mesher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MESHER_BUILD_VIEWPORT "Build the Vulkan/GLFW 3D viewport" ON)
option(MESHER_BUILD_TESTS "Build CPU-only mesh tests" ON)

add_library(mesher_mesh STATIC src/mesh.cpp src/brush.cpp src/remesh.cpp src/refine.cpp src/picking.cpp src/spatial_grid.cpp)
target_include_directories(mesher_mesh PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(mesher_mesh PUBLIC cxx_std_20)
# Relaxation iterations read a frozen vertex grid and update independent
# destination vertices. OpenMP is optional; the serial path stays supported.
find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(mesher_mesh PUBLIC OpenMP::OpenMP_CXX)
endif()

if(MSVC)
    target_compile_options(mesher_mesh PRIVATE /W4)
else()
    target_compile_options(mesher_mesh PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MESHER_BUILD_VIEWPORT)
    find_package(Vulkan REQUIRED)
    find_package(glfw3 3.3 REQUIRED)
    find_program(GLSLC_EXECUTABLE NAMES glslc glslangValidator REQUIRED)

    set(SHADERS mesh.vert mesh.frag wire.vert wire.frag hud.vert hud.frag)
    set(SPIRV_FILES)
    foreach(SHADER IN LISTS SHADERS)
        set(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}")
        set(DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.spv")
        if(GLSLC_EXECUTABLE MATCHES "glslangValidator$")
            add_custom_command(OUTPUT "${DESTINATION}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
                COMMAND "${GLSLC_EXECUTABLE}" -V "${SOURCE}" -o "${DESTINATION}"
                DEPENDS "${SOURCE}" VERBATIM)
        else()
            add_custom_command(OUTPUT "${DESTINATION}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
                COMMAND "${GLSLC_EXECUTABLE}" "${SOURCE}" -o "${DESTINATION}"
                DEPENDS "${SOURCE}" VERBATIM)
        endif()
        list(APPEND SPIRV_FILES "${DESTINATION}")
    endforeach()
    add_custom_target(mesher_shaders DEPENDS ${SPIRV_FILES})
    add_executable(Mesher src/main.cpp src/viewport.cpp)
    add_dependencies(Mesher mesher_shaders)
    target_link_libraries(Mesher PRIVATE mesher_mesh Vulkan::Vulkan glfw)
    if(UNIX AND NOT APPLE)
        # EWMH fullscreen is a window-manager request, not a monitor-owned
        # GLFW fullscreen context. The X11 path is optional so CPU builds and
        # non-X11 platforms continue to work without extra dependencies.
        find_package(X11 QUIET)
        if(X11_FOUND)
            target_link_libraries(Mesher PRIVATE X11::X11)
            target_compile_definitions(Mesher PRIVATE MESHER_X11_WM_FULLSCREEN=1)
        endif()
    endif()
    target_compile_definitions(Mesher PRIVATE MESHER_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
endif()

if(MESHER_BUILD_TESTS)
    enable_testing()
    add_executable(mesher_mesh_tests tests/mesh_tests.cpp)
    target_link_libraries(mesher_mesh_tests PRIVATE mesher_mesh)
    add_test(NAME mesh_generation COMMAND mesher_mesh_tests)
    add_executable(mesher_brush_tests tests/brush_tests.cpp)
    target_link_libraries(mesher_brush_tests PRIVATE mesher_mesh)
    add_test(NAME brushes COMMAND mesher_brush_tests)
    add_executable(mesher_remesh_tests tests/remesh_tests.cpp)
    target_link_libraries(mesher_remesh_tests PRIVATE mesher_mesh)
    add_test(NAME directional_collapse COMMAND mesher_remesh_tests)
    add_executable(mesher_refine_tests tests/refine_tests.cpp)
    target_link_libraries(mesher_refine_tests PRIVATE mesher_mesh)
    add_test(NAME local_refinement COMMAND mesher_refine_tests)
    add_executable(mesher_picking_tests tests/picking_tests.cpp)
    target_link_libraries(mesher_picking_tests PRIVATE mesher_mesh)
    add_test(NAME mouse_picking COMMAND mesher_picking_tests)
    add_executable(mesher_spatial_tests tests/spatial_tests.cpp)
    target_link_libraries(mesher_spatial_tests PRIVATE mesher_mesh)
    add_test(NAME spatial_queries COMMAND mesher_spatial_tests)
    add_executable(mesher_benchmark tests/performance_benchmark.cpp)
    target_link_libraries(mesher_benchmark PRIVATE mesher_mesh)
endif()
