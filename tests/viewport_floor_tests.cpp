#include "mesher/desktop_merge_fixture.hpp"
#include "mesher/viewport_floor.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace mesher;
namespace {
void require(bool pass,const char* what) {
    if(!pass)throw std::runtime_error(what);
}
double lowest(const Mesh& mesh) {
    double result=std::numeric_limits<double>::infinity();
    for(const Vec3 p:mesh.vertices)result=std::min(result,p.z);
    return result;
}
}
int main() {
    try {
        const Mesh empty;
        require(viewportWorkFloorZ(empty)==-1.22,
                "M57 empty background remains legacy Z=-1.22");
        Mesh unit;
        unit.vertices={{0,0,-1},{1,0,0},{0,1,1}};
        require(viewportWorkFloorZ(unit)==-1.22,
                "M57 legacy sphere-bounded geometry retains exactly prior floor height");
        const Mesh asym=desktopMergeAsymmetricTetraFixture();
        const Mesh refl=desktopMergeReflectedTetraFixture();
        require(lowest(asym)==-3&&lowest(refl)==-3,
                "M57 M50/M51 real source fixture contains triangles below legacy floor");
        const double asymmetricFloor=viewportWorkFloorZ(asym);
        const double reflectedFloor=viewportWorkFloorZ(refl);
        require(asymmetricFloor==-3.5&&reflectedFloor==-3.5,
                "M57 startup floor must be below the full asymmetric/reflected tetra source");
        require(asymmetricFloor<lowest(asym)&&reflectedFloor<lowest(refl),
                "M57 depth-writing scenic floor never intersects a supported startup fixture");
        require(asym.triangles.size()==8&&refl.triangles.size()==16,
                "M57 floor computation preserves every original identity and source face");
        Mesh crossing;
        crossing.vertices={{0,0,-2},{0,0,2}};
        require(viewportWorkFloorZ(crossing)==-2.5,
                "M57 crossing plane dynamically lowers based on bounded startup geometry");
        std::cout<<"M57 floor clearances: unit="<<viewportWorkFloorZ(unit)
                 <<" asymmetric="<<asymmetricFloor
                 <<" reflected="<<reflectedFloor
                 <<" sourceMinimum="<<lowest(refl)<<'\n';
    }catch(const std::exception& e) {
        std::cerr<<"M57 viewport floor: "<<e.what()<<'\n';
        return 1;
    }
    return 0;
}
