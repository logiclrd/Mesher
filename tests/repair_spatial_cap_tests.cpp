#include "mesher/repair_cap_spatial.hpp"

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
    expect(patch.boundaryCount==rim.size()&&
           patch.positions.size()==2*rim.size()+1 &&
           patch.faces.size()==3*rim.size()&&
           patch.poissonIterations>0 &&
           patch.poissonIterations<=SpatialCapSettings{}.maxPoissonIterations &&
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
    const std::vector<Vec3> crossing{{
        {-1,-1,0},{1,1,0},{-1,1,0},{1,-1,0}
    }};
    bool refused=false;
    try{(void)buildSpatialCapFrontPoisson(crossing,{});}
    catch(const std::logic_error&){refused=true;}
    expect(refused,
           "R06-M06C projected crossing ring must not escape 3D self-intersection certificate");
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
        physicalLongRimSafety();
        recordedCurved18Safety();
        crossingRimMustRefuse();
        finiteRefusals();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M06C spatial front/Poisson FAILED: "<<e.what()<<"\\n";
        return 1;
    }
}
