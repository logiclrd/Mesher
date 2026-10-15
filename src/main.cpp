#include "mesher/brush.hpp"
#include "mesher/mesh.hpp"
#include "mesher/viewport.hpp"

#include <array>
#include <charconv>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <string>
#include <string_view>
#include <memory>
#include <iomanip>
#include <stdexcept>

int main(int argc, char** argv) {
    mesher::SphereOptions options;
    bool inflate = true, demoMode = false;
    double step = .02, strength = .012, brushRadius = .4, stampRate = 10.0, timeSpeed = .2;
    std::uint32_t segments = 128;
    mesher::CollapseSettings collapseSettings;
    bool compareRemesh = false, compareMitigations = false;
    mesher::CurvatureSettings curvatureSettings;
    mesher::RefineSettings refineSettings;
    bool compareLocalRemesh = false;
    try {
        for(int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            if(arg=="--asymmetric") options.symmetric=false;
            else if(arg=="--collapse") collapseSettings.enabled=true;
            else if(arg=="--refine") refineSettings.enabled=true;
            else if(arg=="--no-edge-flips") refineSettings.optimizeFlips=false;
            else if(arg=="--compare-local-remesh") compareLocalRemesh=true;
            else if(arg=="--compare-remesh") compareRemesh=true;
            else if(arg=="--compare-mitigations") compareMitigations=true;
            else if(arg=="--curvature-correction") curvatureSettings.enabled=true;
            else if(arg=="--no-inflate") inflate=false;
            else if(arg=="--demo") demoMode=true;
            else if(arg=="--step" || arg=="--strength" || arg=="--brush-radius" || arg=="--stamps-per-second" || arg=="--minimum-edge-ratio" || arg=="--curvature-strength" || arg=="--maximum-edge-ratio" || arg=="--time-speed") {
                if(++i>=argc) throw std::invalid_argument("Missing value after "+std::string(arg));
                const std::string value=argv[i];
                std::size_t parsed=0;
                const double v=std::stod(value,&parsed);
                if(parsed!=value.size() || !std::isfinite(v) ||
                   ((arg=="--step" || arg=="--brush-radius" || arg=="--stamps-per-second" || arg=="--minimum-edge-ratio" || arg=="--time-speed") && v<=0))
                    throw std::invalid_argument("Invalid brush parameter: "+value);
                if(arg=="--step")step=v;
                else if(arg=="--brush-radius")brushRadius=v;
                else if(arg=="--stamps-per-second")stampRate=v;
                else if(arg=="--time-speed")timeSpeed=v;
                else if(arg=="--minimum-edge-ratio")collapseSettings.minimumEdgeRatio=v;
                else if(arg=="--maximum-edge-ratio"){
                    if(v<=0)throw std::invalid_argument("Maximum edge ratio must be positive");
                    refineSettings.maximumEdgeRatio=v;
                }
                else if(arg=="--curvature-strength"){
                    if(v<0)throw std::invalid_argument("Curvature strength must be nonnegative");
                    curvatureSettings.strength=v;
                    curvatureSettings.enabled=true;
                }
                else strength=v;
            }
            else if(arg=="--max-split-pairs" || arg=="--max-flip-pairs") {
                if(++i>=argc)throw std::invalid_argument("Missing refinement operation limit");
                const std::string_view value=argv[i];
                std::uint64_t count=0;
                const auto [p,ec]=std::from_chars(value.data(),value.data()+value.size(),count);
                if(ec!=std::errc{} || p!=value.data()+value.size() || count>1000)
                    throw std::invalid_argument("Invalid refinement operation limit");
                if(arg=="--max-split-pairs")
                    refineSettings.maximumSplitPairsPerStamp=static_cast<std::uint32_t>(count);
                else
                    refineSettings.maximumFlipPairsPerStamp=static_cast<std::uint32_t>(count);
            }
            else if(arg=="--max-collapse-pairs") {
                if(++i>=argc) throw std::invalid_argument("Missing --max-collapse-pairs value");
                const std::string_view value=argv[i];
                std::uint64_t n=0;
                auto [p,ec]=std::from_chars(value.data(),value.data()+value.size(),n);
                if(ec!=std::errc{}||p!=value.data()+value.size()||n>10000)
                    throw std::invalid_argument("Invalid max-collapse-pairs");
                collapseSettings.maximumPairsPerStamp=static_cast<std::uint32_t>(n);
            }
            else if(arg=="--segments") {
                if(++i>=argc) throw std::invalid_argument("Missing --segments value");
                const std::string_view value=argv[i];
                std::uint64_t n=0;
                auto [p,ec]=std::from_chars(value.data(),value.data()+value.size(),n);
                if(ec!=std::errc{} || p!=value.data()+value.size() || n==0 || n>100000)
                    throw std::invalid_argument("Segments must be between 1 and 100000");
                segments=static_cast<std::uint32_t>(n);
            }
            else if(arg=="--symmetric")options.symmetric=true;
            else if(arg=="--vertices"||arg=="--seed"||arg=="--relax") {
                if(++i>=argc)throw std::invalid_argument("Missing value after "+std::string(arg));
                const std::string_view value=argv[i];
                std::uint64_t number=0;
                auto [p,ec]=std::from_chars(value.data(),value.data()+value.size(),number);
                if(ec!=std::errc{}||p!=value.data()+value.size())throw std::invalid_argument("Invalid integer: "+std::string(value));
                if(arg=="--vertices") {
                    if(number>1000000)throw std::invalid_argument("Too many vertices");
                    options.vertexCount=static_cast<std::uint32_t>(number);
                } else if(arg=="--seed")options.seed=number;
                else {
                    if(number>10000)throw std::invalid_argument("Too many relaxation iterations");
                    options.relaxationIterations=static_cast<std::uint32_t>(number);
                }
            } else if(arg=="--help"||arg=="-h"){
                std::cout<<"Mesher [--symmetric|--asymmetric] [--vertices N] [--seed N] [--relax N]\n"
                         <<"       [--no-inflate] [--step D] [--strength D] [--segments N] [--brush-radius R]\n"
                         <<"       [--stamps-per-second HZ] [--collapse] [--compare-remesh]\n"
                         <<"       [--minimum-edge-ratio R] [--max-collapse-pairs N]\n"
                         <<"       [--curvature-correction] [--curvature-strength GAIN]\n"
                         <<"       [--compare-mitigations] [--compare-local-remesh]\n"
                         <<"       [--refine] [--no-edge-flips] [--maximum-edge-ratio R]\n"
                         <<"       [--max-split-pairs N] [--max-flip-pairs N]\\n"
                         <<"       [--demo] [--time-speed MODEL_UNITS_PER_SECOND]\\n"
                         <<"Defaults: --symmetric --vertices 5000 --seed 1 --relax 65\n"
                         <<"          Mouse Inflate by default; --demo replays 128 segments at 10 stamps/sec.\n"
                         <<"Escape exits. Space pauses, Right steps (when paused), R replays.\n";
                return 0;
            } else throw std::invalid_argument("Unknown argument: "+std::string(arg));
        }
        std::cout<<"Build: "<<MESHER_BUILD_CONFIGURATION
                 <<"; hull backend: "<<mesher::sphereHullBackend()
                 <<"; OpenMP max relaxation threads: "<<mesher::sphereRelaxationThreads()
                 <<"\n";
        std::cout<<"Generating "<<options.vertexCount
                 <<(options.symmetric?" symmetric":" asymmetric")
                 <<" vertices..."<<std::flush;
        mesher::SphereGenerationTimings generation{};
        auto mesh=mesher::generateSphere(options,&generation);
        std::cout<<" "<<mesh.triangles.size()
                 <<" triangles; watertight manifold verified.\n"
                 <<std::fixed<<std::setprecision(3)
                 <<"Sphere timing: relaxation "<<generation.relaxationSeconds
                 <<" s; hull "<<generation.triangulationSeconds
                 <<" s; validation "<<generation.validationSeconds
                 <<" s; total "<<generation.totalSeconds<<" s\n";
        if(compareLocalRemesh) {
            struct Variant {const char* name;bool refine;bool flips;bool collapse;};
            constexpr std::array<Variant,5> modes{{
                {"baseline",false,false,false},
                {"collapse",false,false,true},
                {"split",true,false,false},
                {"split_flip",true,true,false},
                {"all",true,true,true}
            }};
            std::array<mesher::Mesh,5> meshes{mesh,mesh,mesh,mesh,mesh};
            std::array<std::unique_ptr<mesher::InflateDemoAnimation>,5> runs;
            for(std::size_t i=0;i<modes.size();++i) {
                auto collapse=collapseSettings;
                collapse.enabled=modes[i].collapse;
                auto refine=refineSettings;
                refine.enabled=modes[i].refine;
                refine.optimizeFlips=modes[i].flips;
                runs[i]=std::make_unique<mesher::InflateDemoAnimation>(
                    meshes[i],step,strength,segments,brushRadius,
                    collapse,options.symmetric,curvatureSettings,refine);
            }
            std::cout<<std::setprecision(9)
                     <<"mode,stamp,vertices,faces,min_edge,min_altitude,inverted,"
                       "split_pairs,flip_pairs,added_vertices,collapsed_vertices\n";
            auto dump=[&](std::size_t i) {
                const auto& anim=*runs[i];
                const auto& d=anim.diagnostics();
                std::cout<<modes[i].name<<","<<anim.completed()<<","
                         <<meshes[i].vertices.size()<<","<<meshes[i].triangles.size()
                         <<","<<d.minimumEdgeLength<<","<<d.minimumTriangleAltitude
                         <<","<<d.invertedFaces<<","<<anim.lastRefine().splitPairs
                         <<","<<anim.lastRefine().flipPairs<<","
                         <<anim.lastRefine().newVertices<<","
                         <<anim.lastCollapse().removedVertices<<"\n";
            };
            for(std::size_t i=0;i<modes.size();++i)dump(i);
            for(std::uint32_t stamp=0;stamp<runs[0]->total();++stamp)
                for(std::size_t i=0;i<modes.size();++i) {
                    if(!runs[i]->advance())throw std::logic_error("Inconsistent stamp count");
                    dump(i);
                }
            return 0;
        }
        if(compareRemesh || compareMitigations) {
            struct Variant {const char* name;bool collapse;bool curvature;};
            constexpr std::array<Variant,4> variants{{
                {"baseline",false,false},
                {"curvature",false,true},
                {"collapse",true,false},
                {"both",true,true}
            }};
            std::array<mesher::Mesh,4> meshes{mesh,mesh,mesh,mesh};
            std::array<std::unique_ptr<mesher::InflateDemoAnimation>,4> animations;
            for(std::size_t i=0;i<variants.size();++i) {
                if(!compareMitigations && (i==1 || i==3))continue;
                mesher::CollapseSettings cs=collapseSettings;
                cs.enabled=variants[i].collapse;
                mesher::CurvatureSettings cur=curvatureSettings;
                cur.enabled=variants[i].curvature;
                animations[i]=std::make_unique<mesher::InflateDemoAnimation>(
                    meshes[i],step,strength,segments,brushRadius,cs,options.symmetric,cur);
            }
            std::cout << std::setprecision(9);
            std::cout << "mode,stamp,vertices,faces,min_edge,min_altitude,inverted,"
                         "shortening_below_limit,collapsed_pairs,removed_vertices\n";
            auto dump=[&](std::size_t i) {
                const auto& anim=*animations[i];
                const auto& d=anim.diagnostics();
                std::cout<<variants[i].name<<","<<anim.completed()
                         <<","<<meshes[i].vertices.size()
                         <<","<<meshes[i].triangles.size()<<","<<d.minimumEdgeLength
                         <<","<<d.minimumTriangleAltitude<<","<<d.invertedFaces
                         <<","<<anim.preCollapseDiagnostics().shrinkingShortEdges
                         <<","<<anim.lastCollapse().accepted
                         <<","<<anim.lastCollapse().removedVertices<<"\n";
            };
            for(std::size_t i=0;i<variants.size();++i)
                if(animations[i])dump(i);
            while(true) {
                bool advanced=false;
                for(std::size_t i=0;i<variants.size();++i) {
                    if(!animations[i])continue;
                    if(animations[i]->advance()) {
                        advanced=true;
                        dump(i);
                    }
                }
                if(!advanced)break;
            }
            return 0;
        }
        mesher::ViewportAnimation playback;
        std::unique_ptr<mesher::InflateDemoAnimation> demo;
        mesher::ViewportInteraction interaction;
        std::unique_ptr<mesher::AdaptiveInflateBrush> liveBrush;
        std::unique_ptr<mesher::BrushStroke> liveStroke;
        if(inflate && !demoMode) {
            liveBrush=std::make_unique<mesher::AdaptiveInflateBrush>(
                mesh,strength,curvatureSettings,collapseSettings,refineSettings,
                options.symmetric);
            liveStroke=std::make_unique<mesher::BrushStroke>(mesh,*liveBrush,step);
            auto areasFor=[&](mesher::Vec3 p) {
                std::vector<mesher::BrushArea> areas{{p,brushRadius,.3}};
                if(options.symmetric)
                    areas.push_back({{-p.x,p.y,p.z},brushRadius,.3});
                return areas;
            };
            interaction.begin=[&,areasFor](mesher::Vec3 point) {
                liveStroke->startTimed(areasFor(point),timeSpeed);
            };
            interaction.update=[&](mesher::Vec3 point,double elapsed) {
                if(options.symmetric) {
                    const std::array<mesher::Vec3,2> pair{
                        point,{-point.x,point.y,point.z}};
                    liveStroke->updateTimedRealtime(pair,elapsed);
                } else {
                    const std::array<mesher::Vec3,1> pointOnly{point};
                    liveStroke->updateTimedRealtime(pointOnly,elapsed);
                }
            };
            interaction.end=[&](mesher::Vec3 point,double elapsed) {
                if(options.symmetric) {
                    const std::array<mesher::Vec3,2> pair{
                        point,{-point.x,point.y,point.z}};
                    liveStroke->finishTimedRealtime(pair,elapsed);
                } else {
                    const std::array<mesher::Vec3,1> pointOnly{point};
                    liveStroke->finishTimedRealtime(pointOnly,elapsed);
                }
            };
            interaction.completedStamps=[&]{return liveBrush->completedStamps();};
            interaction.stationaryStampsPerSecond=timeSpeed/step;
            interaction.caption=[&] {
                return "Mouse Inflate | vertices "+std::to_string(mesh.vertices.size())+
                       " | stamps "+std::to_string(liveBrush->completedStamps())+
                       (options.symmetric?" | mirrored":" | asymmetric");
            };
            std::cout<<"Mouse Inflate: left-drag or hold to sculpt; time speed "
                     <<timeSpeed<<" units/sec; step "<<step
                     <<(options.symmetric?"; mirrored.\n":"; single brush.\n");
        }
        if (inflate && demoMode) {
            demo = std::make_unique<mesher::InflateDemoAnimation>(
                mesh, step, strength, segments, brushRadius,
                collapseSettings, options.symmetric, curvatureSettings, refineSettings);
            playback.stampsPerSecond=stampRate;
            playback.advance=[&] { return demo->advance(); };
            playback.reset=[&] { demo->reset(); };
            playback.caption=[&] {

                const auto& d=demo->diagnostics();
                return "Inflate " + std::to_string(demo->completed()) +
                       "/" + std::to_string(demo->total()) +
                       "  verts " + std::to_string(mesh.vertices.size()) +
                       "  short/shrinking " +
                       std::to_string(demo->preCollapseDiagnostics().shrinkingShortEdges) +
                       "  removed " + std::to_string(demo->totalRemoved()) +
                       "  added " + std::to_string(demo->totalAdded()) +
                       "  flips " + std::to_string(demo->totalFlips()) +
                       "  min-edge " + std::to_string(d.minimumEdgeLength) +
                       "  min-alt " + std::to_string(d.minimumTriangleAltitude);
            };
            std::cout<<"Curvature correction: "
                     <<(curvatureSettings.enabled?"on":"off")
                     <<" (gain "<<curvatureSettings.strength<<").\n";
            std::cout<<"Refinement: "<<(refineSettings.enabled?"on":"off")
                     <<" (maximum edge "<<demo->maximumEdgeTarget()
                     <<", flips "<<(refineSettings.optimizeFlips?"on":"off")<<").\n";
            std::cout<<"Remeshing: "<<(collapseSettings.enabled?"directional collapsing":"off")
                     <<" (minimum edge "<<demo->minimumEdgeTarget()<<").\n";
            std::cout<<"Animating "<<demo->total()<<" stamps at "<<stampRate
                     <<" stamps/sec; radius "<<brushRadius<<", hardness 0.3.\n";
        }
        mesher::runViewport(mesh, std::move(playback),std::move(interaction));
    }catch(const std::exception& e) {
        std::cerr<<"Mesher: "<<e.what()<<'\n';
        return 1;
    }
}
