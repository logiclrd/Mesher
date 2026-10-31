#include "mesher/new_model_preview_atlas.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool yes,const char* message) {
    if(!yes)throw std::runtime_error(message);
}
}
int main() {
    try {
        const auto atlas=mesher::loadNewModelPreviewAtlas(
            std::filesystem::path(MESHER_PREVIEW_ASSET_DIR));
        check(atlas.width>0&&atlas.height>0&&
              atlas.rgba.size()==std::size_t(atlas.width)*atlas.height*4u,
              "Atlas RGBA payload has invalid dimensions");
        const mesher::NewDialogRect card{40,50,240,280};
        for(const auto& tile:atlas.tiles) {
            check(tile.width>0&&tile.height>0&&
                  tile.u0>=0.f&&tile.v0>=0.f&&
                  tile.u1<=1.f&&tile.v1<=1.f&&
                  tile.u0<tile.u1&&tile.v0<tile.v1,
                  "Converted preview has invalid atlas coordinates");
            const auto fitted=mesher::fitNewModelPreview(
                card,tile.width,tile.height);
            check(fitted.width>0&&fitted.height>0&&
                  fitted.x>=card.x&&fitted.y>=card.y&&
                  fitted.x+fitted.width<=card.x+card.width&&
                  fitted.y+fitted.height<=card.y+card.height-34,
                  "Preview does not fit inside card above label");
            const double natural=double(tile.width)/double(tile.height);
            const double rendered=double(fitted.width)/double(fitted.height);
            check(std::abs(rendered-natural)<.07,
                  "Preview aspect ratio was distorted");
            // A transparent edge is essential for correct alpha blending.
            std::size_t transparent=0,colored=0;
            const auto x0=static_cast<std::size_t>(
                std::round(tile.u0*atlas.width));
            const auto y0=static_cast<std::size_t>(
                std::round(tile.v0*atlas.height));
            for(std::uint32_t y=0;y<tile.height;++y)
                for(std::uint32_t x=0;x<tile.width;++x) {
                    const auto i=((y0+y)*std::size_t(atlas.width)+x0+x)*4u;
                    const unsigned a=atlas.rgba[i+3];
                    if(a==0)++transparent;
                    else ++colored;
                }
            check(transparent>0&&colored>0,
                  "Preview must retain its transparent PNG background and visible pixels");
        }
        for(std::size_t i=1;i<atlas.tiles.size();++i)
            check(atlas.tiles[i-1].u1<atlas.tiles[i].u0,
                  "Transparent gutter missing between previews");
        check(mesher::fitNewModelPreview(card,0,512).width==0,
              "Invalid image dimensions must not produce geometry");
        std::cout<<"UX09 PNG alpha/aspect preview assets passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"UX09 asset validation failed: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
