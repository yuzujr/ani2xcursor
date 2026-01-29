#include "path_utils.h"

#include <algorithm>
#include <cctype>

namespace ani2xcursor {

std::optional<std::filesystem::path> find_file_icase(const std::filesystem::path& dir,
                                                     const std::string& filename) {
    auto exact_path = dir / filename;
    if (std::filesystem::exists(exact_path)) {
        return exact_path;
    }

    std::string lower_target = filename;
    std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(),
                   [](unsigned char c) {
                       return std::tolower(c);
                   });

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;

        std::string entry_name = entry.path().filename().string();
        std::string lower_entry = entry_name;
        std::transform(lower_entry.begin(), lower_entry.end(), lower_entry.begin(),
                       [](unsigned char c) {
                           return std::tolower(c);
                       });

        if (lower_entry == lower_target) {
            return entry.path();
        }
    }

    return std::nullopt;
}

bool is_ani_file(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return ext == ".ani";
}

bool is_cur_file(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return ext == ".cur";
}

std::string normalize_relative_path(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

}  // namespace ani2xcursor
