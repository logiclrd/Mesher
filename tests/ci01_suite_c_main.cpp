// CI01: distinct CTest registrations execute independent test entry points
// in separate processes, despite sharing the same linked executable.
#include <array>
#include <iostream>
#include <string_view>

int mesher_ci01_repair_r06_m14a_source_backed_topology_main();
int mesher_ci01_repair_r06_m04a_mixed_face_staging_main();
int mesher_ci01_repair_r06_m03_automatic_closure_plan_main();
int mesher_ci01_repair_r06_m02_enclosed_component_evidence_main();
int mesher_ci01_repair_r06_m01_survivor_component_inventory_main();
int mesher_ci01_repair_r06_m06c_mirrored_closure_geometry_main();
int mesher_ci01_repair_r06_m22a_topology_independent_poisson_main();
int mesher_ci01_repair_r06_m22b_cap_selection_and_metrics_main();
int mesher_ci01_repair_r07_certified_live_publication_transition_main();
int mesher_ci01_repair_r07_refusal6_checkpoint_zipper_recovery_main();
int mesher_ci01_repair_r06_zipper_shape_templates_main();
int mesher_ci01_repair_r06_m22b_adaptive_front_main();
int mesher_ci01_repair_r06_m23b_refusal2_new_zipper_private_main();
int mesher_ci01_repair_r06_m23b_finite_bridge_private_main();
int mesher_ci01_repair_r06_m23a_optional_placement_callback_main();
int mesher_ci01_repair_r05_forced_independent_caps_main();

int main(int argc,char** argv) {
    using Test=int(*)();
    struct Entry {std::string_view name;Test run;};
    const std::array<Entry,16> tests{{
        {"repair_r06_m14a_source_backed_topology",&mesher_ci01_repair_r06_m14a_source_backed_topology_main},
        {"repair_r06_m04a_mixed_face_staging",&mesher_ci01_repair_r06_m04a_mixed_face_staging_main},
        {"repair_r06_m03_automatic_closure_plan",&mesher_ci01_repair_r06_m03_automatic_closure_plan_main},
        {"repair_r06_m02_enclosed_component_evidence",&mesher_ci01_repair_r06_m02_enclosed_component_evidence_main},
        {"repair_r06_m01_survivor_component_inventory",&mesher_ci01_repair_r06_m01_survivor_component_inventory_main},
        {"repair_r06_m06c_mirrored_closure_geometry",&mesher_ci01_repair_r06_m06c_mirrored_closure_geometry_main},
        {"repair_r06_m22a_topology_independent_poisson",&mesher_ci01_repair_r06_m22a_topology_independent_poisson_main},
        {"repair_r06_m22b_cap_selection_and_metrics",&mesher_ci01_repair_r06_m22b_cap_selection_and_metrics_main},
        {"repair_r07_certified_live_publication_transition",&mesher_ci01_repair_r07_certified_live_publication_transition_main},
        {"repair_r07_refusal6_checkpoint_zipper_recovery",&mesher_ci01_repair_r07_refusal6_checkpoint_zipper_recovery_main},
        {"repair_r06_zipper_shape_templates",&mesher_ci01_repair_r06_zipper_shape_templates_main},
        {"repair_r06_m22b_adaptive_front",&mesher_ci01_repair_r06_m22b_adaptive_front_main},
        {"repair_r06_m23b_refusal2_new_zipper_private",&mesher_ci01_repair_r06_m23b_refusal2_new_zipper_private_main},
        {"repair_r06_m23b_finite_bridge_private",&mesher_ci01_repair_r06_m23b_finite_bridge_private_main},
        {"repair_r06_m23a_optional_placement_callback",&mesher_ci01_repair_r06_m23a_optional_placement_callback_main},
        {"repair_r05_forced_independent_caps",&mesher_ci01_repair_r05_forced_independent_caps_main},
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
