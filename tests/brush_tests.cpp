#include "mesher/brush.hpp"
#include "mesher/brush_radius.hpp"
#include "mesher/brush_radius_measure.hpp"
#include "mesher/mesh_partitioning.hpp"

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
void testMeasuredImportedBrush() {
    Mesh source{{{0,0,0},{2,0,0},{0,2,0},{0,0,3}},
                {{0,2,1},{0,1,3},{1,2,3},{2,0,3}}};
    auto partitioned=partitionFlatMesh(source,16);
    const auto index=RegionTopologyIndex::build(partitioned.revision);
    const double edge=medianImportedEdgeLength(*index);
    check(edge>=2&&edge<4,
          "Measured median comes from actual triangle edges");
    const auto unit=BrushRadiusRange::forVertexCount(5000);
    const auto fit=calibrateImportedBrush(*index,unit.minimum,1000);
    near(fit.radius.minimum,edge*.5,1e-12,
         "Smallest brush uses measured source triangle size");
    near(fit.radius.maximum,500,1e-12,
         "Largest brush may still span half the source bounds");
    const double start=relativeImportedBrushRadius(unit,.4,fit.radius);
    near(fit.workerScale*unit.minimum,edge,1e-12,
         "Inflate strength and 4D step use one generated-edge reference");
    check(start>=fit.radius.minimum&&start<fit.radius.maximum,
          "Opening imported mesh chooses locally useful brush radius");
    near(relativeImportedBrushRadius(fit.radius,start,fit.radius),
         start,1e-12,"Same local edge scale preserves existing brush width");
}
void testImportedBrushPhysicalScale() {
    const auto base=BrushRadiusRange::forVertexCount(19247);
    const auto large=BrushRadiusRange::forModelScale(19247,120.0);
    near(large.minimum,base.minimum*120,1e-12,
         "Imported nominal-edge radius follows original physical units");
    near(large.maximum,60.0,1e-12,
         "Imported maximum follows half the model bounding radius");
    const double oldRadius=base.fromSliderFraction(.2);
    const double preserved=large.fromSliderFraction(base.sliderFraction(oldRadius));
    near(preserved,oldRadius*120,1e-10,
         "Open preserves relative slider position across coordinate scales");
    check(preserved>large.minimum&&preserved>0.5,
          "Imported brush can cover vertices on a large source mesh");
    const auto tiny=BrushRadiusRange::forModelScale(19247,0.001);
    near(tiny.maximum,0.0005,1e-15,
         "Small imported mesh permits smaller-than-unit brush radius");
    const auto original=BrushRadiusRange::forModelScale(19247,1.0);
    near(original.minimum,base.minimum,0,
         "Unit sphere keeps the original nominal-edge minimum");
    near(original.maximum,base.maximum,0,
         "Unit sphere keeps the original brush maximum");
    try {
        (void)BrushRadiusRange::forModelScale(100,0);
        throw std::runtime_error("Zero physical scale incorrectly accepted");
    }catch(const std::invalid_argument&) {}
}
void testLargeImportedTriangleInflates() {
    // At an arbitrary point inside a large imported triangle the closest
    // vertex can be tens of source units away. A physically scaled brush
    // must actually influence the mesh, not silently stamp zero vertices.
    Mesh patch{{{0,0,0},{60,0,0},{0,60,0}},{{0,1,2}}};
    const double physicalScale=120.0;
    const auto range=BrushRadiusRange::forModelScale(19247,physicalScale);
    const double radius=range.fromSliderFraction(.2);
    InflateBrush brush(.012*physicalScale);
    BrushStroke stroke(patch,brush,.02*physicalScale);
    stroke.start({{{20,20,0},radius,.3}});
    stroke.finish(std::array<Vec3,1>{{{20,20,0}}});
    check(patch.vertices[0].z>0.01 &&
          patch.vertices[1].z>0.0&&patch.vertices[2].z>0.0,
          "Imported-scale Inflate reaches actual vertices under large triangles");
}
void testRadiusSliderAndLiveStroke() {
    constexpr double pi=3.14159265358979323846;
    const auto range=BrushRadiusRange::forVertexCount(100000);
    const double expected=std::sqrt(8*pi/(100000.0*std::sqrt(3.0)));
    near(range.minimum,expected,1e-14,"Minimum brush radius matches nominal edge");
    near(range.maximum,.5,1e-14,"Maximum brush radius is half a model unit");
    near(range.fromSliderFraction(0),.5,1e-14,"Top of slider is maximum");
    near(range.fromSliderFraction(1),expected,1e-14,"Bottom is minimum");
    near(range.fromSliderFraction(.25),
         expected+(.5-expected)*.75,1e-14,"Slider interpolation");
    near(range.sliderFraction(range.fromSliderFraction(.27)),.27,1e-14,
         "Slider forward/backward mapping");
    near(range.clamp(.8),.5,1e-14,"Oversized configured brush is clamped");
    near(range.clamp(.001),expected,1e-14,"Undersized brush is clamped");
    check(BrushRadiusRange::forVertexCount(1).minimum==.5,
          "Very small initial sphere has degenerate but safe slider range");

    struct RadiusRecorder final : Brush {
        std::vector<std::vector<double>> radii;
        void stamp(Mesh&,std::span<const BrushArea> areas) override {
            std::vector<double> row;
            for(const auto& area:areas)row.push_back(area.radius);
            radii.push_back(std::move(row));
        }
    } recorder;
    Mesh mesh;
    BrushStroke stroke(mesh,recorder,.02);
    stroke.startTimed({{{0,0,0},.4,.3},{{0,2,0},.4,.3}},.2);
    stroke.setRadius(.1);
    const std::array<Vec3,2> locations{{{0,0,0},{0,2,0}}};
    stroke.updateTimedRealtime(locations,.1);
    check(recorder.radii.size()==1,"Realtime input does not stamp until pumped");
    check(stroke.pumpTimedRealtime(),"One queued Step applies one stamp");
    check(recorder.radii.size()==2,"Stationary 4D Step produces one new stamp");
    near(recorder.radii[0][0],.4,1e-14,"Start uses configured radius");
    near(recorder.radii[1][0],.1,1e-14,"Slider modifies subsequent stamp");
    near(recorder.radii[1][1],.1,1e-14,"Both mirrored areas change radius");
    stroke.finishTimedRealtime(locations,.1);
    check(stroke.active(),"Mouse release retains queued stroke until drained");
    check(!stroke.pumpTimedRealtime(),"Exact Step terminal does not duplicate");
    check(!stroke.active(),"Stroke completes after radius change");
}
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
void testQueuedInputStepTelemetry() {
    Mesh dummy;
    RecordingBrush brush;
    BrushStroke stroke(dummy,brush,.1);
    stroke.startTimed({{{0,0,0},1,.3}},.2);
    check(stroke.totalQueuedSteps()==0,"Initial stamp is not queued input");
    const std::array<Vec3,1> a{{{.02,0,0}}},
                             b{{{.06,0,0}}},
                             c{{{.16,0,0}}};
    stroke.updateTimedRealtime(a,.1);
    check(stroke.totalQueuedSteps()==0 &&
          stroke.pendingSegments()==1,
          "A sub-Step input must be queued without declaring a new Step");
    stroke.updateTimedRealtime(b,.2);
    check(stroke.totalQueuedSteps()==0,
          "Partial Step length is accumulated across inputs");
    stroke.updateTimedRealtime(c,.5);
    // hypot(.02,.02)+hypot(.04,.02)+hypot(.10,.06) > .1 and < .2
    check(stroke.totalQueuedSteps()==1 && brush.stamps.size()==1,
          "Exactly one Step is scheduled without stamping the mesh");
    check(stroke.pumpTimedRealtime(),"One queued Step renders");
    check(stroke.totalQueuedSteps()==1,
          "Rendering must not decrease total accepted input Steps");
    const std::array<Vec3,1> d{{{.22,0,0}}};
    stroke.updateTimedRealtime(d,.7);
    check(stroke.totalQueuedSteps()==2,
          "A later mouse movement reports only newly scheduled Steps");
    const auto scheduled=stroke.totalQueuedSteps();
    stroke.finishTimedRealtime(d,.7);
    check(stroke.totalQueuedSteps()==scheduled,
          "Mouse release at the same position adds no spurious Step");
    for(std::size_t i=0;stroke.active()&&i<20;++i)
        (void)stroke.pumpTimedRealtime();
    check(!stroke.active()&&brush.ends==1,
          "Queued input must finish after rendering all scheduled Steps");
    check(stroke.totalQueuedSteps()==scheduled,
          "Total accepted Step count remains readable after draining");

    RecordingBrush held;
    BrushStroke idle(dummy,held,.1);
    idle.startTimed({{{0,0,0},1,.3}},.2);
    const std::array<Vec3,1> still{{{0,0,0}}};
    idle.updateTimedRealtime(still,.49);
    check(idle.totalQueuedSteps()==0,
          "Elapsed time just below Step does not count");
    idle.updateTimedRealtime(still,.5);
    check(idle.totalQueuedSteps()==1,
          "Stationary time crossing Step is reported as queued input");
    idle.finishTimedRealtime(still,.5);
    check(idle.pumpTimedRealtime(),"Stationary queued Step renders");
    check(!idle.pumpTimedRealtime() && !idle.active(),
          "Stationary release drains without duplicate stamp");
}
void testRealtimeSamplingBound() {
    Mesh dummy;
    RecordingBrush exactBrush,liveBrush;
    BrushStroke exact(dummy,exactBrush,.1);
    BrushStroke live(dummy,liveBrush,.1);
    const std::vector<BrushArea> areas{{{0,0,0},1,.3},{{0,2,0},1,.3}};
    exact.startTimed(areas,.4);
    live.startTimed(areas,.4);
    const std::array<Vec3,2> far{{{.3,0,0},{.3,2,0}}};
    exact.updateTimed(far,1.0); // five exact grid samples in one update
    live.updateTimedRealtime(far,1.0);
    check(liveBrush.stamps.size()==1 && live.pendingSegments()==1,
          "Long interactive input queues the segment without stamping");
    check(live.pendingSamples()>=5,"Backlog reports all unprocessed 4D Steps");
    for(int i=1;i<=5;++i) {
        check(live.pumpTimedRealtime(),"Pending 4D Step produces one stamp");
        check(liveBrush.stamps.size()==std::size_t(i+1),
              "Only one stamp can be processed per frame");
        near(liveBrush.stamps.back()[0].x,.06*i,1e-12,
             "Interactive path must not jump to newest position");
    }
    check(!live.pumpTimedRealtime(),"No pending stamp at aligned endpoint");
    check(live.skippedStamps()==0,"Exact realtime path never skips stamps");
    const std::array<Vec3,2> still{{{.3,0,0},{.3,2,0}}};
    exact.updateTimed(still,1.125);
    live.updateTimedRealtime(still,1.125);
    check(!live.pumpTimedRealtime(),"Sub-Step time accumulates with no stamp");
    exact.updateTimed(still,1.25);
    live.updateTimedRealtime(still,1.25);
    check(live.pumpTimedRealtime(),"Stationary time completes next Step");
    exact.finishTimed(still,1.25);
    live.finishTimedRealtime(still,1.25);
    check(live.active(),"Input release is not early stroke completion");
    check(!live.pumpTimedRealtime(),"Aligned release closes without duplicate");
    check(!live.active() && liveBrush.ends==1,"Queued release ends brush once");
    check(exactBrush.stamps.size()==liveBrush.stamps.size(),
          "Exact offline and realtime stamp counts agree");
    for(std::size_t i=0;i<exactBrush.stamps.size();++i)
        for(std::size_t j=0;j<areas.size();++j)
            near(length(exactBrush.stamps[i][j]-liveBrush.stamps[i][j]),0,1e-12,
                 "Exact offline and realtime positions agree");

    // Arbitrary sub-Step segments joined at corners: the realtime queue
    // must preserve the polyline, not shortcut to the latest cursor sample.
    RecordingBrush offlineCorners,liveCorners;
    BrushStroke offline(dummy,offlineCorners,.03),queue(dummy,liveCorners,.03);
    offline.startTimed({{{0,0,0},1,.3}},.15);
    queue.startTimed({{{0,0,0},1,.3}},.15);
    const std::array<Vec3,1> a{{{.011,0,0}}},b{{{.022,0,0}}},
                             c{{{.022,.12,0}}},d{{{.10,.12,0}}},
                             e{{{.10,.14,0}}};
    offline.updateTimed(a,.01);
    offline.updateTimed(b,.02);
    offline.updateTimed(c,.30);
    offline.updateTimed(d,.32);
    offline.finishTimed(e,.40);
    queue.updateTimedRealtime(a,.01);
    queue.updateTimedRealtime(b,.02);
    queue.updateTimedRealtime(c,.30);
    queue.updateTimedRealtime(d,.32);
    queue.finishTimedRealtime(e,.40);
    check(liveCorners.stamps.size()==1 && queue.pendingSegments()==5,
          "All polyline input segments are queued before stamping");
    std::size_t frames=0;
    while(queue.active() && frames++<100) {
        const auto previous=liveCorners.stamps.size();
        const bool stamped=queue.pumpTimedRealtime();
        check(liveCorners.stamps.size()-previous<=1,
              "Queued stroke never batches stamps between renders");
        check(stamped==(liveCorners.stamps.size()>previous),
              "Pump result describes exactly one applied stamp");
    }
    check(!queue.active() && liveCorners.ends==1 && frames<100,
          "Release drains all queued segments, then ends stroke");
    check(liveCorners.stamps.size()==offlineCorners.stamps.size(),
          "Zigzag stroke retains offline sample count");
    for(std::size_t i=0;i<offlineCorners.stamps.size();++i)
        near(length(offlineCorners.stamps[i][0]-liveCorners.stamps[i][0]),
             0,1e-12,"Zigzag stroke retains exact positions");

    RecordingBrush invalidBrush;
    BrushStroke invalid(dummy,invalidBrush,.1);
    invalid.startTimed({{{0,0,0},1,.3},{{0,2,0},1,.3}},.4);
    const std::array<Vec3,2> uneven{{{.2,0,0},{.3,2,0}}};
    bool rejected=false;
    try {invalid.updateTimedRealtime(uneven,.8);}
    catch(const std::invalid_argument&) {rejected=true;}
    check(rejected && invalidBrush.stamps.size()==1 &&
          invalid.pendingSegments()==0,
          "Invalid symmetric 4D update leaves queue unmodified");
    const std::array<Vec3,2> original{{{0,0,0},{0,2,0}}};
    invalid.finishTimedRealtime(original,0);
    check(!invalid.pumpTimedRealtime() && invalidBrush.ends==1,
          "Unchanged release closes without terminal duplicate");
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
    try { testMeasuredImportedBrush();testImportedBrushPhysicalScale();testLargeImportedTriangleInflates();testRadiusSliderAndLiveStroke();testFalloff();testSampling();testFourDimensionalStroke();testQueuedInputStepTelemetry();testRealtimeSamplingBound();testInflate();testCurvatureCorrection();testDemo();testWideSymmetricInflate();testAnimatedPlayback(); }
    catch(const std::exception& e){std::cerr<<"Brush test failed: "<<e.what()<<'\n';return 1;}
    std::cout<<"Brush tests passed\n";
    return 0;
}
