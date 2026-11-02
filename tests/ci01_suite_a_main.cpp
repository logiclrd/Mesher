// CI01: distinct CTest registrations execute independent test entry points
// in separate processes, despite sharing the same linked executable.
#include <array>
#include <iostream>
#include <string_view>

int mesher_ci01_ux09_new_model_private_candidate_and_modal_policy_main();
int mesher_ci01_ux09_filename_provenance_and_embedded_vocabulary_main();
int mesher_ci01_geometry_torus_hollow_sphere_fixtures_main();
int mesher_ci01_mesh_export_atomic_snapshot_and_fifo_main();
int mesher_ci01_obj_export_formats_and_roundtrips_main();
int mesher_ci01_ply_export_encoding_colour_roundtrips_main();
int mesher_ci01_stl_export_dialect_and_colour_roundtrips_main();
int mesher_ci01_stl_dual_facet_colour_diffusion_main();
int mesher_ci01_stl_colour_benchmark_main();
int mesher_ci01_brushes_main();
int mesher_ci01_directional_collapse_main();
int mesher_ci01_local_refinement_main();
int mesher_ci01_visible_floor_grid_pixel_distance_main();
int mesher_ci01_surface_attached_mesh_edge_distance_main();
int mesher_ci01_wire_pole_depth_self_occlusion_diagnosis_main();
int mesher_ci01_mouse_picking_main();
int mesher_ci01_viewport_floor_fixture_clearance_main();
int mesher_ci01_global_logarithmic_view_scale_and_picking_main();
int mesher_ci01_revision_bvh_picking_main();
int mesher_ci01_revision_triangle_collisions_main();

int main(int argc,char** argv) {
    using Test=int(*)();
    struct Entry {std::string_view name;Test run;};
    const std::array<Entry,20> tests{{
        {"ux09_new_model_private_candidate_and_modal_policy",&mesher_ci01_ux09_new_model_private_candidate_and_modal_policy_main},
        {"ux09_filename_provenance_and_embedded_vocabulary",&mesher_ci01_ux09_filename_provenance_and_embedded_vocabulary_main},
        {"geometry_torus_hollow_sphere_fixtures",&mesher_ci01_geometry_torus_hollow_sphere_fixtures_main},
        {"mesh_export_atomic_snapshot_and_fifo",&mesher_ci01_mesh_export_atomic_snapshot_and_fifo_main},
        {"obj_export_formats_and_roundtrips",&mesher_ci01_obj_export_formats_and_roundtrips_main},
        {"ply_export_encoding_colour_roundtrips",&mesher_ci01_ply_export_encoding_colour_roundtrips_main},
        {"stl_export_dialect_and_colour_roundtrips",&mesher_ci01_stl_export_dialect_and_colour_roundtrips_main},
        {"stl_dual_facet_colour_diffusion",&mesher_ci01_stl_dual_facet_colour_diffusion_main},
        {"stl_colour_benchmark",&mesher_ci01_stl_colour_benchmark_main},
        {"brushes",&mesher_ci01_brushes_main},
        {"directional_collapse",&mesher_ci01_directional_collapse_main},
        {"local_refinement",&mesher_ci01_local_refinement_main},
        {"visible_floor_grid_pixel_distance",&mesher_ci01_visible_floor_grid_pixel_distance_main},
        {"surface_attached_mesh_edge_distance",&mesher_ci01_surface_attached_mesh_edge_distance_main},
        {"wire_pole_depth_self_occlusion_diagnosis",&mesher_ci01_wire_pole_depth_self_occlusion_diagnosis_main},
        {"mouse_picking",&mesher_ci01_mouse_picking_main},
        {"viewport_floor_fixture_clearance",&mesher_ci01_viewport_floor_fixture_clearance_main},
        {"global_logarithmic_view_scale_and_picking",&mesher_ci01_global_logarithmic_view_scale_and_picking_main},
        {"revision_bvh_picking",&mesher_ci01_revision_bvh_picking_main},
        {"revision_triangle_collisions",&mesher_ci01_revision_triangle_collisions_main},
    }};
    if(argc!=2) {
        std::cerr<<"Expected one registered CTest name\n";
        return 2;
    }
    const std::string_view requested=argv[1];
    for(const auto& test:tests)
        if(test.name==requested)return test.run();
    std::cerr<<"Unknown CI01 test: "<<requested<<"\n";
    return 2;
}
