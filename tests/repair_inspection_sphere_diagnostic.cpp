#include "mesher/mesh.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/repair_inspection.hpp"

#include <array>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void report(const RegionTopologyIndex& index,FaceId first,FaceId second) {
    const auto show=[&](FaceId id) {
        const auto face=index.maybeFace(id);
        if(!face) {std::cout<<"  deleted FaceId "<<id<<"\n";return;}
        std::cout<<"  face "<<id;
        for(auto v:face->vertices) {
            const auto p=index.vertex(v).position;
            std::cout<<" ("<<v<<":"<<p.x<<","<<p.y<<","<<p.z<<")";
        }
        std::cout<<"\n";
    };
    show(first);show(second);
}
void inspectSphere() {
    SphereOptions opts;
    auto mesh=generateSphere(opts);
    VersionedSculptBrush brush(mesh,.012,{},{},{},true);
    const auto original=brush.snapshot();
    auto allFaces=brush.originalOrder()->faceIds;
    auto initial=original->triangleIntersections({},allFaces);
    std::cout<<std::setprecision(17);
    std::cout<<"[r01-sphere] original vertices="<<mesh.vertices.size()
             <<" triangles="<<mesh.triangles.size()
             <<" initial reported triangle pairs="<<initial.size()<<"\n";
    for(std::size_t i=0;i<std::min(initial.size(),std::size_t{4});++i) {
        const auto pair=initial[i];
        std::cout<<"[r01-sphere] baseline "<<pair.first<<"/"<<pair.second
                 <<" ambiguous="<<pair.ambiguous<<"\n";
        report(*original,pair.first,pair.second);
    }
    auto session=std::make_shared<RepairInspectionSession>();
    VersionedSculptBrush guarded(mesh,.012,{},{},{},true,256,
        [session](const ProposedSculptStep& p){return session->inspect(p);});
    const Vec3 center=normalized(Vec3{.55,-.75,-.36});
    const std::array<BrushArea,2> areas{{
        {center,.4,.02},{{-center.x,center.y,center.z},.4,.02}
    }};
    Mesh dummy;
    for(int i=0;i<20;++i) {
        const auto previous=guarded.snapshot();
        guarded.stamp(dummy,areas);
        if(auto frozen=session->frozenSnapshot()) {
            std::cout<<"[r01-sphere] freeze at stamp="<<i+1
                     <<" contact_count="<<frozen->contacts.size()
                     <<" diagnostic="<<frozen->diagnostic<<"\n";
            for(std::size_t j=0;j<std::min(frozen->contacts.size(),std::size_t{8});++j) {
                const auto pair=frozen->contacts[j];
                std::cout<<"[r01-sphere] contact "<<pair.first<<"/"<<pair.second
                         <<" ambiguous="<<pair.ambiguous
                         <<" old_first="<<bool(previous->maybeFace(pair.first))
                         <<" old_second="<<bool(previous->maybeFace(pair.second))<<"\n";
                report(*frozen->candidate,pair.first,pair.second);
                if(previous->maybeFace(pair.first)&&previous->maybeFace(pair.second)) {
                    report(*previous,pair.first,pair.second);
                }
            }
            break;
        }
    }
}
}
int main() {
    try {inspectSphere();}
    catch(const std::exception& e) {
        std::cerr<<"R01 sphere diagnostic: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
