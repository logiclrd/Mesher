#include "mesher/repair_cap_spatial.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
bool finite(Vec3 v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
}
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const auto sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
}

SpatialCapPatch buildSpatialCapFrontPoisson(
    const std::vector<Vec3>& orientedCapRim,
    const std::vector<Vec3>& survivingFaceNormals,
    const SpatialCapSettings& limits) {
    const auto n=orientedCapRim.size();
    if(n<3||n>limits.maxBoundaryVertices||
       n+1>limits.maxInteriorVertices||n*3>limits.maxFaces||
       !limits.maxPoissonIterations||!limits.maxIntersectionTests||
       !std::isfinite(limits.poissonTolerance)||
       !std::isfinite(limits.maximumInitialEdgeFactor)||
       !std::isfinite(limits.normalGuidance)||
       limits.poissonTolerance<=0||limits.maximumInitialEdgeFactor<=1||
       limits.normalGuidance<0||limits.normalGuidance>.5)
        throw std::length_error("R06-M06C spatial front finite-work limits refused");
    if(!survivingFaceNormals.empty()&&survivingFaceNormals.size()!=n)
        throw std::invalid_argument("R06-M06C front normals not indexed to cap rim");

    SpatialCapPatch result;
    result.boundaryCount=n;
    result.positions=orientedCapRim;
    Vec3 center{};
    std::vector<double> originalEdges;
    originalEdges.reserve(n);
    for(std::size_t i=0;i<n;++i) {
        const auto a=orientedCapRim[i];
        const auto b=orientedCapRim[(i+1)%n];
        if(!finite(a)||!finite(b))
            throw std::logic_error("R06-M06C front nonfinite rim position");
        center=center+a;
        originalEdges.push_back(length(b-a));
    }
    center=center/static_cast<double>(n);
    std::sort(originalEdges.begin(),originalEdges.end());
    const auto h=originalEdges[n/2];
    if(!(h>1e-12)||!std::isfinite(h))
        throw std::logic_error("R06-M06C front degenerate edge scale");

    // Advance the oriented FRONT one local mesh layer. The front is the
    // actual 3D rim, rather than a flattened projected polygon. Later
    // patch-vs-patch and candidate-vs-survivor audits may reject a folded
    // front whose interpolated inward ring crosses another surface.
    std::vector<Vec3> normals(n);
    Vec3 averageNormal{};
    for(std::size_t i=0;i<n;++i) {
        const auto source=survivingFaceNormals.empty()?
            Vec3{}:survivingFaceNormals[i];
        if(!finite(source))
            throw std::logic_error("R06-M06C front nonfinite source normal");
        const auto norm=length(source);
        if(norm>1e-12)normals[i]=source/norm;
        averageNormal=averageNormal+normals[i];
        result.positions.push_back(orientedCapRim[i]*.55+center*.45);
    }
    const auto normalLength=length(averageNormal);
    if(normalLength>1e-12)averageNormal=averageNormal/normalLength;
    result.positions.push_back(center);
    const auto hub=static_cast<std::uint32_t>(2*n);

    // The outward surface and inward ring share every ORIGINAL rim edge
    // exactly once. The last ring front is closed to an interior hub.
    for(std::uint32_t i=0;i<n;++i) {
        const auto j=static_cast<std::uint32_t>((i+1)%n);
        const auto a=i,b=j,c=static_cast<std::uint32_t>(n)+j;
        const auto d=static_cast<std::uint32_t>(n)+i;
        result.faces.push_back({a,b,c});
        result.faces.push_back({a,c,d});
        result.faces.push_back({d,c,hub});
    }

    // Sparse graph Laplacian with hard Dirichlet boundary coordinates.
    // Normal-driven RHS is a bounded approximate curvature source.
    // Unlike pure harmonic relaxation this can resist an inwardly
    // sunken patch. Local normals are inherited from surviving faces,
    // interpolated to the hub, and remain guidance, not certification.
    std::vector<std::set<std::uint32_t>> adjacent(2*n+1);
    for(const auto& face:result.faces)
        for(unsigned k=0;k<3;++k) {
            const auto a=face[k],b=face[(k+1)%3];
            adjacent[a].insert(b);
            adjacent[b].insert(a);
        }
    const auto sourceAt=[&](std::size_t index) {
        const auto direction=index<2*n?normals[index-n]:averageNormal;
        const auto degree=static_cast<double>(adjacent[index].size());
        return direction*(limits.normalGuidance*h*degree);
    };
    bool converged=false;
    const auto tolerance=limits.poissonTolerance*h;
    for(std::size_t iteration=0;iteration<limits.maxPoissonIterations;++iteration) {
        double residual=0;
        for(std::size_t i=n;i<result.positions.size();++i) {
            Vec3 rhs=sourceAt(i);
            for(const auto neighbor:adjacent[i])
                rhs=rhs+result.positions[neighbor];
            const auto updated=rhs/static_cast<double>(adjacent[i].size());
            if(!finite(updated))
                throw std::logic_error("R06-M06C Poisson produced nonfinite interior vertex");
            residual=std::max(residual,length(updated-result.positions[i]));
            result.positions[i]=updated;
        }
        result.poissonIterations=iteration+1;
        result.poissonResidual=residual;
        if(residual<=tolerance){converged=true;break;}
    }
    if(!converged)
        throw std::logic_error("R06-M06C Poisson did not meet finite residual bound");
    for(std::size_t i=0;i<n;++i)
        if(result.positions[i].x!=orientedCapRim[i].x||
           result.positions[i].y!=orientedCapRim[i].y||
           result.positions[i].z!=orientedCapRim[i].z)
            throw std::logic_error("R06-M06C fixed Dirichlet rim moved");

    // Validate spatial embedding, not a projected polygon. This pairwise
    // test is finite by construction; uncertain contacts are refusals.
    for(std::size_t i=0;i<result.faces.size();++i) {
        const auto& ids=result.faces[i];
        const auto a=result.positions[ids[0]];
        const auto b=result.positions[ids[1]];
        const auto c=result.positions[ids[2]];
        const auto q=quality(a,b,c);
        if(q<=1e-8||!std::isfinite(q))
            throw std::logic_error("R06-M06C zero-area or collapsed spatial front triangle");
        result.minimumQuality=std::min(result.minimumQuality,q);
        const auto longest=std::max({length(a-b),length(b-c),length(c-a)});
        if(longest>h*limits.maximumInitialEdgeFactor)
            throw std::logic_error("R06-M06C front triangles exceed finite local edge scale");
    }
    std::size_t tests=0;
    for(std::size_t i=0;i<result.faces.size();++i) {
        std::array<Vec3,3> a;
        std::array<VertexId,3> ai;
        for(unsigned k=0;k<3;++k) {
            ai[k]=static_cast<VertexId>(result.faces[i][k])+1;
            a[k]=result.positions[result.faces[i][k]];
        }
        for(std::size_t j=i+1;j<result.faces.size();++j) {
            if(++tests>limits.maxIntersectionTests)
                throw std::length_error("R06-M06C front pairwise intersection budget");
            std::array<Vec3,3> b;
            std::array<VertexId,3> bi;
            for(unsigned k=0;k<3;++k) {
                bi[k]=static_cast<VertexId>(result.faces[j][k])+1;
                b[k]=result.positions[result.faces[j][k]];
            }
            const auto contact=triangleContact(a,ai,b,bi);
            if(contact!=TriangleContact::Disjoint&&
               contact!=TriangleContact::ExpectedAdjacency)
                throw std::logic_error("R06-M06C folded spatial front self-intersects");
        }
    }
    return result;
}
} // namespace mesher
