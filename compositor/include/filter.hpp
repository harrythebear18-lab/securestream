#pragma once

#include "types.hpp"
#include <memory>
#include <string>
#include <string_view>

namespace securestream {

// Filter is a transform applied to a frame before it reaches the encoder.
// Filters are part of the rendering pipeline, not post-processing.
// They operate on BGRA8 frames.
class Filter {
public:
    Filter(std::string name) : name_(std::move(name)) {}
    virtual ~Filter() = default;

    Filter(const Filter&) = delete;
    Filter& operator=(const Filter&) = delete;

    // Apply the filter to a frame in-place.
    virtual void apply(Frame& frame) = 0;

    std::string_view name() const { return name_; }

private:
    std::string name_;
};

} // namespace securestream
