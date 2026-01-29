#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "ico_cur_decoder.h"

namespace ani2xcursor {

uint32_t nominal_size(const CursorImage& img);
std::optional<size_t> find_exact_size_index(std::span<const CursorImage> images,
                                            uint32_t target_size);
size_t find_closest_size_index(std::span<const CursorImage> images, uint32_t target_size);
CursorImage rescale_cursor(const CursorImage& src, uint32_t target_size);
std::vector<uint32_t> collect_cursor_sizes(const std::filesystem::path& cursor_path);
void list_available_sizes(const std::filesystem::path& input_dir);

}  // namespace ani2xcursor
