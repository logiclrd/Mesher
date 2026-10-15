#include "mesher/mesh.hpp"
#include "mesher/brush.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now()-start).count();
}
int main(int argc,char** argv) {
    try {
        mesher::SphereOptions opt;
        opt.vertexCount=argc>1?static_cast<std::uint32_t>(std::stoul(argv[1])):100000;
        opt.relaxationIterations=argc>2?static_cast<std::uint32_t>(std::stoul(argv[2])):65;
        const auto start=Clock::now();
        mesher::SphereGenerationTimings generation{};
        auto mesh=mesher::generateSphere(opt,&generation);
        const double generated=seconds(start);
        std::cerr<<"Build="<<MESHER_BUILD_CONFIGURATION
                 <<" hull="<<mesher::sphereHullBackend()
                 <<" OpenMP_max_threads="<<mesher::sphereRelaxationThreads()
                 <<" relaxation="<<generation.relaxationSeconds
                 <<" hull_time="<<generation.triangulationSeconds
                 <<" validation="<<generation.validationSeconds<<" s\n";
        auto run=[&](bool remesh) {
            auto current=mesh;
            mesher::CollapseSettings collapse;
            collapse.enabled=remesh;
            mesher::RefineSettings refine;
            refine.enabled=remesh;
            mesher::AdaptiveInflateBrush brush(current,.012,{true,.25},
                                                collapse,refine,true);
            mesher::BrushStroke stroke(current,brush,.02);
            const auto stampStart=Clock::now();
            stroke.startTimed({{{.3,-.85,.45},.4,.3},
                               {{-.3,-.85,.45},.4,.3}},.2);
            const std::array<mesher::Vec3,2> points{{{.3,-.85,.45},
                                                    {-.3,-.85,.45}}};
            // Exactly one stationary stamp (0.1 seconds at 0.2 units/sec).
            const auto first=brush.lastTimings();
            stroke.updateTimedRealtime(points,.1);
            const auto second=brush.lastTimings();
            const double elapsed=seconds(stampStart);
            stroke.finishTimedRealtime(points,.1);
            std::cerr<<(remesh?"with remesh":"without remesh")
                     <<" phases: inflate="<<(first.inflateSeconds+second.inflateSeconds)
                     <<" refine="<<(first.refineSeconds+second.refineSeconds)
                     <<" collapse="<<(first.collapseSeconds+second.collapseSeconds)
                     <<" adjacency-rebuild="<<(first.rebuildSeconds+second.rebuildSeconds)
                     <<" seconds\n";
            return elapsed;
        };
        const double stamp=run(false);
        const double remeshStamp=run(true);
        std::cout<<std::fixed<<std::setprecision(5)
                 <<"benchmark,vertices,relaxation_iterations,generation_s,2_stamps_s,2_stamps_remesh_s\n"
                 <<"mesh,"<<opt.vertexCount<<","<<opt.relaxationIterations<<","
                 <<generated<<","<<stamp<<","<<remeshStamp<<"\n";
    }catch(const std::exception& e) {
        std::cerr<<"Benchmark failed: "<<e.what()<<'\n';return 1;
    }
}
