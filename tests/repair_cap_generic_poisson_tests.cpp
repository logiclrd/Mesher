#include "mesher/repair_cap_spatial.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;

namespace {
void require(bool yes,const char* explanation) {
    if(!yes)throw std::runtime_error(explanation);
}

bool same(const std::vector<Vec3>& a,const std::vector<Vec3>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
        if(a[i].x!=b[i].x||a[i].y!=b[i].y||a[i].z!=b[i].z)
            return false;
    return true;
}

void arbitraryTwoInteriorPatch() {
    // Seven triangles triangulate a non-ring pentagonal disk with two
    // independently positioned interior points; no hub or inset-ring IDs.
    SpatialCapPatch patch;
    patch.boundaryCount=5;
    patch.positions={{0,0,0},{1,0,0},{2,.2,0},{1,1,0},{0,1,0},
                     {.6,.3,.4},{1.3,.5,-.3}};
    patch.faces={{0,1,5},{1,2,6},{1,6,5},{2,3,6},
                 {3,4,5},{3,5,6},{4,0,5}};
    const auto before=patch.positions;
    solveSpatialCapFixedRimPoisson(patch,{{0,0,1},{0,0,1}},
                                   SpatialCapSettings{});
    require(patch.poissonIterations>0&&
            patch.poissonIterations<=SpatialCapSettings{}.maxPoissonIterations,
            "M22A general Poisson must converge on a nonring mesh");
    require(patch.positions.size()==7&&patch.faces.size()==7,
            "M22A general Poisson must preserve arbitrary indexed connectivity");
    for(std::size_t i=0;i<patch.boundaryCount;++i)
        require(patch.positions[i].x==before[i].x&&
                patch.positions[i].y==before[i].y&&
                patch.positions[i].z==before[i].z,
                "M22A general Poisson changed source R03 boundary");
    require(patch.positions[5].z>0&&patch.positions[6].z>0,
            "M22A general Poisson should follow per-interior normal guidance");
    std::cout<<"[r06-m22a] two-interior non-ring patch PASS\n";
}

void exactMirroredNonRingPatch() {
    SpatialCapPatch patch;
    patch.boundaryCount=6;
    patch.positions={{0,1,0},{.8,.7,0},{.8,-.7,0},
                     {0,-1,0},{-.8,-.7,0},{-.8,.7,0},
                     {0,0,.4}};
    patch.faces={{0,1,6},{1,2,6},{2,3,6},
                 {3,4,6},{4,5,6},{5,0,6}};
    const auto before=patch.positions;
    SpatialCapSettings symmetric;
    symmetric.requireMirrorPlane=true;
    const std::vector<std::size_t> twins{0,5,4,3,2,1,6};
    solveSpatialCapFixedRimPoisson(patch,{{0,0,1}},symmetric,twins);
    require(patch.poissonIterations>0&&patch.positions[6].x==0,
            "M22A exact mirrored Poisson must retain self-mirrored seam interior");
    for(std::size_t i=0;i<patch.boundaryCount;++i)
        require(patch.positions[i].x==before[i].x&&
                patch.positions[i].y==before[i].y&&
                patch.positions[i].z==before[i].z,
                "M22A mirrored Poisson changed original rim");
    auto mismatched=symmetric;
    const auto copy=patch.positions;
    bool refused=false;
    try {
        solveSpatialCapFixedRimPoisson(patch,{{.1,0,1}},mismatched,twins);
    }catch(const std::logic_error&) {refused=true;}
    require(refused&&same(patch.positions,copy),
            "M22A self-mirrored guidance must be symmetric or refuse atomically");
    std::cout<<"[r06-m22a] exact reflected non-ring patch and refusal PASS\n";
}

void finiteAndProvenanceRejections() {
    SpatialCapPatch patch;
    patch.boundaryCount=4;
    patch.positions={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},{0,0,2}};
    patch.faces={{0,1,4},{1,2,4},{2,3,4},{3,0,4}};
    const auto original=patch.positions;
    bool refused=false;
    try {
        solveSpatialCapFixedRimPoisson(patch,{},SpatialCapSettings{});
    }catch(const std::invalid_argument&){refused=true;}
    require(refused&&same(patch.positions,original),
            "M22A missing guidance must fail without touching patch");
    refused=false;
    try {
        auto bounds=SpatialCapSettings{};
        bounds.maxPoissonIterations=1;
        solveSpatialCapFixedRimPoisson(patch,{{0,0,0}},bounds);
    }catch(const std::logic_error&){refused=true;}
    require(refused&&same(patch.positions,original)&&
            patch.poissonIterations==0,
            "M22A incomplete convergence must preserve input and metadata");
    refused=false;
    try {
        auto bounds=SpatialCapSettings{};
        bounds.maxInteriorVertices=0;
        solveSpatialCapFixedRimPoisson(patch,{{0,0,1}},bounds);
    }catch(const std::invalid_argument&){refused=true;}
    require(refused&&same(patch.positions,original),
            "M22A vertex budget must refuse early and atomically");
    std::cout<<"[r06-m22a] guidance, convergence and budget refusals PASS\n";
}
}

int main() {
    try {
        arbitraryTwoInteriorPatch();
        exactMirroredNonRingPatch();
        finiteAndProvenanceRejections();
    }catch(const std::exception& error) {
        std::cerr<<"R06-M22A: "<<error.what()<<"\n";
        return 1;
    }
}
