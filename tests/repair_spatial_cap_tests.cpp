#include "mesher/repair_cap_spatial.hpp"
#include "mesher/repair_surface_profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;
namespace {
void expect(bool ok,const char* why) {
    if(!ok)throw std::runtime_error(why);
}
void validate(const std::vector<Vec3>& rim,const SpatialCapPatch& patch) {
    const bool correctShape=patch.usedMirroredRibbonFallback?
        (patch.positions.size()==rim.size()+(rim.size()-2)/2 &&
         patch.faces.size()==2*rim.size()-4&&
         patch.poissonIterations==0):
        (patch.positions.size()==2*rim.size()+1 &&
         patch.faces.size()==3*rim.size()&&
         patch.poissonIterations>0&&
         patch.poissonIterations<=SpatialCapSettings{}.maxPoissonIterations);
    expect(patch.boundaryCount==rim.size()&&correctShape&&
           patch.minimumQuality>1e-8,
           "R06-M06C front must have bounded new vertices and nondegenerate triangles");
    for(std::size_t i=0;i<rim.size();++i)
        expect(patch.positions[i].x==rim[i].x&&
               patch.positions[i].y==rim[i].y&&
               patch.positions[i].z==rim[i].z,
               "R06-M06C Poisson must fix exact input boundary positions");
}
void smoothRoundAndCurved() {
    for(const auto warped:{false,true}) {
        std::vector<Vec3> ring;
        std::vector<Vec3> normals;
        const auto pi=std::acos(-1.0);
        for(unsigned i=0;i<12;++i) {
            const auto angle=2*pi*i/12;
            ring.push_back({std::cos(angle),std::sin(angle),
                            warped?.23*std::sin(2*angle):0});
            normals.push_back({0,0,1});
        }
        const auto patch=buildSpatialCapFrontPoisson(ring,normals);
        validate(ring,patch);
        const auto repeated=buildSpatialCapFrontPoisson(ring,normals);
        expect(repeated.poissonIterations==patch.poissonIterations&&
               repeated.positions.back().x==patch.positions.back().x&&
               repeated.positions.back().y==patch.positions.back().y&&
               repeated.positions.back().z==patch.positions.back().z,
               "R06-M06C front and Poisson must be deterministic");
        std::cout<<"[r06-m06c] round/curved front valid: warped="<<warped
                 <<" q="<<patch.minimumQuality
                 <<" interior="<<patch.positions.size()-ring.size()
                 <<" Poisson="<<patch.poissonIterations<<" PASS\\n";
    }
}
std::vector<Vec3> recordedLongRim() {
    // Exact full-precision 32-vertex rim from physical Linux R06 failure.
    // Survivor geometry is not included, so this tests ONLY local patch
    // validity/refusal, never certification against the original mesh.
    return {
        {-0.044151264036858894,-1.1566671061619931,0.033831108615597939},
        {-0.025020214881532169,-1.2140064560635562,0.067774438014291657},
        {-0.0092765259247567476,-1.2248951894726039,0.10525700115887858},
        {-0.024077205210496756,-1.2686991067438909,0.098619863553432052},
        {-0.016219238682046236,-1.2731321674922571,0.14854181575358275},
        {-0.028790053100618572,-1.3122371649832583,0.15122391950066089},
        {-0.025212909551207915,-1.3113300857473054,0.21024967931402777},
        {-0.016267670816768193,-1.2753676388142514,0.25146619547902532},
        {-0.022803986448504749,-1.2235916210536715,0.30110164317607024},
        {-0.023537364375798557,-1.1699891809768284,0.29836385623217943},
        {-0.043250118469480382,-1.108517632055779,0.30068669839189471},
        {-0.037444613667644122,-1.0406824891893676,0.26181842272057526},
        {-0.017530590828567186,-1.0611120926622888,0.23397922857123479},
        {0,-1.0156501440481969,0.23954503655819334},
        {0.017530590828567186,-1.0611120926622888,0.23397922857123479},
        {0.037444613667644122,-1.0406824891893676,0.26181842272057526},
        {0.043250118469480382,-1.108517632055779,0.30068669839189471},
        {0.023537364375798557,-1.1699891809768284,0.29836385623217943},
        {0.022803986448504749,-1.2235916210536715,0.30110164317607024},
        {0.016267670816768193,-1.2753676388142514,0.25146619547902532},
        {0.025212909551207915,-1.3113300857473054,0.21024967931402777},
        {0.028790053100618572,-1.3122371649832583,0.15122391950066089},
        {0.016219238682046236,-1.2731321674922571,0.14854181575358275},
        {0.024077205210496756,-1.2686991067438909,0.098619863553432052},
        {0.0092765259247567476,-1.2248951894726039,0.10525700115887858},
        {0.025020214881532169,-1.2140064560635562,0.067774438014291657},
        {0.044151264036858894,-1.1566671061619931,0.033831108615597939},
        {0.061645399565073658,-1.1176940755719573,0.014313116847271339},
        {0.028798387747083026,-1.0592468994194197,0.026283990878392168},
        {0,-1.061114542337765,0.057831451433854104},
        {-0.028798387747083026,-1.0592468994194197,0.026283990878392168},
        {-0.061645399565073658,-1.1176940755719573,0.014313116847271339}
    };
}
void physicalLongRimSafety() {
    auto rim=recordedLongRim();
    try {
        const auto patch=buildSpatialCapFrontPoisson(rim,{});
        validate(rim,patch);
        std::cout<<"[r06-m06c] recorded 32-vertex rim returned self-checked candidate q="
                 <<patch.minimumQuality<<" (source collision audit still required) PASS\\n";
    }catch(const std::logic_error& e) {
        // An attempted nonprojective patch may be non-embeddable; the
        // safety contract requires refusing instead of returning one.
        std::cout<<"[r06-m06c] recorded physical folded rim conservatively refused: "
                 <<e.what()<<" PASS\\n";
    }
}
void recordedCurved18Safety() {
    // One complete physically successful 18-vertex R05 rim: fallback
    // must be independently safe but ordinary R05 remains preferred.
    const std::vector<Vec3> rim{
        {-0.20744398040584708,-1.4301080106025026,0.42951062798596357},
        {-0.18532103945703621,-1.4583262197270039,0.39111468388194998},
        {-0.20268407864335228,-1.5307148040232426,0.37504879024152765},
        {-0.26250093724793172,-1.5605766782038488,0.39596784954887393},
        {-0.30616806718364842,-1.5775324488771296,0.39579699554459757},
        {-0.35094142050947119,-1.5866100485676875,0.3902685856012269},
        {-0.39348415350250099,-1.5420188815350961,0.41514678801956162},
        {-0.43672074314190662,-1.4892528720084799,0.42021727855606211},
        {-0.40855006256271537,-1.455489319344017,0.44076514219381568},
        {-0.38183441978949229,-1.4147885626944283,0.46947190420240092},
        {-0.39056662513373031,-1.4827850515159398,0.50271158490677392},
        {-0.37159740496998767,-1.5312131097977533,0.48721500584810007},
        {-0.35378470224206671,-1.5783402151739789,0.48126275886970227},
        {-0.30978348927073263,-1.5495750377470652,0.45549542389071845},
        {-0.23900196324941136,-1.5203686819847453,0.443312696461999},
        {-0.2042009660610915,-1.5005539175077962,0.44760669843460865},
        {-0.18508714622169492,-1.4567769634794432,0.45631721362348321},
        {-0.20229625484781158,-1.433846864913523,0.43952577269284793}
    };
    try {
        const auto patch=buildSpatialCapFrontPoisson(rim,{});
        validate(rim,patch);
        std::cout<<"[r06-m06c] recorded curved 18-vertex rim: audited local patch PASS\\n";
    }catch(const std::logic_error& e) {
        std::cout<<"[r06-m06c] recorded curved 18-vertex rim: conservative front refusal "
                 <<e.what()<<" PASS\\n";
    }
}
void crossingRimMustRefuse() {
    const std::vector<Vec3> crossing{
        {-1,-1,0},{1,1,0},{-1,1,0},{1,-1,0}
    };
    bool refused=false;
    try{(void)buildSpatialCapFrontPoisson(crossing,{});}
    catch(const std::logic_error&){refused=true;}
    expect(refused,
           "R06-M06C projected crossing ring must not escape 3D self-intersection certificate");
}
void mirroredSingleRimContract() {
    const std::vector<Vec3> rim{
        {0,1,0},{.5,.86,.03},{.86,.5,.02},{1,0,0},
        {.86,-.5,-.01},{.5,-.86,0},{0,-1,0},
        {-.5,-.86,0},{-.86,-.5,-.01},{-1,0,0},
        {-.86,.5,.02},{-.5,.86,.03}
    };
    SpatialCapSettings symmetric;
    symmetric.requireMirrorPlane=true;
    const std::vector<Vec3> normals(rim.size(),{0,0,1});
    const auto result=buildSpatialCapFrontPoisson(rim,normals,symmetric);
    validate(rim,result);
    for(std::size_t i=0;i<rim.size();++i) {
        const auto& a=result.positions[rim.size()+i];
        const auto& b=result.positions[rim.size()+
            (i==0?0:rim.size()-i)];
        expect(a.x==-b.x&&a.y==b.y&&a.z==b.z,
               "R06-M06C4c normal-guided spatial solver must preserve exact mirrored interior vertex orbits");
    }
    expect(result.positions.back().x==0,
           "R06-M06C4c hub must be a real self-mirrored seam vertex");
    auto optimizedSettings=symmetric;
    optimizedSettings.repairTangentialOvershoot=true;
    const auto optimized=buildSpatialCapFrontPoisson(
        rim,normals,optimizedSettings);
    validate(rim,optimized);
    expect(optimized.minimumQuality+1e-12>=result.minimumQuality,
           "M17D opt-in cannot worsen an already valid reflected cap");
    for(std::size_t i=0;i<rim.size();++i) {
        const auto& a=optimized.positions[rim.size()+i];
        const auto& b=optimized.positions[rim.size()+
            (i==0?0:rim.size()-i)];
        expect(a.x==-b.x&&a.y==b.y&&a.z==b.z,
            "M17D corrected inward vertices preserve exact mirrored Poisson ring orbits");
    }
    auto damaged=rim;
    damaged[2].y+=.001;
    bool refused=false;
    try{(void)buildSpatialCapFrontPoisson(damaged,normals,symmetric);}
    catch(const std::logic_error&){refused=true;}
    expect(refused,"R06-M06C4c tampered reflected contour must refuse without coordinate welding");
    const std::vector<Vec3> noseam{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
    refused=false;
    try{(void)buildSpatialCapFrontPoisson(noseam,{},symmetric);}
    catch(const std::logic_error&){refused=true;}
    expect(refused,
           "R06-M06C4c reflection-plane crossing edges without seam vertices must refuse");
    std::cout<<"[r06-m06c4c] reflected single-loop front has exact X=0 hub and mirrored Poisson orbits PASS\\n";
}
void physicalSymmetric26Rim() {
    // Literal 26-vertex oriented R03 boundary from physical mirrored
    // inflate collision (2026-10-22): M04B refused a CAP with q=.090671
    // after 512 splits. This is a LOCAL solver fixture because no full
    // surviving mesh/checkpoint was captured; production M04 audits
    // must still decide whether the cap is admissible.
    const std::vector<Vec3> ring{
        {-0.035558218186814691,-1.158413129359128,0.2911726175244933},
        {-0.040435085560260361,-1.2135152118297892,0.29263349994667226},
        {-0.044221935512649524,-1.2624673720861881,0.32055274835825498},
        {-0.023840231458855835,-1.2606970809338676,0.3871288396706245},
        {-0.011061777326833224,-1.2366855624113164,0.4442686687592175},
        {-0.020954006809025639,-1.2379322247827864,0.48151425180139268},
        {-0.024789723375292248,-1.1906001559645414,0.52895906197302911},
        {-0.029267806363676867,-1.1127800942111727,0.53480354245878148},
        {-0.018165866450970569,-1.095343006264528,0.50491758323216585},
        {-0.017267073154726802,-1.0526292333363849,0.48606232055192744},
        {0,-1.0386611813517306,0.4289508963713059},
        {0.017267073154726802,-1.0526292333363849,0.48606232055192744},
        {0.018165866450970569,-1.095343006264528,0.50491758323216585},
        {0.029267806363676867,-1.1127800942111727,0.53480354245878148},
        {0.024789723375292248,-1.1906001559645414,0.52895906197302911},
        {0.020954006809025639,-1.2379322247827864,0.48151425180139268},
        {0.011061777326833224,-1.2366855624113164,0.4442686687592175},
        {0.023840231458855835,-1.2606970809338676,0.3871288396706245},
        {0.044221935512649524,-1.2624673720861881,0.32055274835825498},
        {0.040435085560260361,-1.2135152118297892,0.29263349994667226},
        {0.035558218186814691,-1.158413129359128,0.2911726175244933},
        {0.016062221145499492,-1.0973825031392941,0.33474962949139725},
        {0.014942446870618688,-1.0619105344186832,0.33059499265926218},
        {0,-1.0551286880319248,0.38941064248518364},
        {-0.014942446870618688,-1.0619105344186832,0.33059499265926218},
        {-0.016062221145499492,-1.0973825031392941,0.33474962949139725}
    };
    const auto limits=[] {
        SpatialCapSettings l;
        l.requireMirrorPlane=true;
        return l;
    }();
    try {
        const auto patch=buildSpatialCapFrontPoisson(ring,{},limits);
        validate(ring,patch);
        expect(patch.positions.back().x==0,
               "R06-M06C4c physical mirrored rim cannot move Poisson hub off X=0");
        std::cout<<"[r06-m06c4c] physical 26-vertex rim produced exact mirrored local candidate q="
                 <<patch.minimumQuality<<" (whole-object M04 audit remains required) PASS\\n";
    }catch(const std::logic_error& e) {
        // Never claim an invalid cap passed. Fail-closed is acceptable
        // for the local spatial solver until mirror-paired quality
        // retriangulation and full survivor witnesses are available.
        std::cout<<"[r06-m06c4c] physical 26-vertex rim conservatively refused: "
                 <<e.what()<<" PASS\\n";
    }
}
void physicalSymmetric16Rim() {
    // User 2026-10-22 second physical test, whose M04B refinement had
    // no two-sided reflected tracked edge. Exact ordered R03 rim.
    const std::vector<Vec3> ring{
        {0,-1.1098760554323437,0.292600353358898},
        {-0.0081116620586424125,-1.170724542511,0.31365251819483636},
        {-0.019214601753264907,-1.2074127629937643,0.29346835954943445},
        {-0.016309809330932908,-1.2453679480475219,0.33532411102367443},
        {-0.0070403341737743054,-1.2127251584549916,0.39865015332831427},
        {-0.014320256447625093,-1.1790180130168149,0.44601365212029531},
        {-0.033240435511679108,-1.1364802172855437,0.48172135437799213},
        {-0.045889366327617531,-1.0757745130805672,0.48076082290019329},
        {0,-1.0633881626619526,0.41656010700256052},
        {0.045889366327617531,-1.0757745130805672,0.48076082290019329},
        {0.033240435511679108,-1.1364802172855437,0.48172135437799213},
        {0.014320256447625093,-1.1790180130168149,0.44601365212029531},
        {0.0070403341737743054,-1.2127251584549916,0.39865015332831427},
        {0.016309809330932908,-1.2453679480475219,0.33532411102367443},
        {0.019214601753264907,-1.2074127629937643,0.29346835954943445},
        {0.0081116620586424125,-1.170724542511,0.31365251819483636}
    };
    SpatialCapSettings limits;
    limits.requireMirrorPlane=true;
    try {
        const auto patch=buildSpatialCapFrontPoisson(ring,{},limits);
        validate(ring,patch);
        expect(patch.positions.back().x==0,
               "R06-M06C4c1 physical mirror front hub must lie on seam");
        std::cout<<"[r06-m06c4c1] physical 16-point symmetric rim produced"
                 <<" local reflected cap q="<<patch.minimumQuality
                 <<" (surrounding survivor audit not replayed) PASS\\n";
    }catch(const std::logic_error& error) {
        std::cout<<"[r06-m06c4c1] physical 16-point rim safely refused"
                 <<" at spatial front: "<<error.what()<<" PASS\\n";
    }
}

void physicalFoldedMirroredRims() {
    // Both exact R03 directed contours from the 2026-10-22 Linux physical
    // failures #1 and #3. The missing surviving-face normals and ~11K-face
    // sculpt mean this only certifies LOCAL self-embedding and mirror topology.
    const std::vector<std::vector<Vec3>> examples{
        {
            {-0.00571563211033545,-1.2035754110900068,0.33412043231885968},
            {-0.010910359959350479,-1.2494784377004791,0.37135150270675765},
            {-0.0042650354788905187,-1.2187476777266326,0.40397347071817702},
            {-0.0097296810535124848,-1.2413145426172396,0.43119455891605019},
            {-0.015581692562461613,-1.2051700552587983,0.47861588115732845},
            {-0.029204742260248863,-1.1754864475176601,0.51211675796614697},
            {-0.023134210876666932,-1.1355717949919484,0.49848393401167779},
            {0,-1.0858342243625969,0.46761069954695628},
            {0.023134210876666932,-1.1355717949919484,0.49848393401167779},
            {0.029204742260248863,-1.1754864475176601,0.51211675796614697},
            {0.015581692562461613,-1.2051700552587983,0.47861588115732845},
            {0.0097296810535124848,-1.2413145426172396,0.43119455891605019},
            {0.0042650354788905187,-1.2187476777266326,0.40397347071817702},
            {0.010910359959350479,-1.2494784377004791,0.37135150270675765},
            {0.00571563211033545,-1.2035754110900068,0.33412043231885968},
            {0,-1.1550817042834649,0.31321198480413742}
        },
        {
            {-0.018687475644495237,-1.6468542840620548,0.63311572703089936},
            {0,-1.6313878241783006,0.59839569451098551},
            {0.018687475644495237,-1.6468542840620548,0.63311572703089936},
            {0.040374401118989184,-1.6650423315910197,0.67353899759268809},
            {0.043078571123703398,-1.7086687120214255,0.68856323688704568},
            {0.050648872108918455,-1.7595262872494286,0.69996019309010693},
            {0.023445110770740241,-1.7880627159627558,0.64783257581411302},
            {0.019222342523619866,-1.8155047601860497,0.61336223718395511},
            {0.02169278603656645,-1.8381293636980689,0.5757901587431391},
            {0.012547399115915486,-1.8089989284069126,0.50883068126942921},
            {0.0023985385738196727,-1.7696600335199852,0.52441180917821772},
            {0,-1.7154179999421402,0.48325452843610073},
            {-0.0023985385738196727,-1.7696600335199852,0.52441180917821772},
            {-0.012547399115915486,-1.8089989284069126,0.50883068126942921},
            {-0.02169278603656645,-1.8381293636980689,0.5757901587431391},
            {-0.019222342523619866,-1.8155047601860497,0.61336223718395511},
            {-0.023445110770740241,-1.7880627159627558,0.64783257581411302},
            {-0.050648872108918455,-1.7595262872494286,0.69996019309010693},
            {-0.043078571123703398,-1.7086687120214255,0.68856323688704568},
            {-0.040374401118989184,-1.6650423315910197,0.67353899759268809}
        }
    };
    for(std::size_t test=0;test<examples.size();++test) {
        auto rim=examples[test];
        std::reverse(rim.begin(),rim.end()); // same cap-facing order as M04A
        SpatialCapSettings limits;
        limits.requireMirrorPlane=true;
        const auto patch=buildSpatialCapFrontPoisson(rim,{},limits);
        validate(rim,patch);
        for(const auto& face:patch.faces) {
            const auto a=patch.positions[face[0]].x;
            const auto b=patch.positions[face[1]].x;
            const auto c=patch.positions[face[2]].x;
            expect(!(std::min({a,b,c})<0&&std::max({a,b,c})>0),
                   "R06-M06C4f mirror ribbon may never straddle the reflection plane");
        }
        // Force ONLY the alternate locally to exercise its independent
        // intersection/orientation/size checks. Normal production still
        // prefers Poisson and selects this topology solely on self-contact.
        limits.preferMirroredRibbonForTesting=true;
        const auto ribbon=buildSpatialCapFrontPoisson(rim,{},limits);
        validate(rim,ribbon);
        expect(ribbon.usedMirroredRibbonFallback,
               "R06-M06C4f alternate mirror strip must actually be exercised");
        std::cout<<"[r06-m06c4f] physical mirrored folded rim "<<test
                 <<" Poisson q="<<patch.minimumQuality
                 <<" forced alternate q="<<ribbon.minimumQuality<<" PASS\n";
    }
}
void seamProfileDiagnostics() {
    // Three flat fan triangles: CAP edges are outgoing 0->1, 1->2, 2->0.
    // Source guidance is in incoming edge order; all three normals equal.
    SpatialCapPatch flat;
    flat.boundaryCount=3;
    flat.positions={{0,0,0},{1,0,0},{0,1,0},{.25,.25,0}};
    flat.faces={{0,1,3},{1,2,3},{2,0,3}};
    const std::vector<Vec3> normals(3,Vec3{0,0,1});
    const auto planar=measureSpatialCapRimProfile(flat,normals);
    expect(planar.edges.size()==3&&planar.measuredEdges==3&&
           planar.unavailableEdges==0&&
           planar.maximumAbsoluteDihedralRadians<1e-12&&
           planar.maximumAbsoluteNormalizedOffset<1e-12,
           "M18A planar cap/survivor should have zero boundary jump");

    // A nonplanar first inward vertex bends ALL three boundary transitions.
    // Signed normal deviation around the first rim edge should be +45deg.
    auto folded=flat;
    folded.positions.back().z=.25;
    const auto bent=measureSpatialCapRimProfile(folded,normals);
    expect(bent.measuredEdges==3&&
           std::abs(bent.edges[0].signedDihedralRadians-
                    std::acos(-1.)/4)<1e-12&&
           bent.edges[0].normalizedFirstRingNormalOffset>.24&&
           std::abs(bent.edges[0].oppositeFootParameter-.25)<1e-12&&
           bent.maximumAbsoluteDihedralRadians>planar.maximumAbsoluteDihedralRadians,
           "M18A signed boundary dihedral must detect a folded first inward ring");

    // The survivor normal for outgoing CAP edge 0->1 is guidance[1],
    // NOT guidance[0]; this catches the reversed-R03 off-by-one error.
    auto incoming=normals;
    incoming[1]={0,0,-1};
    const auto ordered=measureSpatialCapRimProfile(flat,incoming);
    expect(std::abs(ordered.edges[0].absoluteDihedralRadians-
                    std::acos(-1.))<1e-12&&
           ordered.edges[1].absoluteDihedralRadians<1e-12,
           "M18A must associate each cap edge with its exact incoming survivor");

    const auto missing=measureSpatialCapRimProfile(flat,{});
    expect(missing.measuredEdges==0&&missing.unavailableEdges==3&&
           !missing.edges[0].hasSurvivorNormal,
           "M18A missing survivor guidance must be reported, not invented");
    incoming=normals;
    incoming[1]={0,0,0};
    const auto incomplete=measureSpatialCapRimProfile(flat,incoming);
    expect(incomplete.measuredEdges==2&&incomplete.unavailableEdges==1&&
           !incomplete.edges[0].hasSurvivorNormal,
           "M18A degenerate source normal must be unavailable");

    // Reflection + reversed winding preserves absolute edge dihedrals.
    SpatialCapPatch reflected;
    reflected.boundaryCount=3;
    reflected.positions={{0,0,0},{0,1,0},{-1,0,0},{-.25,.25,.25}};
    reflected.faces={{0,1,3},{1,2,3},{2,0,3}};
    const auto mirror=measureSpatialCapRimProfile(reflected,normals);
    expect(mirror.measuredEdges==3&&
           std::abs(mirror.maximumAbsoluteDihedralRadians-
                    bent.maximumAbsoluteDihedralRadians)<1e-12,
           "M18A absolute dihedral must be invariant under exact reflected geometry");

    auto reversed=flat;
    reversed.faces[0]={1,0,3};
    bool refused=false;
    try {(void)measureSpatialCapRimProfile(reversed,normals);}
    catch(const std::logic_error&) {refused=true;}
    expect(refused,"M18A must never match a reversed/nonexistent directed rim face");

    const auto again=measureSpatialCapRimProfile(folded,normals);
    expect(again.measuredEdges==bent.measuredEdges&&
           again.edges[0].signedDihedralRadians==bent.edges[0].signedDihedralRadians,
           "M18A pure diagnostics must be deterministic");
    std::cout<<"[r06-m18a] planar, folded, incoming-edge, missing-normal,"
                " reflected, reversed-winding and deterministic profiles PASS\\n";
}
void finiteRefusals() {
    std::vector<Vec3> ring;
    for(unsigned i=0;i<8;++i) {
        const auto angle=2*std::acos(-1.0)*i/8;
        ring.push_back({std::cos(angle),std::sin(angle),0});
    }
    auto limited=SpatialCapSettings{};
    limited.maxIntersectionTests=1;
    bool refused=false;
    try{(void)buildSpatialCapFrontPoisson(ring,{},limited);}
    catch(const std::length_error&){refused=true;}
    expect(refused,"R06-M06C requires a real finite collision-work budget");
    limited=SpatialCapSettings{};
    limited.maxPoissonIterations=1;
    refused=false;
    try{(void)buildSpatialCapFrontPoisson(ring,{},limited);}
    catch(const std::logic_error&){refused=true;}
    expect(refused,"R06-M06C must refuse unfinished Poisson solver");
}
}
int main() {
    try {
        smoothRoundAndCurved();
        mirroredSingleRimContract();
        physicalSymmetric26Rim();
        physicalSymmetric16Rim();
        physicalFoldedMirroredRims();
        physicalLongRimSafety();
        recordedCurved18Safety();
        crossingRimMustRefuse();
        seamProfileDiagnostics();
        finiteRefusals();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M06C spatial front/Poisson FAILED: "<<e.what()<<"\\n";
        return 1;
    }
}
