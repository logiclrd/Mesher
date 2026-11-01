#include "mesher/wire_quads.hpp"
#include "mesher/wire_appearance.hpp"
#include "mesher/desktop_merge_fixture.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
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

        // M62 shader-matched analytic coverage: smoother and lighter
        // in BOTH symmetric and depth-clipped one-sided visible cases.
        check(kWireOuterHalfWidthPixels==1.5f,
              "M62 physical stroke halfwidth is calibrated to 1.5px");
        check(wireCoverage(0.0f)==1.0f&&
              wireCoverage(kWireOuterHalfWidthPixels)==0.0f,
              "M62 exact center and fringe boundaries");
        check(wireCoverage(1.0f)==0.5f&&
              wireCoverage(-1.0f)==0.5f,
              "M62 symmetric smoothstep halfway alpha coverage");
        float last=1.0f;
        for(int sample=1;sample<=150;++sample) {
            const float d=float(sample)/100.0f;
            const float coverage=wireCoverage(d);
            check(coverage>=0&&coverage<=last&&
                  std::abs(coverage-wireCoverage(-d))<1e-7f,
                  "M62 symmetric monotonic screen-space alpha");
            last=coverage;
        }
        // Numerical integral is equivalent visible opaque line width.
        // M58: 2px halfwidth and linear 1px ramp => full ink 3px;
        // M62: 1.5px halfwidth smoothstep => full ink 2px, each
        // genuinely visible half still carries ~1px of coverage.
        double halfInk=0.0;
        constexpr int samples=15000;
        for(int i=0;i<samples;++i)
            halfInk+=wireCoverage((float(i)+0.5f)*1.5f/samples)*
                     (1.5/double(samples));
        check(std::abs(halfInk-1.0)<0.001,
              "M62 clipped half still carries a full pixel of AA ink");
        check(std::abs(2*halfInk-2.0)<0.002&&2*halfInk<3.0,
              "M62 full AA stroke lighter than 2px-halfwidth M58");
        check(wireCoverage(std::numeric_limits<float>::quiet_NaN())==0&&
              wireCoverage(1.f,0.f)==0,
              "M62 invalid coverage inputs must be transparent");

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
    return 0;
}
