#include "mesher/durable_checkpoint.hpp"
#include "mesher/repair_mirror_geometry.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <array>
#include <cmath>
#include <regex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace mesher;
namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

void recoveredPhysicalZipperMustPublish() {
    // This is the actual 6,686-vertex frozen source from the live 49-refusal
    // episode, not a reconstructed synthetic pair of contours. The paired
    // sidecar contains the physical *next* stamp against that exact root.
    // Preserve the source/archive unchanged: the desired new behavior is
    // a successfully certified, exactly reflected published zipper.
    const auto directory=std::filesystem::path(__FILE__).parent_path()
        .parent_path()/"repair-cases";
    const auto checkpointPath=directory/"refusal-6.mshr";
    const auto stampPath=directory/"refusal-6.nextstamp";
    auto recovered=readCheckpoint(checkpointPath);
    require(recovered.index&&!recovered.mirrorPairs.empty(),
            "Refusal-6 must restore the actual symmetric live source");

    std::ifstream stampText(stampPath);
    require(static_cast<bool>(stampText),
            "The physical refusal-6 stamp sidecar is required");
    std::string line;
    require(static_cast<bool>(std::getline(stampText,line)),
            "The physical refusal-6 stamp sidecar must not be empty");
    // Parse the committed sidecar rather than silently duplicating its
    // numbers in test code. Reject incomplete or malformed transcripts.
    const std::regex format(
        R"(^radius=([-+0-9.eE]+) brush_centers=\\[\\(([-+0-9.eE]+),([-+0-9.eE]+),([-+0-9.eE]+)\\);\\(([-+0-9.eE]+),([-+0-9.eE]+),([-+0-9.eE]+)\\)\\]$)");
    std::smatch matches;
    require(std::regex_match(line,matches,format)&&matches.size()==8,
            "Refusal-6 nextstamp must have the complete mirrored physical stamp");
    const double radius=std::stod(matches[1].str());
    const double x0=std::stod(matches[2].str());
    const double y0=std::stod(matches[3].str());
    const double z0=std::stod(matches[4].str());
    const double x1=std::stod(matches[5].str());
    const double y1=std::stod(matches[6].str());
    const double z1=std::stod(matches[7].str());
    require(std::isfinite(radius)&&radius>0&&
            std::isfinite(x0)&&std::isfinite(y0)&&std::isfinite(z0)&&
            std::isfinite(x1)&&std::isfinite(y1)&&std::isfinite(z1),
            "Refusal-6 nextstamp must contain seven finite parameters");
    require(x1==-x0&&y1==y0&&z1==z0,
            "Refusal-6 stamp must retain exactly paired mirrored brush centers");

    CurvatureSettings curvature;curvature.enabled=true;
    CollapseSettings collapse;collapse.enabled=true;
    RefineSettings refine;refine.enabled=true;
    SculptCollisionSettings policy;
    policy.certifiedAutoClosure=true;
    policy.certifiedAutoUnifiedZipper=true;
    // Matches the desktop Release defaults: strength=0.012 and all three
    // sculpt mitigations enabled. The 0.02 4D input step controls WHEN to
    // stamp, not the magnitude of an individual Inflate stamp.
    VersionedSculptBrush brush(
        std::move(recovered),.012,curvature,collapse,refine,256,{},policy);
    const auto before=brush.snapshot();
    require(before!=nullptr,"The recovered source must have an immutable root");
    Mesh unused;
    const std::array<BrushArea,2> areas{{
        {{x0,y0,z0},radius,.3},{{x1,y1,z1},radius,.3}
    }};
    brush.stamp(unused,areas);
    const auto after=brush.snapshot();
    std::cout<<"[r07-refusal-6] completed="<<brush.completedStamps()
             <<" published="<<brush.publishedRevisions()
             <<" repaired="<<brush.collisionRepairedStamps()
             <<" rejected="<<brush.rejectedStamps()
             <<" reason="<<brush.lastRejection()<<"\n";
    require(brush.completedStamps()==1&&
            brush.collisionRepairedStamps()==1&&
            brush.rejectedStamps()==0&&
            brush.publishedRevisions()==1&&
            after&&after!=before&&brush.lastRejection().empty(),
            "Refusal-6's actual next physical zipper stamp must publish a certified AUTO repair");
    const auto reflected=auditPrivateReflectedGeometry(*after,100000,100000);
    require(reflected.pairedFaces>0&&reflected.pairedVertices>0,
            "Refusal-6's published repair must preserve exact reflected vertex/face orbits");
    require(brush.undo()&&brush.snapshot()==before,
            "The certified recovery must undo to the exact saved source");
    require(brush.redo()&&brush.snapshot()==after,
            "The certified recovery must redo to the exact repaired root");
    std::cout<<"[r07-refusal-6] physical checkpoint+stamp certified, "
                "mirrored, undoable and redoable PASS\n";
}
}

int main() {
    try {recoveredPhysicalZipperMustPublish();}
    catch(const std::exception& e) {
        std::cerr<<"[r07-refusal-6] FAILED: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
