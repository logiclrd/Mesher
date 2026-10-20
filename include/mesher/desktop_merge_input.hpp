#pragma once
#include "mesher/partition_render.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace mesher {

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
    struct Edges {bool toggle{},submit{},clear{};};
    Edges sample(bool m,bool enter,bool backspace) noexcept {
        const Edges edges{m&&!previousM_,enter&&!previousEnter_,
                          backspace&&!previousBackspace_};
        previousM_=m;
        previousEnter_=enter;
        previousBackspace_=backspace;
        return edges;
    }
private:
    bool previousM_{},previousEnter_{},previousBackspace_{};
};

} // namespace mesher
