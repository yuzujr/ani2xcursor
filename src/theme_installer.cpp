#include "theme_installer.h"
#include "utils/fs.h"

#include <spdlog/spdlog.h>

#include <iostream>

namespace ani2xcursor {

void ThemeInstaller::install(const fs::path& theme_dir, bool overwrite) {
    if (!fs::exists(theme_dir)) {
        throw std::runtime_error("Theme directory does not exist: " + theme_dir.string());
    }
    
    auto theme_name = theme_dir.filename().string();
    auto install_path = get_install_path(theme_name);
    
    spdlog::info("Installing theme '{}' to {}", theme_name, install_path.string());
    
    // Check if already installed
    if (fs::exists(install_path)) {
        if (overwrite) {
            spdlog::info("Removing existing installation...");
            std::error_code ec;
            fs::remove_all(install_path, ec);
            if (ec) {
                throw std::runtime_error("Failed to remove existing theme: " + ec.message());
            }
        } else {
            throw std::runtime_error("Theme already installed. Use --force to overwrite.");
        }
    }
    
    // Create parent directory
    fs::create_directories(install_path.parent_path());
    
    // Copy theme directory recursively, preserving symlinks
    std::error_code ec;
    fs::copy(theme_dir, install_path, 
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    
    if (ec) {
        throw std::runtime_error("Failed to install theme: " + ec.message());
    }
    
    spdlog::info("Theme installed successfully!");
    print_usage_instructions(theme_name);
}

fs::path ThemeInstaller::get_install_path(const std::string& theme_name) {
    return utils::get_xdg_data_home() / "icons" / theme_name;
}

bool ThemeInstaller::is_installed(const std::string& theme_name) {
    return fs::exists(get_install_path(theme_name));
}

void ThemeInstaller::print_usage_instructions(const std::string& theme_name) {
    std::cout << "\n"
              << "=== Cursor Theme Usage Instructions ===\n\n"
              << "To enable the '" << theme_name << "' cursor theme:\n\n"
              << "GNOME / GTK:\n"
              << "  gsettings set org.gnome.desktop.interface cursor-theme '" << theme_name << "'\n\n"
              << "KDE Plasma:\n"
              << "  Open System Settings > Appearance > Cursors\n"
              << "  Or: plasma-apply-cursortheme " << theme_name << "\n\n"
              << "Niri:\n"
              << "  Add to ~/.config/niri/config.kdl:\n"
              << "    cursor {\n"
              << "        xcursor-theme \"" << theme_name << "\"\n"
              << "    }\n\n"
              << "Hyprland:\n"
              << "  Add to ~/.config/hypr/hyprland.conf:\n"
              << "    exec-once = hyprctl setcursor " << theme_name << " 24\n"
              << "  Or run: hyprctl setcursor " << theme_name << " 24\n\n"
              << "Sway:\n"
              << "  Add to ~/.config/sway/config:\n"
              << "    seat * xcursor_theme " << theme_name << " 24\n\n"
              << "X11 (~/.Xresources):\n"
              << "  Xcursor.theme: " << theme_name << "\n"
              << "  Then run: xrdb -merge ~/.Xresources\n\n"
              << "Environment variable (works everywhere):\n"
              << "  export XCURSOR_THEME=" << theme_name << "\n"
              << "  export XCURSOR_SIZE=24\n\n";
}

} // namespace ani2xcursor
