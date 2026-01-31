#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#define _(String) gettext(String)

namespace ani2xcursor {

namespace fs = std::filesystem;

struct ManifestLoadResult {
    std::map<std::string, std::string> role_to_path;
    std::map<std::string, std::vector<uint32_t>> role_to_sizes;
    std::string theme_name;
    std::vector<std::string> warnings;
};

[[nodiscard]] ManifestLoadResult load_manifest_toml(const fs::path& path);

void write_manifest_toml_template(const fs::path& path, const fs::path& input_dir,
                                  const std::map<std::string, std::string>& guesses);

[[nodiscard]] const std::vector<std::string>& known_roles();
[[nodiscard]] bool is_known_role(std::string_view role);
[[nodiscard]] bool is_optional_role(std::string_view role);

}  // namespace ani2xcursor
