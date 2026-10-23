#pragma once

#include "mesher/repair_mixed_source_view.hpp"

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {

// M14B1: existing survivor XYZ comes from the immutable R03 source, never
// from a second writable position map. Only newly allocated/provisional
// repair vertices are owned here. Changes to existing vertices (reserved
// for a future approved transaction edit) go through sourceView overrides.
//
// materialize() is a bounded, READ-ONLY compatibility snapshot of the
// *tracked* staging halo for M14B2's legacy geometry-scoring helpers. It
// cannot change source/transaction state. Any edit invalidates the cache.
class MixedClosurePositions {
public:
    MixedClosurePositions()=default;
    explicit MixedClosurePositions(
        std::shared_ptr<MixedClosureSourceView> source) {
        bind(std::move(source));
    }

    void bind(std::shared_ptr<MixedClosureSourceView> source) {
        if(!source||source_)
            throw std::invalid_argument("M14B1 source may only be bound once");
        source_=std::move(source);
        invalidate();
    }
    bool contains(VertexId id) const {
        requireSource();
        return created_.contains(id)||
            static_cast<bool>(source_->source().maybeVertex(id));
    }
    Vec3 at(VertexId id) const {
        requireSource();
        const auto found=created_.find(id);
        return found!=created_.end()?found->second:source_->position(id);
    }
    void emplace(VertexId id,Vec3 position) {
        requireSource();
        validate(position);
        if(source_->source().maybeVertex(id)) {
            const auto previous=source_->position(id);
            if(previous.x!=position.x||previous.y!=position.y||
               previous.z!=position.z)
                throw std::logic_error(
                    "M14B1 original vertex cannot be replaced by staging");
            trackedOriginal_.insert(id);
        }else {
            if(created_.contains(id))
                throw std::logic_error("M14B1 duplicate new vertex ID");
            source_->registerGeneratedVertex(id);
            created_.emplace(id,position);
        }
        invalidate();
    }
    void set(VertexId id,Vec3 position) {
        requireSource();
        validate(position);
        if(auto found=created_.find(id);found!=created_.end())
            found->second=position;
        else {
            (void)source_->source().vertex(id);
            source_->overridePosition(id,position);
            trackedOriginal_.insert(id);
        }
        invalidate();
    }
    std::size_t erase(VertexId id) {
        requireSource();
        if(source_->source().maybeVertex(id))
            throw std::logic_error(
                "M14B1 may not erase an original survivor vertex");
        const auto removed=created_.erase(id);
        if(removed) {
            source_->retireGeneratedVertex(id);
            invalidate();
        }
        return removed;
    }

    // Materialize ONLY tracked original vertex IDs and transaction-local
    // new IDs. Do not scan or clone the complete surviving mesh. Readers
    // that need a remote old vertex must query at()/sourceView by ID.
    const std::map<VertexId,Vec3>& materialize() const {
        requireSource();
        if(dirty_) {
            snapshot_.clear();
            for(const auto id:trackedOriginal_)
                snapshot_.emplace(id,source_->position(id));
            snapshot_.insert(created_.begin(),created_.end());
            dirty_=false;
        }
        return snapshot_;
    }
    // Iteration only enumerates the tracked halo; read-through at(id)
    // is available for ANY source survivor without copying its XYZ.
    using const_iterator=std::map<VertexId,Vec3>::const_iterator;
    const_iterator begin() const {return materialize().begin();}
    const_iterator end() const {return materialize().end();}
    const_iterator find(VertexId id) const {return materialize().find(id);}

    std::size_t originalHandles() const noexcept {
        return trackedOriginal_.size();
    }
    std::size_t newVertexPositions() const noexcept {
        return created_.size();
    }

private:
    void requireSource() const {
        if(!source_)throw std::logic_error("M14B1 unbound source topology");
    }
    static void validate(Vec3 p) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            throw std::logic_error("M14B1 nonfinite proposed vertex position");
    }
    void invalidate() const noexcept {dirty_=true;}
    std::shared_ptr<MixedClosureSourceView> source_;
    std::set<VertexId> trackedOriginal_;
    std::map<VertexId,Vec3> created_;
    mutable std::map<VertexId,Vec3> snapshot_;
    mutable bool dirty_=true;
};
} // namespace mesher
