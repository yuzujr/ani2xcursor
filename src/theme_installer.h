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
    
    // Get the installation path for a theme
    [[nodiscard]] static fs::path get_install_path(const std::string& theme_name);
    
    // Check if a theme is already installed
    [[nodiscard]] static bool is_installed(const std::string& theme_name);
    
    // Print instructions for enabling the cursor theme
    static void print_usage_instructions(const std::string& theme_name);
};

} // namespace ani2xcursor
