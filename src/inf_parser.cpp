#include "inf_parser.h"
#include "utils/fs.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ani2xcursor {

// ============================================================================
// Helper Functions (anonymous namespace)
// ============================================================================

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
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    } else if (!s.empty() && s.front() == '"') {
        // Handle unterminated quote gracefully (common in some INF files)
        s = s.substr(1);
    }
    return std::string(s);
}

} // anonymous namespace

// ============================================================================
// InfResult Implementation
// ============================================================================

std::optional<std::string> InfResult::get_value(std::string_view role) const {
    for (const auto& m : mappings) {
        if (m.role == role) {
            return m.value;
        }
    }
    return std::nullopt;
}

std::string InfResult::extract_filename(std::string_view path) {
    // Find the last path separator (either \ or /)
    auto last_backslash = path.rfind('\\');
    auto last_slash = path.rfind('/');
    
    size_t last_sep = std::string_view::npos;
    if (last_backslash != std::string_view::npos && last_slash != std::string_view::npos) {
        last_sep = std::max(last_backslash, last_slash);
    } else if (last_backslash != std::string_view::npos) {
        last_sep = last_backslash;
    } else if (last_slash != std::string_view::npos) {
        last_sep = last_slash;
    }
    
    if (last_sep != std::string_view::npos) {
        return std::string(path.substr(last_sep + 1));
    }
    
    return std::string(path);
}

// ============================================================================
// RegLineParser Implementation
// ============================================================================

std::string RegLineParser::parse_field(std::string_view line, size_t& pos) {
    if (pos >= line.size()) {
        return "";
    }
    
    // Skip leading whitespace
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    
    if (pos >= line.size()) {
        return "";
    }
    
    std::string result;
    
    if (line[pos] == '"') {
        // Quoted field
        ++pos;
        while (pos < line.size()) {
            char c = line[pos];
            if (c == '"') {
                ++pos;
                // Check for escaped quote ""
                if (pos < line.size() && line[pos] == '"') {
                    result += '"';
                    ++pos;
                } else {
                    // End of quoted field
                    break;
                }
            } else {
                result += c;
                ++pos;
            }
        }
    } else {
        // Unquoted field - read until comma
        while (pos < line.size() && line[pos] != ',') {
            result += line[pos];
            ++pos;
        }
        // Trim trailing whitespace from unquoted field
        while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) {
            result.pop_back();
        }
    }
    
    // Skip past comma separator
    while (pos < line.size() && (line[pos] == ',' || std::isspace(static_cast<unsigned char>(line[pos])))) {
        if (line[pos] == ',') {
            ++pos;
            break;
        }
        ++pos;
    }
    
    return result;
}

RegEntry RegLineParser::parse(std::string_view line) {
    RegEntry entry;
    size_t pos = 0;
    
    // Parse: ROOT,"SubKey","ValueName",Flags,"Data"
    // or: ROOT,"SubKey","ValueName",,"Data"
    // or: ROOT,"SubKey",,,  (just sets default value or creates key)
    
    entry.root = parse_field(line, pos);
    if (entry.root.empty()) {
        return entry; // invalid
    }
    
    entry.subkey = parse_field(line, pos);
    entry.value_name = parse_field(line, pos);
    entry.flags = parse_field(line, pos);
    entry.data = parse_field(line, pos);
    
    entry.valid = true;
    return entry;
}

// ============================================================================
// InfParser Implementation
// ============================================================================

InfResult InfParser::parse(const fs::path& path) {
    spdlog::debug("Parsing INF file: {}", path.string());
    auto content = utils::read_file_string(path);
    return parse_string(content);
}

InfResult InfParser::parse_string(std::string_view content) {
    InfParser parser;
    parser.parse_impl(content);
    return std::move(parser.result_);
}

// ----------------------------------------------------------------------------
// Main parsing implementation
// ----------------------------------------------------------------------------

void InfParser::parse_impl(std::string_view content) {
    // Phase 1: Split content into sections
    std::string current_section;
    std::string section_content;
    
    std::string content_str{content};
    std::istringstream stream{content_str};
    std::string line;
    
    auto store_section = [&]() {
        if (!current_section.empty()) {
            sections_[to_lower(current_section)] = section_content;
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
            store_section();
            current_section = std::string(trimmed.substr(1, trimmed.size() - 2));
            spdlog::debug("INF: Found section [{}]", current_section);
            continue;
        }
        
        // Add line to current section
        section_content += line;
        section_content += '\n';
    }
    store_section();
    
    // Phase 2: Parse [Strings] first (variables needed for expansion)
    if (auto it = sections_.find("strings"); it != sections_.end()) {
        parse_strings_section(it->second);
    }
    
    // Extract theme name from variables
    if (auto it = variables_.find("scheme_name"); it != variables_.end()) {
        result_.theme_name = it->second;
    }
    
    // Phase 3: Parse [DefaultInstall] to get CopyFiles and AddReg references
    if (auto it = sections_.find("defaultinstall"); it != sections_.end()) {
        parse_default_install_section(it->second);
    }
    
    // Phase 4: Build final mappings from role_mappings_
    for (const auto& [role, value] : role_mappings_) {
        result_.mappings.push_back({role, value});
    }
    
    // Log results
    if (result_.theme_name.empty()) {
        add_warning("SCHEME_NAME not found in [Strings] section");
    }
    
    spdlog::info("INF parsed: theme='{}', {} cursor mappings, {} warnings", 
                 result_.theme_name, result_.mappings.size(), result_.warnings.size());
    
    for (const auto& w : result_.warnings) {
        spdlog::warn("INF: {}", w);
    }
}

// ----------------------------------------------------------------------------
// [Strings] section parsing
// ----------------------------------------------------------------------------

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
}

// ----------------------------------------------------------------------------
// [DefaultInstall] section parsing
// ----------------------------------------------------------------------------

void InfParser::parse_default_install_section(std::string_view content) {
    std::string content_str{content};
    std::istringstream stream{content_str};
    std::string line;
    
    while (std::getline(stream, line)) {
        auto kv = parse_key_value(trim(line));
        if (!kv) continue;
        
        auto& [key, value] = *kv;
        std::string lower_key = to_lower(key);
        
        if (lower_key == "addreg") {
            // AddReg = Section1, Section2, ...
            std::istringstream sections_stream{value};
            std::string section_name;
            while (std::getline(sections_stream, section_name, ',')) {
                auto trimmed_name = std::string(trim(section_name));
                if (!trimmed_name.empty()) {
                    parse_add_reg_section(trimmed_name);
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// AddReg section parsing
// ----------------------------------------------------------------------------

void InfParser::parse_add_reg_section(const std::string& section_name) {
    auto it = sections_.find(to_lower(section_name));
    if (it == sections_.end()) {
        add_warning("AddReg section not found: [" + section_name + "]");
        return;
    }
    
    spdlog::debug("INF: Parsing AddReg section [{}]", section_name);
    
    std::istringstream stream{it->second};
    std::string line;
    
    while (std::getline(stream, line)) {
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';') continue;
        
        RegEntry entry = RegLineParser::parse(trimmed);
        if (!entry.valid) {
            add_warning("Failed to parse reg line: " + std::string(trimmed));
            continue;
        }
        
        // Only process HKCU entries for cursor configuration
        if (!iequals(entry.root, "HKCU")) {
            continue;
        }
        
        // Check subkey to determine entry type
        std::string lower_subkey = to_lower(entry.subkey);
        
        if (lower_subkey == "control panel\\cursors\\schemes") {
            // Scheme.Reg style: comma-separated cursor paths
            process_scheme_reg_entry(entry);
        } else if (lower_subkey == "control panel\\cursors") {
            // Wreg style: individual cursor key mapping
            process_cursor_reg_entry(entry);
        }
    }
}

// ----------------------------------------------------------------------------
// Registry entry processing
// ----------------------------------------------------------------------------

void InfParser::process_cursor_reg_entry(const RegEntry& entry) {
    // Individual cursor mapping: value_name is the Windows cursor key
    // data is the path (may contain %VAR%)
    
    if (entry.value_name.empty()) {
        // Default value - often contains scheme name, can be used to set theme name
        if (!entry.data.empty() && result_.theme_name.empty()) {
            result_.theme_name = expand_vars(entry.data);
        }
        return;
    }
    
    // Map Windows cursor key to internal role
    auto role = win_key_to_role(entry.value_name);
    if (!role) {
        // Unknown cursor key - log but don't fail
        spdlog::debug("INF: Unknown cursor key '{}', skipping", entry.value_name);
        return;
    }
    
    // Expand variables in the path
    std::string expanded_path = expand_vars(entry.data);
    
    // Add mapping with high priority (Wreg overrides Scheme)
    add_mapping(*role, expanded_path, true);
    
    spdlog::debug("INF Wreg: {} ({}) -> {}", entry.value_name, *role, expanded_path);
}

void InfParser::process_scheme_reg_entry(const RegEntry& entry) {
    // Scheme entry: data is a comma-separated list of cursor paths
    // value_name is usually the scheme name
    
    if (entry.data.empty()) {
        return;
    }
    
    // Update theme name if we found it in scheme
    if (!entry.value_name.empty()) {
        std::string scheme_name = expand_vars(entry.value_name);
        if (result_.theme_name.empty()) {
            result_.theme_name = scheme_name;
        }
    }
    
    // Parse the scheme string
    parse_scheme_string(entry.data);
}

// ----------------------------------------------------------------------------
// Scheme string parsing
// ----------------------------------------------------------------------------

void InfParser::parse_scheme_string(std::string_view scheme_data) {
    // Split by comma, map to roles by position
    std::vector<std::string> paths;
    
    std::string current;
    
    for (size_t i = 0; i < scheme_data.size(); ++i) {
        char c = scheme_data[i];
        
        if (c == ',') {
            paths.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    
    // Don't forget the last item
    if (!current.empty()) {
        paths.push_back(current);
    }
    
    // Map to roles by slot position
    for (size_t i = 0; i < paths.size() && i < kSchemeSlotCount; ++i) {
        std::string path = std::string(trim(paths[i]));
        if (path.empty()) continue;
        
        // Expand variables
        std::string expanded = expand_vars(path);
        
        // Add with low priority (Wreg takes precedence)
        add_mapping(kSchemeSlots[i], expanded, false);
        
        spdlog::debug("INF Scheme[{}]: {} -> {}", i, kSchemeSlots[i], expanded);
    }
    
    if (paths.size() > kSchemeSlotCount) {
        add_warning("Scheme string has more entries (" + std::to_string(paths.size()) + 
                   ") than expected slots (" + std::to_string(kSchemeSlotCount) + ")");
    }
}

// ----------------------------------------------------------------------------
// Variable expansion
// ----------------------------------------------------------------------------

std::string InfParser::expand_vars(std::string_view input) const {
    std::string result{input};
    
    // Maximum iterations to prevent infinite loops from circular references
    constexpr int kMaxIterations = 5;
    
    for (int iter = 0; iter < kMaxIterations; ++iter) {
        bool found_var = false;
        std::string output;
        size_t i = 0;
        
        while (i < result.size()) {
            // Look for %
            size_t start = result.find('%', i);
            if (start == std::string::npos) {
                output += result.substr(i);
                break;
            }
            
            // Copy everything before %
            output += result.substr(i, start - i);
            
            // Find closing %
            size_t end = result.find('%', start + 1);
            if (end == std::string::npos) {
                // No closing %, copy rest and done
                output += result.substr(start);
                break;
            }
            
            // Extract variable name
            std::string var_name = to_lower(result.substr(start + 1, end - start - 1));
            
            // Special handling for %10% (Windows directory ID)
            // We preserve it as-is for the caller to handle
            if (!var_name.empty() &&
                std::all_of(var_name.begin(), var_name.end(),
                            [](unsigned char c) { return std::isdigit(c); })) {
                // Preserve numeric DIRID variables like %10% and %24%
                output += "%" + var_name + "%";
                i = end + 1;
                continue;
            }
            
            // Look up variable
            if (auto it = variables_.find(var_name); it != variables_.end()) {
                output += it->second;
                found_var = true;
            } else {
                // Variable not found - preserve original and warn
                output += result.substr(start, end - start + 1);
                // Only warn on first iteration to avoid duplicate warnings
                if (iter == 0) {
                    const_cast<InfParser*>(this)->add_warning(
                        "Unresolved variable: %" + var_name + "%");
                }
            }
            
            i = end + 1;
        }
        
        result = std::move(output);
        
        // If no variables were expanded, we're done
        if (!found_var) {
            break;
        }
    }
    
    return result;
}

// ----------------------------------------------------------------------------
// Helper: Windows cursor key to internal role mapping
// ----------------------------------------------------------------------------

std::optional<std::string> InfParser::win_key_to_role(std::string_view win_key) {
    std::string lower_key = to_lower(win_key);
    
    for (const auto& mapping : kWinCursorKeyTable) {
        if (to_lower(mapping.win_key) == lower_key) {
            return std::string(mapping.role);
        }
    }
    
    return std::nullopt;
}

// ----------------------------------------------------------------------------
// Helper: Add mapping with priority handling
// ----------------------------------------------------------------------------

void InfParser::add_mapping(const std::string& role, const std::string& value, bool high_priority) {
    auto it = role_mappings_.find(role);
    
    if (it == role_mappings_.end()) {
        // New mapping
        role_mappings_[role] = value;
        role_from_wreg_[role] = high_priority;
    } else if (high_priority && !role_from_wreg_[role]) {
        // High priority (Wreg) overrides low priority (Scheme)
        role_mappings_[role] = value;
        role_from_wreg_[role] = true;
        spdlog::debug("INF: Wreg overrides Scheme for role '{}'", role);
    }
    // Otherwise, keep existing mapping (first Wreg wins, or first Scheme if no Wreg)
}

// ----------------------------------------------------------------------------
// Helper: Add warning
// ----------------------------------------------------------------------------

void InfParser::add_warning(const std::string& msg) {
    result_.warnings.push_back(msg);
}

// ----------------------------------------------------------------------------
// Helper: Parse key=value line
// ----------------------------------------------------------------------------

std::optional<std::pair<std::string, std::string>> 
InfParser::parse_key_value(std::string_view line) {
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
