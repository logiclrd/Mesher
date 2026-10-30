#include "mesher/new_model.hpp"
#include "mesher/sculpt_fixtures.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace mesher {
namespace {
void validateCount(std::uint32_t count,NewModelKind kind) {
    const auto minimum=kind==NewModelKind::Torus?128u:8u;
    if(count<minimum||count>1000000u)
        throw std::invalid_argument(
            "New Model vertex count must be between "+
            std::to_string(minimum)+" and 1000000");
}
} // namespace

std::string_view newModelName(NewModelKind kind) noexcept {
    switch(kind) {
    case NewModelKind::AsymmetricSphere:return "Asymmetric Sphere";
    case NewModelKind::SymmetricSphere:return "Symmetric Sphere";
    case NewModelKind::Torus:return "Torus";
    }
    return "Unknown model";
}

std::uint32_t parseNewModelVertexCount(
    std::string_view decimal,NewModelKind kind) {
    if(decimal.empty()||decimal.size()>7)
        throw std::invalid_argument("New Model requires a numeric vertex count");
    std::uint32_t value=0;
    const auto [p,ec]=std::from_chars(decimal.data(),
                                      decimal.data()+decimal.size(),value);
    if(ec!=std::errc{}||p!=decimal.data()+decimal.size())
        throw std::invalid_argument("New Model vertices must be a decimal integer");
    validateCount(value,kind);
    return value;
}

PreparedNewModelSource generateNewModelSource(
    NewModelRequest request,const SphereOptions& startupOptions) {
    if(request.kind!=NewModelKind::AsymmetricSphere&&
       request.kind!=NewModelKind::SymmetricSphere&&
       request.kind!=NewModelKind::Torus)
        throw std::invalid_argument("Unknown New Model primitive");
    validateCount(request.vertices,request.kind);
    const bool symmetric=request.kind==NewModelKind::SymmetricSphere;
    Mesh next;
    if(request.kind==NewModelKind::Torus) {
        next=generateTorusFixture(request.vertices,startupOptions.seed,
                                  startupOptions.relaxationIterations);
        const auto audit=auditFixtureTopology(next);
        if(audit.components!=1||audit.eulerCharacteristic!=0)
            throw std::runtime_error(
                "New Model torus is not one closed genus-one component");
    }else {
        SphereOptions options=startupOptions;
        options.vertexCount=request.vertices;
        options.symmetric=symmetric;
        next=generateSphere(options);
        if(!validateSphere(next))
            throw std::runtime_error("New Model sphere topology validation failed");
    }
    if(next.vertices.empty()||next.triangles.empty())
        throw std::runtime_error("New Model generated empty geometry");
    ViewportSceneBounds bounds;
    for(const auto v:next.vertices)bounds.include(v);
    if(!bounds.hasVertices)
        throw std::runtime_error("New Model could not compute source bounds");
    return {std::move(next),bounds,symmetric};
}
} // namespace mesher
