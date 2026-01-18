#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ani2xcursor {

namespace fs = std::filesystem;

struct MappingLoadResult {
    std::map<std::string, std::string> role_to_path;
    std::string theme_name;
    std::vector<std::string> warnings;
};

[[nodiscard]] MappingLoadResult load_mapping_toml(const fs::path& path);

void write_mapping_toml_template(const fs::path& path,
                                 const fs::path& input_dir,
                                 const std::map<std::string, std::string>& guesses);

[[nodiscard]] const std::vector<std::string>& known_roles();
[[nodiscard]] bool is_known_role(std::string_view role);
[[nodiscard]] bool is_optional_role(std::string_view role);

} // namespace ani2xcursor
