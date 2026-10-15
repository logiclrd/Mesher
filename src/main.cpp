#include "mesher/brush.hpp"
#include "mesher/mesh.hpp"
#include "mesher/viewport.hpp"

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
    bool inflate = true;
    double step = .02, strength = .012, brushRadius = .4, stampRate = 10.0;
    std::uint32_t segments = 128;
    mesher::CollapseSettings collapseSettings;
    bool compareRemesh = false;
    try {
        for(int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            if(arg=="--asymmetric") options.symmetric=false;
            else if(arg=="--collapse") collapseSettings.enabled=true;
            else if(arg=="--compare-remesh") compareRemesh=true;
            else if(arg=="--no-inflate") inflate=false;
            else if(arg=="--step" || arg=="--strength" || arg=="--brush-radius" || arg=="--stamps-per-second" || arg=="--minimum-edge-ratio") {
                if(++i>=argc) throw std::invalid_argument("Missing value after "+std::string(arg));
                const std::string value=argv[i];
                std::size_t parsed=0;
                const double v=std::stod(value,&parsed);
                if(parsed!=value.size() || !std::isfinite(v) ||
                   ((arg=="--step" || arg=="--brush-radius" || arg=="--stamps-per-second" || arg=="--minimum-edge-ratio") && v<=0))
                    throw std::invalid_argument("Invalid brush parameter: "+value);
                if(arg=="--step")step=v;
                else if(arg=="--brush-radius")brushRadius=v;
                else if(arg=="--stamps-per-second")stampRate=v;
                else if(arg=="--minimum-edge-ratio")collapseSettings.minimumEdgeRatio=v;
                else strength=v;
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
                         <<"Defaults: --symmetric --vertices 5000 --seed 1 --relax 65\n"
                         <<"          Inflate animation on, radius .4, Step .02, strength .012, 128 segments at 10 stamps/sec.\n"
                         <<"Escape exits. Space pauses, Right steps (when paused), R replays.\n";
                return 0;
            } else throw std::invalid_argument("Unknown argument: "+std::string(arg));
        }
        std::cout<<"Generating "<<options.vertexCount<<(options.symmetric?" symmetric":" asymmetric")<<" vertices..."<<std::flush;
        auto mesh=mesher::generateSphere(options);
        std::cout<<" "<<mesh.triangles.size()<<" triangles; watertight manifold verified.\n";
        if(compareRemesh) {
            auto baselineMesh=mesh;
            auto collapseMesh=mesh;
            mesher::CollapseSettings enabled=collapseSettings;
            enabled.enabled=true;
            mesher::InflateDemoAnimation baseline(
                baselineMesh,step,strength,segments,brushRadius,{},options.symmetric);
            mesher::InflateDemoAnimation collapsing(
                collapseMesh,step,strength,segments,brushRadius,enabled,options.symmetric);
            std::cout << std::setprecision(9);
            std::cout << "mode,stamp,vertices,faces,min_edge,min_altitude,inverted,"
                         "shortening_below_limit,collapsed_pairs,removed_vertices\n";
            auto dump=[&](const char* name,const mesher::InflateDemoAnimation& anim,
                          const mesher::Mesh& current) {
                const auto& d=anim.diagnostics();
                std::cout<<name<<","<<anim.completed()<<","<<current.vertices.size()
                         <<","<<current.triangles.size()<<","<<d.minimumEdgeLength
                         <<","<<d.minimumTriangleAltitude<<","<<d.invertedFaces
                         <<","<<anim.preCollapseDiagnostics().shrinkingShortEdges
                         <<","<<anim.lastCollapse().accepted
                         <<","<<anim.lastCollapse().removedVertices<<"\n";
            };
            dump("baseline",baseline,baselineMesh);
            dump("collapse",collapsing,collapseMesh);
            while(baseline.advance() && collapsing.advance()) {
                dump("baseline",baseline,baselineMesh);
                dump("collapse",collapsing,collapseMesh);
            }
            return 0;
        }
        mesher::ViewportAnimation playback;
        std::unique_ptr<mesher::InflateDemoAnimation> demo;
        if (inflate) {
            demo = std::make_unique<mesher::InflateDemoAnimation>(
                mesh, step, strength, segments, brushRadius,
                collapseSettings, options.symmetric);
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
                       "  min-edge " + std::to_string(d.minimumEdgeLength) +
                       "  min-alt " + std::to_string(d.minimumTriangleAltitude);
            };
            std::cout<<"Remeshing: "<<(collapseSettings.enabled?"directional collapsing":"off")
                     <<" (minimum edge "<<demo->minimumEdgeTarget()<<").\n";
            std::cout<<"Animating "<<demo->total()<<" stamps at "<<stampRate
                     <<" stamps/sec; radius "<<brushRadius<<", hardness 0.3.\n";
        }
        mesher::runViewport(mesh, std::move(playback));
    }catch(const std::exception& e) {
        std::cerr<<"Mesher: "<<e.what()<<'\n';
        return 1;
    }
}
