#pragma once

#include <filesystem>
#include <map>
#include <vector>

#include "size_filter.h"

namespace ani2xcursor {

namespace fs = std::filesystem;

struct PreviewGenerationResult {
    size_t generated = 0;
    size_t failed = 0;
    std::map<std::string, std::string> guesses;
};

[[nodiscard]] PreviewGenerationResult generate_previews(
    const fs::path& input_dir, const fs::path& preview_dir, SizeFilter filter,
    const std::vector<uint32_t>& specific_sizes);

}  // namespace ani2xcursor
