#include "mesher/viewport.hpp"
#include "mesher/picking.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
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

struct Vertex { float px,py,pz,nx,ny,nz,material; };
Vertex makeVertex(Vec3 p, Vec3 n={}, float material=0) {
    return {float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z),material};
}
struct Drawing {
    std::vector<Vertex> solid;
    std::vector<Vertex> lines;
};
struct Edge {
    std::uint32_t a,b;
    bool operator==(const Edge&) const = default;
};
struct EdgeHash {
    std::size_t operator()(Edge e) const noexcept {return (std::uint64_t(e.a)<<32)|e.b;}
};

Drawing buildDrawing(const Mesh& mesh) {
    Drawing out;
    out.solid.reserve(mesh.triangles.size()*3+6);
    out.lines.reserve(mesh.triangles.size()*3+150);
    std::unordered_set<Edge,EdgeHash> unique;
    unique.reserve(mesh.triangles.size()*2);
    for(const auto& t:mesh.triangles) {
        const Vec3 a=mesh.vertices[t.a],b=mesh.vertices[t.b],c=mesh.vertices[t.c];
        const Vec3 rawNormal=cross(b-a,c-a);
        const Vec3 normal=length(rawNormal)>1e-20 ? normalized(rawNormal) : Vec3{};
        out.solid.push_back(makeVertex(a,normal));
        out.solid.push_back(makeVertex(b,normal));
        out.solid.push_back(makeVertex(c,normal));
        auto edge=[&](std::uint32_t i,std::uint32_t j) {
            Edge e{std::min(i,j),std::max(i,j)};
            if(unique.insert(e).second) {
                out.lines.push_back(makeVertex(mesh.vertices[i]));
                out.lines.push_back(makeVertex(mesh.vertices[j]));
            }
        };
        edge(t.a,t.b);edge(t.b,t.c);edge(t.c,t.a);
    }
    // XY work floor, just below the unit sphere. The grid is drawn with
    // depth testing so that the solid sphere naturally occludes it.
    constexpr double z=-1.22;
    const Vec3 a{-8,-8,z},b{8,-8,z},c{8,8,z},d{-8,8,z};
    for(auto p:{a,b,c,a,c,d})out.solid.push_back(makeVertex(p,{0,0,1},1));
    for(int i=-8;i<=8;++i) {
        out.lines.push_back(makeVertex({double(i),-8,z}));
        out.lines.push_back(makeVertex({double(i),8,z}));
        out.lines.push_back(makeVertex({-8,double(i),z}));
        out.lines.push_back(makeVertex({8,double(i),z}));
    }
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
        case 'F': return {31,16,16,30,16,16,16};
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

std::vector<Vertex> buildHud(std::uint32_t width, std::uint32_t height,
                             const std::string& fps, const std::string& stamps,
                             const std::optional<BrushRadiusRange>& slider,
                             double radius) {
    constexpr int scale=2, characterWidth=6*scale, characterHeight=10*scale;
    constexpr int margin=16, padding=9;
    const int chars=int(std::max(fps.size(),stamps.size()));
    const int boxW=chars*characterWidth+2*padding;
    const int boxH=2*characterHeight+2*padding;
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
    return vertices;
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
Matrix perspective(float aspect) {
    constexpr float fov=0.7853981633974483f; // vertical field of view: 45 degrees
    constexpr float nearPlane=.05f,farPlane=100.f;
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
};
static_assert(sizeof(PushConstants)==96);
PushConstants makeConstants(int width,int height) {
    const float aspect=float(width)/float(height);
    // Match picking and rendering camera geometry exactly.
    const Vec3 eye=viewportCameraEye(double(width),double(height));
    PushConstants result;
    result.viewProjection=multiply(perspective(aspect),cameraView(eye));
    const Vec3 behindLeft=eye+Vec3{-2,-1.5,4};
    result.light0={float(behindLeft.x),float(behindLeft.y),float(behindLeft.z),1};
    result.light1={6,-1,3.5f,1};
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
    ViewportAnimation animation_;
    ViewportInteraction interaction_;
    bool mouseWasDown_=false, sculpting_=false, radiusDragging_=false;
    bool stampedThisFrame_=false;
    double lastPickMs_=0, lastUploadMs_=0, lastStampCallbackMs_=0, lastDrawMs_=0;
    double lastConsoleTime_=0;
    std::uint64_t logFrames_=0, logStamps_=0;
    Vec3 lastCursorHit_{};
    double mouseStartTime_=0;
    bool paused_ = false, complete_ = false;
    bool spaceWasDown_ = false, rightWasDown_ = false, rewindWasDown_ = false;
    double nextStampTime_ = 0;
    double statsBegin_ = 0, measuredFps_ = 0, measuredStampRate_ = 0;
    std::uint64_t framesThisInterval_ = 0, stampsThisInterval_ = 0;
    std::size_t lastInvertedCount_ = 0;
    GLFWwindow* window=nullptr;
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
    VkPipeline solidPipeline=VK_NULL_HANDLE,wirePipeline=VK_NULL_HANDLE,hudPipeline=VK_NULL_HANDLE;
    VkImage depthImage=VK_NULL_HANDLE;
    VkDeviceMemory depthMemory=VK_NULL_HANDLE;
    VkImageView depthView=VK_NULL_HANDLE;
    VkFormat depthFormat=VK_FORMAT_UNDEFINED;
    VkCommandPool commandPool=VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer=VK_NULL_HANDLE;
    VkSemaphore available=VK_NULL_HANDLE;
    std::vector<VkSemaphore> finishedForImage;
    VkFence fence=VK_NULL_HANDLE;
    struct Buffer {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;std::uint32_t vertexCount=0,capacity=0;};
    Buffer solid{},lines{},hud{};

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
        check(vkBindBufferMemory(device,result.buffer,result.memory,0),"Bind vertex memory");
        void* data=nullptr;
        check(vkMapMemory(device,result.memory,0,size,0,&data),"Map vertex memory");
        std::memcpy(data,vertices.data(),static_cast<std::size_t>(size));
        vkUnmapMemory(device,result.memory);
        return result;
    }
    // Vertex positions and triangle normals change on every stamp; topology
    // doesn't, so the allocated GPU buffer sizes remain constant. These
    // host-coherent allocations were already used by the initial upload.
    void replaceBuffer(Buffer& buffer, const std::vector<Vertex>& vertices) {
        if (vertices.size() > buffer.capacity) {
            // Future refinement may grow the mesh; reallocate only when the
            // old allocation cannot contain the changed topology.
            auto newer=upload(vertices);
            vkDestroyBuffer(device,buffer.buffer,nullptr);
            vkFreeMemory(device,buffer.memory,nullptr);
            buffer=newer;
            return;
        }
        buffer.vertexCount=static_cast<std::uint32_t>(vertices.size());
        const VkDeviceSize size = vertices.size() * sizeof(Vertex);
        void* data = nullptr;
        check(vkMapMemory(device, buffer.memory, 0, size, 0, &data),
              "Map dynamic vertex buffer");
        std::memcpy(data, vertices.data(), static_cast<std::size_t>(size));
        vkUnmapMemory(device, buffer.memory);
    }

    void refreshMesh() {
        const double started=glfwGetTime();
        // No vertex buffer writes are permitted while the GPU is reading the
        // buffers from a previous frame.
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
              "Wait before updating dynamic geometry");
        const auto drawing = buildDrawing(mesh_);
        replaceBuffer(solid, drawing.solid);
        replaceBuffer(lines, drawing.lines);
        lastUploadMs_=(glfwGetTime()-started)*1000.0;
    }

    void refreshHud() {
        if (extent.width==0 || extent.height==0) return;
        const std::string fpsText="FPS: "+hudDecimal(measuredFps_);
        const std::string stampText="STAMPS: "+hudDecimal(measuredStampRate_)+
                "/"+hudDecimal(animation_.advance ? animation_.stampsPerSecond : interaction_.stationaryStampsPerSecond);
        const std::optional<BrushRadiusRange> slider=interaction_.setRadius
            ? std::optional<BrushRadiusRange>(interaction_.radiusRange)
            : std::nullopt;
        const auto geometry=buildHud(extent.width,extent.height,fpsText,stampText,
                                     slider,interaction_.radius);
        if(hud.buffer==VK_NULL_HANDLE)hud=upload(geometry);
        else {
            check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),
                  "Wait before refreshing HUD");
            replaceBuffer(hud,geometry);
        }
    }

    std::optional<Vec3> pickCursor() const {
        double cx=0,cy=0;
        glfwGetCursorPos(window,&cx,&cy);
        int ww=0,wh=0,fw=0,fh=0;
        glfwGetWindowSize(window,&ww,&wh);
        glfwGetFramebufferSize(window,&fw,&fh);
        if(ww<=0||wh<=0||fw<=0||fh<=0)return std::nullopt;
        const double px=cx*double(fw)/ww,py=cy*double(fh)/wh;
        if(px<0 || py<0 || px>=fw || py>=fh)return std::nullopt;
        return firstMeshHit(mesh_,viewportRay(px,py,double(fw),double(fh)));
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
          <<"[sculpt] t="<<(now-statsBegin_)
          <<" s interval="<<elapsed
          <<" s FPS="<<double(logFrames_)/elapsed
          <<" stamps_per_sec="<<double(logStamps_)/elapsed
          <<" stamps_total="<<(interaction_.completedStamps
                                     ? interaction_.completedStamps():0)
          <<" pending_steps="<<(interaction_.pendingSamples
                                  ? interaction_.pendingSamples():0)
          <<" pending_segments="<<(interaction_.pendingSegments
                                  ? interaction_.pendingSegments():0)
          <<" callback_ms="<<lastStampCallbackMs_
          <<" picking_ms="<<lastPickMs_
          <<" mesh_upload_ms="<<lastUploadMs_
          <<" last_draw_ms="<<lastDrawMs_;
        if(interaction_.lastStampDetails)os<<" "<<interaction_.lastStampDetails();
        std::cout<<os.str()<<'\n'<<std::flush;
        lastConsoleTime_=now;
        logFrames_=logStamps_=0;
    }

    void updateMouseSculpt() {
        if(!interaction_.begin)return;
        const bool down=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
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
            if(!down)radiusDragging_=false;
            mouseWasDown_=down;
            return;
        }
        if(down&&!mouseWasDown_ &&
           (!interaction_.canBegin || interaction_.canBegin())) {
            const double pickStart=glfwGetTime();
            const auto picked=pickCursor();
            lastPickMs_=(glfwGetTime()-pickStart)*1000.0;
            if(picked) {
                sculpting_=true;
                lastCursorHit_=*picked;
                mouseStartTime_=glfwGetTime();
                const double stampStart=glfwGetTime();
                interaction_.begin(*picked);
                lastStampCallbackMs_=(glfwGetTime()-stampStart)*1000.0;
                ++stampsThisInterval_; // Initial stamp on mouse down
                stampedThisFrame_=true;
                refreshMesh();
                updateTitle();
                reportAfterStamp(1);
            }
        } else if(down&&sculpting_) {
            // Raycast AGAINST the mesh modified on the previous tick.
            const double pickStart=glfwGetTime();
            if(const auto picked=pickCursor())lastCursorHit_=*picked;
            lastPickMs_=(glfwGetTime()-pickStart)*1000.0;
            const double elapsed=std::max(0.0,glfwGetTime()-mouseStartTime_);
            // Store the exact 4D input segment without stamping here.
            // The frame loop consumes at most one queued Step after polling.
            interaction_.update(lastCursorHit_,elapsed);
        } else if(!down&&mouseWasDown_&&sculpting_) {
            const double pickStart=glfwGetTime();
            if(const auto picked=pickCursor())lastCursorHit_=*picked;
            lastPickMs_=(glfwGetTime()-pickStart)*1000.0;
            const double elapsed=std::max(0.0,glfwGetTime()-mouseStartTime_);
            // Mouse-up closes the input path; older queued steps still
            // need to be drawn, including after the mouse is released.
            interaction_.end(lastCursorHit_,elapsed);
            sculpting_=false;
            updateTitle();
        }
        mouseWasDown_=down;
    }

    void pumpMouseStroke() {
        if(!interaction_.pump || stampedThisFrame_)return;
        const double started=glfwGetTime();
        const bool stamped=interaction_.pump();
        if(!stamped)return;
        lastStampCallbackMs_=(glfwGetTime()-started)*1000.0;
        ++stampsThisInterval_;
        refreshMesh();
        updateTitle();
        reportAfterStamp(1);
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
        for (const auto& t : mesh_.triangles) {
            const Vec3 a=mesh_.vertices[t.a], b=mesh_.vertices[t.b],
                       c=mesh_.vertices[t.c];
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
            title+=sculpting_ ? " | Sculpting" : " | LMB: Inflate";
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
        const bool down = glfwGetKey(window, key) == GLFW_PRESS;
        const bool pressed = down && !wasDown;
        wasDown = down;
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
        const auto bytes=loadBinary(std::string(MESHER_SHADER_DIR)+"/"+name+".spv");
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
    VkPipeline makePipeline(bool wire, bool hudOverlay=false) {
        auto vert=shader(hudOverlay?"hud.vert":wire?"wire.vert":"mesh.vert");
        auto frag=shader(hudOverlay?"hud.frag":wire?"wire.frag":"mesh.frag");
        VkPipelineShaderStageCreateInfo vs{}; vs.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs.stage=VK_SHADER_STAGE_VERTEX_BIT;vs.module=vert;vs.pName="main";
        VkPipelineShaderStageCreateInfo fs{}; fs.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs.stage=VK_SHADER_STAGE_FRAGMENT_BIT;fs.module=frag;fs.pName="main";
        std::array stages{vs,fs};
        VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription,3> attrs{};
        attrs[0]={0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,px)};
        attrs[1]={1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,nx)};
        attrs[2]={2,0,VK_FORMAT_R32_SFLOAT,offsetof(Vertex,material)};
        VkPipelineVertexInputStateCreateInfo input{}; input.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;
        input.vertexAttributeDescriptionCount=wire?1:static_cast<std::uint32_t>(attrs.size());
        input.pVertexAttributeDescriptions=attrs.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{}; assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology=wire?VK_PRIMITIVE_TOPOLOGY_LINE_LIST:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
        depth.depthWriteEnable=(wire||hudOverlay)?VK_FALSE:VK_TRUE;
        depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                                       VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{}; blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        if(hudOverlay) {
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
        VkPipelineLayoutCreateInfo layout{}; layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;
        check(vkCreatePipelineLayout(device,&layout,nullptr,&pipelineLayout),"Create pipeline layout");
        solidPipeline=makePipeline(false);
        wirePipeline=makePipeline(true);
        hudPipeline=makePipeline(false,true);
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
        if(wirePipeline)vkDestroyPipeline(device,wirePipeline,nullptr);
        if(hudPipeline)vkDestroyPipeline(device,hudPipeline,nullptr);
        solidPipeline=wirePipeline=hudPipeline=VK_NULL_HANDLE;
        if(pipelineLayout)vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
        pipelineLayout=VK_NULL_HANDLE;
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
        const auto constants=makeConstants(int(extent.width),int(extent.height));
        vkCmdPushConstants(commandBuffer,pipelineLayout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,sizeof(constants),&constants);
        VkDeviceSize offset=0;
        vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,solidPipeline);
        vkCmdBindVertexBuffers(commandBuffer,0,1,&solid.buffer,&offset);
        vkCmdDraw(commandBuffer,solid.vertexCount,1,0,0);
        vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,wirePipeline);
        vkCmdBindVertexBuffers(commandBuffer,0,1,&lines.buffer,&offset);
        vkCmdDraw(commandBuffer,lines.vertexCount,1,0,0);
        vkCmdBindPipeline(commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,hudPipeline);
        vkCmdBindVertexBuffers(commandBuffer,0,1,&hud.buffer,&offset);
        vkCmdDraw(commandBuffer,hud.vertexCount,1,0,0);
        vkCmdEndRenderPass(commandBuffer);
        check(vkEndCommandBuffer(commandBuffer),"End command buffer");
    }
    void draw() {
        const double drawStart=glfwGetTime();
        check(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX),"Wait for render fence");
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
public:
    explicit VulkanViewport(const Mesh& mesh, ViewportAnimation animation,
                            ViewportInteraction interaction)
        : mesh_(mesh), animation_(std::move(animation)),
          interaction_(std::move(interaction)) {
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
        createInstance();
        check(glfwCreateWindowSurface(instance,window,nullptr,&surface),"Create Vulkan window surface");
        selectDevice();
        auto drawing=buildDrawing(mesh);
        solid=upload(drawing.solid);
        lines=upload(drawing.lines);
        createSwapchain();
        createCommands();
        refreshHud();
        statsBegin_=glfwGetTime();
        lastConsoleTime_=statsBegin_;
        nextStampTime_ = glfwGetTime() + (animation_.advance
            ? 1.0 / animation_.stampsPerSecond : 0.0);
        updateTitle();
    }
    ~VulkanViewport() {
        if(device!=VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(available)vkDestroySemaphore(device,available,nullptr);
            if(commandPool)vkDestroyCommandPool(device,commandPool,nullptr);
            destroySwapchain();
            auto freeBuffer=[&](Buffer b) {
                if(b.buffer)vkDestroyBuffer(device,b.buffer,nullptr);
                if(b.memory)vkFreeMemory(device,b.memory,nullptr);
            };
            freeBuffer(solid);freeBuffer(lines);freeBuffer(hud);
            vkDestroyDevice(device,nullptr);
        }
        if(surface)vkDestroySurfaceKHR(instance,surface,nullptr);
        if(instance)vkDestroyInstance(instance,nullptr);
        if(window)glfwDestroyWindow(window);
#ifdef MESHER_X11_WM_FULLSCREEN
        // Only when Escape exits the viewport. Let the WM decide whether to
        // honor the request rather than overriding focus with XSetInputFocus.
        if(restorePreviousFocus_)requestX11Activation(previousActiveWindow_);
#endif
        glfwTerminate();
    }
    void run(){
        while(!glfwWindowShouldClose(window)) {
            stampedThisFrame_=false;
            glfwPollEvents();
            if(glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS) {
                if(sculpting_&&interaction_.end) {
                    interaction_.end(lastCursorHit_,std::max(0.0,glfwGetTime()-mouseStartTime_));
                    sculpting_=false;
                }
                restorePreviousFocus_=true;
                break;
            }
            updateMouseSculpt();
            pumpMouseStroke();
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
