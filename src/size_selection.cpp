#include "size_selection.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace ani2xcursor {

std::vector<size_t> select_size_indices(std::span<const CursorImage> images,
                                        SizeFilter filter,
                                        const std::vector<uint32_t>& specific_sizes) {
    std::vector<size_t> size_indices;
    if (images.empty()) {
        return size_indices;
    }

    if (filter == SizeFilter::All) {
        size_indices.reserve(images.size());
        for (size_t i = 0; i < images.size(); ++i) {
            size_indices.push_back(i);
        }
    } else if (filter == SizeFilter::Max) {
        size_indices.push_back(0);
    } else if (filter == SizeFilter::Specific) {
        for (uint32_t target_size : specific_sizes) {
            size_t best_idx = 0;
            uint32_t best_diff = UINT32_MAX;

            for (size_t idx = 0; idx < images.size(); ++idx) {
                const auto& img = images[idx];
                uint32_t nominal_size = std::max(img.width, img.height);
                uint32_t diff = (nominal_size > target_size) ?
                               (nominal_size - target_size) : (target_size - nominal_size);

                if (diff < best_diff) {
                    best_diff = diff;
                    best_idx = idx;
                }
            }

            if (std::find(size_indices.begin(), size_indices.end(), best_idx) == size_indices.end()) {
                size_indices.push_back(best_idx);
            }
        }
    }

    return size_indices;
}

size_t choose_preview_index(std::span<const CursorImage> images,
                            SizeFilter filter,
                            const std::vector<uint32_t>& specific_sizes) {
    auto indices = select_size_indices(images, filter, specific_sizes);
    if (indices.empty()) {
        throw std::runtime_error("No sizes selected for preview");
    }

    size_t best_idx = indices[0];
    uint32_t best_size = std::max(images[best_idx].width, images[best_idx].height);

    for (size_t idx : indices) {
        uint32_t nominal = std::max(images[idx].width, images[idx].height);
        if (nominal > best_size) {
            best_size = nominal;
            best_idx = idx;
        }
    }

    return best_idx;
}

} // namespace ani2xcursor
