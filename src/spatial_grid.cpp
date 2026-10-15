#include "mesher/spatial_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mesher {

VertexSpatialGrid::VertexSpatialGrid(double cellSize):cellSize_(cellSize) {
    if(!std::isfinite(cellSize) || cellSize<=0)
        throw std::invalid_argument("Spatial grid cell size must be positive and finite");
}

std::size_t VertexSpatialGrid::Hash::operator()(Cell c) const noexcept {
    std::uint64_t x=static_cast<std::uint32_t>(c.x);
    std::uint64_t y=static_cast<std::uint32_t>(c.y);
    std::uint64_t z=static_cast<std::uint32_t>(c.z);
    std::uint64_t h=0x9e3779b97f4a7c15ULL;
    for(auto v:{x,y,z}) {
        v=(v^(v>>30))*0xbf58476d1ce4e5b9ULL;
        v=(v^(v>>27))*0x94d049bb133111ebULL;
        v^=v>>31;
        h^=v+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2);
    }
    return static_cast<std::size_t>(h);
}

VertexSpatialGrid::Cell VertexSpatialGrid::cellFor(Vec3 p) const {
    auto coord=[&](double x) {
        const double c=std::floor(x/cellSize_);
        if(!std::isfinite(c) || c<std::numeric_limits<std::int32_t>::min() ||
           c>std::numeric_limits<std::int32_t>::max())
            throw std::overflow_error("Spatial grid position out of range");
        return static_cast<std::int32_t>(c);
    };
    return {coord(p.x),coord(p.y),coord(p.z)};
}

void VertexSpatialGrid::insert(std::uint32_t i,Cell cell) {
    auto& bucket=bins_[cell];
    locations_[i]={cell,static_cast<std::uint32_t>(bucket.size())};
    bucket.push_back(i);
}

void VertexSpatialGrid::rebuild(const std::vector<Vec3>& vertices) {
    if(vertices.size()>std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("Too many indexed vertices");
    bins_.clear();
    bins_.reserve(vertices.size()/8+1);
    locations_.resize(vertices.size());
    for(std::uint32_t i=0;i<vertices.size();++i)
        insert(i,cellFor(vertices[i]));
}

void VertexSpatialGrid::update(const std::vector<Vec3>& vertices,
                               const std::vector<std::uint32_t>& changed) {
    if(vertices.size()!=locations_.size())
        throw std::invalid_argument("Topology changed: rebuild spatial index");
    for(const auto i:changed) {
        if(i>=vertices.size())throw std::out_of_range("Changed vertex is invalid");
        const Cell next=cellFor(vertices[i]);
        const Location old=locations_[i];
        if(next==old.cell)continue;
        auto it=bins_.find(old.cell);
        if(it==bins_.end())throw std::logic_error("Spatial index missing bucket");
        auto& bucket=it->second;
        const auto moved=bucket.back();
        bucket[old.slot]=moved;
        locations_[moved].slot=old.slot;
        bucket.pop_back();
        if(bucket.empty())bins_.erase(it);
        insert(i,next);
    }
}

void VertexSpatialGrid::querySphere(const std::vector<Vec3>& vertices,
                                   Vec3 center,double radius,
                                   std::vector<std::uint32_t>& result) const {
    if(!std::isfinite(radius)||radius<=0)
        throw std::invalid_argument("Sphere radius must be positive and finite");
    if(vertices.size()!=locations_.size())
        throw std::logic_error("Spatial index is stale");
    const Cell low=cellFor(center-Vec3{radius,radius,radius});
    const Cell high=cellFor(center+Vec3{radius,radius,radius});
    const double radiusSquared=radius*radius;
    for(std::int64_t ix=low.x;ix<=high.x;++ix)
      for(std::int64_t iy=low.y;iy<=high.y;++iy)
        for(std::int64_t iz=low.z;iz<=high.z;++iz) {
            const auto it=bins_.find({static_cast<std::int32_t>(ix),
                                      static_cast<std::int32_t>(iy),
                                      static_cast<std::int32_t>(iz)});
            if(it==bins_.end())continue;
            for(const auto i:it->second) {
                const Vec3 diff=vertices[i]-center;
                if(dot(diff,diff)<radiusSquared)result.push_back(i);
            }
        }
}
} // namespace mesher
