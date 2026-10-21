cmake_minimum_required(VERSION 3.22)
project(Mesher VERSION 0.1.0 LANGUAGES CXX)

# Single-config generators otherwise default to unoptimized (no -O flags).
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
message(STATUS "Mesher build configuration: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MESHER_BUILD_VIEWPORT "Build the Vulkan/GLFW 3D viewport" ON)
option(MESHER_BUILD_TESTS "Build CPU-only mesh tests" ON)

# Treat warnings as build failures for every Mesher-owned C++ target:
# executable, library, test suites and benchmark. Imported dependencies are
# compiled by their providers and are not modified by this setting.
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_library(mesher_mesh STATIC src/mesh.cpp src/brush.cpp src/async_sculpt.cpp src/remesh.cpp src/refine.cpp src/picking.cpp src/spatial_grid.cpp src/versioned_mesh.cpp src/mesh_partitioning.cpp src/region_edit.cpp src/region_index.cpp src/revision_picking.cpp src/triangle_collision.cpp src/repair_intersection.cpp src/repair_intersection_conform.cpp src/repair_intersection_arrange.cpp src/repair_intersection_mirror.cpp src/repair_intersection_commit.cpp src/repair_intersection_mirror_commit.cpp src/repair_intersection_classify.cpp src/repair_intersection_coverage.cpp src/repair_intersection_removal.cpp src/repair_intersection_uncut.cpp src/repair_intersection_surgery.cpp src/repair_intersection_join.cpp src/repair_intersection_step.cpp src/revision_winding.cpp src/repair_plan.cpp src/repair_closure.cpp src/repair_surgery.cpp src/repair_crossing.cpp src/repair_classified.cpp src/repair_fidelity.cpp src/repair_lifetime.cpp src/versioned_inflate.cpp src/versioned_refine.cpp src/versioned_collapse.cpp src/versioned_sculpt.cpp src/partition_render.cpp src/revision_retirement.cpp src/revision_history.cpp src/durable_checkpoint.cpp)
target_include_directories(mesher_mesh PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(mesher_mesh PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(mesher_mesh PUBLIC Threads::Threads)
target_compile_definitions(mesher_mesh PUBLIC
    MESHER_BUILD_CONFIGURATION="$<CONFIG>")

# Previously src/hull_qhull.cpp was checked in but never compiled or linked!
# Do not silently fall back to the quadratic implementation for large models.
option(MESHER_FIND_QHULL "Use system Qhull for fast sphere triangulation" ON)
if(MESHER_FIND_QHULL)
    find_package(Qhull QUIET)
endif()
if(MESHER_FIND_QHULL AND TARGET Qhull::qhull_r)
    target_sources(mesher_mesh PRIVATE src/hull_qhull.cpp)
    target_link_libraries(mesher_mesh PUBLIC Qhull::qhull_r)
    target_compile_definitions(mesher_mesh PRIVATE MESHER_USE_QHULL=1)
    message(STATUS "Mesher hull backend: Qhull (fast, large-mesh capable)")
else()
    message(WARNING
        "Mesher hull backend: quadratic fallback, limited to 10000 vertices. "
        "For large spheres install libqhull-dev and reconfigure.")
endif()
# Relaxation iterations read a frozen vertex grid and update independent
# destination vertices. OpenMP is optional; the serial path stays supported.
find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(mesher_mesh PUBLIC OpenMP::OpenMP_CXX)
endif()

if(MESHER_BUILD_VIEWPORT)
    find_package(Vulkan REQUIRED)
    find_package(glfw3 3.3 REQUIRED)
    find_program(GLSLC_EXECUTABLE NAMES glslc glslangValidator
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)

    set(SHADERS mesh.vert mesh.frag hud.vert hud.frag)
    set(SPIRV_FILES)
    foreach(SHADER IN LISTS SHADERS)
        set(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}")
        set(DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.spv")
        if(GLSLC_EXECUTABLE MATCHES "glslangValidator(\\.exe)?$")
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
    if(MSVC)
        # The lightweight CLI acceptance paths (--help and invalid
        # --merge-geometry combinations) intentionally return BEFORE
        # glfwInit or any Vulkan API call. Windows CI's stripped-down
        # Vulkan SDK provides vulkan-1.lib but omits vulkan-1.dll.
        # Delay-load the Vulkan import library so those GPU-free CLI
        # checks can execute without a system Vulkan loader. The real
        # interactive viewport still requires vulkan-1.dll on first use,
        # and this never substitutes a mock implementation.
        target_link_options(Mesher PRIVATE "/DELAYLOAD:vulkan-1.dll")
        target_link_libraries(Mesher PRIVATE delayimp)
    endif()
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
    # M49: run real desktop argument parsing WITHOUT creating Vulkan or a
    # window. These checks are additive to the 23 existing CPU suites.
    # Invalid combinations must fail closed before GPU initialization.
    if(MESHER_BUILD_VIEWPORT)
        # M59: indexed COW has intentionally empty optional legacy buffers.
        add_test(NAME viewport_cow_empty_optional_buffers
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_viewport_optional_buffers.cmake")
        # M56: real shader text/pipeline invariants in addition to GLSL
        # compilation and full Vulkan-enabled desktop builds.
        add_test(NAME viewport_floor_vulkan_separation_contract
                 COMMAND "${CMAKE_COMMAND}"
                     "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_viewport_floor.cmake")
        add_test(NAME viewport_surface_aa_pipeline_contract
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_aa_wire_quads.cmake")
        add_test(NAME viewport_wire_coverage_calibration_contract
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_wire_coverage.cmake")
        # M64: analytical diagnosis with the exact pre-fix wire shader
        # and BOTH independent surface/floor representations.
        add_test(NAME viewport_pole_wire_depth_diagnosis_contract
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_wire_pole_diagnosis.cmake")
        add_test(NAME viewport_visible_surface_mesh_edges_contract
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_surface_mesh_edges.cmake")
        add_test(NAME viewport_surface_attached_floor_grid_contract
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_surface_floor_grid.cmake")
        add_test(NAME viewport_no_detached_wire_gpu_resources
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_no_detached_wire_gpu.cmake")
        add_test(NAME viewport_wire_depth_occlusion_contract
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_wire_occlusion.cmake")
        add_test(NAME viewport_ctrl_camera_gesture_contract
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_ctrl_camera_gestures.cmake")
        add_test(NAME desktop_merge_default_off_help COMMAND Mesher --help)
        set_tests_properties(desktop_merge_default_off_help PROPERTIES
            PASS_REGULAR_EXPRESSION "Mouse Inflate by default")
        add_test(NAME desktop_merge_reject_demo COMMAND Mesher --merge-geometry --demo)
        set_tests_properties(desktop_merge_reject_demo PROPERTIES WILL_FAIL TRUE)
        add_test(NAME desktop_merge_reject_recovery COMMAND Mesher --merge-geometry --recover m49-not-a-real-checkpoint.mshr)
        set_tests_properties(desktop_merge_reject_recovery PROPERTIES WILL_FAIL TRUE)
        add_test(NAME desktop_merge_reject_noninteractive COMMAND Mesher --merge-geometry --no-inflate)
        set_tests_properties(desktop_merge_reject_noninteractive PROPERTIES WILL_FAIL TRUE)
        # M50: source-positive fixture requires BOTH deliberate protected
        # authorization and asymmetric mode, without starting any GPU.
        add_test(NAME desktop_merge_fixture_reject_unarmed
                 COMMAND Mesher --merge-test-tetra --asymmetric)
        set_tests_properties(desktop_merge_fixture_reject_unarmed PROPERTIES
            WILL_FAIL TRUE)
        add_test(NAME desktop_merge_fixture_reject_symmetric
                 COMMAND Mesher --merge-geometry --merge-test-tetra --symmetric)
        set_tests_properties(desktop_merge_fixture_reject_symmetric PROPERTIES
            WILL_FAIL TRUE)
        # M51: a reflected fixture is only an explicitly protected and
        # symmetric fresh ORIGINAL, never an arbitrary recovered mesh.
        add_test(NAME desktop_merge_reflected_reject_unarmed
                 COMMAND Mesher --merge-test-reflected-tetra)
        set_tests_properties(desktop_merge_reflected_reject_unarmed PROPERTIES
            WILL_FAIL TRUE)
        add_test(NAME desktop_merge_reflected_reject_asymmetric
                 COMMAND Mesher --merge-geometry --asymmetric --merge-test-reflected-tetra)
        set_tests_properties(desktop_merge_reflected_reject_asymmetric PROPERTIES
            WILL_FAIL TRUE)
        add_test(NAME desktop_merge_reflected_reject_conflicting_fixture
                 COMMAND Mesher --merge-geometry --merge-test-tetra --merge-test-reflected-tetra)
        set_tests_properties(desktop_merge_reflected_reject_conflicting_fixture PROPERTIES
            WILL_FAIL TRUE)
    endif()
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
    add_executable(mesher_surface_floor_grid_tests tests/surface_floor_grid_tests.cpp)
    target_link_libraries(mesher_surface_floor_grid_tests PRIVATE mesher_mesh)
    add_test(NAME visible_floor_grid_pixel_distance COMMAND mesher_surface_floor_grid_tests)
    add_executable(mesher_visible_mesh_edge_tests tests/visible_mesh_edge_tests.cpp)
    target_link_libraries(mesher_visible_mesh_edge_tests PRIVATE mesher_mesh)
    add_test(NAME surface_attached_mesh_edge_distance COMMAND mesher_visible_mesh_edge_tests)
    add_executable(mesher_wire_pole_depth_tests tests/wire_pole_depth_tests.cpp)
    target_link_libraries(mesher_wire_pole_depth_tests PRIVATE mesher_mesh)
    add_test(NAME wire_pole_depth_self_occlusion_diagnosis COMMAND mesher_wire_pole_depth_tests)
    add_executable(mesher_picking_tests tests/picking_tests.cpp)
    target_link_libraries(mesher_picking_tests PRIVATE mesher_mesh)
    add_test(NAME mouse_picking COMMAND mesher_picking_tests)
    add_executable(mesher_viewport_floor_tests tests/viewport_floor_tests.cpp)
    target_link_libraries(mesher_viewport_floor_tests PRIVATE mesher_mesh)
    add_test(NAME viewport_floor_fixture_clearance COMMAND mesher_viewport_floor_tests)
    add_executable(mesher_revision_picking_tests tests/revision_picking_tests.cpp)
    target_link_libraries(mesher_revision_picking_tests PRIVATE mesher_mesh)
    add_test(NAME revision_bvh_picking COMMAND mesher_revision_picking_tests)
    add_executable(mesher_collision_tests tests/revision_collision_tests.cpp)
    target_link_libraries(mesher_collision_tests PRIVATE mesher_mesh)
    add_test(NAME revision_triangle_collisions COMMAND mesher_collision_tests)
    add_executable(mesher_winding_tests tests/revision_winding_tests.cpp)
    target_link_libraries(mesher_winding_tests PRIVATE mesher_mesh)
    add_test(NAME positive_winding_classification COMMAND mesher_winding_tests)
    add_executable(mesher_repair_plan_tests tests/repair_plan_tests.cpp)
    target_link_libraries(mesher_repair_plan_tests PRIVATE mesher_mesh)
    add_test(NAME density_aware_excision_plans COMMAND mesher_repair_plan_tests)
    add_executable(mesher_repair_closure_tests tests/repair_closure_tests.cpp)
    target_link_libraries(mesher_repair_closure_tests PRIVATE mesher_mesh)
    add_test(NAME topology_aware_closure_fitness COMMAND mesher_repair_closure_tests)
    add_executable(mesher_repair_surgery_tests tests/repair_surgery_tests.cpp)
    target_link_libraries(mesher_repair_surgery_tests PRIVATE mesher_mesh)
    add_test(NAME private_repair_surgery COMMAND mesher_repair_surgery_tests)
    add_executable(mesher_retirement_tests tests/revision_retirement_tests.cpp)
    target_link_libraries(mesher_retirement_tests PRIVATE mesher_mesh)
    add_test(NAME bounded_revision_retirement COMMAND mesher_retirement_tests)
    add_executable(mesher_history_tests tests/revision_history_tests.cpp)
    target_link_libraries(mesher_history_tests PRIVATE mesher_mesh)
    add_test(NAME persistent_history_checkpoints COMMAND mesher_history_tests)
    # A normal execution is ~0.01s. Bound intermittent hangs, so CTest
    # records a failing diagnostic instead of blocking CI indefinitely.
    set_tests_properties(persistent_history_checkpoints PROPERTIES TIMEOUT 90)
    add_executable(mesher_spatial_tests tests/spatial_tests.cpp)
    target_link_libraries(mesher_spatial_tests PRIVATE mesher_mesh)
    add_test(NAME spatial_queries COMMAND mesher_spatial_tests)
    add_executable(mesher_async_tests tests/async_sculpt_tests.cpp)
    target_link_libraries(mesher_async_tests PRIVATE mesher_mesh)
    add_test(NAME asynchronous_sculpting COMMAND mesher_async_tests)
    # M54 is an independent full accepted FIRST-original worker Boolean
    # audit, not merely a private planner or a GPU screenshot. One
    # original/accepted/undo/redo root is exhaustively checked.
    add_executable(mesher_accepted_union_audit_tests
                   tests/accepted_union_audit_tests.cpp)
    target_link_libraries(mesher_accepted_union_audit_tests PRIVATE mesher_mesh)
    add_test(NAME accepted_union_structural_audit
             COMMAND mesher_accepted_union_audit_tests)
    set_tests_properties(accepted_union_structural_audit PROPERTIES TIMEOUT 90)
    add_executable(mesher_versioned_mesh_tests tests/versioned_mesh_tests.cpp)
    target_link_libraries(mesher_versioned_mesh_tests PRIVATE mesher_mesh)
    add_test(NAME versioned_mesh_storage COMMAND mesher_versioned_mesh_tests)
    add_executable(mesher_partitioning_tests tests/mesh_partitioning_tests.cpp)
    target_link_libraries(mesher_partitioning_tests PRIVATE mesher_mesh)
    add_test(NAME mesh_partitioning COMMAND mesher_partitioning_tests)
    add_executable(mesher_region_edit_tests tests/region_edit_tests.cpp)
    target_link_libraries(mesher_region_edit_tests PRIVATE mesher_mesh)
    add_test(NAME region_edit_transactions COMMAND mesher_region_edit_tests)
    add_executable(mesher_incremental_index_tests tests/incremental_index_tests.cpp)
    target_link_libraries(mesher_incremental_index_tests PRIVATE mesher_mesh)
    add_test(NAME incremental_mesh_indices COMMAND mesher_incremental_index_tests)
    add_executable(mesher_versioned_inflate_tests tests/versioned_inflate_tests.cpp)
    target_link_libraries(mesher_versioned_inflate_tests PRIVATE mesher_mesh)
    add_test(NAME versioned_inflate_local_baseline COMMAND mesher_versioned_inflate_tests)
    add_executable(mesher_versioned_refine_tests tests/versioned_refine_tests.cpp)
    target_link_libraries(mesher_versioned_refine_tests PRIVATE mesher_mesh)
    add_test(NAME versioned_refine_transactions COMMAND mesher_versioned_refine_tests)
    add_executable(mesher_versioned_collapse_tests tests/versioned_collapse_tests.cpp)
    target_link_libraries(mesher_versioned_collapse_tests PRIVATE mesher_mesh)
    add_test(NAME versioned_collapse_transactions COMMAND mesher_versioned_collapse_tests)
    add_executable(mesher_partition_render_tests tests/partition_render_tests.cpp)
    target_link_libraries(mesher_partition_render_tests PRIVATE mesher_mesh)
    add_test(NAME dirty_partition_rendering COMMAND mesher_partition_render_tests)
    add_executable(mesher_wire_quad_tests tests/wire_quads_tests.cpp)
    target_link_libraries(mesher_wire_quad_tests PRIVATE mesher_mesh)
    add_test(NAME screen_space_aa_wire_quad_encoding COMMAND mesher_wire_quad_tests)
    add_executable(mesher_benchmark tests/performance_benchmark.cpp)
    target_link_libraries(mesher_benchmark PRIVATE mesher_mesh)
endif()
