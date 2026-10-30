#pragma once

#include "mesher/brush_radius.hpp"
#include "mesher/region_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace mesher {

// Import-time operation only: uniformly sample immutable triangle edges
// across all source partitions without changing geometry or worker state.
inline double medianImportedEdgeLength(
    const RegionTopologyIndex& source,std::size_t maxFaceSamples=8192) {
    if(!maxFaceSamples)
        throw std::invalid_argument("Imported edge sample limit must be positive");
    std::size_t faceCount=0;
    source.revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {faceCount+=part.faces.size();});
    if(!faceCount)
        throw std::invalid_argument("Imported mesh has no triangle edges");
    const std::size_t stride=1+(faceCount-1)/maxFaceSamples;
    std::vector<double> edges;
    edges.reserve(std::min(faceCount,maxFaceSamples)*3);
    std::size_t ordinal=0;
    source.revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& face:part.faces) {
            if(ordinal++%stride)continue;
            for(int k=0;k<3;++k) {
                const Vec3 a=source.vertex(face.vertices[k]).position;
                const Vec3 b=source.vertex(face.vertices[(k+1)%3]).position;
                const double edge=length(b-a);
                if(std::isfinite(edge)&&edge>0)edges.push_back(edge);
            }
        }
    });
    if(edges.empty())
        throw std::invalid_argument("Imported mesh has no finite positive edges");
    const auto middle=edges.begin()+edges.size()/2;
    std::nth_element(edges.begin(),middle,edges.end());
    return *middle;
}

struct ImportedBrushCalibration {
    BrushRadiusRange radius{};
    double workerScale=1.0;
    double medianEdge=0.0;
};

inline ImportedBrushCalibration calibrateImportedBrush(
    const RegionTopologyIndex& source,double generatedNominalEdge,
    double sourceBoundingRadius) {
    if(!std::isfinite(sourceBoundingRadius)||sourceBoundingRadius<=0)
        throw std::invalid_argument("Imported brush requires finite source bounds");
    const double edge=medianImportedEdgeLength(source);
    const double maximum=sourceBoundingRadius*0.5;
    const double minimum=std::min(maximum,edge*0.5);
    if(!std::isfinite(generatedNominalEdge)||generatedNominalEdge<=0)
        throw std::invalid_argument("Invalid generated reference edge length");
    const double workerScale=edge/generatedNominalEdge;
    if(!std::isfinite(workerScale)||workerScale<=0||
       !std::isfinite(maximum)||maximum<=0||
       !std::isfinite(minimum)||minimum<=0)
        throw std::overflow_error("Imported brush calibration is not finite");
    return {{minimum,maximum},workerScale,edge};
}

inline double relativeImportedBrushRadius(
    const BrushRadiusRange& previous,double previousRadius,
    const BrushRadiusRange& next) {
    if(!std::isfinite(previous.minimum)||previous.minimum<=0||
       !std::isfinite(next.minimum)||next.minimum<=0)
        throw std::invalid_argument("Relative brush conversion needs positive edges");
    return next.clamp((previous.clamp(previousRadius)/previous.minimum)*
                      next.minimum);
}
} // namespace mesher
