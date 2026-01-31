#include "manifest.h"

#include <libintl.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

#include "path_utils.h"
#include "size_tools.h"
#include "utils/fs.h"

namespace ani2xcursor {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return out;
}

std::string strip_comment(std::string_view line) {
    bool in_quote = false;
    char quote_char = '\0';

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if ((c == '"' || c == '\'') && (i == 0 || line[i - 1] != '\\')) {
            if (!in_quote) {
                in_quote = true;
                quote_char = c;
            } else if (quote_char == c) {
                in_quote = false;
            }
        } else if (c == '#' && !in_quote) {
            return std::string(line.substr(0, i));
        }
    }
    return std::string(line);
}

std::string unquote(std::string_view s) {
    s = trim(s);
    if (s.size() >= 2) {
        char first = s.front();
        char last = s.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            s = s.substr(1, s.size() - 2);
        }
    }
    return std::string(s);
}

std::string escape_quotes(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    return out;
}

std::vector<uint32_t> parse_sizes_list(std::string_view value) {
    std::vector<uint32_t> sizes;
    size_t pos = 0;
    while (pos <= value.size()) {
        size_t comma = value.find(',', pos);
        size_t len = (comma == std::string_view::npos) ? value.size() - pos : comma - pos;
        auto token = trim(value.substr(pos, len));
        if (!token.empty()) {
            try {
                size_t idx = 0;
                unsigned long parsed = std::stoul(std::string(token), &idx, 10);
                if (idx != token.size() || parsed == 0 || parsed > 1024) {
                    throw std::invalid_argument(_("out of range"));
                }
                uint32_t size = static_cast<uint32_t>(parsed);
                if (std::find(sizes.begin(), sizes.end(), size) == sizes.end()) {
                    sizes.push_back(size);
                }
            } catch (const std::exception&) {
                return {};
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return sizes;
}

std::string join_sizes(const std::vector<uint32_t>& sizes) {
    std::string out;
    for (uint32_t size : sizes) {
        if (!out.empty()) {
            out += ", ";
        }
        out += std::to_string(size);
    }
    return out;
}

std::vector<uint32_t> collect_sizes_for_guess(const fs::path& input_dir, const std::string& guess) {
    if (guess.empty()) {
        return {};
    }
    fs::path path = fs::path(guess);
    if (!path.is_absolute()) {
        path = input_dir / normalize_relative_path(guess);
    }
    if (!fs::exists(path)) {
        if (path.parent_path().empty()) {
            if (auto found = find_file_icase(input_dir, path.filename().string())) {
                path = *found;
            } else {
                return {};
            }
        } else {
            return {};
        }
    }
    try {
        return collect_cursor_sizes(path);
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace

const std::vector<std::string>& known_roles() {
    static const std::vector<std::string> roles = {
        "pointer", "help", "working", "busy", "precision", "text", "hand",   "unavailable", "vert",
        "horz",    "dgn1", "dgn2",    "move", "alternate", "link", "person", "pin",
    };
    return roles;
}

bool is_known_role(std::string_view role) {
    auto& roles = known_roles();
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

bool is_optional_role(std::string_view role) {
    return role == "person" || role == "pin";
}

ManifestLoadResult load_manifest_toml(const fs::path& path) {
    auto content = utils::read_file_string(path);
    std::string label = path.filename().string();
    std::istringstream stream(content);

    std::string current_section;
    std::map<std::string, std::map<std::string, std::string>> sections;
    std::string line;
    bool first_line = true;

    while (std::getline(stream, line)) {
        if (first_line) {
            first_line = false;
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line = line.substr(3);
            }
        }

        std::string no_comment = strip_comment(line);
        auto trimmed = trim(no_comment);
        if (trimmed.empty()) {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            current_section = to_lower(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }

        auto eq = trimmed.find('=');
        if (eq == std::string_view::npos) {
            throw std::runtime_error(_("Invalid line in ") + label + ": " + std::string(trimmed));
        }

        auto key = to_lower(trim(trimmed.substr(0, eq)));
        auto value = trim(trimmed.substr(eq + 1));

        if (key.empty()) {
            throw std::runtime_error(_("Empty key in ") + label);
        }

        sections[current_section][key] = unquote(value);
    }

    ManifestLoadResult result;

    std::string theme_override;
    if (auto input_it = sections.find("input"); input_it != sections.end()) {
        auto theme_it = input_it->second.find("theme");
        if (theme_it != input_it->second.end() && !theme_it->second.empty()) {
            theme_override = theme_it->second;
        }
    }

    auto files_it = sections.find("files");
    if (files_it == sections.end()) {
        throw std::runtime_error(label + _(" missing [files] section"));
    }

    for (const auto& [key, value] : files_it->second) {
        std::string role = key;
        if (!is_known_role(role)) {
            result.warnings.push_back(_("Unknown role in [files]: '") + key + "'");
            continue;
        }

        result.role_to_path[role] = value;
    }

    if (auto sizes_it = sections.find("sizes"); sizes_it != sections.end()) {
        for (const auto& [key, value] : sizes_it->second) {
            std::string role = key;
            if (!is_known_role(role)) {
                result.warnings.push_back(_("Unknown role in [sizes]: '") + key + "'");
                continue;
            }
            if (value.empty()) {
                continue;
            }
            auto parsed_sizes = parse_sizes_list(value);
            if (parsed_sizes.empty()) {
                result.warnings.push_back(_("Invalid size list in [sizes] for '") + key + "': '" +
                                          value + "'");
                continue;
            }
            result.role_to_sizes[role] = std::move(parsed_sizes);
        }
    }

    if (!theme_override.empty()) {
        result.theme_name = theme_override;
    }

    return result;
}

void write_manifest_toml_template(const fs::path& path, const fs::path& input_dir,
                                  const std::map<std::string, std::string>& guesses) {
    std::error_code ec;
    auto abs_dir = fs::absolute(input_dir, ec);
    if (ec) {
        abs_dir = input_dir;
    }

    std::string content;
    content += _("# ani2xcursor manifest (role mapping + per-role sizes)\n");
    content +=
        _("# Fill in the relative paths (relative to input_dir) for each "
          "Windows role.\n");
    content += _("# Use the preview images in ani2xcursor/previews/ to decide.\n");
    content += _("# Leave empty to skip a role.\n");
    content += "#\n";
    content += _("# Roles (Windows role -> common meaning):\n");
    content += _("# pointer      = Normal Select (Arrow)\n");
    content += _("# help         = Help Select (Question mark)\n");
    content += _("# working      = Working in Background (Arrow + Busy)\n");
    content += _("# busy         = Busy / Wait (Spinner)\n");
    content += _("# precision    = Precision Select (Crosshair)\n");
    content += _("# text         = Text Select (I-beam)\n");
    content += _("# hand         = Handwriting / Pen (NWPen)\n");
    content += _("# unavailable  = Not Allowed / Unavailable (No)\n");
    content += _("# vert         = Vertical Resize (SizeNS)\n");
    content += _("# horz         = Horizontal Resize (SizeWE)\n");
    content += _("# dgn1         = Diagonal Resize 1 (NW-SE, SizeNWSE)\n");
    content += _("# dgn2         = Diagonal Resize 2 (NE-SW, SizeNESW)\n");
    content += _("# move         = Move / Size All (Fleur)\n");
    content += _("# alternate    = Alternate Select (Up Arrow)\n");
    content += _("# link         = Link Select (Hand)\n");
    content += _("# person       = Person Select (optional)\n");
    content += _("# pin          = Pin Select (optional)\n");
    content += "\n";
    content += "[input]\n";
    content += _("# Theme name override (optional)\n");
    content += "theme = \"\"\n";
    content += _("# for reference only (do not edit)\n");
    content += "dir = \"" + escape_quotes(abs_dir.string()) + "\"\n";
    content += "\n";
    content += "[files]\n";
    content += _("# Put relative paths here. Examples:\n");
    content += _("# pointer = \"Normal.ani\"\n");
    content += _("# text    = \"Text.ani\"\n");
    content += "\n";

    size_t max_role_len = 0;
    for (const auto& role : known_roles()) {
        max_role_len = std::max(max_role_len, role.size());
    }

    for (const auto& role : known_roles()) {
        std::string padded = role;
        padded.append(max_role_len + 1 - role.size(), ' ');
        content += padded;
        content += "= \"";
        auto it = guesses.find(role);
        if (it != guesses.end()) {
            content += escape_quotes(it->second);
        }
        content += "\"";
        if (it != guesses.end()) {
            content += _(" # guessed");
        }
        content += "\n";
    }

    content += "\n";
    content += "[sizes]\n";
    content += _("# Optional per-role target size override (comma-separated list).\n");
    content += _("# Example: pointer = \"48\" or pointer = \"32, 48\"\n");
    content += _("# Defaults are filled from the current cursor files when available.\n");
    content += _("# Leave empty to keep all sizes from the file.\n");
    content += "\n";

    for (const auto& role : known_roles()) {
        std::string padded = role;
        padded.append(max_role_len + 1 - role.size(), ' ');
        content += padded;
        std::string sizes_value;
        auto it = guesses.find(role);
        if (it != guesses.end()) {
            auto sizes = collect_sizes_for_guess(input_dir, it->second);
            sizes_value = join_sizes(sizes);
        }
        content += "= \"";
        content += sizes_value;
        content += "\"\n";
    }
    utils::write_file_string(path, content);
}

}  // namespace ani2xcursor
