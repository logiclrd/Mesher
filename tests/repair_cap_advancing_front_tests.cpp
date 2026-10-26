#include "mesher/repair_cap_advancing_front.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;

namespace {

void require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}

AdvancingFrontWorkspace fourCorner(double angleDegrees) {
    const auto a=angleDegrees*std::acos(-1.)/180.;
    const Vec3 p{-2.+std::cos(a),std::sin(a),0};
    const Vec3 selected{-2,0,0};
    const Vec3 other{2,0,0};
    const Vec3 reflected{-p.x,p.y,p.z};
    AdvancingFrontWorkspace state;
    state.positions={p,selected,other,reflected};
    state.front={0,1,2,3};
    return state;
}

std::size_t mirrorId(const AdvancingFrontWorkspace& s,std::size_t i) {
    const auto p=s.positions[i];
    std::size_t id=s.positions.size();
    for(std::size_t j=0;j<s.positions.size();++j) {
        const auto q=s.positions[j];
        if(q.x==-p.x&&q.y==p.y&&q.z==p.z) {
            require(id==s.positions.size(),"M22A ambiguous exact mirror orbit");
            id=j;
        }
    }
    require(id<s.positions.size(),"M22A missing exact mirror orbit");
    return id;
}

bool cyclic(const std::array<std::uint32_t,3>& a,
            const std::array<std::uint32_t,3>& b) {
    for(unsigned k=0;k<3;++k)
        if(a[0]==b[k]&&a[1]==b[(k+1)%3]&&a[2]==b[(k+2)%3])
            return true;
    return false;
}

void verifyMirroredFaces(const AdvancingFrontWorkspace& s) {
    for(const auto& face:s.faces) {
        const std::array<std::uint32_t,3> partner{
            static_cast<std::uint32_t>(mirrorId(s,face[0])),
            static_cast<std::uint32_t>(mirrorId(s,face[2])),
            static_cast<std::uint32_t>(mirrorId(s,face[1]))};
        bool found=false;
        for(const auto& candidate:s.faces)
            if(cyclic(partner,candidate))found=true;
        require(found,"M22A reflected face must have exact inverse-wound partner");
    }
}

void checkThreeRules() {
    for(const auto angle:{30.,60.,110.}) {
        const auto initial=fourCorner(angle);
        const auto result=proposeMirroredBridgeFrontAdvance(
            initial,1,24,48,.1);
        const auto& s=result.workspace;
        const bool acute=angle<45.;
        const bool intermediate=angle>=45.&&angle<90.;
        const auto expectedFaces=acute?2U:(intermediate?4U:2U);
        require(result.addedVertices==(acute?1U:2U)&&
                result.addedFaces==expectedFaces,
                "M22A three mirror-bridge rules must emit prescribed vertices/faces");
        require(s.front.size()==(angle>=90.?5U:3U),
                "M22A three front transformations must preserve cyclic contour");
        require(s.positions[4].x==0&&
                (acute||s.positions[5].x==0),
                "M22A inserted vertices must be exactly on seam");
        verifyMirroredFaces(s);
        const auto repeat=proposeMirroredBridgeFrontAdvance(
            initial,1,24,48,.1);
        require(repeat.workspace.faces==s.faces&&
                repeat.workspace.front==s.front,
                "M22A operation must be deterministic");
        require(initial.positions.size()==4&&initial.faces.empty()&&
                initial.front.size()==4,
                "M22A input must remain bit-identical");
        std::cout<<"[r06-m22a] mirrored bridge angle="<<angle
                 <<" faces="<<result.addedFaces<<" PASS\n";
    }
}

void cyclicWrapAndGeneratedIncidence() {
    auto state=fourCorner(60.);
    state.front={2,3,0,1}; // A at final array index, B at first
    const auto wrap=proposeMirroredBridgeFrontAdvance(state,3,24,48,.1);
    require(wrap.workspace.front.size()==3&&
            wrap.workspace.front.front()==0,
            "M22A wrapped symmetric operation must rotate front consistently");

    auto patch=fourCorner(110.);
    patch.positions.push_back({0,0,1});
    patch.faces.push_back({1,2,4}); // one cap-side generated incident face
    const auto split=proposeMirroredBridgeFrontAdvance(patch,1,24,48,.1);
    require(split.addedFaces==3&&
            split.workspace.faces.size()==4,
            "M22A must conformingly split existing crossing generated face");
    verifyMirroredFaces(split.workspace);
    std::cout<<"[r06-m22a] cyclic wrap and one-face conforming front split PASS\n";
}

void rejectionIsAtomic() {
    {
        auto state=fourCorner(60.);
        state.boundaryCount=4;
        bool refused=false;
        try{(void)proposeMirroredBridgeFrontAdvance(state,1,24,48,.1);}
        catch(const std::logic_error&){refused=true;}
        require(refused&&state.faces.empty()&&state.front.size()==4,
                "M22A crossing original R03 edge must refuse without edit");
    }
    {
        auto state=fourCorner(60.);
        state.positions[3].y+=.001;
        bool refused=false;
        try{(void)proposeMirroredBridgeFrontAdvance(state,1,24,48,.1);}
        catch(const std::logic_error&){refused=true;}
        require(refused&&state.faces.empty(),
                "M22A non-exact mirrored source must refuse atomically");
    }
    {
        auto state=fourCorner(60.);
        bool refused=false;
        try{(void)proposeMirroredBridgeFrontAdvance(state,1,5,48,.1);}
        catch(const std::length_error&){refused=true;}
        require(refused&&state.positions.size()==4,
                "M22A finite vertex budget must reject before mutation");
    }
    {
        auto state=fourCorner(60.);
        bool refused=false;
        try{(void)proposeMirroredBridgeFrontAdvance(state,1,24,3,.1);}
        catch(const std::length_error&){refused=true;}
        require(refused&&state.faces.empty(),
                "M22A face budget must reject before mutation");
    }
    {
        auto state=fourCorner(60.);
        state.positions.push_back({0,0,0}); // existing seam midpoint, distinct ID
        bool refused=false;
        try{(void)proposeMirroredBridgeFrontAdvance(state,1,24,48,.1);}
        catch(const std::logic_error&){refused=true;}
        require(refused&&state.faces.empty(),
                "M22A must not coordinate-weld duplicate seam IDs");
    }
    {
        auto state=fourCorner(60.);
        state.positions.push_back({-.2,0,1});
        state.faces.push_back({1,2,4}); // non-seam third vertex
        bool refused=false;
        try{(void)proposeMirroredBridgeFrontAdvance(state,1,24,48,.1);}
        catch(const std::logic_error&){refused=true;}
        require(refused&&state.faces.size()==1,
                "M22A must refuse non-mirror conforming edge split");
    }
    std::cout<<"[r06-m22a] source, reflection, finite budget and seam ID refusals PASS\n";
}

} // namespace

int main() {
    try {
        checkThreeRules();
        cyclicWrapAndGeneratedIncidence();
        rejectionIsAtomic();
    }catch(const std::exception& e) {
        std::cerr<<"M22A failed: "<<e.what()<<"\n";
        return 1;
    }
}
