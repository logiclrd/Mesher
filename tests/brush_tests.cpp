#include "mesher/brush.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double actual, double expected, double eps, const char* message) {
    if (std::abs(actual - expected) > eps) throw std::runtime_error(message);
}
struct RecordingBrush : Brush {
    std::vector<std::vector<Vec3>> stamps;
    int begins = 0, ends = 0;
    void begin(Mesh&) override { ++begins; }
    void stamp(Mesh&, std::span<const BrushArea> areas) override {
        std::vector<Vec3> centers;
        for (auto& area : areas) centers.push_back(area.center);
        stamps.push_back(std::move(centers));
    }
    void end(Mesh&) override { ++ends; }
};
void testFalloff() {
    near(brushFalloff(0, 1, .3), 1, 1e-14, "Falloff center");
    near(brushFalloff(.3, 1, .3), 1, 1e-14, "Hardness plateau");
    near(brushFalloff(.65, 1, .3), .5, 1e-14, "S-curve midpoint");
    near(brushFalloff(1, 1, .3), 0, 1e-14, "Outer boundary");
    near(brushFalloff(1.1, 1, .3), 0, 1e-14, "Outside");
    near(brushFalloff(.2, 1, 1), 1, 1e-14, "Hardness one plateau");
    near(brushFalloff(1, 1, 1), 0, 1e-14, "Hardness one boundary");
    near(brushFalloff(.5, 1, 0), .5, 1e-14, "Hardness zero");
    const std::array<BrushArea,2> areas{{{{0,0,0},1,.3},{{1,0,0},1,.3}}};
    near(brushInfluence({0,0,0}, areas),1,1e-14,"Maximum, not sum");
    near(brushInfluence({.5,0,0}, areas),
         brushFalloff(.5,1,.3),1e-14,"Overlapping areas don't sum");
    bool bad=false;
    try { (void)brushFalloff(.5,1,1.2); } catch (const std::invalid_argument&) { bad=true; }
    check(bad,"Invalid hardness rejected");
}
void testSampling() {
    Mesh mesh;
    RecordingBrush brush;
    BrushStroke stroke(mesh, brush, .1);
    stroke.start({{{0,0,0},1,.3},{{0,2,0},1,.3}});
    const Vec3 first[] = {{.13,0,0},{.13,2,0}};
    stroke.update(first);
    const Vec3 second[] = {{.16,0,0},{.16,2,0}};
    stroke.update(second); // none: phase carries across calls
    const Vec3 third[] = {{.31,0,0},{.31,2,0}};
    stroke.update(third);
    const Vec3 end[] = {{.38,0,0},{.38,2,0}};
    stroke.finish(end);
    check(!stroke.active() && brush.begins==1 && brush.ends==1,"Lifecycle callbacks");
    check(brush.stamps.size()==5 && stroke.stamps()==5,"Start, three Steps, final endpoint");
    for(std::size_t i=0;i<5;++i) {
        const double expected = i==4 ? .38 : double(i)*.1;
        near(brush.stamps[i][0].x,expected,1e-12,"Stroke phase / overshoot");
        near(brush.stamps[i][1].x,expected,1e-12,"Synchronous second center");
        near(brush.stamps[i][1].y,2,1e-12,"Second center translation");
    }
    // A segment corner must not reset the global arc-length phase.
    RecordingBrush turnBrush;
    BrushStroke turn(mesh,turnBrush,.1);
    turn.start({{{0,0,0},1,.3}});
    const Vec3 corner[]={{.15,0,0}};
    turn.update(corner);
    const Vec3 destination[]={{.15,.15,0}};
    turn.finish(destination);
    check(turnBrush.stamps.size()==4,"Corner phase stamp count");
    near(turnBrush.stamps[2][0].x,.15,1e-12,"Corner sample x");
    near(turnBrush.stamps[2][0].y,.05,1e-12,"Corner overshoot along next leg");
    near(turnBrush.stamps[3][0].y,.15,1e-12,"Corner endpoint");
    // Invariant must be checked before making any stamps or altering state.
    RecordingBrush mismatchBrush;
    BrushStroke mismatch(mesh,mismatchBrush,.1);
    mismatch.start({{{0,0,0},1,.3},{{0,2,0},1,.3}});
    const Vec3 invalid[]={{.2,0,0},{.3,2,0}};
    bool rejected=false;
    try { mismatch.update(invalid); } catch (const std::invalid_argument&) { rejected=true; }
    check(rejected && mismatchBrush.stamps.size()==1,"Reject unequal travel atomically");
    const Vec3 valid[]={{.2,0,0},{.2,2,0}};
    mismatch.finish(valid);
    check(mismatchBrush.stamps.size()==3,"Rejected update did not consume arc length");
    RecordingBrush exactBrush;
    BrushStroke exact(mesh,exactBrush,.1);
    exact.start({{{0,0,0},1,.3}});
    exact.finish(std::array<Vec3,1>{{{.2,0,0}}});
    check(exactBrush.stamps.size()==3,"Don't duplicate step-aligned endpoint");
    RecordingBrush stationaryBrush;
    BrushStroke stationary(mesh,stationaryBrush,.1);
    stationary.start({{{0,0,0},1,.3}});
    stationary.finish(std::array<Vec3,1>{{{0,0,0}}});
    check(stationaryBrush.stamps.size()==1,"Don't duplicate unchanged endpoint");
}
void testInflate() {
    Mesh triangle{{{0,0,0},{1,0,0},{0,1,0}},{{0,1,2}}};
    InflateBrush brush(.2);
    BrushStroke stroke(triangle,brush,.05);
    stroke.start({{{0,0,0},.1,.3}});
    stroke.finish(std::array<Vec3,1>{{{0,0,0}}});
    near(triangle.vertices[0].z,.2,1e-14,"Inflate along incident face normal");
    near(triangle.vertices[1].z,0,1e-14,"No displacement outside footprint");
    near(triangle.vertices[2].z,0,1e-14,"No displacement outside footprint");
}
// Regression: wider overlapping symmetric strokes previously folded triangles
// through the reflection plane despite both halves sharing seam indices.
// Stroke-start normals avoid positive feedback from repeated normal updates.
void testWideSymmetricInflate() {
    SphereOptions options;
    options.vertexCount = 5000;
    auto mesh = generateSphere(options);
    const auto before = mesh.vertices;
    const std::size_t stamps = inflateHalfCircleDemo(mesh, .02, .012, 128, .4);
    check(stamps == 105, "Wide brush has predictable stamp count");
    check(validateSphere(mesh), "Wide symmetric Inflate must preserve outward faces");

    constexpr double pi = 3.14159265358979323846;
    const double spacing = std::sqrt(8*pi/(double(options.vertexCount)*std::sqrt(3.0)));
    auto m = static_cast<std::uint32_t>(std::floor(2*pi/spacing)) + 1;
    if ((options.vertexCount-m)%2) ++m;
    const auto p = (options.vertexCount-m)/2;

    // The seam consists of exactly m shared vertices; no mirrored copies.
    // Both sides' triangles must index that same ring, even after sculpting.
    std::vector<unsigned> positiveCount(m,0), negativeCount(m,0);
    for (const auto& t : mesh.triangles) {
        const bool pos = t.a < p || t.b < p || t.c < p;
        const bool neg = t.a >= p+m || t.b >= p+m || t.c >= p+m;
        check(!(pos && neg), "Hemisphere triangles meet only at the shared seam");
        const auto count = [&](std::uint32_t i) {
            if (i >= p && i < p+m) {
                if (pos) ++positiveCount[i-p];
                if (neg) ++negativeCount[i-p];
            }
        };
        count(t.a); count(t.b); count(t.c);
    }
    for (std::uint32_t i=0;i<m;++i) {
        const auto& vertex = mesh.vertices[p+i];
        check(std::abs(vertex.x) < 1e-11, "Shared seam remains in symmetry plane");
        check(positiveCount[i] > 0 && negativeCount[i] > 0,
              "Both hemispheres must reference the same seam vertex");
    }
    unsigned moved = 0;
    for (std::uint32_t i=0;i<p;++i) {
        const auto a=mesh.vertices[i], b=mesh.vertices[i+p+m];
        check(a.x > 0 && b.x < 0, "Inflated hemisphere vertex must not cross seam");
        check(std::abs(a.x+b.x)<1e-9 && std::abs(a.y-b.y)<1e-9 &&
              std::abs(a.z-b.z)<1e-9, "Wider stroke must retain exact symmetry");
        if (length(a-before[i])>1e-5) ++moved;
    }
    check(moved > 100, "Wide brush should visibly change the sphere");
    std::cout << "Wide symmetric Inflate: " << stamps << " stamps, "
              << moved << " moved positive-side vertices; manifold intact\\n";
}

void testDemo() {
    SphereOptions options;
    options.vertexCount=800; options.relaxationIterations=20;
    auto mesh=generateSphere(options);
    const auto before=mesh.vertices;
    auto count=inflateHalfCircleDemo(mesh);
    check(count>50 && count<150,"Demo stamps the curve at fixed Step");
    check(validateSphere(mesh),"Inflated sphere remains manifold and outward wound");
    check(mesh.vertices.size()==before.size(),"Inflate doesn't add vertices");
    double maxDelta=0;
    std::size_t displaced=0;
    for(std::size_t i=0;i<mesh.vertices.size();++i) {
        const double d=length(mesh.vertices[i]-before[i]);
        maxDelta=std::max(maxDelta,d);
        if(d>1e-7) ++displaced;
    }
    check(maxDelta>.01 && displaced>10 && displaced<mesh.vertices.size()/2,
          "Demo inflates a visible, localized region");
    constexpr double pi=3.14159265358979323846;
    const double spacing=std::sqrt(8*pi/(double(options.vertexCount)*std::sqrt(3.0)));
    auto m=static_cast<std::uint32_t>(std::floor(2*pi/spacing))+1;
    if((options.vertexCount-m)%2) ++m;
    const auto p=(options.vertexCount-m)/2;
    for(std::uint32_t i=0;i<p;++i){
        const auto a=mesh.vertices[i],b=mesh.vertices[i+p+m];
        check(std::abs(a.x+b.x)<1e-9 && std::abs(a.y-b.y)<1e-9 &&
              std::abs(a.z-b.z)<1e-9,"Inflate preserves reflection symmetry");
    }
    std::cout<<"Inflate demo: "<<count<<" stamps, "<<displaced
             <<" moved vertices; maximum displacement "<<maxDelta<<"\n";
}
}
int main() {
    try { testFalloff();testSampling();testInflate();testDemo();testWideSymmetricInflate(); }
    catch(const std::exception& e){std::cerr<<"Brush test failed: "<<e.what()<<'\n';return 1;}
    std::cout<<"Brush tests passed\n";
}
