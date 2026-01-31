#include "theme_installer.h"

#include <spdlog/spdlog.h>

#include "size_tools.h"
#include "spdlog/fmt/bundled/base.h"
#include "utils/fs.h"

namespace ani2xcursor {

void ThemeInstaller::install(const fs::path& theme_dir, bool overwrite) {
    auto theme_dir_abs = fs::weakly_canonical(theme_dir);
    auto theme_name = theme_dir_abs.filename().string();
    if (theme_name.empty() || theme_name == "." || theme_name == "..") {
        throw std::runtime_error(_("Invalid theme directory name: ") + theme_dir.string());
    }
    install(theme_dir, theme_name, overwrite);
}

void ThemeInstaller::install(const fs::path& theme_dir, const std::string& theme_name,
                             bool overwrite) {
    if (!fs::exists(theme_dir)) {
        throw std::runtime_error(_("Theme directory does not exist: ") + theme_dir.string());
    }

    auto install_path = get_install_path(theme_name);
    auto icons_dir = utils::get_xdg_data_home() / "icons";
    if (install_path == icons_dir) {
        throw std::runtime_error(_("Invalid theme install path: ") + install_path.string());
    }

    spdlog::info(spdlog::fmt_lib::runtime(_("Installing theme '{}' to {}")), theme_name, install_path.string());
    // Check if already installed
    if (fs::exists(install_path)) {
        if (overwrite) {
            spdlog::info(_("Removing existing installation..."));
            std::error_code ec;
            fs::remove_all(install_path, ec);
            if (ec) {
                throw std::runtime_error(_("Failed to remove existing theme: ") + ec.message());
            }
        } else {
            throw std::runtime_error(_("Theme already installed. Use --force to overwrite."));
        }
    }

    // Create parent directory
    fs::create_directories(install_path.parent_path());

    // Copy theme directory recursively, preserving symlinks
    std::error_code ec;
    fs::copy(theme_dir, install_path, fs::copy_options::recursive | fs::copy_options::copy_symlinks,
             ec);

    if (ec) {
        throw std::runtime_error(_("Failed to install theme: ") + ec.message());
    }

    spdlog::info(_("Theme installed successfully!"));
}

fs::path ThemeInstaller::get_install_path(const std::string& theme_name) {
    return utils::get_xdg_data_home() / "icons" / theme_name;
}

}  // namespace ani2xcursor
