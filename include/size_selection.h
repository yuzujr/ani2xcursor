#pragma once

#include <span>
#include <vector>

#include "ico_cur_decoder.h"
#include "size_filter.h"

#define _(String) gettext(String)

namespace ani2xcursor {

std::vector<size_t> select_size_indices(std::span<const CursorImage> images, SizeFilter filter,
                                        const std::vector<uint32_t>& specific_sizes);

size_t choose_preview_index(std::span<const CursorImage> images, SizeFilter filter,
                            const std::vector<uint32_t>& specific_sizes);

}  // namespace ani2xcursor
