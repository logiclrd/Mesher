#include "mesher/repair_mirror_geometry.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

using namespace mesher;
namespace {
void require(bool value,const char* reason) {
    if(!value)throw std::runtime_error(reason);
}
Mesh octahedron() {
    Mesh mesh;
    mesh.vertices={
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
    };
    for(const auto t:std::array<std::array<std::uint32_t,3>,8>{{
        {4,0,2},{4,2,1},{4,1,3},{4,3,0},
        {5,2,0},{5,1,2},{5,3,1},{5,0,3}
    }})mesh.triangles.push_back({t[0],t[1],t[2]});
    return mesh;
}
void inspect(Mesh mesh,bool shouldPass) {
    const auto imported=partitionFlatMesh(mesh,64);
    const auto index=RegionTopologyIndex::build(imported.revision);
    bool passed=false;
    try{
        const auto result=auditPrivateReflectedGeometry(*index);
        require(result.vertices==6&&result.faces==8&&
                result.seamVertices==4&&result.pairedFaces==8,
                "R06-M06C4c genuine symmetric octahedron must have paired faces and seam vertices");
        passed=true;
    }catch(const std::logic_error&) {}
    require(passed==shouldPass,
            "R06-M06C4c reflected-geometry audit accepted a forged orbit or refused a genuine one");
}
}
int main() {
    try {
        auto m=octahedron();
        inspect(m,true);
        m.vertices[0].x=1.01;
        inspect(m,false);
        m=octahedron();
        std::swap(m.triangles[0].b,m.triangles[0].c);
        inspect(m,false);
        m=octahedron();
        // A valid triangle winding alone cannot authorize a fabricated
        // cross-plane shortcut that omits the actual X=0 seam.
        m.triangles[0]={4,0,1};
        inspect(m,false);
        std::cout<<"[r06-m06c4c] exact reflection geometry, reciprocal winding, and tamper refusal PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"R06-M06C4c mirror audit FAILED: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
