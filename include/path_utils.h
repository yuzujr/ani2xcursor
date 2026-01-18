#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ani2xcursor {

std::optional<std::filesystem::path> find_file_icase(const std::filesystem::path& dir,
                                                     const std::string& filename);
bool is_ani_file(const std::filesystem::path& path);
bool is_cur_file(const std::filesystem::path& path);
std::string normalize_relative_path(std::string path);

} // namespace ani2xcursor
