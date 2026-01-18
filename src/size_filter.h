#pragma once

#include <cstdint>
#include <vector>

enum class SizeFilter { All, Max, Specific };

struct SizeSelection {
    SizeFilter filter = SizeFilter::All;
    std::vector<uint32_t> specific_sizes;
};
