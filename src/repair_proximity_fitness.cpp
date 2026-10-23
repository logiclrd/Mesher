#include "mesher/repair_auto_plan.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mesher {
namespace {
bool finitePosition(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
struct RingGeometry {
    double area{};
    std::vector<double> vertexWeights;
    double perimeter{};
};
// All vertices participate in the centroid. Translation improves
// conditioning only; the cross-sum is the vector area, NOT the PCA plane.
RingGeometry describeRing(const std::vector<Vec3>& ring) {
    if(ring.size()<3||ring.size()>512)
        throw std::invalid_argument("R06 proximity ring needs 3..512 vertices");
    Vec3 center{};
    for(const auto v:ring) {
        if(!finitePosition(v))
            throw std::invalid_argument("R06 proximity nonfinite rim vertex");
        center=center+v;
    }
    center=center/static_cast<double>(ring.size());
    if(!finitePosition(center))
        throw std::invalid_argument("R06 proximity centroid overflow");
    Vec3 crossSum{};
    RingGeometry out;
    std::vector<double> lengths(ring.size());
    out.vertexWeights.resize(ring.size());
    for(std::size_t i=0;i<ring.size();++i) {
        const auto next=(i+1)%ring.size();
        const auto e=length(ring[next]-ring[i]);
        if(!(e>0)||!std::isfinite(e))
            throw std::invalid_argument("R06 proximity nonpositive rim edge");
        lengths[i]=e;
        out.perimeter+=e;
        crossSum=crossSum+cross(ring[i]-center,ring[next]-center);
    }
    out.area=0.5*length(crossSum);
    if(!std::isfinite(out.area)||!std::isfinite(out.perimeter)||
       !(out.perimeter>0))
        throw std::invalid_argument("R06 proximity ring area/perimeter overflow");
    for(std::size_t i=0;i<ring.size();++i) {
        const auto prev=(i+ring.size()-1)%ring.size();
        out.vertexWeights[i]=0.5*(lengths[prev]+lengths[i]);
    }
    return out;
}
void accumulateDistances(const std::vector<Vec3>& source,
                         const std::vector<Vec3>& target,
                         const RingGeometry& geometry,
                         double& numerator,double& denominator) {
    for(std::size_t i=0;i<source.size();++i) {
        double closest=std::numeric_limits<double>::infinity();
        for(const auto p:target)
            closest=std::min(closest,length(source[i]-p));
        if(!std::isfinite(closest))
            throw std::invalid_argument("R06 proximity closest-vertex overflow");
        const auto weight=geometry.vertexWeights[i];
        numerator+=weight*closest;
        denominator+=weight;
    }
}
} // namespace

const char* autoClosureClassifierName(AutoClosureClassifier algorithm) noexcept {
    switch(algorithm) {
        case AutoClosureClassifier::Area:return "area";
        case AutoClosureClassifier::Proximity:return "proximity";
    }
    return "invalid";
}

ProximityFitnessTerms computeProximityFitness(
    const std::vector<Vec3>& first,const std::vector<Vec3>& second) {
    const auto a=describeRing(first);
    const auto b=describeRing(second);
    ProximityFitnessTerms terms;
    terms.area1=a.area;
    terms.area2=b.area;
    terms.averageArea=0.5*(a.area+b.area);
    terms.areaDifference=std::abs(a.area-b.area);
    double numerator=0,denominator=0;
    accumulateDistances(first,second,a,numerator,denominator);
    accumulateDistances(second,first,b,numerator,denominator);
    if(!(denominator>0)||!std::isfinite(denominator)||
       !std::isfinite(numerator))
        throw std::invalid_argument("R06 proximity distance weight overflow");
    terms.weightedDistance=numerator/denominator;
    terms.distanceSquared=terms.weightedDistance*terms.weightedDistance;
    terms.averageAreaTerm=1.0*terms.averageArea;
    terms.differenceTerm=-1.0*terms.areaDifference;
    terms.distanceTerm=-2.0*terms.distanceSquared;
    terms.score=terms.averageAreaTerm+terms.differenceTerm+terms.distanceTerm;
    if(!std::isfinite(terms.score)||!std::isfinite(terms.distanceSquared))
        throw std::invalid_argument("R06 proximity nonfinite fitness terms");
    return terms;
}
} // namespace mesher
