#include "mesher/picking.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mesher {
Vec3 viewportCameraEye(double width,double height) {
    if(!(width>0&&height>0))throw std::invalid_argument("Viewport dimensions must be positive");
    constexpr double halfFov=0.39269908169872414;
    constexpr double fraction=2.0/3.0;
    const double aspect=width/height;
    const double limit=std::tan(halfFov)*std::min(1.0,aspect);
    const double distance=std::sqrt(1.0+1.0/std::pow(fraction*limit,2));
    return normalized(Vec3{0,-1,.33})*distance;
}
Ray viewportRay(double x,double y,double width,double height) {
    const auto eye=viewportCameraEye(width,height);
    const auto forward=normalized(Vec3{}-eye);
    const auto right=normalized(cross(forward,{0,0,1}));
    const auto up=cross(right,forward);
    const double tangent=std::tan(0.39269908169872414);
    const double dx=(2*x/width-1)*(width/height)*tangent;
    const double dy=(1-2*y/height)*tangent;
    return {eye,normalized(forward+right*dx+up*dy)};
}
std::optional<Vec3> firstMeshHit(const Mesh& mesh,Ray ray) {
    if(!std::isfinite(length(ray.direction)) || length(ray.direction)<1e-12)
        throw std::invalid_argument("Invalid picking ray direction");
    double best=std::numeric_limits<double>::infinity();
    for(const auto t:mesh.triangles) {
        const auto a=mesh.vertices[t.a],b=mesh.vertices[t.b],c=mesh.vertices[t.c];
        const auto ab=b-a,ac=c-a;
        const auto p=cross(ray.direction,ac);
        const double determinant=dot(ab,p);
        if(std::abs(determinant)<1e-13)continue;
        const double inverse=1/determinant;
        const auto displacement=ray.origin-a;
        const double u=dot(displacement,p)*inverse;
        if(u<-1e-12 || u>1.0+1e-12)continue;
        const auto q=cross(displacement,ab);
        const double v=dot(ray.direction,q)*inverse;
        if(v<-1e-12 || u+v>1.0+1e-12)continue;
        const double distance=dot(ac,q)*inverse;
        if(distance>1e-8 && distance<best)best=distance;
    }
    if(!std::isfinite(best))return std::nullopt;
    return ray.origin+ray.direction*best;
}
} // namespace mesher
