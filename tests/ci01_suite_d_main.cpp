// CI01: distinct CTest registrations execute independent test entry points
// in separate processes, despite sharing the same linked executable.
#include <array>
#include <iostream>
#include <string_view>

int mesher_ci01_repair_r04_forced_zipper_refinement_main();
int mesher_ci01_repair_r03_private_open_boundary_excision_main();
int mesher_ci01_repair_r02_private_excision_preview_main();
int mesher_ci01_repair_r01_frozen_intersection_inspection_main();
int mesher_ci01_offmode_poststate_contact_ledger_main();
int mesher_ci01_offmode_private_step_gate_main();
int mesher_ci01_offmode_mesh_scale_clearance_main();
int mesher_ci01_offmode_bounded_normal_backtracking_main();
int mesher_ci01_v18_integrated_near_sheet_checkpoint_acceptance_main();
int mesher_ci01_desktop_offmode_policy_and_recovery_main();
int mesher_ci01_offmode_reflected_opposing_sheet_transactions_main();
int mesher_ci01_persistent_history_checkpoints_main();
int mesher_ci01_asynchronous_sculpting_main();
int mesher_ci01_accepted_union_structural_audit_main();

int main(int argc,char** argv) {
    using Test=int(*)();
    struct Entry {std::string_view name;Test run;};
    const std::array<Entry,14> tests{{
        {"repair_r04_forced_zipper_refinement",&mesher_ci01_repair_r04_forced_zipper_refinement_main},
        {"repair_r03_private_open_boundary_excision",&mesher_ci01_repair_r03_private_open_boundary_excision_main},
        {"repair_r02_private_excision_preview",&mesher_ci01_repair_r02_private_excision_preview_main},
        {"repair_r01_frozen_intersection_inspection",&mesher_ci01_repair_r01_frozen_intersection_inspection_main},
        {"offmode_poststate_contact_ledger",&mesher_ci01_offmode_poststate_contact_ledger_main},
        {"offmode_private_step_gate",&mesher_ci01_offmode_private_step_gate_main},
        {"offmode_mesh_scale_clearance",&mesher_ci01_offmode_mesh_scale_clearance_main},
        {"offmode_bounded_normal_backtracking",&mesher_ci01_offmode_bounded_normal_backtracking_main},
        {"v18_integrated_near_sheet_checkpoint_acceptance",&mesher_ci01_v18_integrated_near_sheet_checkpoint_acceptance_main},
        {"desktop_offmode_policy_and_recovery",&mesher_ci01_desktop_offmode_policy_and_recovery_main},
        {"offmode_reflected_opposing_sheet_transactions",&mesher_ci01_offmode_reflected_opposing_sheet_transactions_main},
        {"persistent_history_checkpoints",&mesher_ci01_persistent_history_checkpoints_main},
        {"asynchronous_sculpting",&mesher_ci01_asynchronous_sculpting_main},
        {"accepted_union_structural_audit",&mesher_ci01_accepted_union_structural_audit_main},
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
