#include "mesher/wire_appearance.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

// V17-M64: A CPU ANALYTIC COUNTEREXAMPLE to the old GPU wire-quad
// approximation. A screen-space quad is offset perpendicular to the
// projected edge, while all its fragments retain the centerline's
// interpolated depth. Its owning filled triangle has a nonzero
// depth slope in that direction except at certain camera poses.
// This is a TEST OF THE CURRENT DEFECT, not a repaired renderer.
namespace {
void require(bool ok,const char* what) {
    if(!ok)throw std::runtime_error(what);
}

enum class DepthModel {LegacyFlatEdge,VisibleOwningSurface};
struct Ink {
    double integrated=0.0;
    double left=0.0;
    double right=0.0;
};
Ink integrate(float normalDepthSlope,DepthModel mode,
              float occluderDepth=1.0f) {
    constexpr int samples=30000;
    const float half=mesher::kWireOuterHalfWidthPixels;
    Ink out;
    for(int i=0;i<samples;++i) {
        const float x=-half+(2.0f*half)*(float(i)+0.5f)/samples;
        const float ownerDepth=0.5f+normalDepthSlope*x;
        const float lineDepth=0.5f;
        const bool nearestVisible=ownerDepth<=occluderDepth;
        // In the proposed face-attached shading, the fragment is
        // shaded only if its OWN visible triangle wins the depth test.
        // The legacy QUAD instead retests a non-owner depth.
        const bool pass=nearestVisible&&
            (mode==DepthModel::VisibleOwningSurface ||
             lineDepth<=ownerDepth);
        const double ink=(pass?mesher::wireCoverage(x):0.0f)*
                         (2.0*half/double(samples));
        out.integrated+=ink;
        (x<0.0f?out.left:out.right)+=ink;
    }
    return out;
}
void test(const char* label) {
    const auto flat=integrate(0.f,DepthModel::LegacyFlatEdge);
    const auto shallow=integrate(0.000003f,DepthModel::LegacyFlatEdge);
    const auto steep=integrate(0.0003f,DepthModel::LegacyFlatEdge);
    const auto opposite=integrate(-0.0003f,DepthModel::LegacyFlatEdge);
    require(std::abs(flat.integrated-2.0)<0.002,
            "At the pole, both halves pass unchanged LEQUAL");
    require(std::abs(shallow.integrated-1.0)<0.008&&
            std::abs(steep.integrated-1.0)<0.002,
            "Even a tiny nonzero owning-surface depth slope masks half the legacy quad");
    require(std::abs(steep.left)<0.001&&steep.right>0.99,
            "Positive normal slope masks negative edge half");
    require(opposite.left>0.99&&std::abs(opposite.right)<0.001,
            "Reversing surface gradient swaps the masked half");
    const auto corrected=integrate(0.0003f,DepthModel::VisibleOwningSurface);
    const auto correctedOpposite=integrate(-0.0003f,DepthModel::VisibleOwningSurface);
    require(std::abs(corrected.integrated-flat.integrated)<0.002&&
            std::abs(correctedOpposite.integrated-flat.integrated)<0.002,
            "Owning-surface fragments have stable coverage across pitch and slope");
    require(std::abs(corrected.left-corrected.right)<0.002,
            "Surface-attached ink keeps symmetric coverage INSIDE visible faces");
    const auto hidden=integrate(0.0003f,DepthModel::VisibleOwningSurface,
                                0.49f);
    require(hidden.integrated==0.0&&hidden.left==0.0&&hidden.right==0.0,
            "A genuinely nearer occluder must still hide the owning surface");
    // A real silhouette may have no second incident visible face.
    // Only the inside half can be shaded; it should never be pulled
    // forward to forge a full outside half.
    require(std::abs(corrected.left-1.0)<0.002,
            "One visible face contributes exactly one AA half-stroke");
    std::cout<<label<<": zero-gradient old ink="<<flat.integrated
             <<", nonzero-gradient old ink="<<steep.integrated
             <<", surface-attached ink="<<corrected.integrated
             <<", silhouette="<<corrected.left<<"\n";
}
}
int main() {
    try {test("M64 pole-dependent self-occlusion diagnosed");}
    catch(const std::exception& e) {
        std::cerr<<"M64 diagnosis failed: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
