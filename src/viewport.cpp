#include "mesher/viewport.hpp"
#include "mesher/startup_splash.hpp"
#include "mesher/repair_step_hud.hpp"
#include "mesher/desktop_merge_input.hpp"
#include "mesher/revision_retirement.hpp"
#include "mesher/picking.hpp"
#include "mesher/viewport_orbit.hpp"
#include "mesher/viewport_floor.hpp"
#include "mesher/wire_appearance.hpp"
#include "mesher/paint_controls.hpp"
#include "mesher/new_model_dialog.hpp"
#include "mesher/new_model_preview_atlas.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "mesher/mapped_keyboard.hpp"
#include "mesher/native_save_dialog.hpp"
#include "mesher/save_as_policy.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#ifdef MESHER_X11_WM_FULLSCREEN
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xatom.h>
#endif
#include <utility>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace mesher {
namespace {

void check(VkResult code, const char* operation) {
    if(code!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(code));
}

using Vertex=PartitionRenderVertex;
Vertex makeVertex(Vec3 p, Vec3 n={}, float material=0) {
    return {float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),material};
}
struct Drawing {std::vector<Vertex> solid;};

Drawing buildDrawing(const Mesh& mesh,Vec3 renderOrigin={}) {
    Drawing out;
    out.solid.reserve(mesh.triangles.size()*3);
    for(const auto& t:mesh.triangles) {
        const Vec3 a=mesh.vertices[t.a],b=mesh.vertices[t.b],c=mesh.vertices[t.c];
        const Vec3 rawNormal=cross(b-a,c-a);
        const Vec3 normal=length(rawNormal)>1e-20 ? normalized(rawNormal) : Vec3{};
        out.solid.push_back(makeVertex(a-renderOrigin,normal));
        out.solid.push_back(makeVertex(b-renderOrigin,normal));
        out.solid.push_back(makeVertex(c-renderOrigin,normal));
    }
    return out;
}

// Separate scenery from the real legacy/partitioned source geometry:
// the floor is its own GPU solid+wire allocation, not stale model
// triangles that could accidentally be drawn over a COW revision.
Drawing buildWorkFloor(double z,Vec3 center={},double halfExtent=8.0) {
    Drawing out;
    out.solid.reserve(6);
    const Vec3 a{center.x-halfExtent,center.y-halfExtent,z};
    const Vec3 b{center.x+halfExtent,center.y-halfExtent,z};
    const Vec3 c{center.x+halfExtent,center.y+halfExtent,z};
    const Vec3 d{center.x-halfExtent,center.y+halfExtent,z};
    for(const auto p:{a,b,c,a,c,d})
        out.solid.push_back(makeVertex(p-center,{0,0,1},1));
    // Unit XY grid now shaded on actual floor fragments (M68).
    return out;
}

using Glyph = std::array<std::uint8_t,7>;
// Compact built-in 5x7 font, independent of installed system fonts.
Glyph hudGlyph(char ch) {
    switch(ch) {
        case '0': return {14,17,19,21,25,17,14};
        case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31};
        case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2};
        case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14};
        case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14};
        case '9': return {14,17,17,15,1,1,14};
        case 'B': return {30,17,17,30,17,17,30};
        case 'C': return {14,17,16,16,16,17,14};
        case 'D': return {30,17,17,17,17,17,30};
        case 'E': return {31,16,16,30,16,16,31};
        case 'F': return {31,16,16,30,16,16,16};
        case 'G': return {14,17,16,23,17,17,14};
        case 'H': return {17,17,17,31,17,17,17};
        case 'I': return {14,4,4,4,4,4,14};
        case 'J': return {7,2,2,2,18,18,12};
        case 'K': return {17,18,20,24,20,18,17};
        case 'L': return {16,16,16,16,16,16,31};
        case 'N': return {17,25,21,19,17,17,17};
        case 'O': return {14,17,17,17,17,17,14};
        case 'Q': return {14,17,17,17,21,18,13};
        case 'U': return {17,17,17,17,17,17,14};
        case 'V': return {17,17,17,17,17,10,4};
        case 'W': return {17,17,17,21,21,21,10};
        case 'X': return {17,17,10,4,10,17,17};
        case 'Y': return {17,17,10,4,4,4,4};
        case 'Z': return {31,1,2,4,8,16,31};
        case ' ': return {};
        case 'P': return {30,17,17,30,16,16,16};
        case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30};
        case 'T': return {31,4,4,4,4,4,4};
        case 'A': return {14,17,17,31,17,17,17};
        case 'M': return {17,27,21,21,17,17,17};
        case ':': return {0,4,4,0,4,4,0};
        case '/': return {1,1,2,4,8,16,16};
        case '.': return {0,0,0,0,0,12,12};
        case '-': return {0,0,0,31,0,0,0};
        case '<': return {2,4,8,16,8,4,2};
        case '>': return {8,4,2,1,2,4,8};
        case '=': return {0,31,0,31,0,0,0};
        case ',': return {0,0,0,0,4,4,8};
        case '_': return {0,0,0,0,0,0,31};
        case '(': return {2,4,8,8,8,4,2};
        case ')': return {8,4,2,2,2,4,8};
        case '+': return {0,4,4,31,4,4,0};
        case '^': return {4,10,17,0,0,0,0};
        case '#': return {10,31,10,10,31,10,0};
        case '?': return {14,17,1,2,4,0,4};
        case '*': return {0,21,14,31,14,21,0};
        default: return {};
    }
}

struct RadiusSliderLayout {
    int left=20,trackX=30, top=0,bottom=0;
};
RadiusSliderLayout radiusSliderLayout(int height) {
    const int span=std::max(1,height/3);
    const int bottom=height-20;
    return {20,30,bottom-span,bottom};
}

struct GlobalScaleSliderLayout {
    int left{},trackX{},top{},bottom{};
    bool contains(double x,double y) const noexcept {
        return x>=left-8&&x<=left+58&&y>=top-34&&y<=bottom+12;
    }
};
GlobalScaleSliderLayout globalScaleSliderLayout(int width,int height) {
    const int bottom=height-20;
    return {width-73,width-63,bottom-std::max(1,height/3),bottom};
}
struct PaintPanelLayout {
    int left=96,top=20,width=290,height=445;
    int wheelX=240,wheelY=160,radius=72;
    int modeY=244,sliderX=161,sliderWidth=200,sliderY=291,spacing=30;
};
std::vector<Vertex> buildHud(std::uint32_t width, std::uint32_t height,
                             const std::string& fps, const std::string& stamps,
                             const std::string& merge,
                             const std::optional<BrushRadiusRange>& slider,
                             double radius,
                             double globalScaleExponent,bool scaleHovered,
                             const std::array<std::string,2>& classifierChoices,
                             bool classifierComparable,bool classifiersAgree,
                             int activeClassifier,
                             const std::vector<std::string>& repairStepLines,
                             std::size_t repairStepScroll,
                             bool paintMode,const PaintControls& paint) {
    constexpr int scale=2, characterWidth=6*scale, characterHeight=10*scale;
    constexpr int margin=16, padding=9;
    const int chars=int(std::max({fps.size(),stamps.size(),merge.size()}));
    const int boxW=chars*characterWidth+2*padding;
    const int boxH=(merge.empty()?2:3)*characterHeight+2*padding;
    const int left=std::max(margin,int(width)-margin-boxW);
    const int top=margin;
    std::vector<Vertex> vertices;
    vertices.reserve(4096);
    auto rectangle=[&](int x,int y,int w,int h,float material) {
        auto ndc=[&](int px,int py)->Vec3{
            return {2.0*px/double(width)-1.0,
                    2.0*py/double(height)-1.0, 0.0};
        };
        const auto a=makeVertex(ndc(x,y),{},material);
        const auto b=makeVertex(ndc(x+w,y),{},material);
        const auto c=makeVertex(ndc(x+w,y+h),{},material);
        const auto d=makeVertex(ndc(x,y+h),{},material);
        for(auto v:{a,b,c,a,c,d})vertices.push_back(v);
    };
    rectangle(left,top,boxW,boxH,1.0f);
    auto textLine=[&](const std::string& line,int row) {
        for(std::size_t i=0;i<line.size();++i) {
            const auto glyph=hudGlyph(line[i]);
            for(int y=0;y<7;++y)for(int x=0;x<5;++x)
                if(glyph[y] & (1u << (4-x))) {
                    rectangle(left+padding+int(i)*characterWidth+x*scale,
                              top+padding+row*characterHeight+y*scale,
                              scale,scale,0.0f);
                }
        }
    };
    textLine(fps,0);
    textLine(stamps,1);
    if(!merge.empty())textLine(merge,2);

    // M16D: classifier diagnostics ALWAYS occupy rows 0 and 1, and all
    // operation/argument text starts precisely at upper-left row 2.
    // This is a separate panel from the upper-right FPS/stamps status.
    if((!classifierChoices[0].empty()&&!classifierChoices[1].empty())||
       !repairStepLines.empty()) {
        const bool stepping=!repairStepLines.empty();
        const std::array<std::string,2> lines{{
            "AREA: "+(classifierChoices[0].empty()?"UNAVAILABLE":classifierChoices[0])+
                (activeClassifier==0?" <---":""),
            "PROXIMITY: "+(classifierChoices[1].empty()?"UNAVAILABLE":classifierChoices[1])+
                (activeClassifier==1?" <---":"")
        }};
        const int leftMax=std::max(160,std::min(
            int(width)-2*margin-2*padding,int(width)*2/3));
        const std::size_t maxColumns=std::size_t(std::max(12,leftMax/characterWidth));
        const int maxRows=std::max(3,std::min(19,
            (int(height)-2*margin-2*padding)/characterHeight));
        const auto page=layoutRepairStepHud(
            repairStepLines,maxColumns,
            std::size_t(std::max(1,maxRows-RepairStepHudPage::firstOperationRow)),
            repairStepScroll);
        const std::size_t visible=stepping?page.lines.size():0;
        const int rows=2+(stepping?int(visible):0);
        const int panelWidth=stepping?int(maxColumns)*characterWidth+2*padding:
            int(std::max(lines[0].size(),lines[1].size()))*characterWidth+2*padding;
        rectangle(margin,margin,panelWidth,
                  rows*characterHeight+2*padding,1.0f);
        const float tint=!classifierComparable?0.0f:
                         classifiersAgree?2.0f:3.0f;
        const auto leftText=[&](const std::string& line,int row,float color) {
            for(std::size_t i=0;i<line.size()&&i<maxColumns;++i) {
                const auto ch=static_cast<unsigned char>(line[i]);
                const auto glyph=hudGlyph(char(std::toupper(ch)));
                for(int y=0;y<7;++y)for(int x=0;x<5;++x)
                    if(glyph[y]&(1u<<(4-x)))
                        rectangle(margin+padding+int(i)*characterWidth+x*scale,
                                  margin+padding+row*characterHeight+y*scale,
                                  scale,scale,color);
            }
        };
        leftText(lines[0],0,tint);
        leftText(lines[1],1,tint);
        for(std::size_t i=0;i<visible;++i)
            leftText(page.lines[i],int(i)+RepairStepHudPage::firstOperationRow,0.0f);
    }

    if(slider) {
        const auto layout=radiusSliderLayout(int(height));
        const int center=layout.trackX+6;
        rectangle(layout.left-7,layout.top-34,63,
                  layout.bottom-layout.top+46,1.0f);
        rectangle(layout.trackX,layout.top,12,
                  layout.bottom-layout.top,0.0f);
        // Dark inner track and bright movable handle, drawn bottom to top.
        rectangle(layout.trackX+3,layout.top+3,6,
                  std::max(1,layout.bottom-layout.top-6),1.0f);
        const int knobY=layout.top+int(std::lround(
            slider->sliderFraction(radius)*double(layout.bottom-layout.top)));
        rectangle(center-15,knobY-6,30,12,0.0f);
        const std::string label="R:"+[&]{
            std::ostringstream os;
            os<<std::fixed<<std::setprecision(3)<<radius;
            return os.str();
        }();
        for(std::size_t i=0;i<label.size();++i) {
            const auto glyph=hudGlyph(label[i]);
            for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)
                if(glyph[yy]&(1u<<(4-xx)))
                    rectangle(layout.left-3+int(i)*8+xx,
                              layout.top-23+yy,1,1,0.0f);
        }
    }
    // Eleven uniformly-spaced ticks correspond to powers 10^-5 ... 10^5.
    {
        const auto layout=globalScaleSliderLayout(int(width),int(height));
        const int centre=layout.trackX+6;
        rectangle(layout.left-7,layout.top-34,63,
                  layout.bottom-layout.top+46,1.0f);
        rectangle(layout.trackX,layout.top,12,layout.bottom-layout.top,0.0f);
        rectangle(layout.trackX+3,layout.top+3,6,
                  std::max(1,layout.bottom-layout.top-6),1.0f);
        for(int tick=0;tick<=10;++tick) {
            const int y=layout.bottom-int(std::lround(
                double(tick)*(layout.bottom-layout.top)/10.0));
            rectangle(layout.trackX-5,y,22,1,0.0f);
        }
        const int knobY=layout.bottom-int(std::lround(
            (globalScaleExponent-kViewportMinExponent)/
            (kViewportMaxExponent-kViewportMinExponent)*
            (layout.bottom-layout.top)));
        rectangle(centre-15,knobY-6,30,12,0.0f);
        const std::string title="SCALE";
        for(std::size_t i=0;i<title.size();++i) {
            const auto glyph=hudGlyph(title[i]);
            for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)
                if(glyph[yy]&(1u<<(4-xx)))
                    rectangle(layout.left-4+int(i)*8+xx,
                              layout.top-23+yy,1,1,0.0f);
        }
        if(scaleHovered) {
            const auto label=viewportScaleLabel(globalScaleExponent);
            const int x0=std::max(3,layout.trackX-14-int(label.size())*8);
            const int y0=std::clamp(knobY-4,3,std::max(3,int(height)-12));
            rectangle(x0-3,y0-4,int(label.size())*8+6,15,1.0f);
            for(std::size_t i=0;i<label.size();++i) {
                const auto glyph=hudGlyph(label[i]);
                for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)
                    if(glyph[yy]&(1u<<(4-xx)))
                        rectangle(x0+int(i)*8+xx,y0+yy,1,1,0.0f);
            }
        }
    }
    if(paintMode&&width>420&&height>475) {
        const PaintPanelLayout panel;
        rectangle(panel.left,panel.top,panel.width,panel.height,1.0f);
        auto point=[&](double x,double y,VertexRGBA color) {
            auto ndc=Vec3{2.0*x/double(width)-1.0,
                          2.0*y/double(height)-1.0,0.0};
            auto v=makeVertex(ndc,{},4.0f);
            v.r=color.r;v.g=color.g;v.b=color.b;v.a=color.a;
            return v;
        };
        const auto appendTriangle=[&](Vertex a,Vertex b,Vertex c) {
            vertices.push_back(a);vertices.push_back(b);vertices.push_back(c);
        };
        // Circular hue/saturation wheel. Vertex RGBA is smoothly
        // interpolated by the ordinary Vulkan HUD pipeline.
        constexpr int segments=72,rings=5;
        constexpr double pi=3.14159265358979323846;
        const auto model=paint.channels==PaintChannels::HSL?
                         PaintChannels::HSL:PaintChannels::HSV;
        const auto values=model==PaintChannels::HSL?
                          rgbToHsl(paint.rgba):rgbToHsv(paint.rgba);
        for(int i=0;i<segments;++i)for(int j=0;j<rings;++j) {
            const double h0=double(i)/segments,h1=double(i+1)/segments;
            const double sat0=double(j)/rings,sat1=double(j+1)/rings;
            const auto vertex=[&](double hue,double sat) {
                const double angle=hue*2*pi;
                return point(panel.wheelX+std::cos(angle)*panel.radius*sat,
                             panel.wheelY-std::sin(angle)*panel.radius*sat,
                             hueToRgb({hue,sat,values[2]},model,1.0f));
            };
            const auto a=vertex(h0,sat0),b=vertex(h1,sat0);
            const auto c=vertex(h1,sat1),d=vertex(h0,sat1);
            appendTriangle(a,b,c);appendTriangle(a,c,d);
        }
        auto label=[&](int x,int y,const std::string& text,int scale=1) {
            for(std::size_t i=0;i<text.size();++i) {
                const auto glyph=hudGlyph(text[i]);
                for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)
                    if(glyph[yy]&(1u<<(4-xx)))
                        rectangle(x+int(i)*6*scale+xx*scale,
                                  y+yy*scale,scale,scale,0.0f);
            }
        };
        label(111,24,"PAINT (B TO EXIT)",2);
        // Cursor marks the actual selected hue/saturation, not merely a
        // colour wheel with no indication of its current selection.
        const int markX=panel.wheelX+int(std::lround(
            std::cos(values[0]*2*pi)*panel.radius*values[1]));
        const int markY=panel.wheelY-int(std::lround(
            std::sin(values[0]*2*pi)*panel.radius*values[1]));
        rectangle(markX-5,markY-5,10,2,0.0f);
        rectangle(markX-5,markY+3,10,2,0.0f);
        rectangle(markX-5,markY-3,2,6,0.0f);
        rectangle(markX+3,markY-3,2,6,0.0f);
        const std::array<std::string,3> names{{"RGB","HSV","HSL"}};
        for(int i=0;i<3;++i) {
            const int x=115+i*84;
            rectangle(x-5,panel.modeY-6,72,26,
                      int(paint.channels)==i?2.0f:1.0f);
            label(x,panel.modeY,names[std::size_t(i)],2);
        }
        const auto channels=paint.values();
        const auto letters=paint.channels==PaintChannels::RGB?
            std::array<char,3>{'R','G','B'}:
            paint.channels==PaintChannels::HSV?
            std::array<char,3>{'H','S','V'}:
            std::array<char,3>{'H','S','L'};
        for(int i=0;i<5;++i) {
            const int y=panel.sliderY+i*panel.spacing;
            const double selected=i<3?channels[std::size_t(i)]:
                                  i==3?paint.rgba.a:paint.hardness;
            label(118,y-4,std::string(1,i<3?letters[std::size_t(i)]:
                                     i==3?'A':'D'),2);
            for(int segment=0;segment<32;++segment) {
                const double fraction=double(segment)/31;
                PaintControls preview=paint;
                if(i<3)preview.setChannel(unsigned(i),fraction);
                else if(i==3)preview.setAlpha(fraction);
                else preview.setHardness(fraction);
                const auto color=preview.rgba;
                const auto a=point(panel.sliderX+segment*panel.sliderWidth/32,
                                   y-3,color);
                const auto b=point(panel.sliderX+(segment+1)*panel.sliderWidth/32,
                                   y-3,color);
                const auto c=point(panel.sliderX+(segment+1)*panel.sliderWidth/32,
                                   y+8,color);
                const auto d=point(panel.sliderX+segment*panel.sliderWidth/32,
                                   y+8,color);
                appendTriangle(a,b,c);appendTriangle(a,c,d);
            }
            const int handleX=panel.sliderX+int(selected*panel.sliderWidth);
            rectangle(handleX-2,y-6,5,17,0.0f);
        }
        // Swatch is a direct preview of the stored vertex paint RGB.
        const auto swatch=paint.rgba;
        const auto a=point(323,90,swatch),b=point(367,90,swatch);
        const auto c=point(367,119,swatch),d=point(323,119,swatch);
        appendTriangle(a,b,c);appendTriangle(a,c,d);
    }
    return vertices;
}

// UX09C: primitives are rendered inside the existing Vulkan HUD. No native
// popup and no secondary window can take keyboard focus away from the model.
// Three PNG-backed buttons inside the existing Vulkan HUD. All image drawing
// uses a single persistent texture, with alpha composited by the HUD pipeline.
void appendNewModelHud(std::vector<Vertex>& vertices,int width,int height,
                       const NewModelDialog& dialog,
                       const std::filesystem::path& hint,
                       const std::string& errorMessage,
                       const NewModelPreviewAtlas& atlas) {
    if(!dialog.visible()||width<=0||height<=0)return;
    const auto layout=newDialogLayout(
        width,height,dialog.phase(),dialog.canDirectSave());
    struct Color {float r,g,b,a;};
    // HSL hue 200 degrees throughout, with lightness changes for elevation.
    // Primary panel: HSL(200,42%,80%). Requested UI text is precisely
    // halfway from that background RGB to black.
    constexpr Color panel{0.716f,0.828f,0.884f,1.0f};
    constexpr Color text{panel.r*.5f,panel.g*.5f,panel.b*.5f,1.0f};
    constexpr Color outline{0.345f,0.607f,0.735f,1.0f};
    constexpr Color button{0.830f,0.897f,0.930f,1.0f};
    constexpr Color input{0.875f,0.929f,0.955f,1.0f};
    constexpr Color backdrop{0.060f,0.140f,0.180f,0.76f};
    constexpr Color error{0.270f,0.490f,0.585f,1.0f};
    const auto point=[&](double x,double y,float material,Color color,
                        float u=0,float v=0) {
        Vertex result=makeVertex(
            {2.0*x/width-1.0,2.0*y/height-1.0,0.0},{u,v,0},material);
        result.r=color.r;result.g=color.g;result.b=color.b;result.a=color.a;
        return result;
    };
    const auto quad=[&](double x0,double y0,double x1,double y1,Color color) {
        if(x1<=x0||y1<=y0)return;
        const auto a=point(x0,y0,6.0f,color);
        const auto b=point(x1,y0,6.0f,color);
        const auto c=point(x1,y1,6.0f,color);
        const auto d=point(x0,y1,6.0f,color);
        for(const auto& vertex:{a,b,c,a,c,d})vertices.push_back(vertex);
    };
    const auto box=[&](NewDialogRect rectangle,Color color) {
        quad(rectangle.x,rectangle.y,
             rectangle.x+rectangle.width,rectangle.y+rectangle.height,color);
    };
    const auto label=[&](int x,int y,std::string_view msg,
                        int scale=2,Color color=text) {
        for(std::size_t i=0;i<msg.size();++i) {
            const auto ch=static_cast<unsigned char>(msg[i]);
            const auto glyph=hudGlyph(char(std::toupper(ch)));
            for(int row=0;row<7;++row)for(int column=0;column<5;++column)
                if(glyph[std::size_t(row)]&(1u<<(4-column)))
                    quad(x+int(i)*6*scale+column*scale,
                         y+row*scale,
                         x+int(i)*6*scale+(column+1)*scale,
                         y+(row+1)*scale,color);
        }
    };
    const auto picture=[&](NewDialogRect card,
                           const NewModelPreviewAtlas::Tile& tile) {
        const auto fitted=fitNewModelPreview(card,tile.width,tile.height);
        if(!fitted.width||!fitted.height)return;
        constexpr Color white{1,1,1,1};
        const double x0=fitted.x,y0=fitted.y;
        const double x1=x0+fitted.width,y1=y0+fitted.height;
        const auto a=point(x0,y0,5.0f,white,tile.u0,tile.v0);
        const auto b=point(x1,y0,5.0f,white,tile.u1,tile.v0);
        const auto c=point(x1,y1,5.0f,white,tile.u1,tile.v1);
        const auto d=point(x0,y1,5.0f,white,tile.u0,tile.v1);
        for(const auto& vertex:{a,b,c,a,c,d})vertices.push_back(vertex);
    };
    const auto buttonBox=[&](NewDialogRect rectangle,std::string_view title) {
        if(rectangle.width<=0)return;
        box(rectangle,outline);
        quad(rectangle.x+2,rectangle.y+2,
             rectangle.x+rectangle.width-2,
             rectangle.y+rectangle.height-2,button);
        const int x=rectangle.x+std::max(
            4,(rectangle.width-int(title.size())*12)/2);
        label(x,rectangle.y+11,title,2,text);
    };
    // Semitransparent hue-200 veil over the existing viewport.
    box(layout.backdrop,backdrop);
    box(layout.panel,outline);
    quad(layout.panel.x+3,layout.panel.y+3,
         layout.panel.x+layout.panel.width-3,
         layout.panel.y+layout.panel.height-3,panel);
    if(dialog.phase()==NewDialogPhase::ChoosePrimitive) {
        label(layout.panel.x+22,layout.panel.y+17,
              "NEW MODEL - SELECT A PRIMITIVE",2,text);
        const auto tile=[&](NewDialogRect card,NewModelKind kind,
                            const NewModelPreviewAtlas::Tile& image) {
            box(card,outline);
            quad(card.x+3,card.y+3,
                 card.x+card.width-3,card.y+card.height-3,button);
            picture(card,image);
            const auto title=std::string(newModelName(kind));
            const int tx=card.x+std::max(
                4,(card.width-int(title.size())*6)/2);
            label(tx,card.y+card.height-23,title,1,text);
        };
        tile(layout.asymmetric,NewModelKind::AsymmetricSphere,atlas.tiles[0]);
        tile(layout.symmetric,NewModelKind::SymmetricSphere,atlas.tiles[1]);
        tile(layout.torus,NewModelKind::Torus,atlas.tiles[2]);
        label(layout.vertices.x-105,layout.vertices.y+9,"VERTICES:",2,text);
        box(layout.vertices,outline);
        quad(layout.vertices.x+2,layout.vertices.y+2,
             layout.vertices.x+layout.vertices.width-2,
             layout.vertices.y+layout.vertices.height-2,input);
        label(layout.vertices.x+10,layout.vertices.y+9,
              dialog.verticesText(),2,text);
        buttonBox(layout.cancel,"CANCEL");
    }else {
        label(layout.panel.x+22,layout.panel.y+24,
              "SAVE CHANGES BEFORE NEW?",2,text);
        if(!hint.empty()) {
            std::string name=hint.filename().string();
            if(name.size()>46)name=name.substr(0,43)+"...";
            label(layout.panel.x+22,layout.panel.y+70,name,2,text);
        }else label(layout.panel.x+22,layout.panel.y+70,
                    "UNTITLED DOCUMENT",2,text);
        const bool saving=dialog.phase()==NewDialogPhase::AwaitSave;
        if(saving)label(layout.panel.x+22,layout.panel.y+115,
                        "SAVING... ESC CANCELS NEW",1,text);
        else label(layout.panel.x+22,layout.panel.y+115,
                   "SAVE, SAVE AS, NO OR CANCEL",1,text);
        if(!saving) {
            buttonBox(layout.save,"SAVE");
            buttonBox(layout.saveAs,"SAVE AS");
            buttonBox(layout.discard,"NO");
        }
        buttonBox(layout.cancel,"CANCEL");
    }
    if(!errorMessage.empty()) {
        auto message=errorMessage;
        if(message.size()>84)message.resize(84);
        const int y=std::min(height-17,
            layout.panel.y+layout.panel.height+9);
        label(std::max(6,layout.panel.x),y,message,1,error);
    }
}

std::string hudDecimal(double value) {
    std::ostringstream os;
    os<<std::fixed<<std::setprecision(1)<<std::max(0.0,value);
    return os.str();
}

struct Matrix {std::array<float,16> m{};};
// Column-major matrices, compatible with GLSL's default mat4 convention.
void at(Matrix& m,int row,int col,float value){m.m[4*col+row]=value;}
float at(const Matrix& m,int row,int col){return m.m[4*col+row];}
Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix out;
    for(int row=0;row<4;++row)for(int col=0;col<4;++col){
        float sum=0;
        for(int k=0;k<4;++k)sum+=at(a,row,k)*at(b,k,col);
        at(out,row,col,sum);
    }
    return out;
}
Matrix cameraView(Vec3 eye) {
    const Vec3 forward=normalized(Vec3{}-eye);
    const Vec3 right=normalized(cross(forward,{0,0,1}));
    const Vec3 up=cross(right,forward);
    Matrix view;
    at(view,0,0,float(right.x));at(view,0,1,float(right.y));at(view,0,2,float(right.z));
    at(view,1,0,float(up.x));at(view,1,1,float(up.y));at(view,1,2,float(up.z));
    at(view,2,0,float(-forward.x));at(view,2,1,float(-forward.y));at(view,2,2,float(-forward.z));
    at(view,0,3,float(-dot(right,eye)));
    at(view,1,3,float(-dot(up,eye)));
    at(view,2,3,float(dot(forward,eye)));
    at(view,3,3,1);
    return view;
}
Matrix perspective(float aspect,float nearPlane,float farPlane) {
    constexpr float fov=0.7853981633974483f; // vertical field of view: 45 degrees
    const float f=1.f/std::tan(fov/2.f);
    Matrix m;
    at(m,0,0,f/aspect);
    at(m,1,1,-f); // Vulkan framebuffer coordinates have downward-positive Y.
    at(m,2,2,farPlane/(nearPlane-farPlane));
    at(m,2,3,nearPlane*farPlane/(nearPlane-farPlane));
    at(m,3,2,-1);
    return m;
}
struct PushConstants {
    Matrix viewProjection;
    std::array<float,4> light0;
    std::array<float,4> light1;
    // Actual framebuffer width/height; edge quad outer halfwidth in px.
    std::array<float,4> wireRaster;
};
static_assert(sizeof(PushConstants)==112);
PushConstants makeConstants(int width,int height,double cameraScale,
                            const ViewportOrbit& orbit,
                            const ViewportSceneBounds& bounds,
                            Vec3 cameraOffset) {
    const float aspect=float(width)/float(height);
    // Match picking and rendering camera geometry exactly.
    const Vec3 eye=viewportCameraEye(double(width),double(height))*cameraScale;
    const Vec3 actualEye=eye+cameraOffset;
    PushConstants result;
    Matrix model;
    const auto& axes=orbit.basis();
    for(int col=0;col<3;++col) {
        at(model,0,col,float(axes[std::size_t(col)].x*orbit.scale()));
        at(model,1,col,float(axes[std::size_t(col)].y*orbit.scale()));
        at(model,2,col,float(axes[std::size_t(col)].z*orbit.scale()));
    }
    // GPU vertices are source-centred before float conversion.
    const Vec3 offset=orbit.translation()-cameraOffset;
    at(model,0,3,float(offset.x));
    at(model,1,3,float(offset.y));
    at(model,2,3,float(offset.z));
    at(model,3,3,1.0f);
    const auto clips=viewportClipRange(actualEye,
        orbit.displayed(bounds.center()),bounds.radius()*orbit.scale());
    result.viewProjection=multiply(
        multiply(perspective(aspect,float(clips.nearPlane),
                            float(clips.farPlane)),cameraView(eye)),model);
    // Constant 45-degree perspective: movement comes exclusively from
    // the source-centred model scale and tracked camera translation.
    const Vec3 behindLeft=eye+Vec3{-2,-1.5,4};
    result.light0={float(behindLeft.x),float(behindLeft.y),float(behindLeft.z),1};
    result.light1={6,-1,3.5f,1};
    // M62: the 2px OUTER halfwidth over-darkened edges. The 1.5px
    // halfwidth retains a smooth one-pixel transition and keeps each
    // visible silhouette half sufficiently legible, without depth bias.
    // Pixel dimensions track the actual HiDPI framebuffer every frame.
    result.wireRaster={float(width),float(height),kWireOuterHalfWidthPixels,0.0f};
    return result;
}

std::vector<char> loadBinary(const std::string& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot open shader: "+path);
    const auto size=file.tellg();
    if(size<=0||size%4!=0)throw std::runtime_error("Invalid SPIR-V file: "+path);
    std::vector<char> data(static_cast<std::size_t>(size));
    file.seekg(0);
    if(!file.read(data.data(),size))throw std::runtime_error("Cannot read shader: "+path);
    return data;
}


#ifdef MESHER_X11_WM_FULLSCREEN
Window activeX11Window() {
    Display* display=glfwGetX11Display();
    if(!display)return None;
    const Atom active=XInternAtom(display,"_NET_ACTIVE_WINDOW",False);
    Atom type=None;
    int format=0;
    unsigned long count=0, remaining=0;
    unsigned char* property=nullptr;
    Window result=None;
    if(XGetWindowProperty(display,DefaultRootWindow(display),active,
                          0,1,False,XA_WINDOW,&type,&format,&count,
                          &remaining,&property)==Success &&
       type==XA_WINDOW && format==32 && count==1 && property)
        result=*reinterpret_cast<Window*>(property);
    if(property)XFree(property);
    return result;
}
void requestX11State(GLFWwindow* window) {
    Display* display=glfwGetX11Display();
    if(!display)return;
    XEvent event{};
    event.xclient.type=ClientMessage;
    event.xclient.window=glfwGetX11Window(window);
    event.xclient.message_type=XInternAtom(display,"_NET_WM_STATE",False);
    event.xclient.format=32;
    event.xclient.data.l[0]=1; // _NET_WM_STATE_ADD
    event.xclient.data.l[1]=long(XInternAtom(display,"_NET_WM_STATE_FULLSCREEN",False));
    event.xclient.data.l[3]=1; // normal application, not pager
    XSendEvent(display,DefaultRootWindow(display),False,
               SubstructureRedirectMask|SubstructureNotifyMask,&event);
    XFlush(display);
}
void requestX11Activation(Window window) {
    if(window==None)return;
    Display* display=glfwGetX11Display();
    if(!display)return;
    XEvent event{};
    event.xclient.type=ClientMessage;
    event.xclient.window=window;
    event.xclient.message_type=XInternAtom(display,"_NET_ACTIVE_WINDOW",False);
    event.xclient.format=32;
    event.xclient.data.l[0]=1; // application-originated activation request
    event.xclient.data.l[1]=CurrentTime;
    XSendEvent(display,DefaultRootWindow(display),False,
               SubstructureRedirectMask|SubstructureNotifyMask,&event);
    XFlush(display);
}
#endif

class VulkanViewport {
    const Mesh& mesh_;
    // Legacy demos use a flat Mesh; live sculpt renders an indexed root.
    RevisionRetirementQueue retiredRevisions_{64};
    std::shared_ptr<const RegionTopologyIndex> displayedIndex_;
    PartitionRenderCache partitionCache_;
    const Mesh& currentMesh() const {return mesh_;}
    ViewportAnimation animation_;
    ViewportInteraction interaction_;
    bool mouseWasDown_=false, sculpting_=false, radiusDragging_=false;
    bool scaleDragging_=false,scaleHovered_=false,scaleConsumedMouse_=false;
    double scaleDragLastY_=0;
    double scaleExponent_=0.0;
    ViewportSceneBounds sceneBounds_{};
    ViewportScaleFollow scaleFollow_;
    double previousFrameTime_=-1.0;
    Vec3 baseCameraEye(double width,double height) const noexcept {
        return viewportCameraEye(width,height)*interaction_.cameraScale;
    }
    Vec3 scaledCameraEye(double width,double height) const noexcept {
        return baseCameraEye(width,height)+scaleFollow_.cameraOffset();
    }
    bool paintMode_=false,paintModeKeyWasDown_=false,painting_=false;
    bool paintPanelDragging_=false;
    // Hover preview is UI-owned only: never part of COW state or undo history.
    std::vector<FaceId> paintPreviewFaces_;
    std::shared_ptr<const RegionTopologyIndex> paintPreviewIndex_;
    std::optional<Ray> paintPreviewRay_;
    double paintPreviewRadius_=-1;
    int paintPanelControl_=-1;
    std::optional<std::pair<double,double>> lastPaintCursor_;
    PaintControls paintControls_;
    bool wireVisible_=true,wireKeyWasDown_=false;
    bool inspectionFrozen_=false;
    bool inspectionIncomplete_=false;
    bool inspectionPreview_=false;
    bool inspectionCutRequested_=false;
    bool inspectionCutSucceeded_=false;
    bool inspectionZipperRequested_=false;
    bool inspectionZipperSucceeded_=false;
    bool inspectionCapRequested_=false;
    bool inspectionCapSucceeded_=false;
    bool inspectionAutoRequested_=false;
    bool inspectionAutoSucceeded_=false;
    bool inspectionQualityWarningPreview_=false;
    std::array<std::string,2> classifierChoices_{};
    bool classifierComparable_=false,classifiersAgree_=false;
    int activeClassifier_=0;
    bool inspectionStepReplaceMesh_=false;
    bool inspectionStepFinished_=false;
    bool inspectionStepCancelled_=false,inspectionStepError_=false;
    std::vector<std::string> repairStepLines_;
    std::size_t repairStepScroll_=0;
    bool stepNextWasDown_=false,stepPrevWasDown_=false;
    bool stepCurrentWasDown_=false,stepContinueWasDown_=false;
    bool stepCancelWasDown_=false,stepUpWasDown_=false,stepDownWasDown_=false;
    bool floorVisible_=true,floorKeyWasDown_=false;
    double floorZ_=-1.22;
    bool stampedThisFrame_=false;
    double lastPickMs_=0, lastUploadMs_=0, lastStampCallbackMs_=0, lastDrawMs_=0;
    double lastGpuFenceWaitMs_=0;
    mutable RegionTopologyIndex::PickStatistics lastPickStats_{};
    double lastConsoleTime_=0, viewportStartTime_=0, mouseReleaseTime_=0;
    std::uint64_t logFrames_=0, logStamps_=0;
    std::size_t displayedStamps_=0;
    std::uint64_t strokeSerial_=0, inputEventSerial_=0;
    bool waitingForDrain_=false;
    Vec3 lastCursorHit_{};
    double mouseStartTime_=0;
    bool paused_ = false, complete_ = false;
    bool spaceWasDown_ = false, rightWasDown_ = false, rewindWasDown_ = false;
    bool undoWasDown_=false,redoWasDown_=false,saveWasDown_=false,exportWasDown_=false;
    bool openWasDown_=false;
    NewModelDialog newModelDialog_;
    std::filesystem::path newModelHint_;
    std::string newModelError_;
    std::vector<char> newModelText_;
    bool modalBackspaceWasDown_=false;
    bool modalEscapeWasDown_=false,modalMouseWasDown_=false;
    bool suppressExitEscape_=false;

    bool oneShotWasDown_=false,oneShotAwaitingDisplay_=false;
    std::size_t oneShotRequestedCount_=0;
    DesktopMergeKeyEdges mergeKeys_;
    DesktopMergeInput mergeInput_;
    ViewportOrbit orbit_;
    bool orbitDragging_=false;
    Vec3 orbitPivotSource_{};
    ViewportCameraGestures cameraGestures_;
    ViewportCursorWrap orbitCursor_;
    std::string lastMergeStatus_;
    std::string lastCollisionStatus_;
    double lastCollisionReportTime_=-1.0;
    double nextStampTime_ = 0;
    double statsBegin_ = 0, measuredFps_ = 0, measuredStampRate_ = 0;
    std::uint64_t framesThisInterval_ = 0, stampsThisInterval_ = 0;
    std::size_t lastInvertedCount_ = 0;
    MappedKeyboard keyboard_;
    GLFWwindow* window=nullptr;
    std::unique_ptr<StartupSplash> startupSplash_;
    bool restorePreviousFocus_ = false;
#ifdef MESHER_X11_WM_FULLSCREEN
    Window previousActiveWindow_ = None;
#endif
    VkInstance instance=VK_NULL_HANDLE;
    VkSurfaceKHR surface=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue graphicsQueue=VK_NULL_HANDLE, presentQueue=VK_NULL_HANDLE;
    std::uint32_t graphicsFamily=0,presentFamily=0;
    VkSwapchainKHR swapchain=VK_NULL_HANDLE;
    VkFormat swapFormat=VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass=VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
    VkPipeline solidPipeline=VK_NULL_HANDLE,hudPipeline=VK_NULL_HANDLE;
    NewModelPreviewAtlas newModelAtlas_;
    VkImage newPreviewImage_=VK_NULL_HANDLE;
    VkDeviceMemory newPreviewMemory_=VK_NULL_HANDLE;
    VkImageView newPreviewView_=VK_NULL_HANDLE;
    VkSampler newPreviewSampler_=VK_NULL_HANDLE;
    VkDescriptorSetLayout newPreviewSetLayout_=VK_NULL_HANDLE;
    VkDescriptorPool newPreviewDescriptorPool_=VK_NULL_HANDLE;
    VkDescriptorSet newPreviewDescriptor_=VK_NULL_HANDLE;
    VkPipeline paintPreviewPipeline=VK_NULL_HANDLE;
    VkImage depthImage=VK_NULL_HANDLE;
    VkDeviceMemory depthMemory=VK_NULL_HANDLE;
    VkImageView depthView=VK_NULL_HANDLE;
    VkFormat depthFormat=VK_FORMAT_UNDEFINED;
    VkCommandPool commandPool=VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer=VK_NULL_HANDLE;
    VkSemaphore available=VK_NULL_HANDLE;
    std::vector<VkSemaphore> finishedForImage;
    VkFence fence=VK_NULL_HANDLE;
    struct Buffer {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;std::uint32_t vertexCount=0,capacity=0;std::size_t allocatedBytes=0;};
    Buffer solid{},floorSolid{},hud{};
    // Inspection has a separate buffer; legacy solid/floor/HUD ownership
    // remains textually and behaviorally unchanged.
    Buffer inspectionSolid{},paintPreviewSolid{};
    struct PartitionBuffers {Buffer solid;};
    std::map<PartitionId,PartitionBuffers> partitionBuffers_;
    std::size_t lastDirtyPartitions_=0,lastDirtyFaces_=0;
    std::size_t lastDirtyBytes_=0;
    std::vector<Buffer> deferredGpuBuffers_;
    std::size_t gpuResidentBytes_=0,gpuPeakResidentBytes_=0;
    std::size_t gpuRetiredBytes_=0,gpuFreedBytes_=0;

    static bool containsExtension(const std::vector<VkExtensionProperties>& list,const char* name) {
        return std::any_of(list.begin(),list.end(),[&](const auto& ext){return std::strcmp(ext.extensionName,name)==0;});
    }
    static std::vector<VkExtensionProperties> instanceExtensions() {
        std::uint32_t count=0;
        check(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr),"Enumerate instance extensions");
        std::vector<VkExtensionProperties> extensions(count);
        check(vkEnumerateInstanceExtensionProperties(nullptr,&count,extensions.data()),"Read instance extensions");
        return extensions;
    }
    static std::vector<VkExtensionProperties> deviceExtensions(VkPhysicalDevice gpu) {
        std::uint32_t count=0;
        check(vkEnumerateDeviceExtensionProperties(gpu,nullptr,&count,nullptr),"Enumerate device extensions");
        std::vector<VkExtensionProperties> extensions(count);
        check(vkEnumerateDeviceExtensionProperties(gpu,nullptr,&count,extensions.data()),"Read device extensions");
        return extensions;
    }
    void createInstance() {
        std::uint32_t count=0;
        const char** required=glfwGetRequiredInstanceExtensions(&count);
        if(!required)throw std::runtime_error("GLFW reports no Vulkan surface extensions");
        std::vector<const char*> extensions(required,required+count);
        VkInstanceCreateFlags flags=0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if(containsExtension(instanceExtensions(),VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            flags|=VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif
        VkApplicationInfo app{}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName="Mesher";
        app.applicationVersion=VK_MAKE_VERSION(0,1,0);
        app.pEngineName="Mesher";
        app.apiVersion=VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.flags=flags;
        info.pApplicationInfo=&app;
        info.enabledExtensionCount=static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames=extensions.data();
        check(vkCreateInstance(&info,nullptr,&instance),"Create Vulkan instance");
    }
    void selectDevice() {
        std::uint32_t count=0;
        check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"Enumerate GPUs");
        if(count==0)throw std::runtime_error("No Vulkan-capable GPU found");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"Read GPUs");
        for(auto gpu:devices) {
            if(!containsExtension(deviceExtensions(gpu),VK_KHR_SWAPCHAIN_EXTENSION_NAME))continue;
            std::uint32_t n=0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,nullptr);
            std::vector<VkQueueFamilyProperties> families(n);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,families.data());
            std::optional<std::uint32_t> graphics,present;
            for(std::uint32_t i=0;i<n;++i) {
                if(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)graphics=i;
                VkBool32 support=VK_FALSE;
                check(vkGetPhysicalDeviceSurfaceSupportKHR(gpu,i,surface,&support),"Query presentation support");
                if(support)present=i;
                if(graphics&&present)break;
            }
            if(!graphics||!present)continue;
            std::uint32_t formatCount=0,modeCount=0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(gpu,surface,&formatCount,nullptr);
            vkGetPhysicalDeviceSurfacePresentModesKHR(gpu,surface,&modeCount,nullptr);
            if(formatCount==0||modeCount==0)continue;
            physical=gpu;graphicsFamily=*graphics;presentFamily=*present;break;
        }
        if(physical==VK_NULL_HANDLE)throw std::runtime_error("No GPU supports Vulkan graphics + window presentation");
        const float priority=1;
        std::set<std::uint32_t> families{graphicsFamily,presentFamily};
        std::vector<VkDeviceQueueCreateInfo> queues;
        for(auto index:families) {
            VkDeviceQueueCreateInfo q{}; q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            q.queueFamilyIndex=index;q.queueCount=1;q.pQueuePriorities=&priority;
            queues.push_back(q);
        }
        std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        // Portability subset may be absent from Vulkan headers unless beta
        // extensions are enabled, but is mandatory on devices advertising it.
        if(containsExtension(deviceExtensions(physical),"VK_KHR_portability_subset"))
            extensions.push_back("VK_KHR_portability_subset");
        VkDeviceCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount=static_cast<std::uint32_t>(queues.size());
        info.pQueueCreateInfos=queues.data();
        info.enabledExtensionCount=static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames=extensions.data();
        check(vkCreateDevice(physical,&info,nullptr,&device),"Create Vulkan device");
        vkGetDeviceQueue(device,graphicsFamily,0,&graphicsQueue);
        vkGetDeviceQueue(device,presentFamily,0,&presentQueue);
    }
    std::uint32_t memoryType(std::uint32_t bits,VkMemoryPropertyFlags flags)const {
        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(physical,&props);
        for(std::uint32_t i=0;i<props.memoryTypeCount;++i)
            if((bits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw std::runtime_error("No compatible Vulkan memory type");
    }
    Buffer upload(const std::vector<Vertex>& vertices) {
        Buffer result;
        if(vertices.empty())throw std::runtime_error("Empty vertex buffer");
        result.vertexCount=result.capacity=static_cast<std::uint32_t>(vertices.size());
        const VkDeviceSize size=vertices.size()*sizeof(Vertex);
        VkBufferCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size=size;
        info.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device,&info,nullptr,&result.buffer),"Create vertex buffer");
        VkMemoryRequirements needs;
        vkGetBufferMemoryRequirements(device,result.buffer,&needs);
        VkMemoryAllocateInfo allocation{}; allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize=needs.size;
        allocation.memoryTypeIndex=memoryType(needs.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device,&allocation,nullptr,&result.memory),"Allocate vertex memory");
        result.allocatedBytes=static_cast<std::size_t>(needs.size);
        gpuResidentBytes_+=result.allocatedBytes;
        gpuPeakResidentBytes_=std::max(gpuPeakResidentBytes_,gpuResidentBytes_);
        check(vkBindBufferMemory(device,result.buffer,result.memory,0),"Bind vertex memory");
        void* data=nullptr;
        check(vkMapMemory(device,result.memory,0,size,0,&data),"Map vertex memory");
        std::memcpy(data,vertices.data(),static_cast<std::size_t>(size));
        vkUnmapMemory(device,result.memory);
        return result;
    }
    void destroyBuffer(Buffer& buffer) {
        if(buffer.buffer)vkDestroyBuffer(device,buffer.buffer,nullptr);
        if(buffer.memory)vkFreeMemory(device,buffer.memory,nullptr);
        if(buffer.allocatedBytes) {
            gpuResidentBytes_-=buffer.allocatedBytes;
            gpuFreedBytes_+=buffer.allocatedBytes;
        }
        buffer={};
    }
    void retireGpuBuffer(Buffer& buffer) {
        if(!buffer.buffer && !buffer.memory)return;
        gpuRetiredBytes_+=buffer.allocatedBytes;
        deferredGpuBuffers_.push_back(buffer);
        buffer={};
    }
    // Only called after the previous graphics fence or vkDeviceWaitIdle.
    // An old vertex allocation may be queued during a frame, but is never
    // freed until the submission which could reference it has completed.
    void drainGpuRetirements() {
        for(auto& old:deferredGpuBuffers_)destroyBuffer(old);
        deferredGpuBuffers_.clear();
        gpuRetiredBytes_=0;
    }
    // The previous frame fence is waited before any buffer replacement or
    // retirement. Only buffer objects for dirty partitions are touched; GPU
    // allocations belonging to unchanged partitions remain entirely intact.
    void replaceBuffer(Buffer& buffer,const std::vector<Vertex>& vertices) {
        if(vertices.empty()) {
            retireGpuBuffer(buffer);
            return;
        }
        if(buffer.buffer==VK_NULL_HANDLE||vertices.size()>buffer.capacity) {
            auto next=upload(vertices);
            retireGpuBuffer(buffer);
            buffer=next;
            return;
        }
        buffer.vertexCount=static_cast<std::uint32_t>(vertices.size());
        const VkDeviceSize size=vertices.size()*sizeof(Vertex);
        void* data=nullptr;
        check(vkMapMemory(device,buffer.memory,0,size,0,&data),
              "Map dynamic vertex buffer");
        std::memcpy(data,vertices.data(),static_cast<std::size_t>(size));
        vkUnmapMemory(device,buffer.memory);
    }
    void initializePartitions() {
        partitionCache_.initialize(displayedIndex_,orbit_.center());
        for(const auto& [id,geometry]:partitionCache_.partitions()) {
            auto& dst=partitionBuffers_[id];
            if(!geometry->solid.empty())dst.solid=upload(geometry->solid);
        }
    }
    void refreshPartitions(
        std::shared_ptr<const RegionTopologyIndex> next,
        const std::vector<PartitionId>& dirty,
        std::optional<Vec3> nextRenderOrigin=std::nullopt) {
        const double started=glfwGetTime();
        // The one-in-flight graphics submission has stopped reading all
        // older vertex buffers before we map, replace, or destroy any of
        // them. Present and resize use the existing device-idle handling.
        const auto fenceBegin=glfwGetTime();
        check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),
              "Wait before retiring dirty Vulkan partition buffers");
        lastGpuFenceWaitMs_=(glfwGetTime()-fenceBegin)*1000;
        drainGpuRetirements();
        if(nextRenderOrigin)partitionCache_.setRenderOrigin(*nextRenderOrigin);
        const auto stats=partitionCache_.advance(next,dirty,&retiredRevisions_);
        std::set<PartitionId> unique(dirty.begin(),dirty.end());
        for(const auto id:unique) {
            const auto found=partitionCache_.partitions().find(id);
            if(found==partitionCache_.partitions().end()) {
                const auto old=partitionBuffers_.find(id);
                if(old!=partitionBuffers_.end()) {
                    retireGpuBuffer(old->second.solid);
                    partitionBuffers_.erase(old);
                }
                continue;
            }
            auto& dst=partitionBuffers_[id];
            replaceBuffer(dst.solid,found->second->solid);
        }
        auto oldDisplayed=std::move(displayedIndex_);
        displayedIndex_=std::move(next);
        if(oldDisplayed!=displayedIndex_)
            retiredRevisions_.retire(std::move(oldDisplayed));
        lastDirtyPartitions_=stats.rebuiltPartitions;
        lastDirtyFaces_=stats.rebuiltFaces;
        lastDirtyBytes_=stats.uploadedBytes;
        lastUploadMs_=(glfwGetTime()-started)*1000;
        if(lastUploadMs_>150.0)
            std::cout<<"[render] slow_dirty_refresh_ms="<<lastUploadMs_
                     <<" fence_ms="<<lastGpuFenceWaitMs_
                     <<" partitions="<<lastDirtyPartitions_
                     <<" faces="<<lastDirtyFaces_
                     <<" bytes="<<lastDirtyBytes_<<"\n";
    }
    void refreshMesh() {
        const double started=glfwGetTime();
        // Synchronous demo mode retains its original flat-mesh buffers.
        check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),
              "Wait before updating legacy geometry");
        const auto drawing=buildDrawing(currentMesh(),orbit_.center());
        replaceBuffer(solid,drawing.solid);
        lastUploadMs_=(glfwGetTime()-started)*1000;
    }

    void refreshHud() {
        if (extent.width==0 || extent.height==0) return;
        const std::string fpsText="FPS: "+hudDecimal(measuredFps_);
        const std::string stampText="STAMPS: "+hudDecimal(measuredStampRate_)+
                "/"+hudDecimal(animation_.advance ? animation_.stampsPerSecond : interaction_.stationaryStampsPerSecond);
        const std::optional<BrushRadiusRange> slider=interaction_.setRadius
            ? std::optional<BrushRadiusRange>(interaction_.radiusRange)
            : std::nullopt;
        const std::string mergeText=interaction_.repairStepMode&&
            inspectionStepCancelled_?"R06: STEP CANCELLED":
            interaction_.repairStepMode&&inspectionStepError_?"R06: STEP ERROR":
            interaction_.repairStepMode&&!inspectionStepFinished_?
            "R06: STEP DEBUG PRIVATE":
            inspectionFrozen_&&inspectionAutoRequested_?
            (inspectionAutoSucceeded_?"R06: AUTO COMPLETE":
             inspectionQualityWarningPreview_?"R06: QUALITY WARN PREVIEW":"R06: AUTO REFUSED"):
            inspectionFrozen_&&inspectionCapRequested_?
            (inspectionCapSucceeded_?"R05: CAP COMPLETE":"R05: CAP REFUSED"):
            inspectionFrozen_&&inspectionZipperRequested_?
            (inspectionZipperSucceeded_?"R04: ZIPPER COMPLETE":"R04: ZIPPER REFUSED"):
            inspectionFrozen_&&inspectionCutRequested_?
            (inspectionCutSucceeded_?"R03: PRIVATE CUT":"R03: EXCISION REFUSED"):
            interaction_.begin?
            (!interaction_.mergeSelected?"MERGE: DISABLED":
            mergeInput_.enabled()?"MERGE: ON SEL "+std::to_string(mergeInput_.selectedCount()):
                     "MERGE: OFF"):"";
        auto geometry=buildHud(extent.width,extent.height,fpsText,stampText,
                                     mergeText,slider,interaction_.radius,
                                     scaleExponent_,scaleHovered_,classifierChoices_,classifierComparable_,
                                     classifiersAgree_,activeClassifier_,
                                     repairStepLines_,repairStepScroll_,
                                     paintMode_,paintControls_);
        if(newModelDialog_.visible())
            appendNewModelHud(geometry,int(extent.width),int(extent.height),
                              newModelDialog_,newModelHint_,newModelError_,
                              newModelAtlas_);
        if(hud.buffer==VK_NULL_HANDLE)hud=upload(geometry);
        else {
            check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),
                  "Wait before refreshing HUD");
            replaceBuffer(hud,geometry);
        }
    }

    // A deliberate RMB gesture may traverse the CURRENT immutable source
    // once to sample its centroid. Never traverse geometry every frame.
    Vec3 currentGeometryCentroid() const {
        Vec3 total{};
        std::size_t count=0;
        if(displayedIndex_) {
            displayedIndex_->revision().forEachPartition(
                [&](PartitionId,const MeshPartition& part) {
                    for(const auto& vertex:part.vertices) {
                        total=total+vertex.position;
                        ++count;
                    }
                });
        } else {
            for(const auto vertex:mesh_.vertices) {
                total=total+vertex;
                ++count;
            }
        }
        return count?total/double(count):Vec3{};
    }

    std::optional<Ray> cursorVisualRay() const {
        double cx=0,cy=0;
        glfwGetCursorPos(window,&cx,&cy);
        int ww=0,wh=0,fw=0,fh=0;
        glfwGetWindowSize(window,&ww,&wh);
        glfwGetFramebufferSize(window,&fw,&fh);
        if(ww<=0||wh<=0||fw<=0||fh<=0)return std::nullopt;
        const double px=cx*double(fw)/ww,py=cy*double(fh)/wh;
        if(px<0||py<0||px>=fw||py>=fh)return std::nullopt;
        auto ray=viewportRay(px,py,double(fw),double(fh));
        ray.origin=baseCameraEye(double(fw),double(fh))+
                   scaleFollow_.cameraOffset();
        return ray;
    }
    std::optional<Ray> cursorSourceRay() const {
        const auto visual=cursorVisualRay();
        if(!visual)return std::nullopt;
        return orbit_.pickingRay(*visual);
    }

    void frameScene(const ViewportSceneBounds& bounds) {
        if(!bounds.hasVertices)return;
        sceneBounds_=bounds;
        int fw=0,fh=0;
        glfwGetFramebufferSize(window,&fw,&fh);
        const double width=double(std::max(1,fw));
        const double height=double(std::max(1,fh));
        const double eyeDistance=length(viewportCameraEye(width,height))*
                                 interaction_.cameraScale;
        scaleExponent_=viewportFitExponent(bounds.radius(),eyeDistance,
                                            width/height);
        orbit_.frame(bounds.center(),viewportScaleFromExponent(scaleExponent_));
        scaleFollow_.reset();
        std::cout<<"[view] fitted bounds radius="<<bounds.radius()
                 <<" centre="<<bounds.center().x<<","<<bounds.center().y
                 <<","<<bounds.center().z<<" scale=10^"<<scaleExponent_<<"\n";
    }
    // Every scale movement is UI-only; no worker or immutable mesh edit.
    void beginScaleFollow(int width,int height,double now) {
        const Vec3 eye=scaledCameraEye(width,height);
        const Vec3 baseEye=baseCameraEye(width,height);
        const Vec3 centroid=currentGeometryCentroid();
        std::vector<ViewportZoomProbe> hits;
        hits.reserve(16);
        for(int row=0;row<4;++row)for(int col=0;col<4;++col) {
            const double x=(double(col)+0.5)*double(width)/4.0;
            const double y=(double(row)+0.5)*double(height)/4.0;
            auto ray=viewportRay(x,y,width,height);
            ray.origin=eye;
            const auto source=orbit_.pickingRay(ray);
            const auto hit=displayedIndex_?
                displayedIndex_->firstRayHit(source):
                firstMeshHit(mesh_,source);
            if(hit)hits.push_back({*hit,
                length(eye-orbit_.displayed(*hit)),true});
        }
        // Without a surface hit, use the centroid's screen-parallel
        // orbit plane. It is the same depth fallback as the RMB gesture.
        const Vec3 forward=normalized(Vec3{}-baseEye);
        auto centreRay=viewportRay(width*.5,height*.5,width,height);
        centreRay.origin=eye;
        const Vec3 fallback=orbit_.choosePivot(
            centreRay,std::nullopt,centroid,forward);
        const auto chosen=nearestViewportProbe(
            hits,centroid,length(eye-orbit_.displayed(fallback)));
        scaleFollow_.begin(chosen.sourcePoint,
                           orbit_.displayed(chosen.sourcePoint),baseEye,now);
        std::cout<<"[view] global scale anchor="
                 <<(chosen.surface?"surface 4x4":"centroid plane")
                 <<" distance="<<chosen.distance<<"\n";
    }
    void updateScaleFollow() {
        const double now=glfwGetTime();
        const double dt=previousFrameTime_<0?1.0/60.0:
            std::clamp(now-previousFrameTime_,0.0,0.25);
        previousFrameTime_=now;
        scaleFollow_.advance(dt,now);
    }
    void updateGlobalScaleInput() {
        scaleConsumedMouse_=false;
        double cx=0,cy=0;
        glfwGetCursorPos(window,&cx,&cy);
        int ww=0,wh=0,fw=0,fh=0;
        glfwGetWindowSize(window,&ww,&wh);
        glfwGetFramebufferSize(window,&fw,&fh);
        if(ww<=0||wh<=0||fw<=0||fh<=0)return;
        const double x=cx*double(fw)/ww,y=cy*double(fh)/wh;
        const auto slider=globalScaleSliderLayout(fw,fh);
        const bool hovering=slider.contains(x,y);
        if(hovering!=scaleHovered_) {
            scaleHovered_=hovering;
            refreshHud();
        }
        const bool down=glfwGetMouseButton(
            window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        if(!scaleDragging_&&down&&!mouseWasDown_&&hovering&&
           !sculpting_&&!painting_&&!orbitDragging_&&!radiusDragging_&&
           !paintPanelDragging_) {
            scaleDragging_=true;
            scaleDragLastY_=y;
        }
        if(!scaleDragging_)return;
        scaleConsumedMouse_=true;
        if(!down) {scaleDragging_=false;return;}
        const bool ctrl=keyboard_.down(GLFW_KEY_LEFT_CONTROL)||
                        keyboard_.down(GLFW_KEY_RIGHT_CONTROL);
        const double exponent=viewportScaleDrag(
            scaleExponent_,y-scaleDragLastY_,
            double(std::max(1,slider.bottom-slider.top)),ctrl);
        scaleDragLastY_=y;
        if(exponent!=scaleExponent_) {
            const double now=glfwGetTime();
            const Vec3 eye=baseCameraEye(double(fw),double(fh));
            if(!scaleFollow_.active())beginScaleFollow(fw,fh,now);
            scaleExponent_=exponent;
            orbit_.setScale(viewportScaleFromExponent(scaleExponent_));
            scaleFollow_.change(orbit_.displayed(scaleFollow_.sourceAnchor()),
                                eye,now);
            clearPaintPreview();
            refreshHud();
            updateTitle();
        }
    }
    void updateMouseOrbit() {
        const bool right=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
        const bool left=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        const bool ctrl=keyboard_.down(GLFW_KEY_LEFT_CONTROL)||
                        keyboard_.down(GLFW_KEY_RIGHT_CONTROL);
        const bool alt=keyboard_.down(GLFW_KEY_LEFT_ALT)||
                       keyboard_.down(GLFW_KEY_RIGHT_ALT);
        const auto gesture=cameraGestures_.sample(
            left,right,ctrl,alt,!sculpting_&&!radiusDragging_&&
                                 !scaleDragging_);
        if(gesture.started)scaleFollow_.cancel();
        double x=0,y=0;
        glfwGetCursorPos(window,&x,&y);
        int ww=0,wh=0,fw=0,fh=0;
        glfwGetWindowSize(window,&ww,&wh);
        glfwGetFramebufferSize(window,&fw,&fh);
        if(gesture.ended) {
            orbitDragging_=false;
            orbitCursor_.stop();
            if(gesture.endedMode==ViewportCameraMode::Strafe)
                std::cout<<"[view] world pan end\n";
            else if(gesture.endedMode==ViewportCameraMode::Dolly)
                std::cout<<"[view] Ctrl+RMB dolly end\n";
            else std::cout<<"[view] RMB orbit end\n";
        }else if(gesture.started) {
            if(gesture.mode==ViewportCameraMode::Orbit) {
                if(const auto visual=cursorVisualRay()) {
                    const auto source=orbit_.pickingRay(*visual);
                    const auto picked=displayedIndex_?
                        displayedIndex_->firstRayHit(source):
                        firstMeshHit(mesh_,source);
                    const Vec3 forward=normalized(
                        Vec3{}-baseCameraEye(double(fw),double(fh)));
                    // Background centroid scan only on this real RMB press.
                    orbitPivotSource_=picked?*picked:orbit_.choosePivot(
                        *visual,std::nullopt,currentGeometryCentroid(),forward);
                    std::cout<<"[view] RMB orbit start pivot="
                             <<(picked?"surface":"screen-parallel centroid")
                             <<" ("<<orbitPivotSource_.x<<","
                             <<orbitPivotSource_.y<<","
                             <<orbitPivotSource_.z<<")\n";
                }else {
                    cameraGestures_.cancel();
                    return;
                }
            }else if(gesture.mode==ViewportCameraMode::Dolly) {
                std::cout<<"[view] Ctrl+RMB view-direction dolly start\n";
            }else {
                std::cout<<"[view] "<<(right?"Alt+RMB":"Ctrl+LMB")
                         <<" view-relative world pan start\n";
            }
            orbitCursor_.begin(x,y,double(ww),double(wh));
            orbitDragging_=true;
        }else if(orbitDragging_&&gesture.mode!=ViewportCameraMode::Inactive) {
            const auto motion=orbitCursor_.sample(x,y,double(ww),double(wh));
            if(ww>0&&wh>0&&fw>0&&fh>0) {
                const double dx=motion.dx*double(fw)/ww;
                const double dy=motion.dy*double(fh)/wh;
                const double span=double(std::min(fw,fh));
                if(gesture.mode==ViewportCameraMode::Dolly||
                   gesture.mode==ViewportCameraMode::Strafe) {
                    const Vec3 eye=scaledCameraEye(double(fw),double(fh));
                    const Vec3 forward=normalized(
                        Vec3{}-baseCameraEye(double(fw),double(fh)));
                    if(gesture.mode==ViewportCameraMode::Dolly)
                        orbit_.dolly(forward,dx,span,length(eye));
                    else {
                        const Vec3 viewRight=normalized(cross(forward,{0,0,1}));
                        const Vec3 viewUp=cross(viewRight,forward);
                        orbit_.strafe(viewRight,viewUp,dx,
                                      dy,double(fh),length(eye));
                    }
                }else
                    orbit_.drag(orbitPivotSource_,dx,dy,span);
            }
            if(motion.warp&&glfwGetWindowAttrib(window,GLFW_FOCUSED)) {
                // Window-client coordinates; never interpret a synthetic
                // GLFW edge warp as a giant orbit, strafe, or dolly delta.
                (void)glfwGetError(nullptr);
                glfwSetCursorPos(window,motion.warpX,motion.warpY);
                const int error=glfwGetError(nullptr);
                if(error==GLFW_NO_ERROR)
                    orbitCursor_.didWarp(motion.warpX,motion.warpY);
                else {
                    orbitCursor_.disableWarp();
                    std::cout<<"[view] cursor wrap unavailable on this "
                                "windowing platform; camera gesture "
                                "continues without wrapping\n";
                }
            }
        }
    }

    std::optional<Vec3> pickCursor() const {
        const auto ray=cursorSourceRay();
        if(!ray)return std::nullopt;
        lastPickStats_={};
        return displayedIndex_?displayedIndex_->firstRayHit(*ray,&lastPickStats_):
               firstMeshHit(currentMesh(),*ray);
    }

    std::optional<RegionTopologyIndex::FaceRayHit> pickCursorSourceFace() const {
        if(!displayedIndex_)return std::nullopt;
        const auto ray=cursorSourceRay();
        if(!ray)return std::nullopt;
        return displayedIndex_->firstRayFaceHit(*ray,&lastPickStats_);
    }
    void updateMergeKeyboard() {
        const auto edges=mergeKeys_.sample(
            keyboard_.active(GLFW_KEY_M),
            keyboard_.active(GLFW_KEY_ENTER),
            keyboard_.active(GLFW_KEY_BACKSPACE),
            keyboard_.active(GLFW_KEY_A));
        const bool ready=!interaction_.canBegin||interaction_.canBegin();
        if(edges.toggle) {
            const auto result=mergeInput_.toggle(
                bool(interaction_.mergeSelected),ready,sculpting_);
            switch(result) {
            case DesktopMergeInput::Result::ModeOn:
            case DesktopMergeInput::Result::ModeOff:
                std::cout<<"[merge] mode="<<(mergeInput_.enabled()?"ON":"OFF")
                         <<" (M toggles, LMB selects FaceIds, fixture A nominates, Enter submits, Backspace clears)\n";
                break;
            case DesktopMergeInput::Result::Unavailable:
                std::cout<<"[merge] unavailable: launch with --merge-geometry to authorize sealed original-root worker\n";
                break;
            case DesktopMergeInput::Result::Busy:
                std::cout<<"[merge] toggle refused: worker busy or terminal\n";
                break;
            default:break;
            }
            refreshHud();
            updateTitle();
        }
        if(edges.nominate&&mergeInput_.enabled()) {
            const auto result=mergeInput_.nominateFixtureOriginals(
                displayedIndex_,interaction_.mergeFixtureOriginal,
                interaction_.mergeFixtureOriginals,ready);
            if(result==DesktopMergeInput::Result::Selected)
                std::cout<<"[merge] explicit A nominated "
                         <<mergeInput_.selectedCount()
                         <<" first-original FaceIds on exact fixture root\n";
            else
                std::cout<<"[merge] A nomination refused (no current authorized "
                           "fixture, stale root, invalid identity, or selection limit)\n";
            refreshHud();
            updateTitle();
        }
        if(edges.clear&&mergeInput_.enabled()) {
            mergeInput_.clear();
            refreshHud();
            updateTitle();
        }
        if(edges.submit&&mergeInput_.enabled()&&interaction_.mergeSelected) {
            auto result=mergeInput_.submit(
                displayedIndex_,bool(interaction_.mergeSelected),ready);
            if(result.result==DesktopMergeInput::Result::Queued) {
                const auto count=result.originals.size();
                // This exact root+FaceId pair is the bounded M47 worker
                // contract; no source scan occurs on the render thread.
                interaction_.mergeSelected(std::move(result.expected),
                                           std::move(result.originals));
                std::cout<<"[merge] queued "<<count
                         <<" explicitly nominated first-original FaceIds\n";
            }else if(result.result==DesktopMergeInput::Result::NoSelection)
                std::cout<<"[merge] refusal: no explicitly selected original FaceIds\n";
            else
                std::cout<<"[merge] refusal: busy or selected immutable root is stale\n";
            refreshHud();
            updateTitle();
        }
    }

    // Status lines use a separate window from the 750ms HUD counter, so
    // console output gives a copyable history of actual presented frames.
    void reportAfterStamp(std::size_t count) {
        logStamps_+=count;
        const double now=glfwGetTime();
        const double elapsed=now-lastConsoleTime_;
        if(elapsed<0.25 || logStamps_==0)return;
        std::ostringstream os;
        os<<std::fixed<<std::setprecision(3)
          <<"[sculpt] t="<<(now-viewportStartTime_)
          <<" s interval="<<elapsed
          <<" s FPS="<<double(logFrames_)/elapsed
          <<" stamps_per_sec="<<double(logStamps_)/elapsed
          <<" input_steps_total="<<(interaction_.totalQueuedSteps
                                    ? interaction_.totalQueuedSteps():0)
          <<" stamps_total="<<(interaction_.completedStamps
                                     ? interaction_.completedStamps():0)
          <<" pending_steps="<<(interaction_.pendingSamples
                                  ? interaction_.pendingSamples():0)
          <<" pending_segments="<<(interaction_.pendingSegments
                                  ? interaction_.pendingSegments():0)
          <<" input_commands="<<(interaction_.inputCommands
                                  ? interaction_.inputCommands():0)
          <<" enqueue_or_stamp_ms="<<lastStampCallbackMs_
          <<" picking_ms="<<lastPickMs_
          <<" picking_boxes="<<lastPickStats_.boxesTested
          <<" picking_partitions="<<lastPickStats_.partitionsTested
          <<" picking_triangles="<<lastPickStats_.trianglesTested
          <<" mesh_upload_ms="<<lastUploadMs_
          <<" dirty_partitions="<<lastDirtyPartitions_
          <<" dirty_faces="<<lastDirtyFaces_
          <<" dirty_upload_bytes="<<lastDirtyBytes_
          <<" gpu_resident_bytes="<<gpuResidentBytes_
          <<" gpu_peak_bytes="<<gpuPeakResidentBytes_
          <<" gpu_pending_retirement_bytes="<<gpuRetiredBytes_
          <<" gpu_freed_bytes="<<gpuFreedBytes_
          <<" gpu_fence_wait_ms="<<lastGpuFenceWaitMs_
          <<" cpu_roots_live="<<RegionTopologyIndex::liveRevisionCount()
          <<" cpu_roots_peak="<<RegionTopologyIndex::peakLiveRevisionCount()
          <<" retire_queue_roots="<<retiredRevisions_.statistics().queuedRoots
          <<" retire_queue_peak="<<retiredRevisions_.statistics().peakQueuedRoots
          <<" retire_backpressure="<<retiredRevisions_.statistics().backpressureWaits
          <<" last_draw_ms="<<lastDrawMs_;
        if(interaction_.lastStampDetails)os<<" "<<interaction_.lastStampDetails();
        std::cout<<os.str()<<'\n'<<std::flush;
        lastConsoleTime_=now;
        logFrames_=logStamps_=0;
    }

    void logInputEvent(const char* event, const char* result,
                       double strokeTime, bool picked,
                       std::size_t newSteps=0,
                       std::size_t pendingBefore=0,
                       std::size_t segmentsBefore=0) {
        if(!interaction_.verboseInput)return;
        double mouseX=0,mouseY=0;
        glfwGetCursorPos(window,&mouseX,&mouseY);
        int ww=0,wh=0,fw=0,fh=0;
        glfwGetWindowSize(window,&ww,&wh);
        glfwGetFramebufferSize(window,&fw,&fh);
        if(ww>0&&wh>0) {
            mouseX*=double(fw)/ww;
            mouseY*=double(fh)/wh;
        }
        const double now=glfwGetTime();
        std::ostringstream os;
        os<<std::fixed<<std::setprecision(4)
          <<"[input] event="<<event
          <<" seq="<<++inputEventSerial_
          <<" stroke="<<strokeSerial_
          <<" t="<<(now-viewportStartTime_)
          <<" stroke_t="<<strokeTime
          <<" result="<<result
          <<" mouse_px=("<<mouseX<<","<<mouseY<<")"
          <<" hit="<<(picked?1:0);
        if(sculpting_ || waitingForDrain_) {
            os<<" center=("<<lastCursorHit_.x<<","
                              <<lastCursorHit_.y<<","
                              <<lastCursorHit_.z<<")";
        }
        os<<" new_steps="<<newSteps
          <<" input_steps_total="<<(interaction_.totalQueuedSteps
                                        ? interaction_.totalQueuedSteps():0)
          <<" pending_before="<<pendingBefore
          <<" pending_after="<<(interaction_.pendingSamples
                                    ? interaction_.pendingSamples():0)
          <<" segments_before="<<segmentsBefore
          <<" segments_after="<<(interaction_.pendingSegments
                                     ? interaction_.pendingSegments():0)
          <<" rendered_stamps="<<(interaction_.completedStamps
                                      ? interaction_.completedStamps():0)
          <<" radius="<<interaction_.radius;
        if(std::strcmp(event,"drained")==0)
            os<<" drain_delay_s="<<(now-mouseReleaseTime_);
        std::cout<<os.str()<<'\n'<<std::flush;
    }

    // Pixel coordinates are framebuffer-scaled like the radius slider.
    bool updatePaintPanel(double x,double y,bool down,bool wasDown) {
        if(!paintMode_||!interaction_.paint)return false;
        const PaintPanelLayout panel;
        if(!down) {
            const bool wasDragging=paintPanelDragging_;
            paintPanelDragging_=false;
            paintPanelControl_=-1;
            return wasDragging;
        }
        if(down&&!wasDown&&x>=panel.left&&
           x<=panel.left+panel.width&&y>=panel.top&&
           y<=panel.top+panel.height) {
            paintPanelDragging_=true;
            const double dx=x-panel.wheelX,dy=y-panel.wheelY;
            if(dx*dx+dy*dy<=panel.radius*panel.radius)
                paintPanelControl_=0;
            else if(y>=panel.modeY-6&&y<=panel.modeY+20)
                paintPanelControl_=6;
            else {
                paintPanelControl_=-1;
                for(int i=0;i<5;++i)
                    if(std::abs(y-(panel.sliderY+i*panel.spacing))<=13)
                        paintPanelControl_=i+1;
            }
        }
        if(!paintPanelDragging_)return false;
        if(paintPanelControl_==6) {
            if(x<192)paintControls_.channels=PaintChannels::RGB;
            else if(x<276)paintControls_.channels=PaintChannels::HSV;
            else paintControls_.channels=PaintChannels::HSL;
        }else if(paintPanelControl_==0) {
            constexpr double pi=3.14159265358979323846;
            const double dx=x-panel.wheelX,dy=panel.wheelY-y;
            const double hue=std::atan2(dy,dx)/(2*pi);
            const double saturation=std::clamp(
                std::hypot(dx,dy)/panel.radius,0.,1.);
            paintControls_.setWheel(hue,saturation);
        }else if(paintPanelControl_>=1&&paintPanelControl_<=5) {
            const double value=std::clamp((x-panel.sliderX)/
                double(panel.sliderWidth),0.,1.);
            const auto control=paintPanelControl_-1;
            if(control<3)paintControls_.setChannel(unsigned(control),value);
            else if(control==3)paintControls_.setAlpha(value);
            else paintControls_.setHardness(value);
        }
        refreshHud();
        return true;
    }

    void updateMouseSculpt() {
        if(scaleConsumedMouse_) {
            mouseWasDown_=glfwGetMouseButton(
                window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
            return;
        }
        if(!interaction_.begin)return;
        // A camera/inspection/merge transition cannot strand a paint
        // history transaction when input handling takes an early return.
        if(painting_&&(inspectionFrozen_||orbitDragging_||
                       mergeInput_.enabled()||!paintMode_)) {
            if(interaction_.paintEnd)interaction_.paintEnd();
            painting_=false;
            lastPaintCursor_.reset();
        }
        if(inspectionFrozen_) {
            // Freeze is detected asynchronously; still close an active
            // stroke so the worker drains queued 4D input in FIFO order.
            if(sculpting_&&interaction_.end) {
                interaction_.end(lastCursorHit_,
                    std::max(0.0,glfwGetTime()-mouseStartTime_));
                sculpting_=false;
            }
            mouseWasDown_=glfwGetMouseButton(
                window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
            return;
        }
        if(orbitDragging_) {
            // View-only RMB, Alt+RMB, and Ctrl+LMB never sculpt
            // or nominate Merge faces.
            mouseWasDown_=glfwGetMouseButton(
                window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
            return;
        }
        const bool down=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        if(mergeInput_.enabled()) {
            if(down&&!mouseWasDown_) {
                const bool ready=!interaction_.canBegin||interaction_.canBegin();
                std::optional<FaceId> face;
                if(ready) {
                    if(const auto picked=pickCursorSourceFace())
                        face=picked->face;
                }
                const auto result=mergeInput_.pick(displayedIndex_,face,ready);
                switch(result) {
                case DesktopMergeInput::Result::Busy:
                    std::cout<<"[merge] pick refused: worker busy or terminal\n";
                    break;
                case DesktopMergeInput::Result::Selected:
                    std::cout<<"[merge] select immutable FaceId "<<*face
                             <<" total="<<mergeInput_.selectedCount()<<"\n";
                    break;
                case DesktopMergeInput::Result::Full:
                    std::cout<<"[merge] selection limit 32; submit or clear\n";
                    break;
                case DesktopMergeInput::Result::NoHit:
                case DesktopMergeInput::Result::InvalidFace:
                    std::cout<<"[merge] no indexed surface face under pointer\n";
                    break;
                default:break;
                }
                refreshHud();
                updateTitle();
            }
            mouseWasDown_=down;
            return; // Never accidentally stamp while selecting Merge source.
        }
        if(interaction_.setRadius) {
            double cx=0,cy=0;
            glfwGetCursorPos(window,&cx,&cy);
            int ww=0,wh=0,fw=0,fh=0;
            glfwGetWindowSize(window,&ww,&wh);
            glfwGetFramebufferSize(window,&fw,&fh);
            if(ww>0&&wh>0&&fw>0&&fh>0) {
                const double x=cx*double(fw)/ww;
                const double y=cy*double(fh)/wh;
                const auto layout=radiusSliderLayout(fh);
                if(down&&!mouseWasDown_ &&
                   x>=layout.left-8 && x<=layout.left+58 &&
                   y>=layout.top-12 && y<=layout.bottom+12)
                    radiusDragging_=true;
                if(radiusDragging_&&down) {
                    const double fraction=(y-layout.top)/
                        double(layout.bottom-layout.top);
                    const double radius=interaction_.radiusRange.fromSliderFraction(fraction);
                    if(std::abs(radius-interaction_.radius)>1e-8) {
                        interaction_.radius=radius;
                        interaction_.setRadius(radius);
                        refreshHud();
                        updateTitle();
                    }
                }
            }
        }
        if(radiusDragging_) {
            if(down&&!mouseWasDown_)logInputEvent("down","slider",0,false);
            if(!down&&mouseWasDown_)logInputEvent("up","slider",0,false);
            if(!down)radiusDragging_=false;
            mouseWasDown_=down;
            return;
        }
        if(paintMode_&&interaction_.paint) {
            double cx=0,cy=0;
            glfwGetCursorPos(window,&cx,&cy);
            int ww=0,wh=0,fw=0,fh=0;
            glfwGetWindowSize(window,&ww,&wh);
            glfwGetFramebufferSize(window,&fw,&fh);
            if(ww>0&&wh>0&&fw>0&&fh>0) {
                cx*=double(fw)/ww;
                cy*=double(fh)/wh;
            }
            if(updatePaintPanel(cx,cy,down,mouseWasDown_)) {
                if(painting_&&interaction_.paintEnd)interaction_.paintEnd();
                mouseWasDown_=down;
                painting_=false;
                lastPaintCursor_.reset();
                return;
            }
            if(down&&(!interaction_.canBegin||interaction_.canBegin())&&
               (!lastPaintCursor_||
                std::hypot(cx-lastPaintCursor_->first,
                           cy-lastPaintCursor_->second)>=1.0)) {
                if(const auto hit=pickCursorSourceFace()) {
                    const auto face=displayedIndex_->face(hit->face).vertices;
                    const auto a=displayedIndex_->vertex(face[0]).position;
                    const auto b=displayedIndex_->vertex(face[1]).position;
                    const auto c=displayedIndex_->vertex(face[2]).position;
                    const auto normal=cross(b-a,c-a);
                    if(length(normal)>1e-12) {
                        if(!painting_&&interaction_.paintBegin)
                            interaction_.paintBegin();
                        interaction_.paint(PaintDab{
                            hit->point,normalized(normal),
                            interaction_.radius,
                            interaction_.paintNominalEdgeLength,
                            paintControls_.rgba,paintControls_.hardness,
                            hit->face});
                        painting_=true;
                        lastPaintCursor_=std::pair{cx,cy};
                    }
                }
            }
            if(!down) {
                if(painting_&&interaction_.paintEnd)interaction_.paintEnd();
                painting_=false;
                lastPaintCursor_.reset();
            }
            mouseWasDown_=down;
            return; // Painting never enqueues Inflate or remesh geometry.
        }
        if(down&&!mouseWasDown_ &&
           (!interaction_.canBegin || interaction_.canBegin())) {
            const double pickStart=glfwGetTime();
            const auto picked=pickCursor();
            lastPickMs_=(glfwGetTime()-pickStart)*1000.0;
            if(picked) {
                ++strokeSerial_;
                waitingForDrain_=false;
                sculpting_=true;
                lastCursorHit_=*picked;
                mouseStartTime_=glfwGetTime();
                const double stampStart=glfwGetTime();
                const bool inverted=
                    keyboard_.down(GLFW_KEY_LEFT_ALT)||
                    keyboard_.down(GLFW_KEY_RIGHT_ALT);
                interaction_.begin(*picked,inverted);
                lastStampCallbackMs_=(glfwGetTime()-stampStart)*1000.0;
                lastConsoleTime_=glfwGetTime();
                logFrames_=0;
                logStamps_=0;
                if(!interaction_.takeSnapshot) {
                    // Legacy synchronous path stamps on mouse-down.
                    ++stampsThisInterval_;
                    stampedThisFrame_=true;
                    refreshMesh();
                    reportAfterStamp(1);
                }
                updateTitle();
                logInputEvent("down","started",0,true);
            } else {
                logInputEvent("down","miss",0,false);
            }
        } else if(down&&!mouseWasDown_) {
            logInputEvent("down","busy",0,false);
        } else if(down&&sculpting_) {
            // Raycast AGAINST the mesh modified on the previous tick.
            const double pickStart=glfwGetTime();
            const auto picked=pickCursor();
            if(picked)lastCursorHit_=*picked;
            lastPickMs_=(glfwGetTime()-pickStart)*1000.0;
            const double elapsed=std::max(0.0,glfwGetTime()-mouseStartTime_);
            const auto stepsBefore=interaction_.totalQueuedSteps
                ? interaction_.totalQueuedSteps():0;
            const auto pendingBefore=interaction_.pendingSamples
                ? interaction_.pendingSamples():0;
            const auto segmentsBefore=interaction_.pendingSegments
                ? interaction_.pendingSegments():0;
            // Store the exact 4D input segment without stamping here.
            // The frame loop consumes at most one queued Step after polling.
            interaction_.update(lastCursorHit_,elapsed);
            const auto stepsAfter=interaction_.totalQueuedSteps
                ? interaction_.totalQueuedSteps():stepsBefore;
            if(stepsAfter>stepsBefore)
                logInputEvent("move",picked?"hit":"held_center",elapsed,
                              bool(picked),stepsAfter-stepsBefore,
                              pendingBefore,segmentsBefore);
        } else if(!down&&mouseWasDown_&&sculpting_) {
            const double pickStart=glfwGetTime();
            const auto picked=pickCursor();
            if(picked)lastCursorHit_=*picked;
            lastPickMs_=(glfwGetTime()-pickStart)*1000.0;
            const double elapsed=std::max(0.0,glfwGetTime()-mouseStartTime_);
            const auto stepsBefore=interaction_.totalQueuedSteps
                ? interaction_.totalQueuedSteps():0;
            const auto pendingBefore=interaction_.pendingSamples
                ? interaction_.pendingSamples():0;
            const auto segmentsBefore=interaction_.pendingSegments
                ? interaction_.pendingSegments():0;
            // Mouse-up closes the input path; older queued steps still
            // need to be drawn, including after the mouse is released.
            interaction_.end(lastCursorHit_,elapsed);
            mouseReleaseTime_=glfwGetTime();
            waitingForDrain_=true;
            const auto stepsAfter=interaction_.totalQueuedSteps
                ? interaction_.totalQueuedSteps():stepsBefore;
            logInputEvent("up","queued",elapsed,bool(picked),
                          stepsAfter-stepsBefore,pendingBefore,segmentsBefore);
            sculpting_=false;
            updateTitle();
        } else if(!down&&mouseWasDown_) {
            logInputEvent("up","ignored",0,false);
        }
        mouseWasDown_=down;
    }

    void clearPaintPreview() {
        paintPreviewFaces_.clear();
        paintPreviewIndex_.reset();
        paintPreviewRay_.reset();
        paintPreviewRadius_=-1;
        // Keep the old allocation reusable; zero draw count does not need
        // a GPU fence and never writes/retires the buffer while in flight.
        paintPreviewSolid.vertexCount=0;
    }
    void updatePaintPreview() {
        if(!paintMode_||!interaction_.paint||!displayedIndex_||
           painting_||sculpting_||orbitDragging_||radiusDragging_||
           scaleDragging_||
           paintPanelDragging_||inspectionFrozen_||
           mergeInput_.enabled()||
           glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS||
           glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS) {
            clearPaintPreview();
            return;
        }
        int ww=0,wh=0,fw=0,fh=0;
        glfwGetWindowSize(window,&ww,&wh);
        glfwGetFramebufferSize(window,&fw,&fh);
        if(ww<=0||wh<=0||fw<=0||fh<=0) {
            clearPaintPreview();
            return;
        }
        double cx=0,cy=0;
        glfwGetCursorPos(window,&cx,&cy);
        const double x=cx*double(fw)/ww,y=cy*double(fh)/wh;
        const PaintPanelLayout panel;
        const auto slider=radiusSliderLayout(fh);
        const auto global=globalScaleSliderLayout(fw,fh);
        if(x<0||x>=fw||y<0||y>=fh||global.contains(x,y)||
           (x>=panel.left&&x<=panel.left+panel.width&&
            y>=panel.top&&y<=panel.top+panel.height)||
           (x>=slider.left-8&&x<=slider.left+58&&
            y>=slider.top-12&&y<=slider.bottom+12)) {
            clearPaintPreview();
            return;
        }
        const auto ray=cursorSourceRay();
        if(!ray) {clearPaintPreview();return;}
        const auto same=[&](Vec3 a,Vec3 b) {
            return a.x==b.x&&a.y==b.y&&a.z==b.z;
        };
        const bool newSourceRoot=paintPreviewIndex_!=displayedIndex_;
        if(!newSourceRoot&&paintPreviewRay_&&
           paintPreviewRadius_==interaction_.radius&&
           same(ray->origin,paintPreviewRay_->origin)&&
           same(ray->direction,paintPreviewRay_->direction))return;
        // Cache a no-hit or fail-closed query, too. A stationary cursor
        // should not raycast every frame when the scene is unchanged.
        paintPreviewIndex_=displayedIndex_;
        paintPreviewRay_=ray;
        paintPreviewRadius_=interaction_.radius;
        std::vector<FaceId> faces;
        try {
            const auto hit=displayedIndex_->firstRayFaceHit(*ray);
            if(hit) {
                const auto triangle=displayedIndex_->face(hit->face).vertices;
                const Vec3 a=displayedIndex_->vertex(triangle[0]).position;
                const Vec3 b=displayedIndex_->vertex(triangle[1]).position;
                const Vec3 c=displayedIndex_->vertex(triangle[2]).position;
                const Vec3 normal=cross(b-a,c-a);
                if(length(normal)>1e-12) {
                    const PaintDab dab{hit->point,normalized(normal),
                        interaction_.radius,interaction_.paintNominalEdgeLength,
                        paintControls_.rgba,paintControls_.hardness,hit->face};
                    faces=collectPaintPreview(displayedIndex_,
                        paintDabsWithMirror(dab,interaction_.paintSymmetric))
                        .affectedFaces;
                }
            }
        }catch(const std::length_error&) {
            // Budget exhaustion cannot expose a partial/misleading brush.
            faces.clear();
        }
        // Stable FaceIds can survive a sculpt/Undo geometry change.
        // Rebuild positions even when their selected IDs are unchanged.
        if(!newSourceRoot&&faces==paintPreviewFaces_)return;
        paintPreviewFaces_=std::move(faces);
        if(paintPreviewFaces_.empty()) {
            paintPreviewSolid.vertexCount=0;
            return;
        }
        std::vector<Vertex> geometry;
        geometry.reserve(paintPreviewFaces_.size()*3);
        for(const auto faceId:paintPreviewFaces_) {
            const auto tri=displayedIndex_->face(faceId).vertices;
            const Vec3 a=displayedIndex_->vertex(tri[0]).position;
            const Vec3 b=displayedIndex_->vertex(tri[1]).position;
            const Vec3 c=displayedIndex_->vertex(tri[2]).position;
            const Vec3 rawNormal=cross(b-a,c-a);
            const Vec3 normal=length(rawNormal)>1e-20?
                normalized(rawNormal):Vec3{};
            for(const auto p:{a,b,c})
                geometry.push_back(makeVertex(p-orbit_.center(),normal,15.5f));
        }
        // Reuse the existing single-flight Vulkan retirement contract:
        // the old buffer must not be mapped or replaced while rendered.
        check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),
              "Wait before updating transient paint preview");
        drainGpuRetirements();
        replaceBuffer(paintPreviewSolid,geometry);
    }

    void pumpMouseStroke() {
        if(interaction_.takeSnapshot) {
            // The worker never shares mutable mesh memory with the renderer.
            // Intermediate completed states coalesce into the newest
            // snapshot; every brush stamp itself has still been applied.
            if(auto snapshot=interaction_.takeSnapshot()) {
                if(snapshot->index!=displayedIndex_)
                    mergeInput_.revisionChanged(snapshot->index);
                const auto previous=displayedStamps_;
                displayedStamps_=snapshot->completedStamps;
                refreshPartitions(std::move(snapshot->index),
                                  snapshot->dirtyPartitions);
                if(oneShotAwaitingDisplay_&&
                   displayedStamps_>=oneShotRequestedCount_) {
                    oneShotAwaitingDisplay_=false;
                    std::cout<<"[sculpt-step] rendered_stamp="<<displayedStamps_
                             <<" exact_one_step=1"
                             <<" pending="<<(interaction_.pendingSamples?
                                              interaction_.pendingSamples():0)
                             <<"\n"<<std::flush;
                }
                if(snapshot->inspectionFrozen) {
                    inspectionFrozen_=true;
                    inspectionIncomplete_=snapshot->inspectionOverlay.empty();
                    inspectionPreview_=snapshot->inspectionPreview;
                    inspectionCutRequested_=snapshot->inspectionCutRequested;
                    inspectionCutSucceeded_=snapshot->inspectionCutSucceeded;
                    inspectionZipperRequested_=snapshot->inspectionZipperRequested;
                    inspectionZipperSucceeded_=snapshot->inspectionZipperSucceeded;
                    inspectionCapRequested_=snapshot->inspectionCapRequested;
                    inspectionAutoRequested_=snapshot->inspectionAutoRequested;
                    inspectionAutoSucceeded_=snapshot->inspectionAutoSucceeded;
                    inspectionQualityWarningPreview_=snapshot->inspectionQualityWarningPreview;
                    classifierChoices_=snapshot->classifierChoices;
                    classifierComparable_=snapshot->classifierComparable;
                    classifiersAgree_=snapshot->classifiersAgree;
                    activeClassifier_=snapshot->activeClassifier;
                    inspectionCapSucceeded_=snapshot->inspectionCapSucceeded;
                    inspectionStepReplaceMesh_=snapshot->inspectionStepReplaceMesh;
                    inspectionStepFinished_=snapshot->inspectionStepFinished;
                    inspectionStepCancelled_=snapshot->inspectionStepCancelled;
                    inspectionStepError_=snapshot->inspectionStepError;
                    if(repairStepLines_!=snapshot->repairStepLines) {
                        repairStepLines_=std::move(snapshot->repairStepLines);
                        repairStepScroll_=0;
                    }
                    // refreshPartitions has waited for the previous GPU
                    // submission fence, so this buffer may be safely staged.
                    auto localOverlay=snapshot->inspectionOverlay;
                    for(auto& vertex:localOverlay) {
                        vertex.px-=float(orbit_.center().x);
                        vertex.py-=float(orbit_.center().y);
                        vertex.pz-=float(orbit_.center().z);
                    }
                    replaceBuffer(inspectionSolid,localOverlay);
                    // This mode is fullscreen on Linux; the window title is
                    // not visible. Update the persistent HUD immediately so
                    // refusal cannot masquerade as a successful orange cut.
                    refreshHud();
                    std::cout<<"[repair-r01] FROZEN: viewing an unpublished "
                                "candidate; LMB/undo/save disabled; orbit, "
                                "W/G and Escape remain available\n"<<std::flush;
                }
                const auto added=displayedStamps_>=previous
                    ?displayedStamps_-previous:0;
                stampsThisInterval_+=added;
                updateTitle();
                if(added)reportAfterStamp(added);
            }
        } else if(interaction_.pump && !stampedThisFrame_) {
            const double started=glfwGetTime();
            const bool stamped=interaction_.pump();
            if(stamped) {
                lastStampCallbackMs_=(glfwGetTime()-started)*1000.0;
                ++stampsThisInterval_;
                refreshMesh();
                updateTitle();
                reportAfterStamp(1);
            }
        }
        if(interaction_.mergeStatus) {
            const auto requests=interaction_.mergeStatus();
            if(!requests.empty()&&requests!=lastMergeStatus_) {
                lastMergeStatus_=requests;
                std::cout<<"[merge] "<<requests<<"\n";
                updateTitle();
                refreshHud();
            }
        }
        // M06: the *separate* OFF-mode constraint policy reports
        // recoverable refusal even when no geometry/stamp was published.
        // No Merge or protected original-source UI selection is implied.
        if(interaction_.collisionStatus) {
            const auto value=interaction_.collisionStatus();
            const auto now=glfwGetTime();
            const bool urgent=value.find(" REFUSED ")!=std::string::npos||
                              value.find(" TERMINAL ")!=std::string::npos||
                              lastCollisionStatus_.find(" REFUSED ")!=std::string::npos;
            // Do not turn a smooth high-frequency stroke into a console
            // line per Step. Never throttle a refusal or the first
            // accepted Step after it; periodic detailed [sculpt] remains.
            if(!value.empty()&&value!=lastCollisionStatus_&&
               (urgent||lastCollisionReportTime_<0||
                now-lastCollisionReportTime_>=1.0)) {
                lastCollisionStatus_=value;
                lastCollisionReportTime_=now;
                std::cout<<(value.rfind("AUTO ",0)==0?
                               "[repair-r07] ":"[collision] ")
                         <<value<<"\n"<<std::flush;
                updateTitle();
            }
        }
        if(waitingForDrain_ && interaction_.canBegin && interaction_.canBegin()) {
            logInputEvent("drained","finished",
                          mouseReleaseTime_-mouseStartTime_,false);
            waitingForDrain_=false;
            updateTitle();
        }
    }

    void sampleFrameRates() {
        const double now=glfwGetTime();
        const double elapsed=now-statsBegin_;
        if(elapsed<0.75)return;
        measuredFps_=double(framesThisInterval_)/elapsed;
        measuredStampRate_=double(stampsThisInterval_)/elapsed;
        framesThisInterval_=stampsThisInterval_=0;
        statsBegin_=now;
        refreshHud();
    }

    std::size_t countInvertedFaces() const {
        std::size_t count = 0;
        const auto& drawingMesh=currentMesh();
        for (const auto& t : drawingMesh.triangles) {
            const Vec3 a=drawingMesh.vertices[t.a], b=drawingMesh.vertices[t.b],
                       c=drawingMesh.vertices[t.c];
            // In this sphere demonstration the center is at the origin.
            // This reports radial winding reversals, not all intersections.
            if (dot(cross(b-a,c-a),a+b+c) <= 1e-13) ++count;
        }
        return count;
    }

    void updateTitle() {
        std::string title = "Mesher";
        if (interaction_.begin) {
            if(interaction_.caption)title+=" - "+interaction_.caption();
            if(inspectionFrozen_)
                title+=interaction_.repairStepMode&&inspectionStepCancelled_?
                    " | R06 PRIVATE STEP CANCELLED":
                    interaction_.repairStepMode&&inspectionStepError_?
                    " | R06 PRIVATE STEP ERROR":
                    interaction_.repairStepMode&&!inspectionStepFinished_?
                    " | R06 PRIVATE STEP DEBUG":
                    inspectionAutoRequested_?
                    (inspectionAutoSucceeded_?" | R06 AUTO CLOSED (FROZEN PRIVATE)":
                     " | R06 AUTO REFUSED (FROZEN)"):
                    inspectionCapRequested_?
                    (inspectionCapSucceeded_?" | R05 CAPS (FROZEN PRIVATE)":
                     " | R05 CAP REFUSED (FROZEN)"):
                    (inspectionZipperRequested_?
                    (inspectionZipperSucceeded_?" | R04 ZIPPER (FROZEN PRIVATE)":
                     " | R04 ZIPPER REFUSED (FROZEN)"):
                    (inspectionCutRequested_?
                    (inspectionCutSucceeded_?" | R03 PRIVATE CUT (FROZEN)":
                     " | R03 EXCISION REFUSED (FROZEN)"):
                    (inspectionPreview_?
                    " | R02 EXCISION PREVIEW (UNPUBLISHED)":
                    (inspectionIncomplete_?
                    " | R01 QUERY INCOMPLETE (FROZEN)":
                    " | R01 CONTACT (FROZEN CANDIDATE)"))));
            if(mergeInput_.enabled())
                title+=" | Merge ON: LMB select; Enter submit ("+
                       std::to_string(mergeInput_.selectedCount())+" faces)";
            else title+=paintMode_ ? " | Paint RGBA (B toggle)" :
                sculpting_ ? " | Sculpting" :
                interaction_.mergeSelected?
                " | Merge OFF: LMB Inflate; M toggle":" | LMB: Inflate";
            if(interaction_.mergeStatus)title+=" | "+interaction_.mergeStatus();
            // Live AUTO reports an immediate visible acceptance/refusal
            // state; the console retains the full diagnostic explanation.
            if(interaction_.collisionStatus&&
               lastCollisionStatus_.rfind("AUTO ",0)==0) {
                auto summary=lastCollisionStatus_;
                const auto refusal=summary.find(" REFUSED ");
                if(refusal!=std::string::npos)
                    summary=summary.substr(0,refusal)+" REFUSED";
                const auto terminal=summary.find(" TERMINAL ");
                if(terminal!=std::string::npos)
                    summary=summary.substr(0,terminal)+" TERMINAL";
                title+=" | "+summary;
            }
            title+=" | Esc: exit";
        } else if (animation_.advance) {
            if (animation_.caption) title += " - " + animation_.caption();
            const auto inverted = countInvertedFaces();
            title += " | inverted faces: " + std::to_string(inverted);
            title += complete_ ? " | complete" : paused_ ? " | paused" : " | playing";
            title += " | Space: pause  Right: step  R: replay  Esc: quit";
            if (inverted != lastInvertedCount_) {
                std::cout << "Inverted faces: " << inverted;
                if (animation_.caption) std::cout << " at " << animation_.caption();
                std::cout << '\n' << std::flush;
                lastInvertedCount_ = inverted;
            }
        }
        title+=wireVisible_?" | W: hide edges":" | W: show edges";
        title+=floorVisible_?" | G: hide grid":" | G: show grid";
        glfwSetWindowTitle(window, title.c_str());
    }

    void advanceOneStamp() {
        if (!animation_.advance || complete_) return;
        if (animation_.advance()) {
            ++stampsThisInterval_;
            refreshMesh();
        } else {
            complete_ = true;
            paused_ = true;
        }
        updateTitle();
    }

    bool justPressed(int key, bool& wasDown) {
        // Consume the callback edge, including a quick tap entirely
        // between successive frames. A key-repeat is not another edge.
        const bool pressed=keyboard_.takePress(key);
        wasDown=keyboard_.down(key);
        return pressed;
    }

    VkFormat chooseDepthFormat()const {
        for(auto candidate:{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D24_UNORM_S8_UINT}) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physical,candidate,&props);
            if(props.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)return candidate;
        }
        throw std::runtime_error("GPU does not support a depth attachment format");
    }
    VkImageView imageView(VkImage image,VkFormat format,VkImageAspectFlags aspect) {
        VkImageViewCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image=image;
        info.viewType=VK_IMAGE_VIEW_TYPE_2D;
        info.format=format;
        info.subresourceRange.aspectMask=aspect;
        info.subresourceRange.levelCount=1;
        info.subresourceRange.layerCount=1;
        VkImageView view=VK_NULL_HANDLE;
        check(vkCreateImageView(device,&info,nullptr,&view),"Create image view");
        return view;
    }
    void createDepth() {
        depthFormat=chooseDepthFormat();
        VkImageCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType=VK_IMAGE_TYPE_2D;
        info.extent={extent.width,extent.height,1};
        info.mipLevels=1;info.arrayLayers=1;
        info.format=depthFormat;info.tiling=VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        info.samples=VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device,&info,nullptr,&depthImage),"Create depth image");
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device,depthImage,&req);
        VkMemoryAllocateInfo alloc{}; alloc.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize=req.size;
        alloc.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device,&alloc,nullptr,&depthMemory),"Allocate depth memory");
        check(vkBindImageMemory(device,depthImage,depthMemory,0),"Bind depth memory");
        depthView=imageView(depthImage,depthFormat,VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    void createNewPreviewAtlasTexture() {
        // Same converted PNG asset convention as Icon/Splash. No PNG
        // decoder is invoked by the application at runtime.
        auto source=std::filesystem::path(MESHER_ASSET_DIR);
        const auto beside=mesherExecutableDirectory()/"images";
        if(std::filesystem::exists(beside/"New-Torus.rgba"))
            source=beside;
#ifdef __APPLE__
        const auto bundled=mesherExecutableDirectory()/"../Resources/images";
        if(std::filesystem::exists(bundled/"New-Torus.rgba"))
            source=bundled;
#endif
        newModelAtlas_=loadNewModelPreviewAtlas(source);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical,&properties);
        if(newModelAtlas_.width>properties.limits.maxImageDimension2D||
           newModelAtlas_.height>properties.limits.maxImageDimension2D)
            throw std::runtime_error("Preview atlas is larger than Vulkan image limit");
        VkFormatProperties formatFeatures{};
        vkGetPhysicalDeviceFormatProperties(
            physical,VK_FORMAT_R8G8B8A8_SRGB,&formatFeatures);
        if(!(formatFeatures.optimalTilingFeatures&
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            throw std::runtime_error("Vulkan device cannot sample preview RGBA");
        const VkDeviceSize bytes=newModelAtlas_.rgba.size();
        // One upload at initialization; all subsequent modal redraws use
        // six image vertices per preview, never per-pixel HUD geometry.
        VkBuffer staging=VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory=VK_NULL_HANDLE;
        VkBufferCreateInfo buffer{};buffer.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer.size=bytes;
        buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device,&buffer,nullptr,&staging),
              "Create preview image staging buffer");
        VkMemoryRequirements required{};
        vkGetBufferMemoryRequirements(device,staging,&required);
        VkMemoryAllocateInfo allocate{};allocate.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize=required.size;
        allocate.memoryTypeIndex=memoryType(required.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device,&allocate,nullptr,&stagingMemory),
              "Allocate preview image staging");
        check(vkBindBufferMemory(device,staging,stagingMemory,0),
              "Bind preview image staging");
        void* memory=nullptr;
        check(vkMapMemory(device,stagingMemory,0,bytes,0,&memory),
              "Map preview image staging");
        std::memcpy(memory,newModelAtlas_.rgba.data(),
                    newModelAtlas_.rgba.size());
        vkUnmapMemory(device,stagingMemory);

        VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType=VK_IMAGE_TYPE_2D;
        image.extent={newModelAtlas_.width,newModelAtlas_.height,1};
        image.mipLevels=1;image.arrayLayers=1;
        image.format=VK_FORMAT_R8G8B8A8_SRGB;
        image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        image.samples=VK_SAMPLE_COUNT_1_BIT;
        image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device,&image,nullptr,&newPreviewImage_),
              "Create preview atlas image");
        vkGetImageMemoryRequirements(device,newPreviewImage_,&required);
        allocate.allocationSize=required.size;
        allocate.memoryTypeIndex=memoryType(required.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device,&allocate,nullptr,&newPreviewMemory_),
              "Allocate preview atlas image");
        check(vkBindImageMemory(device,newPreviewImage_,newPreviewMemory_,0),
              "Bind preview atlas image");

        VkCommandPool pool=VK_NULL_HANDLE;
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex=graphicsFamily;
        poolInfo.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool),
              "Create temporary atlas upload command pool");
        VkCommandBuffer cmd=VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cmdInfo{};
        cmdInfo.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdInfo.commandPool=pool;
        cmdInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device,&cmdInfo,&cmd),
              "Allocate atlas upload commands");
        VkCommandBufferBeginInfo begin{};
        begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd,&begin),"Begin atlas upload");

        VkImageMemoryBarrier transition{};
        transition.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        transition.image=newPreviewImage_;
        transition.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        transition.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        transition.srcAccessMask=0;
        transition.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        transition.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        transition.subresourceRange.levelCount=1;
        transition.subresourceRange.layerCount=1;
        transition.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        transition.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,
            1,&transition);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount=1;
        copy.imageExtent={newModelAtlas_.width,newModelAtlas_.height,1};
        vkCmdCopyBufferToImage(cmd,staging,newPreviewImage_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        transition.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        transition.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transition.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        transition.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,
            1,&transition);
        check(vkEndCommandBuffer(cmd),"End atlas upload");
        VkSubmitInfo submit{};
        submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount=1;
        submit.pCommandBuffers=&cmd;
        check(vkQueueSubmit(graphicsQueue,1,&submit,VK_NULL_HANDLE),
              "Submit atlas upload");
        check(vkQueueWaitIdle(graphicsQueue),"Wait for atlas upload");
        vkDestroyCommandPool(device,pool,nullptr);
        vkDestroyBuffer(device,staging,nullptr);
        vkFreeMemory(device,stagingMemory,nullptr);

        newPreviewView_=imageView(
            newPreviewImage_,VK_FORMAT_R8G8B8A8_SRGB,VK_IMAGE_ASPECT_COLOR_BIT);
        VkSamplerCreateInfo sampler{};
        sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter=VK_FILTER_LINEAR;
        sampler.minFilter=VK_FILTER_LINEAR;
        sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.minLod=0;sampler.maxLod=0;
        check(vkCreateSampler(device,&sampler,nullptr,&newPreviewSampler_),
              "Create preview atlas sampler");
        // Only the three tile dimensions/UVs are needed after upload.
        std::vector<std::uint8_t>().swap(newModelAtlas_.rgba);
    }
    void createNewPreviewDescriptor() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding=0;
        binding.descriptorCount=1;
        binding.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layout{};
        layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount=1;layout.pBindings=&binding;
        check(vkCreateDescriptorSetLayout(device,&layout,nullptr,
              &newPreviewSetLayout_),"Create preview descriptor layout");
        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&poolSize;
        check(vkCreateDescriptorPool(device,&pool,nullptr,
                                    &newPreviewDescriptorPool_),
              "Create preview descriptor pool");
        VkDescriptorSetAllocateInfo allocate{};
        allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate.descriptorPool=newPreviewDescriptorPool_;
        allocate.descriptorSetCount=1;
        allocate.pSetLayouts=&newPreviewSetLayout_;
        check(vkAllocateDescriptorSets(device,&allocate,&newPreviewDescriptor_),
              "Allocate preview atlas descriptor");
        VkDescriptorImageInfo image{};
        image.sampler=newPreviewSampler_;
        image.imageView=newPreviewView_;
        image.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet=newPreviewDescriptor_;
        write.dstBinding=0;
        write.descriptorCount=1;
        write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo=&image;
        vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    }
    void destroyNewPreviewAtlasTexture() {
        if(newPreviewSampler_)
            vkDestroySampler(device,newPreviewSampler_,nullptr);
        if(newPreviewView_)
            vkDestroyImageView(device,newPreviewView_,nullptr);
        if(newPreviewImage_)
            vkDestroyImage(device,newPreviewImage_,nullptr);
        if(newPreviewMemory_)
            vkFreeMemory(device,newPreviewMemory_,nullptr);
        newPreviewSampler_=VK_NULL_HANDLE;
        newPreviewView_=VK_NULL_HANDLE;
        newPreviewImage_=VK_NULL_HANDLE;
        newPreviewMemory_=VK_NULL_HANDLE;
    }

    void createRenderPass() {
        VkAttachmentDescription color{};
        color.format=swapFormat;color.samples=VK_SAMPLE_COUNT_1_BIT;
        color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;color.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentDescription depth{};
        depth.format=depthFormat;depth.samples=VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;depth.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const std::array attachments{color,depth};
        VkAttachmentReference colorRef{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount=1;subpass.pColorAttachments=&colorRef;
        subpass.pDepthStencilAttachment=&depthRef;
        VkSubpassDependency dependency{};
        dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;
        dependency.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask=dependency.srcStageMask;
        dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount=static_cast<std::uint32_t>(attachments.size());
        info.pAttachments=attachments.data();
        info.subpassCount=1;info.pSubpasses=&subpass;
        info.dependencyCount=1;info.pDependencies=&dependency;
        check(vkCreateRenderPass(device,&info,nullptr,&renderPass),"Create render pass");
    }
    VkShaderModule shader(const char* name) {
        // Installed release archives must not depend on the absolute
        // build-directory path embedded by CMake.
        const auto filename=std::string(name)+".spv";
        auto shaderPath=mesherExecutableDirectory()/"shaders"/filename;
#ifdef __APPLE__
        const auto bundled=mesherExecutableDirectory()/
                           "../Resources/shaders"/filename;
        if(std::filesystem::exists(bundled))shaderPath=bundled;
#endif
        if(!std::filesystem::exists(shaderPath))
            shaderPath=std::filesystem::path(MESHER_SHADER_DIR)/filename;
        const auto bytes=loadBinary(shaderPath.string());
        VkShaderModuleCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize=bytes.size();
        // SPIR-V must be 4-byte aligned (vector<char> storage is suitably
        // aligned for the host allocator, but copy for guaranteed alignment).
        std::vector<std::uint32_t> words(bytes.size()/4);
        std::memcpy(words.data(),bytes.data(),bytes.size());
        info.pCode=words.data();
        VkShaderModule result=VK_NULL_HANDLE;
        check(vkCreateShaderModule(device,&info,nullptr,&result),"Create shader module");
        return result;
    }
    VkPipeline makePipeline(bool hudOverlay=false,bool paintOverlay=false) {
        auto vert=shader(hudOverlay?"hud.vert":"mesh.vert");
        auto frag=shader(hudOverlay?"hud.frag":"mesh.frag");
        VkPipelineShaderStageCreateInfo vs{}; vs.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage=VK_SHADER_STAGE_VERTEX_BIT;vs.module=vert;vs.pName="main";
        VkPipelineShaderStageCreateInfo fs{}; fs.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage=VK_SHADER_STAGE_FRAGMENT_BIT;fs.module=frag;fs.pName="main";
        std::array stages{vs,fs};
        VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription,4> attrs{};
        attrs[0]={0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,px)};
        attrs[1]={1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,nx)};
        attrs[2]={2,0,VK_FORMAT_R32_SFLOAT,offsetof(Vertex,material)};
        attrs[3]={3,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(Vertex,r)};
        VkPipelineVertexInputStateCreateInfo input{}; input.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;
        // Original seven-float mesh/HUD vertex binding.
        input.vertexAttributeDescriptionCount=static_cast<std::uint32_t>(attrs.size());
        input.pVertexAttributeDescriptions=attrs.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{}; assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        // Solid triangles are emitted as nonindexed three-vertex groups.
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{}; viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount=1;viewport.scissorCount=1;
        const std::array<VkDynamicState,2> dynamics{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{}; dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount=static_cast<std::uint32_t>(dynamics.size());
        dynamic.pDynamicStates=dynamics.data();
        VkPipelineRasterizationStateCreateInfo raster{}; raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode=VK_POLYGON_MODE_FILL;
        raster.lineWidth=1;
        raster.cullMode=VK_CULL_MODE_NONE;
        raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPipelineMultisampleStateCreateInfo multi{}; multi.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multi.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{}; depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable=hudOverlay?VK_FALSE:VK_TRUE;
        depth.depthWriteEnable=(hudOverlay||paintOverlay)?VK_FALSE:VK_TRUE;
        depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                                       VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{}; blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        if(hudOverlay||paintOverlay) {
            // HUD and paint hover blend; normal solids remain opaque.
            blendAttachment.blendEnable=VK_TRUE;
            blendAttachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.colorBlendOp=VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.alphaBlendOp=VK_BLEND_OP_ADD;
        }
        blend.attachmentCount=1;blend.pAttachments=&blendAttachment;
        VkGraphicsPipelineCreateInfo pipelineInfo{}; pipelineInfo.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount=static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages=stages.data();
        pipelineInfo.pVertexInputState=&input;
        pipelineInfo.pInputAssemblyState=&assembly;
        pipelineInfo.pViewportState=&viewport;
        pipelineInfo.pRasterizationState=&raster;
        pipelineInfo.pMultisampleState=&multi;
        pipelineInfo.pDepthStencilState=&depth;
        pipelineInfo.pColorBlendState=&blend;
        pipelineInfo.pDynamicState=&dynamic;
        pipelineInfo.layout=pipelineLayout;
        pipelineInfo.renderPass=renderPass;
        VkPipeline result=VK_NULL_HANDLE;
        const VkResult code=vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&result);
        vkDestroyShaderModule(device,frag,nullptr);
        vkDestroyShaderModule(device,vert,nullptr);
        check(code,"Create graphics pipeline");
        return result;
    }
    void createSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"Surface capabilities");
        std::uint32_t count=0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,nullptr),"Count surface formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&count,formats.data()),"Surface formats");
        VkSurfaceFormatKHR selected=formats.front();
        for(auto candidate:formats)
            if(candidate.format==VK_FORMAT_B8G8R8A8_SRGB&&candidate.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {selected=candidate;break;}
        swapFormat=selected.format;
        if(caps.currentExtent.width!=std::numeric_limits<std::uint32_t>::max())extent=caps.currentExtent;
        else {
            int width=0,height=0;
            glfwGetFramebufferSize(window,&width,&height);
            extent.width=std::clamp(std::uint32_t(width),caps.minImageExtent.width,caps.maxImageExtent.width);
            extent.height=std::clamp(std::uint32_t(height),caps.minImageExtent.height,caps.maxImageExtent.height);
        }
        std::uint32_t requested=std::max(2u,caps.minImageCount+1);
        if(caps.maxImageCount!=0)requested=std::min(requested,caps.maxImageCount);
        VkSwapchainCreateInfoKHR info{}; info.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface=surface;
        info.minImageCount=requested;
        info.imageFormat=selected.format;info.imageColorSpace=selected.colorSpace;
        info.imageExtent=extent;info.imageArrayLayers=1;
        info.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::array families{graphicsFamily,presentFamily};
        if(graphicsFamily!=presentFamily){
            info.imageSharingMode=VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount=2;info.pQueueFamilyIndices=families.data();
        }else info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform=caps.currentTransform;
        info.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode=VK_PRESENT_MODE_FIFO_KHR;
        info.clipped=VK_TRUE;
        check(vkCreateSwapchainKHR(device,&info,nullptr,&swapchain),"Create swapchain");
        check(vkGetSwapchainImagesKHR(device,swapchain,&count,nullptr),"Count swapchain images");
        images.resize(count);
        check(vkGetSwapchainImagesKHR(device,swapchain,&count,images.data()),"Read swapchain images");
        for(auto image:images)views.push_back(imageView(image,swapFormat,VK_IMAGE_ASPECT_COLOR_BIT));
        // A present-wait semaphore must remain associated with its swapchain
        // image until that image is acquired again. A single shared semaphore
        // could otherwise be reused while presentation still waits on it.
        VkSemaphoreCreateInfo semaphoreInfo{}; semaphoreInfo.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for(std::size_t i=0;i<images.size();++i) {
            VkSemaphore semaphore=VK_NULL_HANDLE;
            check(vkCreateSemaphore(device,&semaphoreInfo,nullptr,&semaphore),"Create present semaphore");
            finishedForImage.push_back(semaphore);
        }
        createDepth();
        createRenderPass();
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(PushConstants)};
        createNewPreviewDescriptor();
        VkPipelineLayoutCreateInfo layout{}; layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;
        layout.setLayoutCount=1;
        layout.pSetLayouts=&newPreviewSetLayout_;
        check(vkCreatePipelineLayout(device,&layout,nullptr,&pipelineLayout),"Create pipeline layout");
        solidPipeline=makePipeline();
        paintPreviewPipeline=makePipeline(false,true);
        hudPipeline=makePipeline(true);
        for(auto view:views) {
            const std::array attachments{view,depthView};
            VkFramebufferCreateInfo fb{}; fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass=renderPass;
            fb.attachmentCount=static_cast<std::uint32_t>(attachments.size());
            fb.pAttachments=attachments.data();
            fb.width=extent.width;fb.height=extent.height;fb.layers=1;
            VkFramebuffer frame=VK_NULL_HANDLE;
            check(vkCreateFramebuffer(device,&fb,nullptr,&frame),"Create framebuffer");
            framebuffers.push_back(frame);
        }
    }
    void destroySwapchain() {
        for(auto fb:framebuffers)vkDestroyFramebuffer(device,fb,nullptr);
        framebuffers.clear();
        for(auto semaphore:finishedForImage)vkDestroySemaphore(device,semaphore,nullptr);
        finishedForImage.clear();
        if(solidPipeline)vkDestroyPipeline(device,solidPipeline,nullptr);
        if(hudPipeline)vkDestroyPipeline(device,hudPipeline,nullptr);
        if(paintPreviewPipeline)vkDestroyPipeline(device,paintPreviewPipeline,nullptr);
        solidPipeline=hudPipeline=paintPreviewPipeline=VK_NULL_HANDLE;
        if(pipelineLayout)vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
        pipelineLayout=VK_NULL_HANDLE;
        if(newPreviewDescriptorPool_)
            vkDestroyDescriptorPool(device,newPreviewDescriptorPool_,nullptr);
        newPreviewDescriptorPool_=VK_NULL_HANDLE;
        newPreviewDescriptor_=VK_NULL_HANDLE;
        if(newPreviewSetLayout_)
            vkDestroyDescriptorSetLayout(device,newPreviewSetLayout_,nullptr);
        newPreviewSetLayout_=VK_NULL_HANDLE;
        if(renderPass)vkDestroyRenderPass(device,renderPass,nullptr);
        renderPass=VK_NULL_HANDLE;
        if(depthView)vkDestroyImageView(device,depthView,nullptr);
        if(depthImage)vkDestroyImage(device,depthImage,nullptr);
        if(depthMemory)vkFreeMemory(device,depthMemory,nullptr);
        depthView=VK_NULL_HANDLE;depthImage=VK_NULL_HANDLE;depthMemory=VK_NULL_HANDLE;
        for(auto view:views)vkDestroyImageView(device,view,nullptr);
        views.clear();images.clear();
        if(swapchain)vkDestroySwapchainKHR(device,swapchain,nullptr);
        swapchain=VK_NULL_HANDLE;
    }
    void recreateSwapchain() {
        int w=0,h=0;
        glfwGetFramebufferSize(window,&w,&h);
        while((w==0||h==0)&&!glfwWindowShouldClose(window)) {
            glfwWaitEvents();glfwGetFramebufferSize(window,&w,&h);
        }
        if(glfwWindowShouldClose(window))return;
        check(vkDeviceWaitIdle(device),"Wait for swapchain recreation");
        drainGpuRetirements();
        destroySwapchain();
        createSwapchain();
        refreshHud();
    }
    void createCommands() {
        VkCommandPoolCreateInfo pool{}; pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex=graphicsFamily;
        pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device,&pool,nullptr,&commandPool),"Create command pool");
        VkCommandBufferAllocateInfo alloc{}; alloc.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool=commandPool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device,&alloc,&commandBuffer),"Allocate command buffer");
        VkSemaphoreCreateInfo semaphore{}; semaphore.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(device,&semaphore,nullptr,&available),"Create acquire semaphore");
        VkFenceCreateInfo fenceInfo{}; fenceInfo.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device,&fenceInfo,nullptr,&fence),"Create frame fence");
    }
    void record(std::uint32_t index) {
        check(vkResetCommandBuffer(commandBuffer,0),"Reset command buffer");
        VkCommandBufferBeginInfo begin{}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        check(vkBeginCommandBuffer(commandBuffer,&begin),"Begin command buffer");
        VkClearValue clear[2]{};
        clear[0].color={{0.055f,0.065f,0.075f,1.0f}};
        clear[1].depthStencil={1.0f,0};
        VkRenderPassBeginInfo pass{}; pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass=renderPass;pass.framebuffer=framebuffers[index];
        pass.renderArea.offset={0,0};pass.renderArea.extent=extent;
        pass.clearValueCount=2;pass.pClearValues=clear;
        vkCmdBeginRenderPass(commandBuffer,&pass,VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{0,0,float(extent.width),float(extent.height),0,1};
        const VkRect2D scissor{{0,0},extent};
        vkCmdSetViewport(commandBuffer,0,1,&viewport);
        vkCmdSetScissor(commandBuffer,0,1,&scissor);
        auto constants=makeConstants(int(extent.width),int(extent.height),
                                           interaction_.cameraScale,orbit_,
                                           sceneBounds_,scaleFollow_.cameraOffset());
        // W drives mesh coverage inside the visible solid fragment.
        constants.wireRaster[3]=wireVisible_?1.0f:0.0f;
        vkCmdPushConstants(commandBuffer,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,sizeof(constants),&constants);
        VkDeviceSize offset=0;
        vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,solidPipeline);
        if(floorVisible_) {
            vkCmdBindVertexBuffers(commandBuffer,0,1,&floorSolid.buffer,&offset);
            vkCmdDraw(commandBuffer,floorSolid.vertexCount,1,0,0);
        }
        if(!inspectionStepReplaceMesh_&&solid.vertexCount) {
            vkCmdBindVertexBuffers(commandBuffer,0,1,&solid.buffer,&offset);
            vkCmdDraw(commandBuffer,solid.vertexCount,1,0,0);
        }
        if(!inspectionStepReplaceMesh_&&displayedIndex_)
            for(const auto& [id,buffers]:partitionBuffers_) {
            (void)id;
            if(!buffers.solid.vertexCount)continue;
            vkCmdBindVertexBuffers(commandBuffer,0,1,&buffers.solid.buffer,&offset);
            vkCmdDraw(commandBuffer,buffers.solid.vertexCount,1,0,0);
        }
        // UX04 transient overlay: coincident with exact source triangles,
        // depth tested against visible mesh with writes disabled. A separate
        // alpha-blended pipeline supplies #2000C000, not persistent paint.
        if(paintPreviewSolid.vertexCount) {
            vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,
                              paintPreviewPipeline);
            vkCmdBindVertexBuffers(commandBuffer,0,1,
                                   &paintPreviewSolid.buffer,&offset);
            vkCmdDraw(commandBuffer,paintPreviewSolid.vertexCount,1,0,0);
            vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,
                              solidPipeline);
        }
        // R01: draw red, depth-tested inspection markers ON TOP of the
        // diagnostic candidate, without modifying its normal mesh data.
        if(inspectionSolid.vertexCount) {
            vkCmdBindVertexBuffers(commandBuffer,0,1,&inspectionSolid.buffer,&offset);
            vkCmdDraw(commandBuffer,inspectionSolid.vertexCount,1,0,0);
        }
        // M68: mesh edges and floor unit grid are shaded on their OWN
        // depth-tested solid fragments. No detached wire quads are
        // emitted; M69 retires now-unused wire GPU buffers/pipeline.
        vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,hudPipeline);
        vkCmdBindDescriptorSets(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,0,1,&newPreviewDescriptor_,0,nullptr);
        vkCmdBindVertexBuffers(commandBuffer,0,1,&hud.buffer,&offset);
        vkCmdDraw(commandBuffer,hud.vertexCount,1,0,0);
        vkCmdEndRenderPass(commandBuffer);
        check(vkEndCommandBuffer(commandBuffer),"End command buffer");
    }
    void draw() {
        const double drawStart=glfwGetTime();
        check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"Wait for render fence");
        drainGpuRetirements();
        std::uint32_t image=0;
        auto acquired=vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,available,VK_NULL_HANDLE,&image);
        if(acquired==VK_ERROR_OUT_OF_DATE_KHR){recreateSwapchain();return;}
        if(acquired!=VK_SUCCESS&&acquired!=VK_SUBOPTIMAL_KHR)check(acquired,"Acquire swapchain image");
        check(vkResetFences(device,1,&fence),"Reset render fence");
        record(image);
        VkPipelineStageFlags wait=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&available;
        submit.pWaitDstStageMask=&wait;
        submit.commandBufferCount=1;submit.pCommandBuffers=&commandBuffer;
        const VkSemaphore completion=finishedForImage[image];
        submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&completion;
        check(vkQueueSubmit(graphicsQueue,1,&submit,fence),"Submit rendering");
        VkPresentInfoKHR present{}; present.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount=1;present.pWaitSemaphores=&completion;
        present.swapchainCount=1;present.pSwapchains=&swapchain;present.pImageIndices=&image;
        auto code=vkQueuePresentKHR(presentQueue,&present);
        if(code==VK_ERROR_OUT_OF_DATE_KHR||code==VK_SUBOPTIMAL_KHR||acquired==VK_SUBOPTIMAL_KHR)
            recreateSwapchain();
        else check(code,"Present frame");
        lastDrawMs_=(glfwGetTime()-drawStart)*1000.0;
        ++framesThisInterval_;
        ++logFrames_;
    }
    // The same owner-union and prepare/commit protocol as Ctrl+O. Only
    // prepared, validated geometry can cross into the live viewport. New
    // additionally resets input, history-facing UI and camera framing.
    void publishPreparedNew(PreparedMeshOpen prepared) {
        if(!prepared.index||!prepared.commit)
            throw std::runtime_error("New produced no valid indexed source");
        std::set<PartitionId> owners;
        if(displayedIndex_)
            displayedIndex_->revision().forEachPartition(
                [&](PartitionId id,const MeshPartition&){owners.insert(id);});
        prepared.index->revision().forEachPartition(
            [&](PartitionId id,const MeshPartition&){owners.insert(id);});
        const std::vector<PartitionId> dirty(owners.begin(),owners.end());
        clearPaintPreview();
        refreshPartitions(prepared.index,dirty,
            prepared.bounds?std::optional<Vec3>(prepared.bounds->center()):
                            std::nullopt);
        prepared.commit();
        if(prepared.bounds&&prepared.bounds->hasVertices) {
            frameScene(*prepared.bounds);
            floorZ_=prepared.bounds->minimum.z-
                std::max(0.25,prepared.bounds->radius()*0.05);
            const auto scenery=buildWorkFloor(
                floorZ_,prepared.bounds->center(),
                std::max(8.0,prepared.bounds->radius()*2.5));
            replaceBuffer(floorSolid,scenery.solid);
        }
        interaction_.radiusRange=prepared.radiusRange;
        interaction_.paintNominalEdgeLength=prepared.radiusRange.minimum;
        interaction_.radius=prepared.radius;
        interaction_.paintSymmetric=prepared.symmetric;
        paintMode_=false;
        painting_=false;
        paintPanelDragging_=false;
        paintControls_=PaintControls{};
        lastPaintCursor_.reset();
        mergeInput_.revisionChanged(displayedIndex_);
        oneShotAwaitingDisplay_=false;
        displayedStamps_=0;
        waitingForDrain_=false;
        mouseWasDown_=glfwGetMouseButton(
            window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        newModelHint_.clear();
        newModelError_.clear();
        refreshHud();
        updateTitle();
        std::cout<<"[new] published independent initial model "
                 <<"updated_owners="<<dirty.size()<<"\n";
    }
    void applyNewModalAction(NewDialogResult action) {
        if(action.action==NewDialogAction::NoAction)return;
        try {
            if(action.action==NewDialogAction::Create) {
                if(!action.model||!interaction_.prepareNewModel)
                    throw std::logic_error(
                        "New source creation unavailable");
                auto prepared=interaction_.prepareNewModel(*action.model);
                publishPreparedNew(std::move(prepared));
                return;
            }
            const bool saveAs=action.action==NewDialogAction::RequestSaveAs;
            if(!interaction_.startNewSave)
                throw std::logic_error("New document Save callback unavailable");
            const bool queued=interaction_.startNewSave(
                window,saveAs,displayedIndex_);
            if(queued)newModelDialog_.saveQueued();
            else newModelDialog_.saveCancelled();
        }catch(const std::exception& error) {
            newModelError_=error.what();
            std::cerr<<"[new] source/save rejected; current document retained: "
                     <<newModelError_<<"\n";
            // A failed generator must leave a retryable chooser, not silently
            // discard the user's previous document or start an empty model.
            if(action.action==NewDialogAction::Create&&action.model)
                newModelDialog_.open(action.model->vertices);
            else if(newModelDialog_.phase()==NewDialogPhase::AwaitSave)
                newModelDialog_.saveCompleted(false);
        }
    }
    void processNewModelModal() {
        if(!newModelDialog_.visible())return;
        const auto info=interaction_.newDocumentInfo?
            interaction_.newDocumentInfo(displayedIndex_):
            NewDocumentInfo{};
        newModelHint_=info.filenameHint;
        if(newModelDialog_.phase()==NewDialogPhase::AwaitSave&&
           interaction_.pollNewSave) {
            const auto state=interaction_.pollNewSave();
            if(state!=NewDocumentSaveProgress::Pending) {
                const bool success=state==NewDocumentSaveProgress::Succeeded;
                const auto decision=newModelDialog_.saveCompleted(success);
                if(!success)newModelError_="SAVE FAILED; OLD DOCUMENT PRESERVED";
                else applyNewModalAction(decision);
            }
        }
        if(!newModelDialog_.visible()) {
            newModelText_.clear();
            return;
        }
        if(justPressed(GLFW_KEY_ESCAPE,modalEscapeWasDown_)) {
            newModelDialog_.cancel();
            newModelText_.clear();
            suppressExitEscape_=true;
            mouseWasDown_=glfwGetMouseButton(
                window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
            return;
        }
        const auto phase=newModelDialog_.phase();
        if(phase==NewDialogPhase::ChoosePrimitive) {
            if(justPressed(GLFW_KEY_BACKSPACE,modalBackspaceWasDown_))
                newModelDialog_.backspace();
            const bool control=keyboard_.down(GLFW_KEY_LEFT_CONTROL)||
                               keyboard_.down(GLFW_KEY_RIGHT_CONTROL);
            if(control&&keyboard_.takePress(GLFW_KEY_A))
                newModelDialog_.clearVertexInput();
        }
        auto choose=[&](NewModelKind kind) {
            try {
                newModelError_.clear();
                const auto decision=newModelDialog_.choose(
                    kind,info.dirty,info.canDirectSave);
                applyNewModalAction(decision);
            }catch(const std::exception& error) {
                newModelError_=error.what();
            }
        };
        auto save=[&](bool saveAs) {
            try {
                newModelError_.clear();
                applyNewModalAction(newModelDialog_.save(saveAs));
            }catch(const std::exception& error) {
                newModelError_=error.what();
            }
        };
        for(const auto codepoint:newModelText_) {
            if(!newModelDialog_.visible())break;
            if(phase==NewDialogPhase::ChoosePrimitive) {
                if(codepoint>='0'&&codepoint<='9')
                    newModelDialog_.typeDigit(codepoint);
            }else if(phase==NewDialogPhase::ConfirmDiscard) {
                const char letter=char(std::toupper(
                    static_cast<unsigned char>(codepoint)));
                if(letter=='N')applyNewModalAction(newModelDialog_.discard());
                else if(letter=='S')
                    save(!newModelDialog_.canDirectSave());
                else if(letter=='A'&&newModelDialog_.canDirectSave())
                    save(true);
            }
        }
        newModelText_.clear();
        double cursorX=0,cursorY=0;
        glfwGetCursorPos(window,&cursorX,&cursorY);
        int ww=0,wh=0,fw=0,fh=0;
        glfwGetWindowSize(window,&ww,&wh);
        glfwGetFramebufferSize(window,&fw,&fh);
        if(ww>0&&wh>0&&fw>0&&fh>0) {
            cursorX*=double(fw)/ww;
            cursorY*=double(fh)/wh;
        }
        const bool down=glfwGetMouseButton(
            window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        const bool clicked=down&&!modalMouseWasDown_;
        modalMouseWasDown_=down;
        if(!clicked||!newModelDialog_.visible())return;
        const auto layout=newDialogLayout(
            fw,fh,newModelDialog_.phase(),newModelDialog_.canDirectSave());
        if(layout.cancel.contains(cursorX,cursorY)) {
            newModelDialog_.cancel();
            mouseWasDown_=down;
            return;
        }
        if(newModelDialog_.phase()==NewDialogPhase::ChoosePrimitive) {
            if(layout.vertices.contains(cursorX,cursorY))
                newModelDialog_.clearVertexInput();
            else if(layout.asymmetric.contains(cursorX,cursorY))
                choose(NewModelKind::AsymmetricSphere);
            else if(layout.symmetric.contains(cursorX,cursorY))
                choose(NewModelKind::SymmetricSphere);
            else if(layout.torus.contains(cursorX,cursorY))
                choose(NewModelKind::Torus);
        }else if(newModelDialog_.phase()==NewDialogPhase::ConfirmDiscard) {
            if(newModelDialog_.canDirectSave()&&
               layout.save.contains(cursorX,cursorY))
                save(false);
            else if(layout.saveAs.contains(cursorX,cursorY))save(true);
            else if(layout.discard.contains(cursorX,cursorY))
                applyNewModalAction(newModelDialog_.discard());
        }
    }

public:
    explicit VulkanViewport(const Mesh& mesh, ViewportAnimation animation,
                            ViewportInteraction interaction)
        : mesh_(mesh), animation_(std::move(animation)),
          interaction_(std::move(interaction)) {
        displayedIndex_=interaction_.initialRevision;
        for(const auto vertex:mesh_.vertices)sceneBounds_.include(vertex);
        if (animation_.advance && (!std::isfinite(animation_.stampsPerSecond) ||
                                   animation_.stampsPerSecond <= 0))
            throw std::invalid_argument("Animation rate must be positive and finite");
        if(!glfwInit())throw std::runtime_error("GLFW initialization failed");
        if(!glfwVulkanSupported())throw std::runtime_error("Vulkan is not supported by GLFW on this system");
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        // A monitor-owned GLFW fullscreen window bypasses normal WM placement.
        // Begin as a normal, managed window so KDE (or another WM) decides
        // which monitor it belongs to, following its placement rules.
        window=glfwCreateWindow(960,720,"Mesher",nullptr,nullptr);
        if(!window)throw std::runtime_error("Cannot open Mesher window");
        setMesherWindowIcon(window,MESHER_ASSET_DIR);
        // GLFW key tokens identify US-layout positions. Translate printable
        // keys using the active OS layout before assigning semantic actions;
        // key releases always use their cached press-time mapping.
        glfwSetWindowUserPointer(window,this);
        glfwSetKeyCallback(window,[](GLFWwindow* w,int key,int scancode,
                                     int action,int){
            auto* self=static_cast<VulkanViewport*>(glfwGetWindowUserPointer(w));
            const char* mappedName=nullptr;
            if(action==GLFW_PRESS && key>=GLFW_KEY_APOSTROPHE &&
               key<=GLFW_KEY_WORLD_2)
                mappedName=glfwGetKeyName(key,scancode);
            self->keyboard_.onKey(key,action,mappedName?mappedName:"");
            if(action==GLFW_PRESS && self->startupSplash_)
                self->startupSplash_->dismiss();
        });
        // Use GLFW's Unicode text callback instead of physical digit scan
        // codes so keyboard layout maps modal numeric input correctly.
        glfwSetCharCallback(window,[](GLFWwindow* w,unsigned int codepoint){
            auto* self=static_cast<VulkanViewport*>(glfwGetWindowUserPointer(w));
            if(self&&self->newModelDialog_.visible()&&codepoint<128)
                self->newModelText_.push_back(static_cast<char>(codepoint));
        });
        glfwSetWindowFocusCallback(window,[](GLFWwindow* w,int focused){
            if(!focused) {
                auto* self=static_cast<VulkanViewport*>(glfwGetWindowUserPointer(w));
                self->keyboard_.clear();
                // Focus-loss synthetic releases may arrive after this
                // callback. All held-key identity is already invalidated.
            }
        });
        glfwSetMouseButtonCallback(window,[](GLFWwindow* w,int,int action,int){
            if(action==GLFW_PRESS) {
                auto* self=static_cast<VulkanViewport*>(glfwGetWindowUserPointer(w));
                if(self->startupSplash_)self->startupSplash_->dismiss();
            }
        });
#ifdef MESHER_X11_WM_FULLSCREEN
        previousActiveWindow_=activeX11Window();
#endif
        glfwShowWindow(window);
        glfwWaitEventsTimeout(0.08); // Give the WM a chance to place/map it.
#ifdef MESHER_X11_WM_FULLSCREEN
        requestX11State(window); // EWMH fullscreen on the already-placed window.
#else
        // Portable fallback: WM-controlled maximize, not primary-monitor
        // exclusive fullscreen. GLFW exposes no compositor-chosen fullscreen.
        glfwMaximizeWindow(window);
#endif
        glfwWaitEventsTimeout(0.10); // Allow the fullscreen transition to settle.
        if(interaction_.startupBounds)frameScene(*interaction_.startupBounds);
        // Open before expensive Vulkan uploads so the splash covers actual
        // startup time, not merely four seconds after initialization.
        startupSplash_=std::make_unique<StartupSplash>(window,MESHER_ASSET_DIR);
        const auto pumpStartupSplash=[this] {
            glfwPollEvents();
            if(interaction_.exportFailure) {
                if(auto failure=interaction_.exportFailure()) {
                    std::cerr<<"[export] background writer failed: "
                             <<*failure<<"\n";
                    nativeMeshExportErrorMessage(window,*failure);
                }
            }
            if(startupSplash_) {
                startupSplash_->tick();
                if(!startupSplash_->active())startupSplash_.reset();
            }
        };
        createInstance();
        check(glfwCreateWindowSurface(instance,window,nullptr,&surface),"Create Vulkan window surface");
        selectDevice();
        createNewPreviewAtlasTexture();
        pumpStartupSplash();
        const Mesh empty;
        // The original imported source is the authority for scenery
        // clearance; never scan an evolving worker root on the UI loop.
        floorZ_=viewportWorkFloorZ(mesh_);
        const auto scenery=interaction_.startupBounds?
            buildWorkFloor(floorZ_,sceneBounds_.center(),
                           std::max(8.0,sceneBounds_.radius()*2.5)):
            buildWorkFloor(floorZ_);
        floorSolid=upload(scenery.solid);
        auto drawing=buildDrawing(displayedIndex_?empty:mesh_,orbit_.center());
        // Indexed COW startup intentionally has no legacy flat model;
        // do not upload its zero-byte arrays (M59), including AA wires.
        if(!drawing.solid.empty())solid=upload(drawing.solid);
        if(displayedIndex_)initializePartitions();
        std::cout<<"[view] grid plane Z="<<floorZ_
                 <<" (startup source bounds); G hides/shows grid\n";
        // The viewport owns the displayed and cached references from here.
        interaction_.initialRevision.reset();
        createSwapchain();
        createCommands();
        pumpStartupSplash();
        refreshHud();
        statsBegin_=glfwGetTime();
        lastConsoleTime_=statsBegin_;
        viewportStartTime_=statsBegin_;
        nextStampTime_ = glfwGetTime() + (animation_.advance
            ? 1.0 / animation_.stampsPerSecond : 0.0);
        updateTitle();
        pumpStartupSplash();
    }
    ~VulkanViewport() {
        // Close child window before destroying its native owner or GLFW.
        startupSplash_.reset();
        if(device!=VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            drainGpuRetirements();
            retiredRevisions_.retire(std::move(displayedIndex_));
            retiredRevisions_.retire(partitionCache_.detachRevision());
            retiredRevisions_.drain();
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(available)vkDestroySemaphore(device,available,nullptr);
            if(commandPool)vkDestroyCommandPool(device,commandPool,nullptr);
            destroySwapchain();
            destroyNewPreviewAtlasTexture();
            destroyBuffer(solid);
            destroyBuffer(floorSolid);
            destroyBuffer(hud);
            destroyBuffer(inspectionSolid);
            destroyBuffer(paintPreviewSolid);
            for(auto& [id,buffers]:partitionBuffers_) {
                (void)id;
                destroyBuffer(buffers.solid);
            }
            partitionBuffers_.clear();
            vkDestroyDevice(device,nullptr);
        }
        if(surface)vkDestroySurfaceKHR(instance,surface,nullptr);
        if(instance)vkDestroyInstance(instance,nullptr);
#ifdef __linux__
        // Qt was lazily initialized by the first file dialog. Tear down
        // its event dispatcher/QPA while the original GLFW X11 window
        // and display still exist, rather than at static process exit.
        shutdownNativeDialogs();
#endif
        if(window)glfwDestroyWindow(window);
#ifdef MESHER_X11_WM_FULLSCREEN
        // Only when Escape exits the viewport. Let the WM decide whether to
        // honor the request rather than overriding focus with XSetInputFocus.
        if(restorePreviousFocus_)requestX11Activation(previousActiveWindow_);
#endif
        glfwTerminate();
    }
    void run(){
        if(interaction_.onReady)interaction_.onReady();
        while(!glfwWindowShouldClose(window)) {
            stampedThisFrame_=false;
            glfwPollEvents();
            if(startupSplash_) {
                startupSplash_->tick();
                if(!startupSplash_->active())startupSplash_.reset();
            }
            // Modal interception precedes the application-wide Escape-to-
            // exit shortcut. Holding Escape after dismissing New must not
            // accidentally close Mesher on the following frame.
            const bool ctrl=keyboard_.down(GLFW_KEY_LEFT_CONTROL)||
                            keyboard_.down(GLFW_KEY_RIGHT_CONTROL);
#ifdef __APPLE__
            const bool shortcut=ctrl||keyboard_.down(GLFW_KEY_LEFT_SUPER)||
                                keyboard_.down(GLFW_KEY_RIGHT_SUPER);
#else
            const bool shortcut=ctrl;
#endif
            if(!newModelDialog_.visible()&&shortcut&&
               !inspectionFrozen_&&!interaction_.repairStepMode&&
               !sculpting_&&!painting_&&!orbitDragging_&&
               !radiusDragging_&&!scaleDragging_&&
               !mergeInput_.enabled()&&interaction_.prepareNewModel&&
               (!interaction_.canBegin||interaction_.canBegin())&&
               keyboard_.takePress(GLFW_KEY_N)) {
                try {
                    // Always use original startup --vertices, never the
                    // currently edited/imported vertex count.
                    if(interaction_.newDocumentInfo&&
                       interaction_.newDocumentInfo(displayedIndex_).busy)
                        throw std::runtime_error(
                            "New requires finished worker edits and disk saves");
                    newModelDialog_.open(interaction_.startupVertexCount);
                    newModelText_.clear();
                    newModelError_.clear();
                    modalMouseWasDown_=glfwGetMouseButton(
                        window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
                    std::cout<<"[new] opened in-viewport model chooser\n";
                }catch(const std::exception& error) {
                    std::cerr<<"[new] cannot open chooser: "
                             <<error.what()<<"\n";
                }
            }
            if(newModelDialog_.visible()) {
                processNewModelModal();
                keyboard_.finishFrame();
                int width=0,height=0;
                glfwGetFramebufferSize(window,&width,&height);
                if(width==0||height==0){glfwWaitEvents();continue;}
                if(std::uint32_t(width)!=extent.width||
                   std::uint32_t(height)!=extent.height)
                    recreateSwapchain();
                else {
                    refreshHud();
                    draw();
                }
                sampleFrameRates();
                continue; // never sculpt, paint, orbit or queue normal shortcuts
            }
            if(!keyboard_.down(GLFW_KEY_ESCAPE))suppressExitEscape_=false;
            if(keyboard_.down(GLFW_KEY_ESCAPE)&&!suppressExitEscape_) {
                if(painting_&&interaction_.paintEnd) {
                    interaction_.paintEnd();
                    painting_=false;
                }
                if(sculpting_&&interaction_.end) {
                    interaction_.end(lastCursorHit_,std::max(
                        0.0,glfwGetTime()-mouseStartTime_));
                    sculpting_=false;
                }
                restorePreviousFocus_=true;
                break;
            }
            // History actions only when the worker is between strokes.
            const bool shift=keyboard_.down(GLFW_KEY_LEFT_SHIFT)||
                             keyboard_.down(GLFW_KEY_RIGHT_SHIFT);
            const bool z=justPressed(GLFW_KEY_Z,undoWasDown_);
            const bool y=justPressed(GLFW_KEY_Y,redoWasDown_);
            const bool save=justPressed(GLFW_KEY_S,saveWasDown_);
            const bool open=justPressed(GLFW_KEY_O,openWasDown_);
            const bool exportPressed=justPressed(GLFW_KEY_E,exportWasDown_);
            // Deliberate solid-only visual diagnostic. This does not edit
            // source triangles, select FaceIds or affect history.
            if(justPressed(GLFW_KEY_W,wireKeyWasDown_)) {
                wireVisible_=!wireVisible_;
                std::cout<<"[view] wireframe "
                         <<(wireVisible_?"ON (depth-owned mesh and grid AA)":
                                        "OFF (solid-only)")<<"\n";
                updateTitle();
            }
            // Geometry display and scenery are independently controllable.
            // G never hides legacy or immutable COW mesh geometry.
            if(justPressed(GLFW_KEY_B,paintModeKeyWasDown_)&&
               interaction_.paint&&!sculpting_&&!painting_&&
               !inspectionFrozen_&&!interaction_.repairStepMode&&
               glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)!=GLFW_PRESS) {
                paintMode_=!paintMode_;
                clearPaintPreview();
                paintPanelDragging_=false;
                lastPaintCursor_.reset();
                std::cout<<"[paint] brush "<<(paintMode_?"ON":"OFF")<<"\n";
                refreshHud();
                updateTitle();
            }
            if(justPressed(GLFW_KEY_G,floorKeyWasDown_)) {
                floorVisible_=!floorVisible_;
                std::cout<<"[view] floor/grid "
                         <<(floorVisible_?"ON":"OFF")
                         <<" (Z="<<floorZ_<<")\n";
                updateTitle();
            }
            if(interaction_.repairStepMode&&interaction_.repairStepCommand) {
                if(justPressed(GLFW_KEY_N,stepNextWasDown_))
                    interaction_.repairStepCommand(0);
                if(justPressed(GLFW_KEY_P,stepPrevWasDown_))
                    interaction_.repairStepCommand(1);
                if(justPressed(GLFW_KEY_C,stepCurrentWasDown_))
                    interaction_.repairStepCommand(2);
                if(justPressed(GLFW_KEY_ENTER,stepContinueWasDown_))
                    interaction_.repairStepCommand(3);
                if(justPressed(GLFW_KEY_BACKSPACE,stepCancelWasDown_))
                    interaction_.repairStepCommand(4);
                if(justPressed(GLFW_KEY_PAGE_UP,stepUpWasDown_)) {
                    repairStepScroll_=repairStepScroll_>3?repairStepScroll_-3:0;
                    refreshHud();
                }
                if(justPressed(GLFW_KEY_PAGE_DOWN,stepDownWasDown_)) {
                    repairStepScroll_+=3;
                    refreshHud();
                }
            }
            if(shortcut&&!inspectionFrozen_&&!sculpting_&&!painting_&&
               (!interaction_.canBegin||interaction_.canBegin())) {
                if(shift&&z&&interaction_.redo)interaction_.redo();
                else if(z&&interaction_.undo)interaction_.undo();
                else if(y&&interaction_.redo)interaction_.redo();
            }
            // Frozen automatic closure is always a case-capture point:
            // success may mean the *classifier* chose the wrong topology.
            // Ctrl+S stores observed outcome, Ctrl+Shift+S annotates
            // "prefer zipper", Ctrl+Alt+S annotates "prefer caps".
            // All modes remain private, with no live checkpoint publication.
            const bool alt=keyboard_.down(GLFW_KEY_LEFT_ALT)||
                           keyboard_.down(GLFW_KEY_RIGHT_ALT);
            if(shortcut&&save&&inspectionFrozen_&&inspectionAutoRequested_&&
               interaction_.frozenRepairCapture) {
                if(shift&&alt)
                    std::cout<<"[repair-r06] CASE_SAVE_REFUSED choose Shift OR Alt, not both\n";
                else interaction_.frozenRepairCapture(shift?2:(alt?1:0));
            }else if(shortcut&&save&&!inspectionFrozen_&&interaction_.checkpoint) {
                try {
                    const auto info=interaction_.newDocumentInfo?
                        interaction_.newDocumentInfo(displayedIndex_):
                        NewDocumentInfo{};
                    if(info.canDirectSave&&interaction_.directCheckpoint) {
                        interaction_.directCheckpoint(displayedIndex_);
                        std::cout<<"[checkpoint] queued native direct Save\n";
                    }else {
                        const auto suggestion=interaction_.checkpointSuggestedPath?
                            interaction_.checkpointSuggestedPath():
                            std::filesystem::path("mesher-checkpoint.mshr");
                        const auto choice=nativeSaveCheckpointDialog(
                            window,suggestion);
                        if(!dispatchCheckpointSaveAs(choice,
                                                    interaction_.checkpoint))
                            std::cout<<"[checkpoint] Save As cancelled; unchanged\n";
                    }
                }catch(const std::exception& error) {
                    std::cerr<<"[checkpoint] Save refused: "<<error.what()<<"\n";
                    nativeOpenErrorMessage(window,error.what());
                }
            }
            // UX07E: export only the exact accepted root currently displayed.
            // Dialog and disclosure run on this GLFW UI thread; flattening,
            // encoding, sync and replacement run only on the export worker.
            if(shortcut&&exportPressed&&interaction_.exportMesh&&
               displayedIndex_&&!inspectionFrozen_&&!sculpting_&&!painting_&&
               !radiusDragging_&&!scaleDragging_&&!orbitDragging_&&
               !mergeInput_.enabled()&&!interaction_.repairStepMode&&
               (!interaction_.canBegin||interaction_.canBegin())) {
                try {
                    const auto suggestion=interaction_.exportSuggestedPath?
                        interaction_.exportSuggestedPath():
                        std::filesystem::path("mesher-export.ply");
                    const auto preferred=interaction_.exportPreferredPreset?
                        interaction_.exportPreferredPreset():
                        MeshExportPreset::PlyLittleMeshAlpha;
                    const auto choice=nativeSaveMeshExportDialog(
                        window,suggestion,preferred);
                    if(choice) {
                        const auto& format=meshExportFormat(choice->preset);
                        if(!format.disclosure.empty()&&
                           !nativeConfirmMeshExportDisclosure(
                               window,format.disclosure)) {
                            std::cout<<"[export] conversion declined; no write queued\n";
                        }else {
                            interaction_.exportMesh(displayedIndex_,*choice);
                            std::cout<<"[export] queued "
                                     <<choice->path.string()<<" codec="
                                     <<format.label<<"\n";
                        }
                    }else std::cout<<"[export] Save As cancelled\n";
                }catch(const std::exception& error) {
                    std::cerr<<"[export] Save As refused: "<<error.what()<<"\n";
                    nativeMeshExportErrorMessage(window,error.what());
                }
            }
            // UX06: prepare a complete private indexed source and worker,
            // then rebuild both old+new partition owners before committing
            // the worker swap. A cancelled or invalid import touches no
            // existing session state, picking, history or GPU buffers.
            if(shortcut&&open&&!inspectionFrozen_&&!sculpting_&&!painting_&&
               !radiusDragging_&&!scaleDragging_&&!orbitDragging_&&
                !mergeInput_.enabled()&&
               !interaction_.repairStepMode&&interaction_.prepareOpen&&
               (!interaction_.canBegin||interaction_.canBegin())) {
                try {
                    const auto suggestion=interaction_.openSuggestedPath?
                        interaction_.openSuggestedPath():
                        std::filesystem::path("mesher-checkpoint.mshr");
                    const auto choice=nativeOpenMeshDialog(window,suggestion);
                    if(choice) {
                        auto prepared=interaction_.prepareOpen(window,*choice);
                        if(!prepared.index||!prepared.commit)
                            throw std::runtime_error("Open produced no valid source");
                        std::set<PartitionId> owners;
                        if(displayedIndex_)
                            displayedIndex_->revision().forEachPartition(
                                [&](PartitionId id,const MeshPartition&) {
                                    owners.insert(id);
                                });
                        prepared.index->revision().forEachPartition(
                            [&](PartitionId id,const MeshPartition&) {
                                owners.insert(id);
                            });
                        const std::vector<PartitionId> dirty(
                            owners.begin(),owners.end());
                        clearPaintPreview();
                        refreshPartitions(prepared.index,dirty,
                            prepared.bounds?std::optional<Vec3>(
                                prepared.bounds->center()):std::nullopt);
                        prepared.commit();
                        if(prepared.bounds&&prepared.bounds->hasVertices) {
                            frameScene(*prepared.bounds);
                            floorZ_=prepared.bounds->minimum.z-
                                std::max(0.25,prepared.bounds->radius()*0.05);
                            const auto scenery=buildWorkFloor(
                                floorZ_,prepared.bounds->center(),
                                std::max(8.0,prepared.bounds->radius()*2.5));
                            replaceBuffer(floorSolid,scenery.solid);
                        }
                        interaction_.radiusRange=prepared.radiusRange;
                        interaction_.paintNominalEdgeLength=prepared.radiusRange.minimum;
                        interaction_.radius=prepared.radius;
                        interaction_.paintSymmetric=prepared.symmetric;
                        mergeInput_.revisionChanged(displayedIndex_);
                        oneShotAwaitingDisplay_=false;
                        displayedStamps_=0;
                        waitingForDrain_=false;
                        refreshHud();
                        updateTitle();
                        std::cout<<"[open] loaded "<<choice->path.string()
                                 <<" full_rebind_owners="<<dirty.size()<<"\n";
                    }else std::cout<<"[open] cancelled; session unchanged\n";
                }catch(const MeshOpenCancelled&) {
                    std::cout<<"[open] PLY alpha decision cancelled; session unchanged\n";
                }catch(const std::exception& error) {
                    std::cerr<<"[open] refused; current session retained: "
                             <<error.what()<<"\n";
                    nativeOpenErrorMessage(window,error.what());
                }
            }
            if(!shortcut)updateMergeKeyboard();
            updateGlobalScaleInput();
            updateMouseOrbit();
            updateScaleFollow();
            updateMouseSculpt();
            pumpMouseStroke();
            updatePaintPreview();
            // Dedicated diagnostic input: one F8 key PRESS -> one worker
            // stamp -> one fully uploaded immutable snapshot. Unlike a
            // quick LMB click, this does not create timed Update/End
            // segments or allow another stamp before the prior frame is
            // visible to picking and inspection.
            if(justPressed(GLFW_KEY_F8,oneShotWasDown_)&&
               interaction_.stampOnce&&!oneShotAwaitingDisplay_&&
               !sculpting_&&!radiusDragging_&&!scaleDragging_&&
                !orbitDragging_&&
               !inspectionFrozen_&&!mergeInput_.enabled()&&
               glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)!=GLFW_PRESS&&
               glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)!=GLFW_PRESS&&
               (!interaction_.canBegin||interaction_.canBegin())&&
               (!interaction_.completedStamps||
                displayedStamps_==interaction_.completedStamps())) {
                if(const auto center=pickCursor()) {
                    const auto requested=displayedStamps_+1;
                    interaction_.stampOnce(*center);
                    oneShotRequestedCount_=requested;
                    oneShotAwaitingDisplay_=true;
                    std::cout<<"[sculpt-step] requested_stamp="<<requested
                             <<" center="<<center->x<<","<<center->y<<","<<center->z
                             <<" one_worker_stamp=1\n"<<std::flush;
                }else {
                    std::cout<<"[sculpt-step] no source face under cursor\n";
                }
            }
            if (animation_.advance) {
                const bool togglePause=justPressed(GLFW_KEY_SPACE, spaceWasDown_);
                const bool singleStep=justPressed(GLFW_KEY_RIGHT, rightWasDown_);
                const bool replay=justPressed(GLFW_KEY_R, rewindWasDown_);
                if (replay && animation_.reset) {
                    animation_.reset();
                    complete_=false;
                    paused_=false;
                    refreshMesh();
                    updateTitle();
                    nextStampTime_ = glfwGetTime() + 1.0 / animation_.stampsPerSecond;
                }
                if (togglePause && !complete_) {
                    paused_=!paused_;
                    nextStampTime_ = glfwGetTime() + 1.0 / animation_.stampsPerSecond;
                    updateTitle();
                }
                if (singleStep && paused_ && !complete_) advanceOneStamp();
                if (!paused_ && !complete_ && glfwGetTime() >= nextStampTime_) {
                    advanceOneStamp();
                    // Never catch up missed frames in a burst. A stamp is
                    // immediately presented before the next is attempted.
                    // Use an anchored cadence rather than adding a full period
                    // to the time spent processing each stamp. At most one
                    // stamp is processed between successive renders.
                    const double period=1.0/animation_.stampsPerSecond;
                    nextStampTime_+=period;
                    if(nextStampTime_<glfwGetTime()-period)
                        nextStampTime_=glfwGetTime();
                }
            }
            keyboard_.finishFrame();
            int width=0,height=0;
            glfwGetFramebufferSize(window,&width,&height);
            if(width==0||height==0){glfwWaitEvents();continue;}
            if(std::uint32_t(width)!=extent.width||std::uint32_t(height)!=extent.height)recreateSwapchain();
            else draw();
            sampleFrameRates();
        }
    }
};
} // namespace
void runViewport(const Mesh& mesh, ViewportAnimation animation,
                 ViewportInteraction interaction) {
    VulkanViewport viewport(mesh, std::move(animation), std::move(interaction));
    viewport.run();
}
} // namespace mesher
