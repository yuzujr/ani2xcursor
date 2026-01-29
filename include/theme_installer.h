#pragma once

#include <filesystem>
#include <string>

namespace ani2xcursor {

namespace fs = std::filesystem;

// Theme installer - copies/installs theme to user's icon directory
class ThemeInstaller {
public:
    // Install theme to XDG_DATA_HOME/icons/<theme_name>
    // If overwrite is true, existing theme will be removed first
    static void install(const fs::path& theme_dir, bool overwrite = true);

    // Install theme_dir as a specific theme name
    static void install(const fs::path& theme_dir, const std::string& theme_name,
                        bool overwrite = true);

    // Get the installation path for a theme
    [[nodiscard]] static fs::path get_install_path(const std::string& theme_name);
};

}  // namespace ani2xcursor
