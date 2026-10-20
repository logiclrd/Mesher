#include "mesher/wire_quads.hpp"
#include "mesher/desktop_merge_fixture.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void check(bool pass,const char* message) {
    if(!pass)throw std::runtime_error(message);
}
bool same(PartitionRenderVertex a,PartitionRenderVertex b) {
    return a.px==b.px&&a.py==b.py&&a.pz==b.pz&&
           a.nx==b.nx&&a.ny==b.ny&&a.nz==b.nz&&
           a.material==b.material;
}
void verify(std::vector<PartitionRenderVertex> lines) {
    check(lines.size()%2==0,"M58 pair count");
    const auto original=lines;
    const auto quad=expandWireQuads(lines);
    check(quad.size()==original.size()*3,
          "M58 each canonical original segment becomes exactly six GPU vertices");
    constexpr std::array<int,6> corners{0,1,2,0,2,3};
    for(std::size_t i=0;i<original.size()/2;++i) {
        const auto a=original[2*i],b=original[2*i+1];
        for(std::size_t j=0;j<6;++j) {
            const auto v=quad[6*i+j];
            check(v.px==a.px&&v.py==a.py&&v.pz==a.pz,
                  "M58 all GPU quad vertices carry true original segment A");
            check(v.nx==b.px&&v.ny==b.py&&v.nz==b.pz,
                  "M58 all GPU quad vertices carry true original segment B");
            check(v.material==float(corners[j]),
                  "M58 two triangles tile exact four corners");
        }
    }
    for(std::size_t i=0;i<lines.size();++i)
        check(same(lines[i],original[i]),"M58 GPU expansion must not modify staged immutable original edges");
}
}
int main() {
    try {
        verify({});
        verify({
            {1,2,3,0,0,0,0},
            {4,5,6,0,0,0,0}
        });
        bool odd=false;
        try { (void)expandWireQuads(std::vector<PartitionRenderVertex>{
              {0,0,0,0,0,0,0}}); }
        catch(const std::invalid_argument&) {odd=true;}
        check(odd,"M58 malformed wire staging fails before GPU submission");

        // Real named asymmetric and reflected first-original COW fixtures:
        // expansion must carry EXACT topological edge endpoints even after
        // duplicate-owner suppression in partition rendering.
        const std::array<Mesh,2> fixtures{
            desktopMergeAsymmetricTetraFixture(),
            desktopMergeReflectedTetraFixture()};
        for(const Mesh& mesh:fixtures) {
            const auto imported=partitionFlatMesh(mesh,256);
            const auto indexed=RegionTopologyIndex::build(imported.revision);
            indexed->revision().forEachPartition([&](
                    PartitionId id,const MeshPartition&) {
                const auto geometry=buildPartitionRenderGeometry(*indexed,id);
                verify(geometry.wire);
            });
        }
        // Separate 34-segment floor-grid uses the same original
        // two-endpoint staging contract as ordinary model lines.
        std::vector<PartitionRenderVertex> grid;
        for(int i=-8;i<=8;++i) {
            grid.push_back({float(i),-8,-3.5f,0,0,0,0});
            grid.push_back({float(i),8,-3.5f,0,0,0,0});
            grid.push_back({-8,float(i),-3.5f,0,0,0,0});
            grid.push_back({8,float(i),-3.5f,0,0,0,0});
        }
        check(grid.size()==68&&expandWireQuads(grid).size()==204,
              "M58 G grid is physically widened consistently with COW edges");
        verify(grid);
        std::cout<<"M58 all canonical wire segments expand to exact AA quad corners\n";
    }catch(const std::exception& e) {
        std::cerr<<"M58 wire quads: "<<e.what()<<'\n';
        return 1;
    }
}
