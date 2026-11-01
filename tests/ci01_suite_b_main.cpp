// CI01: distinct CTest registrations execute independent test entry points
// in separate processes, despite sharing the same linked executable.
#include <array>
#include <iostream>
#include <string_view>

int mesher_ci01_repair_r01_stock_sphere_diagnostic_main();
int mesher_ci01_repair_r06_m07_exact_case_roundtrip_main();
int mesher_ci01_repair_r06_m05_frozen_auto_inspection_main();
int mesher_ci01_repair_r06_m04c_enclosed_retirement_main();
int mesher_ci01_repair_r06_m13_cap_survivor_geometry_audit_main();
int mesher_ci01_repair_r06_m17c_opt_in_real_shape_density_handoff_main();
int mesher_ci01_repair_r06_m17b_canonical_two_sided_mirrored_parameter_split_main();
int mesher_ci01_repair_r06_m16d1_exact_effective_scene_and_diff_main();
int mesher_ci01_repair_r06_m16c2c_remaining_stage_operations_and_terminal_error_main();
int mesher_ci01_repair_r06_m16c2b_owned_stage_snapshot_and_id_diff_main();
int mesher_ci01_repair_r06_m15a_stage_owned_face_and_live_boundary_main();
int mesher_ci01_repair_r06_m14e_complete_private_patch_manifold_gate_main();
int mesher_ci01_repair_r06_m14d_source_witness_provenance_main();
int mesher_ci01_repair_r06_m11a3c_proven_planar_corner_bound_main();
int mesher_ci01_repair_r06_m11a3b_redundant_cap_fan_collapse_main();
int mesher_ci01_repair_r06_m11a3a_constrained_interior_fairing_main();
int mesher_ci01_repair_r06_m10_interior_diagonal_flips_main();
int mesher_ci01_repair_r06_m14c_source_backed_conforming_edge_split_main();

int main(int argc,char** argv) {
    using Test=int(*)();
    struct Entry {std::string_view name;Test run;};
    const std::array<Entry,18> tests{{
        {"repair_r01_stock_sphere_diagnostic",&mesher_ci01_repair_r01_stock_sphere_diagnostic_main},
        {"repair_r06_m07_exact_case_roundtrip",&mesher_ci01_repair_r06_m07_exact_case_roundtrip_main},
        {"repair_r06_m05_frozen_auto_inspection",&mesher_ci01_repair_r06_m05_frozen_auto_inspection_main},
        {"repair_r06_m04c_enclosed_retirement",&mesher_ci01_repair_r06_m04c_enclosed_retirement_main},
        {"repair_r06_m13_cap_survivor_geometry_audit",&mesher_ci01_repair_r06_m13_cap_survivor_geometry_audit_main},
        {"repair_r06_m17c_opt_in_real_shape_density_handoff",&mesher_ci01_repair_r06_m17c_opt_in_real_shape_density_handoff_main},
        {"repair_r06_m17b_canonical_two_sided_mirrored_parameter_split",&mesher_ci01_repair_r06_m17b_canonical_two_sided_mirrored_parameter_split_main},
        {"repair_r06_m16d1_exact_effective_scene_and_diff",&mesher_ci01_repair_r06_m16d1_exact_effective_scene_and_diff_main},
        {"repair_r06_m16c2c_remaining_stage_operations_and_terminal_error",&mesher_ci01_repair_r06_m16c2c_remaining_stage_operations_and_terminal_error_main},
        {"repair_r06_m16c2b_owned_stage_snapshot_and_id_diff",&mesher_ci01_repair_r06_m16c2b_owned_stage_snapshot_and_id_diff_main},
        {"repair_r06_m15a_stage_owned_face_and_live_boundary",&mesher_ci01_repair_r06_m15a_stage_owned_face_and_live_boundary_main},
        {"repair_r06_m14e_complete_private_patch_manifold_gate",&mesher_ci01_repair_r06_m14e_complete_private_patch_manifold_gate_main},
        {"repair_r06_m14d_source_witness_provenance",&mesher_ci01_repair_r06_m14d_source_witness_provenance_main},
        {"repair_r06_m11a3c_proven_planar_corner_bound",&mesher_ci01_repair_r06_m11a3c_proven_planar_corner_bound_main},
        {"repair_r06_m11a3b_redundant_cap_fan_collapse",&mesher_ci01_repair_r06_m11a3b_redundant_cap_fan_collapse_main},
        {"repair_r06_m11a3a_constrained_interior_fairing",&mesher_ci01_repair_r06_m11a3a_constrained_interior_fairing_main},
        {"repair_r06_m10_interior_diagonal_flips",&mesher_ci01_repair_r06_m10_interior_diagonal_flips_main},
        {"repair_r06_m14c_source_backed_conforming_edge_split",&mesher_ci01_repair_r06_m14c_source_backed_conforming_edge_split_main},
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
