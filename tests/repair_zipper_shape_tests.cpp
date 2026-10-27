#include "mesher/repair_zipper_shape.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace mesher;
namespace {
void check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
std::array<Vec3,3> tri(Vec3 a,Vec3 b,Vec3 c) {
    return {a,b,c};
}
void centralApex() {
    const auto v=tri({0,0,0},{2,0,0},{1,.22,0});
    const auto p=chooseZipperShape(v,2.,.10);
    check(p.has_value()&&
          p->kind==ZipperShapeProposal::Kind::ApexToBase,
          "central shallow triangle needs apex-to-biased-base splitting");
    check(std::abs(p->baseT-.5)<1e-12,
          "central perpendicular and midpoint coincide");
    check(p->minimumChildQuality>p->parentQuality,
          "symmetric long base should split into two healthier children");
    check(!chooseZipperShape(v,3.,.10).has_value(),
          "longest edge below 80% of target must NOT subdivide");
}
void overhangAndDiagonal() {
    const auto mild=tri({0,0,0},{1,0,0},{-.05,.12,0});
    const auto p=chooseZipperShape(mild,1.,.10);
    check(p&&p->kind==ZipperShapeProposal::Kind::TwoEdge&&
          !p->useAlternateDiagonal,
          "near-end overhang should select the CD split");
    const auto severe=tri({0,0,0},{1,0,0},{-.5,.12,0});
    const auto q=chooseZipperShape(severe,1.,.10);
    check(q&&q->kind==ZipperShapeProposal::Kind::TwoEdge&&
          q->useAlternateDiagonal,
          "severe overhang should choose the healthier AE diagonal");
    check(q->minimumChildQuality>=.10,
          "shape template may not evade the hard child quality floor");
}
void rejectUnproductiveSkinnySubdivisions() {
    // Both children can satisfy the hard q floor yet have materially
    // WORSE minimum q than their parent. Such a split only creates
    // complexity, without improving the shape it was meant to repair.
    const auto harmful=tri({0,0,0},{1,0,0},{0,.1,0});
    const auto parent=zipperShapeQuality(
        harmful[0],harmful[1],harmful[2]);
    check(parent>.1,"shape regression fixture must start legal");
    check(!chooseZipperShape(harmful,1.,.1),
        "M24 must reject legal-but-shape-worsening subdivision");
}
void windingAndReflection() {
    const auto a=tri({0,0,0},{1,0,0},{-.5,.12,0});
    const auto mirrored=tri({0,0,0},{-1,0,0},{.5,.12,0});
    const auto original=chooseZipperShape(a,1.,.10);
    const auto other=chooseZipperShape(mirrored,1.,.10);
    check(original&&other&&
          original->kind==other->kind&&
          original->useAlternateDiagonal==other->useAlternateDiagonal&&
          std::abs(original->minimumChildQuality-
                   other->minimumChildQuality)<1e-12,
          "reflected triangle must select congruent legal child geometry");
    // Cycling the incoming face IDs must not change its physical template.
    const auto rotated=tri(a[1],a[2],a[0]);
    const auto cycle=chooseZipperShape(rotated,1.,.10);
    check(cycle&&original->kind==cycle->kind&&
          original->useAlternateDiagonal==cycle->useAlternateDiagonal&&
          std::abs(original->minimumChildQuality-
                   cycle->minimumChildQuality)<1e-12,
          "oriented face rotation must leave the template deterministic");
}
}
int main() {
    try {
        centralApex();
        overhangAndDiagonal();
        rejectUnproductiveSkinnySubdivisions();
        windingAndReflection();
        std::cout<<"[repair-r06-zipper-shape] two rules, diagonal quality,"
                    " mirror, winding and scale cutoff PASS\n";
    }catch(const std::exception& e) {
        std::cerr<<"[repair-r06-zipper-shape] FAILED "<<e.what()<<"\n";
        return 1;
    }
}
