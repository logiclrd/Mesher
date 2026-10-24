#pragma once
#include <cmath>
#include <stdexcept>

namespace mesher {

// M17C's scale crossover is independent of whether a face is CERTIFIED
// exempt from quality repair. The caller must first exclude any legitimate
// fixed planar R03 tip exemption; only a genuine defect comes here.
inline bool shouldRepairShapeBeforeDensity(
    double quality,double minimumQuality,double longestEdge,
    double localTarget,double coarseRatio) {
    if(!std::isfinite(quality)||!std::isfinite(minimumQuality)||
       !std::isfinite(longestEdge)||!std::isfinite(localTarget)||
       !std::isfinite(coarseRatio)||minimumQuality<=0||
       minimumQuality>=1||longestEdge<0||localTarget<=0||
       coarseRatio<=1)
        throw std::invalid_argument(
            "R06-M17C requires finite positive quality and local scale");
    return quality<minimumQuality&&
        longestEdge<=localTarget*coarseRatio;
}
} // namespace mesher
