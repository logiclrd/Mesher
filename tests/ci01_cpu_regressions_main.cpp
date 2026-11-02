// CI01: dispatcher only. Each original test translation unit still owns its
// original main body, compiled with an isolated per-source entry-point rename.
// CTest invokes a fresh process for every test, preserving independent stderr,
// stdout, exit codes, timeouts and -R filtering.
#include <array>
#include <iostream>
#include <string_view>

int mesher_ci01_positive_winding_classification_main();
int mesher_ci01_density_aware_excision_plans_main();
int mesher_ci01_topology_aware_closure_fitness_main();
int mesher_ci01_private_repair_surgery_main();
int mesher_ci01_bounded_revision_retirement_main();
int mesher_ci01_spatial_queries_main();
int mesher_ci01_immutable_vertex_rgba_and_shadowed_paint_main();
int mesher_ci01_versioned_mesh_storage_main();
int mesher_ci01_mesh_partitioning_main();
int mesher_ci01_region_edit_transactions_main();
int mesher_ci01_incremental_mesh_indices_main();
int mesher_ci01_versioned_inflate_local_baseline_main();
int mesher_ci01_versioned_refine_transactions_main();
int mesher_ci01_versioned_collapse_transactions_main();
int mesher_ci01_dirty_partition_rendering_main();
int mesher_ci01_screen_space_aa_wire_quad_encoding_main();

int main(int argc,char** argv) {
    using Test=int(*)();
    struct Entry {std::string_view name;Test run;};
    const std::array<Entry,16> tests{{
        {"positive_winding_classification",&mesher_ci01_positive_winding_classification_main},
        {"density_aware_excision_plans",&mesher_ci01_density_aware_excision_plans_main},
        {"topology_aware_closure_fitness",&mesher_ci01_topology_aware_closure_fitness_main},
        {"private_repair_surgery",&mesher_ci01_private_repair_surgery_main},
        {"bounded_revision_retirement",&mesher_ci01_bounded_revision_retirement_main},
        {"spatial_queries",&mesher_ci01_spatial_queries_main},
        {"immutable_vertex_rgba_and_shadowed_paint",&mesher_ci01_immutable_vertex_rgba_and_shadowed_paint_main},
        {"versioned_mesh_storage",&mesher_ci01_versioned_mesh_storage_main},
        {"mesh_partitioning",&mesher_ci01_mesh_partitioning_main},
        {"region_edit_transactions",&mesher_ci01_region_edit_transactions_main},
        {"incremental_mesh_indices",&mesher_ci01_incremental_mesh_indices_main},
        {"versioned_inflate_local_baseline",&mesher_ci01_versioned_inflate_local_baseline_main},
        {"versioned_refine_transactions",&mesher_ci01_versioned_refine_transactions_main},
        {"versioned_collapse_transactions",&mesher_ci01_versioned_collapse_transactions_main},
        {"dirty_partition_rendering",&mesher_ci01_dirty_partition_rendering_main},
        {"screen_space_aa_wire_quad_encoding",&mesher_ci01_screen_space_aa_wire_quad_encoding_main},
    }};
    if(argc!=2) {
        std::cerr<<"Expected one CTest name identifying a CPU regression\n";
        return 2;
    }
    const std::string_view requested=argv[1];
    for(const auto& test:tests)
        if(test.name==requested)return test.run();
    std::cerr<<"Unknown consolidated CPU regression: "<<requested<<"\n";
    return 2;
}
