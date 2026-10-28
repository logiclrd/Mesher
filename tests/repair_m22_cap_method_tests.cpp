#include "mesher/repair_mixed_closure.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace mesher;
namespace {
void require(bool yes,const char* message) {
    if(!yes)throw std::runtime_error(message);
}
struct Source {
    PrivateExcisionResult cut;
    RepairExcisionPreview proposed;
    AutoClosurePlan plan;
};
Source openTetrahedron() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    mesh.triangles={{0,1,3},{0,3,2},{1,2,3}};
    const auto flat=partitionFlatMesh(mesh,64);
    Source out;
    out.cut.index=RegionTopologyIndex::build(flat.revision);
    out.cut.boundaryLoops=1;
    out.cut.boundaryEdges=3;
    out.proposed.complete=true;
    out.proposed.boundaryEdges=3;
    RepairContour rim;
    rim.vertexIds={flat.flatIndex.vertexIds[0],
                   flat.flatIndex.vertexIds[1],
                   flat.flatIndex.vertexIds[2]};
    for(std::size_t i=0;i<rim.vertexIds.size();++i) {
        auto a=rim.vertexIds[i],b=rim.vertexIds[(i+1)%rim.vertexIds.size()];
        if(a>b)std::swap(a,b);
        const auto sides=out.cut.index->maybeEdgeSides({a,b});
        require(sides&&sides->size()==1,
                "M22B tetra source must have one original rim side");
        rim.survivingFaces.push_back(sides->front().face);
    }
    out.proposed.contours.push_back(rim);
    out.plan.status=AutoClosurePlanStatus::Ready;
    out.plan.cappedContours={0};
    AutoClosureRegion region;
    region.contours={0};
    region.caps={0};
    out.plan.regions={region};
    out.plan.geometryRegions=1;
    return out;
}
struct Capture {
    std::ostringstream stream;
    std::streambuf* previous=std::cout.rdbuf(stream.rdbuf());
    ~Capture(){std::cout.rdbuf(previous);}
};
void exercise() {
    const auto source=openTetrahedron();
    MixedPrivateClosureLimits afm;
    {
        Capture log;
        auto closed=applyPrivateMixedClosure(
            source.cut,source.proposed,source.plan,afm);
        require(closed.index&&closed.cappedLoops==1&&
                closed.spatialCapLoops==1,
                "M22B default R06 cap must use spatial AFM");
        const auto output=log.stream.str();
        require(output.find("method=advancing-front")!=std::string::npos&&
                output.find("bottom_decile_faces=")!=std::string::npos&&
                output.find("top_decile_faces=")!=std::string::npos&&
                output.find("q_p10=")!=std::string::npos&&
                output.find("SOURCE_CLOSURE_ACCEPTED")!=std::string::npos&&
                output.find("method=projected-ear")==std::string::npos,
                "M22B default should log AFM metrics and not attempt ears");
    }
    {
        Capture log;
        afm.staging.tryProjectedEarClipping=true;
        auto closed=applyPrivateMixedClosure(
            source.cut,source.proposed,source.plan,afm);
        require(closed.index&&closed.cappedLoops==1&&
                closed.spatialCapLoops==0,
                "M22B opt-in R06 cap should choose projected ears first");
        const auto output=log.stream.str();
        require(output.find("method=projected-ear")!=std::string::npos&&
                output.find("status=CANDIDATE_READY")!=std::string::npos&&
                output.find("edges=3")!=std::string::npos&&
                output.find("SOURCE_CLOSURE_ACCEPTED")!=std::string::npos,
                "M22B ear opt-in should log complete diagnostic candidate");
    }
    require(source.cut.index->retiredFaceCount()==0,
            "M22B diagnostics and two generators must keep source immutable");
    std::cout<<"[repair-r06-m22b] default AFM, opt-in ears, quality metrics PASS\n";
}
}
int main() {
    try{exercise();}
    catch(const std::exception& error) {
        std::cerr<<"M22B: "<<error.what()<<"\n";
        return 1;
    }
}
