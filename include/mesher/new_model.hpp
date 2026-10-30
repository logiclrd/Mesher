#pragma once

#include "mesher/mesh.hpp"
#include "mesher/viewport_scale.hpp"

#include <cstdint>
#include <string_view>

namespace mesher {

enum class NewModelKind {AsymmetricSphere,SymmetricSphere,Torus};
struct NewModelRequest {
    NewModelKind kind=NewModelKind::SymmetricSphere;
    std::uint32_t vertices=5000;
};
struct PreparedNewModelSource {
    Mesh mesh;
    ViewportSceneBounds bounds;
    bool symmetric{};
};

// A pure, privately owned replacement source. Never touches the active
// worker, history, filename, renderer, or CWD. The host must build and
// validate its replacement worker before committing a new document.
PreparedNewModelSource generateNewModelSource(
    NewModelRequest request,const SphereOptions& startupOptions);

// UI-independent input validation, before doing expensive generation.
// Every normal model shares a maximum. Torus has a larger minimum.
std::uint32_t parseNewModelVertexCount(std::string_view decimal,
                                      NewModelKind kind);
std::string_view newModelName(NewModelKind kind) noexcept;

} // namespace mesher
