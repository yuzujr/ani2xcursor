#include "inf_parser.h"
#include "utils/fs.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace ani2xcursor {

std::optional<std::string> InfData::get_filename(std::string_view role) const {
    for (const auto& m : mappings) {
        if (m.role == role) {
            return m.filename;
        }
    }
    return std::nullopt;
}

InfData InfParser::parse(const fs::path& path) {
    spdlog::debug("Parsing INF file: {}", path.string());
    auto content = utils::read_file_string(path);
    return parse_string(content);
}

InfData InfParser::parse_string(std::string_view content) {
    InfParser parser;
    parser.parse_impl(content);
    return std::move(parser.result_);
}

namespace {

// Trim whitespace from both ends
std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

// Case-insensitive string comparison
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char c1, char c2) {
        return std::tolower(static_cast<unsigned char>(c1)) == 
               std::tolower(static_cast<unsigned char>(c2));
    });
}

// Convert string to lowercase
std::string to_lower(std::string_view s) {
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Remove quotes from a string value
std::string unquote(std::string_view s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    return std::string(s);
}

} // anonymous namespace

void InfParser::parse_impl(std::string_view content) {
    // Split into sections
    // Sections start with [SectionName]
    
    std::string current_section;
    std::string section_content;
    
    std::string content_str{content};
    std::istringstream stream{content_str};
    std::string line;
    
    auto process_section = [&]() {
        if (!current_section.empty()) {
            parse_section(current_section, section_content);
        }
        section_content.clear();
    };
    
    while (std::getline(stream, line)) {
        // Remove BOM if present
        if (line.size() >= 3 && 
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
        }
        
        auto trimmed = trim(line);
        
        // Skip empty lines and comments
        if (trimmed.empty() || trimmed[0] == ';') {
            continue;
        }
        
        // Check for section header
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            process_section();
            current_section = std::string(trimmed.substr(1, trimmed.size() - 2));
            spdlog::debug("INF: Found section [{}]", current_section);
            continue;
        }
        
        // Add line to current section
        section_content += line;
        section_content += '\n';
    }
    
    // Process last section
    process_section();
    
    // Validate required data
    if (result_.theme_name.empty()) {
        throw std::runtime_error("INF parsing failed: SCHEME_NAME not found in [Strings] section");
    }
    
    spdlog::info("INF parsed: theme='{}', {} cursor mappings", 
                 result_.theme_name, result_.mappings.size());
}

void InfParser::parse_section(std::string_view section_name, std::string_view content) {
    if (iequals(section_name, "Strings")) {
        parse_strings_section(content);
    } else if (iequals(section_name, "Wreg")) {
        parse_wreg_section(content);
    }
    // Other sections are ignored
}

void InfParser::parse_strings_section(std::string_view content) {
    std::string content_str{content};
    std::istringstream stream{content_str};
    std::string line;
    
    while (std::getline(stream, line)) {
        auto kv = parse_key_value(trim(line));
        if (!kv) continue;
        
        auto& [key, value] = *kv;
        std::string lower_key = to_lower(key);
        
        variables_[lower_key] = unquote(value);
        spdlog::debug("INF [Strings]: {} = {}", lower_key, variables_[lower_key]);
    }
    
    // Extract theme name and cursor dir
    if (auto it = variables_.find("scheme_name"); it != variables_.end()) {
        result_.theme_name = it->second;
    }
    if (auto it = variables_.find("cur_dir"); it != variables_.end()) {
        result_.cursor_dir = it->second;
    }
    
    // Build role->filename mappings from string variables
    // These are the standard Windows cursor role names
    static const std::vector<std::string> roles = {
        "pointer", "help", "working", "busy", "precision", "text",
        "hand", "unavailable", "vert", "horz", "dgn1", "dgn2",
        "move", "alternate", "link", "person", "pin"
    };
    
    for (const auto& role : roles) {
        if (auto it = variables_.find(role); it != variables_.end()) {
            result_.mappings.push_back({role, it->second});
            spdlog::debug("INF mapping: {} -> {}", role, it->second);
        }
    }
}

void InfParser::parse_wreg_section([[maybe_unused]] std::string_view content) {
    // [Wreg] section contains registry entries that map cursor roles to files
    // We can extract additional mappings from here if needed
    // Format: HKCU,"Control Panel\Cursors",<key>,<flags>,<value>
    
    // For now, the [Strings] section already provides what we need
    // This is here for completeness and potential future use
    
    spdlog::debug("INF: Processed [Wreg] section (using [Strings] mappings)");
}

std::string InfParser::resolve_vars(std::string_view input) const {
    std::string result(input);
    
    // Replace %VAR% patterns with their values
    std::regex var_pattern(R"(%([^%]+)%)");
    std::string output;
    
    std::string input_str(input);
    std::sregex_iterator it(input_str.begin(), input_str.end(), var_pattern);
    std::sregex_iterator end;
    
    size_t last_pos = 0;
    for (; it != end; ++it) {
        const auto& match = *it;
        output += input_str.substr(last_pos, match.position() - last_pos);
        
        std::string var_name = to_lower(match[1].str());
        if (auto vit = variables_.find(var_name); vit != variables_.end()) {
            output += vit->second;
        } else {
            // Keep original if variable not found
            output += match.str();
            spdlog::warn("INF: Unresolved variable %{}%", var_name);
        }
        
        last_pos = match.position() + match.length();
    }
    
    output += input_str.substr(last_pos);
    return output;
}

std::optional<std::pair<std::string, std::string>> 
InfParser::parse_key_value(std::string_view line) {
    // Handle continuation lines (ending with \)
    // For simplicity, we don't support multi-line values
    
    auto eq_pos = line.find('=');
    if (eq_pos == std::string_view::npos) {
        return std::nullopt;
    }
    
    auto key = trim(line.substr(0, eq_pos));
    auto value = trim(line.substr(eq_pos + 1));
    
    if (key.empty()) {
        return std::nullopt;
    }
    
    return std::make_pair(std::string(key), std::string(value));
}

} // namespace ani2xcursor
