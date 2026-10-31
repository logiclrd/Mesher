#include "mesher/new_model_preview_atlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace mesher {
namespace {
struct RgbaPicture {
    std::uint32_t width{},height{};
    std::vector<std::uint8_t> pixels;
};

RgbaPicture readPicture(const std::filesystem::path& filename) {
    std::ifstream file(filename,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot open New preview "+filename.string());
    const auto length=file.tellg();
    if(length<8)throw std::runtime_error("Truncated New preview "+filename.string());
    file.seekg(0);
    std::array<std::uint8_t,8> header{};
    file.read(reinterpret_cast<char*>(header.data()),8);
    const auto number=[&](std::size_t offset) -> std::uint32_t {
        return std::uint32_t(header[offset])|
               (std::uint32_t(header[offset+1])<<8)|
               (std::uint32_t(header[offset+2])<<16)|
               (std::uint32_t(header[offset+3])<<24);
    };
    const auto w=number(0),h=number(4);
    if(!w||!h||w>4096||h>4096||
       std::uint64_t(w)*h*4u>
           static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())||
       static_cast<std::uint64_t>(length)!=8ull+std::uint64_t(w)*h*4u)
        throw std::runtime_error("Invalid New preview dimensions/size "+filename.string());
    RgbaPicture result;
    result.width=w;result.height=h;
    result.pixels.resize(std::size_t(w)*h*4);
    file.read(reinterpret_cast<char*>(result.pixels.data()),
              static_cast<std::streamsize>(result.pixels.size()));
    if(!file)throw std::runtime_error("Incomplete New preview "+filename.string());
    return result;
}
} // namespace

NewModelPreviewAtlas loadNewModelPreviewAtlas(
    const std::filesystem::path& assetDirectory) {
    constexpr std::array names{
        "New-AsymmetricSphere.rgba","New-SymmetricSphere.rgba",
        "New-Torus.rgba"};
    std::array<RgbaPicture,3> pictures{};
    std::uint64_t packedWidth=2;
    std::uint32_t maxHeight=0;
    for(std::size_t i=0;i<pictures.size();++i) {
        pictures[i]=readPicture(assetDirectory/names[i]);
        packedWidth+=std::uint64_t(pictures[i].width)+2;
        maxHeight=std::max(maxHeight,pictures[i].height);
    }
    if(packedWidth>8192||maxHeight>8188)
        throw std::runtime_error("New Model preview atlas exceeds supported size");
    NewModelPreviewAtlas atlas;
    atlas.width=static_cast<std::uint32_t>(packedWidth);
    atlas.height=maxHeight+4;
    atlas.rgba.assign(std::size_t(atlas.width)*atlas.height*4u,0);
    std::uint32_t x=2;
    for(std::size_t i=0;i<pictures.size();++i) {
        const auto& picture=pictures[i];
        const std::uint32_t y=2+(maxHeight-picture.height)/2;
        for(std::uint32_t row=0;row<picture.height;++row)
            std::copy_n(picture.pixels.data()+std::size_t(row)*picture.width*4u,
                        std::size_t(picture.width)*4u,
                        atlas.rgba.data()+
                            (std::size_t(y+row)*atlas.width+x)*4u);
        atlas.tiles[i]={
            picture.width,picture.height,
            float(x)/float(atlas.width),
            float(y)/float(atlas.height),
            float(x+picture.width)/float(atlas.width),
            float(y+picture.height)/float(atlas.height)
        };
        x+=picture.width+2;
    }
    return atlas;
}

NewDialogRect fitNewModelPreview(
    NewDialogRect card,std::uint32_t width,std::uint32_t height) {
    if(!width||!height||card.width<18||card.height<53)return {};
    constexpr int inset=9,labelSpace=35;
    const int availableW=card.width-2*inset;
    const int availableH=card.height-labelSpace-2*inset;
    if(availableW<=0||availableH<=0)return {};
    const double scale=std::min(
        double(availableW)/width,double(availableH)/height);
    const int fittedW=std::clamp(
        int(std::floor(width*scale)),1,availableW);
    const int fittedH=std::clamp(
        int(std::floor(height*scale)),1,availableH);
    return {card.x+inset+(availableW-fittedW)/2,
            card.y+inset+(availableH-fittedH)/2,
            fittedW,fittedH};
}
} // namespace mesher
