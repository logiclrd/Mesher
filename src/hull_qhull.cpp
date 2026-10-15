#include "mesher/mesh.hpp"

#include <libqhull_r/qhull_ra.h>
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mesher {

// Qhull's re-entrant Quickhull implementation avoids the quadratic scan
// over all existing faces during incremental point insertion. The returned
// triangles retain input vertex indices and are oriented outwards.
std::vector<Triangle> quickHull(const std::vector<Vec3>& points) {
    if(points.size()<4 || points.size()>std::size_t(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Invalid hull point count");
    std::vector<coordT> coords;
    coords.reserve(points.size()*3);
    Vec3 interior{};
    for(const Vec3 p:points) {
        coords.push_back(p.x);
        coords.push_back(p.y);
        coords.push_back(p.z);
        interior=interior+p;
    }
    interior=interior/double(points.size());
    qhT state{};
    qhT* qh=&state;
    qh_zero(qh,stderr);
    char flags[]="qhull Qt Qx";
    const int exitCode=qh_new_qhull(qh,3,static_cast<int>(points.size()),
                                     coords.data(),False,flags,nullptr,stderr);
    std::vector<Triangle> result;
    if(exitCode==qh_ERRnone) {
        result.reserve(2*points.size());
        facetT* facet=nullptr;
        vertexT* vertex=nullptr;
        vertexT** vertexp=nullptr;
        FORALLfacets {
            std::array<std::uint32_t,3> ids{};
            unsigned count=0;
            FOREACHvertex_(facet->vertices) {
                if(count>=3)break;
                const int index=qh_pointid(qh,vertex->point);
                if(index<0 || index>=static_cast<int>(points.size()))
                    throw std::runtime_error("Qhull returned an invalid vertex index");
                ids[count++]=static_cast<std::uint32_t>(index);
            }
            if(count!=3)throw std::runtime_error("Qhull did not triangulate a face");
            auto [a,b,c]=ids;
            const auto n=cross(points[b]-points[a],points[c]-points[a]);
            if(dot(n,interior-points[a])>0)std::swap(b,c);
            result.push_back({a,b,c});
        }
    }
    qh_freeqhull(qh,!qh_ALL);
    int leakedCurrent=0,leakedTotal=0;
    qh_memfreeshort(qh,&leakedCurrent,&leakedTotal);
    if(exitCode!=qh_ERRnone)
        throw std::runtime_error("Qhull could not triangulate the sphere");
    return result;
}

} // namespace mesher
