#include "mesher/spatial_grid.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace mesher;
void check(bool yes,const char* msg){if(!yes)throw std::runtime_error(msg);}
int main() {
    try {
        std::vector<Vec3> vertices{{0,0,0},{.1,0,0},{.5,0,0},{-.2,0,0},{0,0,.2}};
        VertexSpatialGrid index(.15);
        index.rebuild(vertices);
        std::vector<std::uint32_t> candidates;
        index.querySphere(vertices,{0,0,0},.21,candidates);
        std::sort(candidates.begin(),candidates.end());
        check((candidates==std::vector<std::uint32_t>{0,1,3,4}),
              "Sphere query must include nearby points only");
        vertices[1]={.8,0,0};
        vertices[2]={.02,0,0};
        index.update(vertices,{1,2});
        candidates.clear();
        index.querySphere(vertices,{0,0,0},.21,candidates);
        std::sort(candidates.begin(),candidates.end());
        check((candidates==std::vector<std::uint32_t>{0,2,3,4}),
              "Moved vertices must update the spatial index");
        vertices.push_back({0,0,.01});
        index.rebuild(vertices);
        candidates.clear();
        index.querySphere(vertices,{0,0,0},.21,candidates);
        check(candidates.size()==5,"Topology rebuild must include inserted vertex");
        // Exact comparison agrees with brute force for a larger deterministic
        // distribution across positive and negative spatial hash coordinates.
        vertices.clear();
        for(int i=0;i<5000;++i)
            vertices.push_back({std::sin(i*1.1),std::cos(i*.9),std::sin(i*.3)});
        index.rebuild(vertices);
        for(int q=0;q<20;++q) {
            const Vec3 center{std::sin(q*3.0),std::cos(q*1.2),std::sin(q*.8)};
            candidates.clear();
            index.querySphere(vertices,center,.35,candidates);
            std::sort(candidates.begin(),candidates.end());
            std::vector<std::uint32_t> expected;
            for(std::uint32_t i=0;i<vertices.size();++i) {
                const Vec3 delta=vertices[i]-center;
                if(dot(delta,delta)<.35*.35)expected.push_back(i);
            }
            check(candidates==expected,"Spatial query must match brute-force exactly");
        }
        std::cout<<"Spatial index tests passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Spatial index test failed: "<<error.what()<<'\n';return 1;
    }
    return 0;
}
