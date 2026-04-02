#include "path_utils.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

#include "utils/fs.h"

namespace ani2xcursor {

namespace {

std::string to_lower_ascii(std::string_view value) {
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower;
}

bool has_extension_icase(const std::filesystem::path& path, std::string_view extension) {
    return to_lower_ascii(path.extension().string()) == extension;
}

int score_inf_candidate(const std::filesystem::path& path) {
    int score = 0;
    const auto lower_name = to_lower_ascii(path.filename().string());
    if (lower_name == "install.inf") {
        score += 100;
    } else if (lower_name.find("install") != std::string::npos) {
        score += 15;
    }

    try {
        const auto content = to_lower_ascii(utils::read_file_string(path));
        if (content.find("[defaultinstall]") != std::string::npos) {
            score += 8;
        }
        if (content.find("control panel\\cursors") != std::string::npos) {
            score += 6;
        }
        if (content.find("[scheme.reg]") != std::string::npos) {
            score += 4;
        }
        if (content.find("[wreg]") != std::string::npos) {
            score += 4;
        }
        if (content.find("addreg") != std::string::npos) {
            score += 2;
        }
        if (content.find("copyfiles") != std::string::npos) {
            score += 2;
        }
    } catch (...) {
        // Ignore unreadable files and keep their score low so valid installer INFs win.
    }

    return score;
}

}  // namespace

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

std::optional<std::filesystem::path> find_inf_file(const std::filesystem::path& dir) {
    auto preferred = find_file_icase(dir, "Install.inf");
    if (preferred) {
        std::error_code preferred_ec;
        if (std::filesystem::is_regular_file(*preferred, preferred_ec) && !preferred_ec) {
            return preferred;
        }
    }

    struct InfCandidate {
        std::filesystem::path path;
        int score;
        std::string sort_key;
    };

    std::vector<InfCandidate> candidates;
    std::error_code iter_ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, iter_ec)) {
        if (iter_ec) {
            break;
        }

        std::error_code entry_ec;
        if (!entry.is_regular_file(entry_ec) || entry_ec) {
            continue;
        }

        const auto& path = entry.path();
        if (!has_extension_icase(path, ".inf")) {
            continue;
        }

        candidates.push_back({path, score_inf_candidate(path), to_lower_ascii(path.filename().string())});
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::sort(candidates.begin(), candidates.end(), [](const InfCandidate& lhs,
                                                       const InfCandidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.sort_key < rhs.sort_key;
    });

    return candidates.front().path;
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
