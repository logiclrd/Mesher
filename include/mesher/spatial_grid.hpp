#pragma once
#include "mesher/mesh.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mesher {

// Uniform 3D spatial hash (an octree's leaf-equivalent for fixed-size queries).
// Bounds are not constrained to the initial unit sphere: sculpted vertices
// can move freely. An exact squared-distance check filters the bucket results.
class VertexSpatialGrid {
public:
    explicit VertexSpatialGrid(double cellSize);
    void rebuild(const std::vector<Vec3>& vertices);
    void update(const std::vector<Vec3>& vertices,
                const std::vector<std::uint32_t>& changed);
    void querySphere(const std::vector<Vec3>& vertices,
                     Vec3 center,double radius,
                     std::vector<std::uint32_t>& result) const;
    std::size_t indexedVertices() const noexcept { return locations_.size(); }
private:
    struct Cell {
        std::int32_t x{},y{},z{};
        bool operator==(const Cell&) const = default;
    };
    struct Hash {
        std::size_t operator()(Cell c) const noexcept;
    };
    struct Location {
        Cell cell{};
        std::uint32_t slot{};
    };
    Cell cellFor(Vec3 point) const;
    void insert(std::uint32_t i,Cell cell);
    double cellSize_;
    std::unordered_map<Cell,std::vector<std::uint32_t>,Hash> bins_;
    std::vector<Location> locations_;
};
} // namespace mesher
