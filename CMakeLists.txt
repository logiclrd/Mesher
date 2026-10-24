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

add_library(mesher_mesh STATIC src/mesh.cpp src/sculpt_fixtures.cpp src/brush.cpp src/async_sculpt.cpp src/remesh.cpp src/refine.cpp src/picking.cpp src/spatial_grid.cpp src/versioned_mesh.cpp src/mesh_partitioning.cpp src/region_edit.cpp src/region_index.cpp src/revision_picking.cpp src/triangle_collision.cpp src/repair_inspection.cpp src/repair_case.cpp src/repair_preview.cpp src/repair_excision.cpp src/repair_zipper.cpp src/repair_cap.cpp src/repair_cap_spatial.cpp src/repair_mirror_geometry.cpp src/repair_survivors.cpp src/repair_enclosure.cpp src/repair_auto_plan.cpp src/repair_proximity_fitness.cpp src/repair_quality_flip.cpp src/repair_survivor_split.cpp src/repair_interior_fairing.cpp src/repair_interior_collapse.cpp src/repair_rim_quality.cpp src/repair_seam_audit.cpp src/repair_seam_provenance.cpp src/repair_mixed_stage.cpp src/repair_mixed_closure.cpp src/repair_auto_private.cpp src/offmode_collision.cpp src/offmode_clearance.cpp src/offmode_mirror.cpp src/repair_intersection.cpp src/repair_intersection_conform.cpp src/repair_intersection_arrange.cpp src/repair_intersection_mirror.cpp src/repair_intersection_commit.cpp src/repair_intersection_mirror_commit.cpp src/repair_intersection_classify.cpp src/repair_intersection_coverage.cpp src/repair_intersection_removal.cpp src/repair_intersection_uncut.cpp src/repair_intersection_surgery.cpp src/repair_intersection_join.cpp src/repair_intersection_step.cpp src/revision_winding.cpp src/repair_plan.cpp src/repair_closure.cpp src/repair_surgery.cpp src/repair_crossing.cpp src/repair_classified.cpp src/repair_fidelity.cpp src/repair_lifetime.cpp src/versioned_inflate.cpp src/versioned_refine.cpp src/versioned_collapse.cpp src/versioned_sculpt.cpp src/partition_render.cpp src/revision_retirement.cpp src/revision_history.cpp src/durable_checkpoint.cpp)
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
        # M73 CLI-only policy: all three sculpt mitigations ON by
        # default; explicit --no-* opt-outs, no legacy positive flags.
        add_test(NAME sculpt_cli_default_and_opt_out_contract
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_sculpt_cli_defaults.cmake")
        add_test(NAME sculpt_cli_explicit_opt_out_help
                 COMMAND Mesher --no-collapse --no-curvature-correction
                        --no-refine --help)
        set_tests_properties(sculpt_cli_explicit_opt_out_help PROPERTIES
            PASS_REGULAR_EXPRESSION "collapse/curvature/refine ON")
        add_test(NAME sculpt_cli_reject_legacy_collapse
                 COMMAND Mesher --collapse --help)
        set_tests_properties(sculpt_cli_reject_legacy_collapse PROPERTIES WILL_FAIL TRUE)
        add_test(NAME sculpt_cli_reject_legacy_curvature
                 COMMAND Mesher --curvature-correction --help)
        set_tests_properties(sculpt_cli_reject_legacy_curvature PROPERTIES WILL_FAIL TRUE)
        add_test(NAME sculpt_cli_reject_legacy_refine
                 COMMAND Mesher --refine --help)
        set_tests_properties(sculpt_cli_reject_legacy_refine PROPERTIES WILL_FAIL TRUE)
        # M06: all modes go through the REAL desktop argument parser,
        # without initializing Vulkan/GLFW or opening a checkpoint.
        add_test(NAME desktop_offmode_cli_fail_closed
                 COMMAND "${CMAKE_COMMAND}"
                    "-DMESHER_EXE=$<TARGET_FILE:Mesher>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_desktop_collision_cli.cmake")
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
    add_executable(mesher_sculpt_fixtures_tests tests/sculpt_fixtures_tests.cpp)
    target_link_libraries(mesher_sculpt_fixtures_tests PRIVATE mesher_mesh)
    add_test(NAME geometry_torus_hollow_sphere_fixtures COMMAND mesher_sculpt_fixtures_tests)
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
    # V18-M01: before/after immutable source contact comparison; this is
    # read-only and does not enable the OFF-mode admission policy.
    add_executable(mesher_repair_sphere_diagnostic tests/repair_inspection_sphere_diagnostic.cpp)
    target_link_libraries(mesher_repair_sphere_diagnostic PRIVATE mesher_mesh)
    add_test(NAME repair_r01_stock_sphere_diagnostic COMMAND mesher_repair_sphere_diagnostic)
    # R06-M07: preserve exactly the immutable post-stamp candidate + trusted
    # R01 contacts, replay with the CURRENT R02/M03/M04 code (no Vulkan).
    # The setup test emits real self-contained archive fixtures. Fixture
    # dependencies keep CLI replay smoke tests deterministic under -j.
    add_executable(mesher_repair_case_tests tests/repair_case_tests.cpp)
    target_link_libraries(mesher_repair_case_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m07_exact_case_roundtrip COMMAND mesher_repair_case_tests)
    set_tests_properties(repair_r06_m07_exact_case_roundtrip
        PROPERTIES FIXTURES_SETUP r06_case_archives)
    if(TARGET Mesher)
        add_test(NAME repair_r06_m07_cli_positive
            COMMAND Mesher --repair-replay-test
                    "${CMAKE_CURRENT_BINARY_DIR}/r06-sample-success.r06case")
        set_tests_properties(repair_r06_m07_cli_positive
            PROPERTIES FIXTURES_REQUIRED r06_case_archives
                       PASS_REGULAR_EXPRESSION "REPLAY_TEST PASS")
        add_test(NAME repair_r06_m07_cli_refusal
            COMMAND Mesher --repair-replay-test
                    "${CMAKE_CURRENT_BINARY_DIR}/r06-sample-refused.r06case")
        set_tests_properties(repair_r06_m07_cli_refusal
            PROPERTIES FIXTURES_REQUIRED r06_case_archives
                       PASS_REGULAR_EXPRESSION "REPLAY_TEST PASS")
        # A full closure can still be the WRONG classifier choice. CTest
        # intentionally expects exit 3 for a user-marked preferable ZIPPER
        # when actual M03 chose caps. This does not relax closure safety.
        add_test(NAME repair_r06_m07_cli_semantic_mismatch
            COMMAND "${CMAKE_COMMAND}"
              "-DMESHER_EXE=$<TARGET_FILE:Mesher>"
              "-DR06_CASE_FILE=${CMAKE_CURRENT_BINARY_DIR}/r06-sample-prefer-zipper.r06case"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_repair_case_choice.cmake")
        set_tests_properties(repair_r06_m07_cli_semantic_mismatch
            PROPERTIES FIXTURES_REQUIRED r06_case_archives)
        add_test(NAME repair_r06_m09_classifier_comparison_hud
            COMMAND "${CMAKE_COMMAND}"
              "-DMESHER_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_classifier_comparison_hud.cmake")
        add_test(NAME repair_r06_m08_same_candidate_two_classifiers
            COMMAND "${CMAKE_COMMAND}"
              "-DMESHER_EXE=$<TARGET_FILE:Mesher>"
              "-DR06_CASE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/repair-cases/refusal-2.r06case"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_repair_classifier_cli.cmake")
        add_test(NAME repair_r06_m12_quality_warn_exact_case_1
            COMMAND Mesher --repair-replay-test
              "${CMAKE_CURRENT_SOURCE_DIR}/repair-cases/refusal-1.r06case"
              --repair-quality-warn)
        set_tests_properties(repair_r06_m12_quality_warn_exact_case_1
            PROPERTIES PASS_REGULAR_EXPRESSION "QUALITY_WARNING_PREVIEW untrusted=1")
        add_test(NAME repair_r06_m13_exact_seam_refusal_1
            COMMAND Mesher --repair-replay-test
              "${CMAKE_CURRENT_SOURCE_DIR}/repair-cases/refusal-1.r06case"
              --repair-quality-warn --repair-seam-audit --repair-overlay-exact)
        set_tests_properties(repair_r06_m13_exact_seam_refusal_1
            PROPERTIES PASS_REGULAR_EXPRESSION "repair-r06-seam\\] SUMMARY")
        add_test(NAME repair_r06_m13_exact_seam_refusal_3
            COMMAND Mesher --repair-replay-test
              "${CMAKE_CURRENT_SOURCE_DIR}/repair-cases/refusal-3.r06case"
              --repair-quality-warn --repair-seam-audit --repair-overlay-exact)
        set_tests_properties(repair_r06_m13_exact_seam_refusal_3
            PROPERTIES PASS_REGULAR_EXPRESSION "repair-r06-seam\\] SUMMARY")
        # The five FULL physical 5K-vertex V1 cases, committed by the
        # operator on PR #150, are regression oracles. Do not silently
        # update expected failures if the next algorithm fixes them;
        # review the intended outcome then update/replace the fixture.
        foreach(case_id RANGE 1 5)
            if(case_id EQUAL 2 OR case_id EQUAL 4 OR case_id EQUAL 5)
                # Reviewed M10 recoveries: preserve the V1 archive's old
                # REFUSED bit, require explicit current CLOSED instead.
                add_test(NAME repair_r06_m07_physical_case_${case_id}
                    COMMAND "${CMAKE_COMMAND}"
                      "-DMESHER_EXE=$<TARGET_FILE:Mesher>"
                      "-DR06_CASE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/repair-cases/refusal-${case_id}.r06case"
                      "-DCASE_ID=${case_id}"
                      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_recovered_repair_case.cmake")
            else()
                add_test(NAME repair_r06_m07_physical_case_${case_id}
                    COMMAND Mesher --repair-replay-test
                      "${CMAKE_CURRENT_SOURCE_DIR}/repair-cases/refusal-${case_id}.r06case")
                set_tests_properties(repair_r06_m07_physical_case_${case_id}
                    PROPERTIES PASS_REGULAR_EXPRESSION "REPLAY_TEST PASS")
            endif()
        endforeach()
    endif()
    add_executable(mesher_repair_auto_inspection_tests tests/repair_auto_inspection_tests.cpp)
    target_link_libraries(mesher_repair_auto_inspection_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m05_frozen_auto_inspection COMMAND mesher_repair_auto_inspection_tests)
    add_executable(mesher_repair_auto_private_tests tests/repair_auto_private_tests.cpp)
    target_link_libraries(mesher_repair_auto_private_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m04c_enclosed_retirement COMMAND mesher_repair_auto_private_tests)
    add_executable(mesher_repair_seam_audit_tests tests/repair_seam_audit_tests.cpp)
    target_link_libraries(mesher_repair_seam_audit_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m13_cap_survivor_geometry_audit COMMAND mesher_repair_seam_audit_tests)
    add_executable(mesher_repair_mixed_staging_editor_tests tests/repair_mixed_staging_editor_tests.cpp)
    target_link_libraries(mesher_repair_mixed_staging_editor_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m15a_stage_owned_face_and_live_boundary
             COMMAND mesher_repair_mixed_staging_editor_tests)
    add_executable(mesher_repair_mixed_patch_tests tests/repair_mixed_patch_tests.cpp)
    target_link_libraries(mesher_repair_mixed_patch_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m14e_complete_private_patch_manifold_gate
             COMMAND mesher_repair_mixed_patch_tests)
    add_executable(mesher_repair_seam_provenance_tests tests/repair_seam_provenance_tests.cpp)
    target_link_libraries(mesher_repair_seam_provenance_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m14d_source_witness_provenance
             COMMAND mesher_repair_seam_provenance_tests)
    add_executable(mesher_repair_rim_quality_tests tests/repair_rim_quality_tests.cpp)
    target_link_libraries(mesher_repair_rim_quality_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m11a3c_proven_planar_corner_bound COMMAND mesher_repair_rim_quality_tests)
    add_executable(mesher_repair_interior_collapse_tests tests/repair_interior_collapse_tests.cpp)
    target_link_libraries(mesher_repair_interior_collapse_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m11a3b_redundant_cap_fan_collapse COMMAND mesher_repair_interior_collapse_tests)
    add_executable(mesher_repair_interior_fairing_tests tests/repair_interior_fairing_tests.cpp)
    target_link_libraries(mesher_repair_interior_fairing_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m11a3a_constrained_interior_fairing COMMAND mesher_repair_interior_fairing_tests)
    add_executable(mesher_repair_quality_flip_tests tests/repair_quality_flip_tests.cpp)
    target_link_libraries(mesher_repair_quality_flip_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m10_interior_diagonal_flips COMMAND mesher_repair_quality_flip_tests)
    add_executable(mesher_repair_mixed_edges_tests tests/repair_mixed_edges_tests.cpp)
    target_link_libraries(mesher_repair_mixed_edges_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m14c_source_backed_conforming_edge_split
             COMMAND mesher_repair_mixed_edges_tests)
    add_executable(mesher_repair_mixed_source_view_tests tests/repair_mixed_source_view_tests.cpp)
    target_link_libraries(mesher_repair_mixed_source_view_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m14a_source_backed_topology COMMAND mesher_repair_mixed_source_view_tests)
    add_executable(mesher_repair_mixed_stage_tests tests/repair_mixed_stage_tests.cpp)
    target_link_libraries(mesher_repair_mixed_stage_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m04a_mixed_face_staging COMMAND mesher_repair_mixed_stage_tests)
    add_executable(mesher_repair_auto_plan_tests tests/repair_auto_plan_tests.cpp)
    target_link_libraries(mesher_repair_auto_plan_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m03_automatic_closure_plan COMMAND mesher_repair_auto_plan_tests)
    add_executable(mesher_repair_enclosure_tests tests/repair_enclosure_tests.cpp)
    target_link_libraries(mesher_repair_enclosure_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m02_enclosed_component_evidence COMMAND mesher_repair_enclosure_tests)
    add_executable(mesher_repair_survivors_tests tests/repair_survivors_tests.cpp)
    target_link_libraries(mesher_repair_survivors_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m01_survivor_component_inventory COMMAND mesher_repair_survivors_tests)
    add_executable(mesher_repair_mirror_geometry_tests tests/repair_mirror_geometry_tests.cpp)
    target_link_libraries(mesher_repair_mirror_geometry_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m06c_mirrored_closure_geometry COMMAND mesher_repair_mirror_geometry_tests)
    add_executable(mesher_repair_spatial_cap_tests tests/repair_spatial_cap_tests.cpp)
    target_link_libraries(mesher_repair_spatial_cap_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r06_m06c_spatial_front_poisson COMMAND mesher_repair_spatial_cap_tests)
    add_executable(mesher_repair_cap_tests tests/repair_cap_tests.cpp)
    target_link_libraries(mesher_repair_cap_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r05_forced_independent_caps COMMAND mesher_repair_cap_tests)
    add_executable(mesher_repair_zipper_tests tests/repair_zipper_tests.cpp)
    target_link_libraries(mesher_repair_zipper_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r04_forced_zipper_refinement COMMAND mesher_repair_zipper_tests)
    add_executable(mesher_repair_excision_tests tests/repair_excision_tests.cpp)
    target_link_libraries(mesher_repair_excision_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r03_private_open_boundary_excision COMMAND mesher_repair_excision_tests)
    add_executable(mesher_repair_preview_tests tests/repair_preview_tests.cpp)
    target_link_libraries(mesher_repair_preview_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r02_private_excision_preview COMMAND mesher_repair_preview_tests)
    add_executable(mesher_repair_inspection_tests tests/repair_inspection_tests.cpp)
    target_link_libraries(mesher_repair_inspection_tests PRIVATE mesher_mesh)
    add_test(NAME repair_r01_frozen_intersection_inspection COMMAND mesher_repair_inspection_tests)
    add_executable(mesher_offmode_collision_tests tests/offmode_collision_tests.cpp)
    target_link_libraries(mesher_offmode_collision_tests PRIVATE mesher_mesh)
    add_test(NAME offmode_poststate_contact_ledger COMMAND mesher_offmode_collision_tests)
    # M02 adds the real opt-in pre-CAS worker rejection and FIFO recovery,
    # still without a desktop CLI mode or a default runtime behavior change.
    add_executable(mesher_offmode_step_gate_tests tests/offmode_step_gate_tests.cpp)
    target_link_libraries(mesher_offmode_step_gate_tests PRIVATE mesher_mesh)
    add_test(NAME offmode_private_step_gate COMMAND mesher_offmode_step_gate_tests)
    add_executable(mesher_offmode_clearance_tests tests/offmode_clearance_tests.cpp)
    target_link_libraries(mesher_offmode_clearance_tests PRIVATE mesher_mesh)
    add_test(NAME offmode_mesh_scale_clearance COMMAND mesher_offmode_clearance_tests)
    add_executable(mesher_offmode_backtracking_tests tests/offmode_backtracking_tests.cpp)
    target_link_libraries(mesher_offmode_backtracking_tests PRIVATE mesher_mesh)
    add_test(NAME offmode_bounded_normal_backtracking COMMAND mesher_offmode_backtracking_tests)
    add_executable(mesher_v18_integrated_acceptance_tests tests/v18_integrated_acceptance_tests.cpp)
    target_link_libraries(mesher_v18_integrated_acceptance_tests PRIVATE mesher_mesh)
    add_test(NAME v18_integrated_near_sheet_checkpoint_acceptance
             COMMAND mesher_v18_integrated_acceptance_tests)
    set_tests_properties(v18_integrated_near_sheet_checkpoint_acceptance PROPERTIES TIMEOUT 90)
    add_executable(mesher_desktop_collision_policy_tests tests/desktop_collision_policy_tests.cpp)
    target_link_libraries(mesher_desktop_collision_policy_tests PRIVATE mesher_mesh)
    add_test(NAME desktop_offmode_policy_and_recovery COMMAND mesher_desktop_collision_policy_tests)
    add_executable(mesher_offmode_mirrored_transaction_tests tests/offmode_mirrored_transaction_tests.cpp)
    target_link_libraries(mesher_offmode_mirrored_transaction_tests PRIVATE mesher_mesh)
    add_test(NAME offmode_reflected_opposing_sheet_transactions COMMAND mesher_offmode_mirrored_transaction_tests)
    set_tests_properties(offmode_reflected_opposing_sheet_transactions PROPERTIES TIMEOUT 90)
    set_tests_properties(offmode_bounded_normal_backtracking PROPERTIES TIMEOUT 90)
    set_tests_properties(offmode_private_step_gate PROPERTIES TIMEOUT 90)
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
