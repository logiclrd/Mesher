#pragma once

#include "mesher/new_model_dialog.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace mesher {

// One RGBA8 atlas for the three committed transparent PNGs, built from the
// existing PNG-to-RGBA build assets. Only one sampled image/descriptor is
// needed by the Vulkan viewport, regardless of dialog redraw frequency.
struct NewModelPreviewAtlas {
    struct Tile {
        std::uint32_t width=0, height=0;
        float u0=0, v0=0, u1=0, v1=0;
    };
    std::uint32_t width=0, height=0;
    std::vector<std::uint8_t> rgba;
    std::array<Tile,3> tiles{};
};

// Read all three converted images once. Transparent gutters prevent linear
// filtering from drawing a sliver of the neighboring preview.
NewModelPreviewAtlas loadNewModelPreviewAtlas(
    const std::filesystem::path& assetsDirectory);

// Uniform aspect-fit within a choice card, reserving the bottom text label.
// Never crops or stretches the source artwork.
NewDialogRect fitNewModelPreview(NewDialogRect card,
                                 std::uint32_t width,std::uint32_t height);
} // namespace mesher
