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
void testFourDimensionalStroke() {
    Mesh dummy;
    RecordingBrush brush;
    BrushStroke hold(dummy,brush,.1);
    hold.startTimed({{{0,0,0},1,.3},{{0,2,0},1,.3}},.2);
    const std::array<Vec3,2> positions{{{0,0,0},{0,2,0}}};
    hold.updateTimed(positions,.25); // half a Step in time
    check(brush.stamps.size()==1,"No premature stationary-time stamp");
    hold.updateTimed(positions,.75); // one time-based stamp at 0.5 s
    check(brush.stamps.size()==2,"A stationary pointer still stamps");
    hold.updateTimed(positions,1.0);
    check(brush.stamps.size()==3,"Time overshoot carries across updates");
    hold.finishTimed(positions,1.0);
    check(brush.stamps.size()==3,"No duplicate terminal stamp at exact Step");
    check(!hold.active(),"Timed stroke closes");

    RecordingBrush movingBrush;
    BrushStroke moving(dummy,movingBrush,.1);
    moving.startTimed({{{0,0,0},1,.3},{{0,2,0},1,.3}},.4);
    const std::array<Vec3,2> moved{{{.3,0,0},{.3,2,0}}};
    moving.updateTimed(moved,1.0); // hypot(.3,.4)=.5, five steps
    check(movingBrush.stamps.size()==6,"4D path combines spatial and time distances");
    near(movingBrush.stamps[1][0].x,.06,1e-12,
         "Time-spatial path samples interpolated position");
    near(movingBrush.stamps[5][1].x,.3,1e-12,"Equal mirrored travel");
    moving.finishTimed(moved,1.0);
    check(movingBrush.stamps.size()==6,"No terminal duplicate when aligned in 4D");

    RecordingBrush errorBrush;
    BrushStroke bad(dummy,errorBrush,.1);
    bad.startTimed({{{0,0,0},1,.3},{{0,2,0},1,.3}},.2);
    const std::array<Vec3,2> uneven{{{.1,0,0},{.2,2,0}}};
    bool rejected=false;
    try{bad.updateTimed(uneven,1.0);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&errorBrush.stamps.size()==1,
          "Unequal 4D travel rejected before any stamp");
    rejected=false;
    try{bad.updateTimed(positions,-1);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Negative time rejected");
    bad.finishTimed(positions,.25);
    check(errorBrush.stamps.size()==2,
          "Partial final 4D Step applies one terminal stamp");

    bool invalid=false;
    try{
        RecordingBrush unused;
        BrushStroke reject(dummy,unused,.1);
        reject.startTimed({{{0,0,0},1,.3}},0);
    }catch(const std::invalid_argument&){invalid=true;}
    check(invalid,"Time speed must be positive");
}
void testCurvatureCorrection() {
    // Four planar triangles forming a shallow bowl. Their shared center
    // has negative signed mean curvature and should rise more rapidly.
    auto fan=[](double middleZ) {
        return Mesh{
            {{0,0,middleZ},{1,0,0},{0,1,0},{-1,0,0},{0,-1,0}},
            {{0,1,2},{0,2,3},{0,3,4},{0,4,1}}
        };
    };
    auto stamp=[&](Mesh& mesh,CurvatureSettings settings) {
        InflateBrush brush(.01,settings);
        brush.begin(mesh);
        const BrushArea centered{{0,0,mesh.vertices[0].z},.5,.3};
        brush.stamp(mesh,std::span<const BrushArea>(&centered,1));
        brush.end(mesh);
    };
    Mesh valley=fan(-.2), uncorrected=valley;
    stamp(valley,{true,1.0});stamp(uncorrected,{});
    check(valley.vertices[0].z > uncorrected.vertices[0].z + 1e-5,
          "Concave valley should rise more with curvature correction");
    Mesh dome=fan(.2), domePlain=dome;
    stamp(dome,{true,1.0});stamp(domePlain,{});
    check(dome.vertices[0].z < domePlain.vertices[0].z - 1e-5,
          "Convex region should rise less with curvature correction");
    Mesh flat=fan(0), flatPlain=flat;
    stamp(flat,{true,1.0});stamp(flatPlain,{});
    check(std::abs(flat.vertices[0].z-flatPlain.vertices[0].z)<1e-12,
          "Planar mesh has zero normal curvature correction");
    Mesh unchanged=fan(-.2), zeroGain=unchanged;
    stamp(unchanged,{});
    stamp(zeroGain,{true,0.0});
    check(std::abs(unchanged.vertices[0].z-zeroGain.vertices[0].z)<1e-12,
          "Zero curvature gain reproduces ordinary Inflate");
    bool rejected=false;
    try { InflateBrush invalid(.01,{true,-1.0}); }
    catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"Reject negative curvature gain");
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
    check(mesh.triangles.size() == 2 * mesh.vertices.size() - 4,
          "Inflate never changes mesh connectivity");

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
    unsigned moved = 0, crossed = 0;
    for (std::uint32_t i=0;i<p;++i) {
        const auto a=mesh.vertices[i], b=mesh.vertices[i+p+m];
        if (a.x <= 0 || b.x >= 0) ++crossed;
        check(std::abs(a.x+b.x)<1e-9 && std::abs(a.y-b.y)<1e-9 &&
              std::abs(a.z-b.z)<1e-9, "Wider stroke must retain exact symmetry");
        if (length(a-before[i])>1e-5) ++moved;
    }
    check(moved > 100, "Wide brush should visibly change the sphere");
    std::cout << "Wide symmetric Inflate: " << stamps << " stamps, "
              << moved << " moved positive-side vertices, "
              << crossed << " crossing symmetry plane\n";
}

void testAnimatedPlayback() {
    SphereOptions options;
    options.vertexCount = 500;
    options.relaxationIterations = 20;
    auto incremental = generateSphere(options);
    auto instant = incremental;
    const auto originals = incremental.vertices;
    InflateDemoAnimation animation(incremental, .02, .012, 128, .4);
    check(animation.completed()==0 && animation.total()>50,
          "Animation starts with original, unmodified sphere");
    check(animation.advance(), "First stamp exists");
    check(animation.completed()==1 && animation.total()>1,
          "Exactly one stamp processed");
    std::size_t changed = 0;
    for (std::size_t i=0;i<originals.size();++i)
        if (length(incremental.vertices[i]-originals[i])>1e-12) ++changed;
    check(changed>0, "First animated stamp changes actual geometry");
    animation.reset();
    check(animation.completed()==0, "Rewind returns to stamp zero");
    for (std::size_t i=0;i<originals.size();++i)
        check(length(incremental.vertices[i]-originals[i])==0,
              "Rewind restores exact original vertices");
    std::size_t calls=0;
    while(animation.advance()) ++calls;
    check(calls==animation.total(), "Each advance processes exactly one stamp");
    check(!animation.advance(), "Completed animation cannot stamp again");
    const auto synchronous = inflateHalfCircleDemo(instant, .02, .012, 128, .4);
    check(synchronous==calls, "Animated and synchronous stamp counts agree");
    for (std::size_t i=0;i<originals.size();++i)
        check(length(incremental.vertices[i]-instant.vertices[i])<1e-13,
              "Animated and synchronous geometry are identical");
}

void testDemo() {
    SphereOptions options;
    options.vertexCount=800; options.relaxationIterations=20;
    auto mesh=generateSphere(options);
    const auto before=mesh.vertices;
    auto count=inflateHalfCircleDemo(mesh);
    check(count>50 && count<150,"Demo stamps the curve at fixed Step");
    // With live normals, the large-radius experiment intentionally may fold
    // the surface. Index topology is unchanged; radial winding need not stay outward.
    check(mesh.vertices.size()==before.size(),"Inflate doesn't add vertices");
    double maxDelta=0;
    std::size_t displaced=0;
    for(std::size_t i=0;i<mesh.vertices.size();++i) {
        const double d=length(mesh.vertices[i]-before[i]);
        maxDelta=std::max(maxDelta,d);
        if(d>1e-7) ++displaced;
    }
    check(maxDelta>.01 && displaced>10,
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
    try { testFalloff();testSampling();testFourDimensionalStroke();testInflate();testCurvatureCorrection();testDemo();testWideSymmetricInflate();testAnimatedPlayback(); }
    catch(const std::exception& e){std::cerr<<"Brush test failed: "<<e.what()<<'\n';return 1;}
    std::cout<<"Brush tests passed\n";
}
