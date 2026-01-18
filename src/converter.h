#pragma once

#include "ico_cur_decoder.h"
#include "size_filter.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ani2xcursor {

std::pair<std::vector<CursorImage>, std::vector<uint32_t>>
process_ani_file(const std::filesystem::path& ani_path,
                 SizeFilter filter,
                 const std::vector<uint32_t>& specific_sizes);

std::pair<std::vector<CursorImage>, std::vector<uint32_t>>
process_cur_file(const std::filesystem::path& cur_path,
                 SizeFilter filter,
                 const std::vector<uint32_t>& specific_sizes);

} // namespace ani2xcursor
