#include "mesher/brush.hpp"
#include "mesher/mesh.hpp"
#include "mesher/viewport.hpp"

#include <charconv>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <string>
#include <string_view>
#include <stdexcept>

int main(int argc, char** argv) {
    mesher::SphereOptions options;
    bool inflate = true;
    double step = .02, strength = .012;
    std::uint32_t segments = 128;
    try {
        for(int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            if(arg=="--asymmetric") options.symmetric=false;
            else if(arg=="--no-inflate") inflate=false;
            else if(arg=="--step" || arg=="--strength") {
                if(++i>=argc) throw std::invalid_argument("Missing value after "+std::string(arg));
                const std::string value=argv[i];
                std::size_t parsed=0;
                const double v=std::stod(value,&parsed);
                if(parsed!=value.size() || !std::isfinite(v) ||
                   (arg=="--step" && v<=0))
                    throw std::invalid_argument("Invalid brush parameter: "+value);
                if(arg=="--step")step=v;else strength=v;
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
                         <<"       [--no-inflate] [--step D] [--strength D] [--segments N]\n"
                         <<"Defaults: --symmetric --vertices 5000 --seed 1 --relax 65\n"
                         <<"          Inflate demo on, Step .02, strength .012, 128 segments.\n"
                         <<"Escape exits the full-screen viewport.\n";
                return 0;
            } else throw std::invalid_argument("Unknown argument: "+std::string(arg));
        }
        std::cout<<"Generating "<<options.vertexCount<<(options.symmetric?" symmetric":" asymmetric")<<" vertices..."<<std::flush;
        auto mesh=mesher::generateSphere(options);
        std::cout<<" "<<mesh.triangles.size()<<" triangles; watertight manifold verified.\n";
        if (inflate) {
            const auto count=mesher::inflateHalfCircleDemo(mesh,step,strength,segments);
            std::cout<<"Inflate demo: "<<count<<" stamps, radius 0.4, hardness 0.3.\n";
        }
        mesher::runViewport(mesh);
    }catch(const std::exception& e) {
        std::cerr<<"Mesher: "<<e.what()<<'\n';
        return 1;
    }
}
