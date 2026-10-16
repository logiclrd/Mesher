#pragma once
#include "mesher/repair_surgery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

// Internal pure helper. Kept independently testable so multi-edge
// subdivisions can be checked without opening a live revision.
namespace mesher::detail {
inline double refinedTriangleQuality(Vec3 a,Vec3 b,Vec3 c) {
    const auto ab=dot(b-a,b-a),bc=dot(c-b,c-b),ca=dot(a-c,a-c);
    const auto sum=ab+bc+ca;
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
inline using DirectedBoundary=std::pair<VertexId,VertexId>;
using SideSplits=std::map<DirectedBoundary,std::vector<VertexId>>;
// Split any combination of a triangle's three oriented boundary edges.
// Every original side is advanced geometrically, not by counterpart index.
// Working triangulation stays in memory until ONE atomic RegionEdit update.
std::vector<std::array<VertexId,3>> subdivideRetainedTriangle(
    std::array<VertexId,3> original,
    const SideSplits& splits,
    const std::unordered_map<VertexId,Vec3>& positions,
    const RepairSurgerySettings& settings) {
    std::vector<std::array<VertexId,3>> triangles{original};
    for(unsigned side=0;side<3;++side) {
        const auto from=original[side],to=original[(side+1)%3];
        const auto found=splits.find({from,to});
        if(found==splits.end())continue;
        VertexId at=from;
        for(const auto inserted:found->second) {
            bool replaced=false;
            for(std::size_t i=0;i<triangles.size();++i) {
                const auto triangle=triangles[i];
                for(unsigned j=0;j<3;++j) {
                    if(triangle[j]!=at||triangle[(j+1)%3]!=to)
                        continue;
                    const auto tip=triangle[(j+2)%3];
                    triangles[i]={at,inserted,tip};
                    triangles.push_back({inserted,to,tip});
                    replaced=true;
                    break;
                }
                if(replaced)break;
            }
            if(!replaced)
                throw std::logic_error("Boundary split has lost its retained side");
            at=inserted;
        }
    }
    for(const auto& triangle:triangles) {
        const auto a=positions.at(triangle[0]),
                   b=positions.at(triangle[1]),
                   d=positions.at(triangle[2]);
        if(refinedTriangleQuality(a,b,d)<settings.minimumTriangleQuality)
            throw std::logic_error("Refined retained face has a skinny triangle");
    }
    return triangles;
}

} // namespace mesher::detail
