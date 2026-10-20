#pragma once
#include "mesher/partition_render.hpp"
#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mesher {

// Intentional *bounded* contour admission for explicitly armed desktop
// Merge. This is the existing c1/c2/c3/M03/d2-tested two-tetra policy,
// not a relaxation of original witness, history, mirror or work limits.
// Legacy OFF desktop does not construct or invoke this policy.
inline ExactCutStepLimits desktopMergeExactLimits(bool symmetric) {
    ExactCutStepLimits limits;
    auto& contours=limits.join.removal.contours;
    contours.maximumPlanarityInRadii=4;
    contours.maximumSingleCapPlanarityInRadii=4;
    contours.maximumIsoperimetricRatio=20;
    contours.maximumSingleCapIsoperimetricRatio=20;
    contours.maximumPerimeterRatio=20;
    contours.maximumGapInEdges=symmetric?2.5:20;
    contours.minimumNormalAlignment=0;
    limits.requireMirrors=symmetric; // strict certified reciprocal involution
    return limits;
}

// M49: these transitions ARE the desktop mode/selection rules used by
// VulkanViewport. They require no GLFW, Vulkan, wall clock or mutable mesh.
// The worker remains responsible for the separate M47 first-original,
// geometry, source-certificate and finite-work checks.
class DesktopMergeInput {
public:
    enum class Result {
        Ignored, Unavailable, Busy, ModeOn, ModeOff, Cleared,
        NoHit, InvalidFace, Selected, Duplicate, Full, NoSelection,
        Stale, Queued
    };
    struct Submission {
        Result result{Result::Ignored};
        std::shared_ptr<const RegionTopologyIndex> expected;
        std::vector<FaceId> originals;
    };

    bool enabled() const noexcept {return enabled_;}
    bool inflatesOnLeftClick() const noexcept {return !enabled_;}
    std::size_t selectedCount() const noexcept {return selected_.size();}
    const std::vector<FaceId>& selectedFaces() const noexcept {return selected_;}
    const std::shared_ptr<const RegionTopologyIndex>& selectedRoot() const noexcept {
        return root_;
    }

    Result toggle(bool authorized,bool idle,bool sculpting) {
        if(sculpting)return Result::Ignored;
        if(!authorized)return Result::Unavailable;
        if(!idle)return Result::Busy;
        enabled_=!enabled_;
        clearSelection();
        return enabled_?Result::ModeOn:Result::ModeOff;
    }
    Result clear() {
        if(!enabled_)return Result::Ignored;
        clearSelection();
        return Result::Cleared;
    }
    void revisionChanged(const std::shared_ptr<const RegionTopologyIndex>& displayed) {
        if(root_&&root_!=displayed)clearSelection();
    }
    Result pick(const std::shared_ptr<const RegionTopologyIndex>& displayed,
                std::optional<FaceId> face,bool idle) {
        if(!enabled_)return Result::Ignored;
        if(!idle)return Result::Busy;
        if(!displayed||!face)return Result::NoHit;
        if(!*face)return Result::InvalidFace;
        if(root_!=displayed) {
            clearSelection();
            root_=displayed;
        }
        if(std::find(selected_.begin(),selected_.end(),*face)!=selected_.end())
            return Result::Duplicate;
        if(selected_.size()==maximumSelection)return Result::Full;
        selected_.push_back(*face);
        return Result::Selected;
    }
    // M51 explicit user A-key nomination. The caller supplies a finite
    // named FIRST-original list ONLY for a deliberate test fixture and
    // its exact initial immutable indexed root. No coordinate guess,
    // scene-wide candidate scan, inferred mirror partner or automatic
    // expansion of ordinary mouse picks. Preflight the ENTIRE list
    // before modifying the existing selection.
    Result nominateFixtureOriginals(
            const std::shared_ptr<const RegionTopologyIndex>& displayed,
            const std::shared_ptr<const RegionTopologyIndex>& fixtureOriginal,
            const std::vector<FaceId>& originals,bool idle) {
        if(!enabled_)return Result::Ignored;
        if(!idle)return Result::Busy;
        if(!displayed||!fixtureOriginal||displayed!=fixtureOriginal||
           (root_&&root_!=displayed))return Result::Stale;
        if(originals.empty())return Result::NoSelection;
        if(originals.size()>maximumSelection)return Result::Full;
        std::vector<FaceId> extras;
        for(const FaceId id:originals) {
            if(!id||!displayed->maybeFace(id))return Result::InvalidFace;
            if(std::find(extras.begin(),extras.end(),id)!=extras.end())
                return Result::InvalidFace;
            if(std::find(selected_.begin(),selected_.end(),id)==selected_.end())
                extras.push_back(id);
        }
        if(extras.size()>maximumSelection-selected_.size())return Result::Full;
        if(extras.empty())return Result::Duplicate;
        if(!root_)root_=displayed;
        selected_.insert(selected_.end(),extras.begin(),extras.end());
        return Result::Selected;
    }
    Submission submit(const std::shared_ptr<const RegionTopologyIndex>& displayed,
                      bool authorized,bool idle) {
        if(!enabled_)return {Result::Ignored,{},{}};
        if(!authorized)return {Result::Unavailable,{},{}};
        if(!root_||selected_.empty())return {Result::NoSelection,{},{}};
        if(!idle) {
            clearSelection();
            return {Result::Busy,{},{}};
        }
        if(root_!=displayed) {
            clearSelection();
            return {Result::Stale,{},{}};
        }
        Submission result{Result::Queued,std::move(root_),std::move(selected_)};
        clearSelection();
        return result;
    }
    static constexpr std::size_t maximumSelection=32;
private:
    void clearSelection() {
        selected_.clear();
        root_.reset();
    }
    bool enabled_=false; // Explicitly default OFF, even in authorized sessions.
    std::shared_ptr<const RegionTopologyIndex> root_;
    std::vector<FaceId> selected_;
};

// Real GLFW key states pass through this pure rising-edge detector, so
// held keys cannot toggle/submit/clear repeatedly across rendered frames.
class DesktopMergeKeyEdges {
public:
    struct Edges {bool toggle{},submit{},clear{},nominate{};};
    Edges sample(bool m,bool enter,bool backspace,bool a=false) noexcept {
        const Edges edges{m&&!previousM_,enter&&!previousEnter_,
                          backspace&&!previousBackspace_,a&&!previousA_};
        previousM_=m;
        previousEnter_=enter;
        previousBackspace_=backspace;
        previousA_=a;
        return edges;
    }
private:
    bool previousM_{},previousEnter_{},previousBackspace_{},previousA_{};
};

} // namespace mesher
