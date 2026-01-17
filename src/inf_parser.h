#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ani2xcursor {

namespace fs = std::filesystem;

// Cursor role to filename mapping
struct CursorMapping {
    std::string role;      // e.g., "pointer", "help", "working"
    std::string filename;  // e.g., "Normal.ani"
};

// Parsed INF data
struct InfData {
    std::string theme_name;           // SCHEME_NAME
    std::string cursor_dir;           // CUR_DIR (relative path in Windows)
    std::vector<CursorMapping> mappings;
    
    // Get filename for a role, returns nullopt if not found
    [[nodiscard]] std::optional<std::string> get_filename(std::string_view role) const;
};

// Parse Install.inf file
// Throws std::runtime_error on parse errors
class InfParser {
public:
    // Parse INF file from path
    [[nodiscard]] static InfData parse(const fs::path& path);
    
    // Parse INF content from string
    [[nodiscard]] static InfData parse_string(std::string_view content);

private:
    InfParser() = default;
    
    void parse_impl(std::string_view content);
    void parse_section(std::string_view section_name, std::string_view content);
    void parse_strings_section(std::string_view content);
    void parse_wreg_section(std::string_view content);
    
    // Resolve %VAR% references in a string
    [[nodiscard]] std::string resolve_vars(std::string_view input) const;
    
    // Helper to extract key=value from a line
    [[nodiscard]] static std::optional<std::pair<std::string, std::string>> 
        parse_key_value(std::string_view line);
    
    // Variables from [Strings] section
    std::map<std::string, std::string, std::less<>> variables_;
    
    // Result being built
    InfData result_;
};

} // namespace ani2xcursor
