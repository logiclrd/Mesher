#include "mesher/mesh.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

using namespace mesher;

void check(bool condition, const char* msg) { if(!condition) throw std::runtime_error(msg); }
void test(std::uint32_t n, bool symmetric, std::uint64_t seed) {
    SphereOptions options;
    options.vertexCount=n; options.symmetric=symmetric; options.seed=seed;
    options.relaxationIterations=20;
    const auto mesh=generateSphere(options);
    check(mesh.vertices.size()==n,"Exact vertex count");
    check(validateSphere(mesh),"Manifold");
    check(mesh.triangles.size()==2*n-4,"Sphere triangle count");
    for(auto vertex:mesh.vertices) check(std::abs(length(vertex)-1)<1e-10,"Unit radius");
    if(symmetric){
        const auto spacing=std::sqrt(8*3.14159265358979323846/(double(n)*std::sqrt(3.0)));
        auto m=static_cast<std::uint32_t>(std::floor(2*3.14159265358979323846/spacing))+1;
        if((n-m)%2)++m;
        const auto p=(n-m)/2;
        for(std::uint32_t i=0;i<p;++i) {
            auto a=mesh.vertices[i],b=mesh.vertices[i+p+m];
            check(std::abs(a.x+b.x)<1e-12 && std::abs(a.y-b.y)<1e-12 && std::abs(a.z-b.z)<1e-12,"Mirrored vertex");
        }
        for(auto i=p;i<p+m;++i)check(std::abs(mesh.vertices[i].x)<1e-12,"Fixed seam ring");
    }
    std::cout<<(symmetric?"sym":"asym")<<" n="<<n<<" faces="<<mesh.triangles.size()<<" OK\n";
}
int main(){
    try {
        for(auto n:{8u,10u,11u,12u,30u,100u,500u,5000u})
            for(bool symmetric:{false,true})test(n,symmetric,42);
        for(auto seed:{1ull,2ull,3ull,4ull}) {test(1000,true,seed);test(1000,false,seed);}
        SphereOptions reproducible;
        reproducible.vertexCount=500;
        const auto one=generateSphere(reproducible),two=generateSphere(reproducible);
        check(one.triangles.size()==two.triangles.size(),"Reproducible face count");
        for(std::size_t i=0;i<one.vertices.size();++i)
            check(length(one.vertices[i]-two.vertices[i])==0.0,"Deterministic vertices");
    }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}
